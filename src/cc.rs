//! Native / cross C compiler selection and OS link flags.
//!
//! Resolution order for the C driver:
//! 1. `MAKO_CC` (full path or name)
//! 2. Cross targets: `zig cc` when available (or `MAKO_USE_ZIG=1`)
//! 3. `clang` (default)

use std::path::{Path, PathBuf};
use std::process::Command;

use crate::BuildOpts;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OsKind {
    Windows,
    Macos,
    Linux,
    Wasm,
    Other,
}

pub fn classify_target(triple: Option<&str>) -> OsKind {
    let t = match triple {
        Some(s) => s,
        None => {
            #[cfg(target_os = "windows")]
            {
                return OsKind::Windows;
            }
            #[cfg(target_os = "macos")]
            {
                return OsKind::Macos;
            }
            #[cfg(target_os = "linux")]
            {
                return OsKind::Linux;
            }
            #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
            {
                return OsKind::Other;
            }
        }
    };
    if t.contains("wasm") {
        OsKind::Wasm
    } else if t.contains("windows") {
        OsKind::Windows
    } else if t.contains("apple-darwin") || t.contains("macos") {
        OsKind::Macos
    } else if t.contains("linux") {
        OsKind::Linux
    } else {
        OsKind::Other
    }
}

pub fn is_cross(triple: Option<&str>) -> bool {
    let Some(t) = triple else {
        return false;
    };
    if t.contains("wasm") {
        return true;
    }
    t != host_triple_guess() && !t.is_empty()
}

fn host_triple_guess() -> &'static str {
    #[cfg(all(target_os = "macos", target_arch = "aarch64"))]
    {
        "aarch64-apple-darwin"
    }
    #[cfg(all(target_os = "macos", target_arch = "x86_64"))]
    {
        "x86_64-apple-darwin"
    }
    #[cfg(all(target_os = "linux", target_arch = "x86_64"))]
    {
        "x86_64-unknown-linux-gnu"
    }
    #[cfg(all(target_os = "linux", target_arch = "aarch64"))]
    {
        "aarch64-unknown-linux-gnu"
    }
    #[cfg(all(target_os = "windows", target_arch = "x86_64"))]
    {
        "x86_64-pc-windows-msvc"
    }
    #[cfg(not(any(
        all(target_os = "macos", target_arch = "aarch64"),
        all(target_os = "macos", target_arch = "x86_64"),
        all(target_os = "linux", target_arch = "x86_64"),
        all(target_os = "linux", target_arch = "aarch64"),
        all(target_os = "windows", target_arch = "x86_64"),
    )))]
    {
        "unknown"
    }
}

fn zig_on_path() -> bool {
    Command::new("zig")
        .arg("version")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

pub fn prefer_zig(opts: &BuildOpts) -> bool {
    if std::env::var_os("MAKO_USE_ZIG").is_some() {
        return zig_on_path();
    }
    if opts
        .target
        .as_ref()
        .is_some_and(|t| t.contains("wasm"))
    {
        // Without wasi-sdk, zig cc supplies the WASI sysroot. If wasi-sdk is
        // installed, resolve_cc selects its clang first.
        return zig_on_path();
    }
    if opts
        .target
        .as_ref()
        .is_some_and(|t| t.contains("apple-darwin"))
        && macos_sdk_path().is_some()
    {
        // The host Apple clang knows how to consume the installed SDK. Zig's
        // macOS target still needs an SDK supplied explicitly.
        return false;
    }
    if opts.target.as_ref().is_some_and(|t| !t.contains("wasm")) && is_cross(opts.target.as_deref())
    {
        return zig_on_path();
    }
    false
}

pub fn resolve_cc(opts: &BuildOpts) -> PathBuf {
    if let Some(cc) = std::env::var_os("MAKO_CC") {
        return PathBuf::from(cc);
    }
    let is_wasm = opts
        .target
        .as_ref()
        .map(|t| t.contains("wasm"))
        .unwrap_or(false);
    if is_wasm {
        if let Some(sdk) = wasi_sdk_path() {
            let clang = sdk.join("bin/clang");
            if clang.exists() {
                return clang;
            }
        }
    }
    if prefer_zig(opts) {
        return PathBuf::from("zig");
    }
    PathBuf::from("clang")
}

fn wasi_sdk_path() -> Option<PathBuf> {
    std::env::var_os("WASI_SDK_PATH")
        .map(PathBuf::from)
        .or_else(|| {
            ["/opt/wasi-sdk", "/usr/local/wasi-sdk"]
                .iter()
                .map(PathBuf::from)
                .find(|p| p.join("bin/clang").exists() || p.join("share/wasi-sysroot").exists())
        })
}

fn macos_sdk_path() -> Option<PathBuf> {
    if let Some(root) = std::env::var_os("SDKROOT") {
        let path = PathBuf::from(root);
        if path.is_dir() && path != Path::new("/") {
            return Some(path);
        }
    }
    Command::new("xcrun")
        .args(["--sdk", "macosx", "--show-sdk-path"])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .and_then(|o| {
            let path = PathBuf::from(String::from_utf8_lossy(&o.stdout).trim());
            path.is_dir().then_some(path)
        })
}

pub fn wasi_sdk() -> Option<PathBuf> {
    wasi_sdk_path()
}

/// Add the host Apple SDK when clang is cross-compiling Darwin.
pub fn push_macos_sysroot(cmd: &mut Command, triple: &str, zig: bool) {
    if zig || !triple.contains("apple-darwin") || !is_cross(Some(triple)) {
        return;
    }
    if let Some(sdk) = macos_sdk_path() {
        cmd.arg("-isysroot").arg(sdk);
    }
}

pub fn using_zig(cc: &Path) -> bool {
    cc.file_name().and_then(|s| s.to_str()) == Some("zig") || cc.to_string_lossy().ends_with("/zig")
}

/// Prefix args so `zig` acts as `zig cc …`.
pub fn apply_cc_prefix(cmd: &mut Command, cc: &Path) {
    if using_zig(cc) {
        cmd.arg("cc");
    }
}

pub fn push_target_args(cmd: &mut Command, triple: &str, zig: bool) {
    if triple.contains("wasm") {
        let t = match (zig, triple) {
            // Zig's libc target is `wasm32-wasi`; `wasm32-wasip1` is Mako's
            // public/Rust spelling and is accepted by wasi-sdk clang.
            (true, "wasm32-wasi")
            | (true, "wasm32-unknown-wasi")
            | (true, "wasm32-wasip1")
            | (true, "wasm32-unknown-wasip1") => "wasm32-wasi",
            (false, "wasm32-wasi") | (false, "wasm32-unknown-wasi") => "wasm32-wasip1",
            _ => triple,
        };
        if zig {
            cmd.arg("-target").arg(t);
        } else {
            cmd.arg(format!("--target={t}"));
        }
        return;
    }
    if zig {
        cmd.arg("-target").arg(zig_target_triple(triple));
    } else {
        cmd.arg("-target").arg(clang_target_triple(triple));
    }
}

fn zig_target_triple(triple: &str) -> &str {
    match triple {
        "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu" => "x86_64-windows-gnu",
        "x86_64-unknown-linux-gnu" => "x86_64-linux-gnu",
        "aarch64-unknown-linux-gnu" => "aarch64-linux-gnu",
        "x86_64-unknown-linux-musl" => "x86_64-linux-musl",
        "aarch64-unknown-linux-musl" => "aarch64-linux-musl",
        "aarch64-apple-darwin" => "aarch64-macos",
        "x86_64-apple-darwin" => "x86_64-macos",
        other => other,
    }
}

fn clang_target_triple(triple: &str) -> &str {
    match triple {
        "x86_64-pc-windows-gnu" => "x86_64-w64-windows-gnu",
        "x86_64-unknown-linux-gnu" => "x86_64-linux-gnu",
        "aarch64-unknown-linux-gnu" => "aarch64-linux-gnu",
        other => other,
    }
}

/// Base OS link flags (pthread / Winsock / dl). Optional libs added by caller.
pub fn base_link_args(opts: &BuildOpts) -> Vec<String> {
    let os = classify_target(opts.target.as_deref());
    let mut args = Vec::new();
    match os {
        OsKind::Windows => {
            args.push("-lws2_32".into());
        }
        OsKind::Wasm => {}
        OsKind::Macos | OsKind::Linux | OsKind::Other => {
            args.push("-pthread".into());
            args.push("-lm".into());
        }
    }
    if os == OsKind::Linux {
        args.push("-ldl".into());
    }
    if opts.static_link && os != OsKind::Macos && os != OsKind::Wasm {
        args.push("-static".into());
    }
    if let Some(s) = &opts.sanitize {
        if os != OsKind::Wasm {
            args.push(format!("-fsanitize={s}"));
        }
    }
    args
}

pub fn with_exe_suffix(path: PathBuf, opts: &BuildOpts) -> PathBuf {
    if classify_target(opts.target.as_deref()) != OsKind::Windows {
        return path;
    }
    if path.extension().and_then(|e| e.to_str()) == Some("exe") {
        return path;
    }
    let mut p = path.into_os_string();
    p.push(".exe");
    PathBuf::from(p)
}

pub fn note_cross(opts: &BuildOpts, cc: &Path) {
    let Some(t) = opts.target.as_deref() else {
        return;
    };
    if t.contains("wasm") || !is_cross(Some(t)) {
        return;
    }
    let zig = using_zig(cc);
    eprintln!(
        "mako: cross-compiling for `{t}` via {}{}",
        cc.display(),
        if zig {
            " (zig cc)"
        } else {
            " — install zig or set MAKO_CC; darwin cross needs a macOS SDK"
        }
    );
}

#[cfg(test)]
mod tests {
    use super::{base_link_args, clang_target_triple, zig_target_triple};
    use crate::BuildOpts;

    #[test]
    fn normalizes_common_zig_targets() {
        assert_eq!(
            zig_target_triple("x86_64-unknown-linux-musl"),
            "x86_64-linux-musl"
        );
        assert_eq!(
            zig_target_triple("aarch64-unknown-linux-musl"),
            "aarch64-linux-musl"
        );
        assert_eq!(
            zig_target_triple("x86_64-pc-windows-gnu"),
            "x86_64-windows-gnu"
        );
        assert_eq!(zig_target_triple("aarch64-apple-darwin"), "aarch64-macos");
    }

    #[test]
    fn normalizes_common_clang_targets() {
        assert_eq!(
            clang_target_triple("x86_64-pc-windows-gnu"),
            "x86_64-w64-windows-gnu"
        );
        assert_eq!(
            clang_target_triple("x86_64-unknown-linux-gnu"),
            "x86_64-linux-gnu"
        );
    }

    #[test]
    fn static_link_adds_static_for_linux() {
        let args = base_link_args(&BuildOpts {
            target: Some("x86_64-unknown-linux-musl".into()),
            sanitize: None,
            static_link: true,
            overflow: crate::overflow::OverflowMode::Wrap,
            bounds_always: false,
        });
        assert!(args.iter().any(|a| a == "-static"));
        assert!(args.iter().any(|a| a == "-pthread"));
        assert!(args.iter().any(|a| a == "-ldl"));
    }
}
