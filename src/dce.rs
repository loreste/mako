//! Dead code elimination — remove unreachable functions, structs, and enums
//! from `program.items` before codegen. Walks the call graph starting from
//! roots (main / test functions) and keeps only reachable items.
//!
//! Also detects unused imports and emits warnings.

use std::collections::HashSet;
use std::path::Path;

use crate::ast::*;

/// Remove unreachable items from the program. `roots` are the entry-point
/// function names (e.g. `["main"]` for binaries, `["TestAdd", "TestMul"]`
/// for test builds).
pub fn eliminate(program: &Program, roots: &[String]) -> Program {
    let mut reachable_fns: HashSet<String> = HashSet::new();
    let mut reachable_types: HashSet<String> = HashSet::new();
    let mut queue: Vec<String> = roots.to_vec();

    // Index: function name → body items
    let fn_map: std::collections::HashMap<String, &FnDef> = program
        .items
        .iter()
        .filter_map(|item| match item {
            Item::Fn(f) => Some((f.name.clone(), f)),
            _ => None,
        })
        .collect();

    // Collect all extern function names — always reachable (FFI boundary).
    for item in &program.items {
        if let Item::ExternC(e) = item {
            reachable_fns.insert(e.name.clone());
        }
    }

    // BFS over the call graph.
    while let Some(name) = queue.pop() {
        if !reachable_fns.insert(name.clone()) {
            continue; // already visited
        }
        if let Some(f) = fn_map.get(&name) {
            // Collect called functions and referenced types from this fn body.
            collect_fn_refs(f, &mut queue, &mut reachable_types);
        }
    }

    // Check for unused imports (if source file is provided).
    // This runs after BFS so reachable_fns is complete.

    // Collect method names (bare names like "describe", "push") so we can
    // keep all `Type_method` implementations when the method is called.
    let method_names: HashSet<&str> = reachable_fns
        .iter()
        .filter(|n| !n.contains('_') && !n.contains("__"))
        .map(|n| n.as_str())
        .collect();

    // Keep reachable items, all structs/enums referenced by reachable fns,
    // and all consts (cheap, may be referenced by name).
    let items = program
        .items
        .iter()
        .filter(|item| match item {
            Item::Fn(f) => {
                // Keep generic templates (needed if any instantiation is reachable).
                if !f.type_params.is_empty() {
                    return reachable_fns
                        .iter()
                        .any(|r| r.starts_with(&format!("{}__", f.name)));
                }
                if reachable_fns.contains(&f.name) {
                    return true;
                }
                // Keep method implementations: if `describe` is called,
                // keep `Dog_describe`, `Cat_describe`, etc.
                for method in &method_names {
                    if f.name.ends_with(&format!("_{method}")) {
                        return true;
                    }
                }
                false
            }
            Item::Struct(s) => {
                // Keep if referenced by type or if any method is reachable.
                reachable_types.contains(&s.name)
                    || reachable_fns
                        .iter()
                        .any(|r| r.starts_with(&format!("{}_", s.name)))
            }
            Item::Enum(e) => {
                reachable_types.contains(&e.name)
                    || reachable_fns
                        .iter()
                        .any(|r| r.starts_with(&format!("{}_", e.name)))
            }
            Item::Interface(i) => {
                reachable_types.contains(&i.name)
            }
            // On-blocks: keep if the target type is reachable.
            Item::On(on) => {
                reachable_types.contains(&on.ty)
                    || reachable_fns.iter().any(|r| r.starts_with(&format!("{}_", on.ty)))
            }
            // Keep consts, extern, actors, package/import declarations.
            Item::Const(_)
            | Item::ExternC(_)
            | Item::Actor(_)
            | Item::Package { .. }
            | Item::Import { .. } => true,
        })
        .cloned()
        .collect();

    Program { items }
}

/// Check for unused imports in the source file. Reads the original source,
/// finds `pull` / `import` lines, resolves each to a file, extracts its
/// top-level `fn` names, and warns if none are in the reachable set.
pub fn warn_unused_imports(source_file: &Path, program: &Program, roots: &[String]) {
    // Build the reachable set (same BFS as eliminate).
    let mut reachable_fns: HashSet<String> = HashSet::new();
    let mut reachable_types: HashSet<String> = HashSet::new();
    let mut queue: Vec<String> = roots.to_vec();
    let fn_map: std::collections::HashMap<String, &FnDef> = program
        .items
        .iter()
        .filter_map(|item| match item {
            Item::Fn(f) => Some((f.name.clone(), f)),
            _ => None,
        })
        .collect();
    for item in &program.items {
        if let Item::ExternC(e) = item {
            reachable_fns.insert(e.name.clone());
        }
    }
    while let Some(name) = queue.pop() {
        if !reachable_fns.insert(name.clone()) {
            continue;
        }
        if let Some(f) = fn_map.get(&name) {
            collect_fn_refs(f, &mut queue, &mut reachable_types);
        }
    }

    // Read the original source and find import lines.
    let Ok(src) = std::fs::read_to_string(source_file) else {
        return;
    };
    let base = source_file.parent().unwrap_or(Path::new("."));
    for line in src.lines() {
        let trimmed = line.trim();
        // Match: pull "path" or pull . "path" or import "path"
        let path = if let Some(rest) = trimmed.strip_prefix("pull") {
            extract_import_path(rest)
        } else if let Some(rest) = trimmed.strip_prefix("import") {
            extract_import_path(rest)
        } else {
            None
        };
        let Some(import_path) = path else { continue };
        // Resolve the imported file.
        let resolved = if import_path.starts_with("./") || import_path.starts_with("../") {
            base.join(&import_path)
        } else {
            // stdlib pull — skip (we don't have the stdlib path here).
            continue;
        };
        let resolved = if resolved.extension().is_none() {
            resolved.with_extension("mko")
        } else {
            resolved
        };
        if !resolved.exists() {
            continue;
        }
        // Extract function names from the imported file.
        let Ok(imp_src) = std::fs::read_to_string(&resolved) else {
            continue;
        };
        let mut has_reachable = false;
        for imp_line in imp_src.lines() {
            let imp_trimmed = imp_line.trim();
            if let Some(rest) = imp_trimmed.strip_prefix("fn ") {
                if let Some(name) = rest.split('(').next() {
                    let name = name.trim();
                    if reachable_fns.contains(name) {
                        has_reachable = true;
                        break;
                    }
                }
            }
        }
        if !has_reachable {
            let display_path = resolved
                .strip_prefix(base)
                .unwrap_or(&resolved)
                .display();
            eprintln!(
                "warning: unused import `{import_path}` — no reachable functions from {display_path}"
            );
            eprintln!(
                "  hint: remove the import or use a function from it"
            );
        }
    }
}

fn extract_import_path(rest: &str) -> Option<String> {
    // pull "path" / pull . "path" / import "path"
    let rest = rest.trim();
    let rest = rest.strip_prefix('.').unwrap_or(rest).trim();
    if let Some(s) = rest.strip_prefix('"') {
        if let Some(end) = s.find('"') {
            return Some(s[..end].to_string());
        }
    }
    None
}

/// Warn about unreachable code — statements after return/break/continue.
pub fn warn_unreachable_code(program: &Program) {
    for item in &program.items {
        if let Item::Fn(f) = item {
            check_block_unreachable(&f.body, &f.name);
        }
    }
}

fn check_block_unreachable(block: &Block, fn_name: &str) {
    let stmts = &block.stmts;
    for (i, stmt) in stmts.iter().enumerate() {
        // Check if this statement always diverges AND there are more statements after.
        let diverges = matches!(
            stmt,
            Stmt::Return(_) | Stmt::Break(_) | Stmt::Continue(_)
        );
        if diverges && i + 1 < stmts.len() {
            // Skip if the next statement is just a closing brace artifact.
            let next = &stmts[i + 1];
            if !matches!(next, Stmt::Expr(Expr::Int(_))) {
                eprintln!(
                    "warning: unreachable code after {} in `{fn_name}`",
                    match stmt {
                        Stmt::Return(_) => "return",
                        Stmt::Break(_) => "break",
                        Stmt::Continue(_) => "continue",
                        _ => "diverging statement",
                    }
                );
                break; // Only warn once per block.
            }
        }
        // Recurse into sub-blocks.
        match stmt {
            Stmt::If { then_block, else_block, .. } => {
                check_block_unreachable(then_block, fn_name);
                if let Some(eb) = else_block {
                    check_block_unreachable(eb, fn_name);
                }
            }
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::CFor { body, .. }
            | Stmt::Crew { body, .. } | Stmt::Arena { body, .. } | Stmt::Unsafe { body }
            | Stmt::Defer { body } => {
                check_block_unreachable(body, fn_name);
            }
            _ => {}
        }
    }
}

/// Collect function names called and type names referenced from a function def.
fn collect_fn_refs(f: &FnDef, queue: &mut Vec<String>, types: &mut HashSet<String>) {
    // Params and return type.
    for p in &f.params {
        collect_type_refs(&p.ty, types);
    }
    if let Some(ret) = &f.ret {
        collect_type_refs(ret, types);
    }

    // Body statements.
    for stmt in &f.body.stmts {
        collect_stmt_refs(stmt, queue, types);
    }
}

fn collect_stmt_refs(stmt: &Stmt, queue: &mut Vec<String>, types: &mut HashSet<String>) {
    match stmt {
        Stmt::Let { ty, init, .. } => {
            if let Some(t) = ty {
                collect_type_refs(t, types);
            }
            collect_expr_refs(init, queue, types);
        }
        Stmt::LetMulti { init, .. } => {
            collect_expr_refs(init, queue, types);
        }
        Stmt::LetCommaOk { base, index, .. } => {
            collect_expr_refs(base, queue, types);
            collect_expr_refs(index, queue, types);
        }
        Stmt::Assign { value, .. } => {
            collect_expr_refs(value, queue, types);
        }
        Stmt::IndexAssign { base, index, value } => {
            collect_expr_refs(base, queue, types);
            collect_expr_refs(index, queue, types);
            collect_expr_refs(value, queue, types);
        }
        Stmt::FieldAssign { base, value, .. } => {
            collect_expr_refs(base, queue, types);
            collect_expr_refs(value, queue, types);
        }
        Stmt::Expr(e) => {
            collect_expr_refs(e, queue, types);
        }
        Stmt::Return(Some(e)) => {
            collect_expr_refs(e, queue, types);
        }
        Stmt::Return(None) => {}
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
        } => {
            if let Some(init) = init {
                collect_stmt_refs(init, queue, types);
            }
            collect_expr_refs(cond, queue, types);
            collect_block_refs(then_block, queue, types);
            if let Some(eb) = else_block {
                collect_block_refs(eb, queue, types);
            }
        }
        Stmt::While { cond, body, label: _, .. } => {
            collect_expr_refs(cond, queue, types);
            collect_block_refs(body, queue, types);
        }
        Stmt::For { iter, body, label: _, .. } => {
            collect_expr_refs(iter, queue, types);
            collect_block_refs(body, queue, types);
        }
        Stmt::CFor {
            init,
            cond,
            post,
            body,
            ..
        } => {
            collect_stmt_refs(init, queue, types);
            collect_expr_refs(cond, queue, types);
            collect_stmt_refs(post, queue, types);
            collect_block_refs(body, queue, types);
        }
        Stmt::Break { .. } | Stmt::Continue { .. } => {}
        Stmt::Defer { body } => {
            collect_block_refs(body, queue, types);
        }
        Stmt::Crew { body, .. } => {
            collect_block_refs(body, queue, types);
        }
        Stmt::Arena { body, .. } => {
            collect_block_refs(body, queue, types);
        }
        Stmt::Select { arms, default_arm, .. } => {
            for (_chan, body) in arms {
                collect_block_refs(body, queue, types);
            }
            if let Some(d) = default_arm {
                collect_block_refs(d, queue, types);
            }
        }
        Stmt::Unsafe { body } => {
            collect_block_refs(body, queue, types);
        }
    }
}

fn collect_block_refs(block: &Block, queue: &mut Vec<String>, types: &mut HashSet<String>) {
    for stmt in &block.stmts {
        collect_stmt_refs(stmt, queue, types);
    }
}

fn collect_expr_refs(expr: &Expr, queue: &mut Vec<String>, types: &mut HashSet<String>) {
    match expr {
        Expr::Call { callee, args } => {
            // Direct call: f(args)
            if let Expr::Ident(name) = callee.as_ref() {
                queue.push(name.clone());
            }
            collect_expr_refs(callee, queue, types);
            for a in args {
                collect_expr_refs(a, queue, types);
            }
        }
        Expr::Method {
            receiver,
            method,
            args,
        } => {
            collect_expr_refs(receiver, queue, types);
            // Method call: receiver.method(args) → Type_method in codegen.
            // Without type info we can't resolve `Type`, so mark the method
            // name itself plus common patterns as potentially reachable.
            queue.push(method.clone());
            // Also mark any function that contains this method name as a suffix.
            // This is conservative but necessary without type info.
            for a in args {
                collect_expr_refs(a, queue, types);
            }
        }
        Expr::Ident(_name) => {
            // Idents are usually local variables. Function references (first-class
            // fn values) are rare — accepting false negatives here to avoid
            // marking every local variable as a reachable function.
        }
        Expr::Binary { left, right, .. } => {
            collect_expr_refs(left, queue, types);
            collect_expr_refs(right, queue, types);
        }
        Expr::Unary { expr, .. } => {
            collect_expr_refs(expr, queue, types);
        }
        Expr::Index { base, index } => {
            collect_expr_refs(base, queue, types);
            collect_expr_refs(index, queue, types);
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            collect_expr_refs(base, queue, types);
            if let Some(l) = low {
                collect_expr_refs(l, queue, types);
            }
            if let Some(h) = high {
                collect_expr_refs(h, queue, types);
            }
            if let Some(m) = max {
                collect_expr_refs(m, queue, types);
            }
        }
        Expr::Field { base, .. } => {
            collect_expr_refs(base, queue, types);
        }
        Expr::StructLit { name, fields, update, .. } => {
            types.insert(name.clone());
            for (_, e) in fields {
                collect_expr_refs(e, queue, types);
            }
            if let Some(u) = update {
                collect_expr_refs(u, queue, types);
            }
        }
        Expr::StructLitPos { name, values, .. } => {
            types.insert(name.clone());
            for v in values {
                collect_expr_refs(v, queue, types);
            }
        }
        Expr::Array(elems) | Expr::Tuple(elems) => {
            for e in elems {
                collect_expr_refs(e, queue, types);
            }
        }
        Expr::StringInterp(parts) => {
            collect_interp_refs(parts, queue, types);
        }
        Expr::Convert { ty, args } => {
            collect_type_refs(ty, types);
            for a in args {
                collect_expr_refs(a, queue, types);
            }
        }
        Expr::Make { ty, len, cap } => {
            collect_type_refs(ty, types);
            if let Some(l) = len {
                collect_expr_refs(l, queue, types);
            }
            if let Some(c) = cap {
                collect_expr_refs(c, queue, types);
            }
        }
        Expr::ChanOpen { elem, cap } => {
            collect_type_refs(elem, types);
            collect_expr_refs(cap, queue, types);
        }
        Expr::Lambda { body, params, .. } => {
            // Lambda params are just names (Vec<String>), no types.
            let _ = params;
            collect_expr_refs(body, queue, types);
        }
        Expr::Match { scrutinee, arms } => {
            collect_expr_refs(scrutinee, queue, types);
            for arm in arms {
                collect_pattern_refs(&arm.pattern, queue, types);
                if let Some(g) = &arm.guard {
                    collect_expr_refs(g, queue, types);
                }
                collect_expr_refs(&arm.body, queue, types);
            }
        }
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            collect_expr_refs(cond, queue, types);
            collect_block_refs(then_block, queue, types);
            collect_block_refs(else_block, queue, types);
        }
        Expr::Try(e) => {
            collect_expr_refs(e, queue, types);
        }
        Expr::Block(block) => {
            collect_block_refs(block, queue, types);
        }
        Expr::Kick { expr, .. } => {
            collect_expr_refs(expr, queue, types);
        }
        Expr::Join(e) => {
            collect_expr_refs(e, queue, types);
        }
        Expr::Fan { collection, mapper } => {
            collect_expr_refs(collection, queue, types);
            collect_expr_refs(mapper, queue, types);
        }
        // Literals have no references.
        Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {}
    }
}

fn collect_interp_refs(parts: &[InterpPart], queue: &mut Vec<String>, types: &mut HashSet<String>) {
    for part in parts {
        match part {
            InterpPart::Lit(_) => {}
            InterpPart::Expr(e, _fmt) => collect_expr_refs(e, queue, types),
        }
    }
}

fn collect_pattern_refs(
    pattern: &Pattern,
    queue: &mut Vec<String>,
    types: &mut HashSet<String>,
) {
    match pattern {
        Pattern::Variant { name, bindings, .. } => {
            queue.push(name.clone());
            for f in bindings {
                collect_pattern_refs(f, queue, types);
            }
        }
        Pattern::Struct { name, fields, .. } => {
            types.insert(name.clone());
            for (_, p) in fields {
                collect_pattern_refs(p, queue, types);
            }
        }
        Pattern::Or(alts) => {
            for a in alts {
                collect_pattern_refs(a, queue, types);
            }
        }
        Pattern::Tuple(elems) => {
            for e in elems {
                collect_pattern_refs(e, queue, types);
            }
        }
        Pattern::Wildcard | Pattern::Ident(_) | Pattern::Literal(_) => {}
    }
}

fn collect_type_refs(ty: &TypeExpr, types: &mut HashSet<String>) {
    match ty {
        TypeExpr::Named(name) => {
            types.insert(name.clone());
        }
        TypeExpr::Generic(name, args) => {
            types.insert(name.clone());
            for a in args {
                collect_type_refs(a, types);
            }
        }
        TypeExpr::Map(k, v) => {
            collect_type_refs(k, types);
            collect_type_refs(v, types);
        }
        TypeExpr::Array(elem) => {
            collect_type_refs(elem, types);
        }
        TypeExpr::Fn(params, ret) => {
            for p in params {
                collect_type_refs(p, types);
            }
            collect_type_refs(ret, types);
        }
        TypeExpr::Tuple(elems) => {
            for e in elems {
                collect_type_refs(e, types);
            }
        }
    }
}
