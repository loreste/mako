//! Canonical `.mko` formatter (gofmt-like): parse → print from AST.
//!
//! # Style (opinionated, almost no config)
//! - Indentation: **4 spaces**
//! - Braces: Go/K&R — `{` on the same line as `fn` / `if` / `else` / `for` / …
//! - Spaces around binary operators and after commas
//! - Imports sorted and grouped at the top of the file
//! - One blank line between top-level declarations
//! - No trailing whitespace; file ends with a single newline
//!
//! Idempotent: `format(format(src)) == format(src)` for parseable input.

use crate::ast::*;

const INDENT: &str = "    ";

/// Format a whole program to canonical source.
pub fn format_program(program: &Program) -> String {
    let mut items = program.items.clone();
    // Imports first, sorted by path then alias.
    let mut imports = Vec::new();
    let mut rest = Vec::new();
    for it in items.drain(..) {
        match it {
            Item::Import { .. } => imports.push(it),
            other => rest.push(other),
        }
    }
    imports.sort_by(|a, b| match (a, b) {
        (
            Item::Import {
                path: p1,
                alias: a1,
                mode: m1,
            },
            Item::Import {
                path: p2,
                alias: a2,
                mode: m2,
            },
        ) => import_group_kind(p1)
            .cmp(&import_group_kind(p2))
            .then(p1.cmp(p2))
            .then(a1.cmp(a2))
            .then(m1.cmp(m2)),
        _ => std::cmp::Ordering::Equal,
    });

    let mut out = String::new();
    if !imports.is_empty() {
        out.push_str(&fmt_imports(&imports));
        if !out.ends_with('\n') {
            out.push('\n');
        }
    }
    let mut first = imports.is_empty();
    for it in rest {
        if !first {
            out.push('\n');
        }
        first = false;
        out.push_str(&fmt_item(&it));
        if !out.ends_with('\n') {
            out.push('\n');
        }
    }
    // Exactly one trailing newline; strip trailing spaces on each line.
    let cleaned: String = out
        .lines()
        .map(|l| l.trim_end())
        .collect::<Vec<_>>()
        .join("\n");
    if cleaned.is_empty() {
        String::from("\n")
    } else {
        format!("{cleaned}\n")
    }
}

/// Emit imports in Mako-preferred form: one path → single line; 2+ → `pull ( … )`.
/// Groups with a blank line between std-like, remote (host), and relative — like goimports.
/// Keyword is `pull` (not dual `import`). Aliases use `"path" as name`.
fn fmt_imports(imports: &[Item]) -> String {
    if imports.len() == 1 {
        return fmt_item(&imports[0]);
    }
    let mut items: Vec<&Item> = imports.iter().collect();
    items.sort_by(|a, b| match (a, b) {
        (
            Item::Import {
                path: pa,
                alias: aa,
                mode: ma,
            },
            Item::Import {
                path: pb,
                alias: ab,
                mode: mb,
            },
        ) => import_group_kind(pa)
            .cmp(&import_group_kind(pb))
            .then(pa.cmp(pb))
            .then(aa.cmp(ab))
            .then(ma.cmp(mb)),
        _ => std::cmp::Ordering::Equal,
    });

    let mut o = String::from("pull (\n");
    let mut prev_kind: Option<u8> = None;
    for it in items {
        if let Item::Import { path, alias, mode } = it {
            let kind = import_group_kind(path);
            if let Some(pk) = prev_kind {
                if pk != kind {
                    o.push('\n');
                }
            }
            prev_kind = Some(kind);
            o.push_str(INDENT);
            o.push_str(&fmt_import_spec(path, alias.as_deref(), *mode));
            o.push('\n');
        }
    }
    o.push(')');
    o
}

/// 0 = std-like, 1 = remote module (host in first segment), 2 = relative.
fn import_group_kind(path: &str) -> u8 {
    if path.starts_with("./") || path.starts_with("../") || path.starts_with('/') {
        return 2;
    }
    let first = path.split('/').next().unwrap_or(path);
    if first.contains('.') {
        return 1; // github.com, … 
    }
    0
}

fn fmt_import_spec(path: &str, alias: Option<&str>, mode: ImportMode) -> String {
    let p = escape_str(path);
    match mode {
        ImportMode::Blank => format!("_ \"{p}\""),
        ImportMode::Dot => format!(". \"{p}\""),
        ImportMode::Normal => match alias {
            Some(a) => format!("\"{p}\" as {a}"),
            None => format!("\"{p}\""),
        },
    }
}

fn fmt_item(item: &Item) -> String {
    match item {
        Item::Fn(f) => {
            let mut o = String::new();
            if f.exported {
                o.push_str("export ");
            }
            // Mako-native spelling: `fn`, `name: Type`, `-> Ret`
            o.push_str("fn ");
            o.push_str(&f.name);
            if !f.type_params.is_empty() {
                o.push('[');
                o.push_str(&f.type_params.join(", "));
                o.push(']');
            }
            o.push('(');
            for (i, p) in f.params.iter().enumerate() {
                if i > 0 {
                    o.push_str(", ");
                }
                if p.mutable {
                    o.push_str("mut ");
                }
                o.push_str(&p.name);
                if !matches!(&p.ty, TypeExpr::Named(n) if n == "__self") {
                    o.push_str(": ");
                    o.push_str(&fmt_type(&p.ty));
                }
            }
            o.push(')');
            if let Some(ret) = &f.ret {
                o.push_str(" -> ");
                o.push_str(&fmt_type(ret));
            }
            o.push(' ');
            o.push_str(&fmt_block(&f.body, 0));
            o
        }
        Item::On(on) => {
            let mut o = String::new();
            if on.exported {
                o.push_str("export ");
            }
            o.push_str("on ");
            o.push_str(&on.ty);
            o.push_str(" {\n");
            for m in &on.methods {
                // Format methods with short name (before desugar Type_ prefix)
                let mf = m.clone();
                o.push_str(INDENT);
                o.push_str(&fmt_item(&Item::Fn(mf)).replace('\n', "\n    "));
                o.push('\n');
            }
            o.push('}');
            o
        }
        Item::Struct(s) => {
            let mut o = String::new();
            if !s.derives.is_empty() {
                o.push_str("#[derive(");
                o.push_str(&s.derives.join(", "));
                o.push_str(")]\n");
            }
            // Mako-native: `struct Name { x: T }` (optional export)
            if s.exported {
                o.push_str("export ");
            }
            o.push_str("struct ");
            o.push_str(&s.name);
            o.push_str(" {\n");
            for (n, t) in &s.fields {
                o.push_str(INDENT);
                o.push_str(n);
                o.push_str(": ");
                o.push_str(&fmt_type(t));
                o.push('\n');
            }
            o.push('}');
            o
        }
        Item::Enum(e) => {
            let mut o = String::new();
            if e.exported {
                o.push_str("export ");
            }
            o.push_str(&format!("enum {} {{\n", e.name));
            for v in &e.variants {
                o.push_str(INDENT);
                o.push_str(&v.name);
                if !v.fields.is_empty() {
                    o.push('(');
                    for (i, t) in v.fields.iter().enumerate() {
                        if i > 0 {
                            o.push_str(", ");
                        }
                        o.push_str(&fmt_type(t));
                    }
                    o.push(')');
                }
                o.push('\n');
            }
            o.push('}');
            o
        }
        Item::Actor(a) => {
            let mut o = format!("actor {} {{\n", a.name);
            for arm in &a.receives {
                o.push_str(INDENT);
                o.push_str("receive ");
                o.push_str(&arm.message);
                o.push(' ');
                o.push_str(&fmt_block(&arm.body, 1));
                o.push('\n');
            }
            o.push('}');
            o
        }
        Item::Interface(i) => {
            let mut o = format!("interface {} {{\n", i.name);
            for (n, params, ret) in &i.methods {
                o.push_str(INDENT);
                o.push_str("fn ");
                o.push_str(n);
                o.push('(');
                for (j, t) in params.iter().enumerate() {
                    if j > 0 {
                        o.push_str(", ");
                    }
                    o.push_str(&fmt_type(t));
                }
                o.push_str(") -> ");
                o.push_str(&fmt_type(ret));
                o.push('\n');
            }
            o.push('}');
            o
        }
        Item::ExternC(e) => {
            let mut o = String::from("extern \"C\" fn ");
            o.push_str(&e.name);
            o.push('(');
            for (i, p) in e.params.iter().enumerate() {
                if i > 0 {
                    o.push_str(", ");
                }
                o.push_str(&p.name);
                o.push_str(": ");
                o.push_str(&fmt_type(&p.ty));
            }
            o.push(')');
            if let Some(ret) = &e.ret {
                o.push_str(" -> ");
                o.push_str(&fmt_type(ret));
            }
            o
        }
        Item::Import { path, alias, mode } => {
            format!("pull {}", fmt_import_spec(path, alias.as_deref(), *mode))
        }
        Item::Package { name } => format!("pack {name}"),
        Item::Const(c) => {
            format!("const {} = {}", c.name, fmt_expr(&c.value, 0))
        }
    }
}

fn fmt_type(t: &TypeExpr) -> String {
    match t {
        TypeExpr::Named(n) => n.clone(),
        TypeExpr::Array(i) => format!("[]{}", fmt_type(i)),
        TypeExpr::Map(k, v) => format!("map[{}]{}", fmt_type(k), fmt_type(v)),
        TypeExpr::Generic(n, args) => {
            // Prefer Go-like map[K]V spelling when name is map with 2 args.
            if n == "map" && args.len() == 2 {
                return format!("map[{}]{}", fmt_type(&args[0]), fmt_type(&args[1]));
            }
            let a: Vec<_> = args.iter().map(fmt_type).collect();
            // Mako dual generics: keep [] form as canonical in fmt
            format!("{n}[{}]", a.join(", "))
        }
        TypeExpr::Fn(params, ret) => {
            let p: Vec<_> = params.iter().map(fmt_type).collect();
            format!("fn({}) -> {}", p.join(", "), fmt_type(ret))
        }
        TypeExpr::Tuple(elems) => {
            let e: Vec<_> = elems.iter().map(fmt_type).collect();
            format!("({})", e.join(", "))
        }
    }
}

fn fmt_block(b: &Block, indent: usize) -> String {
    let pad = INDENT.repeat(indent);
    let inner = INDENT.repeat(indent + 1);
    let mut out = String::from("{\n");
    for s in &b.stmts {
        out.push_str(&inner);
        out.push_str(&fmt_stmt(s, indent + 1));
        out.push('\n');
    }
    out.push_str(&pad);
    out.push('}');
    out
}

fn fmt_stmt(s: &Stmt, indent: usize) -> String {
    match s {
        Stmt::LetMulti {
            names,
            mutable: _,
            init,
        } => {
            // Mako-native multi-bind (not `:=`)
            let ns = names.join(", ");
            format!("let {ns} = {}", fmt_expr(init, 0))
        }
        Stmt::Let {
            name,
            mutable,
            ownership,
            ty,
            init,
        } => {
            let mut o = String::new();
            match ownership {
                Ownership::Hold => o.push_str("hold "),
                Ownership::Share => o.push_str("share "),
                Ownership::None => {}
            }
            // Mako-native: `let` / `let mut` (not `var` / `:=`)
            o.push_str("let ");
            if *mutable {
                o.push_str("mut ");
            }
            o.push_str(name);
            if let Some(t) = ty {
                o.push_str(": ");
                o.push_str(&fmt_type(t));
            }
            o.push_str(" = ");
            o.push_str(&fmt_expr(init, 0));
            o
        }
        Stmt::LetCommaOk {
            value,
            ok,
            mutable,
            base,
            index,
        } => {
            let mut o = String::from("let ");
            if *mutable {
                o.push_str("mut ");
            }
            o.push_str(value);
            o.push_str(", ");
            o.push_str(ok);
            o.push_str(" = ");
            o.push_str(&fmt_expr(base, 0));
            o.push('[');
            o.push_str(&fmt_expr(index, 0));
            o.push(']');
            o
        }
        Stmt::Assign { name, value } => format!("{name} = {}", fmt_expr(value, 0)),
        Stmt::FieldAssign { base, field, value } => {
            format!("{}.{} = {}", fmt_expr(base, 0), field, fmt_expr(value, 0))
        }
        Stmt::IndexAssign { base, index, value } => format!(
            "{}[{}] = {}",
            fmt_expr(base, 0),
            fmt_expr(index, 0),
            fmt_expr(value, 0)
        ),
        Stmt::Expr(e) => fmt_expr(e, 0),
        Stmt::Return(None) => "return".into(),
        Stmt::Return(Some(e)) => format!("return {}", fmt_expr(e, 0)),
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
        } => {
            // Preserve the Go-style init clause: `if x := f(); cond { … }`.
            let head = match init {
                Some(i) => format!("if {}; {}", fmt_stmt(i, 0), fmt_expr(cond, 0)),
                None => format!("if {}", fmt_expr(cond, 0)),
            };
            let mut o = format!("{head} {}", fmt_block(then_block, indent));
            if let Some(eb) = else_block {
                // else-if chain: if single stmt is If, print `else if`
                if eb.stmts.len() == 1 {
                    if let Stmt::If {
                        init: i2,
                        cond: c2,
                        then_block: t2,
                        else_block: e2,
                    } = &eb.stmts[0]
                    {
                        o.push_str(" else ");
                        o.push_str(&fmt_stmt(
                            &Stmt::If {
                                init: i2.clone(),
                                cond: c2.clone(),
                                then_block: t2.clone(),
                                else_block: e2.clone(),
                            },
                            indent,
                        ));
                        return o;
                    }
                }
                o.push_str(" else ");
                o.push_str(&fmt_block(eb, indent));
            }
            o
        }
        Stmt::While { label, cond, body } => {
            let pref = label.as_ref().map(|l| format!("{l}: ")).unwrap_or_default();
            format!(
                "{pref}while {} {}",
                fmt_expr(cond, 0),
                fmt_block(body, indent)
            )
        }
        Stmt::For {
            label,
            binders,
            is_range,
            iter,
            body,
        } => {
            let pref = label.as_ref().map(|l| format!("{l}: ")).unwrap_or_default();
            let body_s = fmt_block(body, indent);
            let iter_s = fmt_expr(iter, 0);
            if binders.is_empty() {
                format!("{pref}for range {iter_s} {body_s}")
            } else {
                let bs = binders.join(", ");
                let r = if *is_range { "range " } else { "" };
                format!("{pref}for {bs} in {r}{iter_s} {body_s}")
            }
        }
        Stmt::CFor {
            label,
            init,
            cond,
            post,
            body,
        } => {
            let pref = label.as_ref().map(|l| format!("{l}: ")).unwrap_or_default();
            format!(
                "{pref}for {}; {}; {} {}",
                fmt_stmt(init, 0),
                fmt_expr(cond, 0),
                fmt_stmt(post, 0),
                fmt_block(body, indent)
            )
        }
        Stmt::Break(None) => "break".into(),
        Stmt::Break(Some(l)) => format!("break {l}"),
        Stmt::Continue(None) => "continue".into(),
        Stmt::Continue(Some(l)) => format!("continue {l}"),
        Stmt::Defer { body } => format!("defer {}", fmt_block(body, indent)),
        Stmt::Crew { name, body } => format!("crew {name} {}", fmt_block(body, indent)),
        Stmt::Arena { name, body } => format!("arena {name} {}", fmt_block(body, indent)),
        Stmt::Unsafe { body } => format!("unsafe {}", fmt_block(body, indent)),
        Stmt::Select {
            timeout_ms,
            arms,
            default_arm,
        } => {
            let mut o = format!("select timeout {} {{\n", fmt_expr(timeout_ms, 0));
            let inner = INDENT.repeat(indent + 1);
            for (ch, body) in arms {
                o.push_str(&inner);
                o.push_str(ch);
                o.push_str(" => ");
                o.push_str(&fmt_block(body, indent + 1));
                o.push('\n');
            }
            if let Some(def) = default_arm {
                o.push_str(&inner);
                o.push_str("default => ");
                o.push_str(&fmt_block(def, indent + 1));
                o.push('\n');
            }
            o.push_str(&INDENT.repeat(indent));
            o.push('}');
            o
        }
    }
}

/// Precedence (higher binds tighter). Used to omit redundant parens.
fn bin_prec(op: &BinOp) -> u8 {
    match op {
        BinOp::Or => 1,
        BinOp::And => 2,
        BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => 3,
        BinOp::Add | BinOp::Sub | BinOp::BitOr | BinOp::BitXor => 4,
        BinOp::Mul
        | BinOp::Div
        | BinOp::Mod
        | BinOp::Shl
        | BinOp::Shr
        | BinOp::BitAnd
        | BinOp::BitClear => 5,
    }
}

fn fmt_expr(e: &Expr, parent_prec: u8) -> String {
    match e {
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            format!(
                "if {} {} else {}",
                fmt_expr(cond, 0),
                fmt_block(then_block, 0),
                fmt_block(else_block, 0)
            )
        }
        Expr::Int(n) => n.to_string(),
        Expr::Float(n) => {
            let s = n.to_string();
            if s.contains('.') || s.contains('e') || s.contains('E') {
                s
            } else {
                format!("{s}.0")
            }
        }
        Expr::Bool(b) => b.to_string(),
        Expr::String(s) => format!("\"{}\"", escape_str(s)),
        Expr::Ident(s) => s.clone(),
        Expr::Call { callee, args } => {
            let a: Vec<_> = args.iter().map(|x| fmt_expr(x, 0)).collect();
            format!("{}({})", fmt_expr(callee, 0), a.join(", "))
        }
        Expr::Binary { op, left, right } => {
            let p = bin_prec(op);
            let l = fmt_expr(left, p);
            let r = fmt_expr(right, p + 1); // left-assoc: right side needs higher
            let s = format!("{} {} {}", l, fmt_binop(op), r);
            if p < parent_prec {
                format!("({s})")
            } else {
                s
            }
        }
        Expr::Unary { op, expr } => {
            let inner = fmt_expr(expr, 6);
            let s = match op {
                UnaryOp::Neg => format!("-{inner}"),
                UnaryOp::Not => format!("!{inner}"),
                UnaryOp::BitNot => format!("^{inner}"),
            };
            if parent_prec > 5 {
                format!("({s})")
            } else {
                s
            }
        }
        Expr::Array(elems) => {
            let a: Vec<_> = elems.iter().map(|x| fmt_expr(x, 0)).collect();
            format!("[{}]", a.join(", "))
        }
        Expr::Tuple(elems) => {
            let a: Vec<_> = elems.iter().map(|x| fmt_expr(x, 0)).collect();
            if elems.len() == 1 {
                format!("({},)", a[0])
            } else {
                format!("({})", a.join(", "))
            }
        }
        Expr::ChanOpen { elem, cap } => {
            format!("chan_open[{}]({})", fmt_type(elem), fmt_expr(cap, 0))
        }
        Expr::Convert { ty, args } => {
            let a: Vec<_> = args.iter().map(|x| fmt_expr(x, 0)).collect();
            format!("{}({})", fmt_type(ty), a.join(", "))
        }
        Expr::Make { ty, len, cap } => match (len, cap) {
            (None, None) => format!("make({})", fmt_type(ty)),
            (Some(l), None) => format!("make({}, {})", fmt_type(ty), fmt_expr(l, 0)),
            (Some(l), Some(c)) => {
                format!(
                    "make({}, {}, {})",
                    fmt_type(ty),
                    fmt_expr(l, 0),
                    fmt_expr(c, 0)
                )
            }
            (None, Some(c)) => format!("make({}, {})", fmt_type(ty), fmt_expr(c, 0)),
        },
        Expr::Index { base, index } => {
            format!("{}[{}]", fmt_expr(base, 0), fmt_expr(index, 0))
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            let l = low.as_ref().map(|e| fmt_expr(e, 0)).unwrap_or_default();
            let h = high.as_ref().map(|e| fmt_expr(e, 0)).unwrap_or_default();
            match max {
                Some(m) => format!("{}[{l}:{h}:{}]", fmt_expr(base, 0), fmt_expr(m, 0)),
                None => format!("{}[{l}:{h}]", fmt_expr(base, 0)),
            }
        }
        Expr::Field { base, field } => format!("{}.{}", fmt_expr(base, 0), field),
        Expr::StructLit {
            name,
            fields,
            update,
        } => {
            let mut parts: Vec<_> = fields
                .iter()
                .map(|(n, e)| format!("{n}: {}", fmt_expr(e, 0)))
                .collect();
            if let Some(base) = update {
                parts.push(format!("..{}", fmt_expr(base, 0)));
            }
            if parts.is_empty() {
                format!("{name} {{}}")
            } else {
                format!("{name} {{ {} }}", parts.join(", "))
            }
        }
        Expr::StructLitPos { name, values } => {
            let parts: Vec<_> = values.iter().map(|e| fmt_expr(e, 0)).collect();
            format!("{name}{{{}}}", parts.join(", "))
        }
        Expr::Method {
            receiver,
            method,
            args,
        } => {
            let a: Vec<_> = args.iter().map(|x| fmt_expr(x, 0)).collect();
            format!("{}.{}({})", fmt_expr(receiver, 0), method, a.join(", "))
        }
        Expr::Match { scrutinee, arms } => {
            let mut o = format!("match {} {{\n", fmt_expr(scrutinee, 0));
            for arm in arms {
                o.push_str(INDENT);
                o.push_str(&fmt_pattern(&arm.pattern));
                o.push_str(" => ");
                // Prefer block body on its own line style when body is Block
                match &arm.body {
                    Expr::Block(b) => {
                        o.push_str(&fmt_block(b, 1));
                    }
                    other => {
                        o.push_str(&fmt_expr(other, 0));
                    }
                }
                o.push('\n');
            }
            o.push('}');
            o
        }
        Expr::Lambda { params, body } => {
            let p = params.join(", ");
            match body.as_ref() {
                Expr::Block(b) => format!("fn({p}) {}", fmt_block(b, 0)),
                other => format!("fn({p}) {{ {} }}", fmt_expr(other, 0)),
            }
        }
        Expr::Try(inner) => format!("{}?", fmt_expr(inner, 0)),
        Expr::Block(b) => fmt_block(b, 0),
        Expr::Kick { crew, expr } => format!("{crew}.kick({})", fmt_expr(expr, 0)),
        Expr::Join(j) => format!("{}.join()", fmt_expr(j, 0)),
        Expr::Fan { collection, mapper } => {
            format!("fan({}, {})", fmt_expr(collection, 0), fmt_expr(mapper, 0))
        }
    }
}

fn fmt_pattern(p: &Pattern) -> String {
    match p {
        Pattern::Wildcard => "_".into(),
        Pattern::Ident(s) => s.clone(),
        Pattern::Variant { name, bindings } => {
            if bindings.is_empty() {
                name.clone()
            } else {
                let parts: Vec<_> = bindings.iter().map(fmt_pattern).collect();
                format!("{name}({})", parts.join(", "))
            }
        }
        Pattern::Literal(e) => fmt_expr(e, 0),
        Pattern::Or(ps) => {
            let parts: Vec<_> = ps.iter().map(fmt_pattern).collect();
            parts.join(" | ")
        }
        Pattern::Tuple(ps) => {
            let parts: Vec<_> = ps.iter().map(fmt_pattern).collect();
            format!("({})", parts.join(", "))
        }
        Pattern::Struct { name, fields } => {
            let parts: Vec<_> = fields
                .iter()
                .map(|(f, pat)| match pat {
                    Pattern::Ident(id) if id == f => f.clone(),
                    _ => format!("{f}: {}", fmt_pattern(pat)),
                })
                .collect();
            format!("{name} {{ {} }}", parts.join(", "))
        }
    }
}

fn fmt_binop(op: &BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Mod => "%",
        BinOp::Eq => "==",
        BinOp::Ne => "!=",
        BinOp::Lt => "<",
        BinOp::Le => "<=",
        BinOp::Gt => ">",
        BinOp::Ge => ">=",
        BinOp::And => "&&",
        BinOp::Or => "||",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "^",
        BinOp::BitClear => "&^",
        BinOp::Shl => "<<",
        BinOp::Shr => ">>",
    }
}

fn escape_str(s: &str) -> String {
    let mut o = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '\\' => o.push_str("\\\\"),
            '"' => o.push_str("\\\""),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            c if c.is_control() => o.push_str(&format!("\\u{{{:x}}}", c as u32)),
            c => o.push(c),
        }
    }
    o
}

/// Unified diff (minimal) between original and formatted.
pub fn unified_diff(path: &str, original: &str, formatted: &str) -> String {
    let mut out = String::new();
    out.push_str(&format!("diff {path}\n"));
    out.push_str(&format!("--- a/{path}\n"));
    out.push_str(&format!("+++ b/{path}\n"));
    let a: Vec<&str> = original.lines().collect();
    let b: Vec<&str> = formatted.lines().collect();
    // Simple line-oriented LCS-free hunk: show all differing lines (good enough for fmt -d).
    let max = a.len().max(b.len());
    let mut hunk = String::new();
    let mut any = false;
    for i in 0..max {
        let left = a.get(i).copied();
        let right = b.get(i).copied();
        match (left, right) {
            (Some(x), Some(y)) if x == y => {
                hunk.push_str(&format!(" {x}\n"));
            }
            (Some(x), Some(y)) => {
                any = true;
                hunk.push_str(&format!("-{x}\n"));
                hunk.push_str(&format!("+{y}\n"));
            }
            (Some(x), None) => {
                any = true;
                hunk.push_str(&format!("-{x}\n"));
            }
            (None, Some(y)) => {
                any = true;
                hunk.push_str(&format!("+{y}\n"));
            }
            (None, None) => {}
        }
    }
    if any {
        out.push_str(&format!("@@ fmt @@\n{hunk}"));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn parse(src: &str) -> Program {
        let tokens = Lexer::new(src).tokenize().expect("lex");
        Parser::new(tokens).parse_program().expect("parse")
    }

    #[test]
    fn fmt_idempotent_simple() {
        let src = "fn main() {\nlet x = 1+2\nprint(x)\n}\n";
        let once = format_program(&parse(src));
        let twice = format_program(&parse(&once));
        assert_eq!(once, twice, "fmt not idempotent:\n{once}");
        assert!(once.contains("1 + 2"));
        assert!(once.ends_with('\n'));
    }

    #[test]
    fn fmt_sorts_imports() {
        let src = r#"
import "./b.mko"
import "./a.mko" as a
fn main() {
}
"#;
        let out = format_program(&parse(src));
        // Preferred Mako keyword + alias spelling
        assert!(out.contains("pull ("));
        let a = out.find("\"./a.mko\" as a").unwrap();
        let b = out.find("\"./b.mko\"").unwrap();
        assert!(a < b);
    }

    #[test]
    fn fmt_grouped_import_roundtrip() {
        let src = r#"
pull (
    "strings"
    "path"
)
fn main() {
}
"#;
        let once = format_program(&parse(src));
        let twice = format_program(&parse(&once));
        assert_eq!(once, twice);
        assert!(once.contains("pull ("));
        assert!(once.contains("\"path\""));
        assert!(once.contains("\"strings\""));
    }

    #[test]
    fn fmt_pack_and_pull_preferred() {
        let src = r#"
package lib
import "./x.mko" as x
fn main() {}
"#;
        let out = format_program(&parse(src));
        assert!(out.contains("pack lib"));
        assert!(out.contains("pull \"./x.mko\" as x"));
        assert!(!out.contains("package "));
        assert!(!out.contains("import "));
    }

    #[test]
    fn fmt_match_and_lambda() {
        let src = r#"
fn main() {
    let f = fn(x) { x }
    match f(1) {
        0 => 0
        _ => 1
    }
}
"#;
        let once = format_program(&parse(src));
        let twice = format_program(&parse(&once));
        assert_eq!(once, twice);
        assert!(once.contains("fn(x)"));
        assert!(once.contains("match "));
    }
}

