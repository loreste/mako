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
                extras.extend(expand_actor(actor));
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
    for s in &mut block.stmts { resolve_embed_stmt(s, base); }
}
fn resolve_embed_stmt(stmt: &mut Stmt, base: Option<&std::path::Path>) {
    match stmt {
        Stmt::Let { init, .. } | Stmt::Assign { value: init, .. } => resolve_embed_expr(init, base),
        Stmt::Expr(e) | Stmt::Return(Some(e)) => resolve_embed_expr(e, base),
        Stmt::If { cond, then_block, else_block, .. } => {
            resolve_embed_expr(cond, base);
            resolve_embed_block(then_block, base);
            if let Some(eb) = else_block { resolve_embed_block(eb, base); }
        }
        Stmt::While { body, .. } | Stmt::Defer { body } | Stmt::Unsafe { body } => resolve_embed_block(body, base),
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
                        Ok(c) => { *expr = Expr::String(c); return; }
                        Err(e) => { eprintln!("embed: {}: {e}", full.display()); std::process::exit(1); }
                    }
                }
            }
            if name == "embed_bytes" && args.len() == 1 {
                if let Expr::String(path) = &args[0] {
                    let full = base.map(|b| b.join(path)).unwrap_or_else(|| path.into());
                    match std::fs::read(&full) {
                        Ok(b) => { *expr = Expr::Array(b.into_iter().map(|v| Expr::Int(v as i64)).collect()); return; }
                        Err(e) => { eprintln!("embed_bytes: {}: {e}", full.display()); std::process::exit(1); }
                    }
                }
            }
        }
        resolve_embed_expr(callee, base);
        for a in args { resolve_embed_expr(a, base); }
    }
}


fn desugar_if_let_block(block: &mut Block) {
    let stmts = std::mem::take(&mut block.stmts);
    block.stmts = stmts
        .into_iter()
        .map(|s| desugar_if_let_stmt(s))
        .collect();
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
            mutable: false, variadic: false })
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
                mutable: false, variadic: false }],
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
        Expr::Unary { expr: e, .. } | Expr::Try(e) | Expr::Join(e) => {
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
        Expr::Array(xs) | Expr::Tuple(xs) => {
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
        _ => {}
    }
}

fn rewrite_self_block(b: &mut Block, state_name: &str) {
    for s in &mut b.stmts {
        match s {
            Stmt::Let { init, .. } | Stmt::Assign { value: init, .. } => {
                rewrite_self_fields(init, state_name);
            }
            Stmt::Expr(e) | Stmt::Return(Some(e)) => rewrite_self_fields(e, state_name),
            Stmt::If {
                cond,
                then_block,
                else_block,
                ..
            } => {
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
            Stmt::FieldAssign { base, value, .. } => {
                // `self.n = …` → FieldAssign { base: Ident("self"), field: "n" }
                if matches!(base, Expr::Ident(s) if s == "self") {
                    *base = Expr::Ident(state_name.into());
                } else {
                    rewrite_self_fields(base, state_name);
                }
                rewrite_self_fields(value, state_name);
            }
            _ => {}
        }
    }
}

/// Rewrite  in actor receive arms to  (next message).
fn rewrite_return_to_continue(stmts: &mut Vec<Stmt>) {
    for s in stmts.iter_mut() {
        match s {
            Stmt::Return(None) => {
                *s = Stmt::Continue(None);
            }
            Stmt::Return(Some(_)) => {
                // return <expr> → evaluate expr (for side effects), then continue
                let expr = if let Stmt::Return(Some(e)) = std::mem::replace(s, Stmt::Continue(None)) {
                    e
                } else {
                    unreachable!()
                };
                // Replace with: { expr; continue; }
                // But we can't create a block stmt easily, so just drop the value and continue
                *s = Stmt::Continue(None);
                // ponytail: if the return value matters, the user should use a local
            }
            Stmt::If { then_block, else_block, .. } => {
                rewrite_return_to_continue(&mut then_block.stmts);
                if let Some(eb) = else_block {
                    rewrite_return_to_continue(&mut eb.stmts);
                }
            }
            Stmt::While { body, .. } | Stmt::For { body, .. } => {
                rewrite_return_to_continue(&mut body.stmts);
            }
            _ => {}
        }
    }
}

fn expand_actor(actor: ActorDef) -> Vec<Item> {
    let name = &actor.name;
    let mut items = Vec::new();
    let state_ty = format!("{name}_State");
    let has_state = !actor.fields.is_empty();

    // Owned state struct (if any fields).
    if has_state {
        items.push(Item::Struct(StructDef {
            name: state_ty.clone(),
            type_params: Vec::new(),
            fields: actor.fields.clone(),
            derives: Vec::new(),
            exported: false,
            source_file: None,
        }));
    }

    // Message constructors: Session_Invite() -> pack(tag, 0)
    // or Session_Inc(delta) -> pack(tag, delta) for receive Inc(delta).
    // Determine if this actor needs envelope structs (any non-int or multi-param receives).
    let needs_envelope = actor.receives.iter().any(|arm| {
        arm.params.len() > 1
            || arm.params.iter().any(|(_, ty)| !matches!(ty, TypeExpr::Named(n) if n == "int" || n == "int64"))
    });

    // Generate per-message envelope struct if needed.
    if needs_envelope {
        for arm in &actor.receives {
            if !arm.params.is_empty() {
                let env_name = format!("{name}_{}_Env", arm.message);
                items.push(Item::Struct(StructDef {
                    name: env_name,
                    type_params: Vec::new(),
                    fields: arm.params.iter().map(|(n, ty)| (n.clone(), ty.clone(), None)).collect(),
                    derives: Vec::new(),
                    exported: false,
                    source_file: None,
                }));
            }
        }
    }

    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let (params, pack_args) = if arm.params.is_empty() {
            (vec![], vec![Expr::Int(tag), Expr::Int(0)])
        } else if !needs_envelope && arm.params.len() == 1 {
            // Legacy int-only path
            let (ref pname, _) = arm.params[0];
            (
                vec![Param { name: pname.clone(), ty: TypeExpr::Named("int".into()), mutable: false, variadic: false }],
                vec![Expr::Int(tag), Expr::Ident(pname.clone())],
            )
        } else {
            // Envelope path: construct struct, box pointer
            let env_name = format!("{name}_{}_Env", arm.message);
            let fn_params: Vec<Param> = arm.params.iter().map(|(n, ty)| Param {
                name: n.clone(), ty: ty.clone(), mutable: false, variadic: false,
            }).collect();
            let lit_fields: Vec<(String, Expr)> = arm.params.iter().map(|(n, _)| (n.clone(), Expr::Ident(n.clone()))).collect();
            let construct = Expr::StructLit { name: env_name, fields: lit_fields, update: None };
            (
                fn_params,
                vec![Expr::Int(tag), Expr::Call {
                    callee: Box::new(Expr::Ident("actor_box_payload".into())),
                    args: vec![construct],
                }],
            )
        };
        items.push(Item::Fn(FnDef {
            type_bounds: std::collections::HashMap::new(),
            name: format!("{name}_{}", arm.message),
            type_params: Vec::new(),
            params,
            ret: Some(TypeExpr::Named("int".into())),
            body: Block {
                stmts: vec![Stmt::Return(Some(Expr::Call {
                    callee: Box::new(Expr::Ident("actor_pack".into())),
                    args: pack_args,
                }))],
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

    // Session_spawn(...ctor_args) -> chan[int] (default mailbox 16)
    let ctor_fn_params: Vec<Param> = actor.ctor_params.iter().map(|(n, ty)| Param {
        name: n.clone(), ty: ty.clone(), mutable: false, variadic: false,
    }).collect();
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
            stmts: vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident("actor_spawn".into())),
                args: vec![Expr::Int(16)],
            }))],
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
        mutable: false, variadic: false,
    }];
    spawn_cap_params.extend(ctor_fn_params.clone());
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
            stmts: vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident("actor_spawn".into())),
                args: vec![Expr::Ident("__cap".into())],
            }))],
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
                mutable: false, variadic: false },
            Param {
                name: "__tag".into(),
                ty: TypeExpr::Named("int".into()),
                mutable: false, variadic: false },
        ],
        ret: Some(TypeExpr::Named("bool".into())),
        body: Block {
            stmts: vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident("actor_send".into())),
                args: vec![Expr::Ident("__mbox".into()), Expr::Ident("__tag".into())],
            }))],
            source_lines: Box::default(),
        },
        exported: false,
        is_const: false,
        is_live: false,
        stability: crate::ast::ApiStability::Unspecified,
        contracts: vec![],
        source_file: None,
    }));

    // Session_loop(mbox) — message dispatch (+ optional state)
    let mut loop_stmts: Vec<Stmt> = vec![Stmt::Let {
        name: "__run".into(),
        mutable: true,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Int(1),
    }];

    if has_state {
        // Zero/defaults via partial lit + field defaults, or empty positional.
        let mut lit_fields = Vec::new();
        for (fname, _, def) in &actor.fields {
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

    let mut while_body: Vec<Stmt> = vec![
        Stmt::Let {
            name: "__m".into(),
            mutable: false,
            ownership: Ownership::None,
            ty: None,
            init: Expr::Call {
                callee: Box::new(Expr::Ident("actor_recv".into())),
                args: vec![Expr::Ident("__mbox".into())],
            },
        },
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

    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let mut arm_block = arm.body.clone();
        if has_state {
            rewrite_self_block(&mut arm_block, "__st");
        }
        let mut arm_stmts = Vec::new();
        if !arm.params.is_empty() {
            if !needs_envelope && arm.params.len() == 1 {
                // Legacy int path
                let (ref pname, _) = arm.params[0];
                arm_stmts.push(Stmt::Let {
                    name: pname.clone(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: Some(TypeExpr::Named("int".into())),
                    init: Expr::Ident("__pl".into()),
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
        arm_stmts.extend(arm_block.stmts);
        // Convention: message named Bye / Stop ends the loop
        if arm.message == "Bye" || arm.message == "Stop" {
            arm_stmts.push(Stmt::Assign {
                name: "__run".into(),
                value: Expr::Int(0),
            });
        }
        while_body.push(Stmt::If {
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
            else_block: None,
        });
    }

    // If state has a field `n` or `count`, return it on exit; else 0.
    let ret_expr = if has_state {
        if let Some((fname, _, _)) = actor
            .fields
            .iter()
            .find(|(n, _, _)| n == "n" || n == "count" || n == "value" || n == "result" || n == "total" || n == "state")
        {
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
        label: None,
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
            mutable: false, variadic: false }],
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
