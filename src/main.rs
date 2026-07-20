mod ast;
mod cc;
mod codegen;
mod dce;
mod desugar;
mod diag;
mod errors;
mod fmt;
mod incremental;
mod leak;
mod lexer;
mod lsp;
mod overflow;
mod parser;
mod pkg;
mod recovery;
mod shutdown;
mod tooling;
mod types;

use clap::{CommandFactory, FromArgMatches, Parser as ClapParser, Subcommand, ValueEnum};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Instant;

use codegen::Codegen;
use diag::{json_escape, Diagnostic, Span};
use lexer::{LexError, Lexer};
use overflow::{OverflowCli, OverflowMode};
use parser::Parser;

#[derive(ClapParser)]
#[command(
    name = "mako",
    version,
    about = "Mako — memory safety, simple concurrency, fast builds, no GC.",
    long_about = "Mako compiles .mko sources to native binaries via C.\n\
                  Beachhead: network servers and session-oriented backends.\n\
                  Docs: docs/GUIDE.md · Status: docs/STATUS.md · Release: docs/RELEASE.md\n\n\
                  Common: mako version · mako init · mako run main.mko · mako test · mako fmt -w",
    after_help = "See also: mako help <command> · mako version -v"
)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Clone, Copy, Debug, Default)]
enum OptLevel {
    #[default]
    Debug,
    Release,
}

#[derive(Subcommand)]
enum Commands {
    /// Create a new project (mako.toml + main.mko; optional `--workspace` / `--backend`)
    Init {
        /// Directory to create (default: current directory)
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Package / project name (default: directory name or \"mako-app\")
        #[arg(long, short = 'n')]
        name: Option<String>,
        /// Scaffold `[workspace] members = ["lib", "app"]` with path dep `app` → `lib`
        #[arg(long, default_value_t = false)]
        workspace: bool,
        /// Scaffold an HTTP JSON API service (main + README)
        #[arg(long, default_value_t = false)]
        backend: bool,
    },
    /// Lex, parse, and typecheck a .mko file (or all workspace members)
    Check {
        /// Source file or package/workspace directory
        #[arg(default_value = ".")]
        file: PathBuf,
        /// Focus a single workspace member (name from `[workspace] members`)
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Emit machine-readable JSON diagnostics (array for multi-package checks)
        #[arg(long, default_value_t = false)]
        json: bool,
        /// Disable incremental typecheck cache (default: on)
        #[arg(long, default_value_t = false)]
        no_incremental: bool,
    },
    /// Compile a .mko file to a native binary (debug/-O0 by default; workspace root builds each member)
    Build {
        /// Source file or package/workspace directory
        #[arg(default_value = ".")]
        file: PathBuf,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Output binary path (single-file / `-p` only)
        #[arg(short, long)]
        out: Option<PathBuf>,
        /// Write generated C to stdout / beside sources instead of linking only
        #[arg(long)]
        emit_c: bool,
        /// Optimize (`-O3 -flto`); default is debug `-O0`
        #[arg(long, default_value_t = false)]
        release: bool,
        /// Print frontend / codegen / link timings
        #[arg(long)]
        time: bool,
        /// Cross-compile triple (e.g. `x86_64-unknown-linux-gnu`, `x86_64-pc-windows-gnu`,
        /// `aarch64-apple-darwin`, `wasm32-wasip1`). Uses `zig cc` when available for cross;
        /// override with `MAKO_CC` / `MAKO_USE_ZIG=1`.
        #[arg(long)]
        target: Option<String>,
        /// Pass `-fsanitize=` to clang (e.g. `thread`, `address`)
        #[arg(long)]
        sanitize: Option<String>,
        /// Attempt static link (`clang -static`); Linux musl targets default to static
        #[arg(long, default_value_t = false)]
        static_link: bool,
        /// Opt out of static default for targets where Mako would normally prefer it
        #[arg(long, default_value_t = false)]
        no_static_link: bool,
        /// Disable package/object/typecheck caches (default: incremental on)
        #[arg(long, default_value_t = false)]
        no_incremental: bool,
        /// Parallel compile jobs (default: `MAKO_JOBS` or CPU count)
        #[arg(short = 'j', long = "jobs")]
        jobs: Option<usize>,
        /// Integer overflow: `trap` (abort), `wrap` (C default), `ignore` (same as wrap)
        #[arg(long, value_enum, default_value_t = OverflowCli::Wrap)]
        overflow: OverflowCli,
        /// Keep bounds checks on even in release (`always`)
        #[arg(long, value_enum, default_value_t = BoundsCli::Default)]
        bounds: BoundsCli,
    },
    /// Watch `.mko` sources and rebuild+rerun on change (hot reload seed)
    Dev {
        /// Source file or package directory
        #[arg(default_value = ".")]
        file: PathBuf,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Poll interval in milliseconds
        #[arg(long, default_value_t = 500)]
        interval_ms: u64,
        /// Optimize builds (`-O3`)
        #[arg(long, default_value_t = false)]
        release: bool,
        /// Integer overflow mode for rebuilds
        #[arg(long, value_enum, default_value_t = OverflowCli::Trap)]
        overflow: OverflowCli,
    },
    /// Compile (debug) and run (workspace root: `-p NAME` or unique default main)
    Run {
        /// Source file or package/workspace directory
        #[arg(default_value = ".")]
        file: PathBuf,
        /// Workspace member with `main.mko` (required when multiple runnable members)
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Optimize before run (`-O3 -flto`)
        #[arg(long, default_value_t = false)]
        release: bool,
        /// Print compile timings before the program runs
        #[arg(long)]
        time: bool,
        /// Disable incremental caches (default: on)
        #[arg(long, default_value_t = false)]
        no_incremental: bool,
        /// Parallel compile jobs (default: `MAKO_JOBS` or CPU count)
        #[arg(short = 'j', long = "jobs")]
        jobs: Option<usize>,
        /// Integer overflow: `trap` / `wrap` / `ignore`
        #[arg(long, value_enum, default_value_t = OverflowCli::Wrap)]
        overflow: OverflowCli,
        /// Bounds checks: `default` (debug-only) or `always`
        #[arg(long, value_enum, default_value_t = BoundsCli::Default)]
        bounds: BoundsCli,
        /// Arguments forwarded to the compiled program (`argc` / `args` / `arg_get`)
        #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
        args: Vec<String>,
    },
    /// Format `.mko` sources (gofmt-compatible: stdout by default; `-w` write, `-l` list, `-d` diff)
    Fmt {
        /// Files or directories (default: `.`; workspace-aware with `-p`)
        #[arg(default_value = ".")]
        paths: Vec<PathBuf>,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Write result to (source) file instead of stdout
        #[arg(short = 'w', long = "write", default_value_t = false)]
        write: bool,
        /// List files whose formatting differs from canonical
        #[arg(short = 'l', long = "list", default_value_t = false)]
        list: bool,
        /// Display diffs instead of rewriting files
        #[arg(short = 'd', long = "diff", default_value_t = false)]
        diff: bool,
    },
    /// Discover and run *_test.mko (Test/Fuzz/Property/Snapshot/Mock/Fixture)
    Test {
        /// Test file or directory (default: `.`)
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Like `go test -run`: substring, `*`/`?` glob, or `/regex/`
        #[arg(long = "run", short = 'r')]
        run_filter: Option<String>,
        /// List matched test/category function names before each file runs
        #[arg(long = "verbose", short = 'v')]
        verbose: bool,
        /// Run the suite this many times (like `go test -count`)
        #[arg(long = "count", default_value_t = 1)]
        count: u32,
        /// Enable ThreadSanitizer (`-fsanitize=thread`), like `go test -race`
        #[arg(long = "race", default_value_t = false)]
        race: bool,
        /// Pass `-fsanitize=` (overrides `--race` if both set)
        #[arg(long = "sanitize")]
        sanitize: Option<String>,
        /// Print package-level source/test coverage and test category counts
        #[arg(long = "coverage", default_value_t = false)]
        coverage: bool,
    },
    /// Lint with a few real rules + typecheck (workspace-aware; optional `-p`)
    Lint {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Flag dual/compat spellings (`func`, `:=`, `import`, …) as style — Mako flair preferred
        #[arg(long, default_value_t = false)]
        identity: bool,
        /// Include shadowed variable warnings
        #[arg(long, default_value_t = false)]
        shadow: bool,
    },
    /// Time compile+run of bench_*.mko (workspace-aware; optional `-p`)
    Bench {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Focus a single workspace member
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Emit stable JSON benchmark report(s)
        #[arg(long, default_value_t = false)]
        json: bool,
    },
    /// DAP adapter seed: one-shot JSON or Content-Length stdio loop
    Dap {
        /// Single DAP request JSON (if omitted without --stdio, read one line from stdin)
        #[arg(long)]
        request: Option<String>,
        /// Multi-message DAP over stdin/stdout (Content-Length framing; exit on disconnect)
        #[arg(long, default_value_t = false)]
        stdio: bool,
        /// Max messages in --stdio mode (0 = unlimited until disconnect)
        #[arg(long, default_value_t = 0)]
        max_messages: u64,
    },
    /// Serve profile endpoints until max requests (continuous pprof seed)
    ProfileServe {
        /// TCP port (binds 127.0.0.1)
        #[arg(long, default_value_t = 9470)]
        port: u16,
        /// Exit after this many HTTP requests (0 = run until error; tests use small N)
        #[arg(long, default_value_t = 0)]
        max_requests: u64,
    },
    /// Build and run one program with compile/run timing and optional JSON profile
    Profile {
        /// Source file, package directory, or workspace root
        #[arg(default_value = ".")]
        file: PathBuf,
        /// Workspace member with `main.mko` when profiling a workspace
        #[arg(short = 'p', long = "package")]
        package: Option<String>,
        /// Optimize before profiling (`-O3 -flto`)
        #[arg(long, default_value_t = false)]
        release: bool,
        /// Disable incremental caches (default: on)
        #[arg(long, default_value_t = false)]
        no_incremental: bool,
        /// Parallel compile jobs (default: `MAKO_JOBS` or CPU count)
        #[arg(short = 'j', long = "jobs")]
        jobs: Option<usize>,
        /// Emit machine-readable JSON report
        #[arg(long, default_value_t = false)]
        json: bool,
        /// Arguments forwarded to the compiled program (`argc` / `args` / `arg_get`)
        #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
        args: Vec<String>,
    },
    /// Generate API markdown, runnable example commands, and a symbol search index
    Doc {
        #[arg(default_value = ".")]
        path: PathBuf,
        #[arg(long, default_value = "docs/api")]
        out: PathBuf,
    },
    /// Emit JSON symbol graph and AST summary metadata for tooling
    Metadata {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Refresh an installed Mako prefix from a source checkout
    Update {
        /// Source checkout containing scripts/install.sh or scripts/install.ps1
        #[arg(long, default_value = ".")]
        from: PathBuf,
        /// Install prefix to refresh (default: $HOME/.local)
        #[arg(long)]
        prefix: Option<PathBuf>,
        /// Reuse an existing release binary instead of building first
        #[arg(long, default_value_t = false)]
        skip_build: bool,
    },
    /// Verify install health: compiler, runtime, stdlib, toolchain, editor support
    Doctor,
    /// Deployment helpers
    Deploy {
        #[command(subcommand)]
        cmd: DeployCmd,
    },
    /// Packages: init / add / remove / list / fetch / install / update / lock / publish / audit
    Pkg {
        #[command(subcommand)]
        cmd: PkgCmd,
    },
    /// API compatibility tools
    Api {
        #[command(subcommand)]
        cmd: ApiCmd,
    },
    /// Print version (like `go version`) — also `mako --version` / `-V`
    Version {
        /// Second line: git commit when baked in at build time (`MAKO_GIT_HASH` / git)
        #[arg(short = 'v', long = "verbose", default_value_t = false)]
        verbose: bool,
    },
    /// Minimal language server (stdio JSON-RPC: initialize / hover / shutdown)
    Lsp,
}

#[derive(Subcommand)]
enum DeployCmd {
    /// Write a minimal Dockerfile and .dockerignore
    Docker {
        /// Project directory where Dockerfile will be written
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Mako entry file to build inside the container
        #[arg(long, default_value = "main.mko")]
        entry: PathBuf,
        /// Output binary name inside the container
        #[arg(long, default_value = "server")]
        bin: String,
        /// Port to expose
        #[arg(long, default_value_t = 8080)]
        port: u16,
        /// Final image style
        #[arg(long, value_enum, default_value_t = DeployDockerMode::Scratch)]
        mode: DeployDockerMode,
    },
    /// Write serverless/container-edge deployment starter files
    Serverless {
        /// Project directory where deployment files will be written
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Deployment provider
        #[arg(long, value_enum, default_value_t = DeployServerlessProvider::CloudRun)]
        provider: DeployServerlessProvider,
        /// Service/app name
        #[arg(long, default_value = "mako-app")]
        name: String,
        /// Container image reference (required for Cloud Run)
        #[arg(long)]
        image: Option<String>,
        /// Mako entry file to build inside the generated Dockerfile
        #[arg(long, default_value = "main.mko")]
        entry: PathBuf,
        /// Output binary name inside the container
        #[arg(long, default_value = "server")]
        bin: String,
        /// Port to expose
        #[arg(long, default_value_t = 8080)]
        port: u16,
    },
    /// Write browser/edge WASM starter files for WASI preview1 output
    Wasm {
        /// Directory where browser/edge files will be written
        #[arg(default_value = "wasm-dist")]
        path: PathBuf,
        /// Mako entry file to build to .wasm
        #[arg(long, default_value = "examples/wasi_hello.mko")]
        entry: PathBuf,
        /// Output wasm filename served by the generated page
        #[arg(long, default_value = "hello.wasm")]
        wasm: String,
        /// HTTP port used by the generated local preview command
        #[arg(long, default_value_t = 8080)]
        port: u16,
    },
    /// Write a native or WASM plugin starter using the Mako plugin ABI
    Plugin {
        /// Directory where plugin files will be written
        #[arg(default_value = "mako-plugin")]
        path: PathBuf,
        /// Plugin name
        #[arg(long, default_value = "mako-plugin")]
        name: String,
        /// Plugin kind
        #[arg(long, value_enum, default_value_t = DeployPluginKind::Native)]
        kind: DeployPluginKind,
    },
}

#[derive(Clone, Copy, Debug, ValueEnum)]
pub(crate) enum DeployDockerMode {
    /// Static binary copied into a scratch final image
    Scratch,
    /// Binary copied into a small Debian runtime image
    Debian,
}

#[derive(Clone, Copy, Debug, ValueEnum)]
pub(crate) enum DeployServerlessProvider {
    /// Google Cloud Run service manifest
    CloudRun,
    /// Fly.io app manifest
    Fly,
}

#[derive(Clone, Copy, Debug, ValueEnum)]
pub(crate) enum DeployPluginKind {
    /// Native dynamic library starter using mako_plugin.h
    Native,
    /// WASM plugin manifest and exported function starter
    Wasm,
}

#[derive(Subcommand)]
enum PkgCmd {
    /// Scaffold mako.toml + main.mko (same as `mako init`)
    Init {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// List package name + dependencies from mako.toml (path + git; validates on-disk roots)
    List {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Clone git dependencies into `.mako/deps/` (needs `git` + network; not run in default CI)
    Fetch {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Resolve deps + write mako.lock (alias of install)
    Lock {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Do not fetch git dependencies; require local cache/registry contents
        #[arg(long, default_value_t = false)]
        offline: bool,
    },
    /// Resolve all deps (SemVer + path + git + local registry), fetch, write mako.lock
    Install {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Do not fetch git dependencies; require local cache/registry contents
        #[arg(long, default_value_t = false)]
        offline: bool,
    },
    /// Re-resolve within SemVer bounds and refresh mako.lock
    Update {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Do not fetch git dependencies; require local cache/registry contents
        #[arg(long, default_value_t = false)]
        offline: bool,
    },
    /// Publish an immutable version to the local registry (`.mako/registry` or `$MAKO_REGISTRY`)
    Publish {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Generate PACKAGE.sha256 for legacy packages published without a digest.
    /// Run this inside the registry version directory, or pass its path.
    Seal {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Record a path or URL dependency in mako.toml (updates in place if already present)
    ///
    /// Forms: `mako pkg add helper ../helper` · `mako pkg add helper path=../helper` ·
    /// `mako pkg add path=../helper` (name from directory basename)
    Add {
        /// Dependency name, or `path=...` shorthand
        name: String,
        /// Local path / URL, or `path=...` when name is set
        source: Option<String>,
        #[arg(long, default_value = "0.1.0")]
        version: String,
        #[arg(long = "dir", short = 'C', default_value = ".")]
        project: PathBuf,
    },
    /// Drop a `[dependencies]` entry from mako.toml
    Remove {
        /// Dependency name (e.g. `helper`)
        name: String,
        #[arg(long = "dir", short = 'C', default_value = ".")]
        project: PathBuf,
    },
    /// Audit mako.lock against optional local advisory and license policy files
    Audit {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
}

#[derive(Subcommand)]
enum ApiCmd {
    /// Compare two files/directories and report breaking public API changes
    Diff {
        /// Previous release source file or directory
        old: PathBuf,
        /// New release source file or directory
        new: PathBuf,
    },
}

pub(crate) struct BuildOpts {
    pub(crate) target: Option<String>,
    pub(crate) sanitize: Option<String>,
    pub(crate) static_link: bool,
    pub(crate) overflow: OverflowMode,
    pub(crate) bounds_always: bool,
}

impl Default for BuildOpts {
    fn default() -> Self {
        Self {
            target: None,
            sanitize: None,
            static_link: false,
            overflow: OverflowMode::Wrap,
            bounds_always: false,
        }
    }
}



#[derive(Clone, Copy, Debug, Default, ValueEnum)]
enum BoundsCli {
    #[default]
    Default,
    Always,
}

fn static_link_default(target: Option<&str>) -> bool {
    target.is_some_and(|t| t.contains("linux-musl"))
}

fn effective_static_link(target: Option<&str>, requested: bool, disabled: bool) -> bool {
    if disabled {
        false
    } else {
        requested || static_link_default(target)
    }
}

fn main() {
    let ver: &'static str = Box::leak(clap_version_string().into_boxed_str());
    let matches = Cli::command().version(ver).get_matches();
    let cli = Cli::from_arg_matches(&matches).unwrap_or_else(|e| e.exit());
    if let Err(()) = run(cli) {
        std::process::exit(1);
    }
}

/// Clap prints `{name} {version}` → with this string: `mako version mako0.2.2 darwin/arm64`.
fn clap_version_string() -> String {
    format!(
        "version mako{} {}/{}",
        env!("CARGO_PKG_VERSION"),
        mako_goos(),
        mako_goarch()
    )
}

fn version_line() -> String {
    format!(
        "mako version mako{} {}/{}",
        env!("CARGO_PKG_VERSION"),
        mako_goos(),
        mako_goarch()
    )
}

fn mako_goos() -> &'static str {
    match std::env::consts::OS {
        "macos" => "darwin",
        other => other,
    }
}

fn mako_goarch() -> &'static str {
    match std::env::consts::ARCH {
        "x86_64" => "amd64",
        "aarch64" => "arm64",
        "x86" => "386",
        other => other,
    }
}

fn print_version(verbose: bool) {
    println!("{}", version_line());
    if verbose {
        match option_env!("MAKO_GIT_HASH") {
            Some(h) if !h.is_empty() => println!("commit: {h}"),
            _ => println!("commit: unknown"),
        }
    }
}

fn dap_extract_command(req: &str) -> String {
    if let Some(i) = req.find("\"command\"") {
        let rest = &req[i + 9..];
        if let Some(q1) = rest.find('"') {
            let rest = &rest[q1 + 1..];
            if let Some(q2) = rest.find('"') {
                return rest[..q2].to_string();
            }
        }
    }
    String::new()
}

fn dap_extract_seq(req: &str) -> i64 {
    if let Some(i) = req.find("\"seq\"") {
        let rest = &req[i + 5..];
        return rest
            .chars()
            .skip_while(|c| !c.is_ascii_digit())
            .take_while(|c| c.is_ascii_digit())
            .collect::<String>()
            .parse::<i64>()
            .unwrap_or(0);
    }
    0
}

fn dap_respond_json(req: &str) -> (String, bool) {
    let cmd = dap_extract_command(req);
    let seq = dap_extract_seq(req);
    let disconnect = cmd == "disconnect";
    let out = match cmd.as_str() {
        "initialize" => format!(
            r#"{{"seq":1,"type":"response","request_seq":{seq},"success":true,"command":"initialize","body":{{"supportsConfigurationDoneRequest":true,"supportsEvaluateForHovers":true,"makoSeed":true,"schema":"mako.dap.v1"}}}}"#
        ),
        "threads" => format!(
            r#"{{"seq":3,"type":"response","request_seq":{seq},"success":true,"command":"threads","body":{{"threads":[{{"id":1,"name":"main"}}]}}}}"#
        ),
        "disconnect" | "configurationDone" => format!(
            r#"{{"seq":9,"type":"response","request_seq":{seq},"success":true,"command":"{cmd}"}}"#
        ),
        "stackTrace" => format!(
            r#"{{"seq":11,"type":"response","request_seq":{seq},"success":true,"command":"stackTrace","body":{{"stackFrames":[{{"id":1,"name":"main","line":1,"column":1,"source":{{"name":"main.mko"}}}}],"totalFrames":1}}}}"#
        ),
        "scopes" => format!(
            r#"{{"seq":12,"type":"response","request_seq":{seq},"success":true,"command":"scopes","body":{{"scopes":[{{"name":"Locals","variablesReference":1,"expensive":false}}]}}}}"#
        ),
        "variables" => format!(
            r#"{{"seq":13,"type":"response","request_seq":{seq},"success":true,"command":"variables","body":{{"variables":[]}}}}"#
        ),
        "continue" => format!(
            r#"{{"seq":14,"type":"response","request_seq":{seq},"success":true,"command":"continue","body":{{"allThreadsContinued":true}}}}"#
        ),
        "next" | "stepIn" | "stepOut" => format!(
            r#"{{"seq":15,"type":"response","request_seq":{seq},"success":true,"command":"{cmd}"}}"#
        ),
        "setBreakpoints" => format!(
            r#"{{"seq":16,"type":"response","request_seq":{seq},"success":true,"command":"setBreakpoints","body":{{"breakpoints":[]}}}}"#
        ),
        other => format!(
            r#"{{"seq":99,"type":"response","request_seq":{seq},"success":false,"command":"{other}","message":"unsupported (mako dap seed)"}}"#
        ),
    };
    (out, disconnect)
}

/// Thin DAP seed: one request → one response JSON on stdout.
fn cmd_dap_seed(request: Option<&str>) -> Result<(), ()> {
    let req = if let Some(r) = request {
        r.to_string()
    } else {
        use std::io::Read;
        let mut buf = String::new();
        std::io::stdin()
            .read_to_string(&mut buf)
            .map_err(|e| emit_plain_error(&format!("dap: read stdin: {e}")))?;
        buf.trim().to_string()
    };
    if req.is_empty() {
        emit_plain_error("dap: empty request (pass --request or pipe JSON on stdin)");
        return Err(());
    }
    let (out, _) = dap_respond_json(&req);
    println!("{out}");
    Ok(())
}

/// Multi-message DAP over stdio with Content-Length framing (VS Code-style).
fn cmd_dap_stdio(max_messages: u64) -> Result<(), ()> {
    use std::io::{BufRead, BufReader, Read, Write};
    let stdin = std::io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    let mut stdout = std::io::stdout();
    let mut n: u64 = 0;
    loop {
        if max_messages > 0 && n >= max_messages {
            break;
        }
        // Headers until blank line
        let mut content_len: Option<usize> = None;
        loop {
            let mut line = String::new();
            let r = reader
                .read_line(&mut line)
                .map_err(|e| emit_plain_error(&format!("dap stdio: {e}")))?;
            if r == 0 {
                return Ok(()); // EOF
            }
            let t = line.trim_end_matches(['\r', '\n']);
            if t.is_empty() {
                break;
            }
            if let Some(rest) = t
                .strip_prefix("Content-Length:")
                .or_else(|| t.strip_prefix("content-length:"))
            {
                content_len = rest.trim().parse().ok();
            }
        }
        let len = content_len.unwrap_or(0);
        if len == 0 {
            continue;
        }
        let mut body = vec![0u8; len];
        reader
            .read_exact(&mut body)
            .map_err(|e| emit_plain_error(&format!("dap stdio body: {e}")))?;
        let req = String::from_utf8_lossy(&body);
        let (resp, disconnect) = dap_respond_json(req.trim());
        let header = format!("Content-Length: {}\r\n\r\n", resp.len());
        stdout
            .write_all(header.as_bytes())
            .and_then(|_| stdout.write_all(resp.as_bytes()))
            .and_then(|_| stdout.flush())
            .map_err(|e| emit_plain_error(&format!("dap stdio write: {e}")))?;
        n += 1;
        if disconnect {
            break;
        }
    }
    Ok(())
}

/// Continuous-ish profile HTTP seed on 127.0.0.1:port (exits after max_requests if > 0).
fn cmd_profile_serve(port: u16, max_requests: u64) -> Result<(), ()> {
    use std::io::{Read, Write};
    use std::net::TcpListener;
    let addr = format!("127.0.0.1:{port}");
    let listener = TcpListener::bind(&addr).map_err(|e| {
        emit_plain_error(&format!("profile-serve: bind {addr}: {e}"));
    })?;
    eprintln!("mako profile-serve: http://{addr}/debug/pprof/text (and /json, /debug/profile)");
    let mut served: u64 = 0;
    for stream in listener.incoming() {
        let mut stream = match stream {
            Ok(s) => s,
            Err(_) => continue,
        };
        let mut buf = [0u8; 4096];
        let n = stream.read(&mut buf).unwrap_or(0);
        let req = String::from_utf8_lossy(&buf[..n]);
        let path = req
            .lines()
            .next()
            .and_then(|l| l.split_whitespace().nth(1))
            .unwrap_or("/");
        let body: String = if path.starts_with("/debug/pprof/text") {
            format!(
                "# mako.profile_pprof_text.v1 samples=0\n(no in-process samples — CLI seed)\n"
            )
        } else if path.starts_with("/debug/pprof/json") {
            r#"{"schema":"mako.profile_samples.v1","count":0,"samples":[],"note":"cli seed"}"#
                .into()
        } else if path.starts_with("/debug/profile") {
            r#"{"schema":"mako.profile_snapshot.v1","note":"cli seed"}"#.into()
        } else if path == "/" || path == "/health" {
            "ok\n".into()
        } else {
            "paths: /debug/pprof/text /debug/pprof/json /debug/profile /health\n".into()
        };
        let status = if path.starts_with("/debug/") || path == "/" || path == "/health" {
            "200 OK"
        } else {
            "404 Not Found"
        };
        let ctype = if path.contains("json") || path == "/debug/profile" {
            "application/json"
        } else {
            "text/plain; charset=utf-8"
        };
        let resp = format!(
            "HTTP/1.0 {status}\r\nContent-Type: {ctype}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
            body.len()
        );
        let _ = stream.write_all(resp.as_bytes());
        served += 1;
        if max_requests > 0 && served >= max_requests {
            break;
        }
    }
    Ok(())
}

fn cmd_doctor() -> Result<(), ()> {
    println!("mako doctor");
    println!("  version: {}", version_line());
    let mut ok = true;

    match std::env::current_exe() {
        Ok(exe) => println!("  binary:  ok ({})", exe.display()),
        Err(e) => {
            ok = false;
            println!("  binary:  FAIL ({e})");
        }
    }

    match runtime_include_dir() {
        Ok(rt) => {
            println!("  runtime: ok ({})", rt.display());
            for header in [
                "mako_rt.h",
                "mako_http.h",
                "mako_db.h",
                "mako_security.h",
                "mako_plugin.h",
                "mako_trace.h",
                "mako_std.h",
            ] {
                if rt.join(header).exists() {
                    println!("    {header}: ok");
                } else {
                    ok = false;
                    println!("    {header}: FAIL missing");
                }
            }
            // Install manifest next to share/mako (validate schema + host seed).
            if let Some(share) = rt.parent() {
                let man = share.join("install-manifest.json");
                if man.exists() {
                    match std::fs::read_to_string(&man) {
                        Ok(body) => {
                            let schema_ok = body.contains("mako.install.v1");
                            let has_ver = body.contains("\"version\"");
                            let has_prefix = body.contains("\"prefix\"");
                            let host_line = body
                                .lines()
                                .find(|l| l.contains("\"host\""))
                                .unwrap_or("")
                                .trim()
                                .to_string();
                            let cur_host = format!(
                                "{}-{}",
                                std::env::consts::OS,
                                std::env::consts::ARCH
                            );
                            // uname-style host in manifest is Darwin-arm64; consts is macos/aarch64.
                            let host_ok = !host_line.is_empty();
                            if schema_ok && has_ver && has_prefix {
                                println!(
                                    "  install: ok (manifest {} · {})",
                                    man.display(),
                                    if host_ok { host_line.as_str() } else { "host?" }
                                );
                                println!("  install host now: {} / {}", std::env::consts::OS, std::env::consts::ARCH);
                                let _ = cur_host;
                            } else {
                                ok = false;
                                println!("  install: FAIL manifest missing schema/version/prefix");
                            }
                        }
                        Err(e) => {
                            ok = false;
                            println!("  install: FAIL read manifest ({e})");
                        }
                    }
                } else {
                    println!("  install: warn no install-manifest.json (source checkout ok)");
                }
            }
        }
        Err(e) => {
            ok = false;
            println!("  runtime: FAIL ({e})");
        }
    }

    if let Some(std_dir) = discover_std_dir() {
        println!("  stdlib:  ok ({})", std_dir.display());
    } else {
        ok = false;
        println!("  stdlib:  FAIL (could not find std/strings/strings.mko)");
    }

    println!(
        "  host:    {} / {}",
        std::env::consts::OS,
        std::env::consts::ARCH
    );

    if command_on_path("clang") {
        println!("  clang:   ok");
    } else {
        ok = false;
        println!("  clang:   FAIL (install LLVM clang or Xcode command line tools)");
    }

    if command_on_path("zig") {
        println!("  zig:     ok (optional cross compiler)");
    } else {
        println!("  zig:     warn missing (optional; useful for cross-compilation)");
    }

    match discover_vscode_extension_dir() {
        Some(p) => println!("  vscode:  ok ({})", p.display()),
        None => println!("  vscode:  warn extension scaffold not found/installed"),
    }

    if ok {
        println!("doctor: ok");
        Ok(())
    } else {
        println!("doctor: failed");
        Err(())
    }
}

fn cmd_update(from: &Path, prefix: Option<&Path>, skip_build: bool) -> Result<(), ()> {
    let source = from.canonicalize().map_err(|e| {
        emit_plain_error(&format!("update: source {} not found: {e}", from.display()));
    })?;

    #[cfg(target_os = "windows")]
    let script = source.join("scripts").join("install.ps1");
    #[cfg(not(target_os = "windows"))]
    let script = source.join("scripts").join("install.sh");

    if !script.exists() {
        emit_plain_error(&format!(
            "update: {} is not a Mako source checkout (missing {})",
            source.display(),
            script.display()
        ));
        return Err(());
    }

    println!("mako update");
    println!("  from:   {}", source.display());
    if let Some(p) = prefix {
        println!("  prefix: {}", p.display());
    }

    #[cfg(target_os = "windows")]
    let mut cmd = {
        let mut c = Command::new("powershell");
        c.arg("-ExecutionPolicy")
            .arg("Bypass")
            .arg("-File")
            .arg(&script);
        if let Some(p) = prefix {
            c.arg("-Prefix").arg(p);
        }
        if skip_build {
            c.arg("-SkipBuild");
        }
        c
    };

    #[cfg(not(target_os = "windows"))]
    let mut cmd = {
        let mut c = Command::new(&script);
        if skip_build {
            c.arg("--skip-build");
        }
        if let Some(p) = prefix {
            c.env("PREFIX", p);
        }
        c
    };

    let status = cmd.status().map_err(|e| {
        emit_plain_error(&format!("update: failed to run installer: {e}"));
    })?;
    if !status.success() {
        emit_plain_error(&format!(
            "update: installer exited {}",
            status.code().unwrap_or(-1)
        ));
        return Err(());
    }
    println!("update: complete");
    Ok(())
}

fn command_on_path(cmd: &str) -> bool {
    Command::new(cmd)
        .arg("--version")
        .output()
        .map(|out| out.status.success())
        .unwrap_or(false)
}

fn opt_level(release: bool) -> OptLevel {
    if release {
        OptLevel::Release
    } else {
        OptLevel::Debug
    }
}

fn run(cli: Cli) -> Result<(), ()> {
    match cli.command {
        Commands::Init {
            path,
            name,
            workspace,
            backend,
        } => {
            if workspace && backend {
                emit_plain_error("use either --workspace or --backend, not both");
                Err(())
            } else if workspace {
                cmd_init_workspace(&path, name.as_deref())
            } else if backend {
                cmd_init_backend(&path, name.as_deref())
            } else {
                cmd_init(&path, name.as_deref())
            }
        }
        Commands::Check {
            file,
            package,
            json,
            no_incremental,
        } => cmd_check(&file, package.as_deref(), !no_incremental, json),
        Commands::Build {
            file,
            package,
            out,
            emit_c,
            release,
            time,
            target,
            sanitize,
            static_link,
            no_static_link,
            no_incremental,
            jobs,
            overflow,
            bounds,
        } => {
            let static_link = effective_static_link(target.as_deref(), static_link, no_static_link);
            cmd_build(
                &file,
                package.as_deref(),
                out,
                emit_c,
                release,
                time,
                target,
                sanitize,
                static_link,
                !no_incremental,
                jobs,
                overflow.into(),
                matches!(bounds, BoundsCli::Always),
            )
        }
        Commands::Run {
            file,
            package,
            release,
            time,
            no_incremental,
            jobs,
            overflow,
            bounds,
            args,
        } => cmd_run(
            &file,
            package.as_deref(),
            release,
            time,
            !no_incremental,
            jobs,
            overflow.into(),
            matches!(bounds, BoundsCli::Always),
            &args,
        ),
        Commands::Dev {
            file,
            package,
            interval_ms,
            release,
            overflow,
        } => cmd_dev(
            &file,
            package.as_deref(),
            interval_ms,
            release,
            overflow.into(),
        ),
        Commands::Fmt {
            paths,
            package,
            write,
            list,
            diff,
        } => {
            let roots = if paths.is_empty() {
                vec![PathBuf::from(".")]
            } else {
                paths
            };
            let mut ok = true;
            for path in &roots {
                let r = cmd_tool_paths(path, package.as_deref(), |member| {
                    tooling::run_fmt(member, tooling::FmtOptions { write, list, diff })
                });
                if r.is_err() {
                    ok = false;
                }
            }
            if ok {
                Ok(())
            } else {
                Err(())
            }
        }
        Commands::Test {
            path,
            package,
            run_filter,
            verbose,
            count,
            race,
            sanitize,
            coverage,
        } => {
            let count = count.max(1);
            let sanitize = sanitize.or_else(|| race.then(|| "thread".into()));
            let mut last_ok = Ok(());
            for i in 0..count {
                if count > 1 {
                    println!("mako test: count {}/{}", i + 1, count);
                }
                last_ok = cmd_test(
                    &path,
                    package.as_deref(),
                    run_filter.as_deref(),
                    verbose,
                    coverage,
                    sanitize.as_deref(),
                );
                if last_ok.is_err() {
                    break;
                }
            }
            last_ok
        }
        Commands::Lint {
            path,
            package,
            identity,
            shadow,
        } => {
            if shadow {
                std::env::set_var("MAKO_LINT_SHADOW", "1");
            }
            cmd_tool_paths(&path, package.as_deref(), |member| {
                tooling::run_lint(member, identity)
            })
        }
        Commands::Bench {
            path,
            package,
            json,
        } => cmd_bench_workspace(&path, package.as_deref(), json),
        Commands::Profile {
            file,
            package,
            release,
            no_incremental,
            jobs,
            json,
            args,
        } => cmd_profile(
            &file,
            package.as_deref(),
            release,
            !no_incremental,
            jobs,
            json,
            &args,
        ),
        Commands::Doc { path, out } => tooling::run_doc(&path, &out),
        Commands::Metadata { path } => tooling::run_metadata(&path),
        Commands::Update {
            from,
            prefix,
            skip_build,
        } => cmd_update(&from, prefix.as_deref(), skip_build),
        Commands::Doctor => cmd_doctor(),
        Commands::Deploy { cmd } => match cmd {
            DeployCmd::Docker {
                path,
                entry,
                bin,
                port,
                mode,
            } => tooling::run_deploy_docker(&path, &entry, &bin, port, mode),
            DeployCmd::Serverless {
                path,
                provider,
                name,
                image,
                entry,
                bin,
                port,
            } => tooling::run_deploy_serverless(
                &path,
                provider,
                &name,
                image.as_deref(),
                &entry,
                &bin,
                port,
            ),
            DeployCmd::Wasm {
                path,
                entry,
                wasm,
                port,
            } => tooling::run_deploy_wasm(&path, &entry, &wasm, port),
            DeployCmd::Plugin { path, name, kind } => {
                tooling::run_deploy_plugin(&path, &name, kind)
            }
        },
        Commands::Lsp => lsp::run_stdio().map_err(|e| {
            emit_plain_error(&format!("lsp: {e}"));
        }),
        Commands::Dap {
            request,
            stdio,
            max_messages,
        } => {
            if stdio {
                cmd_dap_stdio(max_messages)
            } else {
                cmd_dap_seed(request.as_deref())
            }
        }
        Commands::ProfileServe { port, max_requests } => {
            cmd_profile_serve(port, max_requests)
        }
        Commands::Pkg { cmd } => run_pkg(cmd),
        Commands::Api { cmd } => match cmd {
            ApiCmd::Diff { old, new } => tooling::run_api_diff(&old, &new),
        },
        Commands::Version { verbose } => {
            print_version(verbose);
            Ok(())
        }
    }
}

/// Resolve `path` to a list of `.mko` entry points.
/// Workspace root (`[workspace] members`) → each member's `main.mko` / `lib.mko`.
/// Optional `package` focuses one member by directory name.
fn resolve_compile_targets(path: &Path, package: Option<&str>) -> Result<Vec<PathBuf>, ()> {
    resolve_package_entries(path, false, package)
}

fn member_basename(dir: &Path) -> String {
    dir.file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_string()
}

fn filter_workspace_members(
    members: Vec<PathBuf>,
    package: Option<&str>,
) -> Result<Vec<PathBuf>, ()> {
    let Some(want) = package else {
        return Ok(members);
    };
    let matched: Vec<_> = members
        .into_iter()
        .filter(|m| member_basename(m) == want)
        .collect();
    if matched.is_empty() {
        emit_plain_error(&format!(
            "workspace: no member named `{want}` — use `-p NAME` with a `[workspace] members` entry"
        ));
        return Err(());
    }
    Ok(matched)
}

/// Paths to operate on for fmt/lint/bench: workspace members, or `path` itself.
fn resolve_tool_dirs(path: &Path, package: Option<&str>) -> Result<Vec<PathBuf>, ()> {
    if path.is_file() {
        if package.is_some() {
            emit_plain_error("-p/--package is only valid with a workspace directory");
            return Err(());
        }
        return Ok(vec![path.to_path_buf()]);
    }
    if !path.is_dir() {
        emit_plain_error(&format!("path not found: {}", path.display()));
        return Err(());
    }
    if let Some(members) = tooling::workspace_member_dirs(path) {
        let members = filter_workspace_members(members, package)?;
        let mut out = Vec::new();
        for m in members {
            if !m.is_dir() {
                emit_plain_error(&format!("workspace member missing: {}", m.display()));
                return Err(());
            }
            out.push(m);
        }
        if out.is_empty() {
            emit_plain_error("workspace has no members");
            return Err(());
        }
        return Ok(out);
    }
    if package.is_some() {
        emit_plain_error(
            "-p/--package requires a workspace root with `[workspace] members` in mako.toml",
        );
        return Err(());
    }
    Ok(vec![path.to_path_buf()])
}

fn cmd_tool_paths(
    path: &Path,
    package: Option<&str>,
    mut each: impl FnMut(&Path) -> Result<(), ()>,
) -> Result<(), ()> {
    let dirs = resolve_tool_dirs(path, package)?;
    let multi = dirs.len() > 1;
    for d in &dirs {
        if multi {
            println!("=== workspace member {} ===", d.display());
        }
        each(d)?;
    }
    Ok(())
}

fn member_has_benches(dir: &Path) -> bool {
    tooling::collect_mako_files(dir).into_iter().any(|f| {
        let n = f.file_name().and_then(|x| x.to_str()).unwrap_or("");
        n.starts_with("bench_") || n.contains("bench")
    })
}

fn cmd_bench_workspace(path: &Path, package: Option<&str>, json: bool) -> Result<(), ()> {
    let dirs = resolve_tool_dirs(path, package)?;
    let multi =
        dirs.len() > 1 || (package.is_none() && tooling::workspace_member_dirs(path).is_some());
    let mut any = false;
    for d in &dirs {
        if !member_has_benches(d) {
            if package.is_some() {
                emit_plain_error(&format!(
                    "workspace member `{}` has no bench_*.mko",
                    member_basename(d)
                ));
                return Err(());
            }
            if multi && !json {
                eprintln!("mako bench: {} (no bench_*.mko — skip)", d.display());
            }
            continue;
        }
        any = true;
        if multi && !json {
            println!("=== workspace member {} ===", d.display());
        }
        if json {
            tooling::run_bench(d, json, &|f| run_file_silent(f))?;
        } else {
            tooling::run_bench(d, json, &|f| run_file_quiet(f))?;
        }
    }
    if !any {
        if multi {
            emit_plain_error(&format!(
                "workspace {}: no bench_*.mko in any member",
                path.display()
            ));
        } else {
            emit_plain_error(&format!(
                "mako bench: no bench_*.mko under {}",
                path.display()
            ));
        }
        return Err(());
    }
    Ok(())
}

fn list_runnable_member_names(members: &[PathBuf]) -> Vec<String> {
    members
        .iter()
        .filter(|m| tooling::package_main_mko(m).is_some())
        .map(|m| member_basename(m))
        .collect()
}

/// When `mains_only`, workspace/package dirs without `main.mko` are skipped (lib-only).
fn resolve_package_entries(
    path: &Path,
    mains_only: bool,
    package: Option<&str>,
) -> Result<Vec<PathBuf>, ()> {
    if path.is_file() {
        if package.is_some() {
            emit_plain_error("-p/--package is only valid with a workspace or package directory");
            return Err(());
        }
        return Ok(vec![path.to_path_buf()]);
    }
    if path.is_dir() {
        if let Some(members) = tooling::workspace_member_dirs(path) {
            let members = filter_workspace_members(members, package)?;
            let mut targets = Vec::new();
            let mut missing = Vec::new();
            for m in &members {
                if !m.is_dir() {
                    missing.push(m.display().to_string());
                    continue;
                }
                let entry = if mains_only {
                    tooling::package_main_mko(m)
                } else {
                    tooling::package_entry_mko(m)
                };
                match entry {
                    Some(entry) => targets.push(entry),
                    None if mains_only => {
                        if package.is_some() {
                            emit_plain_error(&format!(
                                "workspace member `{}` has no main.mko (library-only — cannot build/run)",
                                member_basename(m)
                            ));
                            return Err(());
                        }
                        eprintln!(
                            "mako: skip workspace member `{}` (no main.mko — library-only)",
                            m.display()
                        );
                    }
                    None => {
                        emit_plain_error(&format!(
                            "workspace member `{}` has no main.mko or lib.mko",
                            m.display()
                        ));
                        return Err(());
                    }
                }
            }
            if !missing.is_empty() {
                emit_plain_error(&format!(
                    "workspace member(s) missing on disk: {}",
                    missing.join(", ")
                ));
                return Err(());
            }
            if targets.is_empty() {
                emit_plain_error(
                    "workspace: no buildable members (need at least one package with main.mko)",
                );
                return Err(());
            }
            return Ok(targets);
        }
        if package.is_some() {
            emit_plain_error(
                "-p/--package requires a workspace root with `[workspace] members` in mako.toml",
            );
            return Err(());
        }
        let entry = if mains_only {
            tooling::package_main_mko(path).or_else(|| tooling::package_entry_mko(path))
        } else {
            tooling::package_entry_mko(path)
        };
        if let Some(entry) = entry {
            return Ok(vec![entry]);
        }
        emit_plain_error(&format!(
            "no .mko entry and no [workspace] members under {}",
            path.display()
        ));
        return Err(());
    }
    emit_plain_error(&format!("path not found: {}", path.display()));
    Err(())
}

fn resolve_run_entry(path: &Path, package: Option<&str>) -> Result<PathBuf, ()> {
    if path.is_file() {
        if package.is_some() {
            emit_plain_error("-p/--package is only valid with a workspace directory");
            return Err(());
        }
        return Ok(path.to_path_buf());
    }
    if !path.is_dir() {
        emit_plain_error(&format!("path not found: {}", path.display()));
        return Err(());
    }
    if let Some(members) = tooling::workspace_member_dirs(path) {
        let runnable = list_runnable_member_names(&members);
        if let Some(want) = package {
            let members = filter_workspace_members(members, Some(want))?;
            let m = &members[0];
            return tooling::package_main_mko(m).ok_or_else(|| {
                emit_plain_error(&format!(
                    "workspace member `{want}` has no main.mko — pick a runnable member: {}",
                    if runnable.is_empty() {
                        "(none)".into()
                    } else {
                        runnable.join(", ")
                    }
                ));
            });
        }
        match runnable.as_slice() {
            [] => {
                emit_plain_error(
                    "workspace: no member with main.mko — add a binary package or pass a .mko file",
                );
                Err(())
            }
            [only] => {
                let dir = path.join(only);
                tooling::package_main_mko(&dir).ok_or_else(|| {
                    emit_plain_error(&format!("workspace member `{only}` lost main.mko"));
                })
            }
            many => {
                emit_plain_error(&format!(
                    "workspace has multiple runnable members ({}) — specify `-p NAME` (e.g. `mako run -p {}`)",
                    many.join(", "),
                    many[0]
                ));
                Err(())
            }
        }
    } else if let Some(entry) = tooling::package_main_mko(path) {
        if package.is_some() {
            emit_plain_error("-p/--package requires a workspace root with `[workspace] members`");
            return Err(());
        }
        Ok(entry)
    } else {
        emit_plain_error(&format!(
            "no main.mko under {} — pass a .mko file or use a workspace with `-p NAME`",
            path.display()
        ));
        Err(())
    }
}

fn make_incr_opts(
    incremental: bool,
    release: bool,
    jobs: Option<usize>,
    build: &BuildOpts,
) -> incremental::IncrOptions {
    let cc = cc::resolve_cc(build);
    let mut flags = String::new();
    flags.push_str("compiler=");
    flags.push_str(&cc.display().to_string());
    flags.push(';');
    flags.push_str("compiler-version=");
    flags.push_str(&incremental::compiler_identity(&cc));
    flags.push(';');
    if release {
        flags.push_str(if incremental::release_lto_enabled() {
            "release-opt=O3+flto;"
        } else {
            "release-opt=O3;"
        });
    }
    if let Some(t) = &build.target {
        flags.push_str("target=");
        flags.push_str(t);
        flags.push(';');
    }
    if let Some(s) = &build.sanitize {
        flags.push_str("sanitize=");
        flags.push_str(s);
        flags.push(';');
    }
    if build.static_link {
        flags.push_str("static;");
    }
    flags.push_str(build.overflow.cache_tag());
    if build.bounds_always {
        flags.push_str("bounds=always;");
    }

    // External flags and PGO profiles can affect generated objects without
    // changing Mako source.  They are intentionally non-cacheable: a raw
    // flag string cannot describe headers or changing profile contents.
    let external_codegen_inputs = std::env::var_os("MAKO_CFLAGS").is_some()
        || std::env::var_os("MAKO_PGO_GEN").is_some()
        || std::env::var_os("MAKO_PGO_USE").is_some();
    if let Ok(extra) = std::env::var("MAKO_CFLAGS") {
        flags.push_str("external-cflags=");
        flags.push_str(&extra);
        flags.push(';');
    }
    if let Some(mode) = std::env::var_os("MAKO_PGO_GEN") {
        flags.push_str("pgo-gen=");
        flags.push_str(&mode.to_string_lossy());
        flags.push(';');
    }
    if let Some(profile) = std::env::var_os("MAKO_PGO_USE") {
        flags.push_str("pgo-use=");
        flags.push_str(&profile.to_string_lossy());
        flags.push(';');
    }
    incremental::IncrOptions {
        incremental: incremental && !external_codegen_inputs,
        jobs: jobs.unwrap_or_else(incremental::default_jobs).max(1),
        release,
        verbose_cache: std::env::var_os("MAKO_CACHE_LOG").is_some(),
        flags_fp: flags,
        cc,
        overflow: build.overflow,
        bounds_always: build.bounds_always,
    }
}

fn default_out_bin(file: &Path) -> PathBuf {
    let stem = file.file_stem().and_then(|s| s.to_str()).unwrap_or("a.out");
    let base = if stem == "main" {
        if let Some(pkg) = nearest_package_name(file) {
            PathBuf::from(pkg)
        } else {
            PathBuf::from(stem)
        }
    } else if stem == "lib" {
        if let Some(parent) = file.parent() {
            if let Some(name) = parent.file_name().and_then(|s| s.to_str()) {
                PathBuf::from(name)
            } else {
                PathBuf::from(stem)
            }
        } else {
            PathBuf::from(stem)
        }
    } else {
        file.with_extension("")
            .file_name()
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("a.out"))
    };
    #[cfg(target_os = "windows")]
    {
        cc::with_exe_suffix(
            base,
            &BuildOpts::default(),
        )
    }
    #[cfg(not(target_os = "windows"))]
    {
        base
    }
}

/// `mako dev` — poll source mtime and rebuild+rerun (hot reload seed).
fn cmd_dev(
    path: &Path,
    package: Option<&str>,
    interval_ms: u64,
    release: bool,
    overflow: OverflowMode,
) -> Result<(), ()> {
    use std::time::Duration;
    let file = resolve_run_entry(path, package)?;
    eprintln!(
        "mako dev: watching {} (interval {}ms, Ctrl-C to stop)",
        file.display(),
        interval_ms
    );
    let mut last_mtime = fs::metadata(&file)
        .and_then(|m| m.modified())
        .ok();
    // Initial run
    let _ = cmd_run(
        path,
        package,
        release,
        false,
        true,
        None,
        overflow,
        false,
        &[],
    );
    loop {
        std::thread::sleep(Duration::from_millis(interval_ms.max(50)));
        let mtime = match fs::metadata(&file).and_then(|m| m.modified()) {
            Ok(t) => t,
            Err(_) => continue,
        };
        if last_mtime.map(|t| mtime > t).unwrap_or(true) {
            last_mtime = Some(mtime);
            eprintln!("mako dev: change detected — rebuild");
            let _ = cmd_run(
                path,
                package,
                release,
                false,
                true,
                None,
                overflow,
                false,
                &[],
            );
        }
    }
}

fn cmd_check(path: &Path, package: Option<&str>, incremental: bool, json: bool) -> Result<(), ()> {
    let targets = resolve_compile_targets(path, package)?;
    if json {
        let mut reports = Vec::new();
        let mut ok = true;
        for t in &targets {
            let (file_ok, report) = tooling::check_file_json_report(t);
            if !file_ok {
                ok = false;
            }
            reports.push(report);
        }
        println!("[{}]", reports.join(","));
        return if ok { Ok(()) } else { Err(()) };
    }
    let multi = targets.len() > 1;
    let incr = make_incr_opts(
        incremental,
        false,
        None,
        &BuildOpts {
            target: None,
            sanitize: None,
            static_link: false,
            overflow: OverflowMode::Wrap,
            bounds_always: false,
        },
    );
    for t in &targets {
        if multi {
            println!("check {}", t.display());
        }
        compile_to_ast_with(t, &incr)?;
        if multi {
            println!("ok {}", t.display());
        }
    }
    if !multi {
        println!("ok");
    } else {
        println!("ok ({} packages)", targets.len());
    }
    Ok(())
}

fn cmd_build(
    path: &Path,
    package: Option<&str>,
    out: Option<PathBuf>,
    emit_c: bool,
    release: bool,
    time: bool,
    target: Option<String>,
    sanitize: Option<String>,
    static_link: bool,
    incremental: bool,
    jobs: Option<usize>,
    overflow: OverflowMode,
    bounds_always: bool,
) -> Result<(), ()> {
    let level = opt_level(release);
    let targets = resolve_package_entries(path, true, package)?;
    if targets.len() > 1 && out.is_some() {
        emit_plain_error(
            "workspace build: -o is only valid for a single package / file (use `-p NAME`)",
        );
        return Err(());
    }
    // Safe Mako indexing is checked in release as well as debug. The explicit
    // `unsafe` surface is the opt-out; retaining this CLI flag preserves
    // compatibility with older manifests and scripts.
    let opts = BuildOpts {
        target,
        sanitize,
        static_link,
        overflow,
        bounds_always,
    };
    let incr = make_incr_opts(incremental, release, jobs, &opts);
    for file in &targets {
        let out_bin = out.clone().unwrap_or_else(|| default_out_bin(file));
        let out_bin = cc::with_exe_suffix(out_bin, &opts);
        let (frontend_ms, backend_ms) =
            build_incremental(file, &out_bin, emit_c, level, &opts, &incr)?;
        if time {
            eprintln!(
                "mako frontend: {frontend_ms:.1}ms  backend: {backend_ms:.1}ms  total: {:.1}ms ({})",
                frontend_ms + backend_ms,
                match level {
                    OptLevel::Debug => "debug -O0",
                    OptLevel::Release => "release -O3 -flto",
                }
            );
        }
        println!("built {}", out_bin.display());
    }
    Ok(())
}

fn cmd_run(
    path: &Path,
    package: Option<&str>,
    release: bool,
    time: bool,
    incremental: bool,
    jobs: Option<usize>,
    overflow: OverflowMode,
    bounds_always: bool,
    args: &[String],
) -> Result<(), ()> {
    let file = resolve_run_entry(path, package)?;
    let level = opt_level(release);
    let opts = BuildOpts {
        target: None,
        sanitize: None,
        static_link: false,
        overflow,
        bounds_always,
    };
    let incr = make_incr_opts(incremental, release, jobs, &opts);
    let out_bin = std::env::temp_dir().join(format!(
        "mako_run_{}",
        file.file_stem().and_then(|s| s.to_str()).unwrap_or("prog")
    ));
    let (frontend_ms, backend_ms) = build_incremental(&file, &out_bin, false, level, &opts, &incr)?;
    if time {
        eprintln!(
            "mako frontend: {frontend_ms:.1}ms  backend: {backend_ms:.1}ms  total: {:.1}ms",
            frontend_ms + backend_ms
        );
    }
    let status = Command::new(&out_bin).args(args).status().map_err(|e| {
        emit_plain_error(&format!("could not run binary: {e}"));
    })?;
    if !status.success() {
        emit_plain_error(&format!("program exited with {status}"));
        return Err(());
    }
    Ok(())
}

#[derive(Debug, Clone)]
struct ProfileReport {
    file: PathBuf,
    mode: &'static str,
    frontend_ms: f64,
    backend_ms: f64,
    run_ms: f64,
    exit_code: Option<i32>,
}

impl ProfileReport {
    fn build_ms(&self) -> f64 {
        self.frontend_ms + self.backend_ms
    }

    fn total_ms(&self) -> f64 {
        self.build_ms() + self.run_ms
    }

    fn ok(&self) -> bool {
        self.exit_code == Some(0)
    }

    fn print_text(&self) {
        println!("mako profile: {}", self.file.display());
        println!("  mode:     {}", self.mode);
        println!("  frontend: {:.1}ms", self.frontend_ms);
        println!("  backend:  {:.1}ms", self.backend_ms);
        println!("  build:    {:.1}ms", self.build_ms());
        println!("  run:      {:.1}ms", self.run_ms);
        println!("  total:    {:.1}ms", self.total_ms());
        match self.exit_code {
            Some(code) => println!("  exit:     {code}"),
            None => println!("  exit:     signal/unknown"),
        }
    }

    fn print_json(&self) {
        let exit = self
            .exit_code
            .map(|c| c.to_string())
            .unwrap_or_else(|| "null".into());
        println!(
            r#"{{"schema":"mako.profile.v1","file":"{}","mode":"{}","ok":{},"frontendMs":{:.3},"backendMs":{:.3},"buildMs":{:.3},"runMs":{:.3},"totalMs":{:.3},"exitCode":{}}}"#,
            json_escape(&self.file.display().to_string()),
            self.mode,
            self.ok(),
            self.frontend_ms,
            self.backend_ms,
            self.build_ms(),
            self.run_ms,
            self.total_ms(),
            exit
        );
    }
}

fn cmd_profile(
    path: &Path,
    package: Option<&str>,
    release: bool,
    incremental: bool,
    jobs: Option<usize>,
    json: bool,
    args: &[String],
) -> Result<(), ()> {
    let file = resolve_run_entry(path, package)?;
    let level = opt_level(release);
    let opts = BuildOpts {
        target: None,
        sanitize: None,
        static_link: false,
        overflow: OverflowMode::Wrap,
        bounds_always: false,
    };
    let incr = make_incr_opts(incremental, release, jobs, &opts);
    let out_bin = std::env::temp_dir().join(format!(
        "mako_profile_{}",
        file.file_stem().and_then(|s| s.to_str()).unwrap_or("prog")
    ));
    let (frontend_ms, backend_ms) = build_incremental(&file, &out_bin, false, level, &opts, &incr)?;
    let run_start = Instant::now();
    let status = if json {
        let output = Command::new(&out_bin).args(args).output().map_err(|e| {
            emit_plain_error(&format!("could not run binary: {e}"));
        })?;
        if !output.stdout.is_empty() {
            eprint!("{}", String::from_utf8_lossy(&output.stdout));
        }
        if !output.stderr.is_empty() {
            eprint!("{}", String::from_utf8_lossy(&output.stderr));
        }
        output.status
    } else {
        Command::new(&out_bin).args(args).status().map_err(|e| {
            emit_plain_error(&format!("could not run binary: {e}"));
        })?
    };
    let report = ProfileReport {
        file,
        mode: match level {
            OptLevel::Debug => "debug",
            OptLevel::Release => "release",
        },
        frontend_ms,
        backend_ms,
        run_ms: run_start.elapsed().as_secs_f64() * 1000.0,
        exit_code: status.code(),
    };
    if json {
        report.print_json();
    } else {
        report.print_text();
    }
    if status.success() {
        Ok(())
    } else {
        Err(())
    }
}

fn cmd_test(
    path: &Path,
    package: Option<&str>,
    run_filter: Option<&str>,
    verbose: bool,
    coverage: bool,
    sanitize: Option<&str>,
) -> Result<(), ()> {
    let sanitize = sanitize.map(|s| s.to_string());
    if path.is_dir() {
        if let Some(members) = tooling::workspace_member_dirs(path) {
            let members = filter_workspace_members(members, package)?;
            let mut any = false;
            for m in &members {
                if !m.is_dir() {
                    emit_plain_error(&format!("workspace member missing: {}", m.display()));
                    return Err(());
                }
                let has_tests = tooling::collect_mako_files(m)
                    .into_iter()
                    .any(|f| tooling::is_test_file(&f));
                if !has_tests {
                    if package.is_some() {
                        emit_plain_error(&format!(
                            "workspace member `{}` has no *_test.mko",
                            member_basename(m)
                        ));
                        return Err(());
                    }
                    if verbose {
                        println!("mako test: {} (no *_test.mko — skip)", m.display());
                    }
                    continue;
                }
                any = true;
                println!("=== workspace member {} ===", m.display());
                let san = sanitize.clone();
                tooling::run_tests(
                    m,
                    run_filter,
                    verbose,
                    coverage,
                    &|f, program, names| run_test_package(f, program, names, san.as_deref()),
                    &|f| run_file_quiet(f),
                )?;
            }
            if !any {
                emit_plain_error(&format!(
                    "workspace {}: no *_test.mko in any member",
                    path.display()
                ));
                return Err(());
            }
            return Ok(());
        }
        if package.is_some() {
            emit_plain_error("-p/--package requires a workspace root with `[workspace] members`");
            return Err(());
        }
    } else if package.is_some() {
        emit_plain_error("-p/--package is only valid with a workspace directory");
        return Err(());
    }
    let san = sanitize.clone();
    tooling::run_tests(
        path,
        run_filter,
        verbose,
        coverage,
        &|f, program, names| run_test_package(f, program, names, san.as_deref()),
        &|f| run_file_quiet(f),
    )
}

fn run_file_quiet(file: &Path) -> Result<(), ()> {
    run_file_with_stdio(file, false)
}

fn run_file_silent(file: &Path) -> Result<(), ()> {
    run_file_with_stdio(file, true)
}

fn run_file_with_stdio(file: &Path, silent: bool) -> Result<(), ()> {
    let (c_src, _) = compile_to_c_timed(file)?;
    let out_bin = std::env::temp_dir().join(format!(
        "mako_test_{}",
        file.file_stem().and_then(|s| s.to_str()).unwrap_or("prog")
    ));
    build_c(
        &c_src,
        &out_bin,
        false,
        file,
        OptLevel::Debug,
        &BuildOpts {
            target: None,
            sanitize: None,
            static_link: false,
            overflow: OverflowMode::Wrap,
            bounds_always: false,
        },
    )?;
    let mut cmd = Command::new(&out_bin);
    if silent {
        cmd.stdout(Stdio::null()).stderr(Stdio::null());
    }
    let status = cmd.status().map_err(|e| {
        emit_plain_error(&format!("could not run binary: {e}"));
    })?;
    if !status.success() {
        return Err(());
    }
    Ok(())
}

/// Compile a test package with harness main that runs selected category tests.
fn run_test_package(
    file: &Path,
    program: &ast::Program,
    test_fns: &[String],
    sanitize: Option<&str>,
) -> Result<(), ()> {
    let mut cg = codegen::Codegen::new().with_tests(test_fns.to_vec());
    if let Some(dir) = tooling::find_nearest_manifest_dir(file) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = fs::read_to_string(&manifest) {
            let prof = tooling::codegen_profile_from_toml(
                &text,
                Some(file.display().to_string()),
            );
            cg.bounds_checks_always = prof.bounds_checks_always;
        }
    }
    let c_src = cg.emit(&program);
    let out_bin = std::env::temp_dir().join(format!(
        "mako_gotest_{}",
        file.file_stem().and_then(|s| s.to_str()).unwrap_or("prog")
    ));
    build_c(
        &c_src,
        &out_bin,
        false,
        file,
        OptLevel::Debug,
        &BuildOpts {
            target: None,
            sanitize: sanitize.map(|s| s.to_string()),
            static_link: false,
            overflow: OverflowMode::Wrap,
            bounds_always: false,
        },
    )?;
    // Per-test timeout: default 60s, override with MAKO_TEST_TIMEOUT_SECS.
    let timeout_secs: u64 = std::env::var("MAKO_TEST_TIMEOUT_SECS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(60);
    let mut child = Command::new(&out_bin).spawn().map_err(|e| {
        emit_plain_error(&format!("could not run test binary: {e}"));
    })?;
    let start = std::time::Instant::now();
    let timeout = std::time::Duration::from_secs(timeout_secs);
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                if !status.success() {
                    report_test_exit(file, &status);
                    return Err(());
                }
                return Ok(());
            }
            Ok(None) => {
                if start.elapsed() >= timeout {
                    let _ = child.kill();
                    let _ = child.wait();
                    emit_plain_error(&format!(
                        "{}: test timed out after {}s (hung or too slow — set MAKO_TEST_TIMEOUT_SECS to increase)",
                        file.display(),
                        timeout_secs
                    ));
                    return Err(());
                }
                std::thread::sleep(std::time::Duration::from_millis(50));
            }
            Err(e) => {
                emit_plain_error(&format!("could not wait on test binary: {e}"));
                return Err(());
            }
        }
    }
}

/// Explain *why* a test binary failed. Assertion failures print their own detail
/// to stderr and exit non-zero; a crash is killed by a signal and would otherwise
/// leave "no assertion detail". Surfacing the signal makes CI logs actionable.
fn report_test_exit(file: &Path, status: &std::process::ExitStatus) {
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        if let Some(sig) = status.signal() {
            emit_plain_error(&format!(
                "{}: test process crashed — killed by signal {} ({}); this is a runtime fault (e.g. segfault/abort), not a failed assertion",
                file.display(),
                sig,
                signal_name(sig),
            ));
            return;
        }
    }
    if let Some(code) = status.code() {
        emit_plain_error(&format!(
            "{}: test process exited with code {} (see the assertion output above)",
            file.display(),
            code
        ));
    }
}

fn signal_name(sig: i32) -> &'static str {
    match sig {
        4 => "SIGILL",
        6 => "SIGABRT",
        8 => "SIGFPE",
        11 => "SIGSEGV",
        13 => "SIGPIPE",
        _ => "signal",
    }
}

fn default_pkg_name(path: &Path, name: Option<&str>) -> String {
    name.map(|s| s.to_string())
        .or_else(|| {
            path.file_name()
                .and_then(|s| s.to_str())
                .filter(|s| !s.is_empty() && *s != ".")
                .map(|s| s.to_string())
        })
        .unwrap_or_else(|| "mako-app".into())
}

fn write_new_file(path: &Path, body: &str) -> Result<bool, ()> {
    if path.exists() {
        println!("{} already exists", path.display());
        return Ok(false);
    }
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| {
            emit_plain_error(&format!("could not create {}: {e}", parent.display()));
        })?;
    }
    fs::write(path, body).map_err(|e| {
        emit_plain_error(&format!("could not write {}: {e}", path.display()));
    })?;
    println!("wrote {}", path.display());
    Ok(true)
}

fn cmd_init(path: &Path, name: Option<&str>) -> Result<(), ()> {
    if path.as_os_str() != "." {
        fs::create_dir_all(path).map_err(|e| {
            emit_plain_error(&format!("could not create {}: {e}", path.display()));
        })?;
    }
    let pkg_name = default_pkg_name(path, name);

    let toml = path.join("mako.toml");
    if !toml.exists() {
        let body = format!(
            r#"# Mako package manifest
name = "{pkg_name}"
version = "0.1.0"

[dependencies]
# "helper" = {{ path = "../helper", version = "0.1.0" }}
"#
        );
        write_new_file(&toml, &body)?;
    } else {
        println!("mako.toml already exists at {}", toml.display());
    }

    let main = path.join("main.mko");
    if !main.exists() {
        let body = r#"// Hello Mako — edit and run: mako run main.mko
fn main() {
    print("hello from mako")
}
"#;
        write_new_file(&main, body)?;
    } else {
        println!("main.mko already exists at {}", main.display());
    }

    println!("next: mako run {}", main.display());
    Ok(())
}

/// Local-only workspace scaffold: root `[workspace]` + `lib/` + `app/` (path dep).
fn cmd_init_workspace(path: &Path, name: Option<&str>) -> Result<(), ()> {
    if path.as_os_str() != "." {
        fs::create_dir_all(path).map_err(|e| {
            emit_plain_error(&format!("could not create {}: {e}", path.display()));
        })?;
    }
    let root_name = default_pkg_name(path, name);

    let root_toml = path.join("mako.toml");
    if root_toml.exists() {
        emit_plain_error(&format!(
            "mako.toml already exists at {} — refuse to overwrite with --workspace",
            root_toml.display()
        ));
        return Err(());
    }
    let root_body = format!(
        r#"# Mako workspace (local-only — no registry)
# Generated by `mako init --workspace`
name = "{root_name}"
version = "0.1.0"

[workspace]
members = ["lib", "app"]
"#
    );
    write_new_file(&root_toml, &root_body)?;

    let lib_dir = path.join("lib");
    let lib_toml = lib_dir.join("mako.toml");
    write_new_file(
        &lib_toml,
        r#"name = "lib"
version = "0.1.0"
"#,
    )?;
    write_new_file(
        &lib_dir.join("lib.mko"),
        r#"fn greet(name: string) -> string {
    return "hello " + name
}

fn add(a: int, b: int) -> int {
    return a + b
}
"#,
    )?;

    let app_dir = path.join("app");
    write_new_file(
        &app_dir.join("mako.toml"),
        r#"name = "app"
version = "0.1.0"

[dependencies]
"lib" = { path = "../lib", version = "0.1.0" }
"#,
    )?;
    write_new_file(
        &app_dir.join("main.mko"),
        r#"// Workspace app — depends on member `lib` via mako.toml
fn main() {
    print(lib.greet("mako"))
    print_int(lib.add(20, 22))
}
"#,
    )?;

    println!(
        "next: cd {} && mako check . && mako run -p app",
        path.display()
    );
    Ok(())
}

/// HTTP JSON API service scaffold (`mako init --backend`).
fn cmd_init_backend(path: &Path, name: Option<&str>) -> Result<(), ()> {
    if path.as_os_str() != "." {
        fs::create_dir_all(path).map_err(|e| {
            emit_plain_error(&format!("could not create {}: {e}", path.display()));
        })?;
    }
    let pkg_name = default_pkg_name(path, name);

    let toml = path.join("mako.toml");
    if toml.exists() {
        emit_plain_error(&format!(
            "mako.toml already exists at {} — refuse to overwrite with --backend",
            toml.display()
        ));
        return Err(());
    }
    write_new_file(
        &toml,
        &format!(
            r#"# Mako backend service
# Generated by `mako init --backend`
name = "{pkg_name}"
version = "0.1.0"

[dependencies]
"#
        ),
    )?;

    write_new_file(
        &path.join("main.mko"),
        r#"// Backend API starter — see docs/GUIDE.md "Building APIs"
// Run: mako run main.mko
// Curl: curl -sS http://127.0.0.1:18080/health

fn main() {
    let mut max = 20
    if argc() > 1 {
        match parse_int(arg_get(1)) {
            Ok(v) => {
                if v > 0 {
                    max = v
                }
            }
            Err(_) => {}
        }
    }
    let port = 18080
    let fd = http_bind(port)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("backend listening")
    let mut n = 0
    while n < max {
        let c = http_accept(fd)
        if c < 0 {
            // skip
        } else {
            let method = http_method(c)
            let path = http_path(c)
            if str_eq(path, "/health") {
                let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
            } else {
                if str_eq(path, "/v1/hello") {
                    if str_eq(method, "GET") {
                        let _ = http_respond_json(c, 200, "{\"msg\":\"hello\"}\n")
                    } else {
                        let _ = http_respond_json(c, 405, "{\"error\":\"method\"}\n")
                    }
                } else {
                    let _ = http_respond_json(c, 404, "{\"error\":\"not_found\"}\n")
                }
            }
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
    print("backend done")
}
"#,
    )?;

    write_new_file(
        &path.join("README.md"),
        &format!(
            r#"# {pkg_name}

Mako HTTP JSON API service (from `mako init --backend`).

## Run

```bash
mako run main.mko
# or release:
mako build --release main.mko -o {pkg_name}
./{pkg_name}
```

## Try

```bash
curl -sS http://127.0.0.1:18080/health
curl -sS http://127.0.0.1:18080/v1/hello
```

See docs/GUIDE.md (Building APIs), docs/PERFORMANCE.md, docs/SECURITY.md.
"#
        ),
    )?;

    println!("next: mako run {}", path.join("main.mko").display());
    Ok(())
}

#[derive(Debug, Clone)]
#[allow(dead_code)]
struct PkgDep {
    name: String,
    kind: String, // path | git | other
    source: String,
    version: String,
}

fn parse_mako_toml_meta(text: &str) -> (Option<String>, Option<String>) {
    let mut name = None;
    let mut version = None;
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('#') {
            continue;
        }
        if t.starts_with('[') {
            break;
        }
        if let Some(rest) = t.strip_prefix("name") {
            let rest = rest.trim().trim_start_matches('=').trim();
            let v = rest.trim_matches('"').trim_matches('\'').trim();
            if !v.is_empty() {
                name = Some(v.to_string());
            }
        } else if let Some(rest) = t.strip_prefix("version") {
            let rest = rest.trim().trim_start_matches('=').trim();
            let v = rest.trim_matches('"').trim_matches('\'').trim();
            if !v.is_empty() {
                version = Some(v.to_string());
            }
        }
    }
    (name, version)
}

fn pkg_list(root: &Path) -> Result<(), ()> {
    let manifest = root.join("mako.toml");
    if !manifest.exists() {
        emit_plain_error("no mako.toml — run `mako init` or `mako pkg init` first");
        return Err(());
    }
    let text = fs::read_to_string(&manifest).map_err(|e| {
        emit_plain_error(&format!("read mako.toml: {e}"));
    })?;
    let (name, version) = parse_mako_toml_meta(&text);
    let deps = tooling::parse_manifest_deps(&text);
    println!(
        "package: {} {}",
        name.as_deref().unwrap_or("(unnamed)"),
        version.as_deref().unwrap_or("")
    );
    println!("manifest: {}", manifest.display());
    if deps.is_empty() {
        println!("dependencies: (none)");
        return Ok(());
    }
    println!("dependencies:");
    let mut bad = 0usize;
    for d in &deps {
        let ver = d.version.as_deref().unwrap_or("?");
        if d.is_git() {
            let cache = tooling::git_dep_cache_abs(root, &d.name);
            let pin = d.pin_label();
            let status = if cache.exists() {
                "[fetched]"
            } else {
                bad += 1;
                "[MISSING — run `mako pkg fetch`]"
            };
            println!(
                "  {name}  git={url}  {pin}  version={ver}  {status}",
                name = d.name,
                url = d.git.as_deref().unwrap_or(""),
            );
        } else if let Some(p) = &d.path {
            let full = root.join(p);
            let ok = full.exists();
            let path_ver = resolve_path_dep_version(&full);
            let mut semver_note = String::new();
            if let (Some(req), Some(pv)) = (d.version.as_deref(), path_ver.as_deref()) {
                if tooling::version_satisfies(pv, req) {
                    semver_note = format!(" path-ver={pv} [semver ok]");
                } else {
                    bad += 1;
                    semver_note = format!(" path-ver={pv} [semver FAIL vs {req}]");
                }
            } else if let Some(pv) = &path_ver {
                semver_note = format!(" path-ver={pv}");
            }
            if ok {
                println!(
                    "  {name}  path={path}  version={ver}{semver_note}  [ok]",
                    name = d.name,
                    path = p,
                );
            } else {
                bad += 1;
                println!(
                    "  {name}  path={path}  version={ver}  [MISSING: {}]",
                    full.display(),
                    name = d.name,
                    path = p,
                );
            }
        } else if d.version.is_some() {
            match tooling::registry_resolve(root, &d.name, ver) {
                Ok(dir) => {
                    println!(
                        "  {name}  registry={}  version={ver}  [ok]",
                        dir.display(),
                        name = d.name,
                    );
                }
                Err(e) => {
                    bad += 1;
                    println!("  {}  version={ver}  [MISSING registry: {e}]", d.name);
                }
            }
        } else {
            bad += 1;
            println!("  {}  version={ver}  [no path/git/registry]", d.name);
        }
    }
    if bad > 0 {
        emit_plain_error(&format!(
            "{bad} dependency(ies) missing or SemVer mismatch (path / git / local registry)"
        ));
        return Err(());
    }
    Ok(())
}

/// Clone git deps into `.mako/deps/<name>` (rev/tag/branch when set).
fn pkg_fetch(root: &Path) -> Result<(), ()> {
    let manifest = root.join("mako.toml");
    if !manifest.exists() {
        emit_plain_error("no mako.toml — run `mako init` first");
        return Err(());
    }
    let text = fs::read_to_string(&manifest).map_err(|e| {
        emit_plain_error(&format!("read mako.toml: {e}"));
    })?;
    let deps: Vec<_> = tooling::parse_manifest_deps(&text)
        .into_iter()
        .filter(|d| d.is_git())
        .collect();
    if deps.is_empty() {
        println!("mako pkg fetch: no git dependencies in mako.toml");
        return Ok(());
    }
    let mut ok = 0usize;
    let mut failed = 0usize;
    for d in &deps {
        let url = d.git.as_deref().unwrap();
        match fetch_git_dep(
            root,
            &d.name,
            url,
            d.rev.as_deref(),
            d.tag.as_deref(),
            d.branch.as_deref(),
        ) {
            Ok(rel) => {
                println!("fetched {} -> {} ({})", d.name, rel, d.pin_label());
                ok += 1;
            }
            Err(e) => {
                eprintln!("mako pkg fetch: `{}` failed: {e}", d.name);
                failed += 1;
            }
        }
    }
    println!("mako pkg fetch: {ok} ok, {failed} failed");
    if failed > 0 {
        return Err(());
    }
    Ok(())
}

fn run_pkg(cmd: PkgCmd) -> Result<(), ()> {
    match cmd {
        PkgCmd::Init { path } => {
            // Prefer full scaffold (toml + main.mko).
            cmd_init(&path, None)
        }
        PkgCmd::List { path } => pkg_list(&path),
        PkgCmd::Fetch { path } => pkg_fetch(&path),
        PkgCmd::Lock { path, offline } | PkgCmd::Install { path, offline } => {
            pkg::pkg_install(&path, offline).map_err(|e| emit_plain_error(&e))
        }
        PkgCmd::Update { path, offline } => {
            pkg::pkg_update(&path, offline).map_err(|e| emit_plain_error(&e))
        }
        PkgCmd::Publish { path } => pkg::pkg_publish(&path).map_err(|e| emit_plain_error(&e)),
        PkgCmd::Seal { path } => pkg::pkg_seal(&path).map_err(|e| emit_plain_error(&e)),
        PkgCmd::Add {
            name,
            source,
            version,
            project,
        } => {
            let (dep_name, dep_source) = resolve_pkg_add_args(&name, source.as_deref())?;
            pkg_add(&project, &dep_name, &dep_source, &version)
        }
        PkgCmd::Remove { name, project } => pkg_remove(&project, &name),
        PkgCmd::Audit { path } => tooling::pkg_audit(&path),
    }
}

fn resolve_pkg_add_args(name: &str, source: Option<&str>) -> Result<(String, String), ()> {
    let strip_path = |s: &str| -> String {
        s.strip_prefix("path=")
            .or_else(|| s.strip_prefix("path:"))
            .unwrap_or(s)
            .to_string()
    };
    match source {
        None if name.starts_with("path=") || name.starts_with("path:") => {
            let p = strip_path(name);
            if p.is_empty() {
                emit_plain_error("empty path= — e.g. `mako pkg add path=../helper`");
                return Err(());
            }
            let base = Path::new(&p)
                .file_name()
                .and_then(|s| s.to_str())
                .unwrap_or("dep")
                .trim_end_matches(".mko");
            if base.is_empty() || base == "." || base == ".." {
                emit_plain_error(
                    "could not infer dependency name from path — pass `NAME path=...`",
                );
                return Err(());
            }
            Ok((base.to_string(), p))
        }
        None => {
            emit_plain_error(
                "usage: mako pkg add NAME PATH · mako pkg add NAME path=PATH · mako pkg add path=PATH",
            );
            Err(())
        }
        Some(src) => {
            let p = strip_path(src);
            Ok((name.to_string(), p))
        }
    }
}

/// True when a line is the `"name" = …` dependency entry (ignores comments / other keys).
fn is_dep_entry_line(line: &str, name: &str) -> bool {
    let t = line.trim();
    if t.starts_with('#') {
        return false;
    }
    let key_q = format!("\"{name}\"");
    let key_bare = format!("{name} =");
    t.starts_with(&key_q) || t.starts_with(&key_bare) || t.starts_with(&format!("{name}="))
}

fn pkg_remove(root: &Path, name: &str) -> Result<(), ()> {
    let manifest = root.join("mako.toml");
    if !manifest.exists() {
        emit_plain_error("no mako.toml — run `mako pkg init` first");
        return Err(());
    }
    let text = fs::read_to_string(&manifest).map_err(|e| {
        emit_plain_error(&format!("read mako.toml: {e}"));
    })?;
    let mut out = String::new();
    let mut in_deps = false;
    let mut removed = 0usize;
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed == "[dependencies]" {
            in_deps = true;
            out.push_str(line);
            out.push('\n');
            continue;
        }
        if trimmed.starts_with('[') {
            in_deps = false;
        }
        if in_deps && is_dep_entry_line(line, name) {
            removed += 1;
            continue;
        }
        out.push_str(line);
        out.push('\n');
    }
    if removed == 0 {
        emit_plain_error(&format!("dependency `{name}` not found in mako.toml"));
        return Err(());
    }
    fs::write(&manifest, out).map_err(|e| {
        emit_plain_error(&format!("write mako.toml: {e}"));
    })?;
    println!("removed {name} from {}", manifest.display());
    Ok(())
}

fn pkg_add(root: &Path, name: &str, source: &str, version: &str) -> Result<(), ()> {
    let manifest = root.join("mako.toml");
    if !manifest.exists() {
        emit_plain_error("no mako.toml — run `mako pkg init` first");
        return Err(());
    }
    let mut text = fs::read_to_string(&manifest).map_err(|e| {
        emit_plain_error(&format!("read mako.toml: {e}"));
    })?;
    if !text.contains("[dependencies]") {
        text.push_str("\n[dependencies]\n");
    }
    let is_url = source.starts_with("http://")
        || source.starts_with("https://")
        || source.starts_with("file://")
        || source.starts_with("git@")
        || source.ends_with(".git");

    if !is_url {
        let full = root.join(source);
        if !full.exists() {
            eprintln!(
                "mako pkg add: warning: path `{}` does not exist yet (will fail check/build until created)",
                full.display()
            );
        }
    }

    let mut cache_rel = String::new();
    if is_url {
        match fetch_remote_dep(root, name, source) {
            Ok(rel) => {
                cache_rel = rel;
                println!("fetched {name} -> {cache_rel}");
            }
            Err(e) => {
                eprintln!("mako pkg add: fetch failed ({e}) — recording URL only");
                eprintln!("  limit: needs `git` + network; cache under .mako/cache/deps/");
            }
        }
    }

    let line = if is_url {
        if cache_rel.is_empty() {
            format!(
                "\"{name}\" = {{ git = \"{source}\", version = \"{version}\" }}  # fetch pending"
            )
        } else {
            format!(
                "\"{name}\" = {{ git = \"{source}\", version = \"{version}\", path = \"{cache_rel}\" }}"
            )
        }
    } else {
        format!("\"{name}\" = {{ path = \"{source}\", version = \"{version}\" }}")
    };

    // Update-in-place if the dependency key already exists under [dependencies].
    let mut out = String::new();
    let mut in_deps = false;
    let mut replaced = false;
    for existing in text.lines() {
        let trimmed = existing.trim();
        if trimmed == "[dependencies]" {
            in_deps = true;
            out.push_str(existing);
            out.push('\n');
            continue;
        }
        if trimmed.starts_with('[') {
            in_deps = false;
        }
        if in_deps && is_dep_entry_line(existing, name) {
            if !replaced {
                out.push_str(&line);
                out.push('\n');
                replaced = true;
            }
            // Drop duplicate keys for the same name.
            continue;
        }
        out.push_str(existing);
        out.push('\n');
    }
    if !replaced {
        if !out.ends_with('\n') && !out.is_empty() {
            out.push('\n');
        }
        out.push_str(&line);
        out.push('\n');
    }

    fs::write(&manifest, &out).map_err(|e| {
        emit_plain_error(&format!("write mako.toml: {e}"));
    })?;
    if replaced {
        println!("updated {name} -> {source} (version {version})");
    } else {
        println!("added {name} -> {source} (version {version})");
    }
    println!("hint: run `mako pkg lock` to pin content_hash");
    Ok(())
}

/// Shallow-clone / checkout a git dep into `.mako/deps/<name>`.
fn fetch_git_dep(
    root: &Path,
    name: &str,
    source: &str,
    rev: Option<&str>,
    tag: Option<&str>,
    branch: Option<&str>,
) -> Result<String, String> {
    if !tooling::valid_dep_cache_name(name) {
        return Err(format!(
            "git dep `{name}` has an invalid cache name (allowed: letters, digits, `_`, `-`, `.`)"
        ));
    }
    let cache = tooling::git_dep_cache_abs(root, name);
    let rel = tooling::git_dep_cache_rel(name);
    if cache.join(".git").exists()
        || cache.join("mako.toml").exists()
        || cache.join("lib.mko").exists()
    {
        // Already present — optionally checkout pin if rev/tag given.
        if let Some(r) = rev.or(tag) {
            let _ = Command::new("git")
                .args(["-C"])
                .arg(&cache)
                .args(["checkout", "--force", r])
                .status();
        }
        return Ok(rel);
    }
    if let Some(parent) = cache.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    if cache.exists() {
        let _ = fs::remove_dir_all(&cache);
    }
    let mut args = vec![
        "-c".into(),
        "core.hooksPath=/dev/null".into(),
        "clone".into(),
    ];
    if rev.is_none() {
        args.push("--depth".into());
        args.push("1".into());
    }
    if let Some(b) = branch {
        args.push("--branch".into());
        args.push(b.into());
    } else if let Some(t) = tag {
        args.push("--branch".into());
        args.push(t.into());
    }
    args.push(source.into());
    args.push(cache.display().to_string());
    let status = Command::new("git")
        .args(&args)
        .status()
        .map_err(|e| format!("git not runnable: {e}"))?;
    if !status.success() {
        let _ = fs::remove_dir_all(&cache);
        return Err(format!("git clone exited {}", status.code().unwrap_or(-1)));
    }
    if let Some(r) = rev {
        let st = Command::new("git")
            .args(["-C"])
            .arg(&cache)
            .args(["checkout", "--force", r])
            .status()
            .map_err(|e| format!("git checkout: {e}"))?;
        if !st.success() {
            return Err(format!("git checkout {r} failed"));
        }
    }
    Ok(rel)
}

/// Shallow-clone `source` into `.mako/cache/deps/<name>` (legacy `pkg add` URL path).
fn fetch_remote_dep(root: &Path, name: &str, source: &str) -> Result<String, String> {
    if !tooling::valid_dep_cache_name(name) {
        return Err(format!(
            "remote dep `{name}` has an invalid cache name (allowed: letters, digits, `_`, `-`, `.`)"
        ));
    }
    let cache = root.join(".mako").join("cache").join("deps").join(name);
    if cache.join(".git").exists() || cache.join("mako.toml").exists() {
        return Ok(format!(".mako/cache/deps/{name}"));
    }
    if let Some(parent) = cache.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    if cache.exists() {
        let _ = fs::remove_dir_all(&cache);
    }
    let status = Command::new("git")
        .args([
            "-c",
            "core.hooksPath=/dev/null",
            "clone",
            "--depth",
            "1",
            source,
        ])
        .arg(&cache)
        .status()
        .map_err(|e| format!("git not runnable: {e}"))?;
    if !status.success() {
        let _ = fs::remove_dir_all(&cache);
        return Err(format!("git clone exited {}", status.code().unwrap_or(-1)));
    }
    Ok(format!(".mako/cache/deps/{name}"))
}

/// Read `version = "..."` from a path dep's `mako.toml` (file or directory).
fn resolve_path_dep_version(full: &Path) -> Option<String> {
    let manifest = if full.is_file() {
        full.parent()?.join("mako.toml")
    } else {
        full.join("mako.toml")
    };
    let text = fs::read_to_string(manifest).ok()?;
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('#') {
            continue;
        }
        if t.starts_with("version") {
            return t.split('"').nth(1).map(|s| s.to_string());
        }
    }
    None
}

fn emit_plain_error(message: &str) {
    Diagnostic::error("", "", Span::unknown(), message).emit();
}

fn compile_to_ast(file: &Path) -> Result<ast::Program, ()> {
    let incr = incremental::IncrOptions {
        incremental: false,
        ..Default::default()
    };
    compile_to_ast_with(file, &incr)
}

fn compile_to_ast_with(file: &Path, incr: &incremental::IncrOptions) -> Result<ast::Program, ()> {
    let path = file.display().to_string();
    let src = fs::read_to_string(file).map_err(|e| {
        Diagnostic::error(&path, "", Span::unknown(), format!("cannot read file: {e}")).emit();
    })?;

    let tokens = Lexer::new(&src).tokenize().map_err(|e| {
        let (span, message, hint) = match &e {
            LexError::UnexpectedChar(c, line, col) => (
                Span::new(*line, *col),
                format!("unexpected character `{c}`"),
                Some(String::from(
                    "remove it, or put it inside a string / comment",
                )),
            ),
            LexError::UnterminatedString(line, col) => (
                Span::new(*line, *col),
                String::from("string literal is missing a closing quote"),
                Some(String::from("add a closing \" at the end of the string")),
            ),
            LexError::NumberOutOfRange { literal, line, col } => (
                Span::new(*line, *col),
                format!("numeric literal `{literal}` is out of range"),
                Some(String::from(
                    "use a smaller literal or parse the value from text at runtime",
                )),
            ),
            LexError::ColoredAsync(line, col) => (
                Span::new(*line, *col),
                String::from("colored `async`/`await` is not part of Mako"),
                Some(String::from(
                    "use crew/kick/channels instead — see docs/ASYNC.md",
                )),
            ),
        };
        let mut d = Diagnostic::error(&path, &src, span, message);
        if let Some(h) = hint {
            d = d.with_hint(h);
        }
        d.emit();
    })?;

    let (program, parse_errs) = Parser::new(tokens).parse_with_errors();
    if recovery::emit_parse_errors(&path, &src, &parse_errs) {
        return Err(());
    }

    let program = desugar::desugar(program);
    let program = tooling::resolve_imports(file, program).map_err(|e| {
        Diagnostic::error(&path, &src, Span::unknown(), e).emit();
    })?;
    // Package-per-directory: compile entry + sibling units as one package.
    let program = tooling::merge_package_dir_siblings(file, program).map_err(|e| {
        Diagnostic::error(&path, &src, Span::unknown(), e).emit();
    })?;
    let mut program = tooling::merge_path_dependencies(file, program).map_err(|e| {
        Diagnostic::error(&path, &src, Span::unknown(), e).emit();
    })?;

    incremental::typecheck_incremental(file, &mut program, &src, incr)?;
    Ok(program)
}

fn compile_to_c_timed(file: &Path) -> Result<(String, f64), ()> {
    let t0 = Instant::now();
    let program = compile_to_ast(file)?;
    // Lint warnings: unused imports, unreachable code, unused variables.
    dce::warn_unused_imports(file, &program, &["main".into(), "mako_main".into()]);
    dce::warn_unreachable_code(&program);
    dce::warn_unused_variables(&program);
    if std::env::var_os("MAKO_LINT_SHADOW").is_some() {
        dce::warn_shadowed_variables(&program);
    }
    // Dead code elimination: remove unreachable functions before codegen.
    let program = if std::env::var_os("MAKO_NO_DCE").is_none() {
        dce::eliminate(&program, &["main".into(), "mako_main".into()])
    } else {
        program
    };
    let mut cg = Codegen::new();
    cg.source_file = Some(file.display().to_string());
    if let Some(dir) = tooling::find_nearest_manifest_dir(file) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = fs::read_to_string(&manifest) {
            let prof = tooling::codegen_profile_from_toml(
                &text,
                Some(file.display().to_string()),
            );
            cg.bounds_checks_always = prof.bounds_checks_always;
            if prof.source_file.is_some() {
                cg.source_file = prof.source_file;
            }
        }
    }
    let c = cg.emit(&program);
    Ok((c, t0.elapsed().as_secs_f64() * 1000.0))
}

/// Incremental path: typecheck (cached) → per-unit C→`.o` (cached, parallel) → link.
/// Falls back to monolithic `build_c` for wasm / emit_c / sanitize / cross targets.
fn build_incremental(
    file: &Path,
    out_bin: &Path,
    emit_c: bool,
    level: OptLevel,
    opts: &BuildOpts,
    incr: &incremental::IncrOptions,
) -> Result<(f64, f64), ()> {
    let is_wasm = opts
        .target
        .as_ref()
        .map(|t| t.contains("wasm"))
        .unwrap_or(false);
    let use_sep = !emit_c
        && !is_wasm
        && opts.target.is_none()
        && opts.sanitize.is_none()
        && !opts.static_link;

    let t0 = Instant::now();
    let program = compile_to_ast_with(file, incr)?;
    // Lint warnings.
    dce::warn_unused_imports(file, &program, &["main".into(), "mako_main".into()]);
    dce::warn_unreachable_code(&program);
    dce::warn_unused_variables(&program);
    if std::env::var_os("MAKO_LINT_SHADOW").is_some() {
        dce::warn_shadowed_variables(&program);
    }
    // Dead code elimination: remove unreachable functions before codegen.
    let program = if std::env::var_os("MAKO_NO_DCE").is_none() {
        dce::eliminate(&program, &["main".into(), "mako_main".into()])
    } else {
        program
    };
    let frontend_ms = t0.elapsed().as_secs_f64() * 1000.0;

    if !use_sep {
        let mut cg = Codegen::new();
        cg.overflow_mode = opts.overflow;
        cg.bounds_checks_always = opts.bounds_always;
        cg.source_file = Some(file.display().to_string());
        let c = cg.emit(&program);
        let backend_ms = build_c(&c, out_bin, emit_c, file, level, opts)?;
        return Ok((frontend_ms, backend_ms));
    }

    let runtime_dir = runtime_include_dir().map_err(|e| {
        emit_plain_error(&e);
    })?;

    // Include host feature flags in object keys (OpenSSL/etc. change codegen).
    let mut incr = incr.clone();
    let cflags = compile_cflags(opts);
    for f in &cflags {
        incr.flags_fp.push_str(f);
        incr.flags_fp.push(';');
    }

    let units = incremental::plan_object_units(&program, &incr, &runtime_dir);
    if emit_c {
        if let Some(u) = units.first() {
            let _ = fs::write(file.with_extension("c"), &u.c_src);
        }
    }

    let t1 = Instant::now();
    let (objs, hits, misses) =
        incremental::compile_units_parallel(file, &units, &incr, &runtime_dir, &cflags)?;
    if incr.verbose_cache || std::env::var_os("MAKO_CACHE_LOG").is_some() {
        eprintln!(
            "mako cache: objects hits={hits} misses={misses} jobs={}",
            incr.jobs
        );
    } else if hits > 0 {
        eprintln!("mako cache: {hits} object hit(s), {misses} miss(es)");
    }

    let link_args = link_args_native(opts, &runtime_dir);
    let _link_ms = incremental::link_objects(&objs, out_bin, &incr, &link_args)?;
    let backend_ms = t1.elapsed().as_secs_f64() * 1000.0;
    Ok((frontend_ms, backend_ms))
}

fn compile_cflags(opts: &BuildOpts) -> Vec<String> {
    let mut v = Vec::new();
    // Cross builds skip host optional-lib defines (headers/libs won't match the target).
    if cc::is_cross(opts.target.as_deref()) {
        let _ = opts;
        return v;
    }
    // Defines that affect codegen of headers included at -c time.
    if find_openssl().is_some() {
        v.push("-DMAKO_HAS_OPENSSL".into());
        v.push("-DMAKO_USE_OPENSSL".into());
    }
    if find_nghttp2().is_some() {
        v.push("-DMAKO_HAS_NGHTTP2".into());
    }
    if find_quiche().is_some() {
        v.push("-DMAKO_HAS_QUICHE".into());
    }
    if find_sqlite().is_some() {
        v.push("-DMAKO_HAS_SQLITE".into());
    }
    if find_libpq().is_some() {
        v.push("-DMAKO_HAS_LIBPQ".into());
    }
    if find_zlib().is_some() {
        v.push("-DMAKO_HAS_ZLIB".into());
    }
    if find_opencl().is_some() {
        v.push("-DMAKO_HAS_OPENCL".into());
    }
    if cc::classify_target(opts.target.as_deref()) == cc::OsKind::Linux
        && Path::new("/usr/include/crypt.h").exists()
    {
        v.push("-DMAKO_HAS_CRYPT".into());
    }
    if let Some((inc, _)) = find_openssl() {
        v.push(format!("-I{}", inc.display()));
    }
    if let Some((inc, _)) = find_nghttp2() {
        v.push(format!("-I{}", inc.display()));
    }
    if let Some(q) = find_quiche() {
        v.push(format!("-I{}", q.include.display()));
    }
    if let Some((inc, _)) = find_sqlite() {
        v.push(format!("-I{}", inc.display()));
    }
    if let Some((inc, _)) = find_libpq() {
        v.push(format!("-I{}", inc.display()));
    }
    if let Some((inc, _)) = find_zlib() {
        v.push(format!("-I{}", inc.display()));
    }
    if let Some((inc, _)) = find_opencl() {
        if !inc.as_os_str().is_empty() && inc != Path::new(".") {
            v.push(format!("-I{}", inc.display()));
        }
    }
    let _ = opts;
    v
}

fn link_args_native(opts: &BuildOpts, _runtime_dir: &Path) -> Vec<String> {
    let os = cc::classify_target(opts.target.as_deref());
    let mut args = cc::base_link_args(opts);
    // Optional deps: only auto-link on native (same-OS) builds — cross sysroots rarely have them.
    let native_like = opts.target.is_none() || !cc::is_cross(opts.target.as_deref());
    if !native_like {
        return args;
    }
    if let Some((inc, lib)) = find_openssl() {
        args.push(format!("-I{}", inc.display()));
        args.push(format!("-L{}", lib.display()));
        args.push("-DMAKO_HAS_OPENSSL".into());
        args.push("-DMAKO_USE_OPENSSL".into());
        args.push("-lssl".into());
        args.push("-lcrypto".into());
    }
    if let Some((inc, lib)) = find_nghttp2() {
        args.push(format!("-I{}", inc.display()));
        args.push(format!("-L{}", lib.display()));
        args.push("-DMAKO_HAS_NGHTTP2".into());
        args.push("-lnghttp2".into());
    }
    if let Some(q) = find_quiche() {
        args.push(format!("-I{}", q.include.display()));
        args.push(format!("-L{}", q.lib_dir.display()));
        if os != cc::OsKind::Windows {
            args.push(format!("-Wl,-rpath,{}", q.lib_dir.display()));
        }
        args.push("-DMAKO_HAS_QUICHE".into());
        if q.prefer_static {
            args.push(q.lib_dir.join("libquiche.a").display().to_string());
        } else {
            args.push("-lquiche".into());
        }
        match os {
            cc::OsKind::Macos => {
                args.push("-lc++".into());
                args.push("-framework".into());
                args.push("Security".into());
                args.push("-framework".into());
                args.push("CoreFoundation".into());
            }
            cc::OsKind::Linux | cc::OsKind::Other => {
                args.push("-lstdc++".into());
                args.push("-ldl".into());
                args.push("-lm".into());
            }
            cc::OsKind::Windows | cc::OsKind::Wasm => {}
        }
    }
    if let Some((inc, lib)) = find_sqlite() {
        args.push(format!("-I{}", inc.display()));
        if let Some(l) = lib {
            args.push(format!("-L{}", l.display()));
        }
        args.push("-DMAKO_HAS_SQLITE".into());
        args.push("-lsqlite3".into());
    }
    if let Some((inc, lib)) = find_libpq() {
        args.push(format!("-I{}", inc.display()));
        args.push(format!("-L{}", lib.display()));
        args.push("-DMAKO_HAS_LIBPQ".into());
        args.push("-lpq".into());
    }
    if let Some((inc, lib)) = find_zlib() {
        args.push(format!("-I{}", inc.display()));
        if let Some(l) = lib {
            args.push(format!("-L{}", l.display()));
        }
        args.push("-DMAKO_HAS_ZLIB".into());
        args.push("-lz".into());
    }
    // bcrypt via libxcrypt: Linux ships <crypt.h> + libcrypt with crypt_gensalt_rn.
    if os == cc::OsKind::Linux && Path::new("/usr/include/crypt.h").exists() {
        args.push("-DMAKO_HAS_CRYPT".into());
        args.push("-lcrypt".into());
    }
    // OpenCL: multi-vendor GPU compute (NVIDIA / AMD / Intel ICD + macOS framework).
    if let Some((inc, lib)) = find_opencl() {
        args.push("-DMAKO_HAS_OPENCL".into());
        if !inc.as_os_str().is_empty() && inc != Path::new(".") {
            args.push(format!("-I{}", inc.display()));
        }
        match os {
            cc::OsKind::Macos => {
                args.push("-framework".into());
                args.push("OpenCL".into());
            }
            cc::OsKind::Linux | cc::OsKind::Other => {
                if let Some(l) = lib {
                    args.push(format!("-L{}", l.display()));
                }
                args.push("-lOpenCL".into());
            }
            cc::OsKind::Windows => {
                if let Some(l) = lib {
                    args.push(format!("-L{}", l.display()));
                }
                args.push("-lOpenCL".into());
            }
            cc::OsKind::Wasm => {}
        }
    }
    args
}

fn build_c(
    c_src: &str,
    out_bin: &Path,
    keep_c: bool,
    src_file: &Path,
    level: OptLevel,
    opts: &BuildOpts,
) -> Result<f64, ()> {
    let runtime_dir = runtime_include_dir().map_err(|e| {
        emit_plain_error(&e);
    })?;
    let c_path = if keep_c {
        src_file.with_extension("c")
    } else {
        std::env::temp_dir().join(format!(
            "mako_{}.c",
            src_file
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("out")
        ))
    };
    fs::write(&c_path, c_src).map_err(|e| {
        emit_plain_error(&format!("could not write generated C: {e}"));
    })?;

    let t0 = Instant::now();
    let is_wasm = opts
        .target
        .as_ref()
        .map(|t| t.contains("wasm"))
        .unwrap_or(false);

    let cc_bin = cc::resolve_cc(opts);
    let zig = cc::using_zig(&cc_bin);
    cc::note_cross(opts, &cc_bin);

    let mut cmd = Command::new(&cc_bin);
    cc::apply_cc_prefix(&mut cmd, &cc_bin);
    match level {
        OptLevel::Debug => {
            cmd.arg("-O0").arg("-g");
        }
        OptLevel::Release => {
            // Match incremental path: -O3 + LTO for Go-competitive native speed.
            // Skip LTO for zig cross — can be slow / flaky across targets.
            // MAKO_NO_LTO=1 disables -flto (faster links / some toolchains).
            if zig && cc::is_cross(opts.target.as_deref()) {
                cmd.arg("-O2").arg("-DNDEBUG");
            } else if std::env::var_os("MAKO_NO_LTO").is_some() {
                cmd.arg("-O3").arg("-DNDEBUG");
            } else {
                cmd.arg("-O3").arg("-flto").arg("-DNDEBUG");
            }
        }
    }

    // Optional extra C flags (space-separated), e.g. MAKO_CFLAGS="-march=native".
    if let Ok(extra) = std::env::var("MAKO_CFLAGS") {
        for a in extra.split_whitespace() {
            if !a.is_empty() {
                cmd.arg(a);
            }
        }
    }
    // Profile-guided optimization (clang/gcc): generate then use.
    //   MAKO_PGO_GEN=1 mako build --release …
    //   ./bin …  # train
    //   MAKO_PGO_USE=. mako build --release …
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

    if is_wasm {
        let raw = opts.target.as_deref().unwrap_or("wasm32-wasip1");
        cc::push_target_args(&mut cmd, raw, zig);
        if let Some(sdk) = cc::wasi_sdk() {
            let sysroot = sdk.join("share/wasi-sysroot");
            if sysroot.exists() {
                cmd.arg(format!("--sysroot={}", sysroot.display()));
            }
            eprintln!(
                "mako: wasm target — using wasi-sdk at {} (set WASI_SDK_PATH to override)",
                sdk.display()
            );
        } else if !zig {
            eprintln!(
                "mako: wasm target — wasi-sdk not found.
  Install: https://github.com/WebAssembly/wasi-sdk/releases
  Then: export WASI_SDK_PATH=/path/to/wasi-sdk  (or /opt/wasi-sdk)
  Without wasi-sdk clang + sysroot, wasm builds cannot link wasi-libc."
            );
        }
        cmd.arg("-DMAKO_WASI");
        cmd.arg("-D_WASI_EMULATED_SIGNAL");
        cmd.arg("-D_WASI_EMULATED_PROCESS_CLOCKS");
        cmd.arg("-D_POSIX_C_SOURCE=200809L");
    } else if let Some(t) = &opts.target {
        cc::push_target_args(&mut cmd, t, zig);
        cc::push_macos_sysroot(&mut cmd, t, zig);
    }

    // gnu11 for wasm so wasi-libc POSIX decls stay visible with feature macros.
    if is_wasm {
        cmd.arg("-std=gnu11");
    } else {
        cmd.arg("-std=c11");
    }
    cmd.arg(format!("-I{}", runtime_dir.display())).arg(&c_path);
    // Optional C shim for extern demos / plugins (native only — may pull host APIs).
    if !is_wasm && !cc::is_cross(opts.target.as_deref()) {
        let shim = runtime_dir.join("mako_extern_demo.c");
        if shim.exists() {
            cmd.arg(&shim);
        }
    }
    cmd.arg("-o").arg(out_bin);

    if is_wasm {
        cmd.arg("-lwasi-emulated-signal");
        cmd.arg("-lwasi-emulated-process-clocks");
    } else {
        for a in link_args_native(opts, &runtime_dir) {
            cmd.arg(a);
        }
    }

    let output = cmd.output().map_err(|e| {
        emit_plain_error(&format!(
            "native backend failed to start ({}): {e}",
            cc_bin.display()
        ));
    })?;
    let ms = t0.elapsed().as_secs_f64() * 1000.0;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        Diagnostic::error(
            &src_file.display().to_string(),
            "",
            Span::unknown(),
            "native backend failed while compiling generated C",
        )
        .with_hint(format!(
            "this is usually a compiler bug — {} said:\n{}",
            cc_bin.display(),
            stderr.trim()
        ))
        .emit();
        if !keep_c {
            let _ = fs::remove_file(&c_path);
        }
        return Err(());
    }
    if !keep_c {
        let _ = fs::remove_file(&c_path);
    }
    if matches!(level, OptLevel::Release) && std::env::var_os("MAKO_STRIP").is_some() {
        let _ = Command::new("strip").arg(out_bin).status();
    }
    Ok(ms)
}

fn find_zlib() -> Option<(PathBuf, Option<PathBuf>)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/zlib"),
        PathBuf::from("/usr/local/opt/zlib"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("zlib.h").exists() {
            return Some((inc, Some(lib)));
        }
    }
    if PathBuf::from("/usr/include/zlib.h").exists() {
        return Some((PathBuf::from("/usr/include"), None));
    }
    #[cfg(target_os = "macos")]
    {
        // Xcode SDK ships zlib
        return Some((PathBuf::from("."), None));
    }
    #[cfg(not(target_os = "macos"))]
    {
        None
    }
}

fn find_sqlite() -> Option<(PathBuf, Option<PathBuf>)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/sqlite"),
        PathBuf::from("/usr/local/opt/sqlite"),
        PathBuf::from("/opt/homebrew/opt/sqlite3"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("sqlite3.h").exists() {
            return Some((inc, Some(lib)));
        }
    }
    // macOS SDK often has sqlite3.h; link with -lsqlite3
    #[cfg(target_os = "macos")]
    {
        return Some((PathBuf::from("."), None));
    }
    #[cfg(not(target_os = "macos"))]
    {
        if PathBuf::from("/usr/include/sqlite3.h").exists() {
            return Some((PathBuf::from("/usr/include"), None));
        }
        None
    }
}

/// OpenCL for multi-vendor GPU compute (NVIDIA / AMD / Intel / Apple).
/// Returns `(include_dir, lib_dir_opt)`. On macOS, lib is None (use `-framework OpenCL`).
fn find_opencl() -> Option<(PathBuf, Option<PathBuf>)> {
    // Explicit opt-out (CI / minimal containers).
    if std::env::var_os("MAKO_NO_OPENCL").is_some() {
        return None;
    }
    // macOS: system OpenCL framework (still ships; maps to Apple GPU).
    #[cfg(target_os = "macos")]
    {
        // Header is provided by the SDK when linking the framework.
        return Some((PathBuf::from("."), None));
    }
    #[cfg(not(target_os = "macos"))]
    {
        // Homebrew / system ICD loaders
        let candidates = [
            PathBuf::from("/opt/homebrew/opt/opencl-headers"),
            PathBuf::from("/usr/local/opt/opencl-headers"),
            PathBuf::from("/opt/homebrew/opt/opencl-icd-loader"),
            PathBuf::from("/usr/local/opt/opencl-icd-loader"),
            PathBuf::from("/usr"),
            PathBuf::from("/usr/local"),
            PathBuf::from("/opt/cuda"),
            PathBuf::from("/usr/local/cuda"),
        ];
        for base in candidates {
            let inc = base.join("include");
            if inc.join("CL/cl.h").exists() || inc.join("CL/opencl.h").exists() {
                let lib = base.join("lib");
                let lib64 = base.join("lib64");
                let l = if lib64.exists() {
                    Some(lib64)
                } else if lib.exists() {
                    Some(lib)
                } else {
                    None
                };
                return Some((inc, l));
            }
        }
        if Path::new("/usr/include/CL/cl.h").exists() {
            return Some((PathBuf::from("/usr/include"), Some(PathBuf::from("/usr/lib"))));
        }
        // pkg-config (ocl-icd, OpenCL-Headers)
        for pkg in ["OpenCL", "OpenCL-Headers", "ocl-icd"] {
            if let Ok(out) = Command::new("pkg-config")
                .args(["--variable=includedir", pkg])
                .output()
            {
                if out.status.success() {
                    let inc = PathBuf::from(String::from_utf8_lossy(&out.stdout).trim());
                    if inc.join("CL/cl.h").exists() || inc.join("CL/opencl.h").exists() {
                        let mut lib = None;
                        if let Ok(lout) = Command::new("pkg-config")
                            .args(["--variable=libdir", pkg])
                            .output()
                        {
                            if lout.status.success() {
                                let lp = PathBuf::from(String::from_utf8_lossy(&lout.stdout).trim());
                                if lp.exists() {
                                    lib = Some(lp);
                                }
                            }
                        }
                        return Some((inc, lib));
                    }
                }
            }
        }
        None
    }
}

fn find_libpq() -> Option<(PathBuf, PathBuf)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/libpq"),
        PathBuf::from("/usr/local/opt/libpq"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("libpq-fe.h").exists() && lib.exists() {
            return Some((inc, lib));
        }
    }
    if let Ok(out) = Command::new("pkg-config")
        .args(["--variable=prefix", "libpq"])
        .output()
    {
        if out.status.success() {
            let prefix = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if !prefix.is_empty() {
                let base = PathBuf::from(prefix);
                let inc = base.join("include");
                let lib = base.join("lib");
                if inc.join("libpq-fe.h").exists() {
                    return Some((inc, lib));
                }
            }
        }
    }
    // Debian/Ubuntu style
    if PathBuf::from("/usr/include/postgresql/libpq-fe.h").exists() {
        return Some((
            PathBuf::from("/usr/include/postgresql"),
            PathBuf::from("/usr/lib"),
        ));
    }
    None
}

fn find_openssl() -> Option<(PathBuf, PathBuf)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/openssl@3"),
        PathBuf::from("/usr/local/opt/openssl@3"),
        PathBuf::from("/opt/homebrew/opt/openssl"),
        PathBuf::from("/usr/local/opt/openssl"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("openssl/ssl.h").exists() && lib.exists() {
            return Some((inc, lib));
        }
    }
    // pkg-config
    if let Ok(out) = Command::new("pkg-config")
        .args(["--variable=prefix", "openssl"])
        .output()
    {
        if out.status.success() {
            let prefix = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if !prefix.is_empty() {
                let base = PathBuf::from(prefix);
                let inc = base.join("include");
                let lib = base.join("lib");
                if inc.join("openssl/ssl.h").exists() {
                    return Some((inc, lib));
                }
            }
        }
    }
    // System include path (Linux distro packages)
    if PathBuf::from("/usr/include/openssl/ssl.h").exists() {
        return Some((PathBuf::from("/usr/include"), PathBuf::from("/usr/lib")));
    }
    // Also check multiarch (Debian/Ubuntu)
    let arch_lib = PathBuf::from("/usr/lib").join(std::env::consts::ARCH.to_string() + "-linux-gnu");
    if PathBuf::from("/usr/include/openssl/ssl.h").exists() && arch_lib.exists() {
        return Some((PathBuf::from("/usr/include"), arch_lib));
    }
    None
}

struct QuicheLink {
    include: PathBuf,
    lib_dir: PathBuf,
    /// Prefer `libquiche.a` when the dylib install_name is not rpath-relocatable.
    prefer_static: bool,
}

fn quiche_dylib_ok(lib_dir: &Path) -> bool {
    let dylib = lib_dir.join("libquiche.dylib");
    if !dylib.exists() {
        return false;
    }
    // cargo ffi often stamps install_name /usr/local/lib/libquiche.X.dylib — unusable
    // unless we fixed it to @rpath (build_quiche_ffi.sh --try does this for the local copy).
    if let Ok(out) = Command::new("otool")
        .args(["-D", &dylib.display().to_string()])
        .output()
    {
        let s = String::from_utf8_lossy(&out.stdout);
        if s.contains("@rpath") || s.contains(&lib_dir.display().to_string()) {
            return true;
        }
        return false;
    }
    true
}

fn quiche_pair_ok(include: &Path, lib_dir: &Path) -> Option<QuicheLink> {
    if !include.join("quiche.h").exists() {
        return None;
    }
    let has_a = lib_dir.join("libquiche.a").exists();
    let dylib_ok = quiche_dylib_ok(lib_dir);
    if !dylib_ok && !has_a {
        return None;
    }
    Some(QuicheLink {
        include: include.to_path_buf(),
        lib_dir: lib_dir.to_path_buf(),
        prefer_static: !dylib_ok && has_a,
    })
}

/// Locate quiche C ABI (third_party build, CARGO_TARGET_DIR, or common paths).
fn find_quiche() -> Option<QuicheLink> {
    let mut candidates: Vec<(PathBuf, PathBuf)> = Vec::new();

    // 1) In-tree FFI output (preferred; install_name should be @rpath)
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        let root = PathBuf::from(manifest);
        candidates.push((
            root.join("runtime/third_party/quiche/src/quiche/include"),
            root.join("runtime/third_party/quiche/target/release"),
        ));
    }
    if let Ok(cwd) = std::env::current_dir() {
        candidates.push((
            cwd.join("runtime/third_party/quiche/src/quiche/include"),
            cwd.join("runtime/third_party/quiche/target/release"),
        ));
    }

    // 2) Env overrides
    if let Ok(td) = std::env::var("CARGO_TARGET_DIR") {
        let lib = PathBuf::from(td).join("release");
        let incs = [
            PathBuf::from("runtime/third_party/quiche/src/quiche/include"),
            PathBuf::from("/usr/local/include"),
            PathBuf::from("/opt/homebrew/include"),
        ];
        for inc in incs {
            candidates.push((inc, lib.clone()));
        }
        if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
            candidates.push((
                PathBuf::from(manifest).join("runtime/third_party/quiche/src/quiche/include"),
                lib.clone(),
            ));
        }
    }
    if let Ok(root) = std::env::var("MAKO_QUICHE_ROOT") {
        let root = PathBuf::from(root);
        candidates.push((root.join("include"), root.join("lib")));
        candidates.push((root.join("quiche/include"), root.join("target/release")));
        candidates.push((root.join("src/quiche/include"), root.join("target/release")));
    }

    // 3) Common install prefixes
    for base in [
        PathBuf::from("/usr/local"),
        PathBuf::from("/opt/homebrew"),
        PathBuf::from("/opt/homebrew/opt/quiche"),
        PathBuf::from("/usr/local/opt/quiche"),
    ] {
        candidates.push((base.join("include"), base.join("lib")));
        candidates.push((base.join("include/quiche"), base.join("lib")));
    }

    for (inc, lib) in candidates {
        if let Some(link) = quiche_pair_ok(&inc, &lib) {
            return Some(link);
        }
    }

    // 4) pkg-config (from quiche.pc after ffi build)
    if let Ok(out) = Command::new("pkg-config")
        .args(["--variable=includedir", "quiche"])
        .output()
    {
        if out.status.success() {
            let includedir = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if let Ok(libout) = Command::new("pkg-config")
                .args(["--variable=libdir", "quiche"])
                .output()
            {
                if libout.status.success() {
                    let libdir = String::from_utf8_lossy(&libout.stdout).trim().to_string();
                    if !includedir.is_empty() && !libdir.is_empty() {
                        if let Some(link) =
                            quiche_pair_ok(Path::new(&includedir), Path::new(&libdir))
                        {
                            return Some(link);
                        }
                    }
                }
            }
        }
    }
    None
}

/// Locate libnghttp2 (Homebrew `libnghttp2` / pkg-config). Prefer system link over vendoring.
fn find_nghttp2() -> Option<(PathBuf, PathBuf)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/libnghttp2"),
        PathBuf::from("/usr/local/opt/libnghttp2"),
        PathBuf::from("/opt/homebrew/opt/nghttp2"),
        PathBuf::from("/usr/local/opt/nghttp2"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("nghttp2/nghttp2.h").exists() && lib.exists() {
            return Some((inc, lib));
        }
    }
    if let Ok(out) = Command::new("pkg-config")
        .args(["--variable=prefix", "libnghttp2"])
        .output()
    {
        if out.status.success() {
            let prefix = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if !prefix.is_empty() {
                let base = PathBuf::from(prefix);
                let inc = base.join("include");
                let lib = base.join("lib");
                if inc.join("nghttp2/nghttp2.h").exists() {
                    return Some((inc, lib));
                }
            }
        }
    }
    None
}

fn runtime_include_dir() -> Result<PathBuf, String> {
    // 1) Explicit override (install scripts / CI / brew wrappers)
    if let Ok(rt) = std::env::var("MAKO_RUNTIME") {
        let p = PathBuf::from(rt);
        if p.join("mako_rt.h").exists() {
            return Ok(p.canonicalize().unwrap_or(p));
        }
        return Err(format!(
            "MAKO_RUNTIME set to {} but mako_rt.h not found",
            p.display()
        ));
    }

    // 2) Dev checkout: CARGO_MANIFEST_DIR or cwd
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        let p = PathBuf::from(manifest).join("runtime");
        if p.join("mako_rt.h").exists() {
            return Ok(p);
        }
    }
    let cwd = std::env::current_dir().map_err(|e| e.to_string())?;
    let p = cwd.join("runtime");
    if p.join("mako_rt.h").exists() {
        return Ok(p);
    }

    // 3) Relative to the mako binary (prefix installs)
    //    $PREFIX/bin/mako → $PREFIX/share/mako/runtime
    if let Ok(exe) = std::env::current_exe() {
        if let Some(bin_dir) = exe.parent() {
            let candidates = [
                bin_dir.join("runtime"),
                bin_dir.join("../runtime"),
                bin_dir.join("../share/mako/runtime"),
                bin_dir.join("../../share/mako/runtime"),
                bin_dir.join("../../runtime"),
            ];
            for candidate in candidates {
                if candidate.join("mako_rt.h").exists() {
                    return Ok(candidate.canonicalize().unwrap_or(candidate));
                }
            }
        }
    }

    // 4) Common local prefixes
    let mut prefixes = vec![
        PathBuf::from("/usr/local/share/mako/runtime"),
        PathBuf::from("/opt/homebrew/share/mako/runtime"),
    ];
    if let Some(p) = dirs_home_share_mako() {
        prefixes.push(p);
    }
    for base in prefixes {
        if base.join("mako_rt.h").exists() {
            return Ok(base);
        }
    }

    Err(
        "could not find runtime/mako_rt.h — set MAKO_RUNTIME, run from a checkout, or make install"
            .into(),
    )
}

fn dirs_home_share_mako() -> Option<PathBuf> {
    let home = std::env::var_os("HOME")?;
    Some(PathBuf::from(home).join(".local/share/mako/runtime"))
}

fn discover_std_dir() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("MAKO_STD") {
        let p = PathBuf::from(p);
        if p.join("strings/strings.mko").exists() {
            return Some(p);
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(bin_dir) = exe.parent() {
            for candidate in [
                bin_dir.join("../share/mako/std"),
                bin_dir.join("../../share/mako/std"),
            ] {
                if candidate.join("strings/strings.mko").exists() {
                    return Some(candidate.canonicalize().unwrap_or(candidate));
                }
            }
        }
    }
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        let p = PathBuf::from(manifest).join("std");
        if p.join("strings/strings.mko").exists() {
            return Some(p);
        }
    }
    if let Ok(cwd) = std::env::current_dir() {
        let p = cwd.join("std");
        if p.join("strings/strings.mko").exists() {
            return Some(p);
        }
    }
    dirs_home_share_mako_std()
}

fn dirs_home_share_mako_std() -> Option<PathBuf> {
    let home = std::env::var_os("HOME")?;
    let p = PathBuf::from(home).join(".local/share/mako/std");
    if p.join("strings/strings.mko").exists() {
        Some(p)
    } else {
        None
    }
}

fn discover_vscode_extension_dir() -> Option<PathBuf> {
    if let Ok(cwd) = std::env::current_dir() {
        let p = cwd.join("editors/vscode");
        if p.join("package.json").exists() {
            return Some(p);
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(bin_dir) = exe.parent() {
            for candidate in [
                bin_dir.join("../share/mako/editors/vscode"),
                bin_dir.join("../../share/mako/editors/vscode"),
            ] {
                if candidate.join("package.json").exists() {
                    return Some(candidate.canonicalize().unwrap_or(candidate));
                }
            }
        }
    }
    None
}

/// Read `name = "..."` from the nearest mako.toml walking up from `start`.
fn nearest_package_name(start: &Path) -> Option<String> {
    let mut dir = if start.is_file() {
        start.parent()?.to_path_buf()
    } else {
        start.to_path_buf()
    };
    for _ in 0..8 {
        let toml = dir.join("mako.toml");
        if toml.exists() {
            if let Ok(text) = fs::read_to_string(&toml) {
                for line in text.lines() {
                    let t = line.trim();
                    if t.starts_with('#') {
                        continue;
                    }
                    if let Some(rest) = t.strip_prefix("name") {
                        let rest = rest.trim().trim_start_matches('=').trim();
                        let name = rest.trim_matches('"').trim_matches('\'').trim();
                        if !name.is_empty() {
                            return Some(name.to_string());
                        }
                    }
                    if t.starts_with('[') {
                        break;
                    }
                }
            }
            return None;
        }
        if !dir.pop() {
            break;
        }
    }
    None
}

#[cfg(test)]
mod build_policy_tests {
    use super::{effective_static_link, static_link_default};

    #[test]
    fn linux_musl_targets_default_to_static() {
        assert!(static_link_default(Some("x86_64-unknown-linux-musl")));
        assert!(static_link_default(Some("aarch64-unknown-linux-musl")));
        assert!(!static_link_default(Some("x86_64-unknown-linux-gnu")));
        assert!(!static_link_default(Some("x86_64-apple-darwin")));
        assert!(!static_link_default(None));
    }

    #[test]
    fn static_link_can_be_requested_or_disabled() {
        assert!(effective_static_link(
            Some("x86_64-unknown-linux-gnu"),
            true,
            false
        ));
        assert!(effective_static_link(
            Some("x86_64-unknown-linux-musl"),
            false,
            false
        ));
        assert!(!effective_static_link(
            Some("x86_64-unknown-linux-musl"),
            true,
            true
        ));
    }
}
