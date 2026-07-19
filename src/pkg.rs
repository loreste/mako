//! Mako package manager: resolve, lockfile, install, update, local publish.
//!
//! Layout:
//! - Manifest: `mako.toml` (`name`/`version` + `[dependencies]`)
//! - Lockfile: `mako.lock` (reproducible pins)
//! - Local registry: `$MAKO_REGISTRY` or `<project>/.mako/registry/<key>/<ver>/`
//! - Git cache: `<project>/.mako/deps/<key>/`

use std::collections::{BTreeMap, HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Arc;

use sha2::{Digest, Sha256};

use crate::tooling::{
    dep_cache_key, git_dep_cache_abs, parse_manifest_deps, parse_semver, registry_resolve,
    registry_root, valid_dep_cache_name, version_satisfies, ManifestDep,
};

const LOCKFILE_VERSION: u32 = 2;
const LEGACY_LOCKFILE_VERSION: u32 = 1;
const PACKAGE_HASH_PREFIX: &str = "sha256:";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LockedPackage {
    pub name: String,
    pub version: String,
    pub source: String, // path | git | registry
    pub path: Option<String>,
    pub git: Option<String>,
    pub rev: Option<String>,
    pub tag: Option<String>,
    pub branch: Option<String>,
    pub content_hash: String,
}

#[derive(Debug, Clone, Default)]
pub struct Lockfile {
    pub version: u32,
    pub packages: Vec<LockedPackage>,
}

#[derive(Debug, Default)]
pub(crate) struct VerifiedDependencyRoots {
    by_manifest: HashMap<PathBuf, HashMap<String, Arc<VerifiedPackage>>>,
    packages: HashMap<PathBuf, Arc<VerifiedPackage>>,
}

/// Exact package inputs retained from the hash pass so compilation never
/// reopens mutable dependency files.
#[derive(Debug)]
pub(crate) struct VerifiedPackage {
    root: PathBuf,
    root_is_file: bool,
    files: BTreeMap<PathBuf, Vec<u8>>,
}

impl VerifiedDependencyRoots {
    fn manifest_id(manifest_dir: &Path) -> PathBuf {
        manifest_dir
            .canonicalize()
            .unwrap_or_else(|_| manifest_dir.to_path_buf())
    }

    fn insert(&mut self, manifest_dir: &Path, name: String, package: Arc<VerifiedPackage>) {
        self.packages
            .entry(Self::manifest_id(package.root()))
            .or_insert_with(|| package.clone());
        self.by_manifest
            .entry(Self::manifest_id(manifest_dir))
            .or_default()
            .insert(name, package);
    }

    pub(crate) fn get(&self, manifest_dir: &Path, name: &str) -> Option<&VerifiedPackage> {
        self.by_manifest
            .get(&Self::manifest_id(manifest_dir))?
            .get(name)
            .map(Arc::as_ref)
    }

    pub(crate) fn package_for_path(&self, path: &Path) -> Option<&VerifiedPackage> {
        self.packages
            .values()
            .filter(|package| package.contains(path))
            .max_by_key(|package| package.root().components().count())
            .map(Arc::as_ref)
    }
}

impl VerifiedPackage {
    fn new(root: &Path, snapshot: PackageSnapshot) -> Result<Self, String> {
        Ok(Self {
            root: lexical_absolute(root)?,
            root_is_file: snapshot.root_is_file,
            files: snapshot.files,
        })
    }

    pub(crate) fn root(&self) -> &Path {
        &self.root
    }

    fn relative_path(&self, path: &Path) -> Option<PathBuf> {
        let path = lexical_absolute(path).ok()?;
        if self.root_is_file {
            return if path == self.root {
                self.files.keys().next().cloned()
            } else {
                None
            };
        }
        path.strip_prefix(&self.root).ok().map(Path::to_path_buf)
    }

    fn contains(&self, path: &Path) -> bool {
        let Some(relative) = self.relative_path(path) else {
            return false;
        };
        relative.as_os_str().is_empty()
            || self.files.contains_key(&relative)
            || self
                .files
                .keys()
                .any(|candidate| candidate.starts_with(&relative))
    }

    pub(crate) fn read_source(&self, path: &Path) -> Result<&str, String> {
        let relative = self.relative_path(path).ok_or_else(|| {
            format!(
                "verified package input {} is outside {}",
                path.display(),
                self.root.display()
            )
        })?;
        let contents = self.files.get(&relative).ok_or_else(|| {
            format!(
                "verified package input {} was not present during verification",
                path.display()
            )
        })?;
        std::str::from_utf8(contents)
            .map_err(|error| format!("read verified source {}: {error}", path.display()))
    }

    pub(crate) fn source_paths_at(&self, target: &Path) -> Result<Vec<PathBuf>, String> {
        let relative = self.relative_path(target).ok_or_else(|| {
            format!(
                "verified source path {} is outside {}",
                target.display(),
                self.root.display()
            )
        })?;
        if self.files.contains_key(&relative) {
            return Ok(vec![lexical_absolute(target)?]);
        }

        let mut files: Vec<_> = self
            .files
            .keys()
            .filter(|path| path.parent() == Some(relative.as_path()))
            .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("mko"))
            .filter(|path| {
                let name = path.file_name().and_then(|name| name.to_str()).unwrap_or("");
                !name.starts_with('.')
                    && !name.ends_with("_test.mko")
                    && !(name.starts_with("test_") && name.ends_with(".mko"))
            })
            .map(|path| self.root.join(path))
            .collect();
        files.sort_by(|left, right| {
            let left = left.file_name().and_then(|name| name.to_str()).unwrap_or("");
            let right = right.file_name().and_then(|name| name.to_str()).unwrap_or("");
            match (left == "lib.mko", right == "lib.mko") {
                (true, false) => std::cmp::Ordering::Less,
                (false, true) => std::cmp::Ordering::Greater,
                _ => left.cmp(right),
            }
        });
        if files.is_empty() {
            return Err(format!(
                "verified package directory {} has no .mko sources",
                target.display()
            ));
        }
        Ok(files)
    }

    pub(crate) fn library_sources(&self) -> Result<Vec<PathBuf>, String> {
        let mut files = self.source_paths_at(&self.root)?;
        files.retain(|path| path.file_name().and_then(|name| name.to_str()) != Some("main.mko"));
        if files.is_empty() {
            return Err(format!(
                "verified package {} has no library .mko sources",
                self.root.display()
            ));
        }
        Ok(files)
    }

    pub(crate) fn has_manifest(&self) -> bool {
        !self.root_is_file && self.files.contains_key(Path::new("mako.toml"))
    }

    pub(crate) fn manifest_dir(&self) -> Option<&Path> {
        self.has_manifest().then_some(self.root())
    }
}

fn read_meta(text: &str) -> (Option<String>, Option<String>) {
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

fn excluded_package_entry(name: &str) -> bool {
    matches!(name, ".git" | ".mako" | "target" | "mako.lock")
}

fn normalized_relative_path(path: &Path) -> Result<String, String> {
    let mut parts = Vec::new();
    for component in path.components() {
        match component {
            std::path::Component::Normal(part) => parts.push(
                part.to_str()
                    .ok_or_else(|| {
                        format!(
                            "package path is not valid UTF-8 and cannot be hashed reproducibly: {}",
                            path.display()
                        )
                    })?
                    .to_string(),
            ),
            _ => {
                return Err(format!(
                    "package hash received a non-relative path: {}",
                    path.display()
                ));
            }
        }
    }
    Ok(parts.join("/"))
}

fn normalized_lock_path(path: &Path) -> Result<String, String> {
    let mut parts = Vec::new();
    for component in path.components() {
        match component {
            std::path::Component::CurDir => parts.push(".".to_string()),
            std::path::Component::ParentDir => parts.push("..".to_string()),
            std::path::Component::Normal(part) => parts.push(
                part.to_str()
                    .ok_or_else(|| {
                        format!(
                            "dependency path is not valid UTF-8 and cannot be locked reproducibly: {}",
                            path.display()
                        )
                    })?
                    .to_string(),
            ),
            std::path::Component::Prefix(_) | std::path::Component::RootDir => {
                return Err(format!(
                    "dependency path must be relative to the project: {}",
                    path.display()
                ));
            }
        }
    }
    let normalized = if parts.is_empty() {
        ".".into()
    } else {
        parts.join("/")
    };
    validate_lock_value("path", &normalized).map_err(|_| {
        format!(
            "dependency path contains a quote, backslash, or control character and cannot be locked reproducibly: {}",
            path.display()
        )
    })?;
    Ok(normalized)
}

fn lexical_absolute(path: &Path) -> Result<PathBuf, String> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()
            .map_err(|error| format!("read current directory: {error}"))?
            .join(path)
    };
    let mut normalized = PathBuf::new();
    for component in absolute.components() {
        match component {
            std::path::Component::CurDir => {}
            std::path::Component::ParentDir => {
                normalized.pop();
            }
            _ => normalized.push(component.as_os_str()),
        }
    }
    Ok(normalized)
}

fn project_relative_path(project: &Path, target: &Path) -> Result<PathBuf, String> {
    let project = lexical_absolute(project)?;
    let target = lexical_absolute(target)?;
    let project_parts: Vec<_> = project.components().collect();
    let target_parts: Vec<_> = target.components().collect();
    let common = project_parts
        .iter()
        .zip(&target_parts)
        .take_while(|(left, right)| left == right)
        .count();

    if common == 0
        || project_parts[common..]
            .iter()
            .any(|part| !matches!(part, std::path::Component::Normal(_)))
        || target_parts[common..]
            .iter()
            .any(|part| !matches!(part, std::path::Component::Normal(_)))
    {
        return Err(format!(
            "dependency path {} cannot be expressed relative to project {}",
            target.display(),
            project.display()
        ));
    }

    let mut relative = PathBuf::new();
    for _ in &project_parts[common..] {
        relative.push("..");
    }
    for part in &target_parts[common..] {
        relative.push(part.as_os_str());
    }
    Ok(relative)
}

fn lock_path_for(project: &Path, target: &Path) -> Result<String, String> {
    normalized_lock_path(&project_relative_path(project, target)?)
}

fn locked_source_matches(
    project: &Path,
    manifest_dir: &Path,
    dep: &ManifestDep,
    locked: &LockedPackage,
) -> Result<bool, String> {
    if let Some(path) = &dep.path {
        let expected_path = lock_path_for(project, &manifest_dir.join(path))?;
        return Ok(locked.source == "path" && locked.path.as_deref() == Some(&expected_path));
    }
    if dep.git.is_some() {
        return Ok(locked.source == "git"
            && locked.git == dep.git
            && locked.rev == dep.rev
            && locked.tag == dep.tag
            && locked.branch == dep.branch);
    }
    if dep.version.is_some() {
        return Ok(locked.source == "registry");
    }
    Ok(false)
}

fn read_dependency_manifest(dir: &Path) -> Result<Option<String>, String> {
    let manifest = dir.join("mako.toml");
    match fs::read_to_string(&manifest) {
        Ok(text) => Ok(Some(text)),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(format!(
            "read dependency manifest {}: {error}",
            manifest.display()
        )),
    }
}

fn enqueue_transitive_dependencies(
    package_name: &str,
    dir: &Path,
    queue: &mut Vec<(PathBuf, ManifestDep)>,
    visiting: &mut HashSet<String>,
) -> Result<(), String> {
    let Some(text) = read_dependency_manifest(dir)? else {
        return Ok(());
    };
    for dependency in parse_manifest_deps(&text) {
        if visiting.insert(format!("{package_name}->{}", dependency.name)) {
            queue.push((dir.to_path_buf(), dependency));
        }
    }
    Ok(())
}

fn collect_package_files(
    root: &Path,
    dir: &Path,
    files: &mut Vec<(String, PathBuf)>,
) -> Result<(), String> {
    let entries =
        fs::read_dir(dir).map_err(|e| format!("read package directory {}: {e}", dir.display()))?;
    for entry in entries {
        let entry = entry.map_err(|e| format!("read package entry in {}: {e}", dir.display()))?;
        let name = entry
            .file_name()
            .to_str()
            .ok_or_else(|| {
                format!(
                    "package path is not valid UTF-8 and cannot be hashed reproducibly: {}",
                    entry.path().display()
                )
            })?
            .to_string();
        if excluded_package_entry(&name) {
            continue;
        }
        let path = entry.path();
        let file_type = entry
            .file_type()
            .map_err(|e| format!("inspect package entry {}: {e}", path.display()))?;
        if file_type.is_symlink() {
            return Err(format!(
                "package integrity hashing does not support symbolic links: {}",
                path.display()
            ));
        }
        if file_type.is_dir() {
            collect_package_files(root, &path, files)?;
        } else if file_type.is_file() {
            let relative = path
                .strip_prefix(root)
                .map_err(|e| format!("make package path relative: {e}"))?;
            if relative == Path::new("mako.toml")
                || relative
                    .extension()
                    .and_then(|extension| extension.to_str())
                    == Some("mko")
            {
                files.push((normalized_relative_path(relative)?, path));
            }
        }
    }
    Ok(())
}

struct PackageSnapshot {
    content_hash: String,
    root_is_file: bool,
    files: BTreeMap<PathBuf, Vec<u8>>,
}

/// Read and hash package inputs (root manifest + recursive `.mko` sources) with
/// stable framing and normalized, relative paths. VCS metadata, Mako's
/// dependency cache, Rust build output, and the lockfile itself are excluded.
fn snapshot_path_dep(full: &Path) -> Result<PackageSnapshot, String> {
    if !full.exists() {
        return Err(format!(
            "cannot hash missing package path: {}",
            full.display()
        ));
    }

    let mut files = Vec::new();
    let root_is_file = full.is_file();
    if root_is_file {
        let name = full
            .file_name()
            .ok_or_else(|| format!("package file has no name: {}", full.display()))?;
        files.push((
            normalized_relative_path(Path::new(name))?,
            full.to_path_buf(),
        ));
    } else if full.is_dir() {
        collect_package_files(full, full, &mut files)?;
    } else {
        return Err(format!(
            "package path is neither a regular file nor directory: {}",
            full.display()
        ));
    }
    files.sort_by(|a, b| a.0.cmp(&b.0));

    let mut hasher = Sha256::new();
    let mut snapshot = BTreeMap::new();
    hasher.update(b"mako-package-content-v2\0");
    for (relative, path) in files {
        let contents = fs::read(&path)
            .map_err(|e| format!("read package file {} for hashing: {e}", path.display()))?;
        let relative_bytes = relative.as_bytes();
        hasher.update((relative_bytes.len() as u64).to_be_bytes());
        hasher.update(relative_bytes);
        hasher.update((contents.len() as u64).to_be_bytes());
        hasher.update(&contents);
        snapshot.insert(PathBuf::from(relative), contents);
    }
    Ok(PackageSnapshot {
        content_hash: format!("{PACKAGE_HASH_PREFIX}{:x}", hasher.finalize()),
        root_is_file,
        files: snapshot,
    })
}

fn hash_path_dep(full: &Path) -> Result<String, String> {
    Ok(snapshot_path_dep(full)?.content_hash)
}

fn path_dep_version(full: &Path) -> Option<String> {
    let manifest = if full.is_file() {
        full.parent()?.join("mako.toml")
    } else {
        full.join("mako.toml")
    };
    let text = fs::read_to_string(manifest).ok()?;
    read_meta(&text).1
}

/// Effective registry root: `MAKO_REGISTRY` or `<project>/.mako/registry`.
pub fn effective_registry(project: &Path) -> PathBuf {
    if let Ok(p) = std::env::var("MAKO_REGISTRY") {
        if !p.is_empty() {
            return PathBuf::from(p);
        }
    }
    registry_root(project)
}

fn registry_resolve_in(reg: &Path, name: &str, req: &str) -> Result<(PathBuf, String), String> {
    let root = reg.join(dep_cache_key(name));
    if !root.is_dir() {
        return Err(format!(
            "no local registry entry for `{name}` under {}",
            reg.display()
        ));
    }
    let mut best: Option<(u64, u64, u64, PathBuf, String)> = None;
    let rd = fs::read_dir(&root).map_err(|e| format!("read registry: {e}"))?;
    for ent in rd.flatten() {
        let ver_name = ent.file_name().to_string_lossy().to_string();
        if !version_satisfies(&ver_name, req) {
            continue;
        }
        let Some(sv) = parse_semver(&ver_name) else {
            continue;
        };
        let path = ent.path();
        if !path.join("mako.toml").exists() {
            continue;
        }
        match &best {
            None => best = Some((sv.0, sv.1, sv.2, path, ver_name)),
            Some((a, b, c, _, _)) if (sv.0, sv.1, sv.2) > (*a, *b, *c) => {
                best = Some((sv.0, sv.1, sv.2, path, ver_name));
            }
            _ => {}
        }
    }
    best.map(|(_, _, _, p, v)| (p, v))
        .ok_or_else(|| format!("no version of `{name}` satisfies `{req}` in local registry"))
}

fn fetch_git(project: &Path, dep: &ManifestDep, offline: bool) -> Result<PathBuf, String> {
    if !valid_dep_cache_name(&dep.name) {
        return Err(format!(
            "git dep `{}` has an invalid cache name (allowed: letters, digits, `_`, `-`, `.`)",
            dep.name
        ));
    }
    let url = dep
        .git
        .as_deref()
        .ok_or_else(|| format!("dep `{}` has no git URL", dep.name))?;
    let dest = git_dep_cache_abs(project, &dep.name);
    if dest.exists() {
        return Ok(dest);
    }
    if offline {
        return Err(format!(
            "offline mode: git dep `{}` is not cached at {}",
            dep.name,
            dest.display()
        ));
    }
    if let Some(parent) = dest.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("mkdir: {e}"))?;
    }
    let mut cmd = Command::new("git");
    cmd.arg("-c")
        .arg("core.hooksPath=/dev/null")
        .arg("clone")
        .arg("--depth")
        .arg("1");
    if let Some(b) = &dep.branch {
        cmd.arg("--branch").arg(b);
    } else if let Some(t) = &dep.tag {
        cmd.arg("--branch").arg(t);
    }
    cmd.arg(url).arg(&dest);
    let status = cmd.status().map_err(|e| format!("git clone: {e}"))?;
    if !status.success() {
        let _ = fs::remove_dir_all(&dest);
        return Err(format!("git clone failed for `{url}`"));
    }
    if let Some(r) = &dep.rev {
        let st = Command::new("git")
            .args(["-C"])
            .arg(&dest)
            .args(["checkout", "--force", r])
            .status()
            .map_err(|e| format!("git checkout: {e}"))?;
        if !st.success() {
            return Err(format!("git checkout {r} failed"));
        }
    }
    Ok(dest)
}

/// Resolve one direct dep to an on-disk root + locked metadata.
fn resolve_one(
    project: &Path,
    manifest_dir: &Path,
    dep: &ManifestDep,
    prefer_highest: bool,
    offline: bool,
) -> Result<LockedPackage, String> {
    let _ = prefer_highest;
    let reg = effective_registry(project);
    if let Some(p) = &dep.path {
        let full = manifest_dir.join(p);
        if !full.exists() {
            return Err(format!(
                "path dep `{}` MISSING: {}",
                dep.name,
                full.display()
            ));
        }
        let ver = path_dep_version(&full)
            .or_else(|| dep.version.clone())
            .unwrap_or_else(|| "0.0.0".into());
        if let Some(req) = &dep.version {
            if !version_satisfies(&ver, req) {
                return Err(format!(
                    "path dep `{}` version {ver} does not satisfy `{req}`",
                    dep.name
                ));
            }
        }
        return Ok(LockedPackage {
            name: dep.name.clone(),
            version: ver,
            source: "path".into(),
            path: Some(lock_path_for(project, &full)?),
            git: None,
            rev: None,
            tag: None,
            branch: None,
            content_hash: hash_path_dep(&full)?,
        });
    }
    if dep.git.is_some() {
        let dest = fetch_git(project, dep, offline)?;
        let ver = path_dep_version(&dest)
            .or_else(|| dep.version.clone())
            .unwrap_or_else(|| "0.0.0".into());
        return Ok(LockedPackage {
            name: dep.name.clone(),
            version: ver,
            source: "git".into(),
            path: Some(normalized_lock_path(dest.strip_prefix(project).map_err(
                |_| {
                    format!(
                        "git dependency cache {} is outside project {}",
                        dest.display(),
                        project.display()
                    )
                },
            )?)?),
            git: dep.git.clone(),
            rev: dep.rev.clone(),
            tag: dep.tag.clone(),
            branch: dep.branch.clone(),
            content_hash: hash_path_dep(&dest)?,
        });
    }
    if let Some(req) = &dep.version {
        // Prefer project registry, then MAKO_REGISTRY / effective.
        let (dir, ver) = if let Ok(r) = registry_resolve(project, &dep.name, req) {
            let v = path_dep_version(&r)
                .unwrap_or_else(|| req.trim_start_matches(['^', '~']).to_string());
            (r, v)
        } else {
            registry_resolve_in(&reg, &dep.name, req)?
        };
        // Copy into project cache for reproducible builds if outside project.
        let cache = project
            .join(".mako")
            .join("deps")
            .join(dep_cache_key(&dep.name));
        if !cache.exists() {
            if let Some(parent) = cache.parent() {
                fs::create_dir_all(parent).map_err(|e| format!("mkdir: {e}"))?;
            }
            copy_dir_recursive(&dir, &cache)?;
        }
        let use_path = if cache.exists() { &cache } else { &dir };
        let ver = path_dep_version(use_path).unwrap_or(ver);
        return Ok(LockedPackage {
            name: dep.name.clone(),
            version: ver,
            source: "registry".into(),
            path: Some(normalized_lock_path(
                use_path.strip_prefix(project).map_err(|_| {
                    format!(
                        "registry dependency cache {} is outside project {}",
                        use_path.display(),
                        project.display()
                    )
                })?,
            )?),
            git: None,
            rev: None,
            tag: None,
            branch: None,
            content_hash: hash_path_dep(use_path)?,
        });
    }
    Err(format!(
        "dependency `{}` needs path, git, or version (registry)",
        dep.name
    ))
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<(), String> {
    fs::create_dir_all(dst).map_err(|e| format!("mkdir {}: {e}", dst.display()))?;
    for ent in fs::read_dir(src).map_err(|e| format!("read {}: {e}", src.display()))? {
        let ent = ent.map_err(|e| format!("readdir: {e}"))?;
        let from = ent.path();
        let to = dst.join(ent.file_name());
        if from.is_dir() {
            copy_dir_recursive(&from, &to)?;
        } else {
            fs::copy(&from, &to).map_err(|e| format!("copy {}: {e}", from.display()))?;
        }
    }
    Ok(())
}

/// Transitive resolve with SemVer conflict detection (Cargo-like: one version per name).
pub fn resolve_graph_with_options(
    project: &Path,
    update: bool,
    offline: bool,
) -> Result<Lockfile, String> {
    let manifest = project.join("mako.toml");
    if !manifest.exists() {
        return Err("no mako.toml — run `mako pkg init` first".into());
    }
    let text = fs::read_to_string(&manifest).map_err(|e| format!("read mako.toml: {e}"))?;
    let (root_name, root_ver) = read_meta(&text);
    let root_name = root_name.unwrap_or_else(|| "root".into());
    let root_ver = root_ver.unwrap_or_else(|| "0.1.0".into());

    let lock_path = project.join("mako.lock");
    let existing = if !update && lock_path.exists() {
        let lock = read_lockfile(&lock_path)?;
        if lock.version != LOCKFILE_VERSION {
            return Err(format!(
                "mako.lock format version {} is not supported for integrity verification; run `mako pkg update` to create version {LOCKFILE_VERSION}",
                lock.version
            ));
        }
        Some(lock)
    } else {
        None
    };

    let mut resolved: BTreeMap<String, LockedPackage> = BTreeMap::new();
    let mut reqs: HashMap<String, String> = HashMap::new(); // name → tightest req seen
    let mut queue: Vec<(PathBuf, ManifestDep)> = Vec::new();

    for d in parse_manifest_deps(&text) {
        queue.push((project.to_path_buf(), d));
    }

    let mut visiting = HashSet::new();
    while let Some((from, dep)) = queue.pop() {
        let key = dep.name.clone();
        if let Some(req) = &dep.version {
            if let Some(prev) = reqs.get(&key) {
                // Conflict if neither satisfies the other as a concrete version pick.
                // Keep both constraints: resolved version must satisfy all.
                if prev != req {
                    reqs.insert(key.clone(), format!("{prev}&&{req}"));
                }
            } else {
                reqs.insert(key.clone(), req.clone());
            }
        }

        if resolved.contains_key(&key) && !update {
            // Already locked — verify constraints.
            if let Some(lp) = resolved.get(&key) {
                if let Some(req) = &dep.version {
                    for part in req.split("&&") {
                        if !version_satisfies(&lp.version, part.trim()) {
                            return Err(format!(
                                "version conflict for `{}`: locked {} does not satisfy `{part}` (from {})",
                                key,
                                lp.version,
                                from.display()
                            ));
                        }
                    }
                }
            }
            continue;
        }

        // Prefer existing lock entry when not updating.
        if !update {
            if let Some(lock) = &existing {
                if let Some(lp) = lock.packages.iter().find(|p| p.name == key) {
                    if !locked_source_matches(project, &from, &dep, lp)? {
                        return Err(format!(
                            "lockfile source for `{key}` does not match mako.toml — run `mako pkg update`"
                        ));
                    }
                    if let Some(req) = &dep.version {
                        for part in req.split("&&") {
                            if !version_satisfies(&lp.version, part.trim()) {
                                return Err(format!(
                                    "lockfile `{}` @ {} does not satisfy `{part}` — run `mako pkg update`",
                                    key, lp.version
                                ));
                            }
                        }
                    }
                    // Ensure on disk
                    if let Some(p) = &lp.path {
                        let full = project.join(p);
                        if !full.exists() && lp.source == "git" {
                            if offline {
                                return Err(format!(
                                    "offline mode: locked git dep `{}` is missing at {}",
                                    key,
                                    full.display()
                                ));
                            }
                            fetch_git(project, &dep, false)?;
                        }
                        if !full.exists() && lp.source == "registry" {
                            resolve_one(project, &from, &dep, true, offline)?;
                        }
                        if !full.exists() {
                            return Err(format!(
                                "locked dependency `{}` is missing at {}",
                                key,
                                full.display()
                            ));
                        }
                        let actual_hash = hash_path_dep(&full)?;
                        if actual_hash != lp.content_hash {
                            return Err(format!(
                                "package integrity mismatch for `{}` at {}: expected {}, found {}; restore the locked content or run `mako pkg update` if the change is intentional",
                                key,
                                full.display(),
                                lp.content_hash,
                                actual_hash
                            ));
                        }
                    } else {
                        return Err(format!("locked dependency `{}` has no path to verify", key));
                    }
                    resolved.insert(key.clone(), lp.clone());
                    // Transitive from locked path
                    if let Some(p) = &lp.path {
                        let dir = project.join(p);
                        let next = if dir.is_file() {
                            dir.parent().map(|x| x.to_path_buf())
                        } else {
                            Some(dir)
                        };
                        if let Some(nd) = next {
                            enqueue_transitive_dependencies(&key, &nd, &mut queue, &mut visiting)?;
                        }
                    }
                    continue;
                }
            }
        }

        let locked = resolve_one(project, &from, &dep, true, offline)?;
        if let Some(combo) = reqs.get(&key) {
            for part in combo.split("&&") {
                let part = part.trim();
                if !part.is_empty() && !version_satisfies(&locked.version, part) {
                    return Err(format!(
                        "version conflict for `{}`: resolved {} does not satisfy `{part}`",
                        key, locked.version
                    ));
                }
            }
        }
        if let Some(prev) = resolved.get(&key) {
            if prev.version != locked.version {
                return Err(format!(
                    "version conflict for `{}`: need both {} and {} — pin one version in mako.toml",
                    key, prev.version, locked.version
                ));
            }
        }
        resolved.insert(key.clone(), locked.clone());

        if let Some(p) = &locked.path {
            let dir = project.join(p);
            let next = if dir.is_file() {
                dir.parent().map(|x| x.to_path_buf())
            } else {
                Some(dir)
            };
            if let Some(nd) = next {
                enqueue_transitive_dependencies(&key, &nd, &mut queue, &mut visiting)?;
            }
        }
    }

    let mut packages: Vec<LockedPackage> = resolved.into_values().collect();
    packages.sort_by(|a, b| a.name.cmp(&b.name));
    packages.insert(
        0,
        LockedPackage {
            name: root_name,
            version: root_ver,
            source: "path".into(),
            path: Some(".".into()),
            git: None,
            rev: None,
            tag: None,
            branch: None,
            content_hash: hash_path_dep(project)?,
        },
    );

    Ok(Lockfile {
        version: LOCKFILE_VERSION,
        packages,
    })
}

#[allow(dead_code)]
pub fn resolve_graph(project: &Path, update: bool) -> Result<Lockfile, String> {
    resolve_graph_with_options(project, update, false)
}

/// Verify every dependency reachable from the manifest and return its locked
/// source path. Unreachable lock entries are rejected, while projects without
/// a lockfile retain the unlocked workflow.
pub(crate) fn verified_dependency_roots(
    project: &Path,
) -> Result<Option<VerifiedDependencyRoots>, String> {
    let lock_path = project.join("mako.lock");
    if !lock_path.exists() {
        return Ok(None);
    }

    let manifest_path = project.join("mako.toml");
    let manifest = fs::read_to_string(&manifest_path)
        .map_err(|error| format!("read {}: {error}", manifest_path.display()))?;
    let lock = read_lockfile(&lock_path)?;
    if lock.version != LOCKFILE_VERSION {
        return Err(format!(
            "mako.lock format version {} is not supported for integrity verification; run `mako pkg update` to create version {LOCKFILE_VERSION}",
            lock.version
        ));
    }

    let (root_name, root_version) = read_meta(&manifest);
    let root_name = root_name.unwrap_or_else(|| "root".into());
    let root_version = root_version.unwrap_or_else(|| "0.1.0".into());
    let root = lock
        .packages
        .iter()
        .find(|package| package.source == "path" && package.path.as_deref() == Some("."))
        .ok_or_else(|| {
            "mako.lock is missing the root package; run `mako pkg update`".to_string()
        })?;
    if root.name != root_name || root.version != root_version {
        return Err(
            "mako.lock root package does not match mako.toml; run `mako pkg update`".into(),
        );
    }

    let locked: HashMap<_, _> = lock
        .packages
        .iter()
        .map(|package| (package.name.as_str(), package))
        .collect();
    let mut queue: Vec<(PathBuf, ManifestDep)> = parse_manifest_deps(&manifest)
        .into_iter()
        .map(|dependency| (project.to_path_buf(), dependency))
        .collect();
    let mut verified_packages: HashMap<String, Arc<VerifiedPackage>> = HashMap::new();
    let mut verified_roots = VerifiedDependencyRoots::default();

    while let Some((from, dependency)) = queue.pop() {
        let package = locked.get(dependency.name.as_str()).ok_or_else(|| {
            format!(
                "dependency `{}` from {} is not present in mako.lock; run `mako pkg update`",
                dependency.name,
                from.display()
            )
        })?;
        if !locked_source_matches(project, &from, &dependency, package)? {
            return Err(format!(
                "lockfile source for `{}` does not match mako.toml; run `mako pkg update`",
                dependency.name
            ));
        }
        if let Some(requirement) = &dependency.version {
            if !version_satisfies(&package.version, requirement) {
                return Err(format!(
                    "lockfile `{}` @ {} does not satisfy `{requirement}`; run `mako pkg update`",
                    dependency.name, package.version
                ));
            }
        }

        if let Some(verified) = verified_packages.get(&package.name) {
            verified_roots.insert(&from, package.name.clone(), verified.clone());
            // The first visit already enqueued this package's dependencies.
            continue;
        }
        let path = package
            .path
            .as_deref()
            .ok_or_else(|| format!("locked dependency `{}` has no path to verify", package.name))?;
        let full = project.join(path);
        if !full.exists() {
            return Err(format!(
                "locked dependency `{}` is missing at {}; run `mako pkg install`",
                package.name,
                full.display()
            ));
        }
        let snapshot = snapshot_path_dep(&full)?;
        if snapshot.content_hash != package.content_hash {
            return Err(format!(
                "package integrity mismatch for `{}` at {}: expected {}, found {}; restore the locked content or run `mako pkg update` if the change is intentional",
                package.name,
                full.display(),
                package.content_hash,
                snapshot.content_hash
            ));
        }
        let verified = Arc::new(VerifiedPackage::new(&full, snapshot)?);
        verified_roots.insert(&from, package.name.clone(), verified.clone());
        verified_packages.insert(package.name.clone(), verified.clone());

        let dependency_dir = if full.is_file() {
            full.parent().map(Path::to_path_buf)
        } else {
            Some(full)
        };
        if let Some(dir) = dependency_dir {
            let manifest = if verified.has_manifest() {
                Some(verified.read_source(&dir.join("mako.toml"))?.to_string())
            } else {
                read_dependency_manifest(&dir)?
            };
            if let Some(text) = manifest {
                queue.extend(
                    parse_manifest_deps(&text)
                        .into_iter()
                        .map(|dependency| (dir.clone(), dependency)),
                );
            }
        }
    }

    let mut stale: Vec<_> = lock
        .packages
        .iter()
        .filter(|package| {
            package.name != root.name && !verified_packages.contains_key(&package.name)
        })
        .map(|package| package.name.as_str())
        .collect();
    if !stale.is_empty() {
        stale.sort_unstable();
        return Err(format!(
            "mako.lock contains dependencies not reachable from mako.toml: {}; run `mako pkg update`",
            stale.join(", ")
        ));
    }

    Ok(Some(verified_roots))
}

fn validate_lock_value(field: &str, value: &str) -> Result<(), String> {
    if value
        .chars()
        .any(|character| character == '"' || character == '\\' || character.is_control())
    {
        return Err(format!(
            "lockfile {field} contains an unsupported quote, backslash, or control character"
        ));
    }
    Ok(())
}

fn is_valid_sha256_hash(value: &str) -> bool {
    let Some(hex) = value.strip_prefix(PACKAGE_HASH_PREFIX) else {
        return false;
    };
    hex.len() == 64
        && hex
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn validate_locked_package(package: &LockedPackage, lock_version: u32) -> Result<(), String> {
    for (field, value) in [
        ("package name", package.name.as_str()),
        ("package version", package.version.as_str()),
        ("package source", package.source.as_str()),
        ("content_hash", package.content_hash.as_str()),
    ] {
        if value.is_empty() {
            return Err(format!("lockfile package has an empty {field}"));
        }
        validate_lock_value(field, value)?;
    }
    for (field, value) in [
        ("path", package.path.as_deref()),
        ("git", package.git.as_deref()),
        ("rev", package.rev.as_deref()),
        ("tag", package.tag.as_deref()),
        ("branch", package.branch.as_deref()),
    ] {
        if let Some(value) = value {
            if value.is_empty() {
                return Err(format!(
                    "lockfile package `{}` has an empty {field}",
                    package.name
                ));
            }
            validate_lock_value(field, value)?;
        }
    }
    if package.path.is_none() {
        return Err(format!(
            "lockfile package `{}` is missing path",
            package.name
        ));
    }
    match package.source.as_str() {
        "path" | "registry" => {
            if package.git.is_some()
                || package.rev.is_some()
                || package.tag.is_some()
                || package.branch.is_some()
            {
                return Err(format!(
                    "lockfile package `{}` has source `{}` but contains git metadata",
                    package.name, package.source
                ));
            }
        }
        "git" => {
            if package.git.is_none() {
                return Err(format!(
                    "lockfile git package `{}` is missing git URL",
                    package.name
                ));
            }
        }
        other => {
            return Err(format!(
                "lockfile package `{}` has unsupported source `{other}`",
                package.name
            ));
        }
    }
    if lock_version == LOCKFILE_VERSION && !is_valid_sha256_hash(&package.content_hash) {
        return Err(format!(
            "lockfile package `{}` has invalid SHA-256 content_hash",
            package.name
        ));
    }
    Ok(())
}

fn validate_lockfile(lock: &Lockfile) -> Result<(), String> {
    if lock.version == 0 {
        return Err("lockfile version must be a positive integer".into());
    }
    if lock.version != LEGACY_LOCKFILE_VERSION && lock.version != LOCKFILE_VERSION {
        return Err(format!(
            "lockfile format version {} is not supported",
            lock.version
        ));
    }
    if lock.version == LOCKFILE_VERSION && lock.packages.is_empty() {
        return Err("lockfile version 2 must contain at least one package".into());
    }
    let mut names = HashSet::new();
    for package in &lock.packages {
        validate_locked_package(package, lock.version)?;
        if !names.insert(package.name.as_str()) {
            return Err(format!(
                "lockfile contains duplicate package `{}`",
                package.name
            ));
        }
    }
    Ok(())
}

pub fn write_lockfile(project: &Path, lock: &Lockfile) -> Result<PathBuf, String> {
    if lock.version != LOCKFILE_VERSION {
        return Err(format!(
            "refusing to write lockfile format version {}; current version is {LOCKFILE_VERSION}",
            lock.version
        ));
    }
    validate_lockfile(lock)?;
    let mut out = String::from(
        "# mako.lock — reproducible dependency pin\n# Generated by `mako pkg install` / `mako pkg lock`\n",
    );
    out.push_str(&format!("version = {}\n", lock.version));
    for package in &lock.packages {
        out.push_str("\n[[package]]\n");
        out.push_str(&format!("name = \"{}\"\n", package.name));
        out.push_str(&format!("version = \"{}\"\n", package.version));
        out.push_str(&format!("source = \"{}\"\n", package.source));
        if let Some(path) = &package.path {
            out.push_str(&format!("path = \"{path}\"\n"));
        }
        if let Some(git) = &package.git {
            out.push_str(&format!("git = \"{git}\"\n"));
        }
        if let Some(revision) = &package.rev {
            out.push_str(&format!("rev = \"{revision}\"\n"));
        }
        if let Some(tag) = &package.tag {
            out.push_str(&format!("tag = \"{tag}\"\n"));
        }
        if let Some(branch) = &package.branch {
            out.push_str(&format!("branch = \"{branch}\"\n"));
        }
        out.push_str(&format!("content_hash = \"{}\"\n", package.content_hash));
    }
    let path = project.join("mako.lock");
    fs::write(&path, out).map_err(|e| format!("write mako.lock: {e}"))?;
    Ok(path)
}

fn parse_lock_string(path: &Path, line: usize, value: &str) -> Result<String, String> {
    let value = value.trim();
    let Some(value) = value
        .strip_prefix('"')
        .and_then(|value| value.strip_suffix('"'))
    else {
        return Err(format!(
            "parse {} line {line}: expected a quoted string",
            path.display()
        ));
    };
    validate_lock_value("string value", value)
        .map_err(|error| format!("parse {} line {line}: {error}", path.display()))?;
    Ok(value.to_string())
}

fn empty_locked_package() -> LockedPackage {
    LockedPackage {
        name: String::new(),
        version: String::new(),
        source: String::new(),
        path: None,
        git: None,
        rev: None,
        tag: None,
        branch: None,
        content_hash: String::new(),
    }
}

pub fn read_lockfile(path: &Path) -> Result<Lockfile, String> {
    let text = fs::read_to_string(path).map_err(|e| format!("read {}: {e}", path.display()))?;
    let mut version = None;
    let mut packages = Vec::new();
    let mut current_package = None;
    let mut seen_fields = HashSet::new();
    for (line_index, line) in text.lines().enumerate() {
        let line_number = line_index + 1;
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line == "[[package]]" {
            if let Some(package) = current_package.take() {
                packages.push(package);
            }
            current_package = Some(empty_locked_package());
            seen_fields.clear();
            continue;
        }
        let Some((key, raw_value)) = line.split_once('=') else {
            return Err(format!(
                "parse {} line {line_number}: expected `key = value`",
                path.display()
            ));
        };
        let key = key.trim();
        let package = match current_package.as_mut() {
            Some(package) => package,
            None => {
                if key != "version" {
                    return Err(format!(
                        "parse {} line {line_number}: unexpected field `{key}` outside a package",
                        path.display()
                    ));
                }
                if version.is_some() {
                    return Err(format!(
                        "parse {} line {line_number}: duplicate lockfile version",
                        path.display()
                    ));
                }
                version = Some(raw_value.trim().parse::<u32>().map_err(|_| {
                    format!(
                        "parse {} line {line_number}: lockfile version must be a positive integer",
                        path.display()
                    )
                })?);
                continue;
            }
        };
        if !matches!(
            key,
            "name"
                | "version"
                | "source"
                | "path"
                | "git"
                | "rev"
                | "tag"
                | "branch"
                | "content_hash"
        ) {
            return Err(format!(
                "parse {} line {line_number}: unknown package field `{key}`",
                path.display()
            ));
        }
        if !seen_fields.insert(key.to_string()) {
            return Err(format!(
                "parse {} line {line_number}: duplicate package field `{key}`",
                path.display()
            ));
        }
        let value = parse_lock_string(path, line_number, raw_value)?;
        match key {
            "name" => package.name = value,
            "version" => package.version = value,
            "source" => package.source = value,
            "path" => package.path = Some(value),
            "git" => package.git = Some(value),
            "rev" => package.rev = Some(value),
            "tag" => package.tag = Some(value),
            "branch" => package.branch = Some(value),
            "content_hash" => package.content_hash = value,
            _ => unreachable!(),
        };
    }
    if let Some(package) = current_package {
        packages.push(package);
    }
    let lock = Lockfile {
        version: version
            .ok_or_else(|| format!("parse {}: missing lockfile format version", path.display()))?,
        packages,
    };
    validate_lockfile(&lock).map_err(|error| format!("parse {}: {error}", path.display()))?;
    Ok(lock)
}

pub fn pkg_install(project: &Path, offline: bool) -> Result<(), String> {
    let lock = resolve_graph_with_options(project, false, offline)?;
    let path = write_lockfile(project, &lock)?;
    println!(
        "mako pkg install{}: {} packages → {}",
        if offline { " --offline" } else { "" },
        lock.packages.len(),
        path.display()
    );
    for p in &lock.packages {
        if p.path.as_deref() == Some(".") {
            continue;
        }
        println!("  {} {} ({})", p.name, p.version, p.source);
    }
    Ok(())
}

pub fn pkg_update(project: &Path, offline: bool) -> Result<(), String> {
    let lock = resolve_graph_with_options(project, true, offline)?;
    let path = write_lockfile(project, &lock)?;
    println!(
        "mako pkg update{}: refreshed {} packages → {}",
        if offline { " --offline" } else { "" },
        lock.packages.len(),
        path.display()
    );
    for p in &lock.packages {
        if p.path.as_deref() == Some(".") {
            continue;
        }
        println!("  {} {} ({})", p.name, p.version, p.source);
    }
    Ok(())
}

#[allow(dead_code)]
pub fn pkg_lock(project: &Path, offline: bool) -> Result<(), String> {
    pkg_install(project, offline)
}

fn valid_registry_component(value: &str) -> bool {
    !value.is_empty()
        && value != "."
        && value != ".."
        && !value.starts_with('.')
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
}

fn valid_registry_version(version: &str) -> bool {
    !version.is_empty()
        && !version.starts_with('.')
        && version
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'+' | b'-'))
        && parse_semver(version).is_some()
}

fn valid_registry_name(name: &str) -> bool {
    !name.starts_with('/')
        && !name.ends_with('/')
        && !name.contains('\\')
        && name.split('/').all(valid_registry_component)
}

fn copy_publish_dir_recursive(src: &Path, dest: &Path) -> Result<(), String> {
    fs::create_dir_all(dest).map_err(|e| format!("mkdir {}: {e}", dest.display()))?;
    for ent in fs::read_dir(src).map_err(|e| format!("read {}: {e}", src.display()))? {
        let ent = ent.map_err(|e| format!("readdir: {e}"))?;
        let from = ent.path();
        let to = dest.join(ent.file_name());
        let kind = ent
            .file_type()
            .map_err(|e| format!("inspect {}: {e}", from.display()))?;
        if kind.is_symlink() {
            return Err(format!("refusing to publish symlink {}", from.display()));
        }
        if kind.is_dir() {
            copy_publish_dir_recursive(&from, &to)?;
        } else if kind.is_file() {
            fs::copy(&from, &to).map_err(|e| format!("copy {}: {e}", from.display()))?;
        }
    }
    Ok(())
}

fn copy_package_for_publish(project: &Path, manifest: &Path, dest: &Path) -> Result<(), String> {
    fs::copy(manifest, dest.join("mako.toml")).map_err(|e| format!("copy toml: {e}"))?;
    for ent in fs::read_dir(project).map_err(|e| format!("readdir: {e}"))? {
        let ent = ent.map_err(|e| e.to_string())?;
        let path = ent.path();
        let name = ent.file_name();
        let name = name.to_string_lossy();
        if name == ".mako" || name == "mako.lock" || name.starts_with('.') {
            continue;
        }
        let kind = ent
            .file_type()
            .map_err(|e| format!("inspect {}: {e}", path.display()))?;
        if kind.is_symlink() {
            return Err(format!("refusing to publish symlink {}", path.display()));
        }
        if kind.is_file() && path.extension().and_then(|x| x.to_str()) == Some("mko") {
            fs::copy(&path, dest.join(&*name)).map_err(|e| format!("copy: {e}"))?;
        } else if kind.is_dir() && (name == "src" || name == "lib") {
            copy_publish_dir_recursive(&path, &dest.join(&*name))?;
        }
    }
    Ok(())
}

fn cleanup_publish_staging(staging: &Path) {
    if let Err(error) = fs::remove_dir_all(staging) {
        eprintln!(
            "warning: could not remove publish staging directory {}: {error}",
            staging.display()
        );
    }
}

/// Publish a package with validated registry coordinates. Existing versions
/// are immutable, so publishing the same name and version returns an error.
pub fn pkg_publish(project: &Path) -> Result<(), String> {
    let manifest = project.join("mako.toml");
    if !manifest.exists() {
        return Err("no mako.toml — run `mako pkg init` first".into());
    }
    let text = fs::read_to_string(&manifest).map_err(|e| format!("read: {e}"))?;
    let (name, version) = read_meta(&text);
    let name = name.ok_or_else(|| "mako.toml missing `name`".to_string())?;
    let version = version.ok_or_else(|| "mako.toml missing `version`".to_string())?;
    if !valid_registry_name(&name) {
        return Err(format!(
            "invalid package name `{name}` for registry publication"
        ));
    }
    if !valid_registry_version(&version) {
        return Err(format!(
            "invalid package version `{version}` for registry publication"
        ));
    }
    let reg = effective_registry(project);
    let package_dir = reg.join(dep_cache_key(&name));
    let dest = package_dir.join(&version);
    if dest.exists() {
        return Err(format!(
            "package `{name}@{version}` is already published; choose a new version"
        ));
    }
    fs::create_dir_all(&package_dir).map_err(|e| format!("mkdir: {e}"))?;
    let staging = package_dir.join(format!(
        ".{version}.publish-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos()
    ));

    match fs::create_dir(&staging) {
        Ok(()) => {}
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
            return Err(format!(
                "a publish of `{name}@{version}` is already in progress; retry"
            ));
        }
        Err(error) => return Err(format!("create publish staging directory: {error}")),
    }
    if let Err(error) = copy_package_for_publish(project, &manifest, &staging) {
        cleanup_publish_staging(&staging);
        return Err(error);
    }
    if dest.exists() {
        cleanup_publish_staging(&staging);
        return Err(format!(
            "package `{name}@{version}` was published concurrently; choose a new version"
        ));
    }
    // Published directories are non-empty, so rename cannot replace one on
    // Windows or Unix. It either installs this staging tree or fails the race.
    fs::rename(&staging, &dest).map_err(|error| {
        cleanup_publish_staging(&staging);
        if error.kind() == std::io::ErrorKind::AlreadyExists || dest.exists() {
            format!("package `{name}@{version}` was published concurrently; choose a new version")
        } else {
            format!("finalize package publication: {error}")
        }
    })?;
    println!("published {name}@{version} → {}", dest.display());
    println!(
        "hint: depend with `\"{name}\" = {{ version = \"^{version}\" }}` then `mako pkg install`"
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;

    #[test]
    fn lockfile_roundtrip() {
        let dir = env::temp_dir().join(format!("mako_pkg_test_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n",
        )
        .unwrap();
        let lock = resolve_graph(&dir, true).unwrap();
        let path = write_lockfile(&dir, &lock).unwrap();
        let back = read_lockfile(&path).unwrap();
        assert_eq!(back.version, LOCKFILE_VERSION);
        assert_eq!(back.packages.len(), 1);
        assert_eq!(back.packages[0].name, "app");
        assert!(back.packages[0]
            .content_hash
            .starts_with(PACKAGE_HASH_PREFIX));
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn package_hash_is_recursive_relocatable_and_excludes_local_state() {
        let dir = env::temp_dir().join(format!("mako_pkg_hash_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let first = dir.join("first");
        let second = dir.join("second");
        for root in [&first, &second] {
            fs::create_dir_all(root.join("src").join("nested")).unwrap();
            fs::write(
                root.join("mako.toml"),
                "name = \"util\"\nversion = \"1.0.0\"\n",
            )
            .unwrap();
            fs::write(
                root.join("src").join("nested").join("math.mko"),
                "fn answer() -> int { 42 }\n",
            )
            .unwrap();
        }

        let initial = hash_path_dep(&first).unwrap();
        assert_eq!(initial, hash_path_dep(&second).unwrap());
        assert_eq!(initial.len(), PACKAGE_HASH_PREFIX.len() + 64);

        fs::create_dir_all(first.join(".mako").join("deps")).unwrap();
        fs::create_dir_all(first.join(".git").join("objects")).unwrap();
        fs::create_dir_all(first.join("target").join("debug")).unwrap();
        fs::write(first.join(".mako").join("deps").join("cache"), "ignored").unwrap();
        fs::write(
            first.join(".git").join("objects").join("generated.mko"),
            "fn ignored_git() -> int { 1 }\n",
        )
        .unwrap();
        fs::write(
            first.join("target").join("debug").join("generated.mko"),
            "fn ignored_target() -> int { 1 }\n",
        )
        .unwrap();
        fs::write(first.join("mako.lock"), "ignored").unwrap();
        fs::write(first.join("NOTES.local.md"), "ignored").unwrap();
        assert_eq!(initial, hash_path_dep(&first).unwrap());

        fs::write(
            first.join("src").join("nested").join("math.mko"),
            "fn answer() -> int { 43 }\n",
        )
        .unwrap();
        assert_ne!(initial, hash_path_dep(&first).unwrap());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn package_hash_wire_format_has_known_answer() {
        let dir = env::temp_dir().join(format!("mako_pkg_hash_golden_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("mako.toml"),
            "name = \"golden\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(dir.join("lib.mko"), "fn answer() -> int { 42 }\n").unwrap();

        assert_eq!(
            hash_path_dep(&dir).unwrap(),
            "sha256:9e7e2239891f3a4b15002916b264cd3fe289610fe2e855f506bf38bd137dfbd3"
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[cfg(unix)]
    #[test]
    fn package_hash_rejects_symbolic_links() {
        use std::os::unix::fs::symlink;

        let dir = env::temp_dir().join(format!("mako_pkg_hash_symlink_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("mako.toml"),
            "name = \"linked\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(dir.join("real.mko"), "fn value() -> int { 1 }\n").unwrap();
        symlink(dir.join("real.mko"), dir.join("alias.mko")).unwrap();

        let error = hash_path_dep(&dir).unwrap_err();
        assert!(error.contains("symbolic links"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn locked_dependency_tampering_is_rejected() {
        let dir = env::temp_dir().join(format!("mako_pkg_tamper_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let util = dir.join("util");
        fs::create_dir_all(&app).unwrap();
        fs::create_dir_all(util.join("src").join("nested")).unwrap();
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { path = \"../util\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        fs::write(
            util.join("mako.toml"),
            "name = \"util\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        let nested = util.join("src").join("nested").join("math.mko");
        fs::write(&nested, "fn answer() -> int { 42 }\n").unwrap();

        let lock = resolve_graph(&app, true).unwrap();
        write_lockfile(&app, &lock).unwrap();
        resolve_graph(&app, false).unwrap();

        fs::write(&nested, "fn answer() -> int { 0 }\n").unwrap();
        let err = resolve_graph(&app, false).unwrap_err();
        assert!(err.contains("integrity mismatch"), "unexpected: {err}");
        assert!(err.contains("mako pkg update"), "unexpected: {err}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn locked_dependency_tampering_blocks_compilation() {
        let dir = env::temp_dir().join(format!("mako_pkg_build_verify_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let util = dir.join("util");
        fs::create_dir_all(&app).unwrap();
        fs::create_dir_all(&util).unwrap();
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { path = \"../util\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        let main = app.join("main.mko");
        fs::write(&main, "fn main() { print_int(util.answer()) }\n").unwrap();
        fs::write(
            util.join("mako.toml"),
            "name = \"util\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        let source = util.join("lib.mko");
        fs::write(&source, "fn answer() -> int { 42 }\n").unwrap();

        let lock = resolve_graph(&app, true).unwrap();
        write_lockfile(&app, &lock).unwrap();
        crate::tooling::check_file(&main).unwrap();

        fs::write(&source, "fn answer() -> int { 0 }\n").unwrap();
        let (ok, report) = crate::tooling::check_file_json_report(&main);
        assert!(!ok);
        assert!(
            report.contains("integrity mismatch"),
            "unexpected: {report}"
        );
        let error = verified_dependency_roots(&app).unwrap_err();
        assert!(error.contains("integrity mismatch"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn locked_transitive_path_is_relative_to_its_manifest() {
        let dir = env::temp_dir().join(format!(
            "mako_pkg_transitive_path_{}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let a = app.join("deps").join("a");
        let declared_b = a.join("deps").join("b");
        let wrong_b = app.join("deps").join("b");
        fs::create_dir_all(&declared_b).unwrap();
        fs::create_dir_all(&wrong_b).unwrap();

        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"a\" = { path = \"deps/a\" }\n",
        )
        .unwrap();
        fs::write(
            a.join("mako.toml"),
            "name = \"a\"\nversion = \"1.0.0\"\n\n[dependencies]\n\"b\" = { path = \"deps/b\" }\n",
        )
        .unwrap();
        fs::write(a.join("lib.mko"), "fn marker() -> int { 1 }\n").unwrap();
        fs::write(
            declared_b.join("mako.toml"),
            "name = \"b\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(
            declared_b.join("lib.mko"),
            "fn answer() -> int { 42 }\n",
        )
        .unwrap();
        fs::write(
            wrong_b.join("mako.toml"),
            "name = \"b\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(wrong_b.join("lib.mko"), "fn unrelated() -> int { 0 }\n").unwrap();
        let main = app.join("main.mko");
        fs::write(&main, "fn main() { print_int(b.answer()) }\n").unwrap();

        let lock = resolve_graph(&app, true).unwrap();
        let locked_b = lock
            .packages
            .iter()
            .find(|package| package.name == "b")
            .unwrap();
        assert_eq!(locked_b.path.as_deref(), Some("deps/a/deps/b"));
        write_lockfile(&app, &lock).unwrap();

        let roots = verified_dependency_roots(&app).unwrap().unwrap();
        assert_eq!(
            roots.get(&a, "b").map(VerifiedPackage::root),
            Some(declared_b.as_path())
        );
        crate::tooling::check_file(&main).unwrap();
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn build_verification_rejects_unlocked_manifest_dependencies() {
        let dir = env::temp_dir().join(format!("mako_pkg_build_lockset_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let util = dir.join("util");
        fs::create_dir_all(&app).unwrap();
        fs::create_dir_all(&util).unwrap();
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { path = \"../util\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        fs::write(
            util.join("mako.toml"),
            "name = \"util\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(util.join("lib.mko"), "fn answer() -> int { 42 }\n").unwrap();

        let mut lock = resolve_graph(&app, true).unwrap();
        lock.packages
            .retain(|package| package.path.as_deref() == Some("."));
        write_lockfile(&app, &lock).unwrap();

        let error = verified_dependency_roots(&app).unwrap_err();
        assert!(
            error.contains("not present in mako.lock"),
            "unexpected: {error}"
        );
        assert!(error.contains("mako pkg update"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn legacy_lockfile_requires_explicit_update() {
        let dir = env::temp_dir().join(format!("mako_pkg_legacy_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n",
        )
        .unwrap();
        fs::write(dir.join("mako.lock"), "version = 1\n").unwrap();

        let err = resolve_graph(&dir, false).unwrap_err();
        assert!(err.contains("version 1"), "unexpected: {err}");
        assert!(err.contains("mako pkg update"), "unexpected: {err}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn malformed_lockfiles_are_rejected() {
        let dir = env::temp_dir().join(format!("mako_pkg_bad_lock_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("mako.lock");
        let valid_hash = format!("sha256:{}", "0".repeat(64));
        let cases = [
            (
                "invalid version",
                "version = 2xyz\n".to_string(),
                "positive integer",
            ),
            (
                "unsupported version",
                "version = 99\n".to_string(),
                "format version 99 is not supported",
            ),
            (
                "missing version",
                format!(
                    r#"[[package]]
name = "app"
version = "1.0.0"
source = "path"
path = "."
content_hash = "{valid_hash}"
"#
                ),
                "missing lockfile format version",
            ),
            (
                "source field mismatch",
                format!(
                    r#"version = 2

[[package]]
name = "app"
version = "1.0.0"
source = "path"
path = "."
git = "https://example.invalid/app.git"
content_hash = "{valid_hash}"
"#
                ),
                "contains git metadata",
            ),
            (
                "bad content hash",
                r#"version = 2

[[package]]
name = "app"
version = "1.0.0"
source = "path"
path = "."
content_hash = "garbage"
"#
                .to_string(),
                "invalid SHA-256",
            ),
            (
                "unquoted field",
                format!(
                    r#"version = 2

[[package]]
name = app
version = "1.0.0"
source = "path"
path = "."
content_hash = "{valid_hash}"
"#
                ),
                "expected a quoted string",
            ),
        ];
        for (case, contents, expected) in cases {
            fs::write(&path, contents).unwrap();
            let error = read_lockfile(&path).unwrap_err();
            assert!(
                error.contains(expected),
                "{case}: expected `{expected}` in `{error}`"
            );
        }
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn lockfile_paths_reject_unrepresentable_characters() {
        for path in ["dep\"quoted", "dep\ncontrol"] {
            let error = normalized_lock_path(Path::new(path)).unwrap_err();
            assert!(
                error.contains("cannot be locked reproducibly"),
                "unexpected: {error}"
            );
        }

        let dir = env::temp_dir().join(format!("mako_pkg_bad_path_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let lock = Lockfile {
            version: LOCKFILE_VERSION,
            packages: vec![LockedPackage {
                name: "app".into(),
                version: "1.0.0".into(),
                source: "path".into(),
                path: Some("dep\"quoted".into()),
                git: None,
                rev: None,
                tag: None,
                branch: None,
                content_hash: format!("sha256:{}", "0".repeat(64)),
            }],
        };
        let error = write_lockfile(&dir, &lock).unwrap_err();
        assert!(error.contains("unsupported quote"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn lock_paths_are_relative_to_the_project() {
        #[cfg(windows)]
        let project = PathBuf::from(r"C:\workspace\app");
        #[cfg(not(windows))]
        let project = PathBuf::from("/workspace/app");

        let cases = [
            (project.clone(), "."),
            (project.join("deps/a/deps/b"), "deps/a/deps/b"),
            (project.join("deps/../util"), "util"),
            (project.parent().unwrap().join("util"), "../util"),
        ];
        for (target, expected) in cases {
            assert_eq!(lock_path_for(&project, &target).unwrap(), expected);
        }
    }

    #[cfg(windows)]
    #[test]
    fn lock_paths_reject_targets_on_another_drive() {
        let error = project_relative_path(Path::new(r"C:\workspace\app"), Path::new(r"D:\util"))
            .unwrap_err();
        assert!(
            error.contains("cannot be expressed relative"),
            "unexpected: {error}"
        );
    }

    #[test]
    fn unreadable_transitive_manifest_is_a_hard_error() {
        let dir = env::temp_dir().join(format!("mako_pkg_manifest_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let mid = dir.join("mid");
        let leaf = dir.join("leaf");
        fs::create_dir_all(&app).unwrap();
        fs::create_dir_all(&mid).unwrap();
        fs::create_dir_all(&leaf).unwrap();
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"mid\" = { path = \"../mid\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        fs::write(
            mid.join("mako.toml"),
            "name = \"mid\"\nversion = \"1.0.0\"\n\n[dependencies]\n\"leaf\" = { path = \"../leaf\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        fs::write(mid.join("lib.mko"), "fn mid() -> int { 1 }\n").unwrap();
        fs::write(
            leaf.join("mako.toml"),
            "name = \"leaf\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(leaf.join("lib.mko"), "fn leaf() -> int { 1 }\n").unwrap();

        let mut lock = resolve_graph(&app, true).unwrap();
        fs::write(mid.join("mako.toml"), [0xff, 0xfe, 0xfd]).unwrap();

        let update_error = resolve_graph(&app, true).unwrap_err();
        assert!(
            update_error.contains("read dependency manifest"),
            "unexpected: {update_error}"
        );

        let mid_lock = lock
            .packages
            .iter_mut()
            .find(|package| package.name == "mid")
            .unwrap();
        mid_lock.content_hash = hash_path_dep(&mid).unwrap();
        write_lockfile(&app, &lock).unwrap();
        let install_error = resolve_graph(&app, false).unwrap_err();
        assert!(
            install_error.contains("read dependency manifest"),
            "unexpected: {install_error}"
        );
        assert!(
            install_error.contains("mako.toml"),
            "unexpected: {install_error}"
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn locked_dependency_source_change_requires_update() {
        let dir = env::temp_dir().join(format!("mako_pkg_source_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let app = dir.join("app");
        let first = dir.join("first");
        let second = dir.join("second");
        fs::create_dir_all(&app).unwrap();
        for dependency in [&first, &second] {
            fs::create_dir_all(dependency).unwrap();
            fs::write(
                dependency.join("mako.toml"),
                "name = \"util\"\nversion = \"1.0.0\"\n",
            )
            .unwrap();
            fs::write(dependency.join("lib.mko"), "fn value() -> int { 1 }\n").unwrap();
        }
        let manifest = app.join("mako.toml");
        fs::write(
            &manifest,
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { path = \"../first\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        let lock = resolve_graph(&app, true).unwrap();
        write_lockfile(&app, &lock).unwrap();

        fs::write(
            &manifest,
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { path = \"../second\", version = \"1.0.0\" }\n",
        )
        .unwrap();
        let err = resolve_graph(&app, false).unwrap_err();
        assert!(err.contains("source"), "unexpected: {err}");
        assert!(err.contains("mako pkg update"), "unexpected: {err}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn published_versions_are_immutable() {
        let dir =
            env::temp_dir().join(format!("mako_pkg_immutable_publish_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let package = dir.join("lib");
        fs::create_dir_all(&package).unwrap();
        fs::write(
            package.join("mako.toml"),
            "name = \"util\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        let source = package.join("lib.mko");
        fs::write(&source, "fn answer() -> int { 42 }\n").unwrap();
        fs::create_dir_all(package.join("src").join("nested")).unwrap();
        fs::write(
            package.join("src").join("nested").join("feature.mko"),
            "fn feature() -> int { 7 }\n",
        )
        .unwrap();
        fs::create_dir_all(package.join("lib")).unwrap();
        fs::write(
            package.join("lib").join("module.mko"),
            "fn module() -> int { 8 }\n",
        )
        .unwrap();
        fs::write(package.join(".secret.mko"), "fn secret() {}\n").unwrap();
        fs::write(package.join("mako.lock"), "local state\n").unwrap();
        fs::create_dir_all(package.join(".mako")).unwrap();
        fs::write(package.join(".mako").join("cache"), "local state\n").unwrap();

        pkg_publish(&package).unwrap();
        let published = effective_registry(&package).join("util").join("1.0.0");
        assert_eq!(
            fs::read_to_string(published.join("mako.toml")).unwrap(),
            "name = \"util\"\nversion = \"1.0.0\"\n"
        );
        assert_eq!(
            fs::read_to_string(published.join("lib.mko")).unwrap(),
            "fn answer() -> int { 42 }\n"
        );
        assert!(published.join("src/nested/feature.mko").is_file());
        assert!(published.join("lib/module.mko").is_file());
        assert!(!published.join(".secret.mko").exists());
        assert!(!published.join("mako.lock").exists());
        assert!(!published.join(".mako").exists());

        fs::write(&source, "fn answer() -> int { 0 }\n").unwrap();
        let error = pkg_publish(&package).unwrap_err();
        assert!(error.contains("already published"), "unexpected: {error}");
        assert_eq!(
            fs::read_to_string(published.join("lib.mko")).unwrap(),
            "fn answer() -> int { 42 }\n"
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn publish_rejects_unsafe_registry_coordinates() {
        let dir = env::temp_dir().join(format!(
            "mako_pkg_publish_coordinates_{}",
            std::process::id()
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("lib.mko"), "fn answer() -> int { 42 }\n").unwrap();

        let cases = [
            ("../escape", "1.0.0", "invalid package name"),
            ("scope//util", "1.0.0", "invalid package name"),
            ("scope\\util", "1.0.0", "invalid package name"),
            (".hidden", "1.0.0", "invalid package name"),
            ("scope/.hidden", "1.0.0", "invalid package name"),
            ("util", "../1.0.0", "invalid package version"),
            ("util", "1/0", "invalid package version"),
            ("util", "1\\0", "invalid package version"),
            ("util", ".1.0.0", "invalid package version"),
        ];
        for (name, version, expected) in cases {
            fs::write(
                dir.join("mako.toml"),
                format!("name = \"{name}\"\nversion = \"{version}\"\n"),
            )
            .unwrap();
            let error = pkg_publish(&dir).unwrap_err();
            assert!(
                error.contains(expected),
                "{name}@{version}: unexpected: {error}"
            );
        }
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn scoped_registry_names_are_disjoint_and_resolve() {
        let dir = env::temp_dir().join(format!("mako_pkg_scoped_publish_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let manifest = dir.join("mako.toml");
        let source = dir.join("lib.mko");

        fs::write(&manifest, "name = \"foo\"\nversion = \"1.0.0\"\n").unwrap();
        fs::write(&source, "fn plain() -> int { 1 }\n").unwrap();
        pkg_publish(&dir).unwrap();

        fs::write(
            &manifest,
            "name = \"foo/1.0.0\"\nversion = \"2.0.0+linux\"\n",
        )
        .unwrap();
        fs::write(&source, "fn scoped() -> int { 2 }\n").unwrap();
        pkg_publish(&dir).unwrap();

        let registry = effective_registry(&dir);
        let plain = registry.join("foo").join("1.0.0");
        let scoped = registry.join("foo!1.0.0").join("2.0.0+linux");
        assert_eq!(
            fs::read_to_string(plain.join("lib.mko")).unwrap(),
            "fn plain() -> int { 1 }\n"
        );
        assert!(!plain.join("2.0.0+linux").exists());
        assert_eq!(
            registry_resolve_in(&registry, "foo/1.0.0", "^2.0.0")
                .unwrap()
                .0,
            scoped
        );
        assert_eq!(
            crate::tooling::registry_resolve(&dir, "foo/1.0.0", "^2.0.0").unwrap(),
            scoped
        );

        let app = dir.join("app");
        fs::create_dir_all(&app).unwrap();
        copy_dir_recursive(&registry, &app.join(".mako").join("registry")).unwrap();
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"foo/1.0.0\" = { version = \"^2.0.0\" }\n",
        )
        .unwrap();
        let lock = resolve_graph(&app, true).unwrap();
        let package = lock
            .packages
            .iter()
            .find(|package| package.name == "foo/1.0.0")
            .unwrap();
        assert_eq!(package.version, "2.0.0+linux");
        assert_eq!(package.path.as_deref(), Some(".mako/deps/foo!1.0.0"));
        let _ = fs::remove_dir_all(&dir);
    }

    #[cfg(unix)]
    #[test]
    fn publish_rejects_symlinked_sources() {
        use std::os::unix::fs::symlink;

        let dir = env::temp_dir().join(format!("mako_pkg_publish_symlink_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(dir.join("src")).unwrap();
        fs::write(
            dir.join("mako.toml"),
            "name = \"util\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        let outside = dir.join("outside.mko");
        fs::write(&outside, "fn outside() {}\n").unwrap();
        symlink(&outside, dir.join("src").join("linked.mko")).unwrap();

        let error = pkg_publish(&dir).unwrap_err();
        assert!(error.contains("symlink"), "unexpected: {error}");
        let package_dir = effective_registry(&dir).join("util");
        assert!(!package_dir.join("1.0.0").exists());
        let has_staging = fs::read_dir(package_dir).unwrap().any(|entry| {
            entry
                .unwrap()
                .file_name()
                .to_string_lossy()
                .contains(".publish-")
        });
        assert!(!has_staging);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn registry_publish_and_resolve() {
        let dir = env::temp_dir().join(format!("mako_pkg_reg_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let lib = dir.join("lib");
        let app = dir.join("app");
        fs::create_dir_all(&lib).unwrap();
        fs::create_dir_all(&app).unwrap();
        fs::write(
            lib.join("mako.toml"),
            "name = \"util\"\nversion = \"1.2.0\"\n",
        )
        .unwrap();
        fs::write(
            lib.join("lib.mko"),
            "fn add(a: int, b: int) -> int { a + b }\n",
        )
        .unwrap();
        pkg_publish(&lib).unwrap();
        // Point app registry at lib's published tree via MAKO_REGISTRY
        let reg = effective_registry(&lib);
        fs::write(
            app.join("mako.toml"),
            "name = \"app\"\nversion = \"0.1.0\"\n\n[dependencies]\n\"util\" = { version = \"^1.0.0\" }\n",
        )
        .unwrap();
        let main = app.join("main.mko");
        fs::write(&main, "fn main() { print_int(util.add(2, 3)) }\n").unwrap();
        // Copy registry into app or set env — copy for isolation
        let app_reg = app.join(".mako").join("registry");
        copy_dir_recursive(&reg, &app_reg).unwrap();
        let lock = resolve_graph(&app, true).unwrap();
        assert!(lock
            .packages
            .iter()
            .any(|p| p.name == "util" && p.version.starts_with("1.")));
        let util = lock.packages.iter().find(|p| p.name == "util").unwrap();
        assert_eq!(util.path.as_deref(), Some(".mako/deps/util"));
        write_lockfile(&app, &lock).unwrap();
        resolve_graph(&app, false).unwrap();
        fs::write(
            app_reg.join("util").join("1.2.0").join("lib.mko"),
            "fn broken(",
        )
        .unwrap();
        crate::tooling::check_file(&main).unwrap();
        fs::write(
            app.join(".mako").join("deps").join("util").join("lib.mko"),
            "fn add(a: int, b: int) -> int { 0 }\n",
        )
        .unwrap();
        let error = resolve_graph(&app, false).unwrap_err();
        assert!(error.contains("integrity mismatch"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn offline_git_requires_cached_dep() {
        let dir = env::temp_dir().join(format!("mako_pkg_offline_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("mako.toml"),
            r#"name = "app"
version = "0.1.0"

[dependencies]
"util" = { git = "https://example.invalid/util.git", version = "1.2.0" }
"#,
        )
        .unwrap();

        let err = resolve_graph_with_options(&dir, true, true).unwrap_err();
        assert!(err.contains("offline mode"), "unexpected: {err}");

        let cached = dir.join(".mako").join("deps").join("util");
        fs::create_dir_all(&cached).unwrap();
        fs::write(
            cached.join("mako.toml"),
            "name = \"util\"\nversion = \"1.2.0\"\n",
        )
        .unwrap();
        fs::write(cached.join("lib.mko"), "fn ok() -> int { 1 }\n").unwrap();
        let main = dir.join("main.mko");
        fs::write(&main, "fn main() { print_int(util.ok()) }\n").unwrap();

        let lock = resolve_graph_with_options(&dir, true, true).unwrap();
        assert!(lock
            .packages
            .iter()
            .any(|p| p.name == "util" && p.source == "git" && p.version == "1.2.0"));
        write_lockfile(&dir, &lock).unwrap();
        resolve_graph_with_options(&dir, false, true).unwrap();
        crate::tooling::check_file(&main).unwrap();
        fs::write(cached.join("lib.mko"), "fn ok() -> int { 0 }\n").unwrap();
        let (ok, build_error) = crate::tooling::check_file_json_report(&main);
        assert!(!ok);
        assert!(
            build_error.contains("integrity mismatch"),
            "unexpected: {build_error}"
        );
        let error = resolve_graph_with_options(&dir, false, true).unwrap_err();
        assert!(error.contains("integrity mismatch"), "unexpected: {error}");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn semver_conflict_detected() {
        let dir = env::temp_dir().join(format!("mako_pkg_conf_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let a = dir.join("a");
        let b = dir.join("b");
        let app = dir.join("app");
        fs::create_dir_all(&a).unwrap();
        fs::create_dir_all(&b).unwrap();
        fs::create_dir_all(&app).unwrap();
        fs::write(
            a.join("mako.toml"),
            "name = \"leaf\"\nversion = \"1.0.0\"\n",
        )
        .unwrap();
        fs::write(
            b.join("mako.toml"),
            "name = \"leaf\"\nversion = \"2.0.0\"\n",
        )
        .unwrap();
        fs::write(
            app.join("mako.toml"),
            r#"name = "app"
version = "0.1.0"

[dependencies]
"x" = { path = "../a", version = "1.0.0" }
"y" = { path = "../b", version = "2.0.0" }
"#,
        )
        .unwrap();
        // Different package names x/y — no conflict. Conflict needs same name.
        fs::write(
            app.join("mako.toml"),
            r#"name = "app"
version = "0.1.0"

[dependencies]
"leaf" = { path = "../a", version = "^1.0.0" }
"#,
        )
        .unwrap();
        // Add a second manifest that also wants leaf@2 via a mid package
        let mid = dir.join("mid");
        fs::create_dir_all(&mid).unwrap();
        fs::write(
            mid.join("mako.toml"),
            r#"name = "mid"
version = "0.1.0"

[dependencies]
"leaf" = { path = "../b", version = "^2.0.0" }
"#,
        )
        .unwrap();
        fs::write(
            app.join("mako.toml"),
            r#"name = "app"
version = "0.1.0"

[dependencies]
"leaf" = { path = "../a", version = "^1.0.0" }
"mid" = { path = "../mid", version = "0.1.0" }
"#,
        )
        .unwrap();
        let err = resolve_graph(&app, true).unwrap_err();
        assert!(
            err.contains("conflict") || err.contains("does not satisfy"),
            "unexpected: {err}"
        );
        let _ = fs::remove_dir_all(&dir);
    }
}
