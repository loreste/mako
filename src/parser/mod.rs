//! Recursive-descent parser for Mako.

use crate::ast::*;
use crate::lexer::{Token, TokenKind};

#[derive(Debug, thiserror::Error)]
pub enum ParseError {
    #[error("parse error at {line}:{col}: {message}")]
    Message {
        message: String,
        line: usize,
        col: usize,
    },
}

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    /// When true, a bare `Ident {` is NOT parsed as a struct literal — it is an
    /// operand followed by a block. Set only while parsing the top level of a
    /// control-flow header (`if`/`while`/`for`/`switch`), and cleared inside any
    /// `(…)`/`[…]` so `if valid(Config{1, 2}) { … }` still allows the literal.
    /// This resolves the Go composite-literal-in-condition ambiguity.
    no_struct_lit: bool,
    /// Lexically-enclosing `crew` names, innermost last. A `go f()` statement
    /// schedules onto the innermost crew; empty means `go` is used outside any
    /// crew (an error).
    crew_stack: Vec<String>,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self {
            tokens,
            pos: 0,
            no_struct_lit: false,
            crew_stack: Vec::new(),
        }
    }

    /// Parse a control-flow header expression with struct literals suppressed at
    /// the top level (they remain allowed inside brackets). Restores the prior
    /// setting afterward so nested headers behave correctly.
    fn parse_header_expr(&mut self) -> Result<Expr, ParseError> {
        let saved = std::mem::replace(&mut self.no_struct_lit, true);
        let r = self.parse_expr();
        self.no_struct_lit = saved;
        r
    }

    pub fn parse(mut self) -> Result<Program, ParseError> {
        let mut items = Vec::new();
        let mut errors: Vec<ParseError> = Vec::new();
        while !self.is_eof() {
            if matches!(self.peek_kind(), TokenKind::Import) || self.at_pull_kw() {
                match self.parse_import() {
                    Ok(imps) => items.extend(imps),
                    Err(e) => {
                        errors.push(e);
                        self.recover_to_next_decl();
                    }
                }
            } else {
                match self.parse_item() {
                    Ok(item) => items.push(item),
                    Err(e) => {
                        errors.push(e);
                        self.recover_to_next_decl();
                    }
                }
            }
        }
        if let Some(first) = errors.into_iter().next() {
            // Surface the first error for CLI compatibility; recovery still
            // parsed later items so typecheck/LSP can see partial ASTs when
            // callers use `parse_with_errors`.
            return Err(first);
        }
        Ok(Program { items })
    }

    /// Parse collecting **all** top-level errors (recovery between decls).
    /// Returns `(program, errors)` — program may be partial; errors empty on success.
    pub fn parse_with_errors(mut self) -> (Program, Vec<ParseError>) {
        let mut items = Vec::new();
        let mut errors: Vec<ParseError> = Vec::new();
        while !self.is_eof() {
            if matches!(self.peek_kind(), TokenKind::Import) || self.at_pull_kw() {
                match self.parse_import() {
                    Ok(imps) => items.extend(imps),
                    Err(e) => {
                        errors.push(e);
                        self.recover_to_next_decl();
                    }
                }
            } else {
                match self.parse_item() {
                    Ok(item) => items.push(item),
                    Err(e) => {
                        errors.push(e);
                        self.recover_to_next_decl();
                    }
                }
            }
        }
        (Program { items }, errors)
    }

    /// Skip tokens until the next top-level declaration boundary
    /// (`fn`/`func`/`struct`/`enum`/`pack`/`package`/`const`/`export`/`on`/`import`/`pull`/`type`/`actor`/`interface`/`extern` or EOF).
    ///
    /// When a failed item leaves the cursor already on the next item's start
    /// keyword (e.g. `fn broken(` then `fn ok()` — error reported at the second
    /// `fn`), do **not** consume that keyword. Decl parsers always bump their
    /// own start token first, so re-entering on the same failed start cannot
    /// infinite-loop without progress past that keyword.
    fn recover_to_next_decl(&mut self) {
        if crate::recovery::is_decl_start(self.peek_kind()) {
            return;
        }
        while !self.is_eof() {
            self.bump();
            if crate::recovery::is_decl_start(self.peek_kind()) {
                return;
            }
        }
    }

    /// True if the cursor is on the contextual import keyword `pull`.
    /// `pull` is only a keyword when it leads a top-level declaration; a bare
    /// identifier is never a valid item on its own, so this cannot shadow a
    /// legitimate use of `pull` as a name (those live in expression position).
    fn at_pull_kw(&self) -> bool {
        matches!(self.peek_kind(), TokenKind::Ident(s) if s == "pull")
    }

    /// Alias used by `mako fmt` tests and tooling.
    #[allow(dead_code)]
    pub fn parse_program(self) -> Result<Program, ParseError> {
        self.parse()
    }

    fn peek(&self) -> &Token {
        &self.tokens[self.pos]
    }

    /// True if the current token starts on the same source line as the previously
    /// consumed token. Used so `break` / `continue` only bind a label written on
    /// the same line (`break outer`); an identifier on the next line is a separate
    /// statement, not the label — newlines are otherwise insignificant in Mako.
    fn peek_on_prev_line(&self) -> bool {
        self.pos > 0 && self.tokens[self.pos].line == self.tokens[self.pos - 1].line
    }

    fn peek_kind(&self) -> &TokenKind {
        &self.peek().kind
    }

    fn is_eof(&self) -> bool {
        matches!(self.peek_kind(), TokenKind::Eof)
    }

    fn bump(&mut self) -> &Token {
        let t = &self.tokens[self.pos];
        if !matches!(t.kind, TokenKind::Eof) {
            self.pos += 1;
        }
        t
    }

    fn expect(&mut self, kind: TokenKind) -> Result<(), ParseError> {
        if std::mem::discriminant(self.peek_kind()) == std::mem::discriminant(&kind) {
            self.bump();
            Ok(())
        } else {
            Err(self.err(format!(
                "expected {}, found {}",
                crate::diag::friendly_token(&format!("{kind:?}")),
                crate::diag::friendly_token(&format!("{}", self.peek_kind()))
            )))
        }
    }

    fn err(&self, message: String) -> ParseError {
        let t = self.peek();
        ParseError::Message {
            message,
            line: t.line,
            col: t.col,
        }
    }

    fn parse_item(&mut self) -> Result<Item, ParseError> {
        let mut derives = Vec::new();
        let mut stability = crate::ast::ApiStability::Unspecified;
        while matches!(self.peek_kind(), TokenKind::Hash) {
            let attr = self.parse_attr()?;
            match attr {
                ItemAttr::Derive(d) => derives.extend(d),
                ItemAttr::Stable => stability = crate::ast::ApiStability::Stable,
                ItemAttr::Deprecated(message) => {
                    stability = crate::ast::ApiStability::Deprecated { message }
                }
            }
        }
        let exported = if matches!(self.peek_kind(), TokenKind::Export) {
            self.bump();
            true
        } else {
            false
        };
        // `const fn` — compile-time foldable function
        let is_const_fn = matches!(self.peek_kind(), TokenKind::Const)
            && matches!(
                self.tokens.get(self.pos + 1).map(|t| &t.kind),
                Some(TokenKind::Fn | TokenKind::Func)
            );
        if is_const_fn {
            self.bump(); // const
        }
        match self.peek_kind() {
            TokenKind::Fn | TokenKind::Func => {
                let mut f = self.parse_fn()?;
                // Go-style: Capitalized names are exported; `export` forces it.
                f.exported = exported || is_exported_name(&f.name);
                f.is_const = is_const_fn;
                f.stability = stability;
                Ok(Item::Fn(f))
            }
            TokenKind::On => {
                let mut o = self.parse_on()?;
                o.exported = exported || is_exported_name(&o.ty);
                Ok(Item::On(o))
            }
            TokenKind::Struct => {
                let mut s = self.parse_struct()?;
                s.derives = derives;
                s.exported = exported || is_exported_name(&s.name);
                Ok(Item::Struct(s))
            }
            // Go-style: `type Point struct { … }` / `type Shape enum { … }`
            TokenKind::Type => {
                self.bump();
                let name = self.expect_ident()?;
                match self.peek_kind() {
                    TokenKind::Struct => {
                        let mut s = self.parse_struct()?;
                        s.name = name;
                        s.derives = derives;
                        s.exported = exported || is_exported_name(&s.name);
                        if exported {
                            s.exported = true;
                        }
                        Ok(Item::Struct(s))
                    }
                    TokenKind::Enum => {
                        let mut e = self.parse_enum()?;
                        e.name = name;
                        e.exported = exported || is_exported_name(&e.name);
                        if exported {
                            e.exported = true;
                        }
                        Ok(Item::Enum(e))
                    }
                    _ => Err(self.err(
                        "Go-style `type Name` expects `struct` or `enum` (e.g. type Point struct { … })"
                            .into(),
                    )),
                }
            }
            TokenKind::Enum => {
                let mut e = self.parse_enum()?;
                e.exported = exported || is_exported_name(&e.name);
                if exported {
                    e.exported = true;
                }
                Ok(Item::Enum(e))
            }
            TokenKind::Actor => Ok(Item::Actor(self.parse_actor()?)),
            TokenKind::Interface => Ok(Item::Interface(self.parse_interface()?)),
            TokenKind::Extern => Ok(Item::ExternC(self.parse_extern_c()?)),
            TokenKind::Const => Ok(Item::Const(self.parse_const()?)),
            // Hard keyword `package` or contextual `pack` leading an item.
            TokenKind::Package => self.parse_package(),
            TokenKind::Ident(s) if s == "pack" => self.parse_package(),
            TokenKind::Import => Err(self.err("internal: import handled in parse()".into())),
            _ => Err(self.err(format!("expected item, found {}", self.peek_kind()))),
        }
    }

    /// `on Point { fn distance(self) -> int { … } }`
    /// `on Counter : Adder { fn add(self, delta: int) -> int { … } }`
    fn parse_on(&mut self) -> Result<OnDef, ParseError> {
        self.expect(TokenKind::On)?;
        let ty = self.expect_ident()?;
        let iface = if matches!(self.peek_kind(), TokenKind::Colon) {
            self.bump();
            Some(self.expect_ident()?)
        } else {
            None
        };
        self.expect(TokenKind::LBrace)?;
        let mut methods = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace | TokenKind::Eof) {
            let mut f = self.parse_fn()?;
            f.exported = false;
            methods.push(f);
        }
        self.expect(TokenKind::RBrace)?;
        Ok(OnDef {
            ty,
            iface,
            methods,
            exported: false,
        })
    }

    /// Optional `[T, U]` or `<T, U>` type parameter list.
    fn parse_type_params_opt(&mut self) -> Result<Vec<String>, ParseError> {
        if !matches!(self.peek_kind(), TokenKind::LBracket | TokenKind::Lt) {
            return Ok(Vec::new());
        }
        let close = if matches!(self.peek_kind(), TokenKind::LBracket) {
            self.bump();
            TokenKind::RBracket
        } else {
            self.bump();
            TokenKind::Gt
        };
        let mut params = Vec::new();
        if std::mem::discriminant(self.peek_kind()) != std::mem::discriminant(&close) {
            loop {
                params.push(self.expect_ident()?);
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
        }
        self.expect(close)?;
        Ok(params)
    }

    /// Item attributes: `#[derive(...)]`, `#[stable]`, `#[deprecated]` / `#[deprecated("msg")]`.
    fn parse_attr(&mut self) -> Result<ItemAttr, ParseError> {
        self.expect(TokenKind::Hash)?;
        self.expect(TokenKind::LBracket)?;
        let name = self.expect_ident()?;
        let attr = match name.as_str() {
            "derive" => {
                self.expect(TokenKind::LParen)?;
                let mut out = Vec::new();
                if !matches!(self.peek_kind(), TokenKind::RParen) {
                    loop {
                        out.push(self.expect_ident()?);
                        if matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                        } else {
                            break;
                        }
                    }
                }
                self.expect(TokenKind::RParen)?;
                ItemAttr::Derive(out)
            }
            "stable" => ItemAttr::Stable,
            "deprecated" => {
                let message = if matches!(self.peek_kind(), TokenKind::LParen) {
                    self.bump();
                    let msg = match self.peek_kind().clone() {
                        TokenKind::String(s) => {
                            self.bump();
                            s
                        }
                        _ => {
                            return Err(self.err(
                                "#[deprecated] expects a string message: #[deprecated(\"use bar\")]"
                                    .into(),
                            ));
                        }
                    };
                    self.expect(TokenKind::RParen)?;
                    msg
                } else {
                    String::new()
                };
                ItemAttr::Deprecated(message)
            }
            other => {
                return Err(self.err(format!(
                    "unknown attribute `{other}` (supported: derive, stable, deprecated)"
                )));
            }
        };
        self.expect(TokenKind::RBracket)?;
        Ok(attr)
    }

    /// Parse one or more pulls (dual keyword `import`):
    /// - preferred: `pull "path"` / `pull "path" as alias`
    /// - dual alias: `pull alias "path"`
    /// - specialized: `pull _ "path"` / `pull . "path"`
    /// - `pull ( … )` grouped (preferred for multiple)
    /// - `pull { … }` brace form (compat)
    /// `pack name` (preferred) or `package name` (dual). The leading keyword is
    /// already known to be present at the cursor.
    fn parse_package(&mut self) -> Result<Item, ParseError> {
        self.bump(); // `pack` (Ident) or `package` (keyword)
        let name = self.expect_ident()?;
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(Item::Package { name })
    }

    fn parse_import(&mut self) -> Result<Vec<Item>, ParseError> {
        // Accept the hard keyword `import` or the contextual `pull`.
        if self.at_pull_kw() {
            self.bump();
        } else {
            self.expect(TokenKind::Import)?;
        }
        match self.peek_kind().clone() {
            TokenKind::LParen => {
                self.bump();
                let mut out = Vec::new();
                while !matches!(self.peek_kind(), TokenKind::RParen | TokenKind::Eof) {
                    out.push(self.parse_import_spec()?);
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                }
                self.expect(TokenKind::RParen)?;
                if out.is_empty() {
                    return Err(self.err("pull () needs at least one path".into()));
                }
                Ok(out)
            }
            TokenKind::LBrace => {
                self.bump();
                let mut out = Vec::new();
                while !matches!(self.peek_kind(), TokenKind::RBrace | TokenKind::Eof) {
                    out.push(self.parse_import_spec()?);
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                }
                self.expect(TokenKind::RBrace)?;
                if out.is_empty() {
                    return Err(self.err("pull {} needs at least one path".into()));
                }
                Ok(out)
            }
            TokenKind::String(_) | TokenKind::Ident(_) | TokenKind::Dot => {
                Ok(vec![self.parse_import_spec()?])
            }
            _ => Err(self.err(
                "pull expects \"path\", \"path\" as name, name \"path\", _ \"path\", . \"path\", or pull ( … )"
                    .into(),
            )),
        }
    }

    fn parse_import_spec(&mut self) -> Result<Item, ParseError> {
        // Go blank: `_ "path"`
        if matches!(self.peek_kind(), TokenKind::Ident(ref s) if s == "_") {
            self.bump();
            let path = self.expect_import_path()?;
            return Ok(Item::Import {
                path,
                alias: None,
                mode: ImportMode::Blank,
            });
        }
        // Go dot: `. "path"`
        if matches!(self.peek_kind(), TokenKind::Dot) {
            self.bump();
            let path = self.expect_import_path()?;
            return Ok(Item::Import {
                path,
                alias: None,
                mode: ImportMode::Dot,
            });
        }
        // Dual form: alias "path" (preferred is "path" as alias below)
        if matches!(self.peek_kind(), TokenKind::Ident(_))
            && matches!(
                self.tokens.get(self.pos + 1).map(|t| &t.kind),
                Some(TokenKind::String(_))
            )
        {
            let alias = self.expect_ident()?;
            let path = self.expect_import_path()?;
            return Ok(Item::Import {
                path,
                alias: Some(alias),
                mode: ImportMode::Normal,
            });
        }
        match self.peek_kind().clone() {
            TokenKind::String(path) => {
                self.bump();
                let alias = if matches!(self.peek_kind(), TokenKind::As) {
                    self.bump();
                    Some(self.expect_ident()?)
                } else {
                    None
                };
                Ok(Item::Import {
                    path,
                    alias,
                    mode: ImportMode::Normal,
                })
            }
            _ => Err(self.err(
                "pull spec expects \"path\", \"path\" as name, name \"path\", _ \"path\", or . \"path\""
                    .into(),
            )),
        }
    }

    fn expect_import_path(&mut self) -> Result<String, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::String(p) => {
                self.bump();
                Ok(p)
            }
            _ => Err(self.err("pull expects a string path \"…\"".into())),
        }
    }

    fn parse_const(&mut self) -> Result<ConstDef, ParseError> {
        self.expect(TokenKind::Const)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::Assign)?;
        let value = self.parse_expr()?;
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(ConstDef { name, value })
    }

    fn parse_actor(&mut self) -> Result<ActorDef, ParseError> {
        self.expect(TokenKind::Actor)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut fields = Vec::new();
        let mut receives = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            if matches!(self.peek_kind(), TokenKind::Receive) {
                self.bump();
                let message = self.expect_ident()?;
                let body = self.parse_block()?;
                receives.push(ReceiveArm { message, body });
                continue;
            }
            // State field: `n: int` / `n: int = 0` / optional leading `mut`
            if matches!(self.peek_kind(), TokenKind::Mut) {
                self.bump();
            }
            let fname = self.expect_ident()?;
            let ty = if matches!(self.peek_kind(), TokenKind::Colon) {
                self.bump();
                self.parse_type()?
            } else if self.peek_is_type_start() {
                self.parse_type()?
            } else {
                return Err(self.err(format!(
                    "actor field `{fname}` needs a type (or use `receive Msg {{ … }}`)"
                )));
            };
            let default = if matches!(self.peek_kind(), TokenKind::Assign) {
                self.bump();
                Some(self.parse_expr()?)
            } else {
                None
            };
            fields.push((fname, ty, default));
            if matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
            }
        }
        self.expect(TokenKind::RBrace)?;
        Ok(ActorDef {
            name,
            fields,
            receives,
        })
    }

    fn parse_interface(&mut self) -> Result<InterfaceDef, ParseError> {
        self.expect(TokenKind::Interface)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut methods = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            // Go: `Write([]byte) (int, string)` or `func Write(...)` / Mako `fn write(...) -> int`
            if matches!(self.peek_kind(), TokenKind::Fn | TokenKind::Func) {
                self.bump();
            }
            let mname = self.expect_ident()?;
            self.expect(TokenKind::LParen)?;
            let mut params = Vec::new();
            if !matches!(self.peek_kind(), TokenKind::RParen) {
                loop {
                    // bare Type, `name Type`, or `name: Type`
                    if matches!(self.peek_kind(), TokenKind::Ident(_)) {
                        let save = self.pos;
                        let _ = self.expect_ident()?;
                        if matches!(self.peek_kind(), TokenKind::Colon) {
                            self.bump();
                            params.push(self.parse_type()?);
                        } else if self.peek_is_type_start() {
                            // Go `name Type` in interface — type only matters for iface
                            params.push(self.parse_type()?);
                        } else {
                            self.pos = save;
                            params.push(self.parse_type()?);
                        }
                    } else {
                        params.push(self.parse_type()?);
                    }
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
            }
            self.expect(TokenKind::RParen)?;
            let ret = if matches!(self.peek_kind(), TokenKind::Arrow) {
                self.bump();
                self.parse_type()?
            } else if self.peek_is_type_start() {
                self.parse_type()?
            } else {
                TypeExpr::Named("void".into())
            };
            methods.push((mname, params, ret));
            if matches!(self.peek_kind(), TokenKind::Semicolon) {
                self.bump();
            }
        }
        self.expect(TokenKind::RBrace)?;
        Ok(InterfaceDef { name, methods })
    }

    fn parse_extern_c(&mut self) -> Result<ExternCDef, ParseError> {
        self.expect(TokenKind::Extern)?;
        // extern "C" fn name(...) -> T
        match self.peek_kind() {
            TokenKind::String(s) if s == "C" => {
                self.bump();
            }
            _ => {
                return Err(self.err("expected \"C\" after extern".into()));
            }
        }
        self.expect(TokenKind::Fn)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::LParen)?;
        let mut params = Vec::new();
        if !matches!(self.peek_kind(), TokenKind::RParen) {
            loop {
                let mutable = if matches!(self.peek_kind(), TokenKind::Mut) {
                    self.bump();
                    true
                } else {
                    false
                };
                let pname = self.expect_ident()?;
                let ty = if matches!(self.peek_kind(), TokenKind::Colon) {
                    self.bump();
                    self.parse_type()?
                } else if self.peek_is_type_start() {
                    self.parse_type()?
                } else {
                    return Err(self.err(format!(
                        "extern param `{pname}` needs a type"
                    )));
                };
                params.push(Param {
                    name: pname,
                    ty,
                    mutable,
                });
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
        }
        self.expect(TokenKind::RParen)?;
        let ret = if matches!(self.peek_kind(), TokenKind::Arrow) {
            self.bump();
            Some(self.parse_type()?)
        } else if self.peek_is_type_start() {
            Some(self.parse_type()?)
        } else {
            None
        };
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(ExternCDef { name, params, ret })
    }

    fn parse_fn(&mut self) -> Result<FnDef, ParseError> {
        // `fn` or Go-style `func`
        if matches!(self.peek_kind(), TokenKind::Fn | TokenKind::Func) {
            self.bump();
        } else {
            return Err(self.err("expected fn or func".into()));
        }

        // Go-style method receiver: `fn (p: Point) distance() -> int`
        // Also: `func (p Point) distance() int` (Go spacing, no colon / no arrow).
        let mut go_receiver: Option<Param> = None;
        if matches!(self.peek_kind(), TokenKind::LParen) {
            let save = self.pos;
            self.bump(); // (
            let looks_like_recv = matches!(self.peek_kind(), TokenKind::Ident(_) | TokenKind::Mut);
            if looks_like_recv {
                let mutable = if matches!(self.peek_kind(), TokenKind::Mut) {
                    self.bump();
                    true
                } else {
                    false
                };
                let rname = match self.peek_kind().clone() {
                    TokenKind::Ident(s) => {
                        self.bump();
                        s
                    }
                    _ => {
                        self.pos = save;
                        String::new()
                    }
                };
                if !rname.is_empty() {
                    // Receiver type: `: T` or bare `T` (Go)
                    let rty_ok = matches!(self.peek_kind(), TokenKind::Colon | TokenKind::Ident(_));
                    if rty_ok {
                        let rty = if matches!(self.peek_kind(), TokenKind::Colon) {
                            self.bump();
                            self.parse_type()?
                        } else {
                            TypeExpr::Named(self.expect_ident()?)
                        };
                        if matches!(self.peek_kind(), TokenKind::RParen) {
                            self.bump();
                            // Receiver form only if method name follows
                            if matches!(self.peek_kind(), TokenKind::Ident(_)) {
                                go_receiver = Some(Param {
                                    name: rname,
                                    ty: rty,
                                    mutable,
                                });
                            } else {
                                self.pos = save;
                            }
                        } else {
                            self.pos = save;
                        }
                    } else {
                        self.pos = save;
                    }
                }
            } else {
                self.pos = save;
            }
        }

        let short_name = self.expect_ident()?;
        let type_params = self.parse_type_params_opt()?;
        self.expect(TokenKind::LParen)?;
        let mut params = Vec::new();
        if let Some(recv) = go_receiver.clone() {
            params.push(recv);
        }
        if !matches!(self.peek_kind(), TokenKind::RParen) {
            loop {
                let mutable = if matches!(self.peek_kind(), TokenKind::Mut) {
                    self.bump();
                    true
                } else {
                    false
                };
                let first = self.expect_ident()?;
                // `self` is never part of a Go shared-type list (`self, k: int` must
                // not become two `int` params — that broke method receivers).
                if first == "self" {
                    let ty = if matches!(self.peek_kind(), TokenKind::Colon) {
                        self.bump();
                        self.parse_type()?
                    } else if self.peek_is_type_start() {
                        self.parse_type()?
                    } else {
                        TypeExpr::Named("__self".into())
                    };
                    params.push(Param {
                        name: first,
                        ty,
                        mutable,
                    });
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                        continue;
                    }
                    break;
                }
                // Go identifier list: `a, b int` shares one type; also `a int` / Mako `a: int`
                // Only share when the type is Go-style (no colon). With `:`, each param is alone.
                let mut names = vec![first];
                if matches!(self.peek_kind(), TokenKind::Colon) {
                    // Mako `a: T` — single param
                    self.bump();
                    let ty = self.parse_type()?;
                    params.push(Param {
                        name: names.pop().unwrap(),
                        ty,
                        mutable,
                    });
                } else {
                    // Collect `a, b` only for bare Go shared type `a, b int`
                    while matches!(self.peek_kind(), TokenKind::Comma) {
                        let save = self.pos;
                        self.bump();
                        if !matches!(self.peek_kind(), TokenKind::Ident(_)) {
                            self.pos = save;
                            break;
                        }
                        // Lookahead: next group might be `c: T` (separate) — stop list
                        let save_n = self.pos;
                        let n = self.expect_ident()?;
                        if matches!(self.peek_kind(), TokenKind::Colon) {
                            // `a, c: T` is not a shared list; rewind to comma
                            self.pos = save;
                            break;
                        }
                        if matches!(self.peek_kind(), TokenKind::Comma) || self.peek_is_type_start()
                        {
                            names.push(n);
                            if self.peek_is_type_start() {
                                break;
                            }
                        } else {
                            self.pos = save_n;
                            self.pos = save;
                            break;
                        }
                    }
                    let ty = if self.peek_is_type_start() {
                        self.parse_type()?
                    } else {
                        return Err(self.err(format!(
                            "parameter `{}` needs a type (Go: `name Type` or Mako: `name: Type`)",
                            names[0]
                        )));
                    };
                    for pname in names {
                        params.push(Param {
                            name: pname,
                            ty: ty.clone(),
                            mutable,
                        });
                    }
                }
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
        }
        self.expect(TokenKind::RParen)?;
        // Return type: Mako `-> T` or Go-style bare type / (T, U) after params
        let ret = if matches!(self.peek_kind(), TokenKind::Arrow) {
            self.bump();
            Some(self.parse_type()?)
        } else if !matches!(self.peek_kind(), TokenKind::LBrace | TokenKind::Eof)
            && matches!(
                self.peek_kind(),
                TokenKind::Ident(_)
                    | TokenKind::LParen
                    | TokenKind::LBracket
                    | TokenKind::Fn
                    | TokenKind::Func
            )
        {
            // Go: `func f() int` or `func f() (int, string)`
            Some(self.parse_type()?)
        } else {
            None
        };
        let body = self.parse_block()?;
        // Go receiver → `Type_method` so `p.distance()` resolves like free methods
        let name = if let Some(recv) = &go_receiver {
            let tname = match &recv.ty {
                TypeExpr::Named(n) => n.clone(),
                other => other.to_string(),
            };
            format!("{tname}_{short_name}")
        } else {
            short_name
        };
        Ok(FnDef {
            name,
            type_params,
            params,
            ret,
            body,
            exported: false,
        is_const: false,
            stability: crate::ast::ApiStability::Unspecified,
        })
    }

    fn parse_struct(&mut self) -> Result<StructDef, ParseError> {
        self.expect(TokenKind::Struct)?;
        // When called from `type Name struct`, name may already be consumed — if next is `{`, empty name fixed by caller.
        let name = if matches!(self.peek_kind(), TokenKind::LBrace) {
            String::new()
        } else {
            self.expect_ident()?
        };
        self.expect(TokenKind::LBrace)?;
        let mut fields = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            // Go: `x int` or Mako: `x: int` (optional trailing comma)
            // Optional default: `x: int = 0` / `name: string = ""`
            let fname = self.expect_ident()?;
            let ty = if matches!(self.peek_kind(), TokenKind::Colon) {
                self.bump();
                self.parse_type()?
            } else if self.peek_is_type_start() {
                self.parse_type()?
            } else {
                return Err(self.err(format!(
                    "field `{fname}` needs a type (Go: `{fname} int` or Mako: `{fname}: int`)"
                )));
            };
            let default = if matches!(self.peek_kind(), TokenKind::Assign) {
                self.bump();
                Some(self.parse_expr()?)
            } else {
                None
            };
            fields.push((fname, ty, default));
            if matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
            }
            // Go allows newline-separated fields without commas (already free-form)
        }
        self.expect(TokenKind::RBrace)?;
        Ok(StructDef {
            name,
            fields,
            derives: Vec::new(),
            exported: false,
        })
    }

    /// True if the next token can start a type (Go `name Type` form).
    fn peek_is_type_start(&self) -> bool {
        matches!(
            self.peek_kind(),
            TokenKind::Ident(_)
                | TokenKind::LBracket
                | TokenKind::LParen
                | TokenKind::Fn
                | TokenKind::Func
        )
    }

    fn parse_enum(&mut self) -> Result<EnumDef, ParseError> {
        self.expect(TokenKind::Enum)?;
        let name = self.expect_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut variants = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            let vname = self.expect_ident()?;
            let mut fields = Vec::new();
            if matches!(self.peek_kind(), TokenKind::LParen) {
                self.bump();
                if !matches!(self.peek_kind(), TokenKind::RParen) {
                    loop {
                        fields.push(self.parse_type()?);
                        if matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                        } else {
                            break;
                        }
                    }
                }
                self.expect(TokenKind::RParen)?;
            }
            variants.push(EnumVariant {
                name: vname,
                fields,
            });
            if matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
            }
        }
        self.expect(TokenKind::RBrace)?;
        Ok(EnumDef {
            name,
            variants,
            exported: false,
        })
    }

    fn parse_type(&mut self) -> Result<TypeExpr, ParseError> {
        // Tuple type: `(int, string)`
        if matches!(self.peek_kind(), TokenKind::LParen) {
            self.bump();
            let mut elems = Vec::new();
            if !matches!(self.peek_kind(), TokenKind::RParen) {
                loop {
                    elems.push(self.parse_type()?);
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                        // trailing comma ok
                        if matches!(self.peek_kind(), TokenKind::RParen) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
            self.expect(TokenKind::RParen)?;
            if elems.len() == 1 {
                // `(int)` is just grouping for types too
                return Ok(elems.into_iter().next().unwrap());
            }
            if elems.is_empty() {
                return Err(self.err("empty tuple type `()` is not supported".into()));
            }
            return Ok(TypeExpr::Tuple(elems));
        }
        // Go-like `[]T` or existing `[T]`
        if matches!(self.peek_kind(), TokenKind::LBracket) {
            self.bump();
            if matches!(self.peek_kind(), TokenKind::RBracket) {
                self.bump();
                let inner = self.parse_type()?;
                return Ok(TypeExpr::Array(Box::new(inner)));
            }
            let inner = self.parse_type()?;
            self.expect(TokenKind::RBracket)?;
            return Ok(TypeExpr::Array(Box::new(inner)));
        }
        if matches!(self.peek_kind(), TokenKind::Fn) {
            self.bump();
            self.expect(TokenKind::LParen)?;
            let mut params = Vec::new();
            if !matches!(self.peek_kind(), TokenKind::RParen) {
                loop {
                    params.push(self.parse_type()?);
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
            }
            self.expect(TokenKind::RParen)?;
            self.expect(TokenKind::Arrow)?;
            let ret = self.parse_type()?;
            return Ok(TypeExpr::Fn(params, Box::new(ret)));
        }
        let name = self.expect_ident()?;
        // Pack-qualified type: `eng.Table` → `eng__Table` (matches import prefix rewrite).
        // Call sites already use `eng.table_new()`; type annotations use the same surface.
        let name = if matches!(self.peek_kind(), TokenKind::Dot) {
            self.bump();
            let ty_name = self.expect_ident()?;
            // Multi-segment rare; keep one pack level (alias.Type) like pull rewrite.
            format!("{name}__{ty_name}")
        } else {
            name
        };
        // Go-like `map[K]V` (key in brackets, value follows)
        if name == "map" && matches!(self.peek_kind(), TokenKind::LBracket) {
            self.bump();
            let key = self.parse_type()?;
            self.expect(TokenKind::RBracket)?;
            let val = self.parse_type()?;
            return Ok(TypeExpr::Map(Box::new(key), Box::new(val)));
        }
        // Dual generics: Result[T,E] and Result<T,E>
        if matches!(self.peek_kind(), TokenKind::LBracket | TokenKind::Lt) {
            let close = if matches!(self.peek_kind(), TokenKind::LBracket) {
                self.bump();
                TokenKind::RBracket
            } else {
                self.bump();
                TokenKind::Gt
            };
            let mut args = Vec::new();
            if std::mem::discriminant(self.peek_kind()) != std::mem::discriminant(&close) {
                loop {
                    args.push(self.parse_type()?);
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
            }
            self.expect(close)?;
            return Ok(TypeExpr::Generic(name, args));
        }
        Ok(TypeExpr::Named(name))
    }

    fn parse_block(&mut self) -> Result<Block, ParseError> {
        self.expect(TokenKind::LBrace)?;
        let mut stmts = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            stmts.push(self.parse_stmt()?);
        }
        self.expect(TokenKind::RBrace)?;
        Ok(Block { stmts })
    }

    fn parse_stmt(&mut self) -> Result<Stmt, ParseError> {
        // Contextual `switch` keyword (Go-style). Only a switch when it leads a
        // statement and is not being used as an identifier (`switch = …`,
        // `switch(…)`, `switch.x`, `switch[i]`), so `switch` stays a usable name.
        if self.at_switch_kw() {
            return self.parse_switch();
        }
        // Contextual `go f()` — schedule a call onto the enclosing crew.
        if self.at_go_kw() {
            return self.parse_go();
        }
        if self.at_detach_kw() {
            return self.parse_detach();
        }
        match self.peek_kind() {
            TokenKind::Hold | TokenKind::Share | TokenKind::Let | TokenKind::Var => self.parse_let(),
            TokenKind::Unsafe => {
                self.bump();
                let body = self.parse_block()?;
                Ok(Stmt::Unsafe { body })
            }
            TokenKind::Return => {
                self.bump();
                if matches!(self.peek_kind(), TokenKind::Semicolon | TokenKind::RBrace) {
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    return Ok(Stmt::Return(None));
                }
                let e = self.parse_expr()?;
                if matches!(self.peek_kind(), TokenKind::Semicolon) {
                    self.bump();
                }
                Ok(Stmt::Return(Some(e)))
            }
            TokenKind::If => self.parse_if(),
            TokenKind::While => self.parse_while(None),
            TokenKind::For => self.parse_for(None),
            TokenKind::Break => {
                self.bump();
                let label = if matches!(self.peek_kind(), TokenKind::Ident(_))
                    && self.peek_on_prev_line()
                {
                    Some(self.expect_ident()?)
                } else {
                    None
                };
                if matches!(self.peek_kind(), TokenKind::Semicolon) {
                    self.bump();
                }
                Ok(Stmt::Break(label))
            }
            TokenKind::Continue => {
                self.bump();
                let label = if matches!(self.peek_kind(), TokenKind::Ident(_))
                    && self.peek_on_prev_line()
                {
                    Some(self.expect_ident()?)
                } else {
                    None
                };
                if matches!(self.peek_kind(), TokenKind::Semicolon) {
                    self.bump();
                }
                Ok(Stmt::Continue(label))
            }
            TokenKind::Defer => {
                self.bump();
                if matches!(self.peek_kind(), TokenKind::LBrace) {
                    let body = self.parse_block()?;
                    Ok(Stmt::Defer { body })
                } else {
                    // `defer expr` → single-statement block
                    let e = self.parse_expr()?;
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    Ok(Stmt::Defer {
                        body: Block {
                            stmts: vec![Stmt::Expr(e)],
                        },
                    })
                }
            }
            TokenKind::Crew => {
                self.bump();
                let name = self.expect_ident()?;
                self.crew_stack.push(name.clone());
                let body = self.parse_block();
                self.crew_stack.pop();
                Ok(Stmt::Crew { name, body: body? })
            }
            TokenKind::Arena => {
                self.bump();
                let name = self.expect_ident()?;
                let body = self.parse_block()?;
                Ok(Stmt::Arena { name, body })
            }
            TokenKind::Select => self.parse_select(),
            TokenKind::Ident(_) => {
                // `label: while` / `label: for` — labeled loops for break/continue.
                if self.pos + 1 < self.tokens.len()
                    && matches!(self.tokens[self.pos + 1].kind, TokenKind::Colon)
                    && self.pos + 2 < self.tokens.len()
                    && matches!(
                        self.tokens[self.pos + 2].kind,
                        TokenKind::While | TokenKind::For
                    )
                {
                    let label = self.expect_ident()?;
                    self.expect(TokenKind::Colon)?;
                    return match self.peek_kind() {
                        TokenKind::While => self.parse_while(Some(label)),
                        TokenKind::For => self.parse_for(Some(label)),
                        _ => unreachable!(),
                    };
                }
                // Could be assign, := short decl, multi-return, index-assign, field-assign, or expression
                let checkpoint = self.pos;
                let name = self.expect_ident()?;
                // Go short decl: `x := expr` or `a, b := expr`
                if matches!(self.peek_kind(), TokenKind::ColonAssign) {
                    self.bump();
                    let value = self.parse_expr()?;
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    return Ok(Stmt::Let {
                        name,
                        mutable: true, // Go := is reassignable in same block for existing; we use mut binding
                        ownership: Ownership::None,
                        ty: None,
                        init: value,
                    });
                }
                // Go multi-return short decl / assign: `a, b := f()` or `a, b = f()`
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    let mut names = vec![name.clone()];
                    while matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                        names.push(self.expect_ident()?);
                    }
                    if matches!(
                        self.peek_kind(),
                        TokenKind::ColonAssign | TokenKind::Assign
                    ) {
                        let mutable = matches!(self.peek_kind(), TokenKind::ColonAssign);
                        self.bump();
                        // RHS may be a single tuple-valued expression (`a, b = f()`)
                        // or a parallel list (`a, b = b, a`). A list is packed into a
                        // tuple so it is evaluated fully before any target is written.
                        let mut vals = vec![self.parse_expr()?];
                        while matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                            vals.push(self.parse_expr()?);
                        }
                        let init = if vals.len() == 1 {
                            vals.into_iter().next().unwrap()
                        } else {
                            Expr::Tuple(vals)
                        };
                        if matches!(self.peek_kind(), TokenKind::Semicolon) {
                            self.bump();
                        }
                        return Ok(Stmt::LetMulti {
                            names,
                            mutable,
                            init,
                        });
                    }
                    // Not multi-assign — rewind and treat as expression
                    self.pos = checkpoint;
                }
                if matches!(self.peek_kind(), TokenKind::Assign) {
                    self.bump();
                    let value = self.parse_expr()?;
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    return Ok(Stmt::Assign { name, value });
                }
                // Compound assignment `i += e` / `i -= e` … and `i++` / `i--`,
                // desugared to `i = i <op> e` / `i = i <op> 1`.
                if let Some(op) = compound_binop(self.peek_kind()) {
                    self.bump();
                    let rhs = self.parse_expr()?;
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    return Ok(Stmt::Assign {
                        value: Expr::Binary {
                            op,
                            left: Box::new(Expr::Ident(name.clone())),
                            right: Box::new(rhs),
                        },
                        name,
                    });
                }
                if let Some(op) = incdec_binop(self.peek_kind()) {
                    self.bump();
                    if matches!(self.peek_kind(), TokenKind::Semicolon) {
                        self.bump();
                    }
                    return Ok(Stmt::Assign {
                        value: Expr::Binary {
                            op,
                            left: Box::new(Expr::Ident(name.clone())),
                            right: Box::new(Expr::Int(1)),
                        },
                        name,
                    });
                }
                // `name.field = value` or `name.a.b = value` (nested field assign)
                if matches!(self.peek_kind(), TokenKind::Dot) {
                    let mut base = Expr::Ident(name);
                    loop {
                        self.bump(); // .
                        let field = self.expect_ident()?;
                        if matches!(self.peek_kind(), TokenKind::Assign) {
                            self.bump();
                            let value = self.parse_expr()?;
                            if matches!(self.peek_kind(), TokenKind::Semicolon) {
                                self.bump();
                            }
                            return Ok(Stmt::FieldAssign { base, field, value });
                        }
                        // `obj.field += e` / `obj.field++`
                        if let Some(op) = compound_binop(self.peek_kind()) {
                            self.bump();
                            let rhs = self.parse_expr()?;
                            if matches!(self.peek_kind(), TokenKind::Semicolon) {
                                self.bump();
                            }
                            let cur = Expr::Field {
                                base: Box::new(base.clone()),
                                field: field.clone(),
                            };
                            return Ok(Stmt::FieldAssign {
                                base,
                                field,
                                value: Expr::Binary {
                                    op,
                                    left: Box::new(cur),
                                    right: Box::new(rhs),
                                },
                            });
                        }
                        if let Some(op) = incdec_binop(self.peek_kind()) {
                            self.bump();
                            if matches!(self.peek_kind(), TokenKind::Semicolon) {
                                self.bump();
                            }
                            let cur = Expr::Field {
                                base: Box::new(base.clone()),
                                field: field.clone(),
                            };
                            return Ok(Stmt::FieldAssign {
                                base,
                                field,
                                value: Expr::Binary {
                                    op,
                                    left: Box::new(cur),
                                    right: Box::new(Expr::Int(1)),
                                },
                            });
                        }
                        if matches!(self.peek_kind(), TokenKind::Dot) {
                            base = Expr::Field {
                                base: Box::new(base),
                                field,
                            };
                            continue;
                        }
                        self.pos = checkpoint;
                        break;
                    }
                } else if matches!(self.peek_kind(), TokenKind::LBracket) {
                    // `name[i] = value`
                    self.bump();
                    let index = self.parse_expr()?;
                    self.expect(TokenKind::RBracket)?;
                    if matches!(self.peek_kind(), TokenKind::Assign) {
                        self.bump();
                        let value = self.parse_expr()?;
                        if matches!(self.peek_kind(), TokenKind::Semicolon) {
                            self.bump();
                        }
                        return Ok(Stmt::IndexAssign {
                            base: Expr::Ident(name),
                            index,
                            value,
                        });
                    }
                    // `arr[i] += e` / `arr[i]++`
                    let compound = compound_binop(self.peek_kind())
                        .map(|op| (op, false))
                        .or_else(|| incdec_binop(self.peek_kind()).map(|op| (op, true)));
                    if let Some((op, is_incdec)) = compound {
                        self.bump();
                        let rhs = if is_incdec {
                            Expr::Int(1)
                        } else {
                            self.parse_expr()?
                        };
                        if matches!(self.peek_kind(), TokenKind::Semicolon) {
                            self.bump();
                        }
                        let cur = Expr::Index {
                            base: Box::new(Expr::Ident(name.clone())),
                            index: Box::new(index.clone()),
                        };
                        return Ok(Stmt::IndexAssign {
                            base: Expr::Ident(name),
                            index,
                            value: Expr::Binary {
                                op,
                                left: Box::new(cur),
                                right: Box::new(rhs),
                            },
                        });
                    }
                    self.pos = checkpoint;
                } else {
                    self.pos = checkpoint;
                }
                let e = self.parse_expr()?;
                if matches!(self.peek_kind(), TokenKind::Semicolon) {
                    self.bump();
                }
                Ok(Stmt::Expr(e))
            }
            _ => {
                let e = self.parse_expr()?;
                if matches!(self.peek_kind(), TokenKind::Semicolon) {
                    self.bump();
                }
                Ok(Stmt::Expr(e))
            }
        }
    }

    fn parse_select(&mut self) -> Result<Stmt, ParseError> {
        self.expect(TokenKind::Select)?;
        // select timeout <expr> { ch => { ... } ... [default|_ => { ... }] }
        self.expect(TokenKind::Timeout)?;
        let timeout_ms = self.parse_expr()?;
        self.expect(TokenKind::LBrace)?;
        let mut arms = Vec::new();
        let mut default_arm = None;
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            let is_default = match self.peek_kind() {
                TokenKind::Default => true,
                TokenKind::Ident(s) if s == "_" => true,
                _ => false,
            };
            if is_default {
                self.bump();
                self.expect(TokenKind::FatArrow)?;
                let body = self.parse_block()?;
                if default_arm.is_some() {
                    return Err(self.err("select has multiple default arms".into()));
                }
                default_arm = Some(body);
            } else {
                let ch = self.expect_ident()?;
                self.expect(TokenKind::FatArrow)?;
                let body = self.parse_block()?;
                arms.push((ch, body));
            }
            if matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
            }
        }
        self.expect(TokenKind::RBrace)?;
        if arms.is_empty() {
            return Err(self.err("select needs at least one channel arm".into()));
        }
        if arms.len() > 16 {
            return Err(self.err("select supports up to 16 channel arms (Partial)".into()));
        }
        Ok(Stmt::Select {
            timeout_ms,
            arms,
            default_arm,
        })
    }

    fn parse_let(&mut self) -> Result<Stmt, ParseError> {
        let ownership = if matches!(self.peek_kind(), TokenKind::Hold) {
            self.bump();
            Ownership::Hold
        } else if matches!(self.peek_kind(), TokenKind::Share) {
            self.bump();
            Ownership::Share
        } else {
            Ownership::None
        };
        // Go: `var x = 1` is mutable; Mako `let` stays immutable unless `mut`
        let is_var = matches!(self.peek_kind(), TokenKind::Var);
        if is_var {
            self.bump();
        } else {
            self.expect(TokenKind::Let)?;
        }
        let mutable = if is_var {
            true
        } else if matches!(self.peek_kind(), TokenKind::Mut) {
            self.bump();
            true
        } else {
            false
        };
        if ownership == Ownership::Share && mutable {
            return Err(
                self.err("share bindings are immutable (cannot use `share let mut`)".into())
            );
        }
        let name = self.expect_ident()?;
        // Comma list: Go multi-return `let a, b = f()` or map comma-ok `let v, ok = m[k]`
        if matches!(self.peek_kind(), TokenKind::Comma) {
            if ownership != Ownership::None {
                return Err(self.err("comma bindings do not support hold/share".into()));
            }
            let mut names = vec![name];
            while matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
                names.push(self.expect_ident()?);
            }
            if !matches!(
                self.peek_kind(),
                TokenKind::Assign | TokenKind::ColonAssign
            ) {
                return Err(self.err("expected `=` or `:=` after multi-name binding".into()));
            }
            self.bump();
            // RHS: a single tuple-valued / index expression, or a parallel list
            // (`let a, b = 1, 2`) packed into a tuple for full evaluation first.
            let mut vals = vec![self.parse_expr()?];
            while matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
                vals.push(self.parse_expr()?);
            }
            let init = if vals.len() == 1 {
                vals.into_iter().next().unwrap()
            } else {
                Expr::Tuple(vals)
            };
            if matches!(self.peek_kind(), TokenKind::Semicolon) {
                self.bump();
            }
            // comma-ok map when exactly two names and RHS is index
            if names.len() == 2 {
                if let Expr::Index { base, index } = init {
                    return Ok(Stmt::LetCommaOk {
                        value: names[0].clone(),
                        ok: names[1].clone(),
                        mutable,
                        base: *base,
                        index: *index,
                    });
                }
            }
            return Ok(Stmt::LetMulti {
                names,
                mutable,
                init,
            });
        }
        let ty = if matches!(self.peek_kind(), TokenKind::Colon) {
            self.bump();
            Some(self.parse_type()?)
        } else {
            None
        };
        // Accept `=` or Go-style `:=` after typed/untyped single binding
        if matches!(self.peek_kind(), TokenKind::ColonAssign) {
            self.bump();
        } else {
            self.expect(TokenKind::Assign)?;
        }
        let init = self.parse_expr()?;
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(Stmt::Let {
            name,
            mutable,
            ownership,
            ty,
            init,
        })
    }

    fn parse_if(&mut self) -> Result<Stmt, ParseError> {
        self.expect(TokenKind::If)?;
        // Go-style if-with-init: `if err := f(); err != nil { … }`.
        // Detected by a top-level `;` before the block `{` — unambiguous because a
        // `;` never occurs at bracket-depth 0 inside an expression. When present the
        // init runs before the condition and is scoped to the whole if/else.
        let init = if self.has_stmt_before_block() {
            Some(self.parse_stmt()?) // consumes the separating `;`
        } else {
            None
        };
        let cond = self.parse_header_expr()?;
        let then_block = self.parse_block()?;
        let else_block = if matches!(self.peek_kind(), TokenKind::Else) {
            self.bump();
            if matches!(self.peek_kind(), TokenKind::If) {
                // else if → wrap as block with single if stmt
                let inner = self.parse_if()?;
                Some(Block { stmts: vec![inner] })
            } else {
                Some(self.parse_block()?)
            }
        } else {
            None
        };
        Ok(Stmt::If {
            init: init.map(Box::new),
            cond,
            then_block,
            else_block,
        })
    }

    /// True if a top-level `;` appears before the next block-opening `{`, i.e. the
    /// construct has a leading simple-statement clause (`if init; cond`,
    /// `while init; cond`). Scans without consuming; bracket depth is tracked so a
    /// struct literal's `{` (at depth 0) ends the scan and a `;` inside `()`/`[]`
    /// is ignored.
    fn has_stmt_before_block(&self) -> bool {
        let mut depth: i32 = 0;
        let mut i = self.pos;
        while i < self.tokens.len() {
            match &self.tokens[i].kind {
                TokenKind::Semicolon if depth == 0 => return true,
                TokenKind::LBrace if depth == 0 => return false,
                TokenKind::LParen | TokenKind::LBracket | TokenKind::LBrace => depth += 1,
                TokenKind::RParen | TokenKind::RBracket | TokenKind::RBrace => depth -= 1,
                TokenKind::Eof => return false,
                _ => {}
            }
            i += 1;
        }
        false
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        self.parse_or()
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;
        while matches!(self.peek_kind(), TokenKind::Or | TokenKind::PipePipe) {
            self.bump();
            let right = self.parse_and()?;
            left = Expr::Binary {
                op: BinOp::Or,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_cmp()?;
        while matches!(self.peek_kind(), TokenKind::And | TokenKind::AmpAmp) {
            self.bump();
            let right = self.parse_cmp()?;
            left = Expr::Binary {
                op: BinOp::And,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_cmp(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitor()?;
        loop {
            let op = match self.peek_kind() {
                TokenKind::EqEq => BinOp::Eq,
                TokenKind::BangEq => BinOp::Ne,
                TokenKind::Lt => BinOp::Lt,
                TokenKind::Le => BinOp::Le,
                TokenKind::Gt => BinOp::Gt,
                TokenKind::Ge => BinOp::Ge,
                _ => break,
            };
            self.bump();
            let right = self.parse_bitor()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    /// Go precedence 4: `+` `-` `|` `^`
    fn parse_bitor(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitand()?;
        loop {
            let op = match self.peek_kind() {
                TokenKind::Plus => BinOp::Add,
                TokenKind::Minus => BinOp::Sub,
                TokenKind::Pipe => BinOp::BitOr,
                TokenKind::Caret => BinOp::BitXor,
                _ => break,
            };
            self.bump();
            let right = self.parse_bitand()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    /// Go precedence 5: `*` `/` `%` `<<` `>>` `&` `&^`
    fn parse_bitand(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_unary()?;
        loop {
            let op = match self.peek_kind() {
                TokenKind::Star => BinOp::Mul,
                TokenKind::Slash => BinOp::Div,
                TokenKind::Percent => BinOp::Mod,
                TokenKind::Shl => BinOp::Shl,
                TokenKind::Shr => BinOp::Shr,
                TokenKind::Amp => BinOp::BitAnd,
                TokenKind::AmpCaret => BinOp::BitClear,
                _ => break,
            };
            self.bump();
            let right = self.parse_unary()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    /// True if the current token can begin an expression. Used to decide whether a
    /// brace after a type name holds positional literal values (`Point{1, 2}`) or
    /// is not a literal at all.
    fn peek_starts_expr(&self) -> bool {
        matches!(
            self.peek_kind(),
            TokenKind::Int(_)
                | TokenKind::Float(_)
                | TokenKind::String(_)
                | TokenKind::True
                | TokenKind::False
                | TokenKind::Ident(_)
                | TokenKind::LParen
                | TokenKind::LBracket
                | TokenKind::Minus
                | TokenKind::Bang
                | TokenKind::Not
                | TokenKind::Caret
                | TokenKind::Match
                | TokenKind::Fn
                | TokenKind::Pipe
                | TokenKind::Fan
        )
    }

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        match self.peek_kind() {
            TokenKind::Minus => {
                self.bump();
                Ok(Expr::Unary {
                    op: UnaryOp::Neg,
                    expr: Box::new(self.parse_unary()?),
                })
            }
            TokenKind::Bang | TokenKind::Not => {
                self.bump();
                Ok(Expr::Unary {
                    op: UnaryOp::Not,
                    expr: Box::new(self.parse_unary()?),
                })
            }
            TokenKind::Caret => {
                self.bump();
                Ok(Expr::Unary {
                    op: UnaryOp::BitNot,
                    expr: Box::new(self.parse_unary()?),
                })
            }
            _ => self.parse_postfix(),
        }
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        loop {
            match self.peek_kind() {
                TokenKind::LParen => {
                    self.bump();
                    let saved = std::mem::replace(&mut self.no_struct_lit, false);
                    let mut args = Vec::new();
                    if !matches!(self.peek_kind(), TokenKind::RParen) {
                        loop {
                            args.push(self.parse_expr()?);
                            if matches!(self.peek_kind(), TokenKind::Comma) {
                                self.bump();
                            } else {
                                break;
                            }
                        }
                    }
                    self.expect(TokenKind::RParen)?;
                    self.no_struct_lit = saved;
                    expr = Expr::Call {
                        callee: Box::new(expr),
                        args,
                    };
                }
                TokenKind::Dot => {
                    self.bump();
                    if matches!(self.peek_kind(), TokenKind::Join) {
                        self.bump();
                        // join as method: x.join or x.join()
                        if matches!(self.peek_kind(), TokenKind::LParen) {
                            self.bump();
                            self.expect(TokenKind::RParen)?;
                        }
                        expr = Expr::Join(Box::new(expr));
                        continue;
                    }
                    if matches!(self.peek_kind(), TokenKind::Kick) {
                        // crew.kick(expr)
                        self.bump();
                        self.expect(TokenKind::LParen)?;
                        let inner = self.parse_expr()?;
                        self.expect(TokenKind::RParen)?;
                        let crew = match &expr {
                            Expr::Ident(n) => n.clone(),
                            _ => return Err(self.err("kick receiver must be a crew name".into())),
                        };
                        expr = Expr::Kick {
                            crew,
                            expr: Box::new(inner),
                        };
                        continue;
                    }
                    let field = self.expect_ident()?;
                    if matches!(self.peek_kind(), TokenKind::LParen) {
                        self.bump();
                        let saved = std::mem::replace(&mut self.no_struct_lit, false);
                        let mut args = Vec::new();
                        if !matches!(self.peek_kind(), TokenKind::RParen) {
                            loop {
                                args.push(self.parse_expr()?);
                                if matches!(self.peek_kind(), TokenKind::Comma) {
                                    self.bump();
                                } else {
                                    break;
                                }
                            }
                        }
                        self.expect(TokenKind::RParen)?;
                        self.no_struct_lit = saved;
                        expr = Expr::Method {
                            receiver: Box::new(expr),
                            method: field,
                            args,
                        };
                    } else if matches!(self.peek_kind(), TokenKind::LBrace) && !self.no_struct_lit {
                        // Pack-qualified struct literal: `eng.Point { x: 1, y: 2 }`
                        // or positional `eng.Point { 1, 2 }`. Only when receiver is
                        // a bare pack alias (`Ident`), matching type annotations.
                        if let Expr::Ident(pkg) = &expr {
                            let name = format!("{pkg}__{field}");
                            let save = self.pos;
                            self.bump(); // {
                            if let Some(lit) = self.try_parse_struct_lit_tail(name)? {
                                expr = lit;
                                continue;
                            }
                            self.pos = save;
                        }
                        expr = Expr::Field {
                            base: Box::new(expr),
                            field,
                        };
                    } else {
                        expr = Expr::Field {
                            base: Box::new(expr),
                            field,
                        };
                    }
                }
                TokenKind::LBracket => {
                    self.bump();
                    let idx_saved = std::mem::replace(&mut self.no_struct_lit, false);
                    // Index `a[i]` or slice `a[low:high]` / `a[low:high:max]` / `a[:]` / `a[i:]` / `a[:j]`
                    if matches!(self.peek_kind(), TokenKind::Colon) {
                        self.bump();
                        let high =
                            if matches!(self.peek_kind(), TokenKind::RBracket | TokenKind::Colon) {
                                None
                            } else {
                                Some(Box::new(self.parse_expr()?))
                            };
                        let max = if matches!(self.peek_kind(), TokenKind::Colon) {
                            self.bump();
                            if matches!(self.peek_kind(), TokenKind::RBracket) {
                                None
                            } else {
                                Some(Box::new(self.parse_expr()?))
                            }
                        } else {
                            None
                        };
                        self.expect(TokenKind::RBracket)?;
                        expr = Expr::Slice {
                            base: Box::new(expr),
                            low: None,
                            high,
                            max,
                        };
                    } else {
                        let first = self.parse_expr()?;
                        if matches!(self.peek_kind(), TokenKind::Colon) {
                            self.bump();
                            let high = if matches!(
                                self.peek_kind(),
                                TokenKind::RBracket | TokenKind::Colon
                            ) {
                                None
                            } else {
                                Some(Box::new(self.parse_expr()?))
                            };
                            let max = if matches!(self.peek_kind(), TokenKind::Colon) {
                                self.bump();
                                if matches!(self.peek_kind(), TokenKind::RBracket) {
                                    None
                                } else {
                                    Some(Box::new(self.parse_expr()?))
                                }
                            } else {
                                None
                            };
                            self.expect(TokenKind::RBracket)?;
                            expr = Expr::Slice {
                                base: Box::new(expr),
                                low: Some(Box::new(first)),
                                high,
                                max,
                            };
                        } else {
                            self.expect(TokenKind::RBracket)?;
                            expr = Expr::Index {
                                base: Box::new(expr),
                                index: Box::new(first),
                            };
                        }
                    }
                    self.no_struct_lit = idx_saved;
                }
                TokenKind::Question => {
                    self.bump();
                    expr = Expr::Try(Box::new(expr));
                }
                _ => break,
            }
        }
        Ok(expr)
    }

    /// After `{` has been consumed: parse a struct literal body for `name`.
    /// Returns `None` (and leaves the cursor after `{`) when the braces are not
    /// a clear named/positional literal — caller restores position.
    fn try_parse_struct_lit_tail(&mut self, name: String) -> Result<Option<Expr>, ParseError> {
        let is_named = matches!(self.peek_kind(), TokenKind::DotDot)
            || (matches!(self.peek_kind(), TokenKind::Ident(_)) && {
                let after_ident = self.pos + 1;
                after_ident < self.tokens.len()
                    && matches!(self.tokens[after_ident].kind, TokenKind::Colon)
            });
        if is_named {
            let mut fields = Vec::new();
            let mut update: Option<Box<Expr>> = None;
            while !matches!(self.peek_kind(), TokenKind::RBrace | TokenKind::Eof) {
                if matches!(self.peek_kind(), TokenKind::DotDot) {
                    self.bump();
                    if update.is_some() {
                        return Err(self.err(
                            "struct literal allows at most one `..base` update".into(),
                        ));
                    }
                    update = Some(Box::new(self.parse_expr()?));
                } else {
                    let fname = self.expect_ident()?;
                    self.expect(TokenKind::Colon)?;
                    let fval = self.parse_expr()?;
                    fields.push((fname, fval));
                }
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
            self.expect(TokenKind::RBrace)?;
            return Ok(Some(Expr::StructLit {
                name,
                fields,
                update,
            }));
        }
        // Positional: `Point{}` / `Point{a, b, …}` / `eng.Point{1, 2}`.
        if matches!(self.peek_kind(), TokenKind::RBrace) {
            self.bump();
            return Ok(Some(Expr::StructLitPos {
                name,
                values: vec![],
            }));
        }
        if self.peek_starts_expr() {
            let mut values = Vec::new();
            loop {
                values.push(self.parse_expr()?);
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                    if matches!(self.peek_kind(), TokenKind::RBrace) {
                        break; // trailing comma
                    }
                } else {
                    break;
                }
            }
            self.expect(TokenKind::RBrace)?;
            return Ok(Some(Expr::StructLitPos { name, values }));
        }
        Ok(None)
    }

    /// Split `f"…"` interior into lit / `{expr}` parts. Simple `{ident}` and
    /// `{a.b}` / `{a[i]}` via re-lexing the hole as a full expression.
    /// Supports format specs: `{n:02}`, `{n:x}`, `{f:.2f}`.
    fn parse_fstring(&mut self, raw: &str) -> Result<Expr, ParseError> {
        let mut parts: Vec<InterpPart> = Vec::new();
        let bytes = raw.as_bytes();
        let mut i = 0;
        let mut lit = String::new();
        while i < bytes.len() {
            if bytes[i] == b'{' {
                if i + 1 < bytes.len() && bytes[i + 1] == b'{' {
                    lit.push('{');
                    i += 2;
                    continue;
                }
                if !lit.is_empty() {
                    parts.push(InterpPart::Lit(std::mem::take(&mut lit)));
                }
                i += 1; // {
                let start = i;
                let mut depth = 1;
                while i < bytes.len() && depth > 0 {
                    if bytes[i] == b'{' {
                        depth += 1;
                    } else if bytes[i] == b'}' {
                        depth -= 1;
                    }
                    if depth > 0 {
                        i += 1;
                    }
                }
                if depth != 0 {
                    return Err(self.err("unterminated `{` in f-string".into()));
                }
                let hole = &raw[start..i];
                i += 1; // }
                // Split optional format spec: `{expr:spec}` at top-level `:`.
                // (Nested `{}` / colons inside the expr are rare; use fmt_sprintf* for complex.)
                let (expr_src, fmt_opt) = split_fstring_hole(hole);
                // Re-lex and parse the hole as an expression.
                let tokens = crate::lexer::Lexer::new(expr_src)
                    .tokenize()
                    .map_err(|e| self.err(format!("f-string hole: {e}")))?;
                let mut sub = Parser::new(tokens);
                let expr = sub.parse_expr()?;
                parts.push(InterpPart::Expr(expr, fmt_opt));
            } else if bytes[i] == b'}' {
                if i + 1 < bytes.len() && bytes[i + 1] == b'}' {
                    lit.push('}');
                    i += 2;
                    continue;
                }
                return Err(self.err("stray `}` in f-string (use `}}` to escape)".into()));
            } else {
                // UTF-8 safe: push one char
                let ch = raw[i..].chars().next().unwrap();
                lit.push(ch);
                i += ch.len_utf8();
            }
        }
        if !lit.is_empty() {
            parts.push(InterpPart::Lit(lit));
        }
        if parts.is_empty() {
            parts.push(InterpPart::Lit(String::new()));
        }
        // No holes → plain string
        if parts.iter().all(|p| matches!(p, InterpPart::Lit(_))) {
            let mut s = String::new();
            for p in parts {
                if let InterpPart::Lit(t) = p {
                    s.push_str(&t);
                }
            }
            return Ok(Expr::String(s));
        }
        Ok(Expr::StringInterp(parts))
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Int(n) => {
                self.bump();
                Ok(Expr::Int(n))
            }
            TokenKind::Float(n) => {
                self.bump();
                Ok(Expr::Float(n))
            }
            TokenKind::True => {
                self.bump();
                Ok(Expr::Bool(true))
            }
            TokenKind::False => {
                self.bump();
                Ok(Expr::Bool(false))
            }
            TokenKind::String(s) => {
                self.bump();
                Ok(Expr::String(s))
            }
            TokenKind::FString(raw) => {
                self.bump();
                self.parse_fstring(&raw)
            }
            TokenKind::Ident(name) => {
                self.bump();
                if name == "make" && matches!(self.peek_kind(), TokenKind::LParen) {
                    self.bump();
                    let ty = self.parse_type()?;
                    let (len, cap) = if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                        let len = Some(Box::new(self.parse_expr()?));
                        let cap = if matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                            Some(Box::new(self.parse_expr()?))
                        } else {
                            None
                        };
                        (len, cap)
                    } else {
                        (None, None)
                    };
                    self.expect(TokenKind::RParen)?;
                    return Ok(Expr::Make { ty, len, cap });
                }
                // Mako typed channel: `chan_open[T](cap)` or `chan_open<T>(cap)`
                if name == "chan_open"
                    && matches!(self.peek_kind(), TokenKind::LBracket | TokenKind::Lt)
                {
                    let close = if matches!(self.peek_kind(), TokenKind::LBracket) {
                        self.bump();
                        TokenKind::RBracket
                    } else {
                        self.bump();
                        TokenKind::Gt
                    };
                    let elem = self.parse_type()?;
                    self.expect(close)?;
                    self.expect(TokenKind::LParen)?;
                    let cap = self.parse_expr()?;
                    self.expect(TokenKind::RParen)?;
                    return Ok(Expr::ChanOpen {
                        elem,
                        cap: Box::new(cap),
                    });
                }
                // Struct literal: named `Person { name: "Ada", age: 36 }` or
                // positional `Point{1, 2}` / empty `Point{}`. Suppressed in
                // control-flow headers (see `no_struct_lit`) where `Ident {` opens
                // a block instead.
                if matches!(self.peek_kind(), TokenKind::LBrace) && !self.no_struct_lit {
                    let save = self.pos;
                    self.bump(); // {
                    if let Some(lit) = self.try_parse_struct_lit_tail(name.clone())? {
                        return Ok(lit);
                    }
                    self.pos = save;
                }
                Ok(Expr::Ident(name))
            }
            TokenKind::LParen => {
                self.bump();
                // Brackets are unambiguous → struct literals allowed inside.
                let saved = std::mem::replace(&mut self.no_struct_lit, false);
                // `()` empty not allowed as expr; `(e)` group; `(a, b)` tuple
                if matches!(self.peek_kind(), TokenKind::RParen) {
                    return Err(self.err("empty tuple `()` is not supported".into()));
                }
                let first = self.parse_expr()?;
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    let mut elems = vec![first];
                    while matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                        if matches!(self.peek_kind(), TokenKind::RParen) {
                            break; // trailing comma
                        }
                        elems.push(self.parse_expr()?);
                    }
                    self.expect(TokenKind::RParen)?;
                    self.no_struct_lit = saved;
                    return Ok(Expr::Tuple(elems));
                }
                self.expect(TokenKind::RParen)?;
                self.no_struct_lit = saved;
                Ok(first)
            }
            TokenKind::LBracket => {
                self.bump();
                let saved = std::mem::replace(&mut self.no_struct_lit, false);
                // Go-like `[]T(args)` conversion — not an array literal.
                if matches!(self.peek_kind(), TokenKind::RBracket) {
                    self.bump();
                    let inner = self.parse_type()?;
                    let ty = TypeExpr::Array(Box::new(inner));
                    self.expect(TokenKind::LParen)?;
                    let mut args = Vec::new();
                    if !matches!(self.peek_kind(), TokenKind::RParen) {
                        loop {
                            args.push(self.parse_expr()?);
                            if matches!(self.peek_kind(), TokenKind::Comma) {
                                self.bump();
                            } else {
                                break;
                            }
                        }
                    }
                    self.expect(TokenKind::RParen)?;
                    self.no_struct_lit = saved;
                    return Ok(Expr::Convert { ty, args });
                }
                let mut elems = Vec::new();
                if !matches!(self.peek_kind(), TokenKind::RBracket) {
                    loop {
                        elems.push(self.parse_expr()?);
                        if matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                            // Allow a trailing comma before the closing `]`.
                            if matches!(self.peek_kind(), TokenKind::RBracket) {
                                break;
                            }
                        } else {
                            break;
                        }
                    }
                }
                self.expect(TokenKind::RBracket)?;
                self.no_struct_lit = saved;
                Ok(Expr::Array(elems))
            }
            TokenKind::If => self.parse_if_expr(),
            TokenKind::LBrace => Ok(Expr::Block(self.parse_block()?)),
            TokenKind::Fn => self.parse_fn_lambda(),
            TokenKind::Pipe => self.parse_lambda(),
            TokenKind::Match => self.parse_match(),
            TokenKind::Fan => {
                self.bump();
                self.expect(TokenKind::LParen)?;
                let collection = self.parse_expr()?;
                self.expect(TokenKind::Comma)?;
                let mapper = self.parse_expr()?;
                self.expect(TokenKind::RParen)?;
                Ok(Expr::Fan {
                    collection: Box::new(collection),
                    mapper: Box::new(mapper),
                })
            }
            _ => Err(self.err(format!("unexpected expression: {}", self.peek_kind()))),
        }
    }

    fn parse_lambda(&mut self) -> Result<Expr, ParseError> {
        self.expect(TokenKind::Pipe)?;
        let mut params = Vec::new();
        if !matches!(self.peek_kind(), TokenKind::Pipe) {
            loop {
                params.push(self.expect_ident()?);
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
        }
        self.expect(TokenKind::Pipe)?;
        let body = if matches!(self.peek_kind(), TokenKind::LBrace) {
            Expr::Block(self.parse_block()?)
        } else {
            self.parse_expr()?
        };
        Ok(Expr::Lambda {
            params,
            body: Box::new(body),
        })
    }

    fn parse_fn_lambda(&mut self) -> Result<Expr, ParseError> {
        self.expect(TokenKind::Fn)?;
        self.expect(TokenKind::LParen)?;
        let mut params = Vec::new();
        if !matches!(self.peek_kind(), TokenKind::RParen) {
            loop {
                params.push(self.expect_ident()?);
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
        }
        self.expect(TokenKind::RParen)?;
        let body = if matches!(self.peek_kind(), TokenKind::LBrace) {
            Expr::Block(self.parse_block()?)
        } else {
            self.parse_expr()?
        };
        Ok(Expr::Lambda {
            params,
            body: Box::new(body),
        })
    }

    /// `if cond { … } else { … }` in expression position. An `else` branch is
    /// required; `else if` chains nest as a trailing if-expression. Each branch's
    /// value is the trailing expression of its block.
    fn parse_if_expr(&mut self) -> Result<Expr, ParseError> {
        self.expect(TokenKind::If)?;
        let cond = self.parse_header_expr()?;
        let then_block = self.parse_block()?;
        if !matches!(self.peek_kind(), TokenKind::Else) {
            return Err(self.err(
                "an `if` used as a value needs an `else` branch".into(),
            ));
        }
        self.bump(); // else
        let else_block = if matches!(self.peek_kind(), TokenKind::If) {
            // else-if: the else branch is itself an if-expression.
            Block {
                stmts: vec![Stmt::Expr(self.parse_if_expr()?)],
            }
        } else {
            self.parse_block()?
        };
        Ok(Expr::IfExpr {
            cond: Box::new(cond),
            then_block,
            else_block,
        })
    }

    fn parse_match(&mut self) -> Result<Expr, ParseError> {
        self.expect(TokenKind::Match)?;
        let scrutinee = self.parse_header_expr()?;
        self.expect(TokenKind::LBrace)?;
        let mut arms = Vec::new();
        while !matches!(self.peek_kind(), TokenKind::RBrace) {
            let pattern = self.parse_pattern()?;
            self.expect(TokenKind::FatArrow)?;
            let body = self.parse_expr()?;
            if matches!(self.peek_kind(), TokenKind::Comma) {
                self.bump();
            }
            arms.push(MatchArm { pattern, body });
        }
        self.expect(TokenKind::RBrace)?;
        Ok(Expr::Match {
            scrutinee: Box::new(scrutinee),
            arms,
        })
    }

    /// True if the cursor is on a contextual `switch` keyword leading a statement.
    /// Returns false when `switch` is used as an identifier so the name stays free.
    fn at_switch_kw(&self) -> bool {
        if !matches!(self.peek_kind(), TokenKind::Ident(s) if s == "switch") {
            return false;
        }
        // `switch` is an identifier (not the keyword) when the next token continues
        // an expression or assignment rather than opening a switch header/block.
        !matches!(
            self.tokens.get(self.pos + 1).map(|t| &t.kind),
            Some(
                TokenKind::Assign
                    | TokenKind::ColonAssign
                    | TokenKind::Dot
                    | TokenKind::LBracket
                    | TokenKind::LParen
                    | TokenKind::Comma
                    | TokenKind::Semicolon
            )
        )
    }

    /// True if the cursor is on a contextual `go` statement (`go f()`). Not the
    /// keyword when `go` is used as an identifier (`go = …`, `go(…)`, `go.x`,
    /// `go[i]`) so the name stays free.
    fn at_go_kw(&self) -> bool {
        if !matches!(self.peek_kind(), TokenKind::Ident(s) if s == "go") {
            return false;
        }
        matches!(
            self.tokens.get(self.pos + 1).map(|t| &t.kind),
            Some(TokenKind::Ident(_))
        )
    }

    /// `go f()` / `go obj.method()` — schedule the call onto the innermost
    /// enclosing crew as a fire-and-forget task (`crew.kick(call)`), matching Go's
    /// `go` but keeping Mako's structured-concurrency guarantee (the crew joins it).
    fn parse_go(&mut self) -> Result<Stmt, ParseError> {
        self.bump(); // `go`
        let call = self.parse_expr()?;
        if !matches!(call, Expr::Call { .. } | Expr::Method { .. }) {
            return Err(self.err("`go` requires a function call, e.g. `go worker()`".into()));
        }
        let Some(crew) = self.crew_stack.last().cloned() else {
            return Err(self.err(
                "`go` must be inside a `crew { … }` (Mako has no orphan tasks)".into(),
            ));
        };
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(Stmt::Expr(Expr::Kick {
            crew,
            expr: Box::new(call),
        }))
    }

    /// `detach f()` — spawn on the process-scoped detached nursery (not joined by
    /// the enclosing crew). Join later with `detached_join_all()`.
    fn parse_detach(&mut self) -> Result<Stmt, ParseError> {
        self.bump(); // `detach`
        let call = self.parse_expr()?;
        if !matches!(call, Expr::Call { .. } | Expr::Method { .. }) {
            return Err(self.err(
                "`detach` requires a function call, e.g. `detach worker()`".into(),
            ));
        }
        if matches!(self.peek_kind(), TokenKind::Semicolon) {
            self.bump();
        }
        Ok(Stmt::Expr(Expr::Kick {
            crew: "__detached__".into(),
            expr: Box::new(call),
        }))
    }

    fn at_detach_kw(&self) -> bool {
        if !matches!(self.peek_kind(), TokenKind::Ident(s) if s == "detach") {
            return false;
        }
        matches!(
            self.tokens.get(self.pos + 1).map(|t| &t.kind),
            Some(TokenKind::Ident(_))
        )
    }

    /// Go-style `switch`, desugared to an if/else-if chain — more faithful to Go
    /// than `match`: `case` takes arbitrary expressions, the tag is evaluated once,
    /// `default` is optional, and there is no exhaustiveness requirement.
    ///
    ///   switch x { case 1: … case 2, 3: … default: … }   // value switch
    ///   switch { case cond: … default: … }               // expression-less
    ///   switch x := f(); x { … }                          // with init clause
    fn parse_switch(&mut self) -> Result<Stmt, ParseError> {
        self.bump(); // `switch`
        // Optional init clause: `switch x := f(); x { … }`.
        let init = if self.has_stmt_before_block() {
            Some(self.parse_stmt()?)
        } else {
            None
        };
        // Optional scrutinee — absent for an expression-less `switch { … }`.
        let scrutinee = if matches!(self.peek_kind(), TokenKind::LBrace) {
            None
        } else {
            Some(self.parse_header_expr()?)
        };
        self.expect(TokenKind::LBrace)?;

        let mut cases: Vec<(Vec<Expr>, Block)> = Vec::new();
        let mut default_block: Option<Block> = None;
        while !matches!(self.peek_kind(), TokenKind::RBrace | TokenKind::Eof) {
            if matches!(self.peek_kind(), TokenKind::Default) {
                self.bump();
                self.expect(TokenKind::Colon)?;
                if default_block.is_some() {
                    return Err(self.err("switch has more than one `default`".into()));
                }
                default_block = Some(self.parse_switch_case_body()?);
            } else if matches!(self.peek_kind(), TokenKind::Ident(s) if s == "case") {
                self.bump();
                let mut vals = vec![self.parse_expr()?];
                while matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                    vals.push(self.parse_expr()?);
                }
                self.expect(TokenKind::Colon)?;
                cases.push((vals, self.parse_switch_case_body()?));
            } else {
                return Err(self.err("expected `case` or `default` in switch body".into()));
            }
        }
        self.expect(TokenKind::RBrace)?;

        // Evaluate the tag once. A bare identifier is already re-read cheaply and
        // side-effect free, so only a compound scrutinee needs a temp binding.
        let mut prelude: Vec<Stmt> = Vec::new();
        if let Some(init) = init {
            prelude.push(init);
        }
        let scrut_ref: Option<Expr> = match scrutinee {
            None => None,
            Some(Expr::Ident(name)) => Some(Expr::Ident(name)),
            Some(other) => {
                prelude.push(Stmt::Let {
                    name: "__mako_switch".into(),
                    mutable: false,
                    ownership: Ownership::None,
                    ty: None,
                    init: other,
                });
                Some(Expr::Ident("__mako_switch".into()))
            }
        };

        // Fold cases into an if/else-if chain, innermost `default` first.
        let mut else_block: Option<Block> = default_block;
        for (vals, body) in cases.into_iter().rev() {
            let cond = Self::switch_case_cond(vals, &scrut_ref);
            let if_stmt = Stmt::If {
                init: None,
                cond,
                then_block: body,
                else_block,
            };
            else_block = Some(Block {
                stmts: vec![if_stmt],
            });
        }
        // `else_block` now holds the whole chain (or just the default, or nothing).
        let mut body_stmts: Vec<Stmt> = else_block.map(|b| b.stmts).unwrap_or_default();

        if prelude.is_empty() {
            return Ok(match body_stmts.len() {
                0 => Stmt::If {
                    // Degenerate empty switch → no-op.
                    init: None,
                    cond: Expr::Bool(false),
                    then_block: Block { stmts: vec![] },
                    else_block: None,
                },
                _ => body_stmts.remove(0),
            });
        }
        // Scope the init / tag temp to the switch via a `true` block.
        prelude.append(&mut body_stmts);
        Ok(Stmt::If {
            init: None,
            cond: Expr::Bool(true),
            then_block: Block { stmts: prelude },
            else_block: None,
        })
    }

    /// Condition for one `case`: `scrut == v1 || scrut == v2 …` for a value switch,
    /// or `v1 || v2 …` when there is no scrutinee (expression-less switch).
    fn switch_case_cond(vals: Vec<Expr>, scrut: &Option<Expr>) -> Expr {
        let mut terms = vals.into_iter().map(|v| match scrut {
            Some(s) => Expr::Binary {
                op: BinOp::Eq,
                left: Box::new(s.clone()),
                right: Box::new(v),
            },
            None => v,
        });
        let mut cond = terms.next().expect("case has at least one expression");
        for t in terms {
            cond = Expr::Binary {
                op: BinOp::Or,
                left: Box::new(cond),
                right: Box::new(t),
            };
        }
        cond
    }

    /// Statements of one `case`/`default` arm, up to the next `case`/`default`/`}`.
    fn parse_switch_case_body(&mut self) -> Result<Block, ParseError> {
        let mut stmts = Vec::new();
        while !matches!(
            self.peek_kind(),
            TokenKind::RBrace | TokenKind::Default | TokenKind::Eof
        ) && !matches!(self.peek_kind(), TokenKind::Ident(s) if s == "case")
        {
            stmts.push(self.parse_stmt()?);
        }
        Ok(Block { stmts })
    }

    fn parse_pattern(&mut self) -> Result<Pattern, ParseError> {
        let first = self.parse_pattern_atom()?;
        if !matches!(self.peek_kind(), TokenKind::Pipe) {
            return Ok(first);
        }
        let mut alts = vec![first];
        while matches!(self.peek_kind(), TokenKind::Pipe) {
            self.bump();
            alts.push(self.parse_pattern_atom()?);
        }
        Ok(Pattern::Or(alts))
    }

    fn parse_pattern_atom(&mut self) -> Result<Pattern, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Ident(name) if name == "_" => {
                self.bump();
                Ok(Pattern::Wildcard)
            }
            TokenKind::Ident(name) => {
                self.bump();
                // Pack-qualified paths (same rewrite as type annotations / lits):
                //   eng.Point { x }           → struct pattern eng__Point
                //   eng.Red / eng.Green(n)    → unit / payload variant (bare name)
                //   eng.Color.Red / .Green(n) → type-qualified variant
                if matches!(self.peek_kind(), TokenKind::Dot) {
                    self.bump(); // .
                    let part2 = self.expect_ident()?;
                    if matches!(self.peek_kind(), TokenKind::Dot) {
                        // eng.Color.Red or eng.Color.Green(...)
                        self.bump();
                        let variant = self.expect_ident()?;
                        return self.finish_variant_pattern(variant);
                    }
                    if matches!(self.peek_kind(), TokenKind::LBrace) {
                        // eng.Point { ... }
                        let name = format!("{name}__{part2}");
                        return self.finish_struct_pattern(name);
                    }
                    // eng.Red or eng.Green(...)
                    return self.finish_variant_pattern(part2);
                }
                if matches!(self.peek_kind(), TokenKind::LParen) {
                    self.finish_variant_pattern(name)
                } else if matches!(self.peek_kind(), TokenKind::LBrace) {
                    self.finish_struct_pattern(name)
                } else {
                    Ok(Pattern::Ident(name))
                }
            }
            TokenKind::Int(_) | TokenKind::True | TokenKind::False | TokenKind::String(_) => {
                Ok(Pattern::Literal(self.parse_primary()?))
            }
            TokenKind::LParen => {
                self.bump();
                let mut elems = Vec::new();
                if !matches!(self.peek_kind(), TokenKind::RParen) {
                    loop {
                        elems.push(self.parse_pattern()?);
                        if matches!(self.peek_kind(), TokenKind::Comma) {
                            self.bump();
                            if matches!(self.peek_kind(), TokenKind::RParen) {
                                break;
                            }
                        } else {
                            break;
                        }
                    }
                }
                self.expect(TokenKind::RParen)?;
                if elems.len() == 1 {
                    return Ok(elems.into_iter().next().unwrap());
                }
                if elems.is_empty() {
                    return Err(self.err("empty tuple pattern is not supported".into()));
                }
                Ok(Pattern::Tuple(elems))
            }
            _ => Err(self.err("expected pattern".into())),
        }
    }

    /// `Name(...)` payload variant, or bare `Name` unit variant pattern.
    fn finish_variant_pattern(&mut self, name: String) -> Result<Pattern, ParseError> {
        if matches!(self.peek_kind(), TokenKind::LParen) {
            self.bump();
            let mut bindings = Vec::new();
            if !matches!(self.peek_kind(), TokenKind::RParen) {
                loop {
                    bindings.push(self.parse_pattern()?);
                    if matches!(self.peek_kind(), TokenKind::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
            }
            self.expect(TokenKind::RParen)?;
            Ok(Pattern::Variant { name, bindings })
        } else {
            // Unit variant patterns share `Ident` with irrefutable binds; typecheck
            // decides via the scrutinee enum.
            Ok(Pattern::Ident(name))
        }
    }

    /// Struct pattern body after the type name (leading `{` still to consume).
    fn finish_struct_pattern(&mut self, name: String) -> Result<Pattern, ParseError> {
        self.expect(TokenKind::LBrace)?;
        let mut fields = Vec::new();
        if !matches!(self.peek_kind(), TokenKind::RBrace) {
            loop {
                let fname = self.expect_ident()?;
                let pat = if matches!(self.peek_kind(), TokenKind::Colon) {
                    self.bump();
                    self.parse_pattern()?
                } else {
                    // Shorthand: `x` means `x: x`
                    Pattern::Ident(fname.clone())
                };
                fields.push((fname, pat));
                if matches!(self.peek_kind(), TokenKind::Comma) {
                    self.bump();
                    if matches!(self.peek_kind(), TokenKind::RBrace) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        self.expect(TokenKind::RBrace)?;
        Ok(Pattern::Struct { name, fields })
    }

    fn expect_ident(&mut self) -> Result<String, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Ident(s) => {
                self.bump();
                Ok(s)
            }
            // Allow some keywords as identifiers in type/name positions when needed
            other => Err(self.err(format!("expected identifier, found {other}"))),
        }
    }

    /// `for` binders: `i` / `_` / `i, v` / `_, v`
    fn expect_binder(&mut self) -> Result<String, ParseError> {
        self.expect_ident()
    }

    /// Go-like forms:
    ///   `for i, v in range s { ... }`
    ///   `for i in range s { ... }`
    ///   `for _, v in range s { ... }`
    ///   `for range s { ... }`
    /// Legacy:
    ///   `for i in n` / `for v in arr`
    fn parse_while(&mut self, label: Option<String>) -> Result<Stmt, ParseError> {
        self.bump(); // while
        let cond = self.parse_header_expr()?;
        let body = self.parse_block()?;
        Ok(Stmt::While { label, cond, body })
    }

    /// Parse the post clause of a C-style `for` (an assignment / `i++` / expr).
    /// Struct literals are suppressed because the loop body `{` follows.
    fn parse_simple_post(&mut self) -> Result<Stmt, ParseError> {
        let saved = std::mem::replace(&mut self.no_struct_lit, true);
        let r = self.parse_stmt();
        self.no_struct_lit = saved;
        r
    }

    /// True if `in` appears at bracket-depth 0 before the next `{` — the marker of
    /// a range `for` (`for i in xs`) versus a while-style `for cond {}`.
    fn has_in_before_block(&self) -> bool {
        let mut depth: i32 = 0;
        let mut i = self.pos;
        while i < self.tokens.len() {
            match &self.tokens[i].kind {
                TokenKind::In if depth == 0 => return true,
                TokenKind::LBrace if depth == 0 => return false,
                TokenKind::LParen | TokenKind::LBracket | TokenKind::LBrace => depth += 1,
                TokenKind::RParen | TokenKind::RBracket | TokenKind::RBrace => depth -= 1,
                TokenKind::Eof => return false,
                _ => {}
            }
            i += 1;
        }
        false
    }

    fn parse_for(&mut self, label: Option<String>) -> Result<Stmt, ParseError> {
        self.bump(); // for
        // `for { ... }` — infinite loop (Go form) → `while true`.
        if matches!(self.peek_kind(), TokenKind::LBrace) {
            let body = self.parse_block()?;
            return Ok(Stmt::While {
                label,
                cond: Expr::Bool(true),
                body,
            });
        }
        // C-style three-clause `for init; cond; post { … }` — a top-level `;`
        // before the block distinguishes it from the range and while forms.
        if self.has_stmt_before_block() {
            let init = self.parse_stmt()?; // consumes the first `;`
            let cond = self.parse_expr()?;
            self.expect(TokenKind::Semicolon)?;
            let post = self.parse_simple_post()?;
            let body = self.parse_block()?;
            return Ok(Stmt::CFor {
                label,
                init: Box::new(init),
                cond,
                post: Box::new(post),
                body,
            });
        }
        // `for range expr { ... }` — no binders
        if matches!(self.peek_kind(), TokenKind::Range) {
            self.bump();
            let iter = self.parse_header_expr()?;
            let body = self.parse_block()?;
            return Ok(Stmt::For {
                label,
                binders: vec![],
                is_range: true,
                iter,
                body,
            });
        }
        // `for cond { ... }` — while-style loop (no `in` before the block).
        if !self.has_in_before_block() {
            let cond = self.parse_header_expr()?;
            let body = self.parse_block()?;
            return Ok(Stmt::While { label, cond, body });
        }
        let mut binders = vec![self.expect_binder()?];
        if matches!(self.peek_kind(), TokenKind::Comma) {
            self.bump();
            binders.push(self.expect_binder()?);
        }
        if binders.len() > 2 {
            return Err(self.err("for supports at most two binders (index, value)".into()));
        }
        self.expect(TokenKind::In)?;
        let is_range = if matches!(self.peek_kind(), TokenKind::Range) {
            self.bump();
            true
        } else {
            false
        };
        if binders.len() == 2 && !is_range {
            return Err(self.err("two binders require `range` (e.g. `for i, v in range s`)".into()));
        }
        let iter = self.parse_header_expr()?;
        let body = self.parse_block()?;
        Ok(Stmt::For {
            label,
            binders,
            is_range,
            iter,
            body,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;

    #[test]
    fn parse_simple_fn() {
        let src = "fn add(a: int, b: int) -> int { return a + b }";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let prog = Parser::new(tokens).parse().unwrap();
        assert_eq!(prog.items.len(), 1);
    }

    #[test]
    fn parse_grouped_import_paren() {
        let src = r#"
import (
    "strings"
    lib "./x.mko"
    "path" as p
)
fn main() {}
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let prog = Parser::new(tokens).parse().unwrap();
        let imports: Vec<_> = prog
            .items
            .iter()
            .filter_map(|i| match i {
                Item::Import { path, alias, .. } => Some((path.as_str(), alias.as_deref())),
                _ => None,
            })
            .collect();
        assert_eq!(imports.len(), 3);
        assert_eq!(imports[0], ("strings", None));
        assert_eq!(imports[1], ("./x.mko", Some("lib")));
        assert_eq!(imports[2], ("path", Some("p")));
    }

    #[test]
    fn parse_grouped_import_brace() {
        let src = r#"import { "strings"; "path" } fn main() {}"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let prog = Parser::new(tokens).parse().unwrap();
        let n = prog
            .items
            .iter()
            .filter(|i| matches!(i, Item::Import { .. }))
            .count();
        assert_eq!(n, 2);
    }

    /// Recovery must not eat the next top-level `fn`/`struct` after a broken item.
    #[test]
    fn parse_with_errors_keeps_following_good_decls() {
        let src = r#"
fn broken(
fn good() { return 1 }
struct AlsoBroken {
fn still_ok() { return 2 }
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let (prog, errs) = Parser::new(tokens).parse_with_errors();
        assert!(
            errs.len() >= 2,
            "expected ≥2 parse errors, got {}: {:?}",
            errs.len(),
            errs
        );
        let fn_names: Vec<&str> = prog
            .items
            .iter()
            .filter_map(|i| match i {
                Item::Fn(f) => Some(f.name.as_str()),
                _ => None,
            })
            .collect();
        assert!(
            fn_names.contains(&"good"),
            "recovery ate `good`; items={fn_names:?}"
        );
        assert!(
            fn_names.contains(&"still_ok"),
            "recovery ate `still_ok`; items={fn_names:?}"
        );
    }
}


/// Parsed item-level attribute (`#[…]`).
enum ItemAttr {
    Derive(Vec<String>),
    Stable,
    Deprecated(String),
}

/// Go-style: names starting with an uppercase letter are package-exported.
fn is_exported_name(name: &str) -> bool {
    name.chars().next().map(|c| c.is_ascii_uppercase()).unwrap_or(false)
}

/// Maps a compound-assignment token (`+=`, `-=`, `*=`, `/=`, `%=`) to its binary
/// Split an f-string hole into expression source and optional format spec.
/// `{n:02}` → (`n`, Some("02")); `{name}` → (`name`, None).
/// Uses the first top-level `:` (not inside nested `{}`).
fn split_fstring_hole(hole: &str) -> (&str, Option<String>) {
    let bytes = hole.as_bytes();
    let mut depth = 0i32;
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'{' => depth += 1,
            b'}' => depth -= 1,
            b':' if depth == 0 => {
                let expr = hole[..i].trim();
                // Keep spaces (the ' ' sign flag). Only strip CR/LF/TAB from the right.
                let raw = &hole[i + 1..];
                let spec =
                    raw.trim_end_matches(|c: char| c == '\n' || c == '\r' || c == '\t');
                if !spec.is_empty() {
                    return (expr, Some(spec.to_string()));
                }
                return (expr, None);
            }
            _ => {}
        }
        i += 1;
    }
    (hole.trim(), None)
}

/// operator, so `x += e` desugars to `x = x <op> e`.
fn compound_binop(kind: &TokenKind) -> Option<BinOp> {
    match kind {
        TokenKind::PlusEq => Some(BinOp::Add),
        TokenKind::MinusEq => Some(BinOp::Sub),
        TokenKind::StarEq => Some(BinOp::Mul),
        TokenKind::SlashEq => Some(BinOp::Div),
        TokenKind::PercentEq => Some(BinOp::Mod),
        _ => None,
    }
}

/// Maps `++` / `--` to the operator used against a literal `1`.
fn incdec_binop(kind: &TokenKind) -> Option<BinOp> {
    match kind {
        TokenKind::PlusPlus => Some(BinOp::Add),
        TokenKind::MinusMinus => Some(BinOp::Sub),
        _ => None,
    }
}
