//! Abstract syntax tree for Mako.

use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub struct Program {
    pub items: Vec<Item>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Item {
    Fn(FnDef),
    Struct(StructDef),
    Enum(EnumDef),
    Actor(ActorDef),
    Interface(InterfaceDef),
    ExternC(ExternCDef),
    Const(ConstDef),
    /// Mako-native method block: `on Point { fn distance(self) -> int { … } }`
    /// Desugars to free functions `Point_distance` (backward compatible with existing style).
    On(OnDef),
    /// Unit name: preferred `pack lib`, dual `package lib`.
    /// Optional; for libraries, the name is the default pull qualifier.
    Package {
        name: String,
    },
    /// Pull another file or package (always package-qualified when Normal).
    ///
    /// Preferred Mako spellings:
    /// ```text
    /// pull "strings"                   // strings.contains → strings__contains
    /// pull "./helper.mko" as lib       // explicit alias
    /// pull (
    ///     "fmt"
    ///     "./math.mko" as math
    /// )
    /// ```
    /// Dual / specialized (still accepted):
    /// ```text
    /// import "strings"                 // dual of pull
    /// import lib "./helper.mko"
    /// pull _ "sidefx"                  // blank: load only
    /// pull . "./local.mko"             // dot: no prefix
    /// ```
    Import {
        path: String,
        /// Explicit alias (`"path" as lib` preferred; `lib "path"` dual).
        /// `None` → derive from imported `pack` clause or path basename.
        alias: Option<String>,
        mode: ImportMode,
    },
}

/// How an import binds names into the importer.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Default)]
pub enum ImportMode {
    /// Qualify with package name / alias: `strings.contains(...)`.
    #[default]
    Normal,
    /// `_ "path"` — compile dependency only; no exported names.
    Blank,
    /// `. "path"` — merge symbols without a prefix (specialized).
    Dot,
}

/// `on TypeName { fn method… }` — Mako method surface (not Rust `impl`, not Go methods).
#[derive(Debug, Clone, PartialEq)]
pub struct OnDef {
    pub ty: String,
    pub methods: Vec<FnDef>,
    pub exported: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ConstDef {
    pub name: String,
    pub value: Expr,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ActorDef {
    pub name: String,
    pub receives: Vec<ReceiveArm>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ReceiveArm {
    pub message: String,
    pub body: Block,
}

#[derive(Debug, Clone, PartialEq)]
pub struct InterfaceDef {
    pub name: String,
    pub methods: Vec<(String, Vec<TypeExpr>, TypeExpr)>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExternCDef {
    pub name: String,
    pub params: Vec<Param>,
    pub ret: Option<TypeExpr>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct FnDef {
    pub name: String,
    /// User generics: `fn id[T](x: T) -> T` or `fn id<T>(x: T) -> T` (dual syntax).
    pub type_params: Vec<String>,
    pub params: Vec<Param>,
    pub ret: Option<TypeExpr>,
    pub body: Block,
    /// `export fn` — package boundary marker (enforced when visibility=explicit).
    pub exported: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Param {
    pub name: String,
    pub ty: TypeExpr,
    pub mutable: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct StructDef {
    pub name: String,
    pub fields: Vec<(String, TypeExpr)>,
    /// e.g. ["json"] from `#[derive(json)]`
    pub derives: Vec<String>,
    pub exported: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct EnumDef {
    pub name: String,
    pub variants: Vec<EnumVariant>,
    pub exported: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct EnumVariant {
    pub name: String,
    pub fields: Vec<TypeExpr>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TypeExpr {
    Named(String),
    Generic(String, Vec<TypeExpr>),
    /// Go-like `map[K]V` (parsed as Generic("map", [K, V]) or this form).
    Map(Box<TypeExpr>, Box<TypeExpr>),
    Array(Box<TypeExpr>),
    Fn(Vec<TypeExpr>, Box<TypeExpr>),
    /// Product type: `(int, string)` — Mako tuples (additive).
    Tuple(Vec<TypeExpr>),
}

impl fmt::Display for TypeExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TypeExpr::Named(n) => write!(f, "{n}"),
            TypeExpr::Generic(n, args) => {
                write!(f, "{n}[")?;
                for (i, a) in args.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{a}")?;
                }
                write!(f, "]")
            }
            TypeExpr::Map(k, v) => write!(f, "map[{k}]{v}"),
            TypeExpr::Array(t) => write!(f, "[]{t}"),
            TypeExpr::Fn(params, ret) => {
                write!(f, "fn(")?;
                for (i, p) in params.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{p}")?;
                }
                write!(f, ") -> {ret}")
            }
            TypeExpr::Tuple(elems) => {
                write!(f, "(")?;
                for (i, e) in elems.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{e}")?;
                }
                write!(f, ")")
            }
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Block {
    pub stmts: Vec<Stmt>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Stmt {
    Let {
        name: String,
        mutable: bool,
        /// `hold` = unique/move; `share` = RC; none = ordinary
        ownership: Ownership,
        ty: Option<TypeExpr>,
        init: Expr,
    },
    /// Go multi-return unpack: `let a, b = f()` or `a, b := f()` when `f` returns a tuple.
    LetMulti {
        names: Vec<String>,
        mutable: bool,
        init: Expr,
    },
    /// Go-like comma-ok map lookup: `let v, ok = m[k]`
    LetCommaOk {
        value: String,
        ok: String,
        mutable: bool,
        base: Expr,
        index: Expr,
    },
    Assign {
        name: String,
        value: Expr,
    },
    /// `s[i] = v` — mutate slice element (shared backing visible to aliases)
    IndexAssign {
        base: Expr,
        index: Expr,
        value: Expr,
    },
    /// `base.field = value`
    FieldAssign {
        base: Expr,
        field: String,
        value: Expr,
    },
    Expr(Expr),
    Return(Option<Expr>),
    If {
        /// Optional Go-style init clause: `if x := f(); cond { … }`. Runs before
        /// `cond` and is scoped to the whole if/else.
        init: Option<Box<Stmt>>,
        cond: Expr,
        then_block: Block,
        else_block: Option<Block>,
    },
    While {
        /// Optional loop label: `outer: while …` for `break outer` / `continue outer`.
        label: Option<String>,
        cond: Expr,
        body: Block,
    },
    /// `for binders in [range] iter { body }`
    ///
    /// Binders: 0 (`for range xs`), 1 (legacy or index-only), or 2 (`i, v` / `_, v`).
    /// `"_"` is the blank identifier (not bound).
    For {
        label: Option<String>,
        binders: Vec<String>,
        is_range: bool,
        iter: Expr,
        body: Block,
    },
    /// Go C-style three-clause loop: `for init; cond; post { body }`. `init` and
    /// `post` are simple statements (short decl / assignment); `cond` re-evaluates
    /// each iteration. `continue` runs `post` (lowered to a real C `for`).
    CFor {
        label: Option<String>,
        init: Box<Stmt>,
        cond: Expr,
        post: Box<Stmt>,
        body: Block,
    },
    /// `break` or `break label`
    Break(Option<String>),
    /// `continue` or `continue label`
    Continue(Option<String>),
    /// `defer { ... }` — run on function exit (LIFO), including before `return`
    Defer {
        body: Block,
    },
    /// Structured concurrency scope: `crew name { ... }`
    Crew {
        name: String,
        body: Block,
    },
    /// Region allocator: `arena name { ... }` — freed on exit
    Arena {
        name: String,
        body: Block,
    },
    /// `select timeout ms { ch => { ... } default => { ... } }`
    /// Channel arms + optional default/`_` when nothing ready (timeout or immediate).
    Select {
        timeout_ms: Expr,
        arms: Vec<(String, Block)>,
        /// `default` or `_` arm body when select returns -1
        default_arm: Option<Block>,
    },
    /// `unsafe { ... }` — opt out of debug bounds checks for indexing inside
    /// (explicit, rare; see docs/SECURITY.md). Ownership/NLL still apply.
    Unsafe {
        body: Block,
    },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Ownership {
    #[default]
    None,
    Hold,
    Share,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    Int(i64),
    Float(f64),
    Bool(bool),
    String(String),
    Ident(String),
    Binary {
        op: BinOp,
        left: Box<Expr>,
        right: Box<Expr>,
    },
    Unary {
        op: UnaryOp,
        expr: Box<Expr>,
    },
    Call {
        callee: Box<Expr>,
        args: Vec<Expr>,
    },
    Method {
        receiver: Box<Expr>,
        method: String,
        args: Vec<Expr>,
    },
    Index {
        base: Box<Expr>,
        index: Box<Expr>,
    },
    /// Go-like slice expression: `a[low:high]` or `a[low:high:max]` (omitted ends = None).
    Slice {
        base: Box<Expr>,
        low: Option<Box<Expr>>,
        high: Option<Box<Expr>>,
        max: Option<Box<Expr>>,
    },
    Field {
        base: Box<Expr>,
        field: String,
    },
    /// `Person { name: "Ada", age: 36 }`
    StructLit {
        name: String,
        fields: Vec<(String, Expr)>,
    },
    /// Go-style positional literal `Point{1, 2}` — values in declaration order.
    /// Field names are resolved against the struct definition during type-check
    /// and codegen.
    StructLitPos {
        name: String,
        values: Vec<Expr>,
    },
    Array(Vec<Expr>),
    /// Tuple literal: `(a, b)` (single `(a)` remains grouping).
    Tuple(Vec<Expr>),
    /// Go-like `[]byte("x")` / `[]T(arg)` conversion call.
    Convert {
        ty: TypeExpr,
        args: Vec<Expr>,
    },
    /// Go-like `make([]T, len[, cap])` or `make(map[K]V[, hint])`.
    Make {
        ty: TypeExpr,
        len: Option<Box<Expr>>,
        cap: Option<Box<Expr>>,
    },
    /// Mako typed channel open: `chan_open[string](4)` (int form equals `chan_new`).
    ChanOpen {
        elem: TypeExpr,
        cap: Box<Expr>,
    },
    Lambda {
        params: Vec<String>,
        body: Box<Expr>,
    },
    Match {
        scrutinee: Box<Expr>,
        arms: Vec<MatchArm>,
    },
    /// `if cond { … } else { … }` used as an expression. Each branch's value is
    /// the trailing expression of its block; both branches must be present and
    /// yield compatible types.
    IfExpr {
        cond: Box<Expr>,
        then_block: Block,
        else_block: Block,
    },
    Try(Box<Expr>),
    Block(Block),
    /// `crew.kick(expr)` — schedule work on a crew
    Kick {
        crew: String,
        expr: Box<Expr>,
    },
    /// `job.join()` — wait for a kicked job
    Join(Box<Expr>),
    /// `fan(collection, mapper)` — data-parallel map
    Fan {
        collection: Box<Expr>,
        mapper: Box<Expr>,
    },
}

#[derive(Debug, Clone, PartialEq)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Pattern {
    Wildcard,
    Ident(String),
    Variant {
        name: String,
        bindings: Vec<String>,
    },
    Literal(Expr),
    /// Fallthrough-free multi-match: `0 | 1 | 2 => ...`
    Or(Vec<Pattern>),
    /// Tuple destructure: `(a, b)`
    Tuple(Vec<Pattern>),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    /// Bitwise `&`
    BitAnd,
    /// Bitwise `|` (infix; leading `|` still starts a lambda)
    BitOr,
    /// Bitwise `^`
    BitXor,
    /// Bit clear `&^` (Go)
    BitClear,
    /// `<<`
    Shl,
    /// `>>`
    Shr,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnaryOp {
    Neg,
    Not,
    /// Bitwise complement `^x` (Go)
    BitNot,
}
