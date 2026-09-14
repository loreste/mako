//! Control-flow-aware non-lexical lifetime (NLL) helpers for `hold` / `share`.
//!
//! ## CFG model (function body)
//!
//! Statements form a graph of **basic-block regions**:
//! - Straight-line sequences until `if` / `match` / `while` / `for` / `return` /
//!   `break` / `continue`.
//! - **If**: edge to then; edge to else (or skip-then when no else).
//! - **Match**: one edge per arm.
//! - **Loop**: header → body; body fallthrough / `continue` → header; `break` →
//!   exit; condition-false → exit (unless cond is const-`true`).
//! - **Return** / **break** / **continue**: no fall-through to the next stmt.
//!
//! ## Dataflow
//!
//! - **Moves** (`hold`): gen on consume; kill on reassign into a live hold.
//!   Join at merges = **union** (moved if any reachable arm moved).
//!   Diverging arms do not contribute to the join.
//! - **Shares**: mid-scope last-use ends borrows; join keeps a borrow only if
//!   still live on every *reachable* fall-through arm (if/match), or union when
//!   either path may still borrow (if-without-else).
//! - **Loops**: fixpoint only when some path can reach the header again
//!   (`continue` or body fall-through). Always-`break` / always-`return` bodies
//!   skip the second iteration check (avoids false use-after-move).
//! - **Const bool**: `if false` / `while false` prune dead edges; `while true`
//!   has no condition-false exit.
//!
//! Wired from `TypeChecker` in `mod.rs` (same diagnostics / Copy rules).

use crate::ast::{BinOp, Block, Expr, Stmt, UnaryOp};

/// Fold simple boolean conditions for CFG edge pruning.
pub fn const_bool(expr: &Expr) -> Option<bool> {
    match expr {
        Expr::Bool(b) => Some(*b),
        Expr::Unary {
            op: UnaryOp::Not,
            expr,
        } => const_bool(expr).map(|b| !b),
        Expr::Binary { op, left, right } => match op {
            BinOp::And => match (const_bool(left), const_bool(right)) {
                (Some(false), _) | (_, Some(false)) => Some(false),
                (Some(true), Some(true)) => Some(true),
                (Some(true), r) => r,
                (l, Some(true)) => l,
                _ => None,
            },
            BinOp::Or => match (const_bool(left), const_bool(right)) {
                (Some(true), _) | (_, Some(true)) => Some(true),
                (Some(false), Some(false)) => Some(false),
                (Some(false), r) => r,
                (l, Some(false)) => l,
                _ => None,
            },
            BinOp::Eq => match (left.as_ref(), right.as_ref()) {
                (Expr::Bool(a), Expr::Bool(b)) => Some(a == b),
                (Expr::Int(a), Expr::Int(b)) => Some(a == b),
                // `0 == 1` style folds already covered; also fold `x == x` as true
                // for identical idents (reduces false-positive move joins).
                (Expr::Ident(a), Expr::Ident(b)) if a == b => Some(true),
                // Multi-label CFG: fold `true == true` style after unary/parens
                // via recursive const_bool on sides.
                (l, r) => match (const_bool(l), const_bool(r)) {
                    (Some(a), Some(b)) => Some(a == b),
                    _ => None,
                },
            },
            BinOp::Ne => match (left.as_ref(), right.as_ref()) {
                (Expr::Bool(a), Expr::Bool(b)) => Some(a != b),
                (Expr::Int(a), Expr::Int(b)) => Some(a != b),
                (Expr::Ident(a), Expr::Ident(b)) if a == b => Some(false),
                (l, r) => match (const_bool(l), const_bool(r)) {
                    (Some(a), Some(b)) => Some(a != b),
                    _ => None,
                },
            },
            BinOp::Lt => match (left.as_ref(), right.as_ref()) {
                (Expr::Int(a), Expr::Int(b)) => Some(a < b),
                _ => None,
            },
            BinOp::Le => match (left.as_ref(), right.as_ref()) {
                (Expr::Int(a), Expr::Int(b)) => Some(a <= b),
                _ => None,
            },
            BinOp::Gt => match (left.as_ref(), right.as_ref()) {
                (Expr::Int(a), Expr::Int(b)) => Some(a > b),
                _ => None,
            },
            BinOp::Ge => match (left.as_ref(), right.as_ref()) {
                (Expr::Int(a), Expr::Int(b)) => Some(a >= b),
                _ => None,
            },
            _ => None,
        },
        _ => None,
    }
}

/// True if every path through `stmts` ends in return/break/continue (no fall-through).
pub fn stmts_always_diverges(stmts: &[Stmt]) -> bool {
    if stmts.is_empty() {
        return false;
    }
    match stmts.last() {
        Some(Stmt::Return(_)) | Some(Stmt::Break(_)) | Some(Stmt::Continue(_)) => true,
        Some(Stmt::If {
            init: _,
            cond,
            then_block,
            else_block,
        }) => {
            // A constant-condition `if` only takes one arm (this is how the
            // `if init; cond` desugar's `if true { … }` scope stays transparent).
            match const_bool(cond) {
                Some(true) => block_always_diverges(then_block),
                Some(false) => else_block
                    .as_ref()
                    .map(block_always_diverges)
                    .unwrap_or(false),
                None => {
                    let then_d = block_always_diverges(then_block);
                    match else_block {
                        Some(eb) => then_d && block_always_diverges(eb),
                        None => false,
                    }
                }
            }
        }
        Some(Stmt::While { .. }) | Some(Stmt::For { .. }) => false,
        _ => false,
    }
}

pub fn block_always_diverges(block: &Block) -> bool {
    stmts_always_diverges(&block.stmts)
}

pub fn expr_always_diverges(expr: &Expr) -> bool {
    match expr {
        Expr::Block(b) => block_always_diverges(b),
        _ => false,
    }
}

/// Reachable `continue` in this stmt list (nested loops' continues do not count —
/// they target the inner loop).
pub fn stmts_has_continue(stmts: &[Stmt]) -> bool {
    for stmt in stmts {
        match stmt {
            Stmt::Continue(_) => return true,
            Stmt::Break(_) | Stmt::Return(_) => return false,
            Stmt::If {
                then_block,
                else_block,
                ..
            } => {
                let t = stmts_has_continue(&then_block.stmts);
                let e = else_block
                    .as_ref()
                    .map(|b| stmts_has_continue(&b.stmts))
                    .unwrap_or(false);
                if t || e {
                    return true;
                }
                // Both arms diverge ⇒ later stmts unreachable.
                if block_always_diverges(then_block)
                    && else_block
                        .as_ref()
                        .map(block_always_diverges)
                        .unwrap_or(false)
                {
                    return false;
                }
            }
            // Nested loop: its continue/break stay inside; fall through after.
            Stmt::While { .. } | Stmt::For { .. } => {}
            Stmt::Defer { body }
            | Stmt::Crew { body, .. }
            | Stmt::Arena { body, .. }
            | Stmt::Unsafe { body } => {
                if stmts_has_continue(&body.stmts) {
                    return true;
                }
            }
            Stmt::Select {
                arms, default_arm, ..
            } => {
                for (_, body) in arms {
                    if stmts_has_continue(&body.stmts) {
                        return true;
                    }
                }
                if let Some(d) = default_arm {
                    if stmts_has_continue(&d.stmts) {
                        return true;
                    }
                }
            }
            _ => {}
        }
    }
    false
}

/// True if some path from the start of `body` can return to the loop header
/// (fall through the body end, or `continue`). Used to decide whether a
/// loop-carried second pass is required.
pub fn loop_body_may_reach_header(body: &Block) -> bool {
    if stmts_has_continue(&body.stmts) {
        return true;
    }
    // Fall-through to end of body ⇒ next iteration when cond is still true.
    !block_always_diverges(body)
}
