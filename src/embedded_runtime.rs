use std::fs;
use std::path::{Path, PathBuf};

const ARCHIVE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/libmako_native_runtime.a"));

pub fn materialize(next_to: &Path) -> Result<PathBuf, String> {
    let path = next_to.with_file_name(format!("mako_native_runtime_{}.a", std::process::id()));
    fs::write(&path, ARCHIVE)
        .map_err(|error| format!("could not materialize embedded native runtime: {error}"))?;
    Ok(path)
}
