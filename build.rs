use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn existing_prefix(candidates: &[&str]) -> Option<PathBuf> {
    candidates
        .iter()
        .map(PathBuf::from)
        .find(|path| path.join("bin/llvm-config").is_file())
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
    if PathBuf::from("/usr/include/openssl/ssl.h").exists() {
        return Some((PathBuf::from("/usr/include"), PathBuf::from("/usr/lib")));
    }
    None
}

/// (include_dir, optional lib_dir). lib_dir None means system linker path is enough.
fn find_sqlite() -> Option<(PathBuf, Option<PathBuf>)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/sqlite"),
        PathBuf::from("/usr/local/opt/sqlite"),
        PathBuf::from("/opt/homebrew/opt/sqlite3"),
        PathBuf::from("/usr/local/opt/sqlite3"),
    ];
    for base in candidates {
        let inc = base.join("include");
        let lib = base.join("lib");
        if inc.join("sqlite3.h").exists() {
            return Some((inc, if lib.exists() { Some(lib) } else { None }));
        }
    }
    if let Ok(out) = Command::new("pkg-config")
        .args(["--variable=prefix", "sqlite3"])
        .output()
    {
        if out.status.success() {
            let prefix = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if !prefix.is_empty() {
                let base = PathBuf::from(prefix);
                let inc = base.join("include");
                let lib = base.join("lib");
                if inc.join("sqlite3.h").exists() {
                    return Some((inc, if lib.exists() { Some(lib) } else { None }));
                }
            }
        }
    }
    if PathBuf::from("/usr/include/sqlite3.h").exists() {
        return Some((PathBuf::from("/usr/include"), None));
    }
    // macOS SDK often ships sqlite3.h
    if cfg!(target_os = "macos") {
        return Some((PathBuf::from("/usr/include"), None));
    }
    None
}

fn find_libpq() -> Option<(PathBuf, PathBuf)> {
    let candidates = [
        PathBuf::from("/opt/homebrew/opt/libpq"),
        PathBuf::from("/usr/local/opt/libpq"),
        PathBuf::from("/opt/homebrew/opt/postgresql"),
        PathBuf::from("/usr/local/opt/postgresql"),
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
    if PathBuf::from("/usr/include/postgresql/libpq-fe.h").exists() {
        return Some((
            PathBuf::from("/usr/include/postgresql"),
            PathBuf::from("/usr/lib"),
        ));
    }
    None
}

struct QuicheLink {
    include: PathBuf,
    lib_dir: PathBuf,
    prefer_static: bool,
}

fn find_quiche() -> Option<QuicheLink> {
    let mut candidates: Vec<(PathBuf, PathBuf)> = Vec::new();
    if let Ok(manifest) = env::var("CARGO_MANIFEST_DIR") {
        let root = PathBuf::from(manifest);
        candidates.push((
            root.join("runtime/third_party/quiche/src/quiche/include"),
            root.join("runtime/third_party/quiche/target/release"),
        ));
    }
    if let Ok(cwd) = env::current_dir() {
        candidates.push((
            cwd.join("runtime/third_party/quiche/src/quiche/include"),
            cwd.join("runtime/third_party/quiche/target/release"),
        ));
    }
    candidates.push((
        PathBuf::from("/usr/local/include"),
        PathBuf::from("/usr/local/lib"),
    ));
    candidates.push((
        PathBuf::from("/opt/homebrew/include"),
        PathBuf::from("/opt/homebrew/lib"),
    ));
    for (inc, lib) in candidates {
        let header = if inc.join("quiche.h").exists() {
            true
        } else {
            inc.join("quiche/quiche.h").exists()
        };
        if !header {
            continue;
        }
        let has_a = lib.join("libquiche.a").exists();
        let has_dylib = lib.join("libquiche.dylib").exists() || lib.join("libquiche.so").exists();
        if has_a || has_dylib {
            return Some(QuicheLink {
                include: inc,
                lib_dir: lib,
                prefer_static: has_a,
            });
        }
    }
    None
}

fn main() {
    println!("cargo:rerun-if-changed=src/lld_shim.cpp");
    println!("cargo:rerun-if-env-changed=LLVM_SYS_211_PREFIX");
    println!("cargo:rerun-if-env-changed=MAKO_LLD_STATIC_DIR");
    println!("cargo:rerun-if-env-changed=MAKO_LLD_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=MAKO_NO_OPENCL");
    println!("cargo:rerun-if-env-changed=MAKO_NO_OPENSSL");
    println!("cargo:rerun-if-env-changed=MAKO_NO_QUICHE");
    if env::var_os("CARGO_FEATURE_LLVM_BACKEND").is_none() {
        return;
    }

    let llvm_prefix = env::var_os("LLVM_SYS_211_PREFIX")
        .map(PathBuf::from)
        .or_else(|| {
            existing_prefix(&[
                "/opt/homebrew/opt/llvm@21",
                "/usr/local/opt/llvm@21",
                "/usr/lib/llvm-21",
            ])
        })
        .expect("llvm-backend requires LLVM_SYS_211_PREFIX pointing to LLVM 21");
    let lld_static = env::var_os("MAKO_LLD_STATIC_DIR")
        .map(PathBuf::from)
        .expect("llvm-backend requires MAKO_LLD_STATIC_DIR from bootstrap-native-toolchain.sh");
    for archive in ["liblldMachO.a", "liblldCommon.a"] {
        assert!(
            lld_static.join(archive).is_file(),
            "missing {} in {}",
            archive,
            lld_static.display()
        );
    }

    let lld_include = env::var_os("MAKO_LLD_INCLUDE_DIR")
        .map(PathBuf::from)
        .or_else(|| existing_lld_prefix().map(|prefix| prefix.join("include")))
        .expect("llvm-backend requires MAKO_LLD_INCLUDE_DIR pointing to lld headers");
    assert!(
        lld_include.join("lld/Common/Driver.h").is_file(),
        "missing lld/Common/Driver.h in {}",
        lld_include.display()
    );
    cc::Build::new()
        .cpp(true)
        .file("src/lld_shim.cpp")
        .include(llvm_prefix.join("include"))
        .include(lld_include)
        .flag_if_supported("-std=c++17")
        .compile("mako_lld_shim");

    let mut runtime = cc::Build::new();
    runtime
        .cargo_metadata(false)
        .file("runtime/native_runtime.c")
        .file("runtime/native_bridge.c")
        .file("runtime/mako_extern_demo.c")
        .include("runtime")
        .warnings(true);
    if cfg!(target_os = "macos") {
        runtime.flag("-mmacosx-version-min=13.0");
    }
    runtime.flag_if_supported("-Wno-comment");
    runtime.flag_if_supported("-Wno-unused-parameter");
    runtime.flag_if_supported("-Wno-unused-variable");

    // ---- Optional crypto / GPU / QUIC backends for the native runtime archive ----
    if env::var_os("MAKO_NO_OPENSSL").is_none() {
        if let Some((inc, lib)) = find_openssl() {
            runtime.define("MAKO_HAS_OPENSSL", None);
            runtime.define("MAKO_USE_OPENSSL", None);
            runtime.include(&inc);
            println!("cargo:rustc-link-search=native={}", lib.display());
            println!("cargo:rustc-link-lib=ssl");
            println!("cargo:rustc-link-lib=crypto");
            println!("cargo:warning=native-runtime: OpenSSL enabled ({})", inc.display());
        } else {
            println!("cargo:warning=native-runtime: OpenSSL not found; TLS uses header stubs");
        }
    }

    // GPU host path always available; OpenCL optional.
    if env::var_os("MAKO_NO_OPENCL").is_none() {
        #[cfg(target_os = "macos")]
        {
            runtime.define("MAKO_HAS_OPENCL", None);
            println!("cargo:rustc-link-lib=framework=OpenCL");
            println!("cargo:warning=native-runtime: OpenCL framework enabled");
        }
        #[cfg(not(target_os = "macos"))]
        {
            // Best-effort Linux OpenCL headers
            for base in [
                PathBuf::from("/opt/homebrew/opt/opencl-headers"),
                PathBuf::from("/usr/local/opt/opencl-headers"),
                PathBuf::from("/usr"),
            ] {
                let inc = base.join("include");
                if inc.join("CL/cl.h").exists() || inc.join("CL/opencl.h").exists() {
                    runtime.define("MAKO_HAS_OPENCL", None);
                    runtime.include(inc);
                    println!("cargo:rustc-link-lib=OpenCL");
                    println!("cargo:warning=native-runtime: OpenCL enabled");
                    break;
                }
            }
        }
    }

    if env::var_os("MAKO_NO_QUICHE").is_none() {
        if let Some(q) = find_quiche() {
            runtime.define("MAKO_HAS_QUICHE", None);
            runtime.include(&q.include);
            println!("cargo:rustc-link-search=native={}", q.lib_dir.display());
            if q.prefer_static {
                // Force the archive onto the link line.
                println!(
                    "cargo:rustc-link-arg={}",
                    q.lib_dir.join("libquiche.a").display()
                );
            } else {
                println!("cargo:rustc-link-lib=quiche");
            }
            if cfg!(target_os = "macos") {
                println!("cargo:rustc-link-lib=c++");
                println!("cargo:rustc-link-lib=framework=Security");
                println!("cargo:rustc-link-lib=framework=CoreFoundation");
            } else {
                println!("cargo:rustc-link-lib=stdc++");
                println!("cargo:rustc-link-lib=dl");
                println!("cargo:rustc-link-lib=m");
            }
            println!(
                "cargo:warning=native-runtime: quiche enabled ({})",
                q.include.display()
            );
        } else {
            println!("cargo:warning=native-runtime: quiche not found; H3 uses header stubs");
        }
    }

    // SQLite / libpq: needed so native_bridge can compile the real sql_* paths
    // (header stubs return NULL without these defines).
    if env::var_os("MAKO_NO_SQLITE").is_none() {
        if let Some((inc, lib)) = find_sqlite() {
            runtime.define("MAKO_HAS_SQLITE", None);
            if inc != PathBuf::from("/usr/include") {
                runtime.include(&inc);
            }
            if let Some(lib) = lib {
                println!("cargo:rustc-link-search=native={}", lib.display());
            }
            println!("cargo:rustc-link-lib=sqlite3");
            println!(
                "cargo:warning=native-runtime: SQLite enabled ({})",
                inc.display()
            );
        } else {
            println!("cargo:warning=native-runtime: SQLite not found; sql_open_sqlite stubs");
        }
    }
    if env::var_os("MAKO_NO_LIBPQ").is_none() {
        if let Some((inc, lib)) = find_libpq() {
            runtime.define("MAKO_HAS_LIBPQ", None);
            runtime.include(&inc);
            println!("cargo:rustc-link-search=native={}", lib.display());
            println!("cargo:rustc-link-lib=pq");
            println!(
                "cargo:warning=native-runtime: libpq enabled ({})",
                inc.display()
            );
        } else {
            println!("cargo:warning=native-runtime: libpq not found; postgres sql stubs");
        }
    }
    println!("cargo:rerun-if-env-changed=MAKO_NO_SQLITE");
    println!("cargo:rerun-if-env-changed=MAKO_NO_LIBPQ");

    runtime.compile("mako_native_runtime");
    println!("cargo:rerun-if-changed=runtime/native_runtime.c");
    println!("cargo:rerun-if-changed=runtime/native_bridge.c");
    println!("cargo:rerun-if-changed=runtime/mako_extern_demo.c");
    println!("cargo:rustc-link-search=native={}", lld_static.display());
    println!("cargo:rustc-link-lib=static=lldMachO");
    println!("cargo:rustc-link-lib=static=lldCommon");
}

fn existing_lld_prefix() -> Option<&'static Path> {
    [
        "/opt/homebrew/opt/lld@21",
        "/usr/local/opt/lld@21",
        "/usr/lib/llvm-21",
    ]
    .iter()
    .map(Path::new)
    .find(|path| path.join("include/lld/Common/Driver.h").is_file())
}
