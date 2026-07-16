//! Real-ish tooling: fmt, test, lint, bench, doc, deploy.

use crate::ast::*;
use crate::desugar;
use crate::diag::{json_escape, Diagnostic, Span};
use crate::lexer::{LexError, Lexer};
use crate::parser::{ParseError, Parser};
use crate::types::{TypeChecker, TypeError};
use crate::{DeployDockerMode, DeployPluginKind, DeployServerlessProvider};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Instant;

/// True for Mako sources (unique extension `.mko` — not Python Mako / Make `.mk`).
pub fn is_mko_source(path: &Path) -> bool {
    path.extension().and_then(|e| e.to_str()) == Some("mko")
}

pub fn collect_mako_files(path: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if path.is_file() {
        if is_mko_source(path) {
            out.push(path.to_path_buf());
        }
        return out;
    }
    fn collect_into(dir: &Path, out: &mut Vec<PathBuf>) {
        let Ok(rd) = fs::read_dir(dir) else { return };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
                if name == "target" || name.starts_with('.') {
                    continue;
                }
                collect_into(&p, out);
            } else if is_mko_source(&p) {
                out.push(p);
            }
        }
    }
    collect_into(path, &mut out);
    out.sort();
    out
}

pub fn parse_file(path: &Path) -> Result<Program, String> {
    let src = fs::read_to_string(path).map_err(|e| e.to_string())?;
    let tokens = Lexer::new(&src).tokenize().map_err(|e| format!("{e}"))?;
    let program = Parser::new(tokens).parse().map_err(|e| format!("{e}"))?;
    Ok(desugar::desugar(program))
}

pub fn check_file(path: &Path) -> Result<Program, ()> {
    let path_s = path.display().to_string();
    let src = fs::read_to_string(path).map_err(|e| {
        Diagnostic::error(&path_s, "", Span::unknown(), format!("cannot read: {e}")).emit();
    })?;
    let tokens = Lexer::new(&src).tokenize().map_err(|e| {
        Diagnostic::error(&path_s, &src, Span::unknown(), format!("{e}")).emit();
    })?;
    let program = Parser::new(tokens).parse().map_err(|e| {
        Diagnostic::error(&path_s, &src, Span::unknown(), format!("{e}")).emit();
    })?;
    let program = desugar::desugar(program);
    let program = resolve_imports(path, program).map_err(|e| {
        Diagnostic::error(&path_s, &src, Span::unknown(), e).emit();
    })?;
    // Package-per-directory: merge sibling units (tests and normal packages).
    let program = merge_package_dir_siblings(path, program).map_err(|e| {
        Diagnostic::error(&path_s, &src, Span::unknown(), e).emit();
    })?;
    let mut program = merge_path_dependencies(path, program).map_err(|e| {
        Diagnostic::error(&path_s, &src, Span::unknown(), e).emit();
    })?;
    if is_test_file(path) {
        program
            .items
            .retain(|item| !matches!(item, Item::Fn(f) if f.name == "main"));
    }
    let mut tc = TypeChecker::new();
    if let Some(dir) = find_nearest_manifest_dir(path) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = fs::read_to_string(&manifest) {
            apply_package_safety_flags(&mut tc, &text);
        }
    }
    tc.check(&program).map_err(|e| {
        let crate::types::TypeError::At {
            message,
            hint,
            line,
            col,
        } = e;
        let span = if line > 0 {
            Span::new(line, col)
        } else {
            Span::unknown()
        };
        let mut d = Diagnostic::error(&path_s, &src, span, message);
        if let Some(h) = hint {
            d = d.with_hint(h);
        }
        d.emit();
    })?;
    for f in tc.mono_fns {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(x) if x.name == f.name))
        {
            program.items.push(Item::Fn(f));
        }
    }
    for s in tc.mono_structs {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Struct(x) if x.name == s.name))
        {
            program.items.push(Item::Struct(s));
        }
    }
    for e in tc.mono_enums {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Enum(x) if x.name == e.name))
        {
            program.items.push(Item::Enum(e));
        }
    }
    Ok(program)
}

pub fn check_file_json_report(path: &Path) -> (bool, String) {
    match check_file_structured(path) {
        Ok(program) => {
            let symbols = top_level_symbol_count(&program);
            (
                true,
                format!(
                    r#"{{"ok":true,"file":"{}","diagnostics":[],"symbols":{symbols}}}"#,
                    json_escape(&path.display().to_string())
                ),
            )
        }
        Err(diagnostic) => {
            let file = path.display().to_string();
            (
                false,
                format!(
                    r#"{{"ok":false,"file":"{}","diagnostics":[{}]}}"#,
                    json_escape(&file),
                    diagnostic.to_json()
                ),
            )
        }
    }
}

fn check_file_structured(path: &Path) -> Result<Program, Diagnostic> {
    let path_s = path.display().to_string();
    let src = fs::read_to_string(path).map_err(|e| {
        Diagnostic::error(&path_s, "", Span::unknown(), format!("cannot read: {e}"))
    })?;
    let tokens = Lexer::new(&src)
        .tokenize()
        .map_err(|e| diagnostic_from_lex_error(&path_s, &src, e))?;
    let program = Parser::new(tokens)
        .parse()
        .map_err(|e| diagnostic_from_parse_error(&path_s, &src, e))?;
    let program = desugar::desugar(program);
    let program = resolve_imports(path, program)
        .map_err(|e| Diagnostic::error(&path_s, &src, Span::unknown(), e))?;
    let program = merge_package_dir_siblings(path, program)
        .map_err(|e| Diagnostic::error(&path_s, &src, Span::unknown(), e))?;
    let mut program = merge_path_dependencies(path, program)
        .map_err(|e| Diagnostic::error(&path_s, &src, Span::unknown(), e))?;
    if is_test_file(path) {
        program
            .items
            .retain(|item| !matches!(item, Item::Fn(f) if f.name == "main"));
    }
    let mut tc = TypeChecker::new();
    if let Some(dir) = find_nearest_manifest_dir(path) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = fs::read_to_string(&manifest) {
            apply_package_safety_flags(&mut tc, &text);
        }
    }
    tc.check(&program)
        .map_err(|e| diagnostic_from_type_error(&path_s, &src, e))?;
    for f in tc.mono_fns {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(x) if x.name == f.name))
        {
            program.items.push(Item::Fn(f));
        }
    }
    for s in tc.mono_structs {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Struct(x) if x.name == s.name))
        {
            program.items.push(Item::Struct(s));
        }
    }
    for e in tc.mono_enums {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Enum(x) if x.name == e.name))
        {
            program.items.push(Item::Enum(e));
        }
    }
    Ok(program)
}

fn diagnostic_from_lex_error(file: &str, src: &str, err: LexError) -> Diagnostic {
    let (line, col) = match &err {
        LexError::UnexpectedChar(_, line, col)
        | LexError::UnterminatedString(line, col)
        | LexError::ColoredAsync(line, col) => (line, col),
        LexError::NumberOutOfRange { line, col, .. } => (line, col),
    };
    Diagnostic::error(file, src, Span::new(*line, *col), format!("{err}"))
}

fn diagnostic_from_parse_error(file: &str, src: &str, err: ParseError) -> Diagnostic {
    let ParseError::Message { line, col, .. } = &err;
    Diagnostic::error(file, src, Span::new(*line, *col), format!("{err}"))
}

fn diagnostic_from_type_error(file: &str, src: &str, err: TypeError) -> Diagnostic {
    let TypeError::At {
        message,
        hint,
        line,
        col,
    } = err;
    let span = if line > 0 {
        Span::new(line, col)
    } else {
        Span::unknown()
    };
    let mut diagnostic = Diagnostic::error(file, src, span, message);
    if let Some(h) = hint {
        diagnostic = diagnostic.with_hint(h);
    }
    diagnostic
}

fn top_level_symbol_count(program: &Program) -> usize {
    program
        .items
        .iter()
        .filter(|item| {
            matches!(
                item,
                Item::Fn(_)
                    | Item::Struct(_)
                    | Item::Enum(_)
                    | Item::Actor(_)
                    | Item::Interface(_)
                    | Item::ExternC(_)
                    | Item::Const(_)
                    | Item::On(_)
                    | Item::Package { .. }
            )
        })
        .count()
}

/// Apply `[package] systems = true` / `gc = true` / visibility / profile from mako.toml.
pub fn apply_package_safety_flags(tc: &mut TypeChecker, toml: &str) {
    let mut in_package = false;
    let mut in_profile_release = false;
    for line in toml.lines() {
        let t = line.trim();
        if t.starts_with('#') || t.is_empty() {
            continue;
        }
        if t == "[package]" {
            in_package = true;
            in_profile_release = false;
            continue;
        }
        if t == "[profile.release]" {
            in_package = false;
            in_profile_release = true;
            continue;
        }
        if t.starts_with('[') {
            in_package = false;
            in_profile_release = false;
            continue;
        }
        if in_package {
            if t.starts_with("systems") && t.contains('=') {
                let v = t.split('=').nth(1).unwrap_or("").trim();
                tc.systems_crate = v == "true" || v == "1";
            }
            if (t.starts_with("gc") || t.starts_with("optional_gc")) && t.contains('=') {
                let v = t.split('=').nth(1).unwrap_or("").trim();
                tc.gc_enabled = v == "true" || v == "1";
            }
            if t.starts_with("visibility") && t.contains('=') {
                let v = t
                    .split('=')
                    .nth(1)
                    .unwrap_or("")
                    .trim()
                    .trim_matches('"')
                    .trim_matches('\'');
                tc.explicit_visibility = v == "explicit";
            }
        }
        if in_profile_release && t.starts_with("bounds_checks") && t.contains('=') {
            let v = t
                .split('=')
                .nth(1)
                .unwrap_or("")
                .trim()
                .trim_matches('"')
                .trim_matches('\'');
            // "on" | "always" → keep checks even under NDEBUG; default/debug_only = current
            tc.bounds_checks_always = v == "on" || v == "always" || v == "true" || v == "1";
        }
    }
    // Systems crates never allow GC to weaken ownership.
    if tc.systems_crate {
        tc.gc_enabled = false;
    }
}

/// Release/profile flags for codegen (bounds checks, source maps, optional GC).
#[derive(Clone, Debug, Default)]
pub struct CodegenProfile {
    pub bounds_checks_always: bool,
    pub gc_enabled: bool,
    pub source_file: Option<String>,
}

pub fn codegen_profile_from_toml(toml: &str, source_file: Option<String>) -> CodegenProfile {
    let mut p = CodegenProfile {
        bounds_checks_always: false,
        gc_enabled: false,
        source_file,
    };
    let mut in_package = false;
    let mut in_profile_release = false;
    let mut systems = false;
    for line in toml.lines() {
        let t = line.trim();
        if t.starts_with('#') || t.is_empty() {
            continue;
        }
        if t == "[package]" {
            in_package = true;
            in_profile_release = false;
            continue;
        }
        if t == "[profile.release]" {
            in_package = false;
            in_profile_release = true;
            continue;
        }
        if t.starts_with('[') {
            in_package = false;
            in_profile_release = false;
            continue;
        }
        if in_package {
            if t.starts_with("systems") && t.contains('=') {
                let v = t.split('=').nth(1).unwrap_or("").trim();
                systems = v == "true" || v == "1";
            }
            if (t.starts_with("gc") || t.starts_with("optional_gc")) && t.contains('=') {
                let v = t.split('=').nth(1).unwrap_or("").trim();
                p.gc_enabled = v == "true" || v == "1";
            }
        }
        if in_profile_release && t.starts_with("bounds_checks") && t.contains('=') {
            let v = t
                .split('=')
                .nth(1)
                .unwrap_or("")
                .trim()
                .trim_matches('"')
                .trim_matches('\'');
            p.bounds_checks_always = v == "on" || v == "always" || v == "true" || v == "1";
        }
    }
    if systems {
        p.gc_enabled = false;
    }
    p
}

/// Canonical pretty-printer — see `crate::fmt`.
#[allow(unused_imports)]
pub use crate::fmt::format_program;

#[derive(Clone, Copy, Debug, Default)]
pub struct FmtOptions {
    pub write: bool,
    pub list: bool,
    pub diff: bool,
}

pub fn run_fmt(path: &Path, opts: FmtOptions) -> Result<(), ()> {
    let files = collect_mako_files(path);
    if files.is_empty() {
        eprintln!("mako fmt: no .mko files under {}", path.display());
        return Err(());
    }
    let multi = files.len() > 1 || path.is_dir();
    for f in &files {
        let original = fs::read_to_string(f).map_err(|e| {
            eprintln!("mako fmt: read {}: {e}", f.display());
        })?;
        let program = parse_file(f).map_err(|e| {
            eprintln!("mako fmt: parse {}: {e}", f.display());
        })?;
        let formatted = crate::fmt::format_program(&program);
        if formatted == original {
            continue;
        }
        let display = f.display().to_string();
        if opts.list {
            println!("{display}");
        } else if opts.diff {
            print!(
                "{}",
                crate::fmt::unified_diff(&display, &original, &formatted)
            );
        } else if opts.write {
            fs::write(f, &formatted).map_err(|e| {
                eprintln!("mako fmt: write {}: {e}", f.display());
            })?;
        } else {
            if multi {
                println!("// {display}");
            }
            print!("{formatted}");
        }
    }
    Ok(())
}

fn fmt_type(t: &TypeExpr) -> String {
    format!("{t}")
}

pub fn run_lint(path: &Path, identity: bool) -> Result<(), ()> {
    let files = collect_mako_files(path);
    let mut issues = 0;
    for f in &files {
        let Ok(program) = check_file(f) else {
            issues += 1;
            continue;
        };
        // Rule: empty function body
        for item in &program.items {
            if let Item::Fn(fn_def) = item {
                if fn_def.body.stmts.is_empty() && fn_def.name != "main" {
                    eprintln!("lint: {}: empty function `{}`", f.display(), fn_def.name);
                    issues += 1;
                }
                // Rule: main should exist in examples / apps — soft
                if fn_def.name.starts_with("test_") && fn_def.ret.is_none() {
                    eprintln!(
                        "lint: {}: test `{}` should return int (0 = pass)",
                        f.display(),
                        fn_def.name
                    );
                    issues += 1;
                }
            }
        }
        let has_main = program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(f) if f.name == "main"));
        let is_test = f
            .file_name()
            .and_then(|n| n.to_str())
            .map(|n| n.contains("test"))
            .unwrap_or(false);
        if !has_main && !is_test {
            // only warn for single-file apps that look like programs
            if matches!(f.file_name().and_then(|n| n.to_str()), Some("main.mko")) {
                eprintln!("lint: {}: missing main", f.display());
                issues += 1;
            }
        }
        if identity {
            issues += lint_identity_surface(f);
        }
    }
    if issues == 0 {
        if identity {
            println!(
                "mako lint: ok ({} file(s), identity surface clean)",
                files.len()
            );
        } else {
            println!("mako lint: ok ({} file(s))", files.len());
        }
        Ok(())
    } else {
        eprintln!("mako lint: {issues} issue(s)");
        Err(())
    }
}

/// Style-only: dual spellings are valid, but preferred surface is Mako flair.
/// See docs/IDENTITY.md — does not fail typecheck.
fn lint_identity_surface(path: &Path) -> usize {
    let Ok(src) = fs::read_to_string(path) else {
        return 0;
    };
    let mut n = 0;
    for (i, line) in src.lines().enumerate() {
        let line_no = i + 1;
        let trimmed = line.trim();
        if trimmed.starts_with("//") {
            continue;
        }
        // Strip // comments for scanning
        let code = trimmed.split("//").next().unwrap_or(trimmed);
        let checks: &[(&str, &str)] = &[
            ("func ", "prefer `fn` (Mako) over dual `func`"),
            ("\tfunc ", "prefer `fn` (Mako) over dual `func`"),
            ("func(", "prefer `fn` (Mako) over dual `func`"),
            (" var ", "prefer `let mut` (Mako) over dual `var`"),
            ("\tvar ", "prefer `let mut` (Mako) over dual `var`"),
            ("package ", "prefer `pack` (Mako) over dual `package`"),
            ("type ", "prefer `struct`/`enum` (Mako) over dual `type … struct`"),
            (":=", "prefer `let` / `let mut` (Mako) over dual `:="),
        ];
        for (pat, msg) in checks {
            if code.contains(pat) {
                // Avoid `typeof` / false positives for type in comments already stripped
                if *pat == "type " && !code.contains("struct") && !code.contains("enum") {
                    continue;
                }
                if *pat == " var " || *pat == "\tvar " {
                    // skip `variant` etc.
                    if !code.split_whitespace().any(|w| w == "var" || w.starts_with("var "))
                        && !code.contains(" var ")
                        && !code.starts_with("var ")
                    {
                        continue;
                    }
                }
                eprintln!(
                    "lint(identity): {}:{}: {msg}",
                    path.display(),
                    line_no
                );
                n += 1;
                break; // one identity hit per line
            }
        }
        // import vs pull (prefer pull)
        if code.starts_with("import ") || code.starts_with("import(") || code.contains("\nimport ") {
            if code.trim_start().starts_with("import") {
                eprintln!(
                    "lint(identity): {}:{}: prefer `pull` (Mako) over dual `import`",
                    path.display(),
                    line_no
                );
                n += 1;
            }
        }
    }
    n
}

pub fn run_doc(path: &Path, out_dir: &Path) -> Result<(), ()> {
    let files = collect_mako_files(path);
    if files.is_empty() {
        eprintln!("mako doc: no .mko files under {}", path.display());
        return Err(());
    }
    fs::create_dir_all(out_dir).map_err(|e| {
        eprintln!("mako doc: {e}");
    })?;
    let mut index = String::from("# Mako API\n\n- [Runnable examples](examples.md)\n- [Search index](search-index.json)\n\n## Modules\n\n");
    let mut examples = String::from("# Runnable examples\n\n");
    let mut search = Vec::new();
    for f in &files {
        let program = parse_file(f).map_err(|e| {
            eprintln!("mako doc: {}: {e}", f.display());
        })?;
        let stem = f.file_stem().and_then(|s| s.to_str()).unwrap_or("mod");
        let mut md = format!("# {stem}\n\nSource: `{}`\n\n", f.display());
        let has_main = program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(fn_def) if fn_def.name == "main"));
        let has_tests = program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(fn_def) if fn_def.name.starts_with("Test")));
        if has_main || has_tests {
            md.push_str("## Runnable example\n\n```bash\n");
            md.push_str("mako check ");
            md.push_str(&f.display().to_string());
            md.push('\n');
            if has_main {
                md.push_str("mako run ");
                md.push_str(&f.display().to_string());
                md.push('\n');
            }
            if has_tests {
                md.push_str("mako test ");
                md.push_str(&f.display().to_string());
                md.push('\n');
            }
            md.push_str("```\n\n");
            examples.push_str(&format!("## `{}`\n\n```bash\n", f.display()));
            examples.push_str(&format!("mako check {}\n", f.display()));
            if has_main {
                examples.push_str(&format!("mako run {}\n", f.display()));
            }
            if has_tests {
                examples.push_str(&format!("mako test {}\n", f.display()));
            }
            examples.push_str("```\n\n");
        }
        for item in &program.items {
            match item {
                Item::Fn(fn_def) => {
                    search.push(doc_search_item(
                        "fn",
                        &fn_def.name,
                        f,
                        &format!("{stem}.md"),
                        &fn_signature(fn_def),
                    ));
                    md.push_str(&format!("## `fn {}`\n\n", fn_def.name));
                    md.push_str("```mako\n");
                    md.push_str("fn ");
                    md.push_str(&fn_def.name);
                    md.push('(');
                    for (i, p) in fn_def.params.iter().enumerate() {
                        if i > 0 {
                            md.push_str(", ");
                        }
                        md.push_str(&p.name);
                        md.push_str(": ");
                        md.push_str(&fmt_type(&p.ty));
                    }
                    md.push(')');
                    if let Some(ret) = &fn_def.ret {
                        md.push_str(" -> ");
                        md.push_str(&fmt_type(ret));
                    }
                    md.push_str("\n```\n\n");
                }
                Item::Struct(s) => {
                    search.push(doc_search_item(
                        "struct",
                        &s.name,
                        f,
                        &format!("{stem}.md"),
                        &format!("struct {}", s.name),
                    ));
                    md.push_str(&format!("## `struct {}`\n\n", s.name));
                    md.push_str("```mako\nstruct ");
                    md.push_str(&s.name);
                    md.push_str(" {\n");
                    for (n, ty, _) in &s.fields {
                        md.push_str("    ");
                        md.push_str(n);
                        md.push_str(": ");
                        md.push_str(&fmt_type(ty));
                        md.push('\n');
                    }
                    md.push_str("}\n```\n\n");
                }
                Item::Enum(e) => {
                    search.push(doc_search_item(
                        "enum",
                        &e.name,
                        f,
                        &format!("{stem}.md"),
                        &format!("enum {}", e.name),
                    ));
                    md.push_str(&format!("## `enum {}`\n\n", e.name));
                    md.push_str("```mako\nenum ");
                    md.push_str(&e.name);
                    md.push_str(" {\n");
                    for v in &e.variants {
                        md.push_str("    ");
                        md.push_str(&v.name);
                        if !v.fields.is_empty() {
                            md.push('(');
                            for (i, f) in v.fields.iter().enumerate() {
                                if i > 0 {
                                    md.push_str(", ");
                                }
                                md.push_str(&fmt_type(f));
                            }
                            md.push(')');
                        }
                        md.push('\n');
                    }
                    md.push_str("}\n```\n\n");
                }
                Item::Interface(iface) => {
                    search.push(doc_search_item(
                        "interface",
                        &iface.name,
                        f,
                        &format!("{stem}.md"),
                        &format!("interface {}", iface.name),
                    ));
                    md.push_str(&format!("## `interface {}`\n\n", iface.name));
                    md.push_str("```mako\ninterface ");
                    md.push_str(&iface.name);
                    md.push_str(" {\n");
                    for (m, params, ret) in &iface.methods {
                        md.push_str("    fn ");
                        md.push_str(m);
                        md.push('(');
                        for (i, p) in params.iter().enumerate() {
                            if i > 0 {
                                md.push_str(", ");
                            }
                            md.push_str(&fmt_type(p));
                        }
                        md.push_str(") -> ");
                        md.push_str(&fmt_type(ret));
                        md.push('\n');
                    }
                    md.push_str("}\n```\n\n");
                }
                Item::Const(c) => {
                    search.push(doc_search_item(
                        "const",
                        &c.name,
                        f,
                        &format!("{stem}.md"),
                        &format!("const {}", c.name),
                    ));
                    md.push_str(&format!("## `const {}`\n\n", c.name));
                    md.push_str("```mako\nconst ");
                    md.push_str(&c.name);
                    md.push_str(" = ...\n```\n\n");
                }
                Item::Import { path, alias, mode } => {
                    md.push_str("## import\n\n```mako\n");
                    md.push_str(&match mode {
                        crate::ast::ImportMode::Blank => format!("pull _ \"{path}\""),
                        crate::ast::ImportMode::Dot => format!("pull . \"{path}\""),
                        crate::ast::ImportMode::Normal => match alias {
                            Some(a) => format!("pull \"{path}\" as {a}"),
                            None => format!("pull \"{path}\""),
                        },
                    });
                    md.push_str("\n```\n\n");
                }
                Item::Actor(_) | Item::ExternC(_) | Item::On(_) | Item::Package { .. } => {}
            }
        }
        let out = out_dir.join(format!("{stem}.md"));
        fs::write(&out, &md).map_err(|e| {
            eprintln!("mako doc: write {}: {e}", out.display());
        })?;
        index.push_str(&format!("- [{stem}]({stem}.md)\n"));
        println!("wrote {}", out.display());
    }
    let idx = out_dir.join("index.md");
    fs::write(&idx, index).map_err(|e| {
        eprintln!("mako doc: {e}");
    })?;
    println!("wrote {}", idx.display());
    let examples_path = out_dir.join("examples.md");
    fs::write(&examples_path, examples).map_err(|e| {
        eprintln!("mako doc: {e}");
    })?;
    println!("wrote {}", examples_path.display());
    let search_path = out_dir.join("search-index.json");
    fs::write(
        &search_path,
        format!(
            r#"{{"schema":"mako.doc.search.v1","symbols":[{}]}}"#,
            search.join(",")
        ),
    )
    .map_err(|e| {
        eprintln!("mako doc: {e}");
    })?;
    println!("wrote {}", search_path.display());
    Ok(())
}

fn doc_search_item(kind: &str, name: &str, file: &Path, page: &str, signature: &str) -> String {
    format!(
        r#"{{"kind":"{}","name":"{}","file":"{}","page":"{}","signature":"{}"}}"#,
        json_escape(kind),
        json_escape(name),
        json_escape(&file.display().to_string()),
        json_escape(page),
        json_escape(signature)
    )
}

pub fn run_metadata(path: &Path) -> Result<(), ()> {
    let files = collect_mako_files(path);
    if files.is_empty() {
        eprintln!("mako metadata: no .mko files under {}", path.display());
        return Err(());
    }
    let mut file_json = Vec::new();
    let mut edge_json = Vec::new();
    for f in &files {
        let program = parse_file(f).map_err(|e| {
            eprintln!("mako metadata: {}: {e}", f.display());
        })?;
        let file = f.display().to_string();
        let mut symbols = Vec::new();
        let mut imports = Vec::new();
        for item in &program.items {
            match item {
                Item::Fn(fn_def) => {
                    let id = symbol_id(&file, &fn_def.name);
                    let mut calls = Vec::new();
                    collect_calls_block(&fn_def.body, &mut calls);
                    calls.sort();
                    calls.dedup();
                    for call in &calls {
                        edge_json.push(format!(
                            r#"{{"from":"{}","to":"{}","kind":"call"}}"#,
                            json_escape(&id),
                            json_escape(call)
                        ));
                    }
                    symbols.push(fn_symbol_json("function", &file, fn_def));
                }
                Item::Struct(s) => symbols.push(struct_symbol_json(&file, s)),
                Item::Enum(e) => symbols.push(enum_symbol_json(&file, e)),
                Item::Actor(a) => symbols.push(format!(
                    r#"{{"id":"{}","kind":"actor","name":"{}","receives":{}}}"#,
                    json_escape(&symbol_id(&file, &a.name)),
                    json_escape(&a.name),
                    string_array_json(
                        &a.receives
                            .iter()
                            .map(|r| r.message.clone())
                            .collect::<Vec<_>>()
                    )
                )),
                Item::Interface(i) => symbols.push(interface_symbol_json(&file, i)),
                Item::ExternC(e) => symbols.push(extern_symbol_json(&file, e)),
                Item::On(on) => symbols.push(format!(
                    r#"{{"id":"{}","kind":"on","name":"{}","methods":{}}}"#,
                    json_escape(&symbol_id(&file, &on.ty)),
                    json_escape(&on.ty),
                    on.methods.len()
                )),
                Item::Package { name } => symbols.push(format!(
                    r#"{{"id":"{}","kind":"package","name":"{}"}}"#,
                    json_escape(&symbol_id(&file, name)),
                    json_escape(name)
                )),
                Item::Const(c) => symbols.push(format!(
                    r#"{{"id":"{}","kind":"const","name":"{}"}}"#,
                    json_escape(&symbol_id(&file, &c.name)),
                    json_escape(&c.name)
                )),
                Item::Import { path, alias, mode } => {
                    let mode_s = match mode {
                        crate::ast::ImportMode::Normal => "normal",
                        crate::ast::ImportMode::Blank => "blank",
                        crate::ast::ImportMode::Dot => "dot",
                    };
                    imports.push(format!(
                        r#"{{"path":"{}","alias":{},"mode":"{}"}}"#,
                        json_escape(path),
                        optional_string_json(alias.as_deref()),
                        mode_s
                    ));
                    edge_json.push(format!(
                        r#"{{"from":"{}","to":"{}","kind":"import"}}"#,
                        json_escape(&file),
                        json_escape(path)
                    ));
                }
            }
        }
        file_json.push(format!(
            r#"{{"path":"{}","symbols":[{}],"imports":[{}]}}"#,
            json_escape(&file),
            symbols.join(","),
            imports.join(",")
        ));
    }
    println!(
        r#"{{"schema":"mako.metadata.v1","files":[{}],"edges":[{}]}}"#,
        file_json.join(","),
        edge_json.join(",")
    );
    Ok(())
}

pub fn run_api_diff(old: &Path, new: &Path) -> Result<(), ()> {
    let old_surface = api_surface(old)?;
    let new_surface = api_surface(new)?;
    let mut breaking = Vec::new();
    for (name, old_sig) in &old_surface {
        match new_surface.get(name) {
            None => breaking.push(format!("removed `{name}` ({old_sig})")),
            Some(new_sig) if new_sig != old_sig => {
                breaking.push(format!(
                    "changed `{name}`\n  old: {old_sig}\n  new: {new_sig}"
                ));
            }
            Some(_) => {}
        }
    }
    if breaking.is_empty() {
        println!(
            "mako api diff: compatible ({} old symbol(s), {} new symbol(s))",
            old_surface.len(),
            new_surface.len()
        );
        Ok(())
    } else {
        eprintln!("mako api diff: {} breaking change(s)", breaking.len());
        for item in breaking {
            eprintln!("- {item}");
        }
        Err(())
    }
}

fn api_surface(path: &Path) -> Result<BTreeMap<String, String>, ()> {
    let files = collect_mako_files(path);
    if files.is_empty() {
        eprintln!("mako api diff: no .mko files under {}", path.display());
        return Err(());
    }
    let mut surface = BTreeMap::new();
    for file in &files {
        let program = parse_file(file).map_err(|e| {
            eprintln!("mako api diff: {}: {e}", file.display());
        })?;
        for item in &program.items {
            if let Some((name, sig)) = api_item_signature(item) {
                surface.insert(name, sig);
            }
        }
    }
    Ok(surface)
}

fn api_item_signature(item: &Item) -> Option<(String, String)> {
    match item {
        Item::Fn(f) if f.name != "main" => Some((f.name.clone(), fn_signature(f))),
        Item::Fn(_) => None,
        Item::Struct(s) => Some((s.name.clone(), api_struct_signature(s))),
        Item::Enum(e) => Some((e.name.clone(), api_enum_signature(e))),
        Item::Actor(a) => Some((a.name.clone(), api_actor_signature(a))),
        Item::Interface(i) => Some((i.name.clone(), api_interface_signature(i))),
        Item::ExternC(e) => Some((e.name.clone(), api_extern_signature(e))),
        Item::Const(c) => Some((c.name.clone(), format!("const {}", c.name))),
        Item::On(on) => Some((
            on.ty.clone(),
            format!("on {} ({} methods)", on.ty, on.methods.len()),
        )),
        Item::Package { .. } | Item::Import { .. } => None,
    }
}

fn api_struct_signature(s: &StructDef) -> String {
    let fields = s
        .fields
        .iter()
        .map(|(name, ty, _)| format!("{name}: {}", fmt_type(ty)))
        .collect::<Vec<_>>()
        .join(", ");
    format!("struct {} {{{fields}}}", s.name)
}

fn api_enum_signature(e: &EnumDef) -> String {
    let variants = e
        .variants
        .iter()
        .map(|v| {
            if v.fields.is_empty() {
                v.name.clone()
            } else {
                format!(
                    "{}({})",
                    v.name,
                    v.fields.iter().map(fmt_type).collect::<Vec<_>>().join(", ")
                )
            }
        })
        .collect::<Vec<_>>()
        .join(" | ");
    format!("enum {} = {variants}", e.name)
}

fn api_actor_signature(a: &ActorDef) -> String {
    let receives = a
        .receives
        .iter()
        .map(|r| r.message.clone())
        .collect::<Vec<_>>()
        .join(", ");
    format!("actor {} receives {receives}", a.name)
}

fn api_interface_signature(i: &InterfaceDef) -> String {
    let methods = i
        .methods
        .iter()
        .map(|(name, params, ret)| {
            format!(
                "fn {name}({}) -> {}",
                params.iter().map(fmt_type).collect::<Vec<_>>().join(", "),
                fmt_type(ret)
            )
        })
        .collect::<Vec<_>>()
        .join("; ");
    format!("interface {} {{{methods}}}", i.name)
}

fn api_extern_signature(e: &ExternCDef) -> String {
    let params = e
        .params
        .iter()
        .map(|p| format!("{}: {}", p.name, fmt_type(&p.ty)))
        .collect::<Vec<_>>()
        .join(", ");
    if let Some(ret) = &e.ret {
        format!("extern \"C\" fn {}({params}) -> {}", e.name, fmt_type(ret))
    } else {
        format!("extern \"C\" fn {}({params})", e.name)
    }
}

fn symbol_id(file: &str, name: &str) -> String {
    format!("{file}::{name}")
}

fn optional_string_json(value: Option<&str>) -> String {
    value
        .map(|v| format!(r#""{}""#, json_escape(v)))
        .unwrap_or_else(|| "null".into())
}

fn string_array_json(values: &[String]) -> String {
    format!(
        "[{}]",
        values
            .iter()
            .map(|v| format!(r#""{}""#, json_escape(v)))
            .collect::<Vec<_>>()
            .join(",")
    )
}

fn params_json(params: &[Param]) -> String {
    format!(
        "[{}]",
        params
            .iter()
            .map(|p| {
                format!(
                    r#"{{"name":"{}","type":"{}","mutable":{}}}"#,
                    json_escape(&p.name),
                    json_escape(&fmt_type(&p.ty)),
                    p.mutable
                )
            })
            .collect::<Vec<_>>()
            .join(",")
    )
}

fn fn_symbol_json(kind: &str, file: &str, fn_def: &FnDef) -> String {
    format!(
        r#"{{"id":"{}","kind":"{kind}","name":"{}","params":{},"return":{},"signature":"{}"}}"#,
        json_escape(&symbol_id(file, &fn_def.name)),
        json_escape(&fn_def.name),
        params_json(&fn_def.params),
        optional_string_json(fn_def.ret.as_ref().map(|t| fmt_type(t)).as_deref()),
        json_escape(&fn_signature(fn_def))
    )
}

fn fn_signature(fn_def: &FnDef) -> String {
    let params = fn_def
        .params
        .iter()
        .map(|p| format!("{}: {}", p.name, fmt_type(&p.ty)))
        .collect::<Vec<_>>()
        .join(", ");
    if let Some(ret) = &fn_def.ret {
        format!("fn {}({params}) -> {}", fn_def.name, fmt_type(ret))
    } else {
        format!("fn {}({params})", fn_def.name)
    }
}

fn struct_symbol_json(file: &str, s: &StructDef) -> String {
    let fields = s
        .fields
        .iter()
        .map(|(name, ty, _)| {
            format!(
                r#"{{"name":"{}","type":"{}"}}"#,
                json_escape(name),
                json_escape(&fmt_type(ty))
            )
        })
        .collect::<Vec<_>>()
        .join(",");
    format!(
        r#"{{"id":"{}","kind":"struct","name":"{}","fields":[{}],"derives":{}}}"#,
        json_escape(&symbol_id(file, &s.name)),
        json_escape(&s.name),
        fields,
        string_array_json(&s.derives)
    )
}

fn enum_symbol_json(file: &str, e: &EnumDef) -> String {
    let variants = e
        .variants
        .iter()
        .map(|v| {
            let fields = v.fields.iter().map(fmt_type).collect::<Vec<_>>();
            format!(
                r#"{{"name":"{}","fields":{}}}"#,
                json_escape(&v.name),
                string_array_json(&fields)
            )
        })
        .collect::<Vec<_>>()
        .join(",");
    format!(
        r#"{{"id":"{}","kind":"enum","name":"{}","variants":[{}]}}"#,
        json_escape(&symbol_id(file, &e.name)),
        json_escape(&e.name),
        variants
    )
}

fn interface_symbol_json(file: &str, i: &InterfaceDef) -> String {
    let methods = i
        .methods
        .iter()
        .map(|(name, params, ret)| {
            let params = params.iter().map(fmt_type).collect::<Vec<_>>();
            format!(
                r#"{{"name":"{}","params":{},"return":"{}"}}"#,
                json_escape(name),
                string_array_json(&params),
                json_escape(&fmt_type(ret))
            )
        })
        .collect::<Vec<_>>()
        .join(",");
    format!(
        r#"{{"id":"{}","kind":"interface","name":"{}","methods":[{}]}}"#,
        json_escape(&symbol_id(file, &i.name)),
        json_escape(&i.name),
        methods
    )
}

fn extern_symbol_json(file: &str, e: &ExternCDef) -> String {
    format!(
        r#"{{"id":"{}","kind":"extern_c","name":"{}","params":{},"return":{}}}"#,
        json_escape(&symbol_id(file, &e.name)),
        json_escape(&e.name),
        params_json(&e.params),
        optional_string_json(e.ret.as_ref().map(|t| fmt_type(t)).as_deref())
    )
}

fn collect_calls_block(block: &Block, out: &mut Vec<String>) {
    for stmt in &block.stmts {
        collect_calls_stmt(stmt, out);
    }
}

fn collect_calls_stmt(stmt: &Stmt, out: &mut Vec<String>) {
    match stmt {
        Stmt::Let { init, .. } | Stmt::LetMulti { init, .. } => collect_calls_expr(init, out),
        Stmt::LetCommaOk { base, index, .. } | Stmt::IndexAssign { base, index, .. } => {
            collect_calls_expr(base, out);
            collect_calls_expr(index, out);
            if let Stmt::IndexAssign { value, .. } = stmt {
                collect_calls_expr(value, out);
            }
        }
        Stmt::Assign { value, .. } => collect_calls_expr(value, out),
        Stmt::FieldAssign { base, value, .. } => {
            collect_calls_expr(base, out);
            collect_calls_expr(value, out);
        }
        Stmt::Expr(expr) => collect_calls_expr(expr, out),
        Stmt::Return(Some(expr)) => collect_calls_expr(expr, out),
        Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => {}
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
        } => {
            if let Some(init) = init {
                collect_calls_stmt(init, out);
            }
            collect_calls_expr(cond, out);
            collect_calls_block(then_block, out);
            if let Some(block) = else_block {
                collect_calls_block(block, out);
            }
        }
        Stmt::While { cond, body, .. } => {
            collect_calls_expr(cond, out);
            collect_calls_block(body, out);
        }
        Stmt::For { iter, body, .. } => {
            collect_calls_expr(iter, out);
            collect_calls_block(body, out);
        }
        Stmt::CFor {
            init, cond, post, body, ..
        } => {
            collect_calls_stmt(init, out);
            collect_calls_expr(cond, out);
            collect_calls_stmt(post, out);
            collect_calls_block(body, out);
        }
        Stmt::Defer { body }
        | Stmt::Crew { body, .. }
        | Stmt::Arena { body, .. }
        | Stmt::Unsafe { body } => {
            collect_calls_block(body, out);
        }
        Stmt::Select {
            timeout_ms,
            arms,
            default_arm,
        } => {
            collect_calls_expr(timeout_ms, out);
            for (_, block) in arms {
                collect_calls_block(block, out);
            }
            if let Some(block) = default_arm {
                collect_calls_block(block, out);
            }
        }
    }
}

fn collect_calls_expr(expr: &Expr, out: &mut Vec<String>) {
    match expr {
        Expr::Call { callee, args } => {
            if let Expr::Ident(name) = callee.as_ref() {
                out.push(name.clone());
            }
            collect_calls_expr(callee, out);
            for arg in args {
                collect_calls_expr(arg, out);
            }
        }
        Expr::Method {
            receiver,
            method,
            args,
        } => {
            out.push(format!(".{method}"));
            collect_calls_expr(receiver, out);
            for arg in args {
                collect_calls_expr(arg, out);
            }
        }
        Expr::Binary { left, right, .. } => {
            collect_calls_expr(left, out);
            collect_calls_expr(right, out);
        }
        Expr::Unary { expr, .. }
        | Expr::Field { base: expr, .. }
        | Expr::Try(expr)
        | Expr::Join(expr) => {
            collect_calls_expr(expr, out);
        }
        Expr::Index { base, index } => {
            collect_calls_expr(base, out);
            collect_calls_expr(index, out);
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            collect_calls_expr(base, out);
            for bound in [low, high, max].into_iter().flatten() {
                collect_calls_expr(bound, out);
            }
        }
        Expr::StructLitPos { values, .. } => {
            for value in values {
                collect_calls_expr(value, out);
            }
        }
        Expr::StructLit { fields, update, .. } => {
            for (_, value) in fields {
                collect_calls_expr(value, out);
            }
            if let Some(u) = update {
                collect_calls_expr(u, out);
            }
        }
        Expr::StringInterp(parts) => {
            for p in parts {
                if let crate::ast::InterpPart::Expr(e, _) = p {
                    collect_calls_expr(e, out);
                }
            }
        }
        Expr::Array(items) | Expr::Tuple(items) => {
            for item in items {
                collect_calls_expr(item, out);
            }
        }
        Expr::ChanOpen { cap, .. } => collect_calls_expr(cap, out),
        Expr::Convert { args, .. } => {
            for arg in args {
                collect_calls_expr(arg, out);
            }
        }
        Expr::Make { len, cap, .. } => {
            if let Some(len) = len {
                collect_calls_expr(len, out);
            }
            if let Some(cap) = cap {
                collect_calls_expr(cap, out);
            }
        }
        Expr::Lambda { body, .. } => collect_calls_expr(body, out),
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            collect_calls_expr(cond, out);
            collect_calls_block(then_block, out);
            collect_calls_block(else_block, out);
        }
        Expr::Match { scrutinee, arms } => {
            collect_calls_expr(scrutinee, out);
            for arm in arms {
                collect_calls_expr(&arm.body, out);
            }
        }
        Expr::Block(block) => collect_calls_block(block, out),
        Expr::Kick { expr, .. } => collect_calls_expr(expr, out),
        Expr::Fan { collection, mapper } => {
            collect_calls_expr(collection, out);
            collect_calls_expr(mapper, out);
        }
        Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) | Expr::Ident(_) => {}
    }
}

pub fn run_deploy_docker(
    path: &Path,
    entry: &Path,
    bin: &str,
    port: u16,
    mode: DeployDockerMode,
) -> Result<(), ()> {
    if !valid_container_bin_name(bin) {
        eprintln!(
            "mako deploy docker: --bin must contain only ASCII letters, digits, '.', '_' or '-'"
        );
        return Err(());
    }
    fs::create_dir_all(path).map_err(|e| {
        eprintln!("mako deploy docker: {e}");
    })?;
    let dockerfile = path.join("Dockerfile");
    let entry = docker_shell_quote(&entry.to_string_lossy());
    let final_stage = match mode {
        DeployDockerMode::Scratch => format!(
            r#"FROM scratch
COPY --from=build /out/{bin} /{bin}
EXPOSE {port}
ENTRYPOINT ["/{bin}"]
"#
        ),
        DeployDockerMode::Debian => format!(
            r#"FROM debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /out/{bin} /usr/local/bin/{bin}
EXPOSE {port}
ENTRYPOINT ["/usr/local/bin/{bin}"]
"#
        ),
    };
    let body = format!(
        r#"# Generated by `mako deploy docker`
# Static musl build by default. Use `mako deploy docker --mode debian` when the
# app needs OS cert bundles, shell access, or dynamic runtime dependencies.
FROM debian:bookworm-slim AS build

ARG MAKO_VERSION=latest
ARG MAKO_ARTIFACT=mako-x86_64-unknown-linux-gnu
ARG ZIG_VERSION=0.13.0
ARG ZIG_ARCH=x86_64

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates clang curl xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL "https://ziglang.org/download/${{ZIG_VERSION}}/zig-linux-${{ZIG_ARCH}}-${{ZIG_VERSION}}.tar.xz" \
    | tar -xJ -C /opt \
    && ln -s "/opt/zig-linux-${{ZIG_ARCH}}-${{ZIG_VERSION}}/zig" /usr/local/bin/zig

RUN curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh \
    | bash -s -- --version "${{MAKO_VERSION}}" --artifact "${{MAKO_ARTIFACT}}" --prefix /usr/local --skip-doctor

WORKDIR /app
COPY . .
RUN mako build --release --target x86_64-unknown-linux-musl {entry} -o "/out/{bin}"

{final_stage}"#
    );
    fs::write(&dockerfile, body).map_err(|e| {
        eprintln!("mako deploy docker: {e}");
    })?;
    let dockerignore = path.join(".dockerignore");
    if !dockerignore.exists() {
        let ignore = r#".git
target
dist
.mako-cache
*.o
*.out
Dockerfile
"#;
        fs::write(&dockerignore, ignore).map_err(|e| {
            eprintln!("mako deploy docker: {e}");
        })?;
        println!("wrote {}", dockerignore.display());
    }
    println!("wrote {}", dockerfile.display());
    println!(
        "container mode: {mode:?}; build with `docker build -t mako-app {}`",
        path.display()
    );
    Ok(())
}

fn valid_container_bin_name(name: &str) -> bool {
    !name.is_empty()
        && name
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'.' | b'_' | b'-'))
}

fn docker_shell_quote(value: &str) -> String {
    let mut out = String::from("\"");
    for ch in value.chars() {
        match ch {
            '\\' | '"' | '$' | '`' => {
                out.push('\\');
                out.push(ch);
            }
            '\n' | '\r' => out.push(' '),
            _ => out.push(ch),
        }
    }
    out.push('"');
    out
}

pub fn run_deploy_serverless(
    path: &Path,
    provider: DeployServerlessProvider,
    name: &str,
    image: &str,
    entry: &Path,
    bin: &str,
    port: u16,
) -> Result<(), ()> {
    if !valid_deploy_name(name) {
        eprintln!(
            "mako deploy serverless: --name must contain only ASCII letters, digits, '.', '_' or '-'"
        );
        return Err(());
    }
    if !valid_image_ref(image) {
        eprintln!("mako deploy serverless: --image must be a non-empty image reference without whitespace");
        return Err(());
    }
    run_deploy_docker(path, entry, bin, port, DeployDockerMode::Scratch)?;
    match provider {
        DeployServerlessProvider::CloudRun => write_cloud_run(path, name, image, port),
        DeployServerlessProvider::Fly => write_fly(path, name, port),
    }
}

fn write_cloud_run(path: &Path, name: &str, image: &str, port: u16) -> Result<(), ()> {
    let out = path.join("cloudrun.service.yaml");
    let body = format!(
        r#"apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: {name}
spec:
  template:
    metadata:
      annotations:
        autoscaling.knative.dev/minScale: "0"
    spec:
      containers:
        - image: {image}
          ports:
            - name: http1
              containerPort: {port}
          env:
            - name: PORT
              value: "{port}"
"#
    );
    fs::write(&out, body).map_err(|e| {
        eprintln!("mako deploy serverless: {e}");
    })?;
    println!("wrote {}", out.display());
    println!(
        "deploy with `gcloud run services replace {}`",
        out.display()
    );
    Ok(())
}

fn write_fly(path: &Path, name: &str, port: u16) -> Result<(), ()> {
    let out = path.join("fly.toml");
    let body = format!(
        r#"app = "{name}"
primary_region = "iad"

[build]
  dockerfile = "Dockerfile"

[env]
  PORT = "{port}"

[[services]]
  internal_port = {port}
  protocol = "tcp"
  auto_stop_machines = true
  auto_start_machines = true
  min_machines_running = 0

  [[services.ports]]
    port = 80
    handlers = ["http"]

  [[services.ports]]
    port = 443
    handlers = ["tls", "http"]
"#
    );
    fs::write(&out, body).map_err(|e| {
        eprintln!("mako deploy serverless: {e}");
    })?;
    println!("wrote {}", out.display());
    println!("deploy with `fly deploy --config {}`", out.display());
    Ok(())
}

fn valid_deploy_name(name: &str) -> bool {
    !name.is_empty()
        && name
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'.' | b'_' | b'-'))
}

fn valid_image_ref(image: &str) -> bool {
    !image.is_empty() && !image.bytes().any(|b| b.is_ascii_whitespace())
}

pub fn run_deploy_wasm(path: &Path, entry: &Path, wasm_name: &str, port: u16) -> Result<(), ()> {
    if !valid_wasm_file_name(wasm_name) {
        eprintln!("mako deploy wasm: --wasm must be a simple filename ending in .wasm");
        return Err(());
    }
    fs::create_dir_all(path).map_err(|e| {
        eprintln!("mako deploy wasm: {e}");
    })?;

    let mut loader = include_str!("../wasm/mako-wasi-loader.js")
        .replace("hello.wasm", wasm_name)
        .replace(
            "run ./scripts/wasi-ci-build.sh and copy to wasm/",
            "run ./build-wasm.sh",
        );
    let old_copy_line = format!(" *   cp out/{wasm_name} wasm/");
    let new_copy_line = format!(" *   ./build-wasm.sh  # writes ./{wasm_name}");
    loader = loader
        .replace("Usage (after `./scripts/wasi-ci-build.sh`):", "Usage:")
        .replace(&old_copy_line, &new_copy_line);
    let index = format!(
        r#"<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Mako WASM</title>
</head>
<body>
  <h1>Mako WASM</h1>
  <p>Loads <code>{wasm_name}</code> with the generated WASI preview1 browser polyfill.</p>
  <pre id="out">loading...</pre>
  <script type="module" src="./mako-wasi-loader.js"></script>
</body>
</html>
"#
    );
    let entry_arg = docker_shell_quote(&entry.to_string_lossy());
    let build = format!(
        r#"#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUT_DIR="{out_dir}"
mkdir -p "$OUT_DIR"
mako build {entry_arg} --target wasm32-wasi -o "$OUT_DIR/{wasm_name}"
echo "wrote $OUT_DIR/{wasm_name}"
echo "preview: python3 -m http.server -d $OUT_DIR {port}"
"#,
        out_dir = path.display()
    );
    let readme = format!(
        r#"# Mako WASM deploy starter

This directory targets browser/edge-style WASM using Mako's current
`wasm32-wasi` / `wasm32-wasip1` preview1 path.

## Build

```bash
./build-wasm.sh
python3 -m http.server -d {} {}
```

Open `http://127.0.0.1:{}/`.

## Boundary

- Today: WASI preview1 build output, browser loader/polyfill for console output,
  argv/env stubs, clock/random, and a small virtual file overlay.
- Not claimed yet: WASI preview2 components, HTTP sockets in WASI, DOM bindings,
  Workers request adapters, or component-model interface generation.
"#,
        path.display(),
        port,
        port
    );

    let files = [
        ("index.html", index),
        ("mako-wasi-loader.js", loader),
        ("build-wasm.sh", build),
        ("README.md", readme),
    ];
    for (name, body) in files {
        let out = path.join(name);
        fs::write(&out, body).map_err(|e| {
            eprintln!("mako deploy wasm: {e}");
        })?;
        println!("wrote {}", out.display());
    }

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let script = path.join("build-wasm.sh");
        if let Ok(meta) = fs::metadata(&script) {
            let mut perms = meta.permissions();
            perms.set_mode(0o755);
            let _ = fs::set_permissions(&script, perms);
        }
    }

    println!("build with `{}/build-wasm.sh`", path.display());
    println!(
        "preview with `python3 -m http.server -d {} {port}`",
        path.display()
    );
    Ok(())
}

fn valid_wasm_file_name(name: &str) -> bool {
    name.ends_with(".wasm")
        && !name.contains('/')
        && !name.contains('\\')
        && valid_container_bin_name(name)
}

pub fn run_deploy_plugin(path: &Path, name: &str, kind: DeployPluginKind) -> Result<(), ()> {
    if !valid_deploy_name(name) {
        eprintln!(
            "mako deploy plugin: --name must contain only ASCII letters, digits, '.', '_' or '-'"
        );
        return Err(());
    }
    fs::create_dir_all(path).map_err(|e| {
        eprintln!("mako deploy plugin: {e}");
    })?;
    match kind {
        DeployPluginKind::Native => write_native_plugin(path, name),
        DeployPluginKind::Wasm => write_wasm_plugin(path, name),
    }
}

fn write_native_plugin(path: &Path, name: &str) -> Result<(), ()> {
    let safe_sym = name.replace(['.', '-'], "_");
    let c = format!(
        r#"#include "mako_plugin.h"
#include <stdlib.h>
#include <string.h>

static const MakoPluginHost *HOST = NULL;

static int32_t plugin_init(const MakoPluginHost *host) {{
    HOST = host;
    if (HOST && HOST->log) {{
        HOST->log(1, mako_str_view("{name}: init", sizeof("{name}: init") - 1));
    }}
    return 0;
}}

static void plugin_shutdown(void) {{
    HOST = NULL;
}}

static MakoString plugin_call(MakoString operation, MakoString payload) {{
    /* Product ops: ping, version, echo, name */
    if (operation.len == 4 && memcmp(operation.data, "ping", 4) == 0)
        return mako_str_from_cstr("pong");
    if (operation.len == 7 && memcmp(operation.data, "version", 7) == 0)
        return mako_str_from_cstr("0.1.0");
    if (operation.len == 4 && memcmp(operation.data, "name", 4) == 0)
        return mako_str_from_cstr("{name}");
    if (operation.len == 4 && memcmp(operation.data, "echo", 4) == 0) {{
        if (!payload.data || payload.len == 0) return mako_str_from_cstr("");
        char *d = (char *)malloc(payload.len + 1);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, payload.data, payload.len);
        d[payload.len] = 0;
        MakoString out = {{d, payload.len}};
        return out;
    }}
    return mako_str_from_cstr("{name}: ok");
}}

static void plugin_free_string(MakoString value) {{
    free(value.data);
}}

static const MakoPluginVTable VTABLE = {{
    MAKO_PLUGIN_ABI_VERSION,
    {{ MAKO_PLUGIN_ABI_VERSION, MAKO_PLUGIN_API_VERSION, "{name}", "0.1.0", "native" }},
    plugin_init,
    plugin_shutdown,
    plugin_call,
    plugin_free_string
}};

MAKO_PLUGIN_EXPORT const MakoPluginVTable *mako_plugin_entry(void) {{
    return &VTABLE;
}}
"#
    );
    let artifact = format!("target/lib{safe_sym}.so");
    let manifest = plugin_manifest(name, "native", &artifact);
    let build = format!(
        r#"#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$ROOT/target"
cc -std=c11 -fPIC -shared \
  -I"${{MAKO_RUNTIME:-$ROOT/../../runtime}}" \
  "$ROOT/plugin.c" \
  -o "$ROOT/target/lib{safe_sym}.so"
echo "wrote $ROOT/target/lib{safe_sym}.so"
"#
    );
    write_plugin_files(
        path,
        &[
            ("plugin.c", c),
            ("mako.plugin.toml", manifest),
            ("build-plugin.sh", build),
            ("README.md", plugin_readme(name, "native")),
        ],
    )
}

fn write_wasm_plugin(path: &Path, name: &str) -> Result<(), ()> {
    let src = format!(
        r#"// Mako WASM plugin skeleton.
// Build this module with your WASM toolchain and export `mako_plugin_call`.
// Host ABI target: {api}

#[no_mangle]
pub extern "C" fn mako_plugin_abi_version() -> u32 {{
    1
}}

#[no_mangle]
pub extern "C" fn mako_plugin_call(_op_ptr: u32, _op_len: u32, _payload_ptr: u32, _payload_len: u32) -> u32 {{
    // Return convention is host-defined for now; see docs/ABI.md.
    0
}}
"#,
        api = "mako.plugin.v1"
    );
    let manifest = plugin_manifest(name, "wasm", "plugin.wasm");
    write_plugin_files(
        path,
        &[
            ("plugin.wasm.stub.rs", src),
            ("mako.plugin.toml", manifest),
            ("README.md", plugin_readme(name, "wasm")),
        ],
    )
}

fn write_plugin_files(path: &Path, files: &[(&str, String)]) -> Result<(), ()> {
    for (name, body) in files {
        let out = path.join(name);
        fs::write(&out, body).map_err(|e| {
            eprintln!("mako deploy plugin: {e}");
        })?;
        println!("wrote {}", out.display());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let script = path.join("build-plugin.sh");
        if let Ok(meta) = fs::metadata(&script) {
            let mut perms = meta.permissions();
            perms.set_mode(0o755);
            let _ = fs::set_permissions(&script, perms);
        }
    }
    Ok(())
}

fn plugin_manifest(name: &str, kind: &str, artifact: &str) -> String {
    format!(
        r#"name = "{name}"
version = "0.1.0"
kind = "{kind}"
abi = "mako.plugin.v1"
abi_version = 1
artifact = "{artifact}"
"#
    )
}

fn plugin_readme(name: &str, kind: &str) -> String {
    format!(
        r#"# {name}

Mako {kind} plugin skeleton.

ABI:

- API string: `mako.plugin.v1`
- ABI version: `1`
- Native entrypoint: `mako_plugin_entry`
- Manifest: `mako.plugin.toml`

This is a stable ABI seed. Host-side dynamic loading, capability negotiation,
and WASM component-model adapters are still roadmap work.
"#
    )
}

/// Go-like: `*_test.mko` (preferred) or legacy `test_*.mko`.
pub fn is_test_file(path: &Path) -> bool {
    let n = path.file_name().and_then(|x| x.to_str()).unwrap_or("");
    n.ends_with("_test.mko") || (n.starts_with("test_") && n.ends_with(".mko"))
}

/// Go-style `TestXxx` plus first-class test-tooling categories.
pub fn is_test_fn(name: &str) -> bool {
    test_category(name).is_some()
}

pub fn test_category(name: &str) -> Option<&'static str> {
    if (name.starts_with("Test") && name.len() > 4) || name.starts_with("test_") {
        Some("unit")
    } else if name.starts_with("Fuzz") && name.len() > 4 {
        Some("fuzz")
    } else if name.starts_with("Property") && name.len() > 8 {
        Some("property")
    } else if name.starts_with("Snapshot") && name.len() > 8 {
        Some("snapshot")
    } else if name.starts_with("Mock") && name.len() > 4 {
        Some("mock")
    } else if name.starts_with("Fixture") && name.len() > 7 {
        Some("fixture")
    } else {
        None
    }
}

pub fn list_test_fns(program: &Program) -> Vec<String> {
    let mut names: Vec<String> = program
        .items
        .iter()
        .filter_map(|item| match item {
            Item::Fn(f) if is_test_fn(&f.name) && f.params.is_empty() => Some(f.name.clone()),
            _ => None,
        })
        .collect();
    names.sort();
    names
}

/// Non-test `.mko` siblings of `test_file` (same directory). Used by tooling/tests.
#[allow(dead_code)]
pub fn sibling_package_files(test_file: &Path) -> Vec<PathBuf> {
    let Some(dir) = test_file.parent() else {
        return Vec::new();
    };
    package_dir_sources(dir).unwrap_or_default()
}

/// Package-level names for prefix rewrite.
///
/// Critical: function names must **not** rewrite ordinary Idents (params/locals).
/// e.g. `fn body(...)` + `http_post(url, body)` must leave the param `body` alone;
/// only call callees `body(...)` and const refs are rewritten.
/// Types are separate so `fn int` in `fmt` never rewrites the builtin type `int`.
struct ImportNameSets {
    /// Callables: fns, actors, externs, free methods — rewrite only as call callees.
    fns: std::collections::HashSet<String>,
    /// Consts — rewrite bare Ident refs.
    consts: std::collections::HashSet<String>,
    /// Structs, enums, interfaces, and `on Type` type names.
    types: std::collections::HashSet<String>,
}

/// Make a C-safe / Mako-internal qualifier (`github.com/x` → `github_com_x`).
pub fn sanitize_pkg_alias(alias: &str) -> String {
    let mut out = String::with_capacity(alias.len());
    for c in alias.chars() {
        if c.is_ascii_alphanumeric() || c == '_' {
            out.push(c);
        } else {
            out.push('_');
        }
    }
    if out.is_empty() || out.as_bytes()[0].is_ascii_digit() {
        out.insert(0, '_');
    }
    out
}

/// Apply package qualifier: rename defs and rewrite internal references.
fn apply_import_prefix(program: &mut Program, alias: &str) {
    let alias = sanitize_pkg_alias(alias);
    let alias = alias.as_str();
    let mut names = ImportNameSets {
        fns: std::collections::HashSet::new(),
        consts: std::collections::HashSet::new(),
        types: std::collections::HashSet::new(),
    };
    for item in &program.items {
        match item {
            Item::Fn(f) => {
                names.fns.insert(f.name.clone());
            }
            Item::Const(c) => {
                names.consts.insert(c.name.clone());
            }
            Item::Struct(s) => {
                names.types.insert(s.name.clone());
            }
            Item::Enum(e) => {
                names.types.insert(e.name.clone());
            }
            Item::Interface(i) => {
                names.types.insert(i.name.clone());
            }
            Item::Actor(a) => {
                names.fns.insert(a.name.clone());
            }
            Item::ExternC(e) => {
                names.fns.insert(e.name.clone());
            }
            Item::On(on) => {
                names.types.insert(on.ty.clone());
                for m in &on.methods {
                    names.fns.insert(m.name.clone());
                }
            }
            Item::Package { .. } | Item::Import { .. } => {}
        }
    }
    for item in &mut program.items {
        prefix_item_def(item, alias);
        rewrite_item_refs(item, alias, &names);
    }
}

fn prefix_item_def(item: &mut Item, alias: &str) {
    match item {
        Item::Fn(f) => {
            f.name = format!("{alias}__{}", f.name);
        }
        Item::Const(c) => {
            c.name = format!("{alias}__{}", c.name);
        }
        Item::Struct(s) => {
            s.name = format!("{alias}__{}", s.name);
        }
        Item::Enum(e) => {
            e.name = format!("{alias}__{}", e.name);
        }
        Item::Interface(i) => {
            i.name = format!("{alias}__{}", i.name);
        }
        Item::Actor(a) => {
            a.name = format!("{alias}__{}", a.name);
        }
        Item::ExternC(e) => {
            e.name = format!("{alias}__{}", e.name);
        }
        Item::On(on) => {
            on.ty = format!("{alias}__{}", on.ty);
            for m in &mut on.methods {
                m.name = format!("{alias}__{}", m.name);
            }
        }
        Item::Package { .. } | Item::Import { .. } => {}
    }
}

fn rewrite_item_refs(item: &mut Item, alias: &str, names: &ImportNameSets) {
    match item {
        Item::Fn(f) => {
            for p in &mut f.params {
                rewrite_type_expr(&mut p.ty, alias, names);
            }
            if let Some(ret) = &mut f.ret {
                rewrite_type_expr(ret, alias, names);
            }
            rewrite_block(&mut f.body, alias, names);
        }
        Item::Const(c) => rewrite_expr(&mut c.value, alias, names),
        Item::Struct(s) => {
            for (_, ty, def) in &mut s.fields {
                rewrite_type_expr(ty, alias, names);
                if let Some(d) = def {
                    rewrite_expr(d, alias, names);
                }
            }
        }
        Item::Enum(e) => {
            for v in &mut e.variants {
                for t in &mut v.fields {
                    rewrite_type_expr(t, alias, names);
                }
            }
        }
        Item::Interface(i) => {
            for (_, params, ret) in &mut i.methods {
                for t in params {
                    rewrite_type_expr(t, alias, names);
                }
                rewrite_type_expr(ret, alias, names);
            }
        }
        Item::ExternC(e) => {
            for p in &mut e.params {
                rewrite_type_expr(&mut p.ty, alias, names);
            }
            if let Some(ret) = &mut e.ret {
                rewrite_type_expr(ret, alias, names);
            }
        }
        Item::On(on) => {
            for m in &mut on.methods {
                for p in &mut m.params {
                    rewrite_type_expr(&mut p.ty, alias, names);
                }
                if let Some(ret) = &mut m.ret {
                    rewrite_type_expr(ret, alias, names);
                }
                rewrite_block(&mut m.body, alias, names);
            }
        }
        Item::Actor(a) => {
            for arm in &mut a.receives {
                rewrite_block(&mut arm.body, alias, names);
            }
        }
        Item::Package { .. } | Item::Import { .. } => {}
    }
}

fn rewrite_type_expr(ty: &mut TypeExpr, alias: &str, names: &ImportNameSets) {
    match ty {
        TypeExpr::Named(n) => {
            if names.types.contains(n) {
                *n = format!("{alias}__{n}");
            }
        }
        TypeExpr::Array(inner) => rewrite_type_expr(inner, alias, names),
        TypeExpr::Map(k, v) => {
            rewrite_type_expr(k, alias, names);
            rewrite_type_expr(v, alias, names);
        }
        TypeExpr::Generic(_, args) | TypeExpr::Tuple(args) => {
            for a in args {
                rewrite_type_expr(a, alias, names);
            }
        }
        TypeExpr::Fn(params, ret) => {
            for p in params {
                rewrite_type_expr(p, alias, names);
            }
            rewrite_type_expr(ret, alias, names);
        }
    }
}

fn rewrite_block(b: &mut Block, alias: &str, names: &ImportNameSets) {
    for s in &mut b.stmts {
        rewrite_stmt(s, alias, names);
    }
}

fn rewrite_stmt(s: &mut Stmt, alias: &str, names: &ImportNameSets) {
    match s {
        Stmt::Let { ty, init, .. } => {
            if let Some(t) = ty {
                rewrite_type_expr(t, alias, names);
            }
            rewrite_expr(init, alias, names);
        }
        Stmt::LetMulti { init, .. } => rewrite_expr(init, alias, names),
        Stmt::Assign { value, .. } => rewrite_expr(value, alias, names),
        Stmt::IndexAssign {
            base,
            index,
            value,
        } => {
            rewrite_expr(base, alias, names);
            rewrite_expr(index, alias, names);
            rewrite_expr(value, alias, names);
        }
        Stmt::FieldAssign { base, value, .. } => {
            rewrite_expr(base, alias, names);
            rewrite_expr(value, alias, names);
        }
        Stmt::Expr(e) | Stmt::Return(Some(e)) => rewrite_expr(e, alias, names),
        Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => {}
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
        } => {
            if let Some(init) = init {
                rewrite_stmt(init, alias, names);
            }
            rewrite_expr(cond, alias, names);
            rewrite_block(then_block, alias, names);
            if let Some(eb) = else_block {
                rewrite_block(eb, alias, names);
            }
        }
        Stmt::While { cond, body, .. } => {
            rewrite_expr(cond, alias, names);
            rewrite_block(body, alias, names);
        }
        Stmt::For { iter, body, .. } => {
            rewrite_expr(iter, alias, names);
            rewrite_block(body, alias, names);
        }
        Stmt::CFor {
            init, cond, post, body, ..
        } => {
            rewrite_stmt(init, alias, names);
            rewrite_expr(cond, alias, names);
            rewrite_stmt(post, alias, names);
            rewrite_block(body, alias, names);
        }
        Stmt::Defer { body }
        | Stmt::Crew { body, .. }
        | Stmt::Arena { body, .. }
        | Stmt::Unsafe { body } => rewrite_block(body, alias, names),
        Stmt::Select {
            timeout_ms,
            arms,
            default_arm,
        } => {
            rewrite_expr(timeout_ms, alias, names);
            for (_, b) in arms {
                rewrite_block(b, alias, names);
            }
            if let Some(d) = default_arm {
                rewrite_block(d, alias, names);
            }
        }
        Stmt::LetCommaOk { base, index, .. } => {
            rewrite_expr(base, alias, names);
            rewrite_expr(index, alias, names);
        }
    }
}

fn rewrite_expr(e: &mut Expr, alias: &str, names: &ImportNameSets) {
    match e {
        Expr::Ident(n) => {
            // Const refs only — never rewrite params/locals that share a fn name.
            if names.consts.contains(n) {
                *n = format!("{alias}__{n}");
            }
        }
        Expr::Call { callee, args } => {
            // Rewrite package fn callees: `add(1,2)` → `lib__add(1,2)`.
            if let Expr::Ident(n) = callee.as_mut() {
                if names.fns.contains(n) {
                    *n = format!("{alias}__{n}");
                }
            } else {
                rewrite_expr(callee, alias, names);
            }
            for a in args {
                rewrite_expr(a, alias, names);
            }
        }
        Expr::Method {
            receiver,
            args,
            ..
        } => {
            rewrite_expr(receiver, alias, names);
            for a in args {
                rewrite_expr(a, alias, names);
            }
        }
        Expr::Binary { left, right, .. } => {
            rewrite_expr(left, alias, names);
            rewrite_expr(right, alias, names);
        }
        Expr::Unary { expr, .. }
        | Expr::Field { base: expr, .. }
        | Expr::Try(expr)
        | Expr::Join(expr)
        | Expr::Kick { expr, .. } => rewrite_expr(expr, alias, names),
        Expr::Index { base, index } => {
            rewrite_expr(base, alias, names);
            rewrite_expr(index, alias, names);
        }
        Expr::Slice {
            base,
            low,
            high,
            max,
        } => {
            rewrite_expr(base, alias, names);
            if let Some(x) = low {
                rewrite_expr(x, alias, names);
            }
            if let Some(x) = high {
                rewrite_expr(x, alias, names);
            }
            if let Some(x) = max {
                rewrite_expr(x, alias, names);
            }
        }
        Expr::StructLit {
            name,
            fields,
            update,
        } => {
            if names.types.contains(name) {
                *name = format!("{alias}__{name}");
            }
            for (_, v) in fields {
                rewrite_expr(v, alias, names);
            }
            if let Some(u) = update {
                rewrite_expr(u, alias, names);
            }
        }
        Expr::StructLitPos { name, values } => {
            if names.types.contains(name) {
                *name = format!("{alias}__{name}");
            }
            for v in values {
                rewrite_expr(v, alias, names);
            }
        }
        Expr::Array(xs) | Expr::Tuple(xs) => {
            for x in xs {
                rewrite_expr(x, alias, names);
            }
        }
        Expr::Convert { ty, args } => {
            rewrite_type_expr(ty, alias, names);
            for a in args {
                rewrite_expr(a, alias, names);
            }
        }
        Expr::Make { ty, len, cap } => {
            rewrite_type_expr(ty, alias, names);
            if let Some(l) = len {
                rewrite_expr(l, alias, names);
            }
            if let Some(c) = cap {
                rewrite_expr(c, alias, names);
            }
        }
        Expr::ChanOpen { elem, cap } => {
            rewrite_type_expr(elem, alias, names);
            rewrite_expr(cap, alias, names);
        }
        Expr::Lambda { body, .. } => rewrite_expr(body, alias, names),
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            rewrite_expr(cond, alias, names);
            rewrite_block(then_block, alias, names);
            rewrite_block(else_block, alias, names);
        }
        Expr::Match { scrutinee, arms } => {
            rewrite_expr(scrutinee, alias, names);
            for a in arms {
                rewrite_expr(&mut a.body, alias, names);
            }
        }
        Expr::Block(b) => rewrite_block(b, alias, names),
        Expr::Fan { collection, mapper } => {
            rewrite_expr(collection, alias, names);
            rewrite_expr(mapper, alias, names);
        }
        Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {}
        Expr::StringInterp(parts) => {
            for p in parts {
                if let crate::ast::InterpPart::Expr(e, _) = p {
                    rewrite_expr(e, alias, names);
                }
            }
        }
    }
}

/// A `[dependencies]` path entry from `mako.toml`.
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct PathDep {
    pub name: String,
    pub path: String,
}

/// Full dependency line (path and/or git). Versions checked with local SemVer rules.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestDep {
    pub name: String,
    pub path: Option<String>,
    pub git: Option<String>,
    pub rev: Option<String>,
    pub tag: Option<String>,
    pub branch: Option<String>,
    /// Declared `version = "..."` — compared to on-disk / registry when present.
    pub version: Option<String>,
}

/// Parse `1.2.3` or `v1.2.3` into (major, minor, patch). Extra pre-release suffix ignored.
pub fn parse_semver(s: &str) -> Option<(u64, u64, u64)> {
    let s = s.trim().trim_start_matches('v');
    let core = s.split('-').next()?.split('+').next()?;
    let mut parts = core.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next().unwrap_or("0").parse().ok()?;
    let patch = parts.next().unwrap_or("0").parse().ok()?;
    Some((major, minor, patch))
}

/// `req` is exact `1.2.3`, caret `^1.2.3` (compat within major), or tilde `~1.2.3` (compat within minor).
pub fn version_satisfies(version: &str, req: &str) -> bool {
    let Some(ver) = parse_semver(version) else {
        return version == req;
    };
    let req = req.trim();
    if let Some(rest) = req.strip_prefix('^') {
        let Some(r) = parse_semver(rest) else {
            return false;
        };
        if r.0 == 0 {
            // ^0.y.z → same minor
            return ver.0 == 0 && ver.1 == r.1 && ver.2 >= r.2;
        }
        ver.0 == r.0 && (ver.1 > r.1 || (ver.1 == r.1 && ver.2 >= r.2))
    } else if let Some(rest) = req.strip_prefix('~') {
        let Some(r) = parse_semver(rest) else {
            return false;
        };
        ver.0 == r.0 && ver.1 == r.1 && ver.2 >= r.2
    } else if let Some(r) = parse_semver(req) {
        ver == r
    } else {
        version == req
    }
}

/// Local registry layout: `.mako/registry/<name>/<version>/` with `mako.toml`.
pub fn registry_root(project: &Path) -> PathBuf {
    project.join(".mako").join("registry")
}

/// Path to `.mako/registry/<name>/<version>/` (local registry layout).
#[allow(dead_code)]
pub fn registry_package_dir(project: &Path, name: &str, version: &str) -> PathBuf {
    registry_root(project).join(name).join(version)
}

/// Resolve `name` @ `req` from `.mako/registry/<name>/*` (highest matching SemVer).
pub fn registry_resolve(project: &Path, name: &str, req: &str) -> Result<PathBuf, String> {
    let root = registry_root(project).join(name);
    if !root.is_dir() {
        return Err(format!(
            "no local registry entry for `{name}` under {}",
            registry_root(project).display()
        ));
    }
    let mut best: Option<(u64, u64, u64, PathBuf)> = None;
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
        if !path.join("mako.toml").exists() && !path.is_dir() {
            continue;
        }
        match &best {
            None => best = Some((sv.0, sv.1, sv.2, path)),
            Some((a, b, c, _)) if (sv.0, sv.1, sv.2) > (*a, *b, *c) => {
                best = Some((sv.0, sv.1, sv.2, path));
            }
            _ => {}
        }
    }
    best.map(|(_, _, _, p)| p)
        .ok_or_else(|| format!("no version of `{name}` satisfies `{req}` in local registry"))
}

impl ManifestDep {
    #[allow(dead_code)]
    pub fn is_path(&self) -> bool {
        self.path.is_some() && self.git.is_none()
    }

    pub fn is_git(&self) -> bool {
        self.git.is_some()
    }

    pub fn pin_label(&self) -> String {
        if let Some(r) = &self.rev {
            format!("rev={r}")
        } else if let Some(t) = &self.tag {
            format!("tag={t}")
        } else if let Some(b) = &self.branch {
            format!("branch={b}")
        } else {
            "branch=HEAD".into()
        }
    }
}

pub fn valid_dep_cache_name(name: &str) -> bool {
    !name.is_empty()
        && name != "."
        && name != ".."
        && !name.contains("..")
        && name.bytes().all(|b| {
            matches!(
                b,
                b'a'..=b'z'
                    | b'A'..=b'Z'
                    | b'0'..=b'9'
                    | b'_'
                    | b'-'
                    | b'.'
                    | b'/'
            )
        })
}

/// Sanitize import-path dep names for filesystem cache keys (`a/b` → `a!b`).
pub fn dep_cache_key(name: &str) -> String {
    name.replace('/', "!").replace('\\', "!")
}

/// Extract `"value"` after `key` in a TOML-ish inline table line.
pub fn toml_quoted_field(line: &str, key: &str) -> Option<String> {
    // Prefer `key = "` / `key=` forms; avoid matching key as a substring of URLs weakly
    // by requiring key at a field boundary (start or after `{`/`,`/space).
    let bytes = line.as_bytes();
    let key_b = key.as_bytes();
    let mut i = 0;
    while i + key_b.len() <= bytes.len() {
        if &bytes[i..i + key_b.len()] == key_b {
            let boundary_ok = i == 0 || matches!(bytes[i - 1], b'{' | b',' | b' ' | b'\t');
            let after = i + key_b.len();
            if boundary_ok && after < bytes.len() {
                let rest = line[after..].trim_start();
                if let Some(rest) = rest.strip_prefix('=') {
                    let rest = rest.trim_start();
                    if let Some(v) = rest.strip_prefix('"').and_then(|s| s.split('"').next()) {
                        if !v.is_empty() {
                            return Some(v.to_string());
                        }
                    }
                }
            }
        }
        i += 1;
    }
    None
}

fn dep_entry_name(line: &str) -> Option<String> {
    let trimmed = line.trim();
    if let Some(rest) = trimmed.strip_prefix('"') {
        let name = rest.split('"').next()?.to_string();
        if !name.is_empty() {
            return Some(name);
        }
    }
    // bare key = …
    let name = trimmed.split('=').next()?.trim();
    if name.is_empty() || name.contains('{') {
        return None;
    }
    Some(name.to_string())
}

/// Parse `[dependencies]` entries (path and/or git + optional rev/tag/branch/version).
pub fn parse_manifest_deps(text: &str) -> Vec<ManifestDep> {
    let mut deps = Vec::new();
    let mut in_deps = false;
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('#') || trimmed.is_empty() {
            continue;
        }
        if trimmed == "[dependencies]" {
            in_deps = true;
            continue;
        }
        if trimmed.starts_with('[') {
            in_deps = false;
            continue;
        }
        if !in_deps {
            continue;
        }
        let Some(name) = dep_entry_name(trimmed) else {
            continue;
        };
        let path = toml_quoted_field(trimmed, "path");
        let git = toml_quoted_field(trimmed, "git");
        let version = toml_quoted_field(trimmed, "version");
        // path, git, or registry-only (`"foo" = { version = "^1.0" }` → `.mako/registry`).
        if path.is_none() && git.is_none() && version.is_none() {
            continue;
        }
        deps.push(ManifestDep {
            name,
            path,
            git,
            rev: toml_quoted_field(trimmed, "rev"),
            tag: toml_quoted_field(trimmed, "tag"),
            branch: toml_quoted_field(trimmed, "branch"),
            version,
        });
    }
    deps
}

/// Relative cache dir for a fetched git dep: `.mako/deps/<sanitized>`.
pub fn git_dep_cache_rel(name: &str) -> String {
    format!(".mako/deps/{}", dep_cache_key(name))
}

pub fn git_dep_cache_abs(root: &Path, name: &str) -> PathBuf {
    root.join(".mako").join("deps").join(dep_cache_key(name))
}

/// Resolve on-disk roots for compile/list: path deps as declared; git deps from
/// `.mako/deps/<name>`; registry-only (`version` without path/git) from
/// `.mako/registry/<name>/<ver>/`.
pub fn resolve_dep_root(manifest_dir: &Path, dep: &ManifestDep) -> Result<PathBuf, String> {
    if let Some(p) = &dep.path {
        // Explicit path wins (includes old `.mako/cache/...` from pkg add).
        return Ok(manifest_dir.join(p));
    }
    if dep.git.is_some() {
        if !valid_dep_cache_name(&dep.name) {
            return Err(format!(
                "git dep `{}` has an invalid cache name (allowed: letters, digits, `_`, `-`, `.`)",
                dep.name
            ));
        }
        let cache = git_dep_cache_abs(manifest_dir, &dep.name);
        if cache.exists() {
            return Ok(cache);
        }
        return Err(format!(
            "git dep `{}` MISSING: {} — run `mako pkg fetch` first (needs git + network)",
            dep.name,
            cache.display()
        ));
    }
    if let Some(req) = &dep.version {
        return registry_resolve(manifest_dir, &dep.name, req);
    }
    Err(format!(
        "dependency `{}` has neither path, git, nor version (registry)",
        dep.name
    ))
}

/// A `[dependencies]` path-only view (compat for lock hashing).
#[allow(dead_code)]
pub fn parse_path_deps(text: &str) -> Vec<PathDep> {
    parse_manifest_deps(text)
        .into_iter()
        .filter_map(|d| d.path.map(|path| PathDep { name: d.name, path }))
        .collect()
}

/// Walk up from `start` looking for `mako.toml`; return its directory.
pub fn find_nearest_manifest_dir(start: &Path) -> Option<PathBuf> {
    let mut dir = if start.is_file() {
        start.parent()?.to_path_buf()
    } else {
        start.to_path_buf()
    };
    for _ in 0..12 {
        if dir.join("mako.toml").exists() {
            return Some(dir);
        }
        if !dir.pop() {
            break;
        }
    }
    None
}

/// Parse `[workspace] members = ["a", "b"]` (single- or multi-line) from `mako.toml`.
/// Local-only sketch — no registry.
pub fn parse_workspace_members(text: &str) -> Vec<String> {
    let mut in_workspace = false;
    let mut collecting = false;
    let mut buf = String::new();
    let mut members = Vec::new();
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('#') {
            continue;
        }
        if trimmed.starts_with('[') {
            if collecting {
                break;
            }
            in_workspace = trimmed == "[workspace]";
            continue;
        }
        if !in_workspace {
            continue;
        }
        if let Some(rest) = trimmed.strip_prefix("members") {
            let rest = rest.trim().trim_start_matches('=').trim();
            buf.push_str(rest);
            collecting = true;
            if rest.contains(']') {
                break;
            }
            continue;
        }
        if collecting {
            buf.push_str(trimmed);
            if trimmed.contains(']') {
                break;
            }
        }
    }
    if collecting || !buf.is_empty() {
        for part in buf.split(|c| c == ',' || c == '[' || c == ']') {
            let m = part.trim().trim_matches('"').trim_matches('\'').trim();
            if !m.is_empty() {
                members.push(m.to_string());
            }
        }
    }
    members
}

/// If `root` has a workspace `mako.toml`, return member directories (existing only).
pub fn workspace_member_dirs(root: &Path) -> Option<Vec<PathBuf>> {
    let manifest = root.join("mako.toml");
    if !manifest.is_file() {
        return None;
    }
    let text = fs::read_to_string(&manifest).ok()?;
    let names = parse_workspace_members(&text);
    if names.is_empty() {
        return None;
    }
    let mut out = Vec::new();
    for name in names {
        let p = root.join(&name);
        if p.is_dir() {
            out.push(p);
        } else {
            // Still surface missing members to callers via empty? skip — caller validates
            out.push(p);
        }
    }
    Some(out)
}

/// Preferred compile roots for a package directory: `main.mko` then `lib.mko`.
pub fn package_entry_mko(pkg_dir: &Path) -> Option<PathBuf> {
    let main = pkg_dir.join("main.mko");
    if main.is_file() {
        return Some(main);
    }
    let lib = pkg_dir.join("lib.mko");
    if lib.is_file() {
        return Some(lib);
    }
    None
}

/// Entry used when producing a linked binary (`main.mko` only).
pub fn package_main_mko(pkg_dir: &Path) -> Option<PathBuf> {
    let main = pkg_dir.join("main.mko");
    if main.is_file() {
        Some(main)
    } else {
        None
    }
}

/// `.mko` roots for a path dependency (package-per-directory):
/// - path to a `.mko` file → that file
/// - package dir → all non-test `.mko` except `main.mko` (binary entry stays out of lib surface)
pub fn collect_path_dep_sources(dep_name: &str, dep_root: &Path) -> Result<Vec<PathBuf>, String> {
    let missing = || {
        format!(
            "path dep `{dep_name}` MISSING: {} (fix mako.toml or create the package)",
            dep_root.display()
        )
    };
    if is_mko_source(dep_root) {
        if !dep_root.exists() {
            return Err(missing());
        }
        return Ok(vec![dep_root.to_path_buf()]);
    }
    if !dep_root.exists() {
        return Err(missing());
    }
    if !dep_root.is_dir() {
        return Err(missing());
    }
    let mut out = package_dir_sources(dep_root).map_err(|e| {
        format!(
            "path dep `{dep_name}` has no .mko sources at {} ({e})",
            dep_root.display()
        )
    })?;
    // Library surface: drop binary entry so dependents do not pull `fn main`.
    out.retain(|p| {
        p.file_name()
            .and_then(|n| n.to_str())
            .map(|n| n != "main.mko")
            .unwrap_or(true)
    });
    if out.is_empty() {
        return Err(format!(
            "path dep `{dep_name}` has no library .mko sources at {} \
             (add lib.mko / package units; main.mko alone is not a library)",
            dep_root.display()
        ));
    }
    Ok(out)
}

fn strip_main_fn(program: &mut Program) {
    program
        .items
        .retain(|item| !matches!(item, Item::Fn(f) if f.name == "main"));
}

/// Package root identity for cycle / duplicate detection (dir or .mko file).
fn path_dep_identity(dep_root: &Path) -> PathBuf {
    dep_root
        .canonicalize()
        .unwrap_or_else(|_| dep_root.to_path_buf())
}

/// Merge path + fetched-git deps declared in `manifest_dir/mako.toml` (transitive).
/// Each package is namespaced by the name its *parent* declared: call `helper.add(...)`.
fn merge_path_deps_from_manifest(
    manifest_dir: &Path,
    program: &mut Program,
    seen_pkgs: &mut std::collections::HashSet<PathBuf>,
    seen_sources: &mut std::collections::HashSet<PathBuf>,
) -> Result<(), String> {
    let manifest = manifest_dir.join("mako.toml");
    if !manifest.exists() {
        return Ok(());
    }
    let text =
        fs::read_to_string(&manifest).map_err(|e| format!("read {}: {e}", manifest.display()))?;
    for dep in parse_manifest_deps(&text) {
        let full = resolve_dep_root(manifest_dir, &dep)?;
        let pkg_id = path_dep_identity(&full);
        if !seen_pkgs.insert(pkg_id) {
            continue; // already merged this package (cycle or diamond)
        }
        let sources = collect_path_dep_sources(&dep.name, &full).map_err(|e| {
            if dep.is_git() && !full.exists() {
                format!(
                    "git dep `{}` MISSING: {} — run `mako pkg fetch` first (needs git + network)",
                    dep.name,
                    git_dep_cache_abs(manifest_dir, &dep.name).display()
                )
            } else {
                e
            }
        })?;
        let mut pkg_prog = Program { items: Vec::new() };
        for src in sources {
            let canon = src
                .canonicalize()
                .map_err(|e| format!("dep `{}` MISSING: {} ({e})", dep.name, src.display()))?;
            if !seen_sources.insert(canon.clone()) {
                continue;
            }
            let mut imported = parse_program_file(&canon)?;
            strip_main_fn(&mut imported);
            pkg_prog.items.extend(imported.items);
        }
        if !pkg_prog.items.is_empty() {
            apply_import_prefix(&mut pkg_prog, &dep.name);
            program.items.extend(pkg_prog.items);
        }
        // Transitive: walk the dependency's package directory (or parent of a .mko file).
        let next_dir = if full.is_file() {
            full.parent().map(|p| p.to_path_buf())
        } else {
            Some(full.clone())
        };
        if let Some(dir) = next_dir {
            if dir.join("mako.toml").exists() {
                merge_path_deps_from_manifest(&dir, program, seen_pkgs, seen_sources)?;
            }
        }
    }
    Ok(())
}

/// Merge nearest `mako.toml` path dependencies into `program` (transitive A→B→C).
/// Each dep is namespaced as `depname__fn` so callers use `depname.fn(...)`.
pub fn merge_path_dependencies(entry: &Path, mut program: Program) -> Result<Program, String> {
    let Some(manifest_dir) = find_nearest_manifest_dir(entry) else {
        return Ok(program);
    };
    let mut seen_pkgs = std::collections::HashSet::new();
    let mut seen_sources = std::collections::HashSet::new();
    if let Ok(c) = entry.canonicalize() {
        seen_sources.insert(c);
    }
    // Root package identity — don't treat root as a dep of itself when path="." appears.
    if let Ok(c) = manifest_dir.canonicalize() {
        seen_pkgs.insert(c);
    } else {
        seen_pkgs.insert(manifest_dir.clone());
    }
    merge_path_deps_from_manifest(
        &manifest_dir,
        &mut program,
        &mut seen_pkgs,
        &mut seen_sources,
    )?;
    Ok(program)
}

/// Resolve `import "./path.mko"` items by merging imported files (cycle-safe).
/// Imports are relative to `from_file`'s directory. Nested imports are followed.
/// When importer `mako.toml` has `visibility = "explicit"`, only `export`/capital
/// items from pulled packs are merged.
pub fn resolve_imports(from_file: &Path, program: Program) -> Result<Program, String> {
    let mut seen = std::collections::HashSet::new();
    if let Some(canon) = from_file.canonicalize().ok() {
        seen.insert(canon);
    } else {
        seen.insert(from_file.to_path_buf());
    }
    let explicit = read_visibility_explicit(from_file);
    resolve_imports_rec(from_file, program, &mut seen, explicit)
}

/// `visibility = "explicit"` from nearest `mako.toml` (importer).
fn read_visibility_explicit(from_file: &Path) -> bool {
    let Some(dir) = find_nearest_manifest_dir(from_file) else {
        return false;
    };
    let Ok(text) = fs::read_to_string(dir.join("mako.toml")) else {
        return false;
    };
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('#') {
            continue;
        }
        if t.starts_with("visibility") && t.contains('=') {
            let v = t
                .split('=')
                .nth(1)
                .unwrap_or("")
                .trim()
                .trim_matches('"')
                .trim_matches('\'');
            return v == "explicit";
        }
    }
    false
}

fn name_looks_exported(name: &str) -> bool {
    name.chars()
        .next()
        .map(|c| c.is_ascii_uppercase())
        .unwrap_or(false)
}

/// Drop non-exported items when `visibility = "explicit"`.
fn filter_exported_only(program: &mut Program) {
    program.items.retain(|item| match item {
        Item::Fn(f) => f.exported,
        Item::Struct(s) => s.exported,
        Item::Enum(e) => e.exported,
        Item::Interface(i) => name_looks_exported(&i.name),
        Item::On(o) => o.exported,
        Item::Const(c) => name_looks_exported(&c.name),
        Item::Actor(a) => name_looks_exported(&a.name),
        Item::ExternC(e) => name_looks_exported(&e.name),
        Item::Package { .. } | Item::Import { .. } => false,
    });
}

fn resolve_imports_rec(
    from_file: &Path,
    program: Program,
    seen: &mut std::collections::HashSet<PathBuf>,
    explicit_visibility: bool,
) -> Result<Program, String> {
    let base = from_file
        .parent()
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| PathBuf::from("."));
    let mut out = Program { items: Vec::new() };
    for item in program.items {
        match item {
            Item::Import { path, alias, mode } => {
                let targets = resolve_import_targets(&base, &path)?;
                // Package-per-directory: merge all units first, then qualify once so
                // cross-file calls (more.mko → greet in lib.mko) rewrite correctly.
                let mut pkg_prog = Program { items: Vec::new() };
                let mut pkg_name: Option<String> = None;
                let mut path_alias: Option<String> = None;
                let mut any_new = false;
                for target in &targets {
                    let canon = target.canonicalize().map_err(|e| {
                        format!("import \"{path}\": cannot open {}: {e}", target.display())
                    })?;
                    if !seen.insert(canon.clone()) {
                        continue; // already merged (cycle or duplicate)
                    }
                    any_new = true;
                    let imported = parse_program_file_raw(&canon)?;
                    if pkg_name.is_none() {
                        pkg_name = package_name_of(&imported);
                    } else if let (Some(a), Some(b)) = (&pkg_name, package_name_of(&imported)) {
                        if a != &b {
                            return Err(format!(
                                "import \"{path}\": package-per-directory clash — \
                                 pack `{a}` vs pack `{b}` in {}",
                                target.display()
                            ));
                        }
                    }
                    if path_alias.is_none() {
                        path_alias = path_default_alias(&path, target);
                    }
                    let mut unit =
                        resolve_imports_rec(&canon, imported, seen, explicit_visibility)?;
                    unit.items
                        .retain(|i| !matches!(i, Item::Package { .. } | Item::Import { .. }));
                    pkg_prog.items.extend(unit.items);
                }
                if !any_new || pkg_prog.items.is_empty() {
                    continue;
                }
                match mode {
                    ImportMode::Blank => {
                        // Blank import: load/typecheck dependency only.
                        // Mako has no init(); drop symbols so they stay private.
                        continue;
                    }
                    ImportMode::Dot => {
                        if explicit_visibility {
                            filter_exported_only(&mut pkg_prog);
                        }
                        out.items.extend(pkg_prog.items);
                    }
                    ImportMode::Normal => {
                        // Always package-qualify. Default: package clause (≠ main), else path.
                        let a = alias
                            .clone()
                            .or_else(|| {
                                pkg_name
                                    .clone()
                                    .filter(|n| n != "main" && !n.is_empty())
                            })
                            .or(path_alias)
                            .ok_or_else(|| {
                                format!(
                                    "pull \"{path}\": cannot derive pack name \
                                     (add `pack name` or use `pull \"{path}\" as name`)"
                                )
                            })?;
                        if explicit_visibility {
                            filter_exported_only(&mut pkg_prog);
                        }
                        apply_import_prefix(&mut pkg_prog, &a);
                        out.items.extend(pkg_prog.items);
                    }
                }
            }
            other => out.items.push(other),
        }
    }
    Ok(out)
}

/// First `package name` in a program, if any.
fn package_name_of(program: &Program) -> Option<String> {
    for item in &program.items {
        if let Item::Package { name } = item {
            return Some(name.clone());
        }
    }
    None
}

/// Default import name from path (last element, strip `.mko`).
fn path_default_alias(import_path: &str, resolved: &Path) -> Option<String> {
    let from_import = Path::new(import_path.trim_end_matches('/'))
        .file_name()
        .and_then(|s| s.to_str())
        .map(|s| s.trim_end_matches(".mko").to_string())
        .filter(|s| !s.is_empty() && s != "." && s != "..");
    if from_import.is_some() {
        return from_import;
    }
    resolved
        .file_stem()
        .and_then(|s| s.to_str())
        .map(|s| s.to_string())
}

/// Resolve `import "./x.mko"`, `import "./pkg"`, `import "strings"`,
/// `import "encoding/json"`, or module paths (`github.com/…`, `izi-iva/pkg/…`).
/// Returns one or more source files (directory packages may expand to lib + siblings).
fn resolve_import_targets(base: &Path, path: &str) -> Result<Vec<PathBuf>, String> {
    let is_rel = path.starts_with("./")
        || path.starts_with("../")
        || path.starts_with('/')
        || path.ends_with(".mko");
    if is_rel {
        let rel = PathBuf::from(path);
        let target = if rel.is_absolute() {
            rel
        } else {
            base.join(rel)
        };
        return sources_at_path(&target)
            .map_err(|e| format!("import \"{path}\": {e}"));
    }
    let pkg = path.trim_matches('/');

    // 1) Std: `strings`, `encoding/json`, `net/http`, …
    if let Some(std_root) = find_std_root() {
        if let Ok(srcs) = sources_under_root(&std_root, pkg) {
            return Ok(srcs);
        }
    }

    // 2) Module / vendor / dependency paths (Go-style import paths)
    if let Ok(srcs) = resolve_module_import_path(base, pkg) {
        return Ok(srcs);
    }

    let std_hint = find_std_root()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|| "(std not found)".into());
    Err(format!(
        "import \"{path}\": not found\n  looked under std ({std_hint}), \
         vendor/, .mako/pkg/, module path, and [dependencies]"
    ))
}

/// Prefer package directory (all units), else single-file `root/pkg.mko`.
fn sources_under_root(root: &Path, pkg: &str) -> Result<Vec<PathBuf>, String> {
    let dir = root.join(pkg);
    if dir.is_dir() {
        // Package-per-directory: every non-test .mko in the dir is one package.
        return package_dir_sources(&dir);
    }
    let as_file = root.join(format!("{pkg}.mko"));
    if as_file.is_file() {
        return Ok(vec![as_file]);
    }
    Err(format!("no sources for `{pkg}` under {}", root.display()))
}

fn sources_at_path(target: &Path) -> Result<Vec<PathBuf>, String> {
    if target.is_file() {
        return Ok(vec![target.to_path_buf()]);
    }
    if target.is_dir() {
        return package_dir_sources(target);
    }
    let with_ext = target.with_extension("mko");
    if with_ext.is_file() {
        return Ok(vec![with_ext]);
    }
    Err(format!(
        "not found at {} (file, dir, or .mko)",
        target.display()
    ))
}

/// Resolve non-std import paths:
/// - `[dependencies]` key equal to the import path
/// - `module = "izi-iva"` → `izi-iva/pkg/acd` maps to `<root>/pkg/acd`
/// - `vendor/<path>/`
/// - `.mako/pkg/<path>/`
fn resolve_module_import_path(from_dir: &Path, path: &str) -> Result<Vec<PathBuf>, String> {
    let Some(manifest_dir) = find_nearest_manifest_dir(from_dir) else {
        return Err("no mako.toml for module resolution".into());
    };
    let manifest = manifest_dir.join("mako.toml");
    let text = fs::read_to_string(&manifest).unwrap_or_default();

    // Dependency keyed by full import path
    for dep in parse_manifest_deps(&text) {
        if dep.name == path {
            let root = resolve_dep_root(&manifest_dir, &dep)?;
            return sources_at_path(&root).or_else(|_| {
                if root.is_dir() {
                    package_dir_sources(&root)
                } else {
                    Err(format!("dep `{path}` has no .mko sources at {}", root.display()))
                }
            });
        }
    }

    // Module path: module = "izi-iva"  (top-level or [package])
    if let Some(mod_path) = parse_module_path(&text) {
        if path == mod_path {
            return package_dir_sources(&manifest_dir);
        }
        let prefix = format!("{mod_path}/");
        if let Some(rest) = path.strip_prefix(&prefix) {
            let target = manifest_dir.join(rest);
            if let Ok(srcs) = sources_at_path(&target) {
                return Ok(srcs);
            }
        }
    }

    // vendor/<import path>
    let vendor = manifest_dir.join("vendor").join(path);
    if let Ok(srcs) = sources_at_path(&vendor) {
        return Ok(srcs);
    }

    // .mako/pkg/<import path> (fetched modules)
    let cached = manifest_dir.join(".mako").join("pkg").join(path);
    if let Ok(srcs) = sources_at_path(&cached) {
        return Ok(srcs);
    }

    Err(format!("module path `{path}` not resolved from {}", manifest_dir.display()))
}

/// `module = "izi-iva"` at top level or under `[package]`.
fn parse_module_path(text: &str) -> Option<String> {
    let mut in_package = false;
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('#') || t.is_empty() {
            continue;
        }
        if t == "[package]" {
            in_package = true;
            continue;
        }
        if t.starts_with('[') {
            in_package = false;
            continue;
        }
        // Top-level or [package] `module = "…"`
        if let Some(rest) = t.strip_prefix("module") {
            let rest = rest.trim().trim_start_matches('=').trim();
            let v = rest.trim_matches('"').trim_matches('\'').trim();
            if !v.is_empty() && (in_package || !t.contains('[')) {
                return Some(v.to_string());
            }
        }
    }
    // Also accept bare top-level before any table
    let mut saw_table = false;
    for line in text.lines() {
        let t = line.trim();
        if t.starts_with('[') {
            saw_table = true;
        }
        if saw_table {
            continue;
        }
        if let Some(rest) = t.strip_prefix("module") {
            let rest = rest.trim().trim_start_matches('=').trim();
            let v = rest.trim_matches('"').trim_matches('\'').trim();
            if !v.is_empty() {
                return Some(v.to_string());
            }
        }
    }
    None
}

/// Sources for a package directory: **all** non-test `.mko` files (package-per-directory).
/// Sorted with `lib.mko` first (if present), then alphabetical — stable merge order.
fn package_dir_sources(dir: &Path) -> Result<Vec<PathBuf>, String> {
    let mut files = Vec::new();
    let rd = fs::read_dir(dir).map_err(|e| format!("read {}: {e}", dir.display()))?;
    for ent in rd.flatten() {
        let p = ent.path();
        if p.extension().and_then(|e| e.to_str()) != Some("mko") {
            continue;
        }
        let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if is_test_file(&p) || name.starts_with('.') {
            continue;
        }
        files.push(p);
    }
    files.sort_by(|a, b| {
        let an = a.file_name().and_then(|n| n.to_str()).unwrap_or("");
        let bn = b.file_name().and_then(|n| n.to_str()).unwrap_or("");
        match (an == "lib.mko", bn == "lib.mko") {
            (true, false) => std::cmp::Ordering::Less,
            (false, true) => std::cmp::Ordering::Greater,
            _ => an.cmp(bn),
        }
    });
    if files.is_empty() {
        return Err(format!("package dir {} has no .mko sources", dir.display()));
    }
    Ok(files)
}

/// Whether `entry`'s directory is a multi-file package root (not a flat demo dump).
///
/// Merge when:
/// - the file is a `*_test.mko` (Go-style same-dir package under test), or
/// - the directory has `mako.toml`, or
/// - the directory has `lib.mko` / `main.mko` (package layout convention).
fn package_dir_merge_enabled(entry: &Path) -> bool {
    if is_test_file(entry) {
        return true;
    }
    let Some(dir) = entry.parent() else {
        return false;
    };
    if dir.join("mako.toml").is_file() {
        return true;
    }
    if dir.join("lib.mko").is_file() || dir.join("main.mko").is_file() {
        return true;
    }
    false
}

/// Merge same-directory non-test units into a program already loaded from `entry`.
/// Enforces one `pack`/`package` name per directory (Go package-per-dir model).
pub fn merge_package_dir_siblings(entry: &Path, mut program: Program) -> Result<Program, String> {
    if !package_dir_merge_enabled(entry) {
        return Ok(program);
    }
    let Some(dir) = entry.parent() else {
        return Ok(program);
    };
    if !dir.is_dir() {
        return Ok(program);
    }
    let entry_canon = entry
        .canonicalize()
        .unwrap_or_else(|_| entry.to_path_buf());
    let entry_pkg = package_name_of(&program);
    let sources = match package_dir_sources(dir) {
        Ok(s) => s,
        Err(_) => return Ok(program),
    };
    for sib in sources {
        let sib_canon = sib
            .canonicalize()
            .unwrap_or_else(|_| sib.clone());
        if sib_canon == entry_canon {
            continue;
        }
        // Load sibling with its own imports; do not re-merge path deps (caller owns that).
        let mut extra = parse_program_file_raw(&sib)
            .map_err(|e| format!("{}: {e}", sib.display()))?;
        extra = resolve_imports(&sib, extra)
            .map_err(|e| format!("{}: {e}", sib.display()))?;
        let sib_pkg = package_name_of(&extra);
        match (&entry_pkg, &sib_pkg) {
            (Some(a), Some(b)) if a != b => {
                return Err(format!(
                    "package-per-directory: {} declares pack `{a}` but {} declares pack `{b}` \
                     (one package name per directory)",
                    entry.display(),
                    sib.display()
                ));
            }
            (Some(_), None) | (None, Some(_)) => {
                // One file omitted the clause — allow (clause is optional); prefer declared name.
            }
            _ => {}
        }
        extra
            .items
            .retain(|i| !matches!(i, Item::Package { .. } | Item::Import { .. }));
        program.items.extend(extra.items);
    }
    Ok(program)
}

/// Locate `std/` next to the repo / install (mirrors runtime discovery).
pub fn find_std_root() -> Option<PathBuf> {
    if let Ok(s) = std::env::var("MAKO_STD") {
        let p = PathBuf::from(s);
        if p.is_dir() {
            return Some(p.canonicalize().unwrap_or(p));
        }
    }
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        let p = PathBuf::from(manifest).join("std");
        if p.is_dir() {
            return Some(p);
        }
    }
    if let Ok(cwd) = std::env::current_dir() {
        let p = cwd.join("std");
        if p.is_dir() {
            return Some(p);
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(bin_dir) = exe.parent() {
            for candidate in [
                bin_dir.join("std"),
                bin_dir.join("../std"),
                bin_dir.join("../share/mako/std"),
                bin_dir.join("../../share/mako/std"),
                bin_dir.join("../../std"),
            ] {
                if candidate.is_dir() {
                    return Some(candidate.canonicalize().unwrap_or(candidate));
                }
            }
        }
    }
    if let Some(home) = std::env::var_os("HOME") {
        let p = PathBuf::from(home).join(".local/share/mako/std");
        if p.is_dir() {
            return Some(p);
        }
    }
    for base in [
        PathBuf::from("/usr/local/share/mako/std"),
        PathBuf::from("/opt/homebrew/share/mako/std"),
    ] {
        if base.is_dir() {
            return Some(base);
        }
    }
    None
}

/// Parse + desugar without resolving imports (used by import recursion).
fn parse_program_file_raw(path: &Path) -> Result<Program, String> {
    let src = fs::read_to_string(path).map_err(|e| e.to_string())?;
    let tokens = Lexer::new(&src).tokenize().map_err(|e| format!("{e}"))?;
    let program = Parser::new(tokens).parse().map_err(|e| format!("{e}"))?;
    Ok(desugar::desugar(program))
}

pub fn parse_program_file(path: &Path) -> Result<Program, String> {
    let program = parse_program_file_raw(path)?;
    resolve_imports(path, program)
}

/// Merge test file with same-directory package sources; strip `main` (harness provides it).
pub fn load_test_package(test_file: &Path) -> Result<(Program, Vec<String>), String> {
    let mut program = parse_program_file(test_file)?;
    program = merge_package_dir_siblings(test_file, program)?;
    program = merge_path_dependencies(test_file, program)?;
    program
        .items
        .retain(|item| !matches!(item, Item::Fn(f) if f.name == "main"));
    let mut tc = TypeChecker::new();
    if let Some(dir) = find_nearest_manifest_dir(test_file) {
        let manifest = dir.join("mako.toml");
        if let Ok(text) = fs::read_to_string(&manifest) {
            apply_package_safety_flags(&mut tc, &text);
        }
    }
    tc.check(&program).map_err(|e| format!("{e}"))?;
    for f in tc.mono_fns {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Fn(x) if x.name == f.name))
        {
            program.items.push(Item::Fn(f));
        }
    }
    for s in tc.mono_structs {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Struct(x) if x.name == s.name))
        {
            program.items.push(Item::Struct(s));
        }
    }
    for e in tc.mono_enums {
        if !program
            .items
            .iter()
            .any(|i| matches!(i, Item::Enum(x) if x.name == e.name))
        {
            program.items.push(Item::Enum(e));
        }
    }
    let tests = list_test_fns(&program);
    if tests.is_empty() {
        return Err(format!(
            "{}: no Test/Fuzz/Property/Snapshot/Mock/Fixture functions (empty params)",
            test_file.display()
        ));
    }
    Ok((program, tests))
}

/// Match Go-ish `-run` patterns:
/// - `/regex/` — Rust `regex` crate (unanchored unless you use `^`/`$`)
/// - `*` / `?` globs
/// - otherwise substring
pub fn test_name_matches(name: &str, filter: &str) -> bool {
    if filter.is_empty() {
        return true;
    }
    if let Some(inner) = regex_filter(filter) {
        return match regex::Regex::new(inner) {
            Ok(re) => re.is_match(name),
            Err(_) => false, // invalid pattern matches nothing
        };
    }
    if filter.contains('*') || filter.contains('?') {
        return glob_match(filter, name);
    }
    // A plain filter matches by exact name so focused runs are unambiguous:
    // `--run TestConnect` never also selects `TestConnectPool`. Use a `*`/`?`
    // glob or `/regex/` for intentional fuzzy matching.
    name == filter
}

/// `/pattern/` → Some("pattern"); otherwise None.
fn regex_filter(filter: &str) -> Option<&str> {
    let b = filter.as_bytes();
    if b.len() >= 2 && b[0] == b'/' && b[b.len() - 1] == b'/' {
        Some(&filter[1..filter.len() - 1])
    } else {
        None
    }
}

fn glob_match(pat: &str, text: &str) -> bool {
    let p: Vec<char> = pat.chars().collect();
    let t: Vec<char> = text.chars().collect();
    fn rec(p: &[char], t: &[char]) -> bool {
        match (p.first(), t.first()) {
            (None, None) => true,
            (Some('*'), _) => {
                for i in 0..=t.len() {
                    if rec(&p[1..], &t[i..]) {
                        return true;
                    }
                }
                false
            }
            (Some('?'), Some(_)) => rec(&p[1..], &t[1..]),
            (Some(a), Some(b)) if a == b => rec(&p[1..], &t[1..]),
            _ => false,
        }
    }
    rec(&p, &t)
}

#[cfg(test)]
mod deploy_docker_tests {
    use super::{
        docker_shell_quote, valid_container_bin_name, valid_deploy_name, valid_image_ref,
        valid_wasm_file_name,
    };

    #[test]
    fn validates_container_binary_names() {
        assert!(valid_container_bin_name("server"));
        assert!(valid_container_bin_name("api_v1.2"));
        assert!(valid_container_bin_name("worker-1"));
        assert!(!valid_container_bin_name(""));
        assert!(!valid_container_bin_name("../server"));
        assert!(!valid_container_bin_name("server name"));
        assert!(!valid_container_bin_name("server;rm"));
    }

    #[test]
    fn quotes_docker_shell_args() {
        assert_eq!(docker_shell_quote("main.mko"), "\"main.mko\"");
        assert_eq!(
            docker_shell_quote("app/has \"quote\".mko"),
            "\"app/has \\\"quote\\\".mko\""
        );
        assert_eq!(docker_shell_quote("app/$name.mko"), "\"app/\\$name.mko\"");
    }

    #[test]
    fn validates_serverless_names_and_images() {
        assert!(valid_deploy_name("mako-app"));
        assert!(valid_deploy_name("api_v1.2"));
        assert!(!valid_deploy_name(""));
        assert!(!valid_deploy_name("../api"));
        assert!(!valid_deploy_name("api name"));

        assert!(valid_image_ref("gcr.io/project/mako-app:latest"));
        assert!(valid_image_ref("registry.example.com/team/api:v1"));
        assert!(!valid_image_ref(""));
        assert!(!valid_image_ref("bad image"));
    }

    #[test]
    fn validates_wasm_output_names() {
        assert!(valid_wasm_file_name("hello.wasm"));
        assert!(valid_wasm_file_name("edge_app-1.wasm"));
        assert!(!valid_wasm_file_name("hello"));
        assert!(!valid_wasm_file_name("../hello.wasm"));
        assert!(!valid_wasm_file_name("nested/hello.wasm"));
        assert!(!valid_wasm_file_name("bad name.wasm"));
    }
}

#[cfg(test)]
mod semver_tests {
    use super::{parse_semver, version_satisfies};

    #[test]
    fn parse_and_caret_tilde() {
        assert_eq!(parse_semver("1.2.3"), Some((1, 2, 3)));
        assert_eq!(parse_semver("v0.1.0"), Some((0, 1, 0)));
        assert!(version_satisfies("1.2.5", "^1.2.3"));
        assert!(!version_satisfies("2.0.0", "^1.2.3"));
        assert!(version_satisfies("1.2.9", "~1.2.3"));
        assert!(!version_satisfies("1.3.0", "~1.2.3"));
        assert!(version_satisfies("1.2.3", "1.2.3"));
    }
}

#[cfg(test)]
mod manifest_dep_parse_tests {
    use super::{
        audit_version_matches, parse_advisories, parse_license_policy, parse_lock_packages,
        parse_manifest_deps, toml_quoted_field,
    };

    #[test]
    fn path_and_git_deps() {
        use super::parse_path_deps;
        let t = r#"
[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
"tool" = { git = "https://example.com/tool.git", tag = "v1.2.3", version = "1.2.3" }
"pin" = { git = "https://example.com/pin.git", rev = "abc123", branch = "main" }
"#;
        let deps = parse_manifest_deps(t);
        assert_eq!(deps.len(), 3);
        assert!(deps[0].is_path());
        assert_eq!(deps[0].path.as_deref(), Some("../helper"));
        assert_eq!(deps[0].version.as_deref(), Some("0.1.0"));
        assert!(deps[1].is_git());
        assert_eq!(deps[1].tag.as_deref(), Some("v1.2.3"));
        assert_eq!(deps[1].pin_label(), "tag=v1.2.3");
        assert_eq!(deps[2].rev.as_deref(), Some("abc123"));
        assert_eq!(deps[2].pin_label(), "rev=abc123");
        let paths = parse_path_deps(t);
        assert_eq!(paths.len(), 1);
        assert_eq!(paths[0].name, "helper");
    }

    #[test]
    fn quoted_field_helper() {
        let line = r#""tool" = { git = "https://x/y.git", tag = "v0.1.0" }"#;
        assert_eq!(
            toml_quoted_field(line, "git").as_deref(),
            Some("https://x/y.git")
        );
        assert_eq!(toml_quoted_field(line, "tag").as_deref(), Some("v0.1.0"));
        assert!(toml_quoted_field(line, "rev").is_none());
    }

    #[test]
    fn audit_parses_lock_advisories_and_license_policy() {
        let lock = r#"
version = 1

[[package]]
name = "app"
version = "0.1.0"
source = "path"

[[package]]
name = "util"
version = "1.2.0"
source = "registry"
"#;
        let pkgs = parse_lock_packages(lock);
        assert_eq!(pkgs.len(), 2);
        assert_eq!(pkgs[1].name, "util");
        assert!(audit_version_matches("1.2.0", Some("<=1.2.3")));
        assert!(!audit_version_matches("1.2.4", Some("<=1.2.3")));

        let advisories = parse_advisories(
            r#"
[[advisory]]
id = "CVE-0001"
name = "util"
version = "<=1.2.3"
severity = "high"
"#,
        );
        assert_eq!(advisories.len(), 1);
        assert_eq!(advisories[0].id, "CVE-0001");
        assert_eq!(advisories[0].version.as_deref(), Some("<=1.2.3"));

        let policy = parse_license_policy(
            r#"
allow = ["MIT", "Apache-2.0"]
deny = ["GPL-3.0"]

[licenses]
app = "MIT"
util = "Apache-2.0"
"#,
        );
        assert!(policy.allow.contains("MIT"));
        assert!(policy.deny.contains("GPL-3.0"));
        assert_eq!(
            policy.packages.get("util").map(String::as_str),
            Some("Apache-2.0")
        );
    }
}

#[cfg(test)]
mod doc_tests {
    use super::run_doc;
    use std::env;
    use std::fs;

    #[test]
    fn doc_generates_runnable_examples_and_search_index() {
        let dir = env::temp_dir().join(format!("mako_doc_test_{}", std::process::id()));
        let out = dir.join("api");
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("demo.mko"),
            r#"
struct User {
    name: string
}

fn hello(name: string) -> string {
    name
}

fn main() {
    print(hello("Ada"))
}
"#,
        )
        .unwrap();

        run_doc(&dir, &out).unwrap();
        let index = fs::read_to_string(out.join("index.md")).unwrap();
        let examples = fs::read_to_string(out.join("examples.md")).unwrap();
        let search = fs::read_to_string(out.join("search-index.json")).unwrap();
        assert!(index.contains("Runnable examples"));
        assert!(examples.contains("mako run"));
        assert!(search.contains(r#""schema":"mako.doc.search.v1""#));
        assert!(search.contains(r#""name":"hello""#));
        assert!(search.contains(r#""kind":"struct""#));
        let _ = fs::remove_dir_all(&dir);
    }
}

#[cfg(test)]
mod workspace_parse_tests {
    use super::parse_workspace_members;

    #[test]
    fn members_single_line() {
        let t = r#"
[workspace]
members = ["core", "helper", "app"]
"#;
        assert_eq!(parse_workspace_members(t), vec!["core", "helper", "app"]);
    }

    #[test]
    fn members_multiline() {
        let t = r#"
[workspace]
members = [
  "a",
  "b",
]
"#;
        assert_eq!(parse_workspace_members(t), vec!["a", "b"]);
    }

    #[test]
    fn no_workspace() {
        let t = "name = \"x\"\n";
        assert!(parse_workspace_members(t).is_empty());
    }
}

#[cfg(test)]
mod test_filter_tests {
    use super::test_name_matches;

    #[test]
    fn exact_and_glob() {
        // A plain filter matches by exact name (no substring surprises):
        assert!(test_name_matches("TestAdd", "TestAdd"));
        assert!(!test_name_matches("TestAdd", "Add"));
        assert!(!test_name_matches("TestAddTable", "TestAdd"));
        assert!(!test_name_matches("TestAdd", "Subs"));
        // Globs and regex opt into fuzzy matching:
        assert!(test_name_matches("TestAddTable", "TestAdd*"));
        assert!(test_name_matches("TestSubs", "Test?ubs"));
        assert!(!test_name_matches("TestAdd", "TestSubs*"));
    }

    #[test]
    fn slash_regex() {
        assert!(test_name_matches("TestAdd", "/^TestAdd$/"));
        assert!(!test_name_matches("TestAddTable", "/^TestAdd$/"));
        assert!(test_name_matches("TestAdd", "/Add|Mul/"));
        assert!(test_name_matches("TestMul", "/Add|Mul/"));
        assert!(!test_name_matches("TestSubs", "/Add|Mul/"));
        assert!(test_name_matches("TestAddTable", "/TestAdd.+/"));
        assert!(!test_name_matches("TestAdd", "/[/")); // invalid → no match
    }
}

#[cfg(test)]
mod metadata_tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_mko(name: &str, src: &str) -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!("mako-{name}-{nanos}.mko"));
        fs::write(&path, src).unwrap();
        path
    }

    #[test]
    fn check_json_reports_success_and_symbol_count() {
        let path = temp_mko("json-ok", "fn main() {\n  print(\"ok\")\n}\n");
        let (ok, report) = check_file_json_report(&path);
        let _ = fs::remove_file(&path);
        assert!(ok, "{report}");
        assert!(report.contains(r#""ok":true"#));
        assert!(report.contains(r#""diagnostics":[]"#));
        assert!(report.contains(r#""symbols":1"#));
    }

    #[test]
    fn check_json_reports_parse_error_location() {
        let path = temp_mko("json-err", "fn main( {\n}\n");
        let (ok, report) = check_file_json_report(&path);
        let _ = fs::remove_file(&path);
        assert!(!ok);
        assert!(report.contains(r#""ok":false"#));
        assert!(report.contains(r#""severity":"error"#));
        assert!(report.contains(r#""line":"#));
        assert!(report.contains(r#""column":"#));
    }

    #[test]
    fn metadata_call_collector_finds_direct_calls() {
        let path = temp_mko(
            "metadata-calls",
            "fn main() {\n  print(\"ok\")\n  helper()\n}\nfn helper() {}\n",
        );
        let program = parse_file(&path).unwrap();
        let _ = fs::remove_file(&path);
        let mut calls = Vec::new();
        let main = program
            .items
            .iter()
            .find_map(|item| match item {
                Item::Fn(f) if f.name == "main" => Some(f),
                _ => None,
            })
            .unwrap();
        collect_calls_block(&main.body, &mut calls);
        calls.sort();
        calls.dedup();
        assert_eq!(calls, vec!["helper".to_string(), "print".to_string()]);
    }

    #[test]
    fn api_surface_allows_added_symbols() {
        let old = temp_mko(
            "api-old",
            "fn add(a: int, b: int) -> int { return a + b }\n",
        );
        let new = temp_mko(
            "api-new",
            "fn add(a: int, b: int) -> int { return a + b }\nfn sub(a: int, b: int) -> int { return a - b }\n",
        );
        let old_surface = api_surface(&old).unwrap();
        let new_surface = api_surface(&new).unwrap();
        let _ = fs::remove_file(&old);
        let _ = fs::remove_file(&new);
        assert_eq!(old_surface.get("add"), new_surface.get("add"));
        assert!(new_surface.contains_key("sub"));
    }

    #[test]
    fn api_surface_detects_changed_signature() {
        let old = temp_mko(
            "api-old-change",
            "fn add(a: int, b: int) -> int { return a + b }\n",
        );
        let new = temp_mko(
            "api-new-change",
            "fn add(a: int64, b: int64) -> int64 { return a + b }\n",
        );
        let old_surface = api_surface(&old).unwrap();
        let new_surface = api_surface(&new).unwrap();
        let _ = fs::remove_file(&old);
        let _ = fs::remove_file(&new);
        assert_ne!(old_surface.get("add"), new_surface.get("add"));
    }
}

/// Discover `*_test.mko`, compile each with a category-aware harness, run, report.
pub fn run_tests(
    path: &Path,
    run_filter: Option<&str>,
    verbose: bool,
    coverage: bool,
    compile_and_run_tests: &dyn Fn(&Path, &Program, &[String]) -> Result<(), ()>,
    legacy_main_run: &dyn Fn(&Path) -> Result<(), ()>,
) -> Result<(), ()> {
    let files = collect_mako_files(path);
    let source_files = files.iter().filter(|f| !is_test_file(f)).count();
    let tests: Vec<_> = files.into_iter().filter(|f| is_test_file(f)).collect();
    if tests.is_empty() {
        eprintln!("mako test: no *_test.mko under {}", path.display());
        eprintln!("  hint: add examples/testing/add_test.mko with fn TestAdd()");
        return Err(());
    }
    let mut failed = 0;
    let mut passed = 0;
    let mut skipped_files = 0;
    let mut matched_functions = 0usize;
    let mut category_counts: BTreeMap<&'static str, usize> = BTreeMap::new();
    if let Some(f) = run_filter {
        if verbose {
            println!("mako test: {} (-run {} -v)", path.display(), f);
        } else {
            println!("mako test: {} (-run {})", path.display(), f);
        }
    } else if verbose {
        println!("mako test: {} (-v)", path.display());
    } else {
        println!("mako test: {}", path.display());
    }
    for t in &tests {
        print!("=== {} ===\n", t.display());
        let t0 = Instant::now();
        match load_test_package(t) {
            Ok((program, names)) => {
                let names: Vec<String> = if let Some(filt) = run_filter {
                    names
                        .into_iter()
                        .filter(|n| test_name_matches(n, filt))
                        .collect()
                } else {
                    names
                };
                if names.is_empty() {
                    println!("skip  {} (no test function matched -run)\n", t.display());
                    skipped_files += 1;
                    continue;
                }
                matched_functions += names.len();
                for name in &names {
                    if let Some(kind) = test_category(name) {
                        *category_counts.entry(kind).or_insert(0) += 1;
                    }
                }
                if verbose {
                    println!("run: {}", names.join(", "));
                }
                match compile_and_run_tests(t, &program, &names) {
                    Ok(()) => {
                        println!(
                            "ok  {} ({:.0}ms)\n",
                            t.display(),
                            t0.elapsed().as_secs_f64() * 1000.0
                        );
                        passed += 1;
                    }
                    Err(()) => {
                        println!("FAIL  {}\n", t.display());
                        failed += 1;
                    }
                }
            }
            Err(e) if e.contains("no Test/") => {
                if run_filter.is_some() {
                    println!("skip  {} (legacy main; -run ignores)\n", t.display());
                    skipped_files += 1;
                    continue;
                }
                match legacy_main_run(t) {
                    Ok(()) => {
                        println!(
                            "ok  {} (legacy main, {:.0}ms)\n",
                            t.display(),
                            t0.elapsed().as_secs_f64() * 1000.0
                        );
                        passed += 1;
                    }
                    Err(()) => {
                        println!("FAIL  {}\n", t.display());
                        failed += 1;
                    }
                }
            }
            Err(e) => {
                eprintln!("error: {e}");
                println!("FAIL  {}\n", t.display());
                failed += 1;
            }
        }
    }
    if skipped_files > 0 {
        println!("mako test: {passed} passed, {failed} failed, {skipped_files} skipped");
    } else {
        println!("mako test: {passed} passed, {failed} failed");
    }
    if coverage {
        let covered = tests.len();
        let total = source_files + covered;
        let pct = if total == 0 {
            100.0
        } else {
            (covered as f64 / total as f64) * 100.0
        };
        let cats = ["unit", "fuzz", "property", "snapshot", "mock", "fixture"]
            .iter()
            .map(|k| format!("{k}={}", category_counts.get(k).copied().unwrap_or(0)))
            .collect::<Vec<_>>()
            .join(" ");
        println!(
            "mako coverage: source_files={} test_files={} test_functions={} file_coverage={:.1}% {}",
            source_files,
            tests.len(),
            matched_functions,
            pct,
            cats
        );
    }
    if failed > 0 {
        Err(())
    } else {
        Ok(())
    }
}

pub fn run_bench(
    path: &Path,
    json: bool,
    build_and_run: &dyn Fn(&Path) -> Result<(), ()>,
) -> Result<(), ()> {
    let files = collect_mako_files(path);
    let benches: Vec<_> = files
        .into_iter()
        .filter(|f| {
            let n = f.file_name().and_then(|x| x.to_str()).unwrap_or("");
            n.starts_with("bench_") || n.contains("bench")
        })
        .collect();
    if benches.is_empty() {
        eprintln!("mako bench: no bench_*.mko under {}", path.display());
        return Err(());
    }
    let mut json_items = Vec::new();
    let mut failed = false;
    for b in &benches {
        let t0 = Instant::now();
        if !json {
            print!("bench {} ... ", b.display());
        }
        match build_and_run(b) {
            Ok(()) => {
                let ms = t0.elapsed().as_secs_f64() * 1000.0;
                if json {
                    json_items.push(format!(
                        r#"{{"file":"{}","ok":true,"wallMs":{ms:.3}}}"#,
                        json_escape(&b.display().to_string())
                    ));
                } else {
                    println!("{ms:.1}ms wall (compile+run)");
                }
            }
            Err(()) => {
                failed = true;
                let ms = t0.elapsed().as_secs_f64() * 1000.0;
                if json {
                    json_items.push(format!(
                        r#"{{"file":"{}","ok":false,"wallMs":{ms:.3}}}"#,
                        json_escape(&b.display().to_string())
                    ));
                } else {
                    println!("FAILED");
                }
            }
        }
    }
    if json {
        println!(
            r#"{{"schema":"mako.bench.v1","path":"{}","benches":[{}]}}"#,
            json_escape(&path.display().to_string()),
            json_items.join(",")
        );
    }
    if failed {
        Err(())
    } else {
        Ok(())
    }
}

#[derive(Debug, Clone)]
struct AuditPackage {
    name: String,
    version: String,
}

#[derive(Debug, Clone, Default)]
struct Advisory {
    id: String,
    name: String,
    version: Option<String>,
    severity: String,
}

#[derive(Debug, Clone, Default)]
struct LicensePolicy {
    allow: BTreeSet<String>,
    deny: BTreeSet<String>,
    packages: BTreeMap<String, String>,
}

fn parse_lock_packages(src: &str) -> Vec<AuditPackage> {
    let mut packages = Vec::new();
    let mut name = String::new();
    let mut version = String::new();
    let flush = |name: &mut String, version: &mut String, packages: &mut Vec<AuditPackage>| {
        if !name.is_empty() {
            packages.push(AuditPackage {
                name: std::mem::take(name),
                version: std::mem::take(version),
            });
        }
    };
    for line in src.lines() {
        let t = line.trim();
        if t == "[[package]]" {
            flush(&mut name, &mut version, &mut packages);
        } else if let Some(v) = t.strip_prefix("name = ") {
            name = v.trim().trim_matches('"').to_string();
        } else if let Some(v) = t.strip_prefix("version = ") {
            version = v.trim().trim_matches('"').to_string();
        }
    }
    flush(&mut name, &mut version, &mut packages);
    packages
}

fn parse_string_array(line: &str, key: &str) -> BTreeSet<String> {
    let Some((k, rest)) = line.split_once('=') else {
        return BTreeSet::new();
    };
    if k.trim() != key {
        return BTreeSet::new();
    }
    let rest = rest.trim();
    if !rest.starts_with('[') || !rest.ends_with(']') {
        return BTreeSet::new();
    }
    rest.trim_matches(|c| c == '[' || c == ']')
        .split(',')
        .map(|s| s.trim().trim_matches('"').to_string())
        .filter(|s| !s.is_empty())
        .collect()
}

fn parse_advisories(src: &str) -> Vec<Advisory> {
    let mut advisories = Vec::new();
    let mut cur: Option<Advisory> = None;
    let flush = |cur: &mut Option<Advisory>, advisories: &mut Vec<Advisory>| {
        if let Some(mut a) = cur.take() {
            if !a.name.is_empty() {
                if a.id.is_empty() {
                    a.id = a.name.clone();
                }
                if a.severity.is_empty() {
                    a.severity = "unknown".into();
                }
                advisories.push(a);
            }
        }
    };
    for line in src.lines() {
        let t = line.trim();
        if t.is_empty() || t.starts_with('#') {
            continue;
        }
        if t == "[[advisory]]" {
            flush(&mut cur, &mut advisories);
            cur = Some(Advisory::default());
            continue;
        }
        let Some(a) = cur.as_mut() else {
            continue;
        };
        if let Some(v) = toml_quoted_field(t, "id") {
            a.id = v;
        } else if let Some(v) = toml_quoted_field(t, "name") {
            a.name = v;
        } else if let Some(v) = toml_quoted_field(t, "version") {
            a.version = Some(v);
        } else if let Some(v) = toml_quoted_field(t, "severity") {
            a.severity = v;
        }
    }
    flush(&mut cur, &mut advisories);
    advisories
}

fn parse_license_policy(src: &str) -> LicensePolicy {
    let mut policy = LicensePolicy::default();
    let mut section = "";
    for line in src.lines() {
        let t = line.trim();
        if t.is_empty() || t.starts_with('#') {
            continue;
        }
        if t == "[licenses]" || t == "[packages]" {
            section = t;
            continue;
        }
        if section.is_empty() {
            policy.allow.extend(parse_string_array(t, "allow"));
            policy.deny.extend(parse_string_array(t, "deny"));
        } else if section == "[licenses]" || section == "[packages]" {
            if let Some((name, license)) = t.split_once('=') {
                let name = name.trim().trim_matches('"').to_string();
                let license = license.trim().trim_matches('"').to_string();
                if !name.is_empty() && !license.is_empty() {
                    policy.packages.insert(name, license);
                }
            }
        }
    }
    policy
}

fn audit_version_matches(version: &str, req: Option<&str>) -> bool {
    let Some(req) = req.map(str::trim).filter(|r| !r.is_empty() && *r != "*") else {
        return true;
    };
    let Some(ver) = parse_semver(version) else {
        return version == req;
    };
    let cmp = |op: &str| -> Option<bool> {
        let target = parse_semver(req.strip_prefix(op)?.trim())?;
        Some(match op {
            "<" => ver < target,
            "<=" => ver <= target,
            ">" => ver > target,
            ">=" => ver >= target,
            _ => false,
        })
    };
    cmp("<=")
        .or_else(|| cmp(">="))
        .or_else(|| cmp("<"))
        .or_else(|| cmp(">"))
        .unwrap_or_else(|| version_satisfies(version, req))
}

pub fn pkg_audit(path: &Path) -> Result<(), ()> {
    let cve = path.join("mako-cve.toml");
    let license = path.join("mako-license.toml");
    let lock = path.join("mako.lock");
    if !lock.exists() {
        eprintln!("mako pkg audit: no mako.lock — run `mako pkg lock` first");
        return Err(());
    }
    let lock_src = fs::read_to_string(&lock).unwrap_or_default();
    let packages = parse_lock_packages(&lock_src);
    let mut failures = 0;

    if cve.exists() {
        let advisories = parse_advisories(&fs::read_to_string(&cve).unwrap_or_default());
        for pkg in &packages {
            for adv in &advisories {
                if adv.name == pkg.name
                    && audit_version_matches(&pkg.version, adv.version.as_deref())
                {
                    eprintln!(
                        "advisory hit: {} {} matches {} ({})",
                        pkg.name, pkg.version, adv.id, adv.severity
                    );
                    failures += 1;
                }
            }
        }
    } else {
        println!("mako pkg audit: note — no mako-cve.toml; 0 advisories checked");
        println!("  create mako-cve.toml with [[advisory]] name=... version=... id=...");
    }

    if license.exists() {
        let policy = parse_license_policy(&fs::read_to_string(&license).unwrap_or_default());
        for pkg in &packages {
            let Some(lic) = policy.packages.get(&pkg.name) else {
                eprintln!("license missing: {} {}", pkg.name, pkg.version);
                failures += 1;
                continue;
            };
            if policy.deny.contains(lic) {
                eprintln!("license denied: {} {} uses {}", pkg.name, pkg.version, lic);
                failures += 1;
            }
            if !policy.allow.is_empty() && !policy.allow.contains(lic) {
                eprintln!(
                    "license not allowed: {} {} uses {}",
                    pkg.name, pkg.version, lic
                );
                failures += 1;
            }
        }
    } else {
        println!("mako pkg audit: note — no mako-license.toml; 0 license policies checked");
        println!("  create mako-license.toml with allow=[...] and [licenses] entries");
    }

    if failures == 0 {
        println!(
            "mako pkg audit: ok ({} locked package(s), vulnerability/license policy clean)",
            packages.len()
        );
        Ok(())
    } else {
        eprintln!("mako pkg audit: {failures} issue(s)");
        Err(())
    }
}
