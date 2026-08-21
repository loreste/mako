//! Real DAP (Debug Adapter Protocol) front-end for Mako.
//!
//! `mako dap` speaks DAP on stdin/stdout, builds the requested `.mko` program
//! on `launch`, and proxies the session to a spawned `lldb-dap` child. Because
//! the C backend emits per-statement `#line` directives, stack frames and
//! breakpoints resolve to `.mko` source with no path rewriting — the proxy only
//! rewrites the `launch` request (binary path + formatter init command).
//!
//! Forwarding is verbatim in both directions: client requests reach lldb-dap
//! with their original `seq`, so `request_seq` correlation holds and events
//! keep lldb-dap's own sequence numbers (valid per the protocol).

use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use serde_json::{json, Value};

use crate::{
    build_incremental, emit_plain_error, make_incr_opts, opt_level, resolve_run_entry, BackendCli,
    BuildOpts,
};

/// Entry point for `mako dap`: run the adapter on stdin/stdout.
pub(crate) fn run_stdio() -> Result<(), ()> {
    let lldb_dap = match find_lldb_dap() {
        Some(p) => p,
        None => {
            emit_plain_error(
                "mako dap: lldb-dap not found. Install LLVM/Xcode command line tools, \
                 or set MAKO_LLDB_DAP to its path.",
            );
            return Err(());
        }
    };
    eprintln!("mako dap: using {}", lldb_dap.display());

    let mut child = match Command::new(&lldb_dap)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .spawn()
    {
        Ok(c) => c,
        Err(e) => {
            emit_plain_error(&format!(
                "mako dap: could not spawn {}: {e}",
                lldb_dap.display()
            ));
            return Err(());
        }
    };
    let mut child_stdin = child.stdin.take().expect("piped stdin");
    let child_stdout = child.stdout.take().expect("piped stdout");

    // Pump child stdout → client stdout verbatim.
    let pump = std::thread::spawn(move || {
        let mut reader = BufReader::new(child_stdout);
        let mut out = std::io::stdout();
        while let Ok(Some(msg)) = read_frame(&mut reader) {
            if write_frame(&mut out, &msg).is_err() {
                break;
            }
        }
    });

    // Client stdin → child stdin, intercepting launch/disconnect.
    let stdin = std::io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    while let Ok(Some(msg)) = read_frame(&mut reader) {
        let mut disconnect = false;
        let outgoing = match serde_json::from_slice::<Value>(&msg) {
            Ok(v) if v.get("type").and_then(Value::as_str) == Some("request") => {
                match v.get("command").and_then(Value::as_str) {
                    Some("launch") => match handle_launch(&v) {
                        Ok(rewritten) => Some(rewritten),
                        Err(message) => {
                            // Build failed: answer the client directly, forward nothing.
                            let resp = json!({
                                "seq": 0,
                                "type": "response",
                                "request_seq": v.get("seq").and_then(Value::as_i64).unwrap_or(0),
                                "success": false,
                                "command": "launch",
                                "message": message,
                            });
                            let mut out = std::io::stdout();
                            let _ = write_frame(&mut out, resp.to_string().as_bytes());
                            None
                        }
                    },
                    Some("disconnect") => {
                        disconnect = true;
                        Some(msg.clone())
                    }
                    _ => Some(msg.clone()),
                }
            }
            _ => Some(msg.clone()),
        };
        if let Some(bytes) = outgoing {
            if write_frame(&mut child_stdin, &bytes).is_err() {
                eprintln!("mako dap: lldb-dap stdin closed");
                break;
            }
        }
        if disconnect {
            // Give lldb-dap a moment to shut the inferior down, then reap it.
            std::thread::sleep(std::time::Duration::from_millis(300));
            break;
        }
    }

    drop(child_stdin);
    let _ = child.kill();
    let _ = child.wait();
    let _ = pump.join();
    Ok(())
}

/// Rewrite a DAP `launch` request: build the `.mko` program and point lldb-dap
/// at the resulting binary, preloading the Mako type formatters.
fn handle_launch(req: &Value) -> Result<Vec<u8>, String> {
    let mut req = req.clone();
    let args = req
        .get_mut("arguments")
        .and_then(Value::as_object_mut)
        .ok_or_else(|| "mako dap: launch request has no arguments".to_string())?;

    let program = args
        .get("program")
        .and_then(Value::as_str)
        .ok_or_else(|| "mako dap: launch arguments missing `program`".to_string())?
        .to_string();
    let program_path = PathBuf::from(&program);

    let bin = if program.ends_with(".mko") || program_path.is_dir() {
        build_for_debug(&program_path, None)?
    } else {
        // Already a binary: trust the caller, still add the formatters.
        program_path
    };
    args.insert(
        "program".to_string(),
        Value::String(bin.display().to_string()),
    );

    if let Some(fmt) = find_formatters() {
        let init = format!("command script import {}", fmt.display());
        let entry = Value::String(init);
        match args.get_mut("initCommands") {
            Some(Value::Array(list)) => list.insert(0, entry),
            _ => {
                args.insert("initCommands".to_string(), json!([entry]));
            }
        }
    }
    serde_json::to_vec(&req).map_err(|e| format!("mako dap: serialize launch: {e}"))
}

/// Build a `.mko` source (or workspace directory) for debugging.
/// Always debug flags (`-O0 -g`) on the C backend — that is the path with
/// verified `#line` source mapping.
fn build_for_debug(path: &Path, package: Option<&str>) -> Result<PathBuf, String> {
    // Make the path absolute (without resolving symlinks, so /tmp stays /tmp)
    // — DAP clients send absolute breakpoint paths and a relative `#line`
    // entry in DWARF would leave breakpoints unresolved.
    let abs;
    let path = if path.is_absolute() {
        path
    } else {
        match std::env::current_dir() {
            Ok(cwd) => {
                abs = cwd.join(path);
                &abs
            }
            Err(_) => path,
        }
    };
    let file = resolve_run_entry(path, package)
        .map_err(|_| format!("mako dap: cannot resolve entry for {}", path.display()))?;
    let out_bin = std::env::temp_dir().join(format!(
        "mako_dbg_{}_{}",
        file.file_stem().and_then(|s| s.to_str()).unwrap_or("prog"),
        std::process::id()
    ));
    let opts = BuildOpts::default();
    let incr = make_incr_opts(true, false, None, &opts);
    build_incremental(
        &file,
        &out_bin,
        false,
        BackendCli::C,
        opt_level(false),
        &opts,
        &incr,
    )
    .map_err(|_| {
        format!(
            "mako dap: build failed for {} (see adapter log)",
            file.display()
        )
    })?;
    Ok(out_bin)
}

/// Locate the lldb-dap executable.
fn find_lldb_dap() -> Option<PathBuf> {
    if let Some(p) = std::env::var_os("MAKO_LLDB_DAP") {
        let p = PathBuf::from(p);
        if p.is_file() {
            return Some(p);
        }
    }
    if cfg!(target_os = "macos") {
        if let Ok(out) = Command::new("xcrun").args(["-f", "lldb-dap"]).output() {
            if out.status.success() {
                let p = PathBuf::from(String::from_utf8_lossy(&out.stdout).trim().to_string());
                if p.is_file() {
                    return Some(p);
                }
            }
        }
    }
    for name in [
        "lldb-dap",
        "lldb-dap-21",
        "lldb-dap-20",
        "lldb-dap-19",
        "lldb-dap-18",
        "lldb-dap-17",
        "lldb-dap-16",
    ] {
        if let Some(p) = which(name) {
            return Some(p);
        }
    }
    None
}

/// Locate the Mako lldb formatters shipped next to the binary, falling back to
/// the source tree (dev builds).
fn find_formatters() -> Option<PathBuf> {
    if let Some(p) = std::env::var_os("MAKO_LLDB_FORMATTERS") {
        let p = PathBuf::from(p);
        if p.is_file() {
            return Some(p);
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let p = dir.join("../share/mako/mako_formatters.py");
            if p.is_file() {
                return Some(p);
            }
        }
    }
    let dev = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("editors/lldb/mako_formatters.py");
    if dev.is_file() {
        return Some(dev);
    }
    None
}

/// Locate an interactive `lldb` for `mako debug`.
pub(crate) fn find_lldb() -> Option<PathBuf> {
    if let Some(p) = std::env::var_os("MAKO_LLDB") {
        let p = PathBuf::from(p);
        if p.is_file() {
            return Some(p);
        }
    }
    if cfg!(target_os = "macos") {
        if let Ok(out) = Command::new("xcrun").args(["-f", "lldb"]).output() {
            if out.status.success() {
                let p = PathBuf::from(String::from_utf8_lossy(&out.stdout).trim().to_string());
                if p.is_file() {
                    return Some(p);
                }
            }
        }
    }
    which("lldb")
}

/// Minimal PATH lookup (no external `which` crate).
fn which(name: &str) -> Option<PathBuf> {
    let paths = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&paths) {
        let p = dir.join(name);
        if p.is_file() {
            return Some(p);
        }
    }
    None
}

/// `mako debug file.mko [-- args]`: build and launch an interactive lldb
/// session with the Mako formatters preloaded.
pub(crate) fn cmd_debug(path: &Path, package: Option<&str>, args: &[String]) -> Result<(), ()> {
    let bin = match build_for_debug(path, package) {
        Ok(b) => b,
        Err(e) => {
            emit_plain_error(&e);
            return Err(());
        }
    };
    let lldb = match find_lldb() {
        Some(l) => l,
        None => {
            emit_plain_error("mako debug: lldb not found (set MAKO_LLDB to its path)");
            return Err(());
        }
    };
    let mut cmd = Command::new(&lldb);
    if let Some(fmt) = find_formatters() {
        cmd.arg("-o")
            .arg(format!("command script import {}", fmt.display()));
    }
    if args.is_empty() {
        cmd.arg(&bin);
    } else {
        cmd.arg("--").arg(&bin).args(args);
    }
    let status = cmd.status().map_err(|e| {
        emit_plain_error(&format!(
            "mako debug: could not run {}: {e}",
            lldb.display()
        ));
    })?;
    if !status.success() {
        emit_plain_error(&format!("mako debug: lldb exited with {status}"));
        return Err(());
    }
    Ok(())
}

/// Read one Content-Length framed DAP message. Ok(None) = clean EOF.
fn read_frame(reader: &mut impl BufRead) -> std::io::Result<Option<Vec<u8>>> {
    let mut content_len: Option<usize> = None;
    loop {
        let mut line = String::new();
        let n = reader.read_line(&mut line)?;
        if n == 0 {
            return Ok(None);
        }
        let t = line.trim_end_matches(['\r', '\n']);
        if t.is_empty() {
            if content_len.is_some() {
                break;
            }
            continue;
        }
        if let Some(rest) = t
            .strip_prefix("Content-Length:")
            .or_else(|| t.strip_prefix("content-length:"))
        {
            content_len = rest.trim().parse().ok();
        }
    }
    let len = match content_len {
        Some(l) if l > 0 => l,
        _ => return Ok(None),
    };
    let mut body = vec![0u8; len];
    reader.read_exact(&mut body)?;
    Ok(Some(body))
}

/// Write one Content-Length framed DAP message as a single locked write so
/// frames from the two writer threads can never interleave byte-wise.
fn write_frame(out: &mut impl Write, body: &[u8]) -> std::io::Result<()> {
    let mut frame = Vec::with_capacity(body.len() + 32);
    write!(frame, "Content-Length: {}\r\n\r\n", body.len())?;
    frame.write_all(body)?;
    out.write_all(&frame)?;
    out.flush()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_round_trip() {
        let body = br#"{"seq":1,"type":"request","command":"initialize"}"#;
        let mut buf = Vec::new();
        write_frame(&mut buf, body).unwrap();
        let mut reader = BufReader::new(&buf[..]);
        let got = read_frame(&mut reader).unwrap().unwrap();
        assert_eq!(got, body);
    }

    #[test]
    fn read_frame_eof_is_none() {
        let mut reader = BufReader::new(&b""[..]);
        assert!(read_frame(&mut reader).unwrap().is_none());
    }

    #[test]
    fn launch_rewrite_builds_binary_path_and_formatters() {
        // Avoid running a real build: point `program` at an existing file so
        // the "already a binary" branch is taken.
        let req = json!({
            "seq": 3,
            "type": "request",
            "command": "launch",
            "arguments": { "program": "/bin/ls", "args": [] }
        });
        let out = handle_launch(&req).unwrap();
        let v: Value = serde_json::from_slice(&out).unwrap();
        let args = &v["arguments"];
        assert_eq!(args["program"], "/bin/ls");
        if find_formatters().is_some() {
            let init = args["initCommands"].as_array().unwrap();
            assert!(init[0].as_str().unwrap().contains("command script import"));
        }
        // Untouched fields survive.
        assert_eq!(v["seq"], 3);
    }

    #[test]
    fn launch_rewrite_preserves_existing_init_commands() {
        let req = json!({
            "type": "request",
            "command": "launch",
            "arguments": { "program": "/bin/ls", "initCommands": ["settings set x y"] }
        });
        let out = handle_launch(&req).unwrap();
        let v: Value = serde_json::from_slice(&out).unwrap();
        let init = v["arguments"]["initCommands"].as_array().unwrap();
        assert_eq!(init.last().unwrap(), "settings set x y");
        if find_formatters().is_some() {
            assert_eq!(init.len(), 2);
        }
    }

    #[test]
    fn launch_without_program_is_an_error() {
        let req = json!({"type": "request", "command": "launch", "arguments": {}});
        assert!(handle_launch(&req).is_err());
    }
}
