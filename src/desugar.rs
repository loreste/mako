//! Desugar high-level syntax into core AST (actors, derive, `on` methods, etc.).

use crate::ast::*;

/// Expand `actor` / `#[derive(json)]` / `on Type { … }` into helper functions.
pub fn desugar(mut program: Program) -> Program {
    let mut extras: Vec<Item> = Vec::new();
    let mut kept: Vec<Item> = Vec::new();

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
                    extras.extend(expand_json_derive(&s));
                }
                kept.push(Item::Struct(s));
            }
            other => kept.push(other),
        }
    }

    kept.extend(extras);
    Program { items: kept }
}

/// `on Point { fn distance(self) -> int { … } }` → `fn Point_distance(self: Point, …)`
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
        method.name = format!("{}_{}", on.ty, method.name);
        method.exported = on.exported;
        // Methods from `on` are concrete (no free type params unless user wrote them).
        items.push(Item::Fn(method));
    }
    items
}

/// `Person_to_json(...)` plus `Person_<field>_from_json(j)` extractors.
fn expand_json_derive(s: &StructDef) -> Vec<Item> {
    let mut items = Vec::new();

    let fname = format!("{}_to_json", s.name);
    let params: Vec<Param> = s
        .fields
        .iter()
        .map(|(n, ty, _)| Param {
            name: n.clone(),
            ty: ty.clone(),
            mutable: false,
        })
        .collect();

    let mut pieces = s
        .fields
        .iter()
        .filter_map(|(field, ty, _)| json_field_expr(field, ty));
    let json_expr = pieces
        .next()
        .map(|first| pieces.fold(first, |acc, next| call("json_merge", vec![acc, next])))
        .unwrap_or_else(|| Expr::String("{}".into()));

    items.push(Item::Fn(FnDef {
        name: fname,
        type_params: Vec::new(),
        params,
        ret: Some(TypeExpr::Named("string".into())),
        body: Block {
            stmts: vec![Stmt::Return(Some(json_expr))],
        },
        exported: s.exported,
    is_const: false,
        stability: crate::ast::ApiStability::Unspecified,
    }));

    for (fname_field, ty, _) in &s.fields {
        let (callee, ret_ty) = match ty {
            TypeExpr::Named(n) if n == "string" => ("json_get_string", "string"),
            TypeExpr::Named(n) if n == "int" => ("json_get_int", "int"),
            _ => continue,
        };
        items.push(Item::Fn(FnDef {
            name: format!("{}_{}_from_json", s.name, fname_field),
            type_params: Vec::new(),
            params: vec![Param {
                name: "j".into(),
                ty: TypeExpr::Named("string".into()),
                mutable: false,
            }],
            ret: Some(TypeExpr::Named(ret_ty.into())),
            body: Block {
                stmts: vec![Stmt::Return(Some(Expr::Call {
                    callee: Box::new(Expr::Ident(callee.into())),
                    args: vec![Expr::Ident("j".into()), Expr::String(fname_field.clone())],
                }))],
            },
            exported: s.exported,
        is_const: false,
            stability: crate::ast::ApiStability::Unspecified,
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

fn json_field_expr(field: &str, ty: &TypeExpr) -> Option<Expr> {
    match ty {
        TypeExpr::Named(n) if n == "string" => Some(call(
            "json_object",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        TypeExpr::Named(n) if n == "int" => Some(call(
            "json_i",
            vec![Expr::String(field.into()), Expr::Ident(field.into())],
        )),
        _ => None,
    }
}

fn expand_actor(actor: ActorDef) -> Vec<Item> {
    let name = &actor.name;
    let mut items = Vec::new();

    // Tag helpers: Session_Invite() -> 1, etc.
    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        items.push(Item::Fn(FnDef {
            name: format!("{name}_{}", arm.message),
            type_params: Vec::new(),
            params: vec![],
            ret: Some(TypeExpr::Named("int".into())),
            body: Block {
                stmts: vec![Stmt::Return(Some(Expr::Int(tag)))],
            },
            exported: false,
        is_const: false,
            stability: crate::ast::ApiStability::Unspecified,
        }));
    }

    // Session_spawn() -> chan[int]
    items.push(Item::Fn(FnDef {
        name: format!("{name}_spawn"),
        type_params: Vec::new(),
        params: vec![],
        ret: Some(TypeExpr::Generic(
            "chan".into(),
            vec![TypeExpr::Named("int".into())],
        )),
        body: Block {
            stmts: vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident("actor_spawn".into())),
                args: vec![Expr::Int(16)],
            }))],
        },
        exported: false,
    is_const: false,
        stability: crate::ast::ApiStability::Unspecified,
    }));

    // Session_send(mbox, tag) -> bool
    items.push(Item::Fn(FnDef {
        name: format!("{name}_send"),
        type_params: Vec::new(),
        params: vec![
            Param {
                name: "__mbox".into(),
                ty: TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]),
                mutable: false,
            },
            Param {
                name: "__tag".into(),
                ty: TypeExpr::Named("int".into()),
                mutable: false,
            },
        ],
        ret: Some(TypeExpr::Named("bool".into())),
        body: Block {
            stmts: vec![Stmt::Return(Some(Expr::Call {
                callee: Box::new(Expr::Ident("actor_send".into())),
                args: vec![Expr::Ident("__mbox".into()), Expr::Ident("__tag".into())],
            }))],
        },
        exported: false,
    is_const: false,
        stability: crate::ast::ApiStability::Unspecified,
    }));

    // Session_loop(mbox) — message dispatch
    let mut loop_stmts: Vec<Stmt> = vec![Stmt::Let {
        name: "__run".into(),
        mutable: true,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Int(1),
    }];

    let mut while_body: Vec<Stmt> = vec![Stmt::Let {
        name: "__m".into(),
        mutable: false,
        ownership: Ownership::None,
        ty: None,
        init: Expr::Call {
            callee: Box::new(Expr::Ident("actor_recv".into())),
            args: vec![Expr::Ident("__mbox".into())],
        },
    }];

    for (i, arm) in actor.receives.iter().enumerate() {
        let tag = (i + 1) as i64;
        let mut arm_stmts = arm.body.stmts.clone();
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
                left: Box::new(Expr::Ident("__m".into())),
                right: Box::new(Expr::Int(tag)),
            },
            then_block: Block { stmts: arm_stmts },
            else_block: None,
        });
    }

    loop_stmts.push(Stmt::While {
        label: None,
        cond: Expr::Binary {
            op: BinOp::Eq,
            left: Box::new(Expr::Ident("__run".into())),
            right: Box::new(Expr::Int(1)),
        },
        body: Block { stmts: while_body },
    });
    loop_stmts.push(Stmt::Expr(Expr::Call {
        callee: Box::new(Expr::Ident("actor_stop".into())),
        args: vec![Expr::Ident("__mbox".into())],
    }));
    loop_stmts.push(Stmt::Return(Some(Expr::Int(0))));

    items.push(Item::Fn(FnDef {
        name: format!("{name}_loop"),
        type_params: Vec::new(),
        params: vec![Param {
            name: "__mbox".into(),
            ty: TypeExpr::Generic("chan".into(), vec![TypeExpr::Named("int".into())]),
            mutable: false,
        }],
        ret: Some(TypeExpr::Named("int".into())),
        body: Block { stmts: loop_stmts },
        exported: false,
    is_const: false,
        stability: crate::ast::ApiStability::Unspecified,
    }));

    items
}
