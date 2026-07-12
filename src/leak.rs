//! Leak detector — nestable scope counters over alloc_track.
//!
//! Runtime: [`runtime/mako_leak.h`](../../runtime/mako_leak.h)
//! (`mako_leak_scope_enter` / `exit` / `mako_leak_check`).
//!
//! Builds on existing `leak_mark` / `alloc_track_*` in `mako_std.h`.

/// Scope-based leak builtins.
#[allow(dead_code)]
pub const LEAK_SCOPE_BUILTINS: &[&str] = &[
    "leak_scope_enter",
    "leak_scope_exit",
    "leak_check",
    "leak_assert_scope",
];

/// Legacy mark-based leak builtins (still preferred for long-running services).
#[allow(dead_code)]
pub const LEAK_MARK_BUILTINS: &[&str] = &[
    "leak_mark",
    "leak_bytes_since",
    "leak_detected",
    "leak_assert_clear",
    "leak_report_json",
];

#[allow(dead_code)]
pub fn c_symbol(builtin: &str) -> Option<&'static str> {
    Some(match builtin {
        "leak_scope_enter" => "mako_leak_scope_enter",
        "leak_scope_exit" => "mako_leak_scope_exit",
        "leak_check" => "mako_leak_check",
        "leak_assert_scope" => "mako_leak_assert_scope",
        "leak_mark" => "mako_leak_mark",
        "leak_bytes_since" => "mako_leak_bytes_since",
        "leak_detected" => "mako_leak_detected",
        "leak_assert_clear" => "mako_leak_assert_clear",
        "leak_report_json" => "mako_leak_report_json",
        _ => return None,
    })
}
