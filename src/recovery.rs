//! Parser error recovery — skip to next top-level declaration boundary and
//! collect multiple errors per file.
//!
//! Used by [`crate::parser::Parser::parse_with_errors`] and the CLI check path
//! so `mako check` can report more than one diagnostic.

use crate::diag::{Diagnostic, Span};
use crate::lexer::TokenKind;
use crate::parser::ParseError;

/// Token kinds that start a top-level declaration (recovery sync points).
pub fn is_decl_start(kind: &TokenKind) -> bool {
    matches!(
        kind,
        TokenKind::Fn
            | TokenKind::Func
            | TokenKind::Struct
            | TokenKind::Enum
            | TokenKind::Package
            | TokenKind::Const
            | TokenKind::Export
            | TokenKind::On
            | TokenKind::Import
            | TokenKind::Type
            | TokenKind::Actor
            | TokenKind::Interface
            | TokenKind::Extern
    ) || matches!(kind, TokenKind::Ident(s) if s == "pack" || s == "pull")
}

/// Convert parse errors into diagnostics (all of them).
pub fn parse_errors_to_diagnostics(
    file: &str,
    source: &str,
    errors: &[ParseError],
) -> Vec<Diagnostic> {
    let n = errors.len();
    errors
        .iter()
        .enumerate()
        .map(|(i, e)| {
            let ParseError::Message { message, line, col } = e;
            let mut hint = recovery_hint(message);
            if n > 1 && i + 1 < n {
                hint = format!(
                    "{hint}; recovery continues ({} more error(s))",
                    n - i - 1
                );
            } else if n > 1 && i + 1 == n {
                hint = format!("{hint}; last recovered error in this file");
            }
            Diagnostic::error(file, source, Span::new(*line, *col), message).with_hint(hint)
        })
        .collect()
}

fn recovery_hint(message: &str) -> String {
    let m = message.to_ascii_lowercase();
    if m.contains("found fn") || m.contains("found struct") || m.contains("found enum") {
        "incomplete previous declaration — this token starts the next item; recovery kept it"
            .into()
    } else if m.contains("expected") && m.contains("found eof") {
        "file ended mid-declaration — close braces/parens or finish the signature".into()
    } else if m.contains("expected") {
        "check brackets, commas, and keywords around this spot; recovery continues at next fn/struct/enum"
            .into()
    } else {
        "check brackets, commas, and keywords around this spot; recovery continues at next fn/struct/enum"
            .into()
    }
}

/// Emit all parse diagnostics to stderr; returns true if any error was emitted.
pub fn emit_parse_errors(file: &str, source: &str, errors: &[ParseError]) -> bool {
    if errors.is_empty() {
        return false;
    }
    for d in parse_errors_to_diagnostics(file, source, errors) {
        d.emit();
    }
    true
}
