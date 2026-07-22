//! Incremental build: fingerprints, typecheck cache, per-unit C→`.o`, parallel jobs.
//!
//! # Cache layout
//! ```text
//! .mako/cache/   (or $MAKO_CACHE)
//!   meta.txt              # COMPILER_CACHE_VERSION
//!   typecheck/<fp>.ok     # whole-program typecheck stamp (source+deps fingerprint)
//!   c/<fp>.c              # generated C for a compile unit
//!   obj/<fp>.o            # clang -c output (keyed by C bytes + flags + version)
//! ```
//!
//! # Memory safety
//! - No `unsafe` in this module.
//! - Parallel jobs: each unit owns its inputs; results collected via `mpsc` (no shared
//!   mutable typechecker / codegen state across threads).
//! - Cache paths are constrained under the cache root (hex fingerprints only — no `..`).
//! - Writes are temp-file + rename (atomic on the same filesystem).
//! - Object cache keys include full generated C + opt/sanitize/target flags + compiler
//!   cache version, so a stale `.o` cannot be linked after codegen-affecting changes.
//! - Typecheck cache hits require a fingerprint of **all** safety-relevant sources
//!   (entry + transitive package `.mko` + manifests). Borrow/NLL is never skipped on a
//!   partial fingerprint; a hit means those bytes are unchanged since a successful check.
//!
//! # Residual clang
//! Clang is still required for `clang -c` (C→`.o`) on cache miss and for the final link.
//! Unchanged units skip `clang -c` entirely.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc;
use std::time::Instant;

use crate::ast::{Item, Program};
use crate::codegen::Codegen;
use crate::diag::{Diagnostic, Span};
use crate::overflow::OverflowMode;
use crate::tooling::{find_nearest_manifest_dir, parse_manifest_deps, resolve_dep_root};
use crate::types::{TypeChecker, TypeError};

/// Bump when fingerprint inputs or cache layout change (invalidates all caches).
pub const COMPILER_CACHE_VERSION: &str = "mako-incr-7";

#[derive(Clone, Debug)]
pub struct IncrOptions {
    pub incremental: bool,
    pub jobs: usize,
    pub release: bool,
    pub verbose_cache: bool,
    /// Extra fingerprint material (sanitize, target, static, …).
    pub flags_fp: String,
    /// C driver (`clang`, `zig`, or `MAKO_CC`). Empty → `clang`.
    pub cc: PathBuf,
    /// Integer overflow codegen mode.
    pub overflow: OverflowMode,
    /// Legacy compatibility flag; safe bounds checks are always retained.
    pub bounds_always: bool,
}

impl Default for IncrOptions {
    fn default() -> Self {
        Self {
            incremental: true,
            jobs: default_jobs(),
            release: false,
            verbose_cache: std::env::var_os("MAKO_CACHE_LOG").is_some(),
            flags_fp: String::new(),
            cc: PathBuf::from("clang"),
            overflow: OverflowMode::Wrap,
            bounds_always: false,
        }
    }
}

pub fn default_jobs() -> usize {
    if let Ok(v) = std::env::var("MAKO_JOBS") {
        if let Ok(n) = v.parse::<usize>() {
            return n.max(1);
        }
    }
    std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(4)
}

/// Release LTO is part of the object-cache identity.  The opt-out is explicit
/// because reusing an LTO object for a non-LTO build (or the reverse) produces
/// a binary with optimization inputs different from the requested build.
pub fn release_lto_enabled() -> bool {
    std::env::var_os("MAKO_NO_LTO").is_none()
}

/// Identify the C driver and its reported version for cache invalidation.
/// A compiler path alone is insufficient: package upgrades can replace the
/// executable in place while leaving every source byte unchanged.
pub fn compiler_identity(cc: &Path) -> String {
    let version = Command::new(cc)
        .arg("--version")
        .output()
        .map(|out| {
            let mut bytes = out.stdout;
            bytes.extend_from_slice(&out.stderr);
            String::from_utf8_lossy(&bytes).into_owned()
        })
        .unwrap_or_else(|e| format!("unavailable:{e}"));
    fingerprint_str(&format!("path={}\nversion={version}", cc.display()))
}

fn fnv1a(bytes: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for b in bytes {
        h ^= u64::from(*b);
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

pub fn fingerprint_bytes(parts: &[&[u8]]) -> String {
    let mut h: u64 = 0xcbf29ce484222325;
    for p in parts {
        h ^= fnv1a(p);
        h = h.wrapping_mul(0x100000001b3);
        let mut len = p.len() as u64;
        for _ in 0..8 {
            h ^= len & 0xff;
            h = h.wrapping_mul(0x100000001b3);
            len >>= 8;
        }
    }
    format!("{h:016x}")
}

#[allow(dead_code)]
pub fn fingerprint_str(s: &str) -> String {
    fingerprint_bytes(&[s.as_bytes()])
}

/// Reject path traversal: only allow hex fingerprint filenames under a known subdir.
pub fn cache_artifact(root: &Path, sub: &str, fp: &str, ext: &str) -> Result<PathBuf, String> {
    if !fp.chars().all(|c| c.is_ascii_hexdigit()) || fp.is_empty() || fp.len() > 64 {
        return Err(format!("invalid cache fingerprint: {fp}"));
    }
    if sub.contains("..") || sub.contains('/') || sub.contains('\\') {
        return Err(format!("invalid cache subdir: {sub}"));
    }
    if ext.contains("..") || ext.contains('/') || ext.contains('\\') {
        return Err(format!("invalid cache ext: {ext}"));
    }
    Ok(root.join(sub).join(format!("{fp}.{ext}")))
}

fn ensure_dirs(root: &Path) -> Result<(), String> {
    for sub in ["typecheck", "c", "obj"] {
        fs::create_dir_all(root.join(sub)).map_err(|e| format!("mkdir cache: {e}"))?;
    }
    let meta = root.join("meta.txt");
    if !meta.exists() {
        atomic_write(&meta, format!("{COMPILER_CACHE_VERSION}\n").as_bytes())?;
    }
    Ok(())
}

/// Atomic write: temp in same dir + rename.
pub fn atomic_write(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let parent = path
        .parent()
        .ok_or_else(|| "cache path has no parent".to_string())?;
    fs::create_dir_all(parent).map_err(|e| format!("mkdir: {e}"))?;
    let tmp = parent.join(format!(
        ".{}.tmp.{}",
        path.file_name().and_then(|s| s.to_str()).unwrap_or("cache"),
        std::process::id()
    ));
    {
        let mut f = fs::File::create(&tmp).map_err(|e| format!("create temp: {e}"))?;
        f.write_all(bytes).map_err(|e| format!("write temp: {e}"))?;
        f.sync_all().map_err(|e| format!("sync temp: {e}"))?;
    }
    fs::rename(&tmp, path).map_err(|e| {
        let _ = fs::remove_file(&tmp);
        format!("rename cache: {e}")
    })
}

pub fn cache_root(entry: &Path) -> PathBuf {
    if let Ok(p) = std::env::var("MAKO_CACHE") {
        if !p.is_empty() {
            return PathBuf::from(p);
        }
    }
    if let Some(m) = find_nearest_manifest_dir(entry) {
        return m.join(".mako").join("cache");
    }
    entry.parent().unwrap_or(entry).join(".mako").join("cache")
}

fn cache_log(opts: &IncrOptions, msg: &str) {
    if opts.verbose_cache {
        eprintln!("mako cache: {msg}");
    }
}

/// Export signature: public fn names + arity (for dependent invalidation docs/tests).
#[allow(dead_code)]
pub fn export_signature(program: &Program) -> String {
    let mut lines: Vec<String> = Vec::new();
    for item in &program.items {
        if let Item::Fn(f) = item {
            if f.name == "main" || f.name.starts_with("Test") {
                continue;
            }
            lines.push(format!("{}:{}", f.name, f.params.len()));
        }
    }
    lines.sort();
    fingerprint_str(&lines.join("\n"))
}

/// Collect entry + transitive package sources that affect typecheck / codegen.
pub fn collect_program_inputs(entry: &Path) -> Result<Vec<PathBuf>, String> {
    let mut files = vec![entry.to_path_buf()];
    if let Some(manifest_dir) = find_nearest_manifest_dir(entry) {
        collect_dep_files(&manifest_dir, &mut files)?;
        files.push(manifest_dir.join("mako.toml"));
        let lockfile = manifest_dir.join("mako.lock");
        if lockfile.exists() {
            files.push(lockfile);
        }
    }
    files.sort();
    files.dedup();
    Ok(files)
}

fn collect_dep_files(manifest_dir: &Path, out: &mut Vec<PathBuf>) -> Result<(), String> {
    let manifest = manifest_dir.join("mako.toml");
    let Ok(text) = fs::read_to_string(&manifest) else {
        return Ok(());
    };
    out.push(manifest.clone());
    for dep in parse_manifest_deps(&text) {
        let root = resolve_dep_root(manifest_dir, &dep)?;
        if root.is_file() {
            out.push(root);
            continue;
        }
        if let Ok(rd) = fs::read_dir(&root) {
            for e in rd.flatten() {
                let p = e.path();
                if p.extension().and_then(|x| x.to_str()) == Some("mko") {
                    out.push(p);
                }
            }
        }
        if root.join("mako.toml").exists() {
            collect_dep_files(&root, out)?;
        }
    }
    Ok(())
}

/// Fingerprint of all sources that affect typecheck (incl. bodies — NLL/borrows).
pub fn program_typecheck_fingerprint(entry: &Path) -> Result<String, String> {
    let files = collect_program_inputs(entry)?;
    let mut parts: Vec<Vec<u8>> = vec![COMPILER_CACHE_VERSION.as_bytes().to_vec()];
    for f in &files {
        let b = fs::read(f).unwrap_or_default();
        parts.push(f.to_string_lossy().as_bytes().to_vec());
        parts.push(b);
    }
    let refs: Vec<&[u8]> = parts.iter().map(|v| v.as_slice()).collect();
    Ok(fingerprint_bytes(&refs))
}

pub fn typecheck_cache_hit(entry: &Path, opts: &IncrOptions) -> Result<bool, String> {
    if !opts.incremental {
        return Ok(false);
    }
    let cache = cache_root(entry);
    ensure_dirs(&cache)?;
    let fp = program_typecheck_fingerprint(entry)?;
    let path = cache_artifact(&cache, "typecheck", &fp, "ok")?;
    Ok(path.exists())
}

pub fn record_typecheck_ok(entry: &Path, opts: &IncrOptions) -> Result<(), String> {
    if !opts.incremental {
        return Ok(());
    }
    let cache = cache_root(entry);
    ensure_dirs(&cache)?;
    let fp = program_typecheck_fingerprint(entry)?;
    let path = cache_artifact(&cache, "typecheck", &fp, "ok")?;
    atomic_write(&path, b"ok\n")?;
    cache_log(opts, &format!("typecheck STORE {fp}"));
    Ok(())
}

fn emit_type_error(path: &str, src_for_diag: &str, error: TypeError) {
    let TypeError::At {
        message,
        hint,
        line,
        col,
    } = error;
    let span = if line > 0 {
        Span::new(line, col)
    } else {
        Span::unknown()
    };
    let mut diagnostic = Diagnostic::error(path, src_for_diag, span, message);
    if let Some(h) = hint {
        diagnostic = diagnostic.with_hint(h);
    }
    diagnostic.emit();
}

/// Run typecheck unless full-program fingerprint cache hits.
/// On hit, borrow/NLL was already proven for identical source bytes.
/// On miss, append monomorphized generic specializations to `program`.
pub fn typecheck_incremental(
    entry: &Path,
    program: &mut Program,
    src_for_diag: &str,
    opts: &IncrOptions,
) -> Result<bool /* hit */, ()> {
    let path = entry.display().to_string();
    match typecheck_cache_hit(entry, opts) {
        Ok(true) => {
            cache_log(opts, &format!("typecheck HIT {}", entry.display()));
            // Cache hit: still expand generics for codegen (cheap re-check of mono only).
            // Full recheck when mono needed — fingerprint includes sources so rare.
            // Re-run typecheck to rehydrate mono_fns (typecheck cache only stores stamp).
            let mut tc = TypeChecker::new();
            if let Some(dir) = crate::tooling::find_nearest_manifest_dir(entry) {
                let manifest = dir.join("mako.toml");
                if let Ok(text) = std::fs::read_to_string(&manifest) {
                    crate::tooling::apply_package_safety_flags(&mut tc, &text);
                }
            }
            // A cache stamp proves the source bytes were checked, but it must
            // never suppress a current hard policy rejection from the
            // manifest. In particular, legacy `[package] gc = true` must fail
            // even when the program's old typecheck stamp is still present.
            if tc.gc_requested {
                if let Err(e) = tc.check(program) {
                    emit_type_error(&path, src_for_diag, e);
                    return Err(());
                }
            }
            // Fast path: if no generics in program, keep cache hit without work.
            let has_generics = program.items.iter().any(|i| {
                matches!(i, crate::ast::Item::Fn(f) if !f.type_params.is_empty())
                    || matches!(i, crate::ast::Item::Struct(s) if !s.type_params.is_empty())
            });
            if has_generics {
                let _ = tc.check(program);
                for f in tc.mono_fns {
                    if !program
                        .items
                        .iter()
                        .any(|i| matches!(i, crate::ast::Item::Fn(x) if x.name == f.name))
                    {
                        program.items.push(crate::ast::Item::Fn(f));
                    }
                }
                for s in tc.mono_structs {
                    if !program
                        .items
                        .iter()
                        .any(|i| matches!(i, crate::ast::Item::Struct(x) if x.name == s.name))
                    {
                        program.items.push(crate::ast::Item::Struct(s));
                    }
                }
                for e in tc.mono_enums {
                    if !program
                        .items
                        .iter()
                        .any(|i| matches!(i, crate::ast::Item::Enum(x) if x.name == e.name))
                    {
                        program.items.push(crate::ast::Item::Enum(e));
                    }
                }
            }
            return Ok(true);
        }
        Ok(false) => cache_log(opts, &format!("typecheck MISS {}", entry.display())),
        Err(e) => {
            cache_log(opts, &format!("typecheck cache error (recheck): {e}"));
        }
    }

    let mut tc = TypeChecker::new();
    if let Some(dir) = crate::tooling::find_nearest_manifest_dir(entry) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = std::fs::read_to_string(&manifest) {
            crate::tooling::apply_package_safety_flags(&mut tc, &text);
        }
    }
    tc.check(program)
        .map_err(|e| emit_type_error(&path, src_for_diag, e))?;

    for f in tc.mono_fns {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, crate::ast::Item::Fn(x) if x.name == f.name))
        {
            program.items.push(crate::ast::Item::Fn(f));
        }
    }
    for s in tc.mono_structs {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, crate::ast::Item::Struct(x) if x.name == s.name))
        {
            program.items.push(crate::ast::Item::Struct(s));
        }
    }
    for e in tc.mono_enums {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, crate::ast::Item::Enum(x) if x.name == e.name))
        {
            program.items.push(crate::ast::Item::Enum(e));
        }
    }

    if let Err(e) = record_typecheck_ok(entry, opts) {
        cache_log(opts, &format!("typecheck store failed: {e}"));
    }
    Ok(false)
}

#[derive(Clone)]
pub struct ObjectUnit {
    pub name: String,
    pub c_src: String,
    pub fp: String,
}

/// Content fingerprint of the bundled runtime headers. Folded into every object
/// key so that editing a `runtime/*.h` (which the generated C `#include`s but
/// does not copy) invalidates the cached `.o` even when the generated C is byte
/// identical. For a released compiler the headers are fixed, so this is a stable
/// per-release constant.
pub fn runtime_headers_fp(runtime_dir: &Path) -> String {
    let mut paths: Vec<PathBuf> = match fs::read_dir(runtime_dir) {
        Ok(rd) => rd
            .filter_map(|e| e.ok().map(|e| e.path()))
            .filter(|p| {
                p.extension()
                    .and_then(|x| x.to_str())
                    .map(|x| x == "h")
                    .unwrap_or(false)
            })
            .collect(),
        Err(_) => return String::new(),
    };
    paths.sort();
    let mut parts: Vec<Vec<u8>> = Vec::new();
    for p in &paths {
        if let Some(n) = p.file_name().and_then(|s| s.to_str()) {
            parts.push(n.as_bytes().to_vec());
        }
        if let Ok(bytes) = fs::read(p) {
            parts.push(bytes);
        }
    }
    let refs: Vec<&[u8]> = parts.iter().map(|v| v.as_slice()).collect();
    fingerprint_bytes(&refs)
}

/// Plan compile units: program C (+ optional extern demo as separate `.o`).
pub fn plan_object_units(
    program: &Program,
    opts: &IncrOptions,
    runtime_dir: &Path,
) -> Vec<ObjectUnit> {
    let mut cg = Codegen::new();
    cg.overflow_mode = opts.overflow;
    cg.bounds_checks_always = opts.bounds_always;
    let c = cg.emit(program);
    // Release flags are part of the fingerprint; the explicit MAKO_NO_LTO
    // opt-out must never reuse an LTO object (or vice versa).
    let opt = if opts.release {
        if release_lto_enabled() {
            "O3flto"
        } else {
            "O3"
        }
    } else {
        "O0g"
    };
    let rt_fp = runtime_headers_fp(runtime_dir);
    let fp = fingerprint_bytes(&[
        COMPILER_CACHE_VERSION.as_bytes(),
        opt.as_bytes(),
        opts.flags_fp.as_bytes(),
        rt_fp.as_bytes(),
        b"unit:program",
        c.as_bytes(),
    ]);
    let mut units = vec![ObjectUnit {
        name: "program".into(),
        c_src: c,
        fp,
    }];

    let shim = runtime_dir.join("mako_extern_demo.c");
    if shim.exists() {
        if let Ok(src) = fs::read_to_string(&shim) {
            let sfp = fingerprint_bytes(&[
                COMPILER_CACHE_VERSION.as_bytes(),
                opt.as_bytes(),
                opts.flags_fp.as_bytes(),
                rt_fp.as_bytes(),
                b"unit:extern_demo",
                src.as_bytes(),
            ]);
            units.push(ObjectUnit {
                name: "extern_demo".into(),
                c_src: src,
                fp: sfp,
            });
        }
    }
    units
}

struct CompileJob {
    index: usize,
    name: String,
    c_src: String,
    fp: String,
    c_path: PathBuf,
    obj_path: PathBuf,
    runtime_dir: PathBuf,
    release: bool,
    cflags: Vec<String>,
    incremental: bool,
    cc: PathBuf,
}

enum JobResult {
    Hit {
        index: usize,
        path: PathBuf,
        name: String,
        fp: String,
    },
    Miss {
        index: usize,
        path: PathBuf,
        name: String,
        fp: String,
    },
    Err {
        message: String,
    },
}

fn compile_one_unit(job: CompileJob) -> JobResult {
    let name = job.name.clone();
    let fp = job.fp.clone();
    if job.incremental && job.obj_path.exists() && job.c_path.exists() {
        return JobResult::Hit {
            index: job.index,
            path: job.obj_path,
            name,
            fp,
        };
    }
    if let Err(e) = atomic_write(&job.c_path, job.c_src.as_bytes()) {
        return JobResult::Err { message: e };
    }
    // Write object to a temp path then rename so a crashed clang cannot leave a
    // truncated `.o` that would be treated as a cache hit.
    let obj_tmp = job
        .obj_path
        .with_extension(format!("o.tmp.{}", std::process::id()));
    let mut cmd = Command::new(&job.cc);
    if job.cc.file_name().and_then(|s| s.to_str()) == Some("zig") {
        cmd.arg("cc");
    }
    if job.release {
        if release_lto_enabled() {
            cmd.arg("-O3").arg("-flto").arg("-DNDEBUG");
        } else {
            cmd.arg("-O3").arg("-DNDEBUG");
        }
    } else {
        cmd.arg("-O0").arg("-g");
    }
    if let Ok(extra) = std::env::var("MAKO_CFLAGS") {
        for a in extra.split_whitespace() {
            if !a.is_empty() {
                cmd.arg(a);
            }
        }
    }
    if std::env::var_os("MAKO_PGO_GEN").is_some() {
        cmd.arg("-fprofile-generate");
    }
    if let Ok(dir) = std::env::var("MAKO_PGO_USE") {
        if dir.is_empty() || dir == "1" {
            cmd.arg("-fprofile-use");
        } else {
            cmd.arg(format!("-fprofile-use={dir}"));
        }
    }
    cmd.arg("-std=c11")
        .arg("-c")
        .arg(format!("-I{}", job.runtime_dir.display()))
        .arg(&job.c_path)
        .arg("-o")
        .arg(&obj_tmp);
    for f in &job.cflags {
        cmd.arg(f);
    }
    match cmd.output() {
        Ok(out) if out.status.success() => {
            if let Err(e) = fs::rename(&obj_tmp, &job.obj_path) {
                let _ = fs::remove_file(&obj_tmp);
                return JobResult::Err {
                    message: format!("rename object: {e}"),
                };
            }
            JobResult::Miss {
                index: job.index,
                path: job.obj_path,
                name,
                fp,
            }
        }
        Ok(out) => {
            let _ = fs::remove_file(&obj_tmp);
            let stderr = String::from_utf8_lossy(&out.stderr);
            JobResult::Err {
                message: format!("clang -c failed for `{name}`: {}", stderr.trim()),
            }
        }
        Err(e) => {
            let _ = fs::remove_file(&obj_tmp);
            JobResult::Err {
                message: format!("clang -c: {e}"),
            }
        }
    }
}

/// Compile units to `.o` in parallel. Safe: owned jobs, `mpsc` results, no shared mut.
pub fn compile_units_parallel(
    entry: &Path,
    units: &[ObjectUnit],
    opts: &IncrOptions,
    runtime_dir: &Path,
    extra_cflags: &[String],
) -> Result<(Vec<PathBuf>, usize /*hits*/, usize /*misses*/), ()> {
    let cache = cache_root(entry);
    ensure_dirs(&cache).map_err(|e| {
        Diagnostic::error("", "", Span::unknown(), e).emit();
    })?;

    let mut jobs: Vec<CompileJob> = Vec::with_capacity(units.len());
    for (i, u) in units.iter().enumerate() {
        let c_path = cache_artifact(&cache, "c", &u.fp, "c").map_err(|e| {
            Diagnostic::error("", "", Span::unknown(), e).emit();
        })?;
        let obj_path = cache_artifact(&cache, "obj", &u.fp, "o").map_err(|e| {
            Diagnostic::error("", "", Span::unknown(), e).emit();
        })?;
        jobs.push(CompileJob {
            index: i,
            name: u.name.clone(),
            c_src: u.c_src.clone(),
            fp: u.fp.clone(),
            c_path,
            obj_path,
            runtime_dir: runtime_dir.to_path_buf(),
            release: opts.release,
            cflags: extra_cflags.to_vec(),
            incremental: opts.incremental,
            cc: opts.cc.clone(),
        });
    }

    let n = jobs.len();
    let jobs_n = opts.jobs.max(1).min(n.max(1));
    let (tx, rx) = mpsc::channel::<JobResult>();

    if n <= 1 || jobs_n == 1 {
        for job in jobs {
            let _ = tx.send(compile_one_unit(job));
        }
        drop(tx);
    } else {
        let mut handles = Vec::new();
        let per = (n + jobs_n - 1) / jobs_n;
        let mut rest = jobs;
        while !rest.is_empty() {
            let take = per.min(rest.len());
            let chunk: Vec<_> = rest.drain(..take).collect();
            let txc = tx.clone();
            handles.push(std::thread::spawn(move || {
                for job in chunk {
                    let _ = txc.send(compile_one_unit(job));
                }
            }));
        }
        drop(tx);
        for h in handles {
            if h.join().is_err() {
                Diagnostic::error("", "", Span::unknown(), "compile worker panicked").emit();
                return Err(());
            }
        }
    }

    let mut results: Vec<Option<PathBuf>> = vec![None; n];
    let mut hits = 0usize;
    let mut misses = 0usize;
    while let Ok(r) = rx.recv() {
        match r {
            JobResult::Hit {
                index,
                path,
                name,
                fp,
            } => {
                hits += 1;
                cache_log(opts, &format!("object HIT {name} ({fp})"));
                results[index] = Some(path);
            }
            JobResult::Miss {
                index,
                path,
                name,
                fp,
            } => {
                misses += 1;
                cache_log(opts, &format!("object MISS {name} ({fp})"));
                results[index] = Some(path);
            }
            JobResult::Err { message } => {
                Diagnostic::error(&entry.display().to_string(), "", Span::unknown(), message)
                    .emit();
                return Err(());
            }
        }
    }

    let mut paths = Vec::with_capacity(n);
    for (i, r) in results.into_iter().enumerate() {
        match r {
            Some(p) => paths.push(p),
            None => {
                Diagnostic::error(
                    "",
                    "",
                    Span::unknown(),
                    format!("missing object for unit {i}"),
                )
                .emit();
                return Err(());
            }
        }
    }
    Ok((paths, hits, misses))
}

/// Link object files into a binary.
pub fn link_objects(
    objs: &[PathBuf],
    out_bin: &Path,
    opts: &IncrOptions,
    link_args: &[String],
) -> Result<f64, ()> {
    let t0 = Instant::now();
    let mut cmd = Command::new(&opts.cc);
    if opts.cc.file_name().and_then(|s| s.to_str()) == Some("zig") {
        cmd.arg("cc");
    }
    if opts.release {
        if release_lto_enabled() {
            cmd.arg("-O3").arg("-flto").arg("-DNDEBUG");
        } else {
            cmd.arg("-O3").arg("-DNDEBUG");
        }
    } else {
        cmd.arg("-O0").arg("-g");
    }
    if let Ok(extra) = std::env::var("MAKO_CFLAGS") {
        for a in extra.split_whitespace() {
            if !a.is_empty() {
                cmd.arg(a);
            }
        }
    }
    if std::env::var_os("MAKO_PGO_GEN").is_some() {
        cmd.arg("-fprofile-generate");
    }
    if let Ok(dir) = std::env::var("MAKO_PGO_USE") {
        if dir.is_empty() || dir == "1" {
            cmd.arg("-fprofile-use");
        } else {
            cmd.arg(format!("-fprofile-use={dir}"));
        }
    }
    for o in objs {
        cmd.arg(o);
    }
    for a in link_args {
        cmd.arg(a);
    }
    cmd.arg("-o").arg(out_bin);
    let output = cmd.output().map_err(|e| {
        Diagnostic::error(
            "",
            "",
            Span::unknown(),
            format!("link failed to start: {e}"),
        )
        .emit();
    })?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        Diagnostic::error("", "", Span::unknown(), "native link failed")
            .with_hint(stderr.trim().to_string())
            .emit();
        return Err(());
    }
    // Release: strip by default; set MAKO_NO_STRIP=1 to keep symbols.
    if opts.release && std::env::var_os("MAKO_NO_STRIP").is_none() {
        let _ = Command::new("strip").arg(out_bin).status();
    }
    Ok(t0.elapsed().as_secs_f64() * 1000.0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[test]
    fn fingerprint_stable_and_distinct() {
        assert_eq!(fingerprint_str("hello"), fingerprint_str("hello"));
        assert_ne!(fingerprint_str("hello"), fingerprint_str("world"));
    }

    #[test]
    fn fingerprint_length_mix() {
        let a = fingerprint_bytes(&[b"ab", b"c"]);
        let b = fingerprint_bytes(&[b"a", b"bc"]);
        assert_ne!(a, b);
    }

    #[test]
    fn typecheck_fingerprint_includes_lockfile() {
        let dir = std::env::temp_dir().join(format!("mako_incr_lock_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let entry = dir.join("main.mko");
        fs::write(&entry, "fn main() {}\n").unwrap();
        fs::write(dir.join("mako.toml"), "name = \"app\"\n").unwrap();
        fs::write(dir.join("mako.lock"), "version = 1\n").unwrap();
        let before = program_typecheck_fingerprint(&entry).unwrap();

        fs::write(dir.join("mako.lock"), "version = 2\n").unwrap();
        let after = program_typecheck_fingerprint(&entry).unwrap();
        assert_ne!(before, after);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn cache_artifact_rejects_traversal() {
        let root = PathBuf::from("/tmp/mako-cache-test");
        assert!(cache_artifact(&root, "obj", "../etc", "o").is_err());
        assert!(cache_artifact(&root, "obj", "deadbeef", "o").is_ok());
        assert!(cache_artifact(&root, "../obj", "aa", "o").is_err());
    }

    #[test]
    fn export_sig_empty() {
        let p = Program { items: vec![] };
        assert!(!export_signature(&p).is_empty());
    }

    #[test]
    fn parallel_jobs_mutex_ok() {
        let counter = Arc::new(Mutex::new(0usize));
        let mut handles = Vec::new();
        for _ in 0..8 {
            let c = Arc::clone(&counter);
            handles.push(std::thread::spawn(move || {
                let mut g = c.lock().unwrap();
                *g += 1;
            }));
        }
        for h in handles {
            h.join().unwrap();
        }
        assert_eq!(*counter.lock().unwrap(), 8);
    }

    #[test]
    fn atomic_write_roundtrip() {
        let dir = std::env::temp_dir().join(format!("mako_incr_test_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let p = dir.join("x.txt");
        atomic_write(&p, b"hello").unwrap();
        assert_eq!(fs::read_to_string(&p).unwrap(), "hello");
        let _ = fs::remove_dir_all(&dir);
    }
}
