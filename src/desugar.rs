//! Desugar high-level syntax into core AST (actors, derive, `on` methods, etc.).

use crate::ast::*;

/// Expand `actor` / `#[derive(json)]` / `on Type { … }` into helper functions.
pub fn desugar(mut program: Program, source_path: Option<&str>) -> Program {
    let mut extras: Vec<Item> = Vec::new();
    let mut kept: Vec<Item> = Vec::new();
    let struct_fields: std::collections::HashMap<String, Vec<(String, TypeExpr)>> = program
        .items
        .iter()
        .filter_map(|item| match item {
            Item::Struct(s) => Some((
                s.name.clone(),
                s.fields
                    .iter()
                    .map(|(name, ty, _)| (name.clone(), ty.clone()))
                    .collect(),
            )),
            _ => None,
        })
        .collect();

    for item in program.items.drain(..) {
        match item {
            Item::Actor(actor) => {
                // Defer actor expansion to after merge (desugar_if_let_all)
                // so types from imported/merged files are available.
                kept.push(Item::Actor(actor));
                continue;
            }
            Item::On(on) => {
                extras.extend(expand_on(on));
            }
            Item::Struct(s) => {
                if s.derives.iter().any(|d| d == "json") {
                    extras.extend(expand_json_derive(&s, &struct_fields));
                }
                kept.push(Item::Struct(s));
            }
            other => kept.push(other),
        }
    }

    // Remove cfg-excluded placeholder items.
    kept.retain(|item| !matches!(item, Item::Fn(f) if f.name == "__cfg_excluded__"));
    kept.extend(extras);
    let mut prog = Program { items: kept };
    for item in &mut prog.items {
        match item {
            Item::Fn(f) => {
                let base_dir = source_path
                    .and_then(|p| std::path::Path::new(p).parent())
                    .map(|p| p.to_path_buf());
                desugar_if_let_block(&mut f.body);
                resolve_embed_block(&mut f.body, base_dir.as_deref());
            }
            Item::On(on) => {
                for m in &mut on.methods {
                    desugar_if_let_block(&mut m.body);
                    let base_dir = source_path
                        .and_then(|p| std::path::Path::new(p).parent())
                        .map(|p| p.to_path_buf());
                    resolve_embed_block(&mut m.body, base_dir.as_deref());
                }
            }
            _ => {}
        }
    }
    prog
}

fn resolve_embed_block(block: &mut Block, base: Option<&std::path::Path>) {
    for s in &mut block.stmts {
        resolve_embed_stmt(s, base);
    }
}
fn resolve_embed_stmt(stmt: &mut Stmt, base: Option<&std::path::Path>) {
    match stmt {
        Stmt::Let { init, .. } | Stmt::Assign { value: init, .. } => resolve_embed_expr(init, base),
        Stmt::Expr(e) | Stmt::Return(Some(e)) => resolve_embed_expr(e, base),
        Stmt::If {
            cond,
            then_block,
            else_block,
            ..
        } => {
            resolve_embed_expr(cond, base);
            resolve_embed_block(then_block, base);
            if let Some(eb) = else_block {
                resolve_embed_block(eb, base);
            }
        }
        Stmt::While { body, .. } | Stmt::Defer { body } | Stmt::Unsafe { body } => {
            resolve_embed_block(body, base)
        }
        Stmt::For { body, .. } => resolve_embed_block(body, base),
        _ => {}
    }
}
fn resolve_embed_expr(expr: &mut Expr, base: Option<&std::path::Path>) {
    if let Expr::Call { callee, args } = expr {
        if let Expr::Ident(name) = callee.as_ref() {
            if name == "embed" && args.len() == 1 {
                if let Expr::String(path) = &args[0] {
                    let full = base.map(|b| b.join(path)).unwrap_or_else(|| path.into());
                    match std::fs::read_to_string(&full) {
                        Ok(c) => {
                            *expr = Expr::String(c);
                            return;
                        }
                        Err(e) => {
                            eprintln!("embed: {}: {e}", full.display());
                            std::process::exit(1);
                        }
                    }
                }
            }
            if name == "embed_bytes" && args.len() == 1 {
                if let Expr::String(path) = &args[0] {
                    let full = base.map(|b| b.join(path)).unwrap_or_else(|| path.into());
                    match std::fs::read(&full) {
                        Ok(b) => {
                            *expr =
                                Expr::Array(b.into_iter().map(|v| Expr::Int(v as i64)).collect());
                            return;
                        }
                        Err(e) => {
                            eprintln!("embed_bytes: {}: {e}", full.display());
                            std::process::exit(1);
                        }
                    }
                }
            }
        }
        resolve_embed_expr(callee, base);
        for a in args {
            resolve_embed_expr(a, base);
        }
    }
}

fn desugar_if_let_block(block: &mut Block) {
    let stmts = std::mem::take(&mut block.stmts);
    block.stmts = stmts.into_iter().map(|s| desugar_if_let_stmt(s)).collect();
}

fn desugar_if_let_stmt(stmt: Stmt) -> Stmt {
    match stmt {
        Stmt::IfLet {
            pattern,
            scrutinee,
            then_block,
            else_block,
        } => {
            let mut then_block = then_block;
            let mut else_block = else_block;
            desugar_if_let_block(&mut then_block);
            if let Some(eb) = &mut else_block {
                desugar_if_let_block(eb);
            }
            // Desugar: match scrutinee { Pattern => then, _ => else }
            let mut arms = vec![MatchArm {
                pattern,
                guard: None,
                body: Expr::Block(then_block),
            }];
            arms.push(MatchArm {
                pattern: Pattern::Wildcard,
                guard: None,
                body: Expr::Block(else_block.unwrap_or(Block {
                    stmts: vec![],
                    source_lines: Box::default(),
                })),
            });
            Stmt::Expr(Expr::Match {
                scrutinee: Box::new(scrutinee),
                arms,
            })
        }
        Stmt::If {
            init,
            cond,
            mut then_block,
            mut else_block,
        } => {
            desugar_if_let_block(&mut then_block);
            if let Some(eb) = &mut else_block {
                desugar_if_let_block(eb);
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            }
        }
        Stmt::While {
            label,
            cond,
            mut body,
        } => {
            desugar_if_let_block(&mut body);
            Stmt::While { label, cond, body }
        }
        Stmt::For {
            label,
            binders,
            is_range,
            iter,
            mut body,
        } => {
            desugar_if_let_block(&mut body);
            Stmt::For {
                label,
                binders,
                is_range,
                iter,
                body,
            }
        }
        other => other,
    }
}

/// `on Point { fn distance(self) -> int { … } }` → `fn Point_distance(self: Point, …)`
/// `on Counter : Adder { fn add… }` → `fn Adder_Counter_add(self: Counter, …)`
fn expand_on(on: OnDef) -> Vec<Item> {
    let mut items = Vec::new();
    for mut method in on.methods {
        // Fill bare `self` / `__self` type with the receiver type.
        for p in &mut method.params {
            if p.name == "self" {
                if matches!(&p.ty, TypeExpr::Named(n) if n == "__self") {
                    p.ty = TypeExpr::Named(on.ty.clone());
                }
            }
        }
        // If first param is self without proper type still Named(__self), fix it.
        if let Some(p) = method.params.first_mut() {
            if p.name == "self" {
                if matches!(&p.ty, TypeExpr::Named(n) if n == "__self" || n.is_empty()) {
                    p.ty = TypeExpr::Named(on.ty.clone());
                }
            }
        }
        method.name = if let Some(ref iface) = on.iface {
            format!("{iface}_{}_{}", on.ty, method.name)
        } else {
            format!("{}_{}", on.ty, method.name)
        };
        method.exported = on.exported;
        // Methods from `on` are concrete (no free type params unless user wrote them).
        items.push(Item::Fn(method));
    }
    items
}

/// `Person_to_json(...)` plus `Person_<field>_from_json(j)` extractors.
fn expand_json_derive(
    s: &StructDef,
    struct_fields: &std::collections::HashMap<String, Vec<(String, TypeExpr)>>,
) -> Vec<Item> {
    let mut items = Vec::new();

    let fname = format!("{}_to_json", s.name);
    let params: Vec<Param> = s
        .fields
        .iter()
        .map(|(n, ty, _)| Param {
            name: n.clone(),
            ty: ty.clone(),
            mutable: false,
            variadic: false,
        })
        .collect();

    let mut pieces = s
        .fields
        .iter()
        .filter_map(|(field, ty, _)| json_field_expr(field, ty, struct_fields));
    let json_expr = pieces
        .next()
        .map(|first| pieces.fold(first, |acc, next| call("json_merge", vec![acc, next])))
        .unwrap_or_else(|| Expr::String("{}".into()));

    items.push(Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name: fname,
        type_params: Vec::new(),
        params,
        ret: Some(TypeExpr::Named("string".into())),
        body: Block {
            stmts: vec![Stmt::Return(Some(json_expr))],
            source_lines: Box::default(),
        },
        exported: s.exported,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    for (fname_field, ty, _) in &s.fields {
        let (callee, ret_ty) = match ty {
            TypeExpr::Named(n) if n == "string" => ("json_get_string", "string"),
            TypeExpr::Named(n) if n == "int" || n == "int64" => ("json_get_int", "int"),
            TypeExpr::Named(n) if n == "float" || n == "float64" => ("json_get_float", "float"),
            TypeExpr::Named(n) if n == "bool" => ("json_get_bool", "int"),
            _ => continue,
        };
        items.push(Item::Fn(FnDef {
            type_bounds: std::collections::HashMap::new(),
            name: format!("{}_{}_from_json", s.name, fname_field),
            type_params: Vec::new(),
            params: vec![Param {
                name: "j".into(),
                ty: TypeExpr::Named("string".into()),
                mutable: false,
                variadic: false,
            }],
            ret: Some(TypeExpr::Named(ret_ty.into())),
            body: Block {
                stmts: vec![Stmt::Return(Some(Expr::Call {
                    callee: Box::new(Expr::Ident(callee.into())),
                    args: vec![Expr::Ident("j".into()), Expr::String(fname_field.clone())],
                }))],
                source_lines: Box::default(),
            },
            exported: s.exported,
            is_const: false,
            is_live: false,
            stability: crate::ast::ApiStability::Unspecified,
            contracts: vec![],
            source_file: None,
        }));
    }

    items
}

fn call(name: &str, args: Vec<Expr>) -> Expr {
    Expr::Call {
        callee: Box::new(Expr::Ident(name.into())),
        args,
    }
}

fn json_field_expr(
    field: &str,
    ty: &TypeExpr,
    struct_fields: &std::collections::HashMap<String, Vec<(String, TypeExpr)>>,
) -> Option<Expr> {
    match ty {
        TypeExpr::Named(n) if n == "string" => Some(call(
            "json_object",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        TypeExpr::Named(n) if n == "int" || n == "int64" => Some(call(
            "json_i",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        TypeExpr::Named(n) if n == "float" || n == "float64" => Some(call(
            "json_f",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        TypeExpr::Named(n) if n == "bool" => Some(call(
            "json_b",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        TypeExpr::Named(n) if struct_fields.contains_key(n) => {
            let nested_args = struct_fields[n]
                .iter()
                .map(|(nested_field, _)| Expr::Field {
                    base: Box::new(Expr::Ident(field.into())),
                    field: nested_field.clone(),
                })
                .collect();
            Some(call(
                "json_nest",
                vec![
                    Expr::String(field.into()),
                    Expr::Call {
                        callee: Box::new(Expr::Ident(format!("{n}_to_json"))),
                        args: nested_args,
                    },
                ],
            ))
        }
        _ => None,
    }
}

fn rewrite_self_fields(expr: &mut Expr, state_name: &str) {
    match expr {
        Expr::Field { base, field: _ } => {
            if matches!(base.as_ref(), Expr::Ident(s) if s == "self") {
                *base = Box::new(Expr::Ident(state_name.into()));
            } else {
                rewrite_self_fields(base, state_name);
            }
        }
        Expr::Binary { left, right, .. } => {
            rewrite_self_fields(left, state_name);
            rewrite_self_fields(right, state_name);
        }
        Expr::Unary { expr: e, .. }
        | Expr::Try(e)
        | Expr::Join(e)
        | Expr::Kick { expr: e, .. }
        | Expr::ChanOpen { cap: e, .. } => {
            rewrite_self_fields(e, state_name);
        }
        Expr::Call { callee, args } => {
            rewrite_self_fields(callee, state_name);
            for a in args {
                rewrite_self_fields(a, state_name);
            }
        }
        Expr::Method { receiver, args, .. } => {
            rewrite_self_fields(receiver, state_name);
            for a in args {
                rewrite_self_fields(a, state_name);
            }
        }
        Expr::Index { base, index } => {
            rewrite_self_fields(base, state_name);
            rewrite_self_fields(index, state_name);
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            rewrite_self_fields(base, state_name);
            for bound in [low, high, max].into_iter().flatten() {
                rewrite_self_fields(bound, state_name);
            }
        }
        Expr::Array(xs)
        | Expr::Tuple(xs)
        | Expr::StructLitPos { values: xs, .. }
        | Expr::Convert { args: xs, .. } => {
            for x in xs {
                rewrite_self_fields(x, state_name);
            }
        }
        Expr::StructLit { fields, update, .. } => {
            for (_, e) in fields {
                rewrite_self_fields(e, state_name);
            }
            if let Some(u) = update {
                rewrite_self_fields(u, state_name);
            }
        }
        Expr::Match { scrutinee, arms } => {
            rewrite_self_fields(scrutinee, state_name);
            for a in arms {
                if let Some(guard) = &mut a.guard {
                    rewrite_self_fields(guard, state_name);
                }
                rewrite_self_fields(&mut a.body, state_name);
            }
        }
        Expr::Block(b) => rewrite_self_block(b, state_name),
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            rewrite_self_fields(cond, state_name);
            rewrite_self_block(then_block, state_name);
            rewrite_self_block(else_block, state_name);
        }
        Expr::Lambda { body, .. } => rewrite_self_fields(body, state_name),
        Expr::StringInterp(parts) => {
            for part in parts {
                if let InterpPart::Expr(expr, _) = part {
                    rewrite_self_fields(expr, state_name);
                }
            }
        }
        Expr::Make { len, cap, .. } => {
            for value in [len, cap].into_iter().flatten() {
                rewrite_self_fields(value, state_name);
            }
        }
        Expr::Fan { collection, mapper } => {
            rewrite_self_fields(collection, state_name);
            rewrite_self_fields(mapper, state_name);
        }
        Expr::Ident(name) => {
            if name == "self" {
                *name = state_name.into();
            }
        }
        Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {}
    }
}

fn rewrite_self_block(b: &mut Block, state_name: &str) {
    for s in &mut b.stmts {
        rewrite_self_stmt(s, state_name);
    }
}

// Keep this exhaustive: every receive-body expression must use the same state
// binding, including statements nested inside typed envelope dispatch arms.
fn rewrite_self_stmt(s: &mut Stmt, state_name: &str) {
    match s {
        Stmt::Assign { name, value } => {
            if name == "self" {
                *name = state_name.into();
            }
            rewrite_self_fields(value, state_name);
        }
        Stmt::Let { init, .. } | Stmt::LetMulti { init, .. } => {
            rewrite_self_fields(init, state_name);
        }
        Stmt::Expr(e) | Stmt::Return(Some(e)) => rewrite_self_fields(e, state_name),
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
            ..
        } => {
            if let Some(init) = init {
                rewrite_self_stmt(init, state_name);
            }
            rewrite_self_fields(cond, state_name);
            rewrite_self_block(then_block, state_name);
            if let Some(eb) = else_block {
                rewrite_self_block(eb, state_name);
            }
        }
        Stmt::While { cond, body, .. } => {
            rewrite_self_fields(cond, state_name);
            rewrite_self_block(body, state_name);
        }
        Stmt::IfLet {
            scrutinee,
            then_block,
            else_block,
            ..
        } => {
            rewrite_self_fields(scrutinee, state_name);
            rewrite_self_block(then_block, state_name);
            if let Some(eb) = else_block {
                rewrite_self_block(eb, state_name);
            }
        }
        Stmt::LetCommaOk { base, index, .. } => {
            rewrite_self_fields(base, state_name);
            rewrite_self_fields(index, state_name);
        }
        Stmt::IndexAssign { base, index, value } => {
            rewrite_self_fields(base, state_name);
            rewrite_self_fields(index, state_name);
            rewrite_self_fields(value, state_name);
        }
        Stmt::For { iter, body, .. } => {
            rewrite_self_fields(iter, state_name);
            rewrite_self_block(body, state_name);
        }
        Stmt::CFor {
            init,
            cond,
            post,
            body,
            ..
        } => {
            rewrite_self_stmt(init, state_name);
            rewrite_self_fields(cond, state_name);
            rewrite_self_stmt(post, state_name);
            rewrite_self_block(body, state_name);
        }
        Stmt::Defer { body }
        | Stmt::Crew { body, .. }
        | Stmt::Arena { body, .. }
        | Stmt::Unsafe { body } => rewrite_self_block(body, state_name),
        Stmt::Select {
            timeout_ms,
            arms,
            default_arm,
        } => {
            rewrite_self_fields(timeout_ms, state_name);
            for (_, body) in arms {
                rewrite_self_block(body, state_name);
            }
            if let Some(body) = default_arm {
                rewrite_self_block(body, state_name);
            }
        }
        Stmt::FieldAssign { base, value, .. } => {
            // `self.n = …` → FieldAssign { base: Ident("self"), field: "n" }
            if matches!(base, Expr::Ident(s) if s == "self") {
                *base = Expr::Ident(state_name.into());
            } else {
                rewrite_self_fields(base, state_name);
            }
            rewrite_self_fields(value, state_name);
        }
        Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => {}
    }
}

#[cfg(test)]
mod actor_self_tests {
    use super::*;
    use crate::{lexer::Lexer, parser::Parser, types::TypeChecker};

    #[test]
    fn actor_self_state_also_resolves_in_single_file() {
        // The multi-file CLI fixture must also work with its units combined:
        // indexed state writes were skipped regardless of the number of files.
        let source = format!(
            "{}\n{}",
            include_str!("../examples/testing/actor_multifile/state.mko"),
            include_str!("../examples/testing/actor_multifile/actor_test.mko")
                .replace("pull \"state.mko\"", "")
        );
        let parsed = Parser::new(Lexer::new(&source).tokenize().unwrap())
            .parse()
            .unwrap();
        let mut program = desugar(parsed, None);
        desugar_if_let_all(&mut program);
        TypeChecker::new().check(&program).unwrap();
    }

    #[test]
    fn actor_multifile_check_paths_expand_deferred_actors() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("examples/testing/actor_multifile/actor_test.mko");
        assert!(crate::tooling::check_file(&path).is_ok());
        let (ok, report) = crate::tooling::check_file_json_report(&path);
        assert!(ok, "{report}");
    }

    #[test]
    fn actor_self_rewrite_preserves_mutable_capture_rejection() {
        let source = include_str!("../examples/bad/actor_state_kick_capture.mko");
        let parsed = Parser::new(Lexer::new(source).tokenize().unwrap())
            .parse()
            .unwrap();
        let mut program = desugar(parsed, None);
        desugar_if_let_all(&mut program);
        let error = TypeChecker::new().check(&program).unwrap_err();
        assert!(error.to_string().contains("mutable capture"), "{error}");
    }

    fn expand_source(source: &str) -> Program {
        let parsed = Parser::new(Lexer::new(source).tokenize().unwrap())
            .parse()
            .unwrap();
        let mut program = desugar(parsed, None);
        desugar_if_let_all(&mut program);
        program
    }

    fn fn_named<'a>(program: &'a Program, name: &str) -> &'a FnDef {
        program
            .items
            .iter()
            .find_map(|item| match item {
                Item::Fn(f) if f.name == name => Some(f),
                _ => None,
            })
            .unwrap_or_else(|| panic!("missing fn {name}"))
    }

    fn struct_named<'a>(program: &'a Program, name: &str) -> &'a StructDef {
        program
            .items
            .iter()
            .find_map(|item| match item {
                Item::Struct(s) if s.name == name => Some(s),
                _ => None,
            })
            .unwrap_or_else(|| panic!("missing struct {name}"))
    }

    fn expr_calls(expr: &Expr, name: &str) -> bool {
        match expr {
            Expr::Call { callee, args } => {
                matches!(callee.as_ref(), Expr::Ident(n) if n == name)
                    || expr_calls(callee, name)
                    || args.iter().any(|a| expr_calls(a, name))
            }
            Expr::Method { receiver, args, .. } => {
                expr_calls(receiver, name) || args.iter().any(|a| expr_calls(a, name))
            }
            Expr::Binary { left, right, .. } => expr_calls(left, name) || expr_calls(right, name),
            Expr::Unary { expr: e, .. }
            | Expr::Try(e)
            | Expr::Join(e)
            | Expr::Kick { expr: e, .. }
            | Expr::ChanOpen { cap: e, .. }
            | Expr::Field { base: e, .. } => expr_calls(e, name),
            Expr::Index { base, index } => expr_calls(base, name) || expr_calls(index, name),
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                expr_calls(base, name)
                    || [low, high, max]
                        .into_iter()
                        .flatten()
                        .any(|b| expr_calls(b, name))
            }
            Expr::Array(xs)
            | Expr::Tuple(xs)
            | Expr::StructLitPos { values: xs, .. }
            | Expr::Convert { args: xs, .. } => xs.iter().any(|x| expr_calls(x, name)),
            Expr::StructLit { fields, update, .. } => {
                fields.iter().any(|(_, e)| expr_calls(e, name))
                    || update.as_ref().is_some_and(|u| expr_calls(u, name))
            }
            Expr::IfExpr {
                cond,
                then_block,
                else_block,
            } => {
                expr_calls(cond, name)
                    || stmts_call(&then_block.stmts, name)
                    || stmts_call(&else_block.stmts, name)
            }
            Expr::Match { scrutinee, arms } => {
                expr_calls(scrutinee, name)
                    || arms.iter().any(|a| {
                        a.guard.as_ref().is_some_and(|g| expr_calls(g, name))
                            || expr_calls(&a.body, name)
                    })
            }
            Expr::Block(b) => stmts_call(&b.stmts, name),
            Expr::Lambda { body, .. } => expr_calls(body, name),
            Expr::Make { len, cap, .. } => [len, cap]
                .into_iter()
                .flatten()
                .any(|v| expr_calls(v, name)),
            Expr::Fan { collection, mapper } => {
                expr_calls(collection, name) || expr_calls(mapper, name)
            }
            Expr::StringInterp(parts) => parts.iter().any(|p| match p {
                InterpPart::Expr(e, _) => expr_calls(e, name),
                InterpPart::Lit(_) => false,
            }),
            Expr::Ident(_) | Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {
                false
            }
        }
    }

    fn stmt_calls(stmt: &Stmt, name: &str) -> bool {
        match stmt {
            Stmt::Let { init, .. }
            | Stmt::LetMulti { init, .. }
            | Stmt::Assign { value: init, .. } => expr_calls(init, name),
            Stmt::Expr(e) | Stmt::Return(Some(e)) => expr_calls(e, name),
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                init.as_ref().is_some_and(|s| stmt_calls(s, name))
                    || expr_calls(cond, name)
                    || stmts_call(&then_block.stmts, name)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_call(&b.stmts, name))
            }
            Stmt::While { cond, body, .. } => {
                expr_calls(cond, name) || stmts_call(&body.stmts, name)
            }
            Stmt::For { iter, body, .. } => expr_calls(iter, name) || stmts_call(&body.stmts, name),
            Stmt::CFor {
                init,
                cond,
                post,
                body,
                ..
            } => {
                stmt_calls(init, name)
                    || expr_calls(cond, name)
                    || stmt_calls(post, name)
                    || stmts_call(&body.stmts, name)
            }
            Stmt::IfLet {
                scrutinee,
                then_block,
                else_block,
                ..
            } => {
                expr_calls(scrutinee, name)
                    || stmts_call(&then_block.stmts, name)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_call(&b.stmts, name))
            }
            Stmt::LetCommaOk { base, index, .. } => {
                expr_calls(base, name) || expr_calls(index, name)
            }
            Stmt::IndexAssign { base, index, value } => {
                expr_calls(base, name) || expr_calls(index, name) || expr_calls(value, name)
            }
            Stmt::FieldAssign { base, value, .. } => {
                expr_calls(base, name) || expr_calls(value, name)
            }
            Stmt::Defer { body }
            | Stmt::Crew { body, .. }
            | Stmt::Arena { body, .. }
            | Stmt::Unsafe { body } => stmts_call(&body.stmts, name),
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                expr_calls(timeout_ms, name)
                    || arms.iter().any(|(_, b)| stmts_call(&b.stmts, name))
                    || default_arm
                        .as_ref()
                        .is_some_and(|b| stmts_call(&b.stmts, name))
            }
            Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => false,
        }
    }

    fn stmts_call(stmts: &[Stmt], name: &str) -> bool {
        stmts.iter().any(|s| stmt_calls(s, name))
    }

    fn stmts_have_continue_label(stmts: &[Stmt], label: &str) -> bool {
        stmts.iter().any(|s| match s {
            Stmt::Continue(Some(l)) if l == label => true,
            Stmt::If {
                then_block,
                else_block,
                ..
            } => {
                stmts_have_continue_label(&then_block.stmts, label)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_have_continue_label(&b.stmts, label))
            }
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::CFor { body, .. } => {
                stmts_have_continue_label(&body.stmts, label)
            }
            _ => false,
        })
    }

    const ENVELOPE_ACTOR: &str = r#"
actor Boxer {
    receive Dump(tag: string, blob: []int) { let _ = len(tag) + len(blob) }
    receive Tick(delta: int) { let _ = delta }
    receive Find(xs: []int) {
        for x in xs {
            if x == 1 { return }
        }
    }
    receive Bye { let _ = 0 }
}
fn main() { let _ = Boxer_spawn() }
"#;

    #[test]
    fn actor_envelope_struct_stores_tag_first() {
        let program = expand_source(ENVELOPE_ACTOR);
        let env = struct_named(&program, "Boxer_Dump_Env");
        assert_eq!(env.fields[0].0, "__actor_tag");
        assert!(env.fields.iter().any(|(n, _, _)| n == "tag"));
        assert!(env.fields.iter().any(|(n, _, _)| n == "blob"));
    }

    #[test]
    fn actor_envelope_ctor_boxes_pointer_not_pack() {
        let program = expand_source(ENVELOPE_ACTOR);
        let dump = fn_named(&program, "Boxer_Dump");
        assert!(
            stmts_call(&dump.body.stmts, "actor_box_payload"),
            "envelope ctor must box the full pointer"
        );
        assert!(
            !stmts_call(&dump.body.stmts, "actor_pack"),
            "envelope ctor must not 48-bit-pack the pointer"
        );
        let tick = fn_named(&program, "Boxer_Tick");
        assert!(
            stmts_call(&tick.body.stmts, "actor_pack"),
            "scalar ctor still packs"
        );
        assert!(!stmts_call(&tick.body.stmts, "actor_box_payload"));
    }

    #[test]
    fn actor_loop_drain_unboxes_instead_of_shallow_free() {
        let program = expand_source(ENVELOPE_ACTOR);
        let loop_fn = fn_named(&program, "Boxer_loop");
        assert!(
            stmts_call(&loop_fn.body.stmts, "actor_unbox_payload"),
            "drain/handle must unbox typed envelopes"
        );
        assert!(
            !stmts_call(&loop_fn.body.stmts, "actor_free_payload"),
            "drain must not shallow-free envelope shells"
        );
    }

    #[test]
    fn actor_receive_return_continues_actor_loop() {
        let program = expand_source(ENVELOPE_ACTOR);
        let loop_fn = fn_named(&program, "Boxer_loop");
        assert!(
            stmts_have_continue_label(&loop_fn.body.stmts, "__actor_loop"),
            "return in Find must become continue __actor_loop"
        );
    }

    const PORT_ACTOR: &str = r#"
actor Gate {
    n: int = 0
    receive Work(done: chan[int]) on exec {
        self.n = self.n + 1
        let _ = done.send(self.n)
    }
    receive Ping(reply: chan[int]) on control {
        let _ = reply.send(self.n)
    }
    receive Bye on control { let _ = 0 }
}
fn main() { let _ = Gate_spawn() }
"#;

    fn stmts_have_select(stmts: &[Stmt]) -> bool {
        stmts.iter().any(|s| match s {
            Stmt::Select { .. } => true,
            Stmt::If {
                then_block,
                else_block,
                ..
            } => {
                stmts_have_select(&then_block.stmts)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_have_select(&b.stmts))
            }
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::CFor { body, .. } => {
                stmts_have_select(&body.stmts)
            }
            Stmt::IfLet {
                then_block,
                else_block,
                ..
            } => {
                stmts_have_select(&then_block.stmts)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_have_select(&b.stmts))
            }
            _ => false,
        })
    }

    fn stmts_have_method(stmts: &[Stmt], method: &str) -> bool {
        fn expr_has(expr: &Expr, method: &str) -> bool {
            match expr {
                Expr::Method {
                    receiver,
                    method: m,
                    args,
                } => {
                    m == method
                        || expr_has(receiver, method)
                        || args.iter().any(|a| expr_has(a, method))
                }
                Expr::Call { callee, args } => {
                    expr_has(callee, method) || args.iter().any(|a| expr_has(a, method))
                }
                Expr::Binary { left, right, .. } => {
                    expr_has(left, method) || expr_has(right, method)
                }
                Expr::Unary { expr: e, .. }
                | Expr::Try(e)
                | Expr::Join(e)
                | Expr::Kick { expr: e, .. }
                | Expr::ChanOpen { cap: e, .. }
                | Expr::Field { base: e, .. } => expr_has(e, method),
                Expr::IfExpr {
                    cond,
                    then_block,
                    else_block,
                } => {
                    expr_has(cond, method)
                        || stmts_have_method(&then_block.stmts, method)
                        || stmts_have_method(&else_block.stmts, method)
                }
                Expr::Match { scrutinee, arms } => {
                    expr_has(scrutinee, method) || arms.iter().any(|a| expr_has(&a.body, method))
                }
                Expr::Block(b) => stmts_have_method(&b.stmts, method),
                _ => false,
            }
        }
        stmts.iter().any(|s| match s {
            Stmt::Let { init, .. }
            | Stmt::LetMulti { init, .. }
            | Stmt::Assign { value: init, .. } => expr_has(init, method),
            Stmt::Expr(e) | Stmt::Return(Some(e)) => expr_has(e, method),
            Stmt::If {
                then_block,
                else_block,
                ..
            } => {
                stmts_have_method(&then_block.stmts, method)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_have_method(&b.stmts, method))
            }
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::CFor { body, .. } => {
                stmts_have_method(&body.stmts, method)
            }
            Stmt::IfLet {
                scrutinee,
                then_block,
                else_block,
                ..
            } => {
                expr_has(scrutinee, method)
                    || stmts_have_method(&then_block.stmts, method)
                    || else_block
                        .as_ref()
                        .is_some_and(|b| stmts_have_method(&b.stmts, method))
            }
            Stmt::Select {
                arms, default_arm, ..
            } => {
                arms.iter()
                    .any(|(_, b)| stmts_have_method(&b.stmts, method))
                    || default_arm
                        .as_ref()
                        .is_some_and(|b| stmts_have_method(&b.stmts, method))
            }
            _ => false,
        })
    }

    #[test]
    fn actor_receive_on_port_parses() {
        let parsed = Parser::new(Lexer::new(PORT_ACTOR).tokenize().unwrap())
            .parse()
            .unwrap();
        let actor = parsed.items.iter().find_map(|item| match item {
            Item::Actor(a) if a.name == "Gate" => Some(a),
            _ => None,
        });
        let actor = actor.expect("Gate actor");
        assert_eq!(actor.receives[0].port.as_deref(), Some("exec"));
        assert_eq!(actor.receives[1].port.as_deref(), Some("control"));
        assert_eq!(actor.receives[2].port.as_deref(), Some("control"));
    }

    #[test]
    fn actor_ports_expand_handle_and_priority() {
        let program = expand_source(PORT_ACTOR);
        let handle = struct_named(&program, "Gate");
        assert_eq!(handle.fields[0].0, "control", "control port is field 0");
        assert_eq!(handle.fields[1].0, "exec");
        let spawn = fn_named(&program, "Gate_spawn");
        assert!(matches!(
            spawn.ret,
            Some(TypeExpr::Named(ref n)) if n == "Gate"
        ));
        let _ = fn_named(&program, "Gate_control_send");
        let _ = fn_named(&program, "Gate_exec_send");
        let _ = fn_named(&program, "Gate_Work_send");
        let _ = fn_named(&program, "Gate_Ping_send");
        let _ = fn_named(&program, "Gate_Bye_send");
        let loop_fn = fn_named(&program, "Gate_loop");
        assert!(
            stmts_call(&loop_fn.body.stmts, "actor_try_recv"),
            "multi-port loop try_recv's each mailbox"
        );
        assert!(
            stmts_have_select(&loop_fn.body.stmts),
            "multi-port loop parks on select when every port is empty"
        );
        TypeChecker::new().check(&program).unwrap();
    }
}

fn rewrite_return_in_expr(expr: &mut Expr) {
    match expr {
        Expr::Block(b) => rewrite_return_to_continue(&mut b.stmts),
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            rewrite_return_in_expr(cond);
            rewrite_return_to_continue(&mut then_block.stmts);
            rewrite_return_to_continue(&mut else_block.stmts);
        }
        Expr::Match { scrutinee, arms } => {
            rewrite_return_in_expr(scrutinee);
            for a in arms {
                if let Some(guard) = &mut a.guard {
                    rewrite_return_in_expr(guard);
                }
                rewrite_return_in_expr(&mut a.body);
            }
        }
        // A closure has its own return target, outside the receive handler.
        Expr::Lambda { .. } => {},
        Expr::Call { callee, args } => {
            rewrite_return_in_expr(callee);
            for a in args {
                rewrite_return_in_expr(a);
            }
        }
        Expr::Method { receiver, args, .. } => {
            rewrite_return_in_expr(receiver);
            for a in args {
                rewrite_return_in_expr(a);
            }
        }
        Expr::Binary { left, right, .. } => {
            rewrite_return_in_expr(left);
            rewrite_return_in_expr(right);
        }
        Expr::Unary { expr: e, .. }
        | Expr::Try(e)
        | Expr::Join(e)
        | Expr::Kick { expr: e, .. }
        | Expr::ChanOpen { cap: e, .. } => rewrite_return_in_expr(e),
        Expr::Field { base, .. } => rewrite_return_in_expr(base),
        Expr::Index { base, index } => {
            rewrite_return_in_expr(base);
            rewrite_return_in_expr(index);
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            rewrite_return_in_expr(base);
            for bound in [low, high, max].into_iter().flatten() {
                rewrite_return_in_expr(bound);
            }
        }
        Expr::Array(xs)
        | Expr::Tuple(xs)
        | Expr::StructLitPos { values: xs, .. }
        | Expr::Convert { args: xs, .. } => {
            for x in xs {
                rewrite_return_in_expr(x);
            }
        }
        Expr::StructLit { fields, update, .. } => {
            for (_, e) in fields {
                rewrite_return_in_expr(e);
            }
            if let Some(u) = update {
                rewrite_return_in_expr(u);
            }
        }
        Expr::StringInterp(parts) => {
            for part in parts {
                if let InterpPart::Expr(expr, _) = part {
                    rewrite_return_in_expr(expr);
                }
            }
        }
        Expr::Make { len, cap, .. } => {
            for value in [len, cap].into_iter().flatten() {
                rewrite_return_in_expr(value);
            }
        }
        Expr::Fan { collection, mapper } => {
            rewrite_return_in_expr(collection);
            rewrite_return_in_expr(mapper);
        }
        Expr::Ident(_) | Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {}
    }
}

/// Rewrite `return` in actor receive arms to `continue __actor_loop` (next message).
fn rewrite_return_to_continue(stmts: &mut Vec<Stmt>) {
    let mut new_stmts = Vec::with_capacity(stmts.len());
    for s in stmts.drain(..) {
        match s {
            Stmt::Return(None) => {
                new_stmts.push(Stmt::Continue(Some("__actor_loop".into())));
            }
            Stmt::Return(Some(e)) => {
                new_stmts.push(Stmt::Expr(e));
                new_stmts.push(Stmt::Continue(Some("__actor_loop".into())));
            }
            Stmt::If {
                cond,
                mut then_block,
                else_block,
                init,
            } => {
                rewrite_return_to_continue(&mut then_block.stmts);
                let else_block = else_block.map(|mut eb| {
                    rewrite_return_to_continue(&mut eb.stmts);
                    eb
                });
                new_stmts.push(Stmt::If {
                    cond,
                    then_block,
                    else_block,
                    init,
                });
            }
            Stmt::While {
                cond,
                mut body,
                label,
            } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::While { cond, body, label });
            }
            Stmt::For {
                label,
                binders,
                is_range,
                iter,
                mut body,
            } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::For {
                    label,
                    binders,
                    is_range,
                    iter,
                    body,
                });
            }
            Stmt::CFor {
                label,
                init,
                cond,
                post,
                mut body,
            } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::CFor {
                    label,
                    init,
                    cond,
                    post,
                    body,
                });
            }
            Stmt::IfLet {
                pattern,
                scrutinee,
                mut then_block,
                else_block,
            } => {
                rewrite_return_to_continue(&mut then_block.stmts);
                let else_block = else_block.map(|mut eb| {
                    rewrite_return_to_continue(&mut eb.stmts);
                    eb
                });
                new_stmts.push(Stmt::IfLet {
                    pattern,
                    scrutinee,
                    then_block,
                    else_block,
                });
            }
            Stmt::Defer { mut body } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::Defer { body });
            }
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                let arms = arms
                    .into_iter()
                    .map(|(n, mut b)| {
                        rewrite_return_to_continue(&mut b.stmts);
                        (n, b)
                    })
                    .collect();
                let default_arm = default_arm.map(|mut b| {
                    rewrite_return_to_continue(&mut b.stmts);
                    b
                });
                new_stmts.push(Stmt::Select {
                    timeout_ms,
                    arms,
                    default_arm,
                });
            }
            Stmt::Unsafe { mut body } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::Unsafe { body });
            }
            Stmt::Crew {
                name,
                policy,
                mut body,
            } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::Crew { name, policy, body });
            }
            Stmt::Arena { name, mut body } => {
                rewrite_return_to_continue(&mut body.stmts);
                new_stmts.push(Stmt::Arena { name, body });
            }
            Stmt::Expr(mut e) => {
                rewrite_return_in_expr(&mut e);
                new_stmts.push(Stmt::Expr(e));
            }
            other => new_stmts.push(other),
        }
    }
    *stmts = new_stmts;
}

fn arm_needs_envelope(arm: &ReceiveArm) -> bool {
    arm.params.len() > 1
        || (arm.params.len() == 1
            && !matches!(
                arm.params[0].1,
                TypeExpr::Named(ref n) if n == "int" || n == "int64" || n == "bool"
            ))
}

fn actor_scalar_decode(ty: &TypeExpr) -> Expr {
    let payload = Expr::Ident("__pl".into());
    if matches!(ty, TypeExpr::Named(n) if n == "bool") {
        Expr::Binary {
            op: BinOp::Ne,
            left: Box::new(payload),
            right: Box::new(Expr::Int(0)),
        }
    } else {
        payload
    }
}

fn actor_pair_inline(arm: &ReceiveArm) -> bool {
    arm.params.len() == 2
        && arm
            .params
            .iter()
            .all(|(_, ty)| matches!(ty, TypeExpr::Named(n) if n == "int" || n == "int64"))
}

fn actor_binary(op: BinOp, left: Expr, right: Expr) -> Expr {
    Expr::Binary {
        op,
        left: Box::new(left),
        right: Box::new(right),
    }
}

// Two signed 24-bit integers fit the existing 48-bit inline payload. Values
// outside that range use the normal envelope, preserving the full int range.
fn actor_message_ctor_body(arm: &ReceiveArm, tag: i64, fallback: Expr) -> Vec<Stmt> {
    if arm.params.len() == 1 && matches!(&arm.params[0].1, TypeExpr::Named(n) if n == "bool") {
        return vec![
            Stmt::If {
                init: None,
                cond: Expr::Ident(arm.params[0].0.clone()),
                then_block: Block {
                    stmts: vec![Stmt::Return(Some(actor_batch_call(
                        "actor_pack",
                        vec![Expr::Int(tag), Expr::Int(1)],
                    )))],
                    source_lines: Box::default(),
                },
                else_block: None,
            },
            Stmt::Return(Some(actor_batch_call(
                "actor_pack",
                vec![Expr::Int(tag), Expr::Int(0)],
            ))),
        ];
    }
    if !actor_pair_inline(arm) {
        return vec![Stmt::Return(Some(fallback))];
    }
    let a = Expr::Ident(arm.params[0].0.clone());
    let b = Expr::Ident(arm.params[1].0.clone());
    let fits = |x: Expr| {
        actor_binary(
            BinOp::And,
            actor_binary(BinOp::Ge, x.clone(), Expr::Int(-8388608)),
            actor_binary(BinOp::Le, x, Expr::Int(8388607)),
        )
    };
    let packed = actor_batch_call(
        "actor_pack",
        vec![
            Expr::Int(tag),
            actor_binary(
                BinOp::Add,
                actor_binary(BinOp::Mul, a.clone(), Expr::Int(16777216)),
                actor_binary(BinOp::BitAnd, b.clone(), Expr::Int(16777215)),
            ),
        ],
    );
    vec![
        Stmt::If {
            init: None,
            cond: actor_binary(BinOp::And, fits(a), fits(b)),
            then_block: Block {
                stmts: vec![Stmt::Return(Some(packed))],
                source_lines: Box::default(),
            },
            else_block: None,
        },
        Stmt::Return(Some(fallback)),
    ]
}

fn actor_pair_dispatch(arm: &ReceiveArm, body: &Block, fallback: Vec<Stmt>) -> Vec<Stmt> {
    if !actor_pair_inline(arm) {
        return fallback;
    }
    let payload = Expr::Ident("__pl".into());
    let low = actor_binary(BinOp::BitAnd, payload.clone(), Expr::Int(16777215));
    let values = [
        actor_binary(
            BinOp::Div,
            actor_binary(BinOp::Sub, payload, low.clone()),
            Expr::Int(16777216),
        ),
        actor_binary(
            BinOp::Sub,
            actor_binary(BinOp::BitXor, low, Expr::Int(8388608)),
            Expr::Int(8388608),
        ),
    ];
    let mut stmts: Vec<Stmt> = arm
        .params
        .iter()
        .zip(values)
        .map(|((name, ty), init)| Stmt::Let {
            name: name.clone(),
            mutable: false,
            ownership: Ownership::None,
            ty: Some(ty.clone()),
            init,
        })
        .collect();
    stmts.extend(body.stmts.clone());
    if arm.message == "Bye" || arm.message == "Stop" {
        stmts.push(Stmt::Assign {
            name: "__run".into(),
            value: Expr::Int(0),
        });
    }
    vec![Stmt::If {
        init: None,
        cond: actor_binary(BinOp::Lt, Expr::Ident("__m".into()), Expr::Int(0)),
        then_block: Block {
            stmts,
            source_lines: Box::default(),
        },
        else_block: Some(Block {
            stmts: fallback,
            source_lines: Box::default(),
        }),
    }]
}

const ACTOR_TAG_FIELD: &str = "__actor_tag";
const CTOR_TAG: i64 = 0x7FFF;

fn envelope_struct_fields(params: &[(String, TypeExpr)]) -> Vec<(String, TypeExpr, Option<Expr>)> {
    let mut fields = vec![(ACTOR_TAG_FIELD.into(), TypeExpr::Named("int".into()), None)];
    fields.extend(params.iter().map(|(n, ty)| (n.clone(), ty.clone(), None)));
    fields
}

fn envelope_lit(env_name: String, tag: i64, params: &[(String, TypeExpr)]) -> Expr {
    let mut lit_fields: Vec<(String, Expr)> = vec![(ACTOR_TAG_FIELD.into(), Expr::Int(tag))];
    lit_fields.extend(
        params
            .iter()
            .map(|(n, _)| (n.clone(), Expr::Ident(n.clone()))),
    );
    Expr::StructLit {
        name: env_name,
        fields: lit_fields,
        update: None,
    }
}

fn box_payload(expr: Expr) -> Expr {
    Expr::Call {
        callee: Box::new(Expr::Ident("actor_box_payload".into())),
        args: vec![expr],
    }
}

fn arm_port(arm: &ReceiveArm) -> String {
    arm.port.clone().unwrap_or_else(|| "__mbox".into())
}

fn mailbox_drain_stmts(envelopes: &[(i64, String)]) -> Vec<Stmt> {
    let mut stmts = vec![close_mailbox("__mbox")];
    stmts.extend(mailbox_drain_named("__mbox", envelopes));
    stmts
}

fn close_mailbox(name: &str) -> Stmt {
    Stmt::Expr(Expr::Call {
        callee: Box::new(Expr::Ident("actor_stop".into())),
        args: vec![Expr::Ident(name.into())],
    })
}

fn actor_send_body(name: &str, mailbox: Expr, message: Expr, nonblocking: bool) -> Vec<Stmt> {
    vec![
        Stmt::Let {
            name: "__sent".into(), mutable: false, ownership: Ownership::None, ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident(if nonblocking { "actor_try_send" } else { "actor_send" }.into())),
                args: vec![mailbox, message.clone()],
            },
        },
        Stmt::If {
            init: None,
            cond: Expr::Binary { op: BinOp::Eq, left: Box::new(Expr::Ident("__sent".into())),
                right: Box::new(if nonblocking { Expr::Int(0) } else { Expr::Bool(false) }) },
            then_block: Block { stmts: vec![Stmt::Expr(Expr::Call {
                callee: Box::new(Expr::Ident(format!("{name}_drop_message"))), args: vec![message],
            })], source_lines: Box::default() },
            else_block: None,
        },
        Stmt::Return(Some(Expr::Ident("__sent".into()))),
    ]
}

fn actor_drop_message(name: &str, envelopes: &[(i64, String)]) -> Item {
    let mut body = Vec::new();
    if !envelopes.is_empty() {
        let mut drops = vec![Stmt::Let {
            name: "__drop_tag".into(), mutable: false, ownership: Ownership::None, ty: None,
            init: Expr::Call { callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                args: vec![Expr::Ident("__msg".into())] },
        }];
        for (tag, env) in envelopes {
            drops.push(Stmt::If {
                init: None,
                cond: Expr::Binary { op: BinOp::Eq, left: Box::new(Expr::Ident("__drop_tag".into())), right: Box::new(Expr::Int(*tag)) },
                then_block: Block { stmts: vec![Stmt::Let {
                    name: "__dropped_env".into(), mutable: false, ownership: Ownership::None,
                    ty: Some(TypeExpr::Named(env.clone())),
                    init: Expr::Call { callee: Box::new(Expr::Ident("actor_unbox_payload".into())), args: vec![Expr::Ident("__msg".into())] },
                }], source_lines: Box::default() }, else_block: None,
            });
        }
        body.push(Stmt::If {
            init: None,
            cond: Expr::Binary { op: BinOp::Gt, left: Box::new(Expr::Ident("__msg".into())), right: Box::new(Expr::Int(0)) },
            then_block: Block { stmts: drops, source_lines: Box::default() }, else_block: None,
        });
    }
    body.push(Stmt::Return(None));
    actor_fn(format!("{name}_drop_message"), vec![Param { name: "__msg".into(), ty: TypeExpr::Named("int".into()), mutable: false, variadic: false }], None, body)
}

fn mailbox_drain_named(mbox: &str, envelopes: &[(i64, String)]) -> Vec<Stmt> {
    if envelopes.is_empty() {
        return Vec::new();
    }
    // Unique temps per mailbox so sequential multi-port drains do not rebind.
    let dm = format!("__dm_{mbox}");
    let dtag = format!("__dtag_{mbox}");
    let dpl = format!("__dpl_{mbox}");
    let denv = format!("__denv_{mbox}");
    let mut drain_stmts = vec![
        Stmt::Let {
            name: dm.clone(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_recv".into())),
                args: vec![Expr::Ident(mbox.into())],
            },
        },
        Stmt::If {
            init: None,
            cond: Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(Expr::Ident(dm.clone())),
                right: Box::new(Expr::Int(0)),
            },
            then_block: Block {
                stmts: vec![Stmt::Break(None)],
                source_lines: Box::default(),
            },
            else_block: None,
        },
        Stmt::Let {
            name: dtag.clone(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                args: vec![Expr::Ident(dm.clone())],
            },
        },
        Stmt::Let {
            name: dpl.clone(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_payload".into())),
                args: vec![Expr::Ident(dm.clone())],
            },
        },
    ];
    for (tag, env_name) in envelopes {
        drain_stmts.push(Stmt::If {
            init: None,
            cond: actor_binary(BinOp::And,
                actor_binary(BinOp::Gt, Expr::Ident(dm.clone()), Expr::Int(0)),
                actor_binary(BinOp::Eq, Expr::Ident(dtag.clone()), Expr::Int(*tag))),
            then_block: Block {
                stmts: vec![Stmt::Let {
                    name: denv.clone(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(TypeExpr::Named(env_name.clone())),
                    init: Expr::Call {
                        callee: Box::new(Expr::Ident("actor_unbox_payload".into())),
                        args: vec![Expr::Ident(dpl.clone())],
                    },
                }],
                source_lines: Box::default(),
            },
            else_block: None,
        });
    }
    vec![Stmt::While {
        label: None,
        cond: Expr::Binary {
            op: BinOp::Gt,
            left: Box::new(Expr::Call {
                callee: Box::new(Expr::Ident("actor_len".into())),
                args: vec![Expr::Ident(mbox.into())],
            }),
            right: Box::new(Expr::Int(0)),
        },
        body: Block {
            stmts: drain_stmts,
            source_lines: Box::default(),
        },
    }]
}

fn port_envelopes(
    actor: &ActorDef,
    name: &str,
    port: &str,
    first_port: &str,
    has_ctor: bool,
    ctor_env_name: &str,
) -> Vec<(i64, String)> {
    let mut envs = Vec::new();
    if has_ctor && port == first_port {
        envs.push((CTOR_TAG, ctor_env_name.to_string()));
    }
    for (i, arm) in actor.receives.iter().enumerate() {
        if arm_port(arm) == port && arm_needs_envelope(arm) {
            envs.push(((i + 1) as i64, format!("{name}_{}_Env", arm.message)));
        }
    }
    envs
}

fn drain_and_stop_ports(
    actor: &ActorDef,
    name: &str,
    port_order: &[String],
    has_ctor: bool,
    ctor_env_name: &str,
) -> Vec<Stmt> {
    let first = port_order.first().map(|s| s.as_str()).unwrap_or("__mbox");
    // Close every port before taking the final queue snapshot. A racing send
    // either commits before close and is drained, or fails and keeps ownership.
    let mut stmts: Vec<Stmt> = port_order.iter().map(|p| close_mailbox(p)).collect();
    for p in port_order {
        let envs = port_envelopes(actor, name, p, first, has_ctor, ctor_env_name);
        stmts.extend(mailbox_drain_named(p, &envs));
        stmts.push(Stmt::Expr(Expr::Call {
            callee: Box::new(Expr::Ident("actor_stop".into())),
            args: vec![Expr::Ident(p.clone())],
        }));
    }
    stmts
}

fn actor_fn(name: String, params: Vec<Param>, ret: Option<TypeExpr>, stmts: Vec<Stmt>) -> Item {
    Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name,
        type_params: Vec::new(),
        params,
        ret,
        body: Block {
            stmts,
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    })
}

fn expand_actor_ports(
    actor: &ActorDef,
    name: &str,
    state_ty: &str,
    has_ctor: bool,
    has_state: bool,
    ctor_env_name: &str,
    port_order: &[String],
    actor_fields: &[(String, TypeExpr, Option<Expr>)],
) -> Vec<Item> {
    let mut items = Vec::new();
    let handle = name.to_string();
    let chan_int = TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]);
    items.push(Item::Struct(StructDef {
        name: handle.clone(),
        type_params: Vec::new(),
        fields: port_order
            .iter()
            .map(|p| (p.clone(), chan_int.clone(), None))
            .collect(),
        derives: Vec::new(),
        exported: false,
        source_file: None,
    }));

    let ctor_fn_params: Vec<Param> = actor
        .ctor_params
        .iter()
        .map(|(n, ty)| Param {
            name: n.clone(),
            ty: ty.clone(),
            mutable: false,
            variadic: false,
        })
        .collect();

    let spawn_body = |cap: Expr| -> Vec<Stmt> {
        let mut stmts = Vec::new();
        for p in port_order {
            stmts.push(Stmt::Let {
                name: p.clone(),
                mutable: false,
                ownership: Ownership::None,
                ty: None,
                init: Expr::Call {
                    callee: Box::new(Expr::Ident("actor_spawn".into())),
                    args: vec![cap.clone()],
                },
            });
        }
        if has_ctor {
            let first = &port_order[0];
            stmts.push(Stmt::Expr(Expr::Call {
                callee: Box::new(Expr::Ident("actor_send".into())),
                args: vec![
                    Expr::Ident(first.clone()),
                    box_payload(envelope_lit(
                        ctor_env_name.to_string(),
                        CTOR_TAG,
                        &actor.ctor_params,
                    )),
                ],
            }));
        }
        let fields = port_order
            .iter()
            .map(|p| (p.clone(), Expr::Ident(p.clone())))
            .collect();
        stmts.push(Stmt::Return(Some(Expr::StructLit {
            name: handle.clone(),
            fields,
            update: None,
        })));
        stmts
    };

    items.push(actor_fn(
        format!("{name}_spawn"),
        ctor_fn_params.clone(),
        Some(TypeExpr::Named(handle.clone())),
        spawn_body(Expr::Int(16)),
    ));
    let mut cap_params = vec![Param {
        name: "__cap".into(),
        ty: TypeExpr::Named("int".into()),
        mutable: false,
        variadic: false,
    }];
    cap_params.extend(ctor_fn_params);
    items.push(actor_fn(
        format!("{name}_spawn_cap"),
        cap_params,
        Some(TypeExpr::Named(handle.clone())),
        spawn_body(Expr::Ident("__cap".into())),
    ));

    for p in port_order {
        items.push(actor_fn(
            format!("{name}_{p}_send"),
            vec![
                Param {
                    name: "__h".into(),
                    ty: TypeExpr::Named(handle.clone()),
                    mutable: false,
                    variadic: false,
                },
                Param {
                    name: "__msg".into(),
                    ty: TypeExpr::Named("int".into()),
                    mutable: false,
                    variadic: false,
                },
            ],
            Some(TypeExpr::Named("bool".into())),
            actor_send_body(name, Expr::Field {
                base: Box::new(Expr::Ident("__h".into())), field: p.clone(),
            }, Expr::Ident("__msg".into()), false),
        ));
    }
    for arm in &actor.receives {
        let p = arm_port(arm);
        let params: Vec<Param> = arm
            .params
            .iter()
            .map(|(n, ty)| Param {
                name: n.clone(),
                ty: ty.clone(),
                mutable: false,
                variadic: false,
            })
            .collect();
        let args: Vec<Expr> = arm
            .params
            .iter()
            .map(|(n, _)| Expr::Ident(n.clone()))
            .collect();
        items.push(actor_fn(
            format!("{name}_{}_send", arm.message),
            {
                let mut ps = vec![Param {
                    name: "__h".into(),
                    ty: TypeExpr::Named(handle.clone()),
                    mutable: false,
                    variadic: false,
                }];
                ps.extend(params);
                ps
            },
            Some(TypeExpr::Named("bool".into())),
            vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident(format!("{name}_{p}_send"))),
                args: vec![
                    Expr::Ident("__h".into()),
                    Expr::Call {
                        callee: Box::new(Expr::Ident(format!("{name}_{}", arm.message))),
                        args,
                    },
                ],
            }))],
        ));
    }

    let process = actor_process_from_m(actor, name, has_state);
    let mut loop_stmts: Vec<Stmt> = Vec::new();
    for p in port_order {
        loop_stmts.push(Stmt::Let {
            name: p.clone(),
            mutable: false,
            ownership: Ownership::None,
            ty: Some(chan_int.clone()),
            init: Expr::Field {
                base: Box::new(Expr::Ident("__h".into())),
                field: p.clone(),
            },
        });
    }
    loop_stmts.push(Stmt::Let {
        name: "__run".into(),
        mutable: true,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Int(1),
    });
    if has_ctor {
        let first = port_order[0].clone();
        loop_stmts.push(Stmt::Let {
            name: "__ctor_m".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_recv".into())),
                args: vec![Expr::Ident(first.clone())],
            },
        });
        loop_stmts.push(Stmt::If {
            init: None,
            cond: Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(Expr::Call {
                    callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                    args: vec![Expr::Ident("__ctor_m".into())],
                }),
                right: Box::new(Expr::Int(0)),
            },
            then_block: Block {
                stmts: {
                    let mut early =
                        drain_and_stop_ports(actor, name, port_order, has_ctor, ctor_env_name);
                    early.push(Stmt::Return(Some(Expr::Int(0))));
                    early
                },
                source_lines: Box::default(),
            },
            else_block: None,
        });
        loop_stmts.push(Stmt::Let {
            name: "__ctor_pl".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_payload".into())),
                args: vec![Expr::Ident("__ctor_m".into())],
            },
        });
        loop_stmts.push(Stmt::Let {
            name: "__ctor_env".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: Some(TypeExpr::Named(ctor_env_name.into())),
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_unbox_payload".into())),
                args: vec![Expr::Ident("__ctor_pl".into())],
            },
        });
        for (pname, pty) in &actor.ctor_params {
            loop_stmts.push(Stmt::Let {
                name: pname.clone(),
                mutable: false,
                ownership: Ownership::None,
                ty: Some(pty.clone()),
                init: Expr::Field {
                    base: Box::new(Expr::Ident("__ctor_env".into())),
                    field: pname.clone(),
                },
            });
        }
    }
    if has_state {
        let mut lit_fields = Vec::new();
        for (fname, _, def) in actor_fields {
            if let Some(d) = def {
                lit_fields.push((fname.clone(), d.clone()));
            }
        }
        let init = if lit_fields.is_empty() {
            Expr::StructLitPos {
                name: state_ty.into(),
                values: vec![],
            }
        } else {
            Expr::StructLit {
                name: state_ty.into(),
                fields: lit_fields,
                update: None,
            }
        };
        loop_stmts.push(Stmt::Let {
            name: "__st".into(),
            mutable: true,
            ownership: Ownership::None,
            ty: Some(TypeExpr::Named(state_ty.into())),
            init,
        });
    }
    let mut while_body: Vec<Stmt> = Vec::new();
    for p in port_order {
        let got = format!("__got_{p}");
        while_body.push(Stmt::Let { name: got.clone(), mutable: false, ownership: Ownership::None,
            ty: None, init: actor_batch_call("actor_try_recv", vec![Expr::Ident(p.clone())]) });
        let mut then_stmts = vec![Stmt::Let {
            name: "__m".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Ident(got.clone()),
        }];
        then_stmts.extend(process.clone());
        then_stmts.push(Stmt::Continue(Some("__actor_loop".into())));
        while_body.push(Stmt::If {
            init: None,
            cond: actor_binary(BinOp::Ne, Expr::Ident(got), Expr::Int(0)),
            then_block: Block {
                stmts: then_stmts,
                source_lines: Box::default(),
            },
            else_block: None,
        });
    }
    let sel_arms = port_order
        .iter()
        .map(|p| {
            let mut body = vec![Stmt::Let {
                name: "__m".into(),
                mutable: false,
                ownership: Ownership::None,
                ty: None,
                init: Expr::Call {
                    callee: Box::new(Expr::Ident("chan_select_value".into())),
                    args: vec![],
                },
            }];
            body.extend(process.clone());
            (
                p.clone(),
                Block {
                    stmts: body,
                    source_lines: Box::default(),
                },
            )
        })
        .collect();
    while_body.push(Stmt::Select {
        timeout_ms: Expr::Int(-1),
        arms: sel_arms,
        default_arm: None,
    });
    loop_stmts.push(Stmt::While {
        label: Some("__actor_loop".into()),
        cond: Expr::Binary {
            op: BinOp::Eq,
            left: Box::new(Expr::Ident("__run".into())),
            right: Box::new(Expr::Int(1)),
        },
        body: Block {
            stmts: while_body,
            source_lines: Box::default(),
        },
    });
    loop_stmts.extend(drain_and_stop_ports(
        actor,
        name,
        port_order,
        has_ctor,
        ctor_env_name,
    ));
    let ret_expr = if has_state {
        if let Some((fname, _, _)) = actor_fields.iter().find(|(n, ty, _)| {
            (n == "n"
                || n == "count"
                || n == "value"
                || n == "result"
                || n == "total"
                || n == "state")
                && matches!(ty, TypeExpr::Named(t) if t == "int" || t == "int64")
        }) {
            Expr::Field {
                base: Box::new(Expr::Ident("__st".into())),
                field: fname.clone(),
            }
        } else {
            Expr::Int(0)
        }
    } else {
        Expr::Int(0)
    };
    loop_stmts.push(Stmt::Return(Some(ret_expr)));
    items.push(actor_fn(
        format!("{name}_loop"),
        vec![Param {
            name: "__h".into(),
            ty: TypeExpr::Named(handle),
            mutable: false,
            variadic: false,
        }],
        Some(TypeExpr::Named("int".into())),
        loop_stmts,
    ));
    items
}

fn actor_process_from_m(actor: &ActorDef, name: &str, has_state: bool) -> Vec<Stmt> {
    let mut stmts = vec![
        Stmt::Let {
            name: "__tag".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                args: vec![Expr::Ident("__m".into())],
            },
        },
        Stmt::If {
            init: None,
            cond: Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(Expr::Ident("__tag".into())),
                right: Box::new(Expr::Int(0)),
            },
            then_block: Block {
                stmts: vec![
                    Stmt::Assign {
                        name: "__run".into(),
                        value: Expr::Int(0),
                    },
                    Stmt::Break(None),
                ],
                source_lines: Box::default(),
            },
            else_block: None,
        },
        Stmt::Let {
            name: "__pl".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_payload".into())),
                args: vec![Expr::Ident("__m".into())],
            },
        },
    ];
    let mut dispatch_arms: Vec<(i64, Vec<Stmt>)> = Vec::new();
    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let needs_env = arm_needs_envelope(arm);
        let mut arm_block = arm.body.clone();
        if has_state {
            rewrite_self_block(&mut arm_block, "__st");
        }
        let mut arm_stmts = Vec::new();
        if !arm.params.is_empty() {
            if !needs_env {
                let (ref pname, ref pty) = arm.params[0];
                arm_stmts.push(Stmt::Let {
                    name: pname.clone(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(pty.clone()),
                    init: actor_scalar_decode(pty),
                });
            } else {
                let env_name = format!("{name}_{}_Env", arm.message);
                arm_stmts.push(Stmt::Let {
                    name: "__env".into(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(TypeExpr::Named(env_name)),
                    init: Expr::Call {
                        callee: Box::new(Expr::Ident("actor_unbox_payload".into())),
                        args: vec![Expr::Ident("__pl".into())],
                    },
                });
                for (pname, pty) in &arm.params {
                    arm_stmts.push(Stmt::Let {
                        name: pname.clone(),
                        mutable: false,
                        ownership: Ownership::None,
                        ty: Some(pty.clone()),
                        init: Expr::Field {
                            base: Box::new(Expr::Ident("__env".into())),
                            field: pname.clone(),
                        },
                    });
                }
            }
        }
        rewrite_return_to_continue(&mut arm_block.stmts);
        arm_stmts.extend(arm_block.stmts.clone());
        if arm.message == "Bye" || arm.message == "Stop" {
            arm_stmts.push(Stmt::Assign {
                name: "__run".into(),
                value: Expr::Int(0),
            });
        }
        dispatch_arms.push((tag, actor_pair_dispatch(arm, &arm_block, arm_stmts)));
    }
    let mut dispatch_else: Option<Block> = None;
    for (tag, arm_stmts) in dispatch_arms.into_iter().rev() {
        dispatch_else = Some(Block {
            stmts: vec![Stmt::If {
                init: None,
                cond: Expr::Binary {
                    op: BinOp::Eq,
                    left: Box::new(Expr::Ident("__tag".into())),
                    right: Box::new(Expr::Int(tag)),
                },
                then_block: Block {
                    stmts: arm_stmts,
                    source_lines: Box::default(),
                },
                else_block: dispatch_else,
            }],
            source_lines: Box::default(),
        });
    }
    if let Some(block) = dispatch_else {
        stmts.extend(block.stmts);
    }
    stmts
}

// A receiver owns its prefetched messages. Keeping the cursor in the loop
// scope makes early returns safe; shutdown explicitly drops the unused suffix.
fn actor_batch_let(name: &str, init: Expr) -> Stmt {
    Stmt::Let {
        name: name.into(),
        mutable: true,
        ownership: Ownership::None,
        ty: None,
        init,
    }
}

fn actor_batch_call(name: &str, args: Vec<Expr>) -> Expr {
    Expr::Call {
        callee: Box::new(Expr::Ident(name.into())),
        args,
    }
}

fn actor_batch_index() -> Expr {
    Expr::Index {
        base: Box::new(Expr::Ident("__batch".into())),
        index: Box::new(Expr::Ident("__batch_i".into())),
    }
}

fn actor_batch_advance() -> Stmt {
    Stmt::Assign {
        name: "__batch_i".into(),
        value: Expr::Binary {
            op: BinOp::Add,
            left: Box::new(Expr::Ident("__batch_i".into())),
            right: Box::new(Expr::Int(1)),
        },
    }
}

fn expand_actor(actor: ActorDef) -> Vec<Item> {
    let name = &actor.name;
    let mut items = Vec::new();
    let state_ty = format!("{name}_State");
    let mut actor_fields = actor.fields.clone();

    // Ensure all ctor params are registered as state fields so they can be accessed as self.param
    for (pname, pty) in &actor.ctor_params {
        if let Some(existing) = actor_fields.iter_mut().find(|(f, _, _)| f == pname) {
            if existing.2.is_none() {
                existing.2 = Some(Expr::Ident(pname.clone()));
            }
        } else {
            actor_fields.push((pname.clone(), pty.clone(), Some(Expr::Ident(pname.clone()))));
        }
    }

    let has_state = !actor_fields.is_empty();
    let has_ctor = !actor.ctor_params.is_empty();

    // Owned state struct (if any fields).
    if has_state {
        items.push(Item::Struct(StructDef {
            name: state_ty.clone(),
            type_params: Vec::new(),
            fields: actor_fields.clone(),
            derives: Vec::new(),
            exported: false,
            source_file: None,
        }));
    }

    // Constructor envelope struct if constructor has parameters.
    let ctor_env_name = format!("{name}_Ctor_Env");
    if has_ctor {
        items.push(Item::Struct(StructDef {
            name: ctor_env_name.clone(),
            type_params: Vec::new(),
            fields: envelope_struct_fields(&actor.ctor_params),
            derives: Vec::new(),
            exported: false,
            source_file: None,
        }));
    }

    // Generate per-message envelope struct only for arms that actually need it.
    for arm in &actor.receives {
        if arm_needs_envelope(arm) {
            let env_name = format!("{name}_{}_Env", arm.message);
            items.push(Item::Struct(StructDef {
                name: env_name,
                type_params: Vec::new(),
                fields: envelope_struct_fields(&arm.params),
                derives: Vec::new(),
                exported: false,
                source_file: None,
            }));
        }
    }

    // Message constructors: Session_Invite() -> pack(tag, 0)
    // or Session_Inc(delta) -> pack(tag, delta) for receive Inc(delta: int) (zero-alloc),
    // or Session_Msg(args...) -> pack(tag, box_payload(env)) for envelope arms.
    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let (params, packed) = if arm.params.is_empty() {
            (
                vec![],
                Expr::Call {
                    callee: Box::new(Expr::Ident("actor_pack".into())),
                    args: vec![Expr::Int(tag), Expr::Int(0)],
                },
            )
        } else if !arm_needs_envelope(arm) {
            // Fast zero-alloc path: single scalar
            let (ref pname, ref pty) = arm.params[0];
            (
                vec![Param {
                    name: pname.clone(),
                    ty: pty.clone(),
                    mutable: false,
                    variadic: false,
                }],
                Expr::Call {
                    callee: Box::new(Expr::Ident("actor_pack".into())),
                    args: vec![Expr::Int(tag),
                        if matches!(pty, TypeExpr::Named(n) if n == "bool") {
                            Expr::Int(0) // Boolean constructor emits explicit 0/1 branches.
                        } else { Expr::Ident(pname.clone()) }],
                },
            )
        } else {
            // Envelope path: tag lives in the boxed struct; send the full pointer.
            let env_name = format!("{name}_{}_Env", arm.message);
            let fn_params: Vec<Param> = arm
                .params
                .iter()
                .map(|(n, ty)| Param {
                    name: n.clone(),
                    ty: ty.clone(),
                    mutable: false,
                    variadic: false,
                })
                .collect();
            (
                fn_params,
                box_payload(envelope_lit(env_name, tag, &arm.params)),
            )
        };
        items.push(Item::Fn(FnDef {
            type_bounds: std::collections::HashMap::new(),
            name: format!("{name}_{}", arm.message),
            type_params: Vec::new(),
            params,
            ret: Some(TypeExpr::Named("int".into())),
            body: Block {
                stmts: actor_message_ctor_body(arm, tag, packed),
                source_lines: Box::default(),
            },
            exported: false,
            is_const: false,
            is_live: false,
            stability: crate::ast::ApiStability::Unspecified,
            contracts: vec![],
            source_file: None,
        }));
    }

    let mut all_envelopes: Vec<(i64, String)> = actor.receives.iter().enumerate()
        .filter(|(_, arm)| arm_needs_envelope(arm))
        .map(|(i, arm)| ((i + 1) as i64, format!("{name}_{}_Env", arm.message))).collect();
    if has_ctor { all_envelopes.push((CTOR_TAG, ctor_env_name.clone())); }
    items.push(actor_drop_message(name, &all_envelopes));

    let mut port_order: Vec<String> = Vec::new();
    for arm in &actor.receives {
        let p = arm_port(arm);
        if !port_order.contains(&p) {
            port_order.push(p);
        }
    }
    // A port named `control` is always polled first so Open/Bye/Init are not
    // stuck behind a full exec queue (FayDB's head-of-line case).
    if let Some(i) = port_order.iter().position(|p| p == "control") {
        if i != 0 {
            let c = port_order.remove(i);
            port_order.insert(0, c);
        }
    }
    if port_order.len() > 1 {
        items.extend(expand_actor_ports(
            &actor,
            name,
            &state_ty,
            has_ctor,
            has_state,
            &ctor_env_name,
            &port_order,
            &actor_fields,
        ));
        return items;
    }

    // Session_spawn(...ctor_args) -> chan[int] (default mailbox 16)
    let ctor_fn_params: Vec<Param> = actor
        .ctor_params
        .iter()
        .map(|(n, ty)| Param {
            name: n.clone(),
            ty: ty.clone(),
            mutable: false,
            variadic: false,
        })
        .collect();

    let mut spawn_stmts = vec![Stmt::Let {
        name: "__mbox".into(),
        mutable: false,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Call {
            callee: Box::new(Expr::Ident("actor_spawn".into())),
            args: vec![Expr::Int(16)],
        },
    }];

    if has_ctor {
        spawn_stmts.push(Stmt::Expr(Expr::Call {
            callee: Box::new(Expr::Ident("actor_send".into())),
            args: vec![
                Expr::Ident("__mbox".into()),
                box_payload(envelope_lit(
                    ctor_env_name.clone(),
                    CTOR_TAG,
                    &actor.ctor_params,
                )),
            ],
        }));
    }
    spawn_stmts.push(Stmt::Return(Some(Expr::Ident("__mbox".into()))));

    items.push(Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name: format!("{name}_spawn"),
        type_params: Vec::new(),
        params: ctor_fn_params.clone(),
        ret: Some(TypeExpr::Generic(
            "chan".into(),
            vec![TypeExpr::Named("int".into())],
        )),
        body: Block {
            stmts: spawn_stmts,
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    // Session_spawn_cap(cap, ...ctor_args) -> chan[int]
    let mut spawn_cap_params = vec![Param {
        name: "__cap".into(),
        ty: TypeExpr::Named("int".into()),
        mutable: false,
        variadic: false,
    }];
    spawn_cap_params.extend(ctor_fn_params.clone());

    let mut spawn_cap_stmts = vec![Stmt::Let {
        name: "__mbox".into(),
        mutable: false,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Call {
            callee: Box::new(Expr::Ident("actor_spawn".into())),
            args: vec![Expr::Ident("__cap".into())],
        },
    }];

    if has_ctor {
        spawn_cap_stmts.push(Stmt::Expr(Expr::Call {
            callee: Box::new(Expr::Ident("actor_send".into())),
            args: vec![
                Expr::Ident("__mbox".into()),
                box_payload(envelope_lit(
                    ctor_env_name.clone(),
                    CTOR_TAG,
                    &actor.ctor_params,
                )),
            ],
        }));
    }
    spawn_cap_stmts.push(Stmt::Return(Some(Expr::Ident("__mbox".into()))));

    items.push(Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name: format!("{name}_spawn_cap"),
        type_params: Vec::new(),
        params: spawn_cap_params,
        ret: Some(TypeExpr::Generic(
            "chan".into(),
            vec![TypeExpr::Named("int".into())],
        )),
        body: Block {
            stmts: spawn_cap_stmts,
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    // Session_send(mbox, tag) -> bool
    items.push(Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name: format!("{name}_send"),
        type_params: Vec::new(),
        params: vec![
            Param {
                name: "__mbox".into(),
                ty: TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]),
                mutable: false,
                variadic: false,
            },
            Param {
                name: "__tag".into(),
                ty: TypeExpr::Named("int".into()),
                mutable: false,
                variadic: false,
            },
        ],
        ret: Some(TypeExpr::Named("bool".into())),
        body: Block {
            stmts: actor_send_body(name, Expr::Ident("__mbox".into()), Expr::Ident("__tag".into()), false),
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    let mut envelopes: Vec<(i64, String)> = Vec::new();
    if has_ctor {
        envelopes.push((CTOR_TAG, ctor_env_name.clone()));
    }
    for (i, arm) in actor.receives.iter().enumerate() {
        if arm_needs_envelope(arm) {
            envelopes.push(((i + 1) as i64, format!("{name}_{}_Env", arm.message)));
        }
    }

    items.push(actor_fn(format!("{name}_try_send"), vec![
        Param { name: "__mbox".into(), ty: TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]), mutable: false, variadic: false },
        Param { name: "__msg".into(), ty: TypeExpr::Named("int".into()), mutable: false, variadic: false },
    ], Some(TypeExpr::Named("int".into())), actor_send_body(name, Expr::Ident("__mbox".into()), Expr::Ident("__msg".into()), true)));

    // Session_loop(mbox) — message dispatch (+ optional state)
    let mut loop_stmts: Vec<Stmt> = vec![Stmt::Let {
        name: "__run".into(),
        mutable: true,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Int(1),
    }];

    if has_ctor {
        loop_stmts.push(Stmt::Let {
            name: "__ctor_m".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_recv".into())),
                args: vec![Expr::Ident("__mbox".into())],
            },
        });
        loop_stmts.push(Stmt::If {
            init: None,
            cond: Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(Expr::Call {
                    callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                    args: vec![Expr::Ident("__ctor_m".into())],
                }),
                right: Box::new(Expr::Int(0)),
            },
            then_block: Block {
                stmts: {
                    let mut early = mailbox_drain_stmts(&envelopes);
                    early.push(Stmt::Expr(Expr::Call {
                        callee: Box::new(Expr::Ident("actor_stop".into())),
                        args: vec![Expr::Ident("__mbox".into())],
                    }));
                    early.push(Stmt::Return(Some(Expr::Int(0))));
                    early
                },
                source_lines: Box::default(),
            },
            else_block: None,
        });
        loop_stmts.push(Stmt::Let {
            name: "__ctor_pl".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_payload".into())),
                args: vec![Expr::Ident("__ctor_m".into())],
            },
        });
        loop_stmts.push(Stmt::Let {
            name: "__ctor_env".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: Some(TypeExpr::Named(ctor_env_name.clone())),
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_unbox_payload".into())),
                args: vec![Expr::Ident("__ctor_pl".into())],
            },
        });
        for (pname, pty) in &actor.ctor_params {
            loop_stmts.push(Stmt::Let {
                name: pname.clone(),
                mutable: false,
                ownership: Ownership::None,
                ty: Some(pty.clone()),
                init: Expr::Field {
                    base: Box::new(Expr::Ident("__ctor_env".into())),
                    field: pname.clone(),
                },
            });
        }
    }

    if has_state {
        // Zero/defaults via partial lit + field defaults, or empty positional.
        let mut lit_fields = Vec::new();
        for (fname, _, def) in &actor_fields {
            if let Some(d) = def {
                lit_fields.push((fname.clone(), d.clone()));
            }
        }
        let init = if lit_fields.is_empty() {
            Expr::StructLitPos {
                name: state_ty.clone(),
                values: vec![],
            }
        } else {
            Expr::StructLit {
                name: state_ty.clone(),
                fields: lit_fields,
                update: None,
            }
        };
        loop_stmts.push(Stmt::Let {
            name: "__st".into(),
            mutable: true,
            ownership: Ownership::None,
            ty: Some(TypeExpr::Named(state_ty.clone())),
            init,
        });
    }

    loop_stmts.extend([
        actor_batch_let("__batch", Expr::Make { ty: TypeExpr::Array(Box::new(TypeExpr::Named("int".into()))), len: Some(Box::new(Expr::Int(16))), cap: None }),
        actor_batch_let("__batch_i", Expr::Int(0)),
        actor_batch_let("__batch_n", Expr::Int(0)),
    ]);
    let mut while_body: Vec<Stmt> = vec![
        Stmt::If {
            init: None,
            cond: Expr::Binary { op: BinOp::Eq, left: Box::new(Expr::Ident("__batch_i".into())), right: Box::new(Expr::Ident("__batch_n".into())) },
            then_block: Block { stmts: vec![
                Stmt::Assign { name: "__batch_n".into(), value: actor_batch_call("actor_recv_batch", vec![Expr::Ident("__mbox".into()), Expr::Ident("__batch".into())]) },
                Stmt::Assign { name: "__batch_i".into(), value: Expr::Int(0) },
                Stmt::If { init: None,
                    cond: Expr::Binary { op: BinOp::Eq, left: Box::new(Expr::Ident("__batch_n".into())), right: Box::new(Expr::Int(0)) },
                    then_block: Block { stmts: vec![Stmt::Break(None)], source_lines: Box::default() }, else_block: None },
            ], source_lines: Box::default() }, else_block: None,
        },
        Stmt::Let {
            name: "__m".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: actor_batch_index(),
        },
        actor_batch_advance(),
        Stmt::Let {
            name: "__tag".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_tag".into())),
                args: vec![Expr::Ident("__m".into())],
            },
        },
        Stmt::If {
            init: None,
            cond: Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(Expr::Ident("__tag".into())),
                right: Box::new(Expr::Int(0)),
            },
            then_block: Block {
                stmts: vec![
                    Stmt::Assign {
                        name: "__run".into(),
                        value: Expr::Int(0),
                    },
                    Stmt::Break(None),
                ],
                source_lines: Box::default(),
            },
            else_block: None,
        },
        Stmt::Let {
            name: "__pl".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_msg_payload".into())),
                args: vec![Expr::Ident("__m".into())],
            },
        },
    ];

    let mut dispatch_arms: Vec<(i64, Vec<Stmt>)> = Vec::new();
    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let needs_env = arm_needs_envelope(arm);
        let mut arm_block = arm.body.clone();
        if has_state {
            rewrite_self_block(&mut arm_block, "__st");
        }
        let mut arm_stmts = Vec::new();
        if !arm.params.is_empty() {
            if !needs_env {
                // Legacy / fast scalar path (zero allocation)
                let (ref pname, ref pty) = arm.params[0];
                arm_stmts.push(Stmt::Let {
                    name: pname.clone(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(pty.clone()),
                    init: actor_scalar_decode(pty),
                });
            } else {
                // Envelope path: unbox pointer, extract fields
                let env_name = format!("{name}_{}_Env", arm.message);
                arm_stmts.push(Stmt::Let {
                    name: "__env".into(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(TypeExpr::Named(env_name)),
                    init: Expr::Call {
                        callee: Box::new(Expr::Ident("actor_unbox_payload".into())),
                        args: vec![Expr::Ident("__pl".into())],
                    },
                });
                for (pname, pty) in &arm.params {
                    arm_stmts.push(Stmt::Let {
                        name: pname.clone(),
                        mutable: false,
                        ownership: Ownership::None,
                        ty: Some(pty.clone()),
                        init: Expr::Field {
                            base: Box::new(Expr::Ident("__env".into())),
                            field: pname.clone(),
                        },
                    });
                }
            }
        }
        rewrite_return_to_continue(&mut arm_block.stmts);
        arm_stmts.extend(arm_block.stmts.clone());
        // Convention: message named Bye / Stop ends the loop
        if arm.message == "Bye" || arm.message == "Stop" {
            arm_stmts.push(Stmt::Assign {
                name: "__run".into(),
                value: Expr::Int(0),
            });
        }
        dispatch_arms.push((tag, actor_pair_dispatch(arm, &arm_block, arm_stmts)));
    }

    // else-if chain so a hit does not keep testing later tags.
    let mut dispatch_else: Option<Block> = None;
    for (tag, arm_stmts) in dispatch_arms.into_iter().rev() {
        dispatch_else = Some(Block {
            stmts: vec![Stmt::If {
                init: None,
                cond: Expr::Binary {
                    op: BinOp::Eq,
                    left: Box::new(Expr::Ident("__tag".into())),
                    right: Box::new(Expr::Int(tag)),
                },
                then_block: Block {
                    stmts: arm_stmts,
                    source_lines: Box::default(),
                },
                else_block: dispatch_else,
            }],
            source_lines: Box::default(),
        });
    }
    if let Some(block) = dispatch_else {
        while_body.extend(block.stmts);
    }

    // If state has an int field `n`, `count`, etc., return it on exit; else 0.
    let ret_expr = if has_state {
        if let Some((fname, _, _)) = actor_fields.iter().find(|(n, ty, _)| {
            (n == "n"
                || n == "count"
                || n == "value"
                || n == "result"
                || n == "total"
                || n == "state")
                && matches!(ty, TypeExpr::Named(t) if t == "int" || t == "int64")
        }) {
            Expr::Field {
                base: Box::new(Expr::Ident("__st".into())),
                field: fname.clone(),
            }
        } else {
            Expr::Int(0)
        }
    } else {
        Expr::Int(0)
    };

    loop_stmts.push(Stmt::While {
        label: Some("__actor_loop".into()),
        cond: Expr::Binary {
            op: BinOp::Eq,
            left: Box::new(Expr::Ident("__run".into())),
            right: Box::new(Expr::Int(1)),
        },
        body: Block {
            stmts: while_body,
            source_lines: Box::default(),
        },
    });

    // Close first so a blocked producer cannot escape shutdown cleanup while
    // prefetched messages are destroyed. Preserve typed ownership for the tail.
    loop_stmts.push(close_mailbox("__mbox"));
    loop_stmts.push(Stmt::While {
        label: None,
        cond: Expr::Binary { op: BinOp::Lt, left: Box::new(Expr::Ident("__batch_i".into())), right: Box::new(Expr::Ident("__batch_n".into())) },
        body: Block { stmts: vec![
            Stmt::Expr(actor_batch_call(&format!("{name}_drop_message"), vec![actor_batch_index()])),
            actor_batch_advance(),
        ], source_lines: Box::default() },
    });
    // Drain remaining envelopes by typed unbox so nested strings/chans/slices free.
    loop_stmts.extend(mailbox_drain_stmts(&envelopes));

    loop_stmts.push(Stmt::Expr(Expr::Call {
        callee: Box::new(Expr::Ident("actor_stop".into())),
        args: vec![Expr::Ident("__mbox".into())],
    }));
    loop_stmts.push(Stmt::Return(Some(ret_expr)));

    items.push(Item::Fn(FnDef {
        type_bounds: std::collections::HashMap::new(),
        name: format!("{name}_loop"),
        type_params: Vec::new(),
        params: vec![Param {
            name: "__mbox".into(),
            ty: TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]),
            mutable: false,
            variadic: false,
        }],
        ret: Some(TypeExpr::Named("int".into())),
        body: Block {
            stmts: loop_stmts,
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    items
}

/// Re-run if-let desugaring on a merged program (after resolve_imports / merge).
/// Imported and merged files may contain IfLet that the initial desugar missed.
pub fn desugar_if_let_all(program: &mut Program) {
    // Expand deferred actors now that all types from merged files are available.
    let mut actor_items = Vec::new();
    program.items.retain(|item| {
        if let Item::Actor(actor) = item {
            actor_items.extend(expand_actor(actor.clone()));
            false
        } else {
            true
        }
    });
    program.items.extend(actor_items);

    for item in &mut program.items {
        match item {
            Item::Fn(f) => desugar_if_let_block(&mut f.body),
            Item::On(on) => {
                for m in &mut on.methods {
                    desugar_if_let_block(&mut m.body);
                    resolve_embed_block(&mut m.body, None);
                }
            }
            _ => {}
        }
    }
}
