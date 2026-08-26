//! Human-readable compiler diagnostics for Mako.

use std::io::{self, IsTerminal, Write};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)]
pub enum Severity {
    Error,
    Warning,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Span {
    pub line: usize,
    pub col: usize,
}

impl Span {
    pub fn new(line: usize, col: usize) -> Self {
        Self { line, col }
    }

    pub fn unknown() -> Self {
        Self { line: 0, col: 0 }
    }

    pub fn is_known(self) -> bool {
        self.line > 0
    }
}

pub fn infer_span_from_message(source: &str, message: &str) -> Option<Span> {
    for name in backtick_names(message) {
        if let Some(span) = find_word(source, &name) {
            return Some(span);
        }
    }
    if message.contains("cannot apply arithmetic")
        || message.contains("cannot compare")
        || message.contains("equality type mismatch")
        || message.contains("binary")
    {
        if let Some(span) = find_any(
            source,
            &["+", "-", "*", "/", "%", "==", "!=", "<=", ">=", "<", ">"],
        ) {
            return Some(span);
        }
    }
    if message.contains("return type mismatch") || message.contains("trailing expression") {
        if let Some(span) = last_expr_like_line(source) {
            return Some(span);
        }
    }
    None
}

fn backtick_names(message: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut rest = message;
    while let Some(start) = rest.find('`') {
        let after = &rest[start + 1..];
        let Some(end) = after.find('`') else {
            break;
        };
        let name = &after[..end];
        if !name.is_empty()
            && name
                .chars()
                .all(|c| c == '_' || c == '.' || c.is_ascii_alphanumeric())
        {
            out.push(name.to_string());
        }
        rest = &after[end + 1..];
    }
    out
}

fn find_word(source: &str, word: &str) -> Option<Span> {
    for (line_idx, line) in source.lines().enumerate() {
        let mut search_from = 0usize;
        while let Some(pos) = line[search_from..].find(word) {
            let col0 = search_from + pos;
            let before = line[..col0].chars().next_back();
            let after = line[col0 + word.len()..].chars().next();
            let boundary_before = before
                .map(|c| !(c == '_' || c.is_ascii_alphanumeric()))
                .unwrap_or(true);
            let boundary_after = after
                .map(|c| !(c == '_' || c.is_ascii_alphanumeric()))
                .unwrap_or(true);
            if boundary_before && boundary_after {
                return Some(Span::new(line_idx + 1, col0 + 1));
            }
            search_from = col0 + word.len();
        }
    }
    None
}

fn find_any(source: &str, needles: &[&str]) -> Option<Span> {
    for (line_idx, line) in source.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with("//") {
            continue;
        }
        let mut best: Option<usize> = None;
        for needle in needles {
            if let Some(pos) = line.find(needle) {
                best = Some(best.map(|b| b.min(pos)).unwrap_or(pos));
            }
        }
        if let Some(pos) = best {
            return Some(Span::new(line_idx + 1, pos + 1));
        }
    }
    None
}

fn last_expr_like_line(source: &str) -> Option<Span> {
    let lines: Vec<_> = source.lines().enumerate().collect();
    for (line_idx, line) in lines.into_iter().rev() {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed == "}" || trimmed.starts_with("//") {
            continue;
        }
        let col = line.find(|c: char| !c.is_whitespace()).unwrap_or(0) + 1;
        return Some(Span::new(line_idx + 1, col));
    }
    None
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: Severity,
    pub message: String,
    pub hint: Option<String>,
    pub span: Span,
    pub file: String,
    pub source: String,
}

impl Diagnostic {
    pub fn error(
        file: impl Into<String>,
        source: impl Into<String>,
        span: Span,
        message: impl Into<String>,
    ) -> Self {
        Self {
            severity: Severity::Error,
            message: message.into(),
            hint: None,
            span,
            file: file.into(),
            source: source.into(),
        }
    }

    pub fn with_hint(mut self, hint: impl Into<String>) -> Self {
        self.hint = Some(hint.into());
        self
    }

    pub fn emit(&self) {
        let _ = self.write_to(&mut io::stderr());
    }

    pub fn write_to<W: Write>(&self, w: &mut W) -> io::Result<()> {
        let color = io::stderr().is_terminal();
        let (label, paint) = match self.severity {
            Severity::Error => ("error", "\x1b[31;1m"),
            Severity::Warning => ("warning", "\x1b[33;1m"),
        };
        let reset = "\x1b[0m";
        let bold = "\x1b[1m";
        let cyan = "\x1b[36m";
        let green = "\x1b[32m";

        if color {
            write!(
                w,
                "{paint}{label}{reset}{bold}: {msg}{reset}\n",
                msg = self.message
            )?;
        } else {
            writeln!(w, "{label}: {}", self.message)?;
        }

        if self.span.is_known() {
            let loc = format!("{}:{}:{}", self.file, self.span.line, self.span.col);
            if color {
                writeln!(w, "  {cyan}-->{reset} {loc}")?;
            } else {
                writeln!(w, "  --> {loc}")?;
            }

            if let Some(line_text) = source_line(&self.source, self.span.line) {
                let gutter = format!("{}", self.span.line);
                let pad = gutter.len();
                if color {
                    writeln!(w, "  {cyan}{:>pad$} |{reset}", "", pad = pad)?;
                    writeln!(w, "  {cyan}{gutter} |{reset} {line_text}",)?;
                    let col = self.span.col.max(1);
                    let spaces = " ".repeat(col - 1);
                    writeln!(
                        w,
                        "  {cyan}{:>pad$} |{reset} {spaces}{green}^{reset}",
                        "",
                        pad = pad
                    )?;
                } else {
                    writeln!(w, "  {:>pad$} |", "", pad = pad)?;
                    writeln!(w, "  {gutter} | {line_text}")?;
                    let col = self.span.col.max(1);
                    let spaces = " ".repeat(col - 1);
                    writeln!(w, "  {:>pad$} | {spaces}^", "", pad = pad)?;
                }
            }
        } else if !self.file.is_empty() {
            if color {
                writeln!(w, "  {cyan}-->{reset} {}", self.file)?;
            } else {
                writeln!(w, "  --> {}", self.file)?;
            }
        }

        if let Some(hint) = &self.hint {
            if color {
                writeln!(w, "  {green}help:{reset} {hint}")?;
            } else {
                writeln!(w, "  help: {hint}")?;
            }
        }
        writeln!(w)?;
        Ok(())
    }

    pub fn to_json(&self) -> String {
        let severity = match self.severity {
            Severity::Error => "error",
            Severity::Warning => "warning",
        };
        let line_text = source_line(&self.source, self.span.line).unwrap_or_default();
        let hint = self
            .hint
            .as_ref()
            .map(|h| format!(r#","hint":"{}""#, json_escape(h)))
            .unwrap_or_default();
        let source = if line_text.is_empty() {
            String::new()
        } else {
            format!(r#","sourceLine":"{}""#, json_escape(&line_text))
        };
        format!(
            r#"{{"severity":"{severity}","file":"{}","line":{},"column":{},"message":"{}"{hint}{source}}}"#,
            json_escape(&self.file),
            self.span.line,
            self.span.col,
            json_escape(&self.message)
        )
    }
}

fn source_line(source: &str, line: usize) -> Option<String> {
    if line == 0 {
        return None;
    }
    source.lines().nth(line - 1).map(|s| s.to_string())
}

pub fn json_escape(s: &str) -> String {
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

/// Levenshtein-based suggestion for undefined names.
pub fn suggest_name(name: &str, candidates: &[String]) -> Option<String> {
    let mut best: Option<(usize, &String)> = None;
    for c in candidates {
        let d = edit_distance(name, c);
        if d > 0 && d <= 2 {
            if best.map(|(bd, _)| d < bd).unwrap_or(true) {
                best = Some((d, c));
            }
        }
    }
    best.map(|(_, s)| s.clone())
}

fn edit_distance(a: &str, b: &str) -> usize {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut prev: Vec<usize> = (0..=b.len()).collect();
    let mut cur = vec![0; b.len() + 1];
    for i in 1..=a.len() {
        cur[0] = i;
        for j in 1..=b.len() {
            let cost = if a[i - 1] == b[j - 1] { 0 } else { 1 };
            cur[j] = (prev[j] + 1).min(cur[j - 1] + 1).min(prev[j - 1] + cost);
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    prev[b.len()]
}

pub fn friendly_token(kind: &str) -> String {
    match kind {
        "LBrace" => "'{'".into(),
        "RBrace" => "'}'".into(),
        "LParen" => "'('".into(),
        "RParen" => "')'".into(),
        "LBracket" => "'['".into(),
        "RBracket" => "']'".into(),
        "Comma" => "','".into(),
        "Colon" => "':'".into(),
        "Arrow" => "'->'".into(),
        "FatArrow" => "'=>'".into(),
        "Assign" => "'='".into(),
        "Eof" => "end of file".into(),
        "Ident" => "a name".into(),
        other => other.to_lowercase(),
    }
}
