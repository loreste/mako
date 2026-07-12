//! Checked integer arithmetic — overflow modes and CLI mapping.
//!
//! Runtime: [`runtime/mako_overflow.h`](../../runtime/mako_overflow.h)
//! (`mako_checked_add_i64`, `mako_add_i64` trap path, etc.).
//!
//! Codegen emits `mako_add_i64` / `sub` / `mul` for integer `+ - *` when mode is
//! [`OverflowMode::Trap`]. Explicit builtins `checked_add` / `would_overflow_*`
//! always use the checked C helpers.

use clap::ValueEnum;

/// How integer `+ - *` are emitted for `int64_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum OverflowMode {
    /// C wraparound (default).
    #[default]
    Wrap,
    /// Abort via `mako_overflow_trap` on overflow.
    Trap,
    /// Same codegen as wrap; documented opt-out.
    Ignore,
}

impl OverflowMode {
    /// C preprocessor value for `MAKO_OVERFLOW_MODE`.
    pub fn c_define_value(self) -> u8 {
        match self {
            OverflowMode::Wrap => 0,
            OverflowMode::Trap => 1,
            OverflowMode::Ignore => 2,
        }
    }

    /// Fingerprint fragment for incremental cache keys.
    pub fn cache_tag(self) -> &'static str {
        match self {
            OverflowMode::Wrap => "overflow=wrap;",
            OverflowMode::Trap => "overflow=trap;",
            OverflowMode::Ignore => "overflow=ignore;",
        }
    }

    /// Intrinsic name for a binary op when trapping (`+` → `mako_add_i64`).
    pub fn trap_fn(self, op: char) -> Option<&'static str> {
        if self != OverflowMode::Trap {
            return None;
        }
        match op {
            '+' => Some("mako_add_i64"),
            '-' => Some("mako_sub_i64"),
            '*' => Some("mako_mul_i64"),
            _ => None,
        }
    }
}

/// CLI enum for `--overflow`.
#[derive(Clone, Copy, Debug, Default, ValueEnum)]
pub enum OverflowCli {
    #[default]
    Wrap,
    Trap,
    Ignore,
}

impl From<OverflowCli> for OverflowMode {
    fn from(v: OverflowCli) -> Self {
        match v {
            OverflowCli::Wrap => OverflowMode::Wrap,
            OverflowCli::Trap => OverflowMode::Trap,
            OverflowCli::Ignore => OverflowMode::Ignore,
        }
    }
}

/// Builtin names registered for checked arithmetic (types + codegen must cover each).
/// Inventory for tests/docs; registration lives in `types` + `codegen` match arms.
#[allow(dead_code)]
pub const CHECKED_BUILTINS: &[&str] = &[
    "checked_add",
    "checked_sub",
    "checked_mul",
    "would_overflow_add",
    "would_overflow_sub",
    "would_overflow_mul",
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checked_builtins_include_sub() {
        assert!(CHECKED_BUILTINS.contains(&"would_overflow_sub"));
        assert!(CHECKED_BUILTINS.contains(&"checked_sub"));
        // Full add/sub/mul surface — no half-wired ops.
        for op in ["add", "sub", "mul"] {
            assert!(
                CHECKED_BUILTINS.iter().any(|b| b.ends_with(op)),
                "missing checked op {op}"
            );
            assert!(
                CHECKED_BUILTINS
                    .iter()
                    .any(|b| *b == format!("would_overflow_{op}")),
                "missing would_overflow_{op}"
            );
        }
    }
}
