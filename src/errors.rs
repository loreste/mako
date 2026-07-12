//! Typed error enums for `Result[T, E]`.
//!
//! When `E` is a user `enum`, Err packs tag/payload via `mako_err_enum` and
//! `match Err(e)` reconstructs the enum for nested match.
//!
//! Runtime shape: `MakoResultInt` in `mako_rt.h` (`err_kind`, `err_tag`, …).
//! Codegen tracks `Result[_, Enum]` via `fn_result_err_enum` / `result_err_enums`.

use crate::ast::TypeExpr;

/// If `ty` is `Result[_, E]` and `E` is a named non-string type, return `E`.
pub fn result_err_type_name(ty: &TypeExpr) -> Option<&str> {
    match ty {
        TypeExpr::Generic(n, args) if n == "Result" && args.len() == 2 => {
            if let TypeExpr::Named(en) = &args[1] {
                if en != "string" {
                    return Some(en.as_str());
                }
            }
            None
        }
        _ => None,
    }
}

/// C type name for a user enum error payload (`MakoEnum_IoError`).
pub fn result_err_enum_c(ty: &TypeExpr, is_known_enum: impl Fn(&str) -> bool) -> Option<String> {
    let en = result_err_type_name(ty)?;
    if is_known_enum(en) {
        Some(format!("MakoEnum_{en}"))
    } else {
        None
    }
}

/// True when Err payload should use string path (`mako_err_int`).
#[allow(dead_code)]
pub fn is_string_err(ty: &TypeExpr) -> bool {
    matches!(
        ty,
        TypeExpr::Generic(n, args)
            if n == "Result"
                && args.len() == 2
                && matches!(&args[1], TypeExpr::Named(e) if e == "string")
    ) || matches!(ty, TypeExpr::Named(n) if n == "string")
}
