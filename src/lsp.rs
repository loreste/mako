//! Minimal JSON-RPC language server (stdio).
//! Supports: initialize, shutdown, exit, hover, completion (keywords), code actions,
//! didOpen/didChange → textDocument/publishDiagnostics (lex/parse/typecheck),
//! textDocument/definition (incl. cross-file import), textDocument/documentSymbol.

use crate::desugar;
use crate::lexer::{Lexer, TokenKind};
use crate::parser::Parser;
use crate::types::TypeChecker;
use std::collections::HashMap;
use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};

const KEYWORDS: &[&str] = &[
    "actor",
    "and",
    "arena",
    "as",
    "break",
    "const",
    "continue",
    "crew",
    "default",
    "defer",
    "else",
    "enum",
    "extern",
    "false",
    "fan",
    "fn",
    "for",
    "hold",
    "if",
    "import",
    "in",
    "interface",
    "join",
    "kick",
    "let",
    "match",
    "mut",
    "not",
    "or",
    "range",
    "receive",
    "return",
    "select",
    "share",
    "struct",
    "timeout",
    "true",
    "while",
];

fn read_message(stdin: &mut impl BufRead) -> io::Result<Option<String>> {
    let mut content_length: Option<usize> = None;
    loop {
        let mut line = String::new();
        let n = stdin.read_line(&mut line)?;
        if n == 0 {
            return Ok(None);
        }
        let trimmed = line.trim_end();
        if trimmed.is_empty() {
            break;
        }
        let lower = trimmed.to_ascii_lowercase();
        if let Some(rest) = lower.strip_prefix("content-length:") {
            content_length = rest.trim().parse().ok();
        }
    }
    let len = content_length
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing Content-Length"))?;
    let mut buf = vec![0u8; len];
    stdin.read_exact(&mut buf)?;
    Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
}

fn write_message(stdout: &mut impl Write, body: &str) -> io::Result<()> {
    write!(stdout, "Content-Length: {}\r\n\r\n{}", body.len(), body)?;
    stdout.flush()
}

fn json_get_str<'a>(obj: &'a str, key: &str) -> Option<&'a str> {
    let pat = format!("\"{key}\"");
    let i = obj.find(&pat)?;
    let after = &obj[i + pat.len()..];
    let colon = after.find(':')?;
    let rest = after[colon + 1..].trim_start();
    if let Some(stripped) = rest.strip_prefix('"') {
        let end = stripped.find('"')?;
        Some(&stripped[..end])
    } else {
        None
    }
}

fn json_unescape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => out.push('\n'),
                Some('r') => out.push('\r'),
                Some('t') => out.push('\t'),
                Some('"') => out.push('"'),
                Some('\\') => out.push('\\'),
                Some('u') => {
                    // skip \uXXXX roughly
                    for _ in 0..4 {
                        let _ = chars.next();
                    }
                }
                Some(other) => out.push(other),
                None => {}
            }
        } else {
            out.push(c);
        }
    }
    out
}

fn json_get_id(obj: &str) -> String {
    if let Some(i) = obj.find("\"id\"") {
        let after = &obj[i + 4..];
        if let Some(colon) = after.find(':') {
            let rest = after[colon + 1..].trim_start();
            if let Some(stripped) = rest.strip_prefix('"') {
                if let Some(end) = stripped.find('"') {
                    return format!("\"{}\"", &stripped[..end]);
                }
            } else {
                let num: String = rest
                    .chars()
                    .take_while(|c| c.is_ascii_digit() || *c == '-')
                    .collect();
                if !num.is_empty() {
                    return num;
                }
            }
        }
    }
    "null".into()
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn diagnose(src: &str) -> Vec<(u32, u32, u32, u32, String)> {
    // LSP uses 0-based line/character. Lexer/parser use 1-based line/col.
    let tokens = match Lexer::new(src).tokenize() {
        Ok(t) => t,
        Err(e) => {
            let msg = format!("{e}");
            let (line, col) = parse_loc_from_msg(&msg).unwrap_or((1, 1));
            let sl = line.saturating_sub(1) as u32;
            let sc = col.saturating_sub(1) as u32;
            return vec![(sl, sc, sl, sc + 1, msg)];
        }
    };
    let program = match Parser::new(tokens).parse() {
        Ok(p) => p,
        Err(e) => {
            let msg = format!("{e}");
            let (line, col) = parse_loc_from_msg(&msg).unwrap_or((1, 1));
            let sl = line.saturating_sub(1) as u32;
            let sc = col.saturating_sub(1) as u32;
            return vec![(sl, sc, sl, sc + 1, msg)];
        }
    };
    let program = desugar::desugar(program);
    if let Err(e) = TypeChecker::new().check(&program) {
        let msg = format!("{e}");
        let (line, col) = match &e {
            crate::types::TypeError::At { line, col, .. } if *line > 0 => (*line, *col),
            _ => parse_loc_from_msg(&msg).unwrap_or((0, 0)),
        };
        if line > 0 {
            let sl = line.saturating_sub(1) as u32;
            let sc = col.saturating_sub(1) as u32;
            return vec![(sl, sc, sl, sc.saturating_add(1), msg)];
        }
        // Cheap span: locate `` `name` `` from "cannot find `name`" in source.
        if let Some(name) = extract_backtick_name(&msg) {
            if let Some((sl, sc, el, ec)) = find_ident_span(src, &name) {
                return vec![(sl, sc, el, ec, msg)];
            }
        }
        return vec![(0, 0, 0, 1, msg)];
    }
    Vec::new()
}

fn extract_backtick_name(msg: &str) -> Option<String> {
    let start = msg.find('`')?;
    let rest = &msg[start + 1..];
    let end = rest.find('`')?;
    let name = &rest[..end];
    if name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') && !name.is_empty() {
        Some(name.to_string())
    } else {
        None
    }
}

fn find_ident_span(src: &str, name: &str) -> Option<(u32, u32, u32, u32)> {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return None;
    };
    for t in tokens {
        if let TokenKind::Ident(ref n) = t.kind {
            if n == name {
                let sl = t.line.saturating_sub(1) as u32;
                let sc = t.col.saturating_sub(1) as u32;
                return Some((sl, sc, sl, sc + name.len() as u32));
            }
        }
    }
    None
}

/// Parse `at L:C` or `at L:C:` style locations from error Display strings.
fn parse_loc_from_msg(msg: &str) -> Option<(usize, usize)> {
    // "parse error at 2:1: ..." or "unexpected character 'x' at 1:3"
    for marker in [" at ", "at "] {
        if let Some(i) = msg.rfind(marker) {
            let rest = &msg[i + marker.len()..];
            let mut nums = rest
                .split(|c: char| !c.is_ascii_digit())
                .filter(|s| !s.is_empty());
            let line: usize = nums.next()?.parse().ok()?;
            let col: usize = nums.next()?.parse().ok()?;
            return Some((line, col));
        }
    }
    // "parse error at {line}:{col}:"
    if let Some(i) = msg.find("parse error at ") {
        let rest = &msg[i + "parse error at ".len()..];
        let mut parts = rest.split(|c: char| c == ':' || c == ' ');
        let line: usize = parts.next()?.parse().ok()?;
        let col: usize = parts.next()?.parse().ok()?;
        return Some((line, col));
    }
    None
}

/// Collect `fn name` definitions: (name, 0-based line, 0-based col, name_len).
fn collect_fn_defs(src: &str) -> Vec<(String, u32, u32, u32)> {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut i = 0;
    while i + 1 < tokens.len() {
        if matches!(tokens[i].kind, crate::lexer::TokenKind::Fn) {
            if let crate::lexer::TokenKind::Ident(ref name) = tokens[i + 1].kind {
                let line = tokens[i + 1].line.saturating_sub(1) as u32;
                let col = tokens[i + 1].col.saturating_sub(1) as u32;
                out.push((name.clone(), line, col, name.len() as u32));
            }
        }
        i += 1;
    }
    out
}

/// Collect fn signature labels: (name, "name(a: int, b: int) -> int", param_count).
fn collect_fn_sigs(src: &str) -> Vec<(String, String, usize)> {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut i = 0;
    while i + 1 < tokens.len() {
        if matches!(tokens[i].kind, TokenKind::Fn) {
            if let TokenKind::Ident(ref name) = tokens[i + 1].kind {
                let mut j = i + 2;
                if j >= tokens.len() || !matches!(tokens[j].kind, TokenKind::LParen) {
                    i += 1;
                    continue;
                }
                j += 1;
                let mut params: Vec<String> = Vec::new();
                let mut cur = String::new();
                let mut depth = 1i32;
                while j < tokens.len() && depth > 0 {
                    match &tokens[j].kind {
                        TokenKind::LParen => {
                            depth += 1;
                            cur.push('(');
                        }
                        TokenKind::RParen => {
                            depth -= 1;
                            if depth == 0 {
                                if !cur.trim().is_empty() {
                                    params.push(cur.trim().to_string());
                                }
                                break;
                            }
                            cur.push(')');
                        }
                        TokenKind::Comma if depth == 1 => {
                            if !cur.trim().is_empty() {
                                params.push(cur.trim().to_string());
                            }
                            cur.clear();
                        }
                        TokenKind::Ident(s) => {
                            if !cur.is_empty()
                                && !cur.ends_with(' ')
                                && !cur.ends_with(':')
                                && !cur.ends_with(',')
                                && !cur.ends_with('(')
                                && !cur.ends_with('[')
                            {
                                cur.push(' ');
                            }
                            cur.push_str(s);
                        }
                        TokenKind::Colon => cur.push_str(": "),
                        TokenKind::LBracket => cur.push('['),
                        TokenKind::RBracket => cur.push(']'),
                        TokenKind::Arrow => cur.push_str(" -> "),
                        other => {
                            let s = format!("{other}");
                            if s.len() <= 2 {
                                cur.push_str(&s);
                            }
                        }
                    }
                    j += 1;
                }
                let mut ret = String::new();
                j += 1; // past )
                if j < tokens.len() && matches!(tokens[j].kind, TokenKind::Arrow) {
                    j += 1;
                    while j < tokens.len() {
                        match &tokens[j].kind {
                            TokenKind::Ident(s) => {
                                if !ret.is_empty() {
                                    ret.push(' ');
                                }
                                ret.push_str(s);
                                break;
                            }
                            TokenKind::LBrace | TokenKind::Assign => break,
                            _ => {}
                        }
                        j += 1;
                    }
                }
                let label = if ret.is_empty() {
                    format!("{name}({})", params.join(", "))
                } else {
                    format!("{name}({}) -> {ret}", params.join(", "))
                };
                out.push((name.clone(), label, params.len()));
            }
        }
        i += 1;
    }
    out
}

/// textDocument/signatureHelp — call-site fn label from same-file defs (Partial).
fn signature_help(src: &str, line: u32, character: u32) -> String {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return "null".into();
    };
    // Find token index at/before cursor
    let mut at = 0usize;
    for (i, t) in tokens.iter().enumerate() {
        let tl = t.line.saturating_sub(1) as u32;
        let tc = t.col.saturating_sub(1) as u32;
        if tl < line || (tl == line && tc <= character) {
            at = i;
        } else {
            break;
        }
    }
    // Walk back to find open call: Ident LParen … cursor
    let mut depth = 0i32;
    let mut commas = 0usize;
    let mut fname: Option<String> = None;
    let mut i = at;
    loop {
        match &tokens[i].kind {
            TokenKind::RParen => depth += 1,
            TokenKind::LParen => {
                if depth == 0 {
                    if i > 0 {
                        if let TokenKind::Ident(ref n) = tokens[i - 1].kind {
                            fname = Some(n.clone());
                        }
                    }
                    break;
                }
                depth -= 1;
            }
            TokenKind::Comma if depth == 0 => commas += 1,
            _ => {}
        }
        if i == 0 {
            break;
        }
        i -= 1;
    }
    let Some(name) = fname else {
        return "null".into();
    };
    for (n, label, _pc) in collect_fn_sigs(src) {
        if n == name {
            let active = commas as u32;
            return format!(
                r#"{{"signatures":[{{"label":"{}","parameters":[]}}],"activeSignature":0,"activeParameter":{active}}}"#,
                json_escape(&label)
            );
        }
    }
    // Builtins seed
    let builtins: &[(&str, &str)] = &[
        ("print", "print(s: string)"),
        ("print_int", "print_int(n: int)"),
        ("assert_eq", "assert_eq(a, b)"),
        (
            "pb_encode_simple",
            "pb_encode_simple(name: string, id: int) -> string",
        ),
        (
            "http2_headers_frame",
            "http2_headers_frame(stream: int, block: string, flags: int) -> string",
        ),
    ];
    for (n, label) in builtins {
        if *n == name {
            return format!(
                r#"{{"signatures":[{{"label":"{label}","parameters":[]}}],"activeSignature":0,"activeParameter":{commas}}}"#
            );
        }
    }
    "null".into()
}

/// Collect `struct Name` definitions (same shape as fn defs).
fn collect_struct_defs(src: &str) -> Vec<(String, u32, u32, u32)> {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut i = 0;
    while i + 1 < tokens.len() {
        if matches!(tokens[i].kind, TokenKind::Struct) {
            if let TokenKind::Ident(ref name) = tokens[i + 1].kind {
                let line = tokens[i + 1].line.saturating_sub(1) as u32;
                let col = tokens[i + 1].col.saturating_sub(1) as u32;
                out.push((name.clone(), line, col, name.len() as u32));
            }
        }
        i += 1;
    }
    out
}

/// LSP DocumentSymbol[] for top-level `fn` / `struct` (kind Function=12, Struct=23).
fn document_symbols(src: &str) -> String {
    let mut items = Vec::new();
    for (name, dl, dc, len) in collect_fn_defs(src) {
        let end = dc + len;
        items.push(format!(
            r#"{{"name":"{name}","kind":12,"range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{end}}}}},"selectionRange":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{end}}}}}}}"#
        ));
    }
    for (name, dl, dc, len) in collect_struct_defs(src) {
        let end = dc + len;
        items.push(format!(
            r#"{{"name":"{name}","kind":23,"range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{end}}}}},"selectionRange":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{end}}}}}}}"#
        ));
    }
    format!("[{}]", items.join(","))
}

/// workspace/symbol: search open docs + sibling `.mko` beside each open file (Partial).
fn workspace_symbols(docs: &HashMap<String, String>, query: &str) -> String {
    let q = query.to_ascii_lowercase();
    let mut items = Vec::new();
    let mut seen: HashMap<String, ()> = HashMap::new();

    let mut push = |uri: &str, name: &str, kind: u32, dl: u32, dc: u32, len: u32| {
        if !q.is_empty() && !name.to_ascii_lowercase().contains(&q) {
            return;
        }
        let key = format!("{uri}::{name}::{kind}");
        if seen.contains_key(&key) {
            return;
        }
        seen.insert(key, ());
        let end = dc + len;
        items.push(format!(
            r#"{{"name":"{name}","kind":{kind},"location":{{"uri":"{}","range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{end}}}}}}}}}"#,
            json_escape(uri)
        ));
    };

    for (uri, src) in docs {
        for (name, dl, dc, len) in collect_fn_defs(src) {
            push(uri, &name, 12, dl, dc, len);
        }
        for (name, dl, dc, len) in collect_struct_defs(src) {
            push(uri, &name, 23, dl, dc, len);
        }
        // Sibling .mko in same directory (capped — avoid huge dirs like /tmp)
        if let Some(path) = uri_to_path(uri) {
            if let Some(dir) = path.parent() {
                if let Ok(rd) = std::fs::read_dir(dir) {
                    let mut n = 0usize;
                    for ent in rd.flatten() {
                        if n >= 48 {
                            break;
                        }
                        let p = ent.path();
                        if p.extension().and_then(|e| e.to_str()) != Some("mko") {
                            continue;
                        }
                        n += 1;
                        let Ok(src2) = std::fs::read_to_string(&p) else {
                            continue;
                        };
                        let uri2 = format!("file://{}", p.display());
                        for (name, dl, dc, len) in collect_fn_defs(&src2) {
                            push(&uri2, &name, 12, dl, dc, len);
                        }
                        for (name, dl, dc, len) in collect_struct_defs(&src2) {
                            push(&uri2, &name, 23, dl, dc, len);
                        }
                    }
                }
            }
        }
    }
    format!("[{}]", items.join(","))
}

/// Collect identifier occurrences of `needle` as (0-based line, col, len).
fn collect_ident_refs(src: &str, needle: &str) -> Vec<(u32, u32, u32)> {
    if needle.is_empty() {
        return Vec::new();
    }
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for t in tokens {
        if let TokenKind::Ident(ref name) = t.kind {
            if name == needle {
                let line = t.line.saturating_sub(1) as u32;
                let col = t.col.saturating_sub(1) as u32;
                out.push((line, col, name.len() as u32));
            }
        }
    }
    out
}

/// Byte offset of (line, col) in src (0-based).
fn offset_at(src: &str, line: u32, character: u32) -> Option<usize> {
    let mut cur_line = 0u32;
    let mut cur_col = 0u32;
    for (i, ch) in src.char_indices() {
        if cur_line == line && cur_col == character {
            return Some(i);
        }
        if ch == '\n' {
            cur_line += 1;
            cur_col = 0;
        } else {
            cur_col += 1;
        }
    }
    if cur_line == line && cur_col == character {
        return Some(src.len());
    }
    None
}

fn is_fn_name(src: &str, name: &str) -> bool {
    collect_fn_defs(src).iter().any(|(n, _, _, _)| n == name)
}

fn resolve_import_path(base: &Path, imp_path: &str) -> PathBuf {
    if Path::new(imp_path).is_absolute() {
        PathBuf::from(imp_path)
    } else {
        base.join(imp_path)
    }
}

/// Apply same-file identifier rename; returns new source text.
fn apply_ident_rename(src: &str, old: &str, new_name: &str) -> String {
    let refs = collect_ident_refs(src, old);
    let mut edits: Vec<(usize, usize, String)> = Vec::new();
    for (dl, dc, len) in &refs {
        let Some(start) = offset_at(src, *dl, *dc) else {
            continue;
        };
        edits.push((start, start + *len as usize, new_name.to_string()));
    }
    edits.sort_by(|a, b| b.0.cmp(&a.0));
    let mut new_src = src.to_string();
    for (start, end, rep) in edits {
        new_src.replace_range(start..end, &rep);
    }
    new_src
}

fn full_doc_edit(uri: &str, old_src: &str, new_src: &str) -> String {
    format!(
        r#"{{"{}":[{{"range":{{"start":{{"line":0,"character":0}},"end":{{"line":{},"character":0}}}},"newText":"{}"}}]}}"#,
        json_escape(uri),
        old_src.lines().count(),
        json_escape(new_src)
    )
}

/// textDocument/prepareRename — same-file `fn` or imported fn name under cursor.
fn prepare_rename(uri: &str, src: &str, line: u32, character: u32) -> String {
    let word = word_at(src, line, character);
    if word.is_empty() {
        return "null".into();
    }
    let local = is_fn_name(src, &word);
    let mut imported = false;
    if !local {
        if let Some(path) = uri_to_path(uri) {
            let base = path.parent().unwrap_or(Path::new(".")).to_path_buf();
            for (imp_path, _) in collect_imports(src) {
                let target = resolve_import_path(&base, &imp_path);
                let Ok(imp_src) = std::fs::read_to_string(&target) else {
                    continue;
                };
                if is_fn_name(&imp_src, &word) {
                    imported = true;
                    break;
                }
            }
        }
    }
    if !local && !imported {
        return "null".into();
    }
    for (dl, dc, len) in collect_ident_refs(src, &word) {
        if dl == line && character >= dc && character <= dc + len {
            return format!(
                r#"{{"range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{}}}}},"placeholder":"{}"}}"#,
                dc + len,
                json_escape(&word)
            );
        }
    }
    if local {
        for (name, dl, dc, len) in collect_fn_defs(src) {
            if name == word {
                return format!(
                    r#"{{"range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{}}}}},"placeholder":"{}"}}"#,
                    dc + len,
                    json_escape(&word)
                );
            }
        }
    }
    "null".into()
}

/// textDocument/rename — same-file + imported definition files (Partial).
fn rename_symbol(uri: &str, src: &str, line: u32, character: u32, new_name: &str) -> String {
    let word = word_at(src, line, character);
    if word.is_empty() || new_name.is_empty() {
        return "null".into();
    }
    if !new_name
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_')
        || new_name.chars().next().is_some_and(|c| c.is_ascii_digit())
    {
        return "null".into();
    }
    let local = is_fn_name(src, &word);
    let mut change_parts: Vec<String> = Vec::new();

    if local {
        let new_src = apply_ident_rename(src, &word, new_name);
        change_parts.push(full_doc_edit(uri, src, &new_src));
        // Also update importers that pull this file? skip — rename from def site only updates this file
    } else {
        // Imported fn: rename in current file + defining import file(s)
        let new_src = apply_ident_rename(src, &word, new_name);
        change_parts.push(full_doc_edit(uri, src, &new_src));
        if let Some(path) = uri_to_path(uri) {
            let base = path.parent().unwrap_or(Path::new(".")).to_path_buf();
            for (imp_path, _) in collect_imports(src) {
                let target = resolve_import_path(&base, &imp_path);
                let Ok(imp_src) = std::fs::read_to_string(&target) else {
                    continue;
                };
                if !is_fn_name(&imp_src, &word) {
                    continue;
                }
                let target_uri = format!("file://{}", target.display());
                let new_imp = apply_ident_rename(&imp_src, &word, new_name);
                change_parts.push(full_doc_edit(&target_uri, &imp_src, &new_imp));
            }
        }
        if change_parts.len() < 2 {
            // no defining file found — still allow current-file-only if refs exist
            if collect_ident_refs(src, &word).is_empty() {
                return "null".into();
            }
        }
    }

    if change_parts.is_empty() {
        return "null".into();
    }
    // Merge into {"changes":{ uri: [...], uri2: [...] }}
    let inner = change_parts
        .iter()
        .map(|p| p.trim_start_matches('{').trim_end_matches('}').to_string())
        .collect::<Vec<_>>()
        .join(",");
    format!(r#"{{"changes":{{{inner}}}}}"#)
}

/// textDocument/references — same file + imported files (Partial).
fn find_references(uri: &str, src: &str, line: u32, character: u32) -> String {
    let word = word_at(src, line, character);
    if word.is_empty() {
        return "[]".into();
    }
    let mut locs = Vec::new();
    for (dl, dc, len) in collect_ident_refs(src, &word) {
        locs.push(location_json(uri, dl, dc, len));
    }
    if let Some(path) = uri_to_path(uri) {
        let base = path.parent().unwrap_or(Path::new(".")).to_path_buf();
        for (imp_path, _) in collect_imports(src) {
            let target = if Path::new(&imp_path).is_absolute() {
                PathBuf::from(&imp_path)
            } else {
                base.join(&imp_path)
            };
            let Ok(imp_src) = std::fs::read_to_string(&target) else {
                continue;
            };
            let target_uri = format!("file://{}", target.display());
            for (dl, dc, len) in collect_ident_refs(&imp_src, &word) {
                locs.push(location_json(&target_uri, dl, dc, len));
            }
        }
    }
    format!("[{}]", locs.join(","))
}

fn word_at(src: &str, line: u32, character: u32) -> String {
    let mut cur_line = 0u32;
    let mut cur_col = 0u32;
    let mut idx = src.len();
    for (i, ch) in src.char_indices() {
        if cur_line == line && cur_col == character {
            idx = i;
            break;
        }
        if ch == '\n' {
            cur_line += 1;
            cur_col = 0;
        } else {
            cur_col += 1;
        }
    }
    // expand to full identifier around idx
    let bytes = src.as_bytes();
    let mut start = idx.min(bytes.len());
    let mut end = start;
    while start > 0 {
        let c = bytes[start - 1] as char;
        if c.is_ascii_alphanumeric() || c == '_' {
            start -= 1;
        } else {
            break;
        }
    }
    while end < bytes.len() {
        let c = bytes[end] as char;
        if c.is_ascii_alphanumeric() || c == '_' {
            end += 1;
        } else {
            break;
        }
    }
    src[start..end].to_string()
}

fn goto_definition(uri: &str, src: &str, line: u32, character: u32) -> Option<String> {
    let word = word_at(src, line, character);
    if word.is_empty() {
        return None;
    }
    // Same-file first
    for (name, dl, dc, len) in collect_fn_defs(src) {
        if name == word {
            return Some(location_json(uri, dl, dc, len));
        }
    }
    // Cross-file via import graph (and `as` aliases: lib.add → lib__add in imported file as add)
    let path = uri_to_path(uri)?;
    let base = path.parent()?.to_path_buf();
    for (imp_path, alias) in collect_imports(src) {
        let target = if Path::new(&imp_path).is_absolute() {
            PathBuf::from(&imp_path)
        } else {
            base.join(&imp_path)
        };
        let Ok(imp_src) = std::fs::read_to_string(&target) else {
            continue;
        };
        let target_uri = format!("file://{}", target.display());
        let search = if let Some(a) = alias {
            // `lib.add` call site word is just `add` when on method; also support `lib__add`
            if word.starts_with(&format!("{a}__")) {
                word[a.len() + 2..].to_string()
            } else {
                word.clone()
            }
        } else {
            word.clone()
        };
        for (name, dl, dc, len) in collect_fn_defs(&imp_src) {
            if name == search || name == word {
                return Some(location_json(&target_uri, dl, dc, len));
            }
        }
        // Nested: if imported file itself imports, one level is enough for MVP Partial;
        // recurse one more hop for shallow graphs.
        let nested_base = target.parent().unwrap_or(Path::new(".")).to_path_buf();
        for (imp2, _) in collect_imports(&imp_src) {
            let t2 = if Path::new(&imp2).is_absolute() {
                PathBuf::from(&imp2)
            } else {
                nested_base.join(&imp2)
            };
            let Ok(src2) = std::fs::read_to_string(&t2) else {
                continue;
            };
            let uri2 = format!("file://{}", t2.display());
            for (name, dl, dc, len) in collect_fn_defs(&src2) {
                if name == search || name == word {
                    return Some(location_json(&uri2, dl, dc, len));
                }
            }
        }
    }
    None
}

fn location_json(uri: &str, dl: u32, dc: u32, len: u32) -> String {
    format!(
        r#"{{"uri":"{}","range":{{"start":{{"line":{dl},"character":{dc}}},"end":{{"line":{dl},"character":{}}}}}}}"#,
        json_escape(uri),
        dc + len
    )
}

/// Parse `import "./x.mko"` and `import "./x.mko" as foo` from source tokens.
fn collect_imports(src: &str) -> Vec<(String, Option<String>)> {
    let Ok(tokens) = Lexer::new(src).tokenize() else {
        return Vec::new();
    };
    let mut out = Vec::new();
    let mut i = 0;
    while i < tokens.len() {
        if matches!(tokens[i].kind, TokenKind::Import) {
            if i + 1 < tokens.len() {
                if let TokenKind::String(ref path) = tokens[i + 1].kind {
                    let mut alias = None;
                    if i + 3 < tokens.len() && matches!(tokens[i + 2].kind, TokenKind::As) {
                        if let TokenKind::Ident(ref a) = tokens[i + 3].kind {
                            alias = Some(a.clone());
                        }
                    }
                    out.push((path.clone(), alias));
                }
            }
        }
        i += 1;
    }
    out
}

fn publish_diagnostics(stdout: &mut impl Write, uri: &str, src: &str) -> io::Result<()> {
    let diags = diagnose(src);
    let mut arr = String::from("[");
    for (i, (sl, sc, el, ec, msg)) in diags.iter().enumerate() {
        if i > 0 {
            arr.push(',');
        }
        arr.push_str(&format!(
            r#"{{"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}},"severity":1,"source":"mako","message":"{}"}}"#,
            json_escape(msg)
        ));
    }
    arr.push(']');
    let body = format!(
        r#"{{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{{"uri":"{}","diagnostics":{arr}}}}}"#,
        json_escape(uri)
    );
    write_message(stdout, &body)
}

fn completion_items(prefix: &str) -> String {
    let mut items = Vec::new();
    for kw in KEYWORDS {
        if prefix.is_empty() || kw.starts_with(prefix) {
            items.push(format!(
                r#"{{"label":"{kw}","kind":14,"detail":"keyword"}}"#
            ));
        }
    }
    // builtins seed
    for b in [
        "print",
        "print_int",
        "assert",
        "assert_eq",
        "len",
        "append",
        "regex_match",
        "regex_find",
        "regex_capture",
        "sleep_ms",
        "now_ms",
        "now_ns",
        "exit",
    ] {
        if prefix.is_empty() || b.starts_with(prefix) {
            items.push(format!(r#"{{"label":"{b}","kind":3,"detail":"builtin"}}"#));
        }
    }
    format!("[{}]", items.join(","))
}

fn code_actions(has_diagnostics: bool) -> String {
    let mut actions = Vec::new();
    if has_diagnostics {
        actions.push(
            r#"{"title":"Mako: Check current file","kind":"quickfix","command":{"title":"Mako: Check current file","command":"mako.check"}}"#
                .to_string(),
        );
    }
    actions.push(
        r#"{"title":"Mako: Format current file","kind":"source.fixAll.mako","command":{"title":"Mako: Format current file","command":"mako.format"}}"#
            .to_string(),
    );
    actions.push(
        r#"{"title":"Mako: Run tests","kind":"source","command":{"title":"Mako: Run tests","command":"mako.test"}}"#
            .to_string(),
    );
    format!("[{}]", actions.join(","))
}

fn word_prefix_at(src: &str, line: u32, character: u32) -> String {
    let mut cur_line = 0u32;
    let mut cur_col = 0u32;
    let mut idx = src.len();
    for (i, ch) in src.char_indices() {
        if cur_line == line && cur_col == character {
            idx = i;
            break;
        }
        if ch == '\n' {
            cur_line += 1;
            cur_col = 0;
        } else {
            cur_col += 1;
        }
    }
    let before = &src[..idx.min(src.len())];
    let mut start = before.len();
    for (i, ch) in before.char_indices().rev() {
        if ch.is_ascii_alphanumeric() || ch == '_' {
            start = i;
        } else {
            break;
        }
    }
    before[start..].to_string()
}

fn parse_position(msg: &str) -> (u32, u32) {
    let line = msg
        .find("\"line\"")
        .and_then(|i| {
            let after = &msg[i + 6..];
            let colon = after.find(':')?;
            let rest = after[colon + 1..].trim_start();
            rest.chars()
                .take_while(|c| c.is_ascii_digit())
                .collect::<String>()
                .parse()
                .ok()
        })
        .unwrap_or(0);
    let character = msg
        .find("\"character\"")
        .and_then(|i| {
            let after = &msg[i + 11..];
            let colon = after.find(':')?;
            let rest = after[colon + 1..].trim_start();
            rest.chars()
                .take_while(|c| c.is_ascii_digit())
                .collect::<String>()
                .parse()
                .ok()
        })
        .unwrap_or(0);
    (line, character)
}

fn extract_text_document_text(msg: &str) -> Option<String> {
    // "text":"...."  (may contain escapes)
    let key = "\"text\"";
    let i = msg.find(key)?;
    let after = &msg[i + key.len()..];
    let colon = after.find(':')?;
    let rest = after[colon + 1..].trim_start();
    if !rest.starts_with('"') {
        return None;
    }
    let mut out = String::new();
    let mut chars = rest[1..].chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => out.push('\n'),
                Some('r') => out.push('\r'),
                Some('t') => out.push('\t'),
                Some('"') => out.push('"'),
                Some('\\') => out.push('\\'),
                Some(other) => out.push(other),
                None => break,
            }
        } else if c == '"' {
            break;
        } else {
            out.push(c);
        }
    }
    Some(out)
}

fn uri_to_path(uri: &str) -> Option<PathBuf> {
    let u = json_unescape(uri);
    let path = u.strip_prefix("file://")?;
    Some(PathBuf::from(path))
}

/// Run LSP on stdio until exit. Returns Ok(()) on clean shutdown.
pub fn run_stdio() -> io::Result<()> {
    let stdin = io::stdin();
    let mut stdin = stdin.lock();
    let stdout = io::stdout();
    let mut stdout = stdout.lock();
    let mut shutdown = false;
    let mut docs: HashMap<String, String> = HashMap::new();

    while let Some(msg) = read_message(&mut stdin)? {
        let method = json_get_str(&msg, "method").unwrap_or("");
        let id = json_get_id(&msg);

        match method {
            "initialize" => {
                let result = r#"{"capabilities":{"hoverProvider":true,"completionProvider":{"triggerCharacters":["."]},"textDocumentSync":{"openClose":true,"change":1},"definitionProvider":true,"documentSymbolProvider":true,"workspaceSymbolProvider":true,"referencesProvider":true,"codeActionProvider":true,"signatureHelpProvider":{"triggerCharacters":["(",","]},"renameProvider":{"prepareProvider":true}},"serverInfo":{"name":"mako-lsp","version":"0.4.6"}}"#;
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "initialized" => {}
            "textDocument/didOpen" => {
                if let Some(uri) = json_get_str(&msg, "uri") {
                    let text = extract_text_document_text(&msg).unwrap_or_default();
                    // Prefer disk if file:// and text empty
                    let src = if text.is_empty() {
                        uri_to_path(uri)
                            .and_then(|p| std::fs::read_to_string(p).ok())
                            .unwrap_or_default()
                    } else {
                        text
                    };
                    docs.insert(uri.to_string(), src.clone());
                    publish_diagnostics(&mut stdout, uri, &src)?;
                }
            }
            "textDocument/didChange" => {
                if let Some(uri) = json_get_str(&msg, "uri") {
                    // Full-document sync: last contentChanges[].text
                    let text = extract_text_document_text(&msg).unwrap_or_else(|| {
                        // Prefer the last "text" occurrence inside contentChanges
                        extract_text_document_text(&msg).unwrap_or_default()
                    });
                    // If extractor got the uri's unrelated field, fall back to scanning all texts —
                    // our extractor finds first "text"; for didChange the change text is typically last.
                    let text = {
                        let mut last = text;
                        let mut search = msg.as_str();
                        while let Some(i) = search.find("\"text\"") {
                            let slice = &search[i..];
                            if let Some(t) = extract_text_document_text(slice) {
                                last = t;
                            }
                            search = &search[i + 6..];
                        }
                        last
                    };
                    docs.insert(uri.to_string(), text.clone());
                    publish_diagnostics(&mut stdout, uri, &text)?;
                }
            }
            "textDocument/didClose" => {
                if let Some(uri) = json_get_str(&msg, "uri") {
                    docs.remove(uri);
                    let body = format!(
                        r#"{{"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{{"uri":"{}","diagnostics":[]}}}}"#,
                        json_escape(uri)
                    );
                    write_message(&mut stdout, &body)?;
                }
            }
            "textDocument/hover" => {
                let result = r#"{"contents":{"kind":"markdown","value":"**mako** LSP Partial\n\nDiagnostics on open/change · keyword completion · `mako check` for CLI."}}"#;
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/completion" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let prefix = word_prefix_at(src, line, character);
                let items = completion_items(&prefix);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{items}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/codeAction" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let has_diagnostics = !diagnose(src).is_empty();
                let result = code_actions(has_diagnostics);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/definition" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = match goto_definition(uri, src, line, character) {
                    Some(loc) => loc,
                    None => "null".into(),
                };
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/documentSymbol" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = document_symbols(src);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "workspace/symbol" => {
                let query = json_get_str(&msg, "query").unwrap_or("");
                let result = workspace_symbols(&docs, query);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/references" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = find_references(uri, src, line, character);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/signatureHelp" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = signature_help(src, line, character);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/prepareRename" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = prepare_rename(uri, src, line, character);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "textDocument/rename" => {
                let uri = json_get_str(&msg, "uri").unwrap_or("");
                let (line, character) = parse_position(&msg);
                let new_name = json_get_str(&msg, "newName").unwrap_or("");
                let src = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                let result = rename_symbol(uri, src, line, character, new_name);
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":{result}}}"#);
                write_message(&mut stdout, &body)?;
            }
            "shutdown" => {
                shutdown = true;
                let body = format!(r#"{{"jsonrpc":"2.0","id":{id},"result":null}}"#);
                write_message(&mut stdout, &body)?;
            }
            "exit" => {
                break;
            }
            "" if msg.contains("\"id\"") => {
                let body = format!(
                    r#"{{"jsonrpc":"2.0","id":{id},"error":{{"code":-32601,"message":"Method not found"}}}}"#
                );
                write_message(&mut stdout, &body)?;
            }
            _ => {}
        }
        if shutdown && method == "exit" {
            break;
        }
    }
    Ok(())
}
