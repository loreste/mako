use std::collections::HashMap;

use super::Type;

/// Return builtin signatures in a stable order for LSP completion and docs.
///
/// This is the first extraction point for the builtin registry. The large
/// registration table still lives in `TypeChecker::new`; future changes should
/// move coherent groups into this module instead of adding more ad hoc entries
/// directly to `mod.rs`.
pub fn signatures_from(fns: &HashMap<String, Type>) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = fns
        .iter()
        .map(|(name, ty)| (name.clone(), ty.display()))
        .collect();
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}
