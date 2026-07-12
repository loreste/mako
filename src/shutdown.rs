//! Graceful shutdown surface — SIGTERM/SIGINT, listener close, drain.
//!
//! Runtime: [`runtime/mako_shutdown.h`](../../runtime/mako_shutdown.h)
//!
//! Mako builtins (wired in types + codegen):
//! - `signal_on_term()` / `install_graceful_shutdown(grace_ms)`
//! - `register_listener(fd)` / `close_listeners()`
//! - `server_shutdown_begin(grace_ms)` / `server_drain(timeout_ms)`
//! - `shutdown_requested()` / `should_stop_accepting()`
//!
//! Pairs with existing `http_shutdown_*` and `signal_watch` for HTTP-specific
//! graceful drain.

/// Builtin names for the shutdown surface.
#[allow(dead_code)]
pub const SHUTDOWN_BUILTINS: &[&str] = &[
    "signal_on_term",
    "install_graceful_shutdown",
    "register_listener",
    "close_listeners",
    "server_shutdown_begin",
    "server_drain",
    "shutdown_requested",
    "should_stop_accepting",
];

/// C runtime symbols corresponding to each builtin (for docs / tooling).
#[allow(dead_code)]
pub fn c_symbol(builtin: &str) -> Option<&'static str> {
    Some(match builtin {
        "signal_on_term" => "mako_signal_on_term",
        "install_graceful_shutdown" => "mako_install_graceful_shutdown",
        "register_listener" => "mako_register_listener",
        "close_listeners" => "mako_close_listeners",
        "server_shutdown_begin" => "mako_server_shutdown_begin",
        "server_drain" => "mako_server_drain",
        "shutdown_requested" => "mako_shutdown_requested",
        "should_stop_accepting" => "mako_should_stop_accepting",
        _ => return None,
    })
}
