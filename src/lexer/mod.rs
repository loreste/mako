//! Lexer for Mako source.

use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub line: usize,
    pub col: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // Literals
    Int(i64),
    Float(f64),
    String(String),
    Ident(String),

    // Keywords
    Fn,
    Let,
    Mut,
    If,
    Else,
    While,
    For,
    In,
    Range,
    Break,
    Continue,
    Return,
    Defer,
    True,
    False,
    Struct,
    Enum,
    Match,
    Crew,
    Kick,
    Join,
    Fan,
    Arena,
    Actor,
    Receive,
    Interface,
    Extern,
    Hold,
    Share,
    Unsafe,
    As,
    Select,
    Timeout,
    Default,
    Const,
    Import,
    And,
    Or,
    Not,
    /// Package export: `export fn` / `export struct` / `export on`
    Export,
    /// Method block: `on Point { fn distance(self) … }`
    On,
    /// Dual: `func` → same as `fn`
    Func,
    /// Dual: `var x = …` → mutable binding
    Var,
    /// Unit name: preferred `pack lib`, dual `package lib`
    Package,
    /// Dual: `type Point struct { … }`
    Type,

    // Punctuation
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Colon,
    /// Go short declaration `:=`
    ColonAssign,
    Semicolon,
    Dot,
    Arrow,
    FatArrow,
    Assign,
    Question,
    Pipe,
    PipePipe,
    Amp,
    AmpAmp,
    Caret,
    AmpCaret,
    Shl,
    Shr,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    // Compound assignment and increment/decrement (desugared in the parser).
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    PlusPlus,
    MinusMinus,
    EqEq,
    BangEq,
    Lt,
    Le,
    Gt,
    Ge,
    Bang,
    Hash,

    Eof,
}

impl fmt::Display for TokenKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TokenKind::Int(n) => write!(f, "{n}"),
            TokenKind::Float(n) => write!(f, "{n}"),
            TokenKind::String(s) => write!(f, "\"{s}\""),
            TokenKind::Ident(s) => write!(f, "{s}"),
            other => write!(f, "{other:?}"),
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum LexError {
    #[error("unexpected character '{0}' at {1}:{2}")]
    UnexpectedChar(char, usize, usize),
    #[error("unterminated string at {0}:{1}")]
    UnterminatedString(usize, usize),
    #[error("numeric literal `{literal}` is out of range at {line}:{col}")]
    NumberOutOfRange {
        literal: String,
        line: usize,
        col: usize,
    },
    #[error("colored `async`/`await` is not part of Mako (use crew/kick/channels) at {0}:{1}")]
    ColoredAsync(usize, usize),
}

pub struct Lexer<'a> {
    src: &'a [u8],
    pos: usize,
    line: usize,
    col: usize,
}

impl<'a> Lexer<'a> {
    pub fn new(src: &'a str) -> Self {
        Self {
            src: src.as_bytes(),
            pos: 0,
            line: 1,
            col: 1,
        }
    }

    pub fn tokenize(mut self) -> Result<Vec<Token>, LexError> {
        let mut tokens = Vec::new();
        loop {
            let tok = self.next_token()?;
            let is_eof = tok.kind == TokenKind::Eof;
            tokens.push(tok);
            if is_eof {
                break;
            }
        }
        Ok(tokens)
    }

    fn peek(&self) -> Option<u8> {
        self.src.get(self.pos).copied()
    }

    fn peek2(&self) -> Option<u8> {
        self.src.get(self.pos + 1).copied()
    }

    fn bump(&mut self) -> Option<u8> {
        let c = self.peek()?;
        self.pos += 1;
        if c == b'\n' {
            self.line += 1;
            self.col = 1;
        } else {
            self.col += 1;
        }
        Some(c)
    }

    fn skip_ws_and_comments(&mut self) {
        loop {
            while let Some(c) = self.peek() {
                if c.is_ascii_whitespace() {
                    self.bump();
                } else {
                    break;
                }
            }
            if self.peek() == Some(b'/') && self.peek2() == Some(b'/') {
                while let Some(c) = self.peek() {
                    if c == b'\n' {
                        break;
                    }
                    self.bump();
                }
                continue;
            }
            if self.peek() == Some(b'/') && self.peek2() == Some(b'*') {
                self.bump();
                self.bump();
                while let Some(c) = self.peek() {
                    if c == b'*' && self.peek2() == Some(b'/') {
                        self.bump();
                        self.bump();
                        break;
                    }
                    self.bump();
                }
                continue;
            }
            break;
        }
    }

    fn next_token(&mut self) -> Result<Token, LexError> {
        self.skip_ws_and_comments();
        let line = self.line;
        let col = self.col;

        let Some(c) = self.peek() else {
            return Ok(Token {
                kind: TokenKind::Eof,
                line,
                col,
            });
        };

        let kind = match c {
            b'(' => {
                self.bump();
                TokenKind::LParen
            }
            b')' => {
                self.bump();
                TokenKind::RParen
            }
            b'{' => {
                self.bump();
                TokenKind::LBrace
            }
            b'}' => {
                self.bump();
                TokenKind::RBrace
            }
            b'[' => {
                self.bump();
                TokenKind::LBracket
            }
            b']' => {
                self.bump();
                TokenKind::RBracket
            }
            b',' => {
                self.bump();
                TokenKind::Comma
            }
            b';' => {
                self.bump();
                TokenKind::Semicolon
            }
            b'%' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::PercentEq
                } else {
                    TokenKind::Percent
                }
            }
            b'+' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::PlusEq
                } else if self.peek() == Some(b'+') {
                    self.bump();
                    TokenKind::PlusPlus
                } else {
                    TokenKind::Plus
                }
            }
            b'*' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::StarEq
                } else {
                    TokenKind::Star
                }
            }
            b'?' => {
                self.bump();
                TokenKind::Question
            }
            b'.' => {
                self.bump();
                TokenKind::Dot
            }
            b':' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::ColonAssign
                } else {
                    TokenKind::Colon
                }
            }
            b'-' => {
                self.bump();
                if self.peek() == Some(b'>') {
                    self.bump();
                    TokenKind::Arrow
                } else if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::MinusEq
                } else if self.peek() == Some(b'-') {
                    self.bump();
                    TokenKind::MinusMinus
                } else {
                    TokenKind::Minus
                }
            }
            b'=' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::EqEq
                } else if self.peek() == Some(b'>') {
                    self.bump();
                    TokenKind::FatArrow
                } else {
                    TokenKind::Assign
                }
            }
            b'!' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::BangEq
                } else {
                    TokenKind::Bang
                }
            }
            b'#' => {
                self.bump();
                TokenKind::Hash
            }
            b'<' => {
                self.bump();
                if self.peek() == Some(b'<') {
                    self.bump();
                    TokenKind::Shl
                } else if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::Le
                } else {
                    TokenKind::Lt
                }
            }
            b'>' => {
                self.bump();
                if self.peek() == Some(b'>') {
                    self.bump();
                    TokenKind::Shr
                } else if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::Ge
                } else {
                    TokenKind::Gt
                }
            }
            b'/' => {
                self.bump();
                if self.peek() == Some(b'=') {
                    self.bump();
                    TokenKind::SlashEq
                } else {
                    TokenKind::Slash
                }
            }
            b'|' => {
                self.bump();
                if self.peek() == Some(b'|') {
                    self.bump();
                    TokenKind::PipePipe
                } else {
                    TokenKind::Pipe
                }
            }
            b'&' => {
                self.bump();
                if self.peek() == Some(b'&') {
                    self.bump();
                    TokenKind::AmpAmp
                } else if self.peek() == Some(b'^') {
                    self.bump();
                    TokenKind::AmpCaret
                } else {
                    TokenKind::Amp
                }
            }
            b'^' => {
                self.bump();
                TokenKind::Caret
            }
            b'"' => TokenKind::String(self.lex_string()?),
            b'0'..=b'9' => self.lex_number()?,
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => {
                let kind = self.lex_ident();
                if let TokenKind::Ident(ref s) = kind {
                    if s == "async" || s == "await" {
                        return Err(LexError::ColoredAsync(line, col));
                    }
                }
                kind
            }
            other => {
                return Err(LexError::UnexpectedChar(other as char, line, col));
            }
        };

        Ok(Token { kind, line, col })
    }

    fn lex_string(&mut self) -> Result<String, LexError> {
        let line = self.line;
        let col = self.col;
        self.bump(); // "
        let mut s = String::new();
        while let Some(c) = self.peek() {
            if c == b'"' {
                self.bump();
                return Ok(s);
            }
            if c == b'\\' {
                self.bump();
                match self.bump() {
                    Some(b'n') => s.push('\n'),
                    Some(b't') => s.push('\t'),
                    Some(b'r') => s.push('\r'),
                    Some(b'\\') => s.push('\\'),
                    Some(b'"') => s.push('"'),
                    Some(other) => s.push(other as char),
                    None => return Err(LexError::UnterminatedString(line, col)),
                }
            } else {
                // Decode UTF-8 so multibyte runes stay one char (byte len via .len() later).
                let ch = self.bump_utf8_char(line, col)?;
                s.push(ch);
            }
        }
        Err(LexError::UnterminatedString(line, col))
    }

    fn bump_utf8_char(&mut self, line: usize, col: usize) -> Result<char, LexError> {
        let b0 = match self.bump() {
            Some(b) => b,
            None => return Err(LexError::UnterminatedString(line, col)),
        };
        if b0 < 0x80 {
            return Ok(b0 as char);
        }
        let width = if b0 & 0xE0 == 0xC0 {
            2
        } else if b0 & 0xF0 == 0xE0 {
            3
        } else if b0 & 0xF8 == 0xF0 {
            4
        } else {
            return Err(LexError::UnexpectedChar(b0 as char, line, col));
        };
        let mut buf = [0u8; 4];
        buf[0] = b0;
        for i in 1..width {
            match self.bump() {
                Some(b) => buf[i] = b,
                None => return Err(LexError::UnterminatedString(line, col)),
            }
        }
        std::str::from_utf8(&buf[..width])
            .ok()
            .and_then(|s| s.chars().next())
            .ok_or_else(|| LexError::UnexpectedChar(b0 as char, line, col))
    }

    fn lex_number(&mut self) -> Result<TokenKind, LexError> {
        let start = self.pos;
        let line = self.line;
        let col = self.col;
        while matches!(self.peek(), Some(b'0'..=b'9')) {
            self.bump();
        }
        if self.peek() == Some(b'.') && matches!(self.peek2(), Some(b'0'..=b'9')) {
            self.bump();
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.bump();
            }
            let s = std::str::from_utf8(&self.src[start..self.pos]).unwrap();
            return s
                .parse()
                .map(TokenKind::Float)
                .map_err(|_| LexError::NumberOutOfRange {
                    literal: s.to_string(),
                    line,
                    col,
                });
        }
        let s = std::str::from_utf8(&self.src[start..self.pos]).unwrap();
        s.parse()
            .map(TokenKind::Int)
            .map_err(|_| LexError::NumberOutOfRange {
                literal: s.to_string(),
                line,
                col,
            })
    }

    fn lex_ident(&mut self) -> TokenKind {
        let start = self.pos;
        while matches!(
            self.peek(),
            Some(b'a'..=b'z' | b'A'..=b'Z' | b'0'..=b'9' | b'_')
        ) {
            self.bump();
        }
        let s = std::str::from_utf8(&self.src[start..self.pos]).unwrap();
        match s {
            "fn" => TokenKind::Fn,
            "func" => TokenKind::Func,
            "let" => TokenKind::Let,
            "var" => TokenKind::Var,
            "mut" => TokenKind::Mut,
            // Preferred Mako `pack` is a *contextual* keyword: only a package
            // declaration when it leads a top-level item (handled in the parser).
            // Elsewhere it stays a plain identifier so `let pack = …` still works.
            "package" => TokenKind::Package,
            "type" => TokenKind::Type,
            "if" => TokenKind::If,
            "else" => TokenKind::Else,
            "while" => TokenKind::While,
            "for" => TokenKind::For,
            "in" => TokenKind::In,
            "range" => TokenKind::Range,
            "break" => TokenKind::Break,
            "continue" => TokenKind::Continue,
            "return" => TokenKind::Return,
            "defer" => TokenKind::Defer,
            "true" => TokenKind::True,
            "false" => TokenKind::False,
            "struct" => TokenKind::Struct,
            "enum" => TokenKind::Enum,
            "match" => TokenKind::Match,
            "crew" => TokenKind::Crew,
            "kick" => TokenKind::Kick,
            "join" => TokenKind::Join,
            "fan" => TokenKind::Fan,
            "arena" => TokenKind::Arena,
            "actor" => TokenKind::Actor,
            "receive" => TokenKind::Receive,
            "interface" => TokenKind::Interface,
            "extern" => TokenKind::Extern,
            "hold" => TokenKind::Hold,
            "share" => TokenKind::Share,
            "unsafe" => TokenKind::Unsafe,
            "as" => TokenKind::As,
            "select" => TokenKind::Select,
            "timeout" => TokenKind::Timeout,
            "default" => TokenKind::Default,
            "const" => TokenKind::Const,
            // Preferred Mako `pull` is a *contextual* keyword: only an import when
            // it leads a top-level item (handled in the parser). Elsewhere it stays
            // a plain identifier so `let pull = …` still works.
            "import" => TokenKind::Import,
            "and" => TokenKind::And,
            "or" => TokenKind::Or,
            "not" => TokenKind::Not,
            "export" => TokenKind::Export,
            "on" => TokenKind::On,
            _ => TokenKind::Ident(s.to_string()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lex_fn() {
        let tokens = Lexer::new("fn main() { let x = 1 }").tokenize().unwrap();
        assert!(matches!(tokens[0].kind, TokenKind::Fn));
        assert!(matches!(tokens[1].kind, TokenKind::Ident(ref s) if s == "main"));
    }

    #[test]
    fn oversized_int_is_lex_error() {
        let src = "999999999999999999999999999999999999999999999";
        assert!(matches!(
            Lexer::new(src).tokenize(),
            Err(LexError::NumberOutOfRange { .. })
        ));
    }
}
