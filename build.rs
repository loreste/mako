use std::env;
use std::path::{Path, PathBuf};

fn existing_prefix(candidates: &[&str]) -> Option<PathBuf> {
    candidates
        .iter()
        .map(PathBuf::from)
        .find(|path| path.join("bin/llvm-config").is_file())
}

fn main() {
    println!("cargo:rerun-if-changed=src/lld_shim.cpp");
    println!("cargo:rerun-if-env-changed=LLVM_SYS_211_PREFIX");
    println!("cargo:rerun-if-env-changed=MAKO_LLD_STATIC_DIR");
    println!("cargo:rerun-if-env-changed=MAKO_LLD_INCLUDE_DIR");
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
        .warnings(true);
    if cfg!(target_os = "macos") {
        runtime.flag("-mmacosx-version-min=13.0");
    }
    runtime.compile("mako_native_runtime");
    println!("cargo:rerun-if-changed=runtime/native_runtime.c");
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
