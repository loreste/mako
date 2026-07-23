//! Type checker with local inference, Option/Result, and no nil.
//!
//! Hold/share NLL uses CFG-aware joins (`nll` module): diverge-aware if/match,
//! loop fixpoints only when the body can re-enter the header, const-bool edge
//! pruning.

mod nll;

use std::collections::{HashMap, HashSet};

use crate::ast::*;
use nll::{block_always_diverges, const_bool, expr_always_diverges, loop_body_may_reach_header};

// A chunk returns Some(result) for a recognized builtin and None to continue.
// The closure preserves the existing early-return behavior inside each arm.
macro_rules! check_builtin_match {
    ($name:expr, { $($arms:tt)* }) => {{
        let mut unmatched = false;
        let result = (|| -> Result<Type, TypeError> {
            match $name {
                $($arms)*
                _ => {
                    unmatched = true;
                    Ok(Type::Void)
                }
            }
        })();
        if unmatched { None } else { Some(result) }
    }};
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Type {
    Void,
    /// Platform natural integer (Go-like). On current 64-bit C targets → `int64_t`,
    /// but distinct from `Int64` / `Int32` in the type system (explicit conversion required).
    Int,
    /// Always 64-bit (`int64_t`).
    Int64,
    /// Always 32-bit (`int32_t` in C; stored/passed as int64_t with range checks later).
    Int32,
    /// Always 8-bit signed (`int8_t`); stored as int64_t with range check on convert.
    Int8,
    /// Always 64-bit unsigned (`uint64_t`); stored as uint64_t / int64_t bit pattern.
    UInt64,
    /// Unsigned 8-bit (Go `byte`); element type of `[]byte`.
    Byte,
    Float,
    Bool,
    String,
    Array(Box<Type>),
    /// Go-like `map[K]V`
    Map(Box<Type>, Box<Type>),
    Named(String),
    Option(Box<Type>),
    Result(Box<Type>, Box<Type>),
    Job(Box<Type>),
    Chan(Box<Type>),
    /// First-class FIFO message queue: `queue[T]` (payload owned; no GC).
    Queue(Box<Type>),
    /// Parsed GraphQL document handle (`graphql_parse` / HTTP body).
    Graphql,
    Fn(Vec<Type>, Box<Type>),
    Struct {
        name: String,
        fields: Vec<(String, Type)>,
    },
    Enum {
        name: String,
        variants: Vec<(String, Vec<Type>)>,
    },
    /// Dynamic interface value (fat pointer seed).
    Interface {
        name: String,
    },
    Crew,
    Arena,
    /// Growable string buffer (`str_builder`)
    StrBuilder,
    /// Mutex (`mutex_new`)
    Mutex,
    /// Readers–writer mutex (`rwmutex_new`)
    RWMutex,
    /// Concurrent hashmap (`cmap_new`)
    CMap,
    /// Memory-mapped file (`mmap_open`)
    MMap,
    /// Event loop (`evloop_new`)
    EvLoop,
    /// Binary buffer (`buf_pack_new`)
    Buf,
    /// Game UDP socket (`game_udp_bind`)
    GameUDP,
    /// Consistent hash ring (`chash_new`)
    CHash,
    /// Rate limiter (`ratelimit_new`)
    RateLimiter,
    /// Circuit breaker (`breaker_new`)
    CircuitBreaker,
    /// HTTP engine (`httpengine_new`)
    HttpEngine,
    /// Buffered reader (`buf_reader_new`)
    BufReader,
    /// Buffered writer (`buf_writer_new`)
    BufWriter,
    /// Typed HTTP request (`http_request_parse`)
    HttpRequest,
    /// Unified SQL DB handle (`sql_open_sqlite` / `sql_open_postgres`)
    SqlDB,
    /// WaitGroup (`wait_group_new`)
    WaitGroup,
    /// RFC 4122 UUID (16 bytes)
    Uuid,
    /// Product type `(T, U, …)`
    Tuple(Vec<Type>),
}

impl Type {
    pub fn display(&self) -> String {
        match self {
            Type::Void => "void".into(),
            Type::Int => "int".into(),
            Type::Int64 => "int64".into(),
            Type::Int32 => "int32".into(),
            Type::Int8 => "int8".into(),
            Type::UInt64 => "uint64".into(),
            Type::Byte => "byte".into(),
            Type::Float => "float".into(),
            Type::Bool => "bool".into(),
            Type::String => "string".into(),
            Type::Array(t) => format!("[]{}", t.display()),
            Type::Map(k, v) => format!("map[{}]{}", k.display(), v.display()),
            Type::Named(n) => n.clone(),
            Type::Option(t) => format!("Option[{}]", t.display()),
            Type::Result(t, e) => format!("Result[{}, {}]", t.display(), e.display()),
            Type::Job(t) => format!("Job[{}]", t.display()),
            Type::Chan(t) => format!("chan[{}]", t.display()),
            Type::Queue(t) => format!("queue[{}]", t.display()),
            Type::Graphql => "Graphql".into(),
            Type::Fn(params, ret) => {
                let ps: Vec<_> = params.iter().map(|p| p.display()).collect();
                format!("fn({}) -> {}", ps.join(", "), ret.display())
            }
            Type::Struct { name, .. } => name.clone(),
            Type::Enum { name, .. } => name.clone(),
            Type::Interface { name } => name.clone(),
            Type::Crew => "Crew".into(),
            Type::Arena => "Arena".into(),
            Type::StrBuilder => "StrBuilder".into(),
            Type::Mutex => "Mutex".into(),
            Type::RWMutex => "RWMutex".into(),
            Type::CMap => "CMap".into(),
            Type::MMap => "MMap".into(),
            Type::EvLoop => "EvLoop".into(),
            Type::Buf => "Buf".into(),
            Type::GameUDP => "GameUDP".into(),
            Type::CHash => "CHash".into(),
            Type::RateLimiter => "RateLimiter".into(),
            Type::CircuitBreaker => "CircuitBreaker".into(),
            Type::HttpEngine => "HttpEngine".into(),
            Type::BufReader => "BufReader".into(),
            Type::BufWriter => "BufWriter".into(),
            Type::HttpRequest => "HttpRequest".into(),
            Type::SqlDB => "SqlDB".into(),
            Type::WaitGroup => "WaitGroup".into(),
            Type::Uuid => "Uuid".into(),
            Type::Tuple(elems) => {
                let e: Vec<_> = elems.iter().map(|t| t.display()).collect();
                format!("({})", e.join(", "))
            }
        }
    }

    /// Stable monomorphization tag for a concrete type (used in specialized fn names).
    pub fn mono_tag(&self) -> String {
        match self {
            Type::Int => "int".into(),
            Type::Int64 => "int64".into(),
            Type::Int32 => "int32".into(),
            Type::Int8 => "int8".into(),
            Type::UInt64 => "uint64".into(),
            Type::Byte => "byte".into(),
            Type::Float => "float".into(),
            Type::Bool => "bool".into(),
            Type::String => "string".into(),
            Type::Void => "void".into(),
            Type::Array(t) => format!("arr_{}", t.mono_tag()),
            Type::Chan(t) => format!("chan_{}", t.mono_tag()),
            Type::Queue(t) => format!("queue_{}", t.mono_tag()),
            Type::Graphql => "Graphql".into(),
            Type::Map(k, v) => format!("map_{}_{}", k.mono_tag(), v.mono_tag()),
            Type::Option(t) => format!("Option_{}_", t.mono_tag()),
            Type::Result(t, e) => format!("Result_{}_{}", t.mono_tag(), e.mono_tag()),
            Type::Named(n) => n.clone(),
            Type::Tuple(elems) => {
                let e: Vec<_> = elems.iter().map(|t| t.mono_tag()).collect();
                format!("tup_{}", e.join("_"))
            }
            other => other
                .display()
                .chars()
                .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
                .collect(),
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum TypeError {
    #[error("{message}")]
    At {
        message: String,
        hint: Option<String>,
        line: usize,
        col: usize,
    },
}

impl TypeError {
    pub fn new(message: impl Into<String>) -> Self {
        TypeError::At {
            message: message.into(),
            hint: None,
            line: 0,
            col: 0,
        }
    }

    #[allow(dead_code)]
    pub fn at(line: usize, col: usize, message: impl Into<String>) -> Self {
        TypeError::At {
            message: message.into(),
            hint: None,
            line,
            col,
        }
    }

    pub fn hint(mut self, hint: impl Into<String>) -> Self {
        let TypeError::At { hint: h, .. } = &mut self;
        *h = Some(hint.into());
        self
    }

    #[allow(dead_code)]
    pub fn message(&self) -> &str {
        match self {
            TypeError::At { message, .. } => message,
        }
    }
}

/// Enum variant constructor: which enum and payload types.
#[derive(Debug, Clone)]
struct VariantCtor {
    enum_name: String,
    fields: Vec<Type>,
}

/// Static environment facts for a first-class function value.
///
/// `captured` is the flattened set of outer locals reachable through the
/// closure environment. `unsafe_mut` is the subset that is mutable and is not
/// an explicitly synchronized handle. `unknown` means the compiler cannot
/// prove what the environment contains; such a value cannot cross a parallel
/// boundary.
#[derive(Debug, Clone, Default)]
struct FnCaptureInfo {
    captured: HashSet<String>,
    unsafe_mut: HashSet<String>,
    unknown: bool,
}

pub struct TypeChecker {
    types: HashMap<String, Type>,
    fns: HashMap<String, Type>,
    variants: HashMap<String, VariantCtor>,
    scopes: Vec<HashMap<String, (Type, bool)>>,
    current_ret: Type,
    /// Expected type for nested Ok/Err/Some constructors (e.g. Ok(Some(Ok(x)))).
    current_expected: Option<Type>,
    interfaces: Vec<InterfaceDef>,
    /// Names bound with `hold` that have been moved (use-after-move error).
    moved_holds: HashMap<String, bool>,
    /// Currently live `hold` bindings in scope.
    hold_vars: HashMap<String, bool>,
    /// Partial moves: hold name → set of moved field names.
    hold_moved_fields: HashMap<String, HashSet<String>>,
    /// `share let` bindings — immutable after bind (shared-borrow seed).
    share_vars: HashMap<String, bool>,
    /// Locals currently shared via `share_int(x)` — reject mut assign while live.
    shared_borrows: HashMap<String, bool>,
    /// `share let` name → source local borrowed (for NLL end-of-scope).
    share_sources: HashMap<String, String>,
    /// Scope depth when each share binding was introduced.
    share_scope_depth: HashMap<String, usize>,
    /// Function name → which params are `mut` (reject shared args into mut params).
    fn_mut_params: HashMap<String, Vec<bool>>,
    /// Comptime-folded `const` integers.
    const_ints: HashMap<String, i64>,
    /// Comptime-folded `const` strings (seed: literals, concat, used by str_len).
    const_strs: HashMap<String, String>,
    /// `const fn` definitions for compile-time evaluation.
    const_fns: HashMap<String, FnDef>,
    /// Nesting depth of `arena { }` (SAFE-007 escape checks).
    arena_depth: usize,
    /// Locals bound to arena-backed storage in the current arena nest (names).
    arena_owned: HashSet<String>,
    /// Binding name → scope depth when defined (for slice-view escape checks).
    binding_depth: HashMap<String, usize>,
    /// Nesting depth of `for` / `while` (for `break` / `continue`).
    loop_depth: usize,
    /// Innermost-first stack of loop labels (`None` = unlabeled).
    loop_labels: Vec<Option<String>>,
    /// Per nested loop: holds moved on a `continue` path (loop-carried into next
    /// iteration / post-loop). Fall-through if-join still skips diverging arms;
    /// these accumulate separately so continue-only moves are not lost.
    loop_continue_moved: Vec<(HashMap<String, bool>, HashMap<String, HashSet<String>>)>,
    /// Per nested loop: holds moved on a `break` path. If-join drops diverging
    /// arms, so break-only moves would otherwise vanish after the loop.
    loop_break_moved: Vec<(HashMap<String, bool>, HashMap<String, HashSet<String>>)>,
    /// `defer` bodies — typechecked at function exit against final NLL state.
    pending_defers: Vec<Block>,
    /// Nesting depth of `unsafe { }` (bounds-check opt-out in codegen).
    pub unsafe_depth: usize,
    /// A removed `[package] gc = true` setting was requested.
    pub gc_requested: bool,
    /// Locals captured by outstanding kicks that must not be mutated until join (race seed).
    race_outstanding: HashSet<String>,
    /// Stack of per-kick capture sets (join pops the latest; supports nested kicks).
    race_stack: Vec<HashSet<String>>,
    /// Names of `let mut` locals that are Sync-ish (Mutex/CMap/AtomicInt/ShareInt).
    race_sync_locals: HashSet<String>,
    /// Capture metadata for function-valued locals, kept in lockstep with `scopes`.
    /// A function value with an unknown environment is never allowed to cross a
    /// parallel boundary: the compiler must be able to prove its captures.
    fn_capture_scopes: Vec<HashMap<String, FnCaptureInfo>>,
    /// `visibility = "explicit"` — non-exported items stay package-private (seed).
    pub explicit_visibility: bool,
    /// Legacy profile setting; safe bounds checks are always retained.
    pub bounds_checks_always: bool,
    /// Generic function templates: `fn id[T](x: T) -> T`
    generic_fns: HashMap<String, FnDef>,
    /// Generic struct templates: `struct Pair[T] { a: T, b: T }`
    generic_structs: HashMap<String, StructDef>,
    /// Generic enum templates: `enum Tree[T] { Leaf(T), Branch(Tree[T], Tree[T]) }`
    generic_enums: HashMap<String, EnumDef>,
    /// Mono names already generated for generic enums.
    mono_enum_generated: HashSet<String>,
    /// Monomorphized enum defs (for codegen emission).
    pub mono_enums: Vec<EnumDef>,
    /// Mono names already generated for generic structs.
    mono_struct_generated: HashSet<String>,
    /// Monomorphized struct defs (for codegen emission).
    pub mono_structs: Vec<StructDef>,
    /// `#[deprecated("msg")]` functions — call sites hard-error with the message.
    deprecated_fns: HashMap<String, String>,
    /// Active type parameters while checking a generic template body.
    active_type_params: HashSet<String>,
    /// Specialized monomorphizations produced during typecheck (for codegen).
    pub mono_fns: Vec<FnDef>,
    /// mono name already generated
    mono_generated: HashSet<String>,
    /// Struct field defaults: struct name → field → default expr.
    struct_field_defaults: HashMap<String, HashMap<String, Expr>>,
    /// LSP: collect (name, type_display) for each let binding during check.
    pub lsp_bindings: Vec<(String, String)>,
    /// LSP mode — when true, collects binding types.
    pub lsp_mode: bool,
}

impl TypeChecker {
    pub fn new() -> Self {
        let mut fns = HashMap::new();
        // Builtins
        fns.insert(
            "print".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "print_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "print_int64".into(),
            Type::Fn(vec![Type::Int64], Box::new(Type::Void)),
        );
        fns.insert(
            "print_int32".into(),
            Type::Fn(vec![Type::Int32], Box::new(Type::Void)),
        );
        fns.insert(
            "print_int8".into(),
            Type::Fn(vec![Type::Int8], Box::new(Type::Void)),
        );
        fns.insert(
            "print_uint64".into(),
            Type::Fn(vec![Type::UInt64], Box::new(Type::Void)),
        );
        fns.insert(
            "print_float".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Void)),
        );
        fns.insert(
            "copy".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "len".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "cap".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "append".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "assert".into(),
            Type::Fn(vec![Type::Bool], Box::new(Type::Void)),
        );
        fns.insert(
            "assert_eq".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "assert_eq_str".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "fail".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "t_run".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "t_run_nested".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "result_unwrap_or".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "wrap_err".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::String,
                ],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        // Prefer this name in docs — richer error context seed
        fns.insert(
            "error_context".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::String,
                ],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "error_join".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                ],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        // error_tag("NotFound", "user") → Err("NotFound: user") — enum-like string tags
        fns.insert(
            "error_tag".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "errorf".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "error_is".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::String,
                ],
                Box::new(Type::Bool),
            ),
        );
        fns.insert(
            "error_string".into(),
            Type::Fn(
                vec![Type::Result(Box::new(Type::Int), Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        // Richer error chain seed (Go errors.Unwrap / errors.As style on string wraps).
        fns.insert(
            "error_unwrap".into(),
            Type::Fn(
                vec![Type::Result(Box::new(Type::Int), Box::new(Type::String))],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "error_root".into(),
            Type::Fn(
                vec![Type::Result(Box::new(Type::Int), Box::new(Type::String))],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "error_as_tag".into(),
            Type::Fn(
                vec![Type::Result(Box::new(Type::Int), Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "error_has_tag".into(),
            Type::Fn(
                vec![
                    Type::Result(Box::new(Type::Int), Box::new(Type::String)),
                    Type::String,
                ],
                Box::new(Type::Bool),
            ),
        );
        fns.insert("dbg".into(), Type::Fn(vec![Type::Int], Box::new(Type::Int)));
        fns.insert(
            "dbg_str".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "str_eq".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "str_contains".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "str_has_prefix".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "str_has_suffix".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Bool)),
        );
        // String region ops (no substring alloc)
        fns.insert(
            "str_slice_eq".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_slice_ci_eq".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_slice_contains".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_slice_index".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_slice_ci_index".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_slice_ci_starts".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_at_eq".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_byte_at".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "str_index".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "str_last_index".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "str_trim".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_trim_space".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_trim_left".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_trim_right".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_to_lower".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_to_upper".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "str_repeat".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "str_replace".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "str_split".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "str_fields".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "str_join".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "rune_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "str_builder".into(),
            Type::Fn(vec![], Box::new(Type::StrBuilder)),
        );
        fns.insert(
            "builder_write".into(),
            Type::Fn(vec![Type::StrBuilder, Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "builder_write_slice".into(),
            Type::Fn(
                vec![Type::StrBuilder, Type::String, Type::Int, Type::Int],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "builder_write_byte".into(),
            Type::Fn(vec![Type::StrBuilder, Type::Byte], Box::new(Type::Void)),
        );
        fns.insert(
            "builder_string".into(),
            Type::Fn(vec![Type::StrBuilder], Box::new(Type::String)),
        );
        fns.insert(
            "builder_len".into(),
            Type::Fn(vec![Type::StrBuilder], Box::new(Type::Int)),
        );
        fns.insert("uuid_v4".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert("uuid_v7".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert(
            "uuid_v5".into(),
            Type::Fn(vec![Type::Uuid, Type::String], Box::new(Type::Uuid)),
        );
        fns.insert("uuid_nil".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert("uuid_ns_dns".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert("uuid_ns_url".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert("uuid_ns_oid".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert(
            "uuid_ns_x500".into(),
            Type::Fn(vec![], Box::new(Type::Uuid)),
        );
        fns.insert(
            "uuid_string".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::String)),
        );
        fns.insert(
            "uuid_string_upper".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::String)),
        );
        fns.insert(
            "uuid_urn".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::String)),
        );
        fns.insert(
            "uuid_bytes".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::String)),
        );
        fns.insert(
            "uuid_from_bytes".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Uuid)),
        );
        fns.insert(
            "uuid_parse".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Uuid)),
        );
        fns.insert(
            "uuid_parse_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "uuid_eq".into(),
            Type::Fn(vec![Type::Uuid, Type::Uuid], Box::new(Type::Bool)),
        );
        fns.insert(
            "uuid_cmp".into(),
            Type::Fn(vec![Type::Uuid, Type::Uuid], Box::new(Type::Int)),
        );
        fns.insert(
            "uuid_is_nil".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::Bool)),
        );
        fns.insert(
            "uuid_version".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::Int)),
        );
        fns.insert(
            "uuid_variant".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::Int)),
        );
        fns.insert(
            "uuid_check".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        // ULID — same 16-byte POD, Crockford Base32 string form, time-sortable.
        fns.insert("ulid_new".into(), Type::Fn(vec![], Box::new(Type::Uuid)));
        fns.insert(
            "ulid_string".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::String)),
        );
        fns.insert(
            "ulid_parse".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Uuid)),
        );
        fns.insert(
            "ulid_parse_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "ulid_timestamp_ms".into(),
            Type::Fn(vec![Type::Uuid], Box::new(Type::Int)),
        );
        fns.insert(
            "int_to_string".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "arena_text".into(),
            Type::Fn(vec![Type::Arena, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "arena_ints".into(),
            Type::Fn(
                vec![Type::Arena, Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "arena_stamp".into(),
            Type::Fn(vec![Type::Arena, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_serve".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http_echo".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "chan_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Chan(Box::new(Type::Int)))),
        );
        fns.insert(
            "chan_try_send".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        /* string channel take-send: move ownership (no clone). */
        fns.insert(
            "chan_str_send_take".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::String)), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "chan_str_try_send_take".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::String)), Type::String],
                Box::new(Type::Int),
            ),
        );
        // chan_len and chan_cap accept any chan[T] — handled in check_expr
        // as special-cased builtins rather than fixed signatures.
        fns.insert(
            "sleep_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "time_sleep_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "elapsed_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "exit".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert("now_ms".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("now_ns".into(), Type::Fn(vec![], Box::new(Type::Int)));
        // Low-latency clocks: wall (REALTIME) vs mono (MONOTONIC[_RAW])
        fns.insert("wall_ns".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("wall_us".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("wall_ms".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("mono_ns".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("mono_us".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("mono_ms".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("mono_res_ns".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "mono_overhead_ns".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "elapsed_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "elapsed_us".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "elapsed_mono_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "deadline_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "deadline_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "deadline_remaining_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "deadline_remaining_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "deadline_expired".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        // Timed channel ops (int ring): 1 ok, 0 timeout, -1 closed
        fns.insert(
            "chan_send_timeout".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::Int)), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "chan_recv_timeout".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "sleep_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "sleep_us".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "spin_until_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "sleep_until_ns".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "black_box".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "runtime_stats_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "runtime_stats_reset".into(),
            Type::Fn(vec![], Box::new(Type::Void)),
        );
        // RT-002/003: bounded scheduler (opt-in worker pool behind kick).
        fns.insert(
            "sched_set_workers".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "sched_workers".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        // SAFE-005: string_view surface — zero-copy view; own via str_to_owned.
        fns.insert(
            "str_as_view".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("string_view".into())),
            ),
        );
        fns.insert(
            "str_to_owned".into(),
            Type::Fn(
                vec![Type::Named("string_view".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "path_join".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "path_clean".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "path_base".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "path_dir".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "path_ext".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "path_is_abs".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert("getcwd".into(), Type::Fn(vec![], Box::new(Type::String)));
        fns.insert(
            "chdir".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "read_dir".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "is_dir".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "format_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_float".into(),
            Type::Fn(vec![Type::Float, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_bool".into(),
            Type::Fn(vec![Type::Bool], Box::new(Type::String)),
        );
        fns.insert(
            "parse_bool".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "fmt_sprintf".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_sprintf_d".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_sprintf_dd".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "format_int_hex".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_hex_upper".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_bin".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_oct".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_dec".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_base".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_hex_pad".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_int_hex_prefix".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "format_pad".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "parse_int_base".into(),
            Type::Fn(
                vec![Type::String, Type::Int],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "parse_int_hex".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "parse_int_bin".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "parse_int_oct".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "parse_int_auto".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        /* Expanded fmt / print (Go-style) */
        fns.insert(
            "fmt_sprintf2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fmt_sprintf3".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fmt_sprintf4".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fmt_sprintf_f".into(),
            Type::Fn(
                vec![Type::String, Type::Float, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fmt_sprint".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_sprint2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_sprint3".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fmt_sprintln".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_sprintln2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_print".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_print2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_println".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_println2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_printf".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_printf2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "fmt_printf3".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "fmt_eprint".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_eprintln".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_eprintf".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fmt_errorf".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "fmt_errorf2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "print_raw".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "eprint".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "eprintln".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hex_encode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert("abs".into(), Type::Fn(vec![Type::Int], Box::new(Type::Int)));
        fns.insert(
            "min".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "max".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "clamp".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "math_abs".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_sqrt".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_pow".into(),
            Type::Fn(vec![Type::Float, Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_floor".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_ceil".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_sin".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_cos".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_log".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "math_exp".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "ints_contains".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Bool),
            ),
        );
        fns.insert(
            "strings_contains".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::String],
                Box::new(Type::Bool),
            ),
        );
        fns.insert(
            "ints_copy".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "ints_index".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "time_format".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert("time_unix".into(), Type::Fn(vec![], Box::new(Type::Int)));
        // Full time: calendar, parse, duration
        for name in [
            "time_year",
            "time_month",
            "time_day",
            "time_hour",
            "time_minute",
            "time_second",
            "time_millisecond",
            "time_weekday",
            "time_yearday",
            "time_trunc_day",
            "time_trunc_hour",
            "time_since_ms",
            "time_until_ms",
        ] {
            fns.insert(name.into(), Type::Fn(vec![Type::Int], Box::new(Type::Int)));
        }
        fns.insert(
            "time_date".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "time_add_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "time_sub_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "time_after".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "time_before".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "time_equal".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "time_local_offset_sec".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "time_format_local".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "time_format_date".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "time_format_clock".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "time_parse_rfc3339".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "time_parse_date".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        for name in [
            "duration_ms",
            "duration_us_as_ms",
            "duration_seconds",
            "duration_minutes",
            "duration_hours",
            "duration_days",
            "duration_to_seconds",
            "duration_to_minutes",
            "duration_to_hours",
        ] {
            fns.insert(name.into(), Type::Fn(vec![Type::Int], Box::new(Type::Int)));
        }
        fns.insert(
            "duration_string".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        // Full syscall
        fns.insert(
            "syscall_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        for name in [
            "syscall_getpid",
            "syscall_getppid",
            "syscall_getuid",
            "syscall_geteuid",
            "syscall_getgid",
            "syscall_getegid",
            "syscall_errno",
            "syscall_pagesize",
            "syscall_ncpu",
            "syscall_getrlimit_nofile",
            "syscall_pipe",
            "syscall_pipe_read_fd",
            "syscall_pipe_write_fd",
        ] {
            fns.insert(name.into(), Type::Fn(vec![], Box::new(Type::Int)));
        }
        fns.insert(
            "syscall_hostname".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_uname_sysname".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_uname_release".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_uname_machine".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_uname_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_errno_str".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_kill".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_umask".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_isatty".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_dup".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_dup2".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_read".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_access".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_chmod".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_symlink".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "syscall_readlink".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "syscall_setrlimit_nofile".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert("mutex_new".into(), Type::Fn(vec![], Box::new(Type::Mutex)));
        fns.insert(
            "mutex_lock".into(),
            Type::Fn(vec![Type::Mutex], Box::new(Type::Void)),
        );
        fns.insert(
            "mutex_unlock".into(),
            Type::Fn(vec![Type::Mutex], Box::new(Type::Void)),
        );
        fns.insert(
            "rwmutex_new".into(),
            Type::Fn(vec![], Box::new(Type::RWMutex)),
        );
        fns.insert(
            "rwmutex_rlock".into(),
            Type::Fn(vec![Type::RWMutex], Box::new(Type::Void)),
        );
        fns.insert(
            "rwmutex_runlock".into(),
            Type::Fn(vec![Type::RWMutex], Box::new(Type::Void)),
        );
        fns.insert(
            "rwmutex_lock".into(),
            Type::Fn(vec![Type::RWMutex], Box::new(Type::Void)),
        );
        fns.insert(
            "rwmutex_unlock".into(),
            Type::Fn(vec![Type::RWMutex], Box::new(Type::Void)),
        );
        // Concurrent hashmap
        fns.insert("cmap_new".into(), Type::Fn(vec![], Box::new(Type::CMap)));
        fns.insert(
            "cmap_set".into(),
            Type::Fn(
                vec![Type::CMap, Type::String, Type::String],
                Box::new(Type::Void),
            ),
        );
        /* map set_take: move key (and val for ss) into map — no clone (hot path). */
        fns.insert(
            "map_si_set_take".into(),
            Type::Fn(
                vec![
                    Type::Map(Box::new(Type::String), Box::new(Type::Int)),
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "map_ss_set_take".into(),
            Type::Fn(
                vec![
                    Type::Map(Box::new(Type::String), Box::new(Type::String)),
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "cmap_get".into(),
            Type::Fn(vec![Type::CMap, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cmap_has".into(),
            Type::Fn(vec![Type::CMap, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cmap_del".into(),
            Type::Fn(vec![Type::CMap, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cmap_len".into(),
            Type::Fn(vec![Type::CMap], Box::new(Type::Int)),
        );
        fns.insert(
            "cmap_incr".into(),
            Type::Fn(
                vec![Type::CMap, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        // Direct I/O (mako_dio.h)
        fns.insert(
            "file_open".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "file_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "pread".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "pwrite".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "file_append".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "file_append2".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "file_append3".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "fsync".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fdatasync".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fallocate".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "file_size".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "path_file_size".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "file_truncate".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "file_seek".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "file_read_exact".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        // mmap
        fns.insert(
            "mmap_open".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::MMap)),
        );
        fns.insert(
            "mmap_create".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::MMap)),
        );
        fns.insert(
            "mmap_read".into(),
            Type::Fn(
                vec![Type::MMap, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "mmap_write".into(),
            Type::Fn(
                vec![Type::MMap, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mmap_sync".into(),
            Type::Fn(vec![Type::MMap, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "mmap_size".into(),
            Type::Fn(vec![Type::MMap], Box::new(Type::Int)),
        );
        fns.insert(
            "mmap_close".into(),
            Type::Fn(vec![Type::MMap], Box::new(Type::Int)),
        );
        // Storage primitives seed (page + WAL).
        fns.insert(
            "page_alloc".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Page".into()))),
        );
        fns.insert(
            "page_size".into(),
            Type::Fn(vec![Type::Named("Page".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "page_write".into(),
            Type::Fn(
                vec![Type::Named("Page".into()), Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "page_read".into(),
            Type::Fn(
                vec![Type::Named("Page".into()), Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "page_free".into(),
            Type::Fn(vec![Type::Named("Page".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "wal_open".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Named("Wal".into()))),
        );
        fns.insert(
            "wal_append".into(),
            Type::Fn(
                vec![Type::Named("Wal".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "wal_sync".into(),
            Type::Fn(vec![Type::Named("Wal".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "wal_size".into(),
            Type::Fn(vec![Type::Named("Wal".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "wal_read_at".into(),
            Type::Fn(
                vec![Type::Named("Wal".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert("wal_next_off".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "wal_close".into(),
            Type::Fn(vec![Type::Named("Wal".into())], Box::new(Type::Int)),
        );
        // Hash index + transactional store
        fns.insert(
            "hindex_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("HIndex".into()))),
        );
        fns.insert(
            "hindex_put".into(),
            Type::Fn(
                vec![Type::Named("HIndex".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "hindex_get".into(),
            Type::Fn(
                vec![Type::Named("HIndex".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "hindex_del".into(),
            Type::Fn(
                vec![Type::Named("HIndex".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "hindex_len".into(),
            Type::Fn(vec![Type::Named("HIndex".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "hindex_free".into(),
            Type::Fn(vec![Type::Named("HIndex".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "store_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Store".into()))),
        );
        fns.insert(
            "store_attach_wal".into(),
            Type::Fn(
                vec![Type::Named("Store".into()), Type::Named("Wal".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "store_get".into(),
            Type::Fn(
                vec![Type::Named("Store".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "store_put".into(),
            Type::Fn(
                vec![Type::Named("Store".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "store_del".into(),
            Type::Fn(
                vec![Type::Named("Store".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "store_begin".into(),
            Type::Fn(vec![Type::Named("Store".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "store_commit".into(),
            Type::Fn(vec![Type::Named("Store".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "store_rollback".into(),
            Type::Fn(vec![Type::Named("Store".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "store_len".into(),
            Type::Fn(vec![Type::Named("Store".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "store_free".into(),
            Type::Fn(vec![Type::Named("Store".into())], Box::new(Type::Int)),
        );
        // Game multiplayer snapshot seed
        fns.insert(
            "snap_encode2".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "snap_encode4".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "snap_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "snap_get".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "snap_predict".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "snap_reconcile".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "snap_diff".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "snap_apply_delta".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "netcode_lag_comp_tick".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "netcode_interp".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );

        fns.insert(
            "btree_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("BTree".into()))),
        );
        fns.insert(
            "btree_put".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_get".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_len".into(),
            Type::Fn(vec![Type::Named("BTree".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "btree_free".into(),
            Type::Fn(vec![Type::Named("BTree".into())], Box::new(Type::Int)),
        );

        fns.insert(
            "btree_save".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_load".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Named("BTree".into()))),
        );
        fns.insert(
            "sst_build4".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Named("Sst".into())),
            ),
        );
        fns.insert(
            "sst_get".into(),
            Type::Fn(
                vec![Type::Named("Sst".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sst_len".into(),
            Type::Fn(vec![Type::Named("Sst".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "sst_free".into(),
            Type::Fn(vec![Type::Named("Sst".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pcache_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("PageCache".into()))),
        );
        fns.insert(
            "pcache_get".into(),
            Type::Fn(
                vec![Type::Named("PageCache".into()), Type::Int],
                Box::new(Type::Named("Page".into())),
            ),
        );
        fns.insert(
            "pcache_hits".into(),
            Type::Fn(vec![Type::Named("PageCache".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pcache_misses".into(),
            Type::Fn(vec![Type::Named("PageCache".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pcache_free".into(),
            Type::Fn(vec![Type::Named("PageCache".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "mvcc_gc".into(),
            Type::Fn(
                vec![Type::Named("Mvcc".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mvcc_live".into(),
            Type::Fn(vec![Type::Named("Mvcc".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "simd_dot_i64_4".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "simd_sum_i64_4".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );

        fns.insert(
            "lsm_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Lsm".into()))),
        );
        fns.insert(
            "lsm_attach_run".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::Named("Wal".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_put".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_get".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_flush".into(),
            Type::Fn(vec![Type::Named("Lsm".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "lsm_flushes".into(),
            Type::Fn(vec![Type::Named("Lsm".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "lsm_compact".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_compact_down".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_sst_levels".into(),
            Type::Fn(vec![Type::Named("Lsm".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "lsm_level_len".into(),
            Type::Fn(
                vec![Type::Named("Lsm".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "lsm_compactions".into(),
            Type::Fn(vec![Type::Named("Lsm".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "lsm_free".into(),
            Type::Fn(vec![Type::Named("Lsm".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pbtree_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("PageBTree".into()))),
        );
        fns.insert(
            "pbtree_put".into(),
            Type::Fn(
                vec![Type::Named("PageBTree".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pbtree_get".into(),
            Type::Fn(
                vec![Type::Named("PageBTree".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pbtree_len".into(),
            Type::Fn(vec![Type::Named("PageBTree".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pbtree_pages".into(),
            Type::Fn(vec![Type::Named("PageBTree".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pbtree_free".into(),
            Type::Fn(vec![Type::Named("PageBTree".into())], Box::new(Type::Int)),
        );
        // Storage polish seeds: bloom · ordered range · disk page manager
        fns.insert(
            "bloom_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("Bloom".into()))),
        );
        fns.insert(
            "bloom_add".into(),
            Type::Fn(
                vec![Type::Named("Bloom".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "bloom_maybe".into(),
            Type::Fn(
                vec![Type::Named("Bloom".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "bloom_len".into(),
            Type::Fn(vec![Type::Named("Bloom".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "bloom_clear".into(),
            Type::Fn(vec![Type::Named("Bloom".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "bloom_free".into(),
            Type::Fn(vec![Type::Named("Bloom".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "bloom_add_str".into(),
            Type::Fn(
                vec![Type::Named("Bloom".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "bloom_maybe_str".into(),
            Type::Fn(
                vec![Type::Named("Bloom".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "str_hash64".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "btree_range".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_get_all".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_put_str".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_get_str".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "btree_range_str".into(),
            Type::Fn(
                vec![Type::Named("BTree".into()), Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sst_range".into(),
            Type::Fn(
                vec![Type::Named("Sst".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sst_build8".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Named("Sst".into())),
            ),
        );
        fns.insert(
            "sst_build_n".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Named("Sst".into())),
            ),
        );
        fns.insert("range_len".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("range_cap".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "range_key_at".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "range_val_at".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert("range_rewind".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("range_next".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("range_key".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("range_val".into(), Type::Fn(vec![], Box::new(Type::Int)));
        // Multi-value ordered map (duplicate keys)
        fns.insert(
            "multimap_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("MultiMap".into()))),
        );
        fns.insert(
            "multimap_put".into(),
            Type::Fn(
                vec![Type::Named("MultiMap".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "multimap_get".into(),
            Type::Fn(
                vec![Type::Named("MultiMap".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "multimap_get_all".into(),
            Type::Fn(
                vec![Type::Named("MultiMap".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "multimap_range".into(),
            Type::Fn(
                vec![Type::Named("MultiMap".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "multimap_len".into(),
            Type::Fn(vec![Type::Named("MultiMap".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "multimap_free".into(),
            Type::Fn(vec![Type::Named("MultiMap".into())], Box::new(Type::Int)),
        );
        // Domain handle registry (int slots)
        fns.insert(
            "domain_reg_put_bloom".into(),
            Type::Fn(vec![Type::Named("Bloom".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "domain_reg_get_bloom".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Bloom".into()))),
        );
        fns.insert(
            "domain_reg_put_btree".into(),
            Type::Fn(vec![Type::Named("BTree".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "domain_reg_get_btree".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("BTree".into()))),
        );
        fns.insert(
            "domain_reg_put_pman".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "domain_reg_get_pman".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("PageMan".into()))),
        );
        fns.insert(
            "domain_reg_del".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_open".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Named("PageMan".into()))),
        );
        fns.insert(
            "pman_alloc".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "predict_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Predict".into()))),
        );
        fns.insert(
            "predict_tick".into(),
            Type::Fn(vec![Type::Named("Predict".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "predict_state".into(),
            Type::Fn(vec![Type::Named("Predict".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "predict_input".into(),
            Type::Fn(
                vec![Type::Named("Predict".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "predict_reconcile".into(),
            Type::Fn(
                vec![Type::Named("Predict".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "predict_free".into(),
            Type::Fn(vec![Type::Named("Predict".into())], Box::new(Type::Int)),
        );

        fns.insert(
            "pman_set".into(),
            Type::Fn(
                vec![
                    Type::Named("PageMan".into()),
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pman_get".into(),
            Type::Fn(
                vec![Type::Named("PageMan".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pman_sync".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_pages".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_reads".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_writes".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_close".into(),
            Type::Fn(vec![Type::Named("PageMan".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pman_write_page".into(),
            Type::Fn(
                vec![Type::Named("PageMan".into()), Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pman_read_page".into(),
            Type::Fn(
                vec![Type::Named("PageMan".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "store_recover_wal".into(),
            Type::Fn(
                vec![Type::Named("Store".into()), Type::Named("Wal".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "file_mtime_ns".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_watch".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_changed".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_unwatch".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_watch_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_note_swap".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_swap_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_stamp".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_status_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );

        fns.insert(
            "mvcc_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("Mvcc".into()))),
        );
        fns.insert(
            "mvcc_begin".into(),
            Type::Fn(vec![Type::Named("Mvcc".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "mvcc_put".into(),
            Type::Fn(
                vec![Type::Named("Mvcc".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mvcc_get".into(),
            Type::Fn(
                vec![Type::Named("Mvcc".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mvcc_versions".into(),
            Type::Fn(vec![Type::Named("Mvcc".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "mvcc_free".into(),
            Type::Fn(vec![Type::Named("Mvcc".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "rollback_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("Rollback".into()))),
        );
        fns.insert(
            "rollback_push".into(),
            Type::Fn(
                vec![Type::Named("Rollback".into()), Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "rollback_get".into(),
            Type::Fn(
                vec![Type::Named("Rollback".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "rollback_restore_slot0".into(),
            Type::Fn(
                vec![Type::Named("Rollback".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "rollback_len".into(),
            Type::Fn(vec![Type::Named("Rollback".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "rollback_free".into(),
            Type::Fn(vec![Type::Named("Rollback".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_window_open".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Named("GfxWindow".into())),
            ),
        );
        fns.insert(
            "gfx_window_width".into(),
            Type::Fn(vec![Type::Named("GfxWindow".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_window_height".into(),
            Type::Fn(vec![Type::Named("GfxWindow".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_window_close".into(),
            Type::Fn(vec![Type::Named("GfxWindow".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_poll".into(),
            Type::Fn(vec![Type::Named("GfxWindow".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_backend_name".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );

        fns.insert(
            "gfx_shader_compile".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_asset_size".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "audio_mix".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "physics_step_x".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "physics_step_v".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ai_rope_cos".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ai_rope_sin".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ai_rope_apply_x".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ai_rope_apply_y".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "kv_cache_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("KvCache".into()))),
        );
        fns.insert(
            "kv_cache_append".into(),
            Type::Fn(
                vec![Type::Named("KvCache".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "kv_cache_get_k".into(),
            Type::Fn(
                vec![Type::Named("KvCache".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "kv_cache_get_v".into(),
            Type::Fn(
                vec![Type::Named("KvCache".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "kv_cache_len".into(),
            Type::Fn(vec![Type::Named("KvCache".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "kv_cache_free".into(),
            Type::Fn(vec![Type::Named("KvCache".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gemm2x2".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert("gemm_c01".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("gemm_c10".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("gemm_c11".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "f32_to_f16_bits".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_set_loc".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_file".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert("debug_line".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "debug_frame_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );

        // Event loop
        fns.insert(
            "evloop_new".into(),
            Type::Fn(vec![], Box::new(Type::EvLoop)),
        );
        fns.insert(
            "evloop_add".into(),
            Type::Fn(
                vec![Type::EvLoop, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "evloop_mod".into(),
            Type::Fn(
                vec![Type::EvLoop, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "evloop_del".into(),
            Type::Fn(vec![Type::EvLoop, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "evloop_wait".into(),
            Type::Fn(vec![Type::EvLoop, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "evloop_event_fd".into(),
            Type::Fn(vec![Type::EvLoop, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "evloop_event_flags".into(),
            Type::Fn(vec![Type::EvLoop, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "evloop_close".into(),
            Type::Fn(vec![Type::EvLoop], Box::new(Type::Int)),
        );
        fns.insert(
            "evloop_shutdown".into(),
            Type::Fn(vec![Type::EvLoop], Box::new(Type::Int)),
        );
        fns.insert(
            "crew_drain".into(),
            Type::Fn(vec![Type::Crew, Type::Int], Box::new(Type::Int)),
        );
        // Non-blocking I/O helpers
        fns.insert(
            "nb_listen".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "nb_accept".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "nb_read".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gfx_window_pixels".into(),
            Type::Fn(vec![Type::Named("GfxWindow".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "gfx_window_fill".into(),
            Type::Fn(
                vec![Type::Named("GfxWindow".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gfx_window_set_pixel".into(),
            Type::Fn(
                vec![
                    Type::Named("GfxWindow".into()),
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gfx_window_get_pixel".into(),
            Type::Fn(
                vec![Type::Named("GfxWindow".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );

        fns.insert(
            "hot_reload_plugin_watch".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_plugin_poll".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_plugin_call".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hot_reload_plugin_handle".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_plugin_swaps".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hot_reload_plugin_close".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );

        fns.insert(
            "nb_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "nb_udp_bind".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "nb_udp_recv".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "nb_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        // Binary buffer
        fns.insert(
            "buf_pack_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Buf)),
        );
        fns.insert(
            "buf_from_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Buf)),
        );
        fns.insert(
            "buf_to_string".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::String)),
        );
        fns.insert(
            "buf_len".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_pos".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_reset".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_seek".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_free".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u8".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u16".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u32".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u64".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_i32".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_f32".into(),
            Type::Fn(vec![Type::Buf, Type::Float], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_f64".into(),
            Type::Fn(vec![Type::Buf, Type::Float], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_bytes".into(),
            Type::Fn(vec![Type::Buf, Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_str".into(),
            Type::Fn(vec![Type::Buf, Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u16be".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_write_u32be".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "buf_read_u8".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_u16".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_u32".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_u64".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_i32".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_f32".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Float)),
        );
        fns.insert(
            "buf_read_f64".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Float)),
        );
        fns.insert(
            "buf_read_bytes".into(),
            Type::Fn(vec![Type::Buf, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "buf_read_str".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::String)),
        );
        fns.insert(
            "buf_read_u16be".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_read_u32be".into(),
            Type::Fn(vec![Type::Buf], Box::new(Type::Int)),
        );
        // Game UDP
        fns.insert(
            "game_udp_bind".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::GameUDP)),
        );
        fns.insert(
            "game_udp_bind_addr".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::GameUDP)),
        );
        fns.insert(
            "game_udp_recv".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::String)),
        );
        fns.insert(
            "game_udp_sender".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::Int)),
        );
        fns.insert(
            "game_udp_sender_addr".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::String)),
        );
        fns.insert(
            "game_udp_send".into(),
            Type::Fn(
                vec![Type::GameUDP, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "game_udp_send_to".into(),
            Type::Fn(
                vec![Type::GameUDP, Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "game_udp_broadcast".into(),
            Type::Fn(vec![Type::GameUDP, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "game_udp_kick".into(),
            Type::Fn(vec![Type::GameUDP, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "game_udp_peers".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::Int)),
        );
        fns.insert(
            "game_udp_fd".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::Int)),
        );
        fns.insert(
            "game_udp_close".into(),
            Type::Fn(vec![Type::GameUDP], Box::new(Type::Void)),
        );
        // Tick timer
        fns.insert("tick_now_us".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "tick_sleep_us".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        // Cloud / distributed
        fns.insert(
            "chash_new".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::CHash)),
        );
        fns.insert(
            "chash_get".into(),
            Type::Fn(vec![Type::CHash, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "chash_add_node".into(),
            Type::Fn(vec![Type::CHash], Box::new(Type::Int)),
        );
        fns.insert(
            "chash_remove_node".into(),
            Type::Fn(vec![Type::CHash, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "chash_node_count".into(),
            Type::Fn(vec![Type::CHash], Box::new(Type::Int)),
        );
        fns.insert(
            "chash_free".into(),
            Type::Fn(vec![Type::CHash], Box::new(Type::Void)),
        );
        fns.insert(
            "ratelimit_new".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::RateLimiter)),
        );
        fns.insert(
            "ratelimit_allow".into(),
            Type::Fn(vec![Type::RateLimiter], Box::new(Type::Int)),
        );
        fns.insert(
            "ratelimit_remaining".into(),
            Type::Fn(vec![Type::RateLimiter], Box::new(Type::Int)),
        );
        fns.insert(
            "ratelimit_free".into(),
            Type::Fn(vec![Type::RateLimiter], Box::new(Type::Void)),
        );
        fns.insert(
            "breaker_new".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::CircuitBreaker),
            ),
        );
        fns.insert(
            "breaker_allow".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Int)),
        );
        fns.insert(
            "breaker_success".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Void)),
        );
        fns.insert(
            "breaker_failure".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Void)),
        );
        fns.insert(
            "breaker_state".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Int)),
        );
        fns.insert(
            "breaker_reset".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Void)),
        );
        fns.insert(
            "breaker_free".into(),
            Type::Fn(vec![Type::CircuitBreaker], Box::new(Type::Void)),
        );
        fns.insert(
            "jwt_sign".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "jwt_verify".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jwt_verify_rs256".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jwt_verify_jwks".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jwt_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "backoff_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "env_get_or".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "env_has".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        // HTTP Engine
        fns.insert(
            "httpengine_new".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::HttpEngine)),
        );
        fns.insert(
            "httpengine_route".into(),
            Type::Fn(
                vec![
                    Type::HttpEngine,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "httpengine_serve".into(),
            Type::Fn(vec![Type::HttpEngine], Box::new(Type::Int)),
        );
        // Math
        fns.insert(
            "sqrt".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "sin".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "cos".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "atan2".into(),
            Type::Fn(vec![Type::Float, Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "floor_f".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "ceil_f".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "abs_f".into(),
            Type::Fn(vec![Type::Float], Box::new(Type::Float)),
        );
        fns.insert(
            "dist2d".into(),
            Type::Fn(
                vec![Type::Float, Type::Float, Type::Float, Type::Float],
                Box::new(Type::Float),
            ),
        );
        fns.insert(
            "lerp".into(),
            Type::Fn(
                vec![Type::Float, Type::Float, Type::Float],
                Box::new(Type::Float),
            ),
        );
        fns.insert(
            "clamp_f".into(),
            Type::Fn(
                vec![Type::Float, Type::Float, Type::Float],
                Box::new(Type::Float),
            ),
        );
        fns.insert(
            "random_bytes".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "random_int".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "log_debug".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "log_kv".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "read_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "write_file".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "append_file".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "env_get".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "env_set".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mkdir".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "file_exists".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "remove_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        // Filesystem / storage
        fns.insert(
            "rename".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mkdir_all".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rmdir".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "remove_all".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "copy_file".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "is_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "path_size".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "file_mtime".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "chmod".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "atomic_write_file".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert("temp_dir".into(), Type::Fn(vec![], Box::new(Type::String)));
        fns.insert(
            "temp_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "symlink".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "readlink".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "realpath".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "parse_int".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Result(Box::new(Type::Int), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "parse_float".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Float)),
        );
        fns.insert(
            "base64_encode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "base64_decode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sort_ints".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "sort_strings".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "http_get".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http_post".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http_request".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "https_request".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "https_get".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "https_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "https_last_status".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "https_last_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "oidc_discovery".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "oidc_token".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        // LLM programming (OpenAI-compatible / xAI)
        fns.insert(
            "llm_message".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_messages_append".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_chat_body".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_system_user".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_body_with_tools".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_estimate_tokens".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_retry_delay_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_redact_key".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_content".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_finish_reason".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_usage_prompt_tokens".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_usage_completion_tokens".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_usage_total_tokens".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_tool_call_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_tool_call_name".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "llm_tool_call_args".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "llm_sse_data".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_sse_delta".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_stream_append".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_json_extract".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_api_key".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "llm_base_url".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "llm_default_model".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "llm_https_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_chat".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_ask".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_https_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_last_status".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_is_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_error_message".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_should_retry".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_chat_stream".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_chat_retry".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_body_force_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_embed_body".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "llm_embeddings".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "llm_embed".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "llm_embedding_dim".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "llm_embedding_json".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        // Low-level SIP / SDP / RTP (RFC 3261 / 4566 / 3550)
        fns.insert(
            "sip_is_request".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_is_response".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_method".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_request_uri".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_status_code".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_reason".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_header".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_header_n".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_header_count".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        // Zero-copy hot path (no malloc; TLS view or one-shot compares)
        fns.insert(
            "sip_header_view".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_header_view_n".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sip_body_view".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_method_view".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert("sip_view_len".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "sip_view_offset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_view_eq".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_view_ci_eq".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_view_contains".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_view_copy".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "sip_header_eq".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sip_header_ci_eq".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sip_header_contains".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sip_method_eq".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_body".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_content_length".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_msg_complete".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_msg_needed".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_header_line".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_headers_append".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_request".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_response".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_value".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_value_nat".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_value_rport".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_add_received".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_fix_source".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_via_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_port".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_via_has_rport".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_via_rport".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_via_received".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_maddr".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_transport".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_response_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_response_port".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_via_response_addr".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_msg_fix_top_via".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_msg_response_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_msg_response_port".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_record_route".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_prepend_header".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_insert_via".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_strip_via".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_first_message_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_from_value".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_to_value".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_contact_value".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_cseq_value".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_addr_tag".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_via_branch".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_branch".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert("sip_tag".into(), Type::Fn(vec![], Box::new(Type::String)));
        fns.insert(
            "sip_call_id_new".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert("sip_cseq_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "sip_dialog_id".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_txn_key".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_uri_scheme".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_uri_user".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_uri_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_uri_port".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_uri_build".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_udp_bind".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_udp_send".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sip_udp_recv".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sip_tcp_send".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sip_md5_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_digest_response".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_digest_response_ha1".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_www_authenticate".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_proxy_authenticate".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_authorization_digest".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_auth_param".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_copy_headers_for_response".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_reply".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_ensure_to_tag".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sip_reply_with_to_tag".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sip_method_is".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sdp_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sdp_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_origin".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_origin_addr".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_session_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_timing".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_connection".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_connection_addr".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_connection_is_ip6".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sdp_media_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sdp_media".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_type".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_port".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sdp_media_proto".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_formats".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_connection".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_connection_addr".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_attr".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_attr".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_direction".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_media_direction".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_set_media_direction".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_replace_connection_addr".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_replace_media_port".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_append_line".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_build_audio".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_build_av".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_attr_rtpmap".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sdp_attr_fmtp".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sdp_attr_candidate".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "rtp_header_len".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_parse_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_marker".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_payload_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_seq".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_timestamp".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_ssrc".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "rtp_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "rtp_pack".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_get_timeout".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_post_timeout".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_last_status".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_last_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "udp_bind".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "udp_bind_addr".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "udp_send_to".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "udp_recv".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "udp_recv_from".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "udp_last_sender_host".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "udp_last_sender_port".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "udp_last_sender".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "udp_local_port".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "udp_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "unix_socket_pair".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "unix_socket_pair_peer".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "unix_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "unix_read".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "unix_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_detect".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_settings_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_empty_settings".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "http2_settings_max_concurrent".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_settings_ack".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "http2_client_preface".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "http2_server_preface".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "http2_is_settings_ack".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_conn_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        // Per-connection HTTP/2 state handles (for servers/proxies juggling many
        // connections on one thread).
        fns.insert(
            "http2_conn_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("Http2Conn".into()))),
        );
        fns.insert(
            "http2_conn_use".into(),
            Type::Fn(vec![Type::Named("Http2Conn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_free".into(),
            Type::Fn(vec![Type::Named("Http2Conn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_recv".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_send_settings".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_send_settings_ack".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_settings_ack_needed".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_auto_settings_ack".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "http2_conn_pump".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http2_conn_goaway_last".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_max_concurrent".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_active_streams".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_set_server".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_is_server".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_preface_received".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_settings_exchanged".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_closing".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_header_block".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_conn_header_stream".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_header_assembling".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_send_goaway".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_goaway".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_conn_initial_window".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_max_frame_size".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_header_table_size".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_enable_push".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_max_header_list".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_conn_unacked".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_of".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_recv_window_of".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_recv_window_conn".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_body_overflow".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_conn".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_blocked".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_consume".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_increment".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_frame_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_frame_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_frame_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_frame_flags".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_encode_indexed".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_decode_indexed".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_static_name".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_static_value".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_encode_literal".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_literal_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_literal_value".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_decode_block".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_decoded_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_decoded_name".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_decoded_value".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_decode_clear".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_headers_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http2_data_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        // Build a complete response (HEADERS :status + content-length, then DATA
        // with END_STREAM) for a stream — the "write response" half of a server.
        fns.insert(
            "http2_response".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http2_response_ct".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        // Stream multiplexing: concurrent ready streams + bodies.
        fns.insert(
            "http2_ready_streams".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_next_ready_stream".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_take".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_body".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_stream_body_len".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_body_done".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_frame_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http2_continuation_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http2_goaway_frame".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_ping_frame".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_window_update_frame".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_is_goaway".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_is_ping".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_is_window_update".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_goaway_last_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_goaway_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_window_update_increment".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_rst_stream_frame".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_priority_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http2_is_rst_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_is_priority".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_rst_stream_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_priority_dep".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_priority_weight".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_priority_exclusive".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_priority_apply".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_priority_dep".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_priority_weight".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_priority_exclusive".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_priority_child_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_schedule_next".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_push_promise_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http2_is_push_promise".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "http2_push_promise_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_concat_frames".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http2_header_block".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http2_frame_at".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_dyn_insert".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_dyn_name".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_dyn_value".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_dyn_clear".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_dyn_len".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "hpack_dyn_name_at".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_dyn_value_at".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_huffman_encode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hpack_huffman_decode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http2_stream_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_id".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_state".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_state_of".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_apply".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_apply_local".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_half_closed_remote".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http2_stream_half_closed_local".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_detect".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_long_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_short_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_spin_bit".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_key_phase".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_long_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_is_retry".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_is_version_negotiation".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_vn_version_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_vn_version_at".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_vn_select".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_has_crypto".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "quic_crypto_offset".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_crypto_data_offset".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_crypto_data_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_crypto_data".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_crypto_frame".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "quic_crypto_payload".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_payload_crypto_data".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_payload_crypto_data_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_ack_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_ack_largest".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_ack_delay".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_ack_range_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_ack_first_range".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_ack_smallest".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_is_ack".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_frame".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_is_stream".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_fin".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_id_of".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_offset".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_data_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_stream_data".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_dcid_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_dcid".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_scid_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_scid".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_payload_offset".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "quic_hkdf_expand_label".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_hkdf_expand_label_hex".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_initial_client_secret".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_secret_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_key".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_iv".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_key_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_iv_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_hp".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_client_hp_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_protect".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_initial_unprotect".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_header_protection_mask".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_header_protection_mask_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_hp_mask".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_initial_hp_mask_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "quic_header_protect_apply".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_header_protect_remove".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_initial_packet_protect".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quic_initial_packet_unprotect".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "hex_decode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_record_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_record_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_record_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_aead_seal".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_aead_open".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_record_appdata_seal".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_record_appdata_open".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_record_seq_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_record_write_seq".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_record_read_seq".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_record_appdata_seal_seq".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_record_appdata_open_seq".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_client_hello".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_client_hello_legacy_version".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_client_hello_random".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_client_hello_has_aes128_gcm".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_server_hello".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_hello_random".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_certificate".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_certificate_der".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_certificate_verify".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_certificate_verify_scheme".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_certificate_verify_sig".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_encrypted_extensions".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "tls_finished".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_hs_msg_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert("tls_hs_reset".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("tls_hs_state".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "tls_hs_advance".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_is_app".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_feed".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_client_hello".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_server_hello".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_finished_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_hs_session_encrypted_extensions".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_certificate".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_certificate_verify".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_hs_session_finished".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_finished_verify_data".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_finished_verify_data_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_transcript_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_transcript_append".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_transcript_len".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_transcript_finished_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_derive_secret".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_derive_secret_hex".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_client_handshake_traffic_secret".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_handshake_traffic_secret".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_client_handshake_traffic_secret_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_handshake_traffic_secret_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_client_application_traffic_secret".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_application_traffic_secret".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_client_application_traffic_secret_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_application_traffic_secret_hex".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "pb_encode_varint".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "pb_decode_varint".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_varint_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_encode_key".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "pb_key_field".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_key_wire".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_zigzag_encode".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_zigzag_decode".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_encode_sint".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "pb_decode_sint".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_encode_bytes".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "pb_bytes_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_encode_field_varint".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "pb_encode_simple".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "pb_simple_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "pb_simple_id".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_encode_nested".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "pb_nested_inner".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "pb_encode_repeated_varint".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "pb_repeated_count".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "pb_repeated_at".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "regex_match".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Bool)),
        );
        fns.insert(
            "regex_find".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "regex_capture".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert("argc".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "args".into(),
            Type::Fn(vec![], Box::new(Type::Array(Box::new(Type::String)))),
        );
        fns.insert(
            "arg_get".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "json_array_push_string".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_object_from_map_ss".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "log_warn".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "actor_spawn".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Chan(Box::new(Type::Int)))),
        );
        fns.insert(
            "actor_send".into(),
            Type::Fn(
                vec![Type::Chan(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Bool),
            ),
        );
        fns.insert(
            "actor_recv".into(),
            Type::Fn(vec![Type::Chan(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "actor_stop".into(),
            Type::Fn(vec![Type::Chan(Box::new(Type::Int))], Box::new(Type::Void)),
        );
        // Packed actor messages: tag (high 16) + int payload (low 48).
        fns.insert(
            "actor_pack".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "actor_msg_tag".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "actor_msg_payload".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_listen".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_listen_addr".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_accept".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_write_all".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_read_print".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_read".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tcp_read_fast".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tcp_read_n".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tcp_peer_addr".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tcp_local_addr".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tcp_shutdown".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_linger".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sock_error".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_nodelay".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        // Session controls: timeouts, keepalive, accept backlog.
        fns.insert(
            "tcp_set_timeout".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_keepalive".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_listen_backlog".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        // JSON / crypto / log / metrics / share / slice / safe_add
        fns.insert(
            "json_object".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_has".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "json_get_string".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_get_int".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "json_nest".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_get_object".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_path_string".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "json_path_int".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "json_merge".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "openapi_route".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "openapi_doc".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "graphql_field".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_arg".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_data".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_request".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_is_mutation".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sse_event".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sse_retry".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "rpc_frame".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "rpc_method".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "rpc_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_array_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "json_array_get_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "json_array_get_string".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "json_array_ints3".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "json_array_strings2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "json_array_push_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "bin_encode_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_get_string".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_get_int".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "yaml_get_bool".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "yaml_has".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "yaml_escape".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_pair".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_pair_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_pair_bool".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_merge".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "yaml_get_list".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "yaml_keys".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "toml_get_string".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "toml_get_int".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "toml_get_bool".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "toml_get_float".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Float)),
        );
        fns.insert(
            "toml_has".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "toml_get_in".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "toml_get_int_in".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "toml_escape".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "toml_pair".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "toml_pair_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "toml_pair_bool".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "toml_section".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "toml_merge".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "toml_keys".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "msgpack_int_hex".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_int_hex".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "avro_long_hex".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "avro_encode_long".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "avro_decode_long".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "avro_long_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "avro_encode_bool".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "avro_decode_bool".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "avro_encode_null".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "avro_encode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "avro_decode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "avro_encode_array_long".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "avro_decode_array_long".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "graphql_is_query".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "graphql_operation_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_request_vars".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_has_field".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "graphql_data2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "graphql_query_from_body".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_variables_from_body".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "graphql_fields".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        // Language constructor: HTTP body or raw query → Graphql value.
        fns.insert(
            "graphql_parse".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Graphql)),
        );
        // In-process message queues (std/messaging).
        fns.insert("mq_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "mq_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "mq_declare".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mq_publish".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mq_try_take".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mq_len".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mq_purge".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "time_offset_named".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "time_format_offset".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "time_in_offset".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        // Binary hex helpers
        fns.insert(
            "bytes_to_hex".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hex_to_bytes".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        // MessagePack binary
        fns.insert(
            "msgpack_encode_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "msgpack_encode_bool".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "msgpack_encode_nil".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "msgpack_encode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "msgpack_encode_array_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "msgpack_decode_int".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "msgpack_decode_bool".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "msgpack_is_nil".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "msgpack_decode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "msgpack_decode_array_int".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        // CBOR binary
        fns.insert(
            "cbor_encode_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_encode_bool".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_encode_null".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_encode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_encode_array_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "cbor_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cbor_decode_int".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cbor_decode_bool".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cbor_is_null".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "cbor_decode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cbor_decode_array_int".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        // List combinators / mono-style helpers
        fns.insert(
            "list_take_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_drop_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_zip_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_find_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_count_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_any_eq_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_all_eq_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_take_while_lt_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_map_add_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_map_mul_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_filter_lt_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_filter_gt_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_fold_add_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_fold_mul_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sha256".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hmac_sha256".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        // Raw-byte variants (for binary protocols like SCRAM-SHA-256).
        fns.insert(
            "sha256_raw".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hmac_sha256_raw".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "xor_bytes".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        // PBKDF2-HMAC-SHA256 (password, salt, iterations, dklen) -> derived key.
        fns.insert(
            "pbkdf2_sha256".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "log_info".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "log_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "validate_required".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "validate_min_len".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "validate_max_len".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "validate_int_range".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "validate_email".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "game_fixed_steps".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "game_fixed_remainder".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "game_alpha".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "game_frame_budget_ok".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fx_from_int".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fx_to_int".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fx_mul".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "fx_div".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "det_rng_next".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "det_rng_range".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "replay_append".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "replay_input".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_push".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_pop".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_peek".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_len".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ring_cap".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "lfq_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "lfq_try_push".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "lfq_try_pop".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "lfq_len".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sg_gather2".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sg_gather3".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sg_slice".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fsm_rule".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "fsm_is".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "fsm_can".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "fsm_transition".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "frame_alloc_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "frame_alloc".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "frame_reset".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "frame_used".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "frame_cap".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "obj_pool_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "obj_acquire".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "obj_release".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "obj_available".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "obj_pool_cap".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_track_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_track_alloc".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_track_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_live_bytes".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_high_bytes".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "alloc_report_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert("leak_mark".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "leak_bytes_since".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "leak_detected".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "leak_assert_clear".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "leak_report_json".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "leak_scope_enter".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "leak_scope_exit".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert("leak_check".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "leak_assert_scope".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        // Checked arithmetic (always trap on overflow).
        fns.insert(
            "checked_add".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "checked_sub".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "checked_mul".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "would_overflow_add".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "would_overflow_sub".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "would_overflow_mul".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        // Graceful shutdown.
        fns.insert(
            "signal_on_term".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "register_listener".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "close_listeners".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "server_shutdown_begin".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "server_drain".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "shutdown_requested".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "should_stop_accepting".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "install_graceful_shutdown".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        // Tracing.
        fns.insert("trace_id".into(), Type::Fn(vec![], Box::new(Type::String)));
        fns.insert(
            "trace_export_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "trace_export_otlp_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "trace_export_otlp_pb".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "trace_export_otlp_pb_len".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "otlp_http_export".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "otlp_export_traces_json".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "otlp_export_traces_pb".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_request_ct".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "trace_span_id".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "trace_current".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "trace_set".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert("trace_clear".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "trace_begin".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert("trace_end".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "trace_log".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "stack_trace".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "crash_report_install".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "crash_report_installed".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        // Closure env free + debugger / task inspect seeds.
        fns.insert(
            "fn_drop".into(),
            Type::Fn(
                vec![Type::Fn(vec![], Box::new(Type::Int))],
                Box::new(Type::Void),
            ),
        );
        // Overload: accept any fn value — use a loose type via special-case below if needed.
        // Real typing: fn_drop is checked specially; register as taking a generic-ish.
        fns.insert(
            "fn_has_env".into(),
            Type::Fn(
                vec![Type::Fn(vec![], Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "task_done".into(),
            Type::Fn(vec![Type::Job(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "task_joined".into(),
            Type::Fn(vec![Type::Job(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "task_id".into(),
            Type::Fn(vec![Type::Job(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "tasks_inspect_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "debug_break".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_break_hits".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_break_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_set_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_get_int".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_locals_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "debug_bp_enable".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_bp_disable".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_bp".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_trap_enable".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_trap_enabled".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_line_bp_set".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_line_bp_clear".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_line_bp_hits".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_push_frame".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "debug_pop_frame".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_frame_depth".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_frames_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "debug_snapshot_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "debug_set_current_task".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "debug_current_task".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "dap_initialize_response".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "dap_stopped_event".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "dap_threads_response".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "dap_request_command".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dap_request_seq".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dap_handle_request".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "process_rss_bytes".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "process_cpu_user_us".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "process_cpu_sys_us".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_snapshot_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "profile_sample_clear".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_once".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_len".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_start".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_stop".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_cpu_us".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_sample_wall_ns".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_samples_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "profile_samples_pprof_text".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "profile_sample_thread_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "profile_pprof_http_body".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "profile_http_route".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "ecs_world_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_spawn".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_alive".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_despawn".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_add".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "ecs_set".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "ecs_has".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_get".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_remove".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_query_count".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_query_first".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_archetype".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ecs_system_add".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        /* GPU / accelerator seed (host compute path; see mako_gpu.h). */
        fns.insert(
            "gpu_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_backend".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "gpu_device_open".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_device_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_device_backend".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gpu_device_name".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gpu_device_vendor".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gpu_device_is_gpu".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_opencl_ok".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert("gpu_metal_ok".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert("gpu_cuda_ok".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "gpu_vulkan_ok".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );

        fns.insert(
            "gpu_set_prefer_host".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_new".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_len".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_cap".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_buf_read".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gpu_upload_f32".into(),
            Type::Fn(
                vec![Type::Int, Type::Array(Box::new(Type::Float))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_download_f32".into(),
            Type::Fn(
                vec![Type::Int],
                Box::new(Type::Array(Box::new(Type::Float))),
            ),
        );
        fns.insert(
            "gpu_f32_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_add_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_mul_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_scale_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Float], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_fill_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Float], Box::new(Type::Int)),
        );
        /* AI building blocks (inference/training primitives). */
        fns.insert(
            "gpu_relu_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_saxpy_f32".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Float],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_bias_add_f32".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_matmul_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_softmax_rows_f32".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_sum_f32".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Float)),
        );
        fns.insert(
            "gpu_gelu_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_silu_f32".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gpu_transpose_f32".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_layernorm_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Float,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_attention_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "gpu_mha_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        /* Local model weight store + safetensors / .makomodel I/O */
        fns.insert(
            "model_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "model_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "model_tensor_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "model_tensor_name".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "model_tensor_buf".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_tensor_elems".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_tensor_ndim".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_tensor_dim".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "model_set_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::Array(Box::new(Type::Float)),
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "model_load_safetensors".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_load_gguf".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_save".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_load".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "model_linear_f32".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        /* Vocab tokenizer seed */
        fns.insert("tok_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "tok_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_set".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tok_size".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_id".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_token".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tok_load_json".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_load_lines".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_encode".into(),
            Type::Fn(
                vec![Type::Int, Type::String],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "tok_decode".into(),
            Type::Fn(
                vec![Type::Int, Type::Array(Box::new(Type::Int))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tok_load_merges".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tok_load_bpe".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tok_encode_bpe".into(),
            Type::Fn(
                vec![Type::Int, Type::String],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "tok_merge_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "cookie_get".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cookie_make".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "session_id_new".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        // Configurable resource limits
        fns.insert(
            "limits_new".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int],
                Box::new(Type::Named("Limits".into())),
            ),
        );
        fns.insert(
            "limits_free".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "limits_try_mem".into(),
            Type::Fn(
                vec![Type::Named("Limits".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "limits_release_mem".into(),
            Type::Fn(
                vec![Type::Named("Limits".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "limits_check_time".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "limits_try_conn".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "limits_release_conn".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "limits_mem_used".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "limits_open_conns".into(),
            Type::Fn(vec![Type::Named("Limits".into())], Box::new(Type::Int)),
        );
        // Remote session cancellation
        fns.insert(
            "session_cancel_token".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "session_cancel".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "session_cancelled".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "session_cancel_clear".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        // SCRAM channel binding
        fns.insert(
            "scram_gs2_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "scram_cbind_b64".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "scram_client_final_without_proof".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "csrf_token".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "csrf_check".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "auth_bearer".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "auth_check_bearer".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "auth_basic_header".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "auth_check_basic".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "auth_role_has".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "authz_allow_role".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "auth_token_sign".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "auth_token_subject".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "auth_token_check".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "auth_session_cookie".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "rate_allow".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "rate_remaining".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_content_encoding".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http_compress_if_accepted".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cache_put".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "cache_get".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cache_has".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "job_schedule".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "job_due".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "job_delay_ms".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "job_cancel".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "conn_pool_slot".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "conn_pool_next".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "lb_pick2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "lb_pick3".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "slog_redact".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "slog_with_redacted".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "metric_inc".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "metric_add".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "metric_get".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "gauge_set".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "gauge_add".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "gauge_get".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "hist_observe".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "hist_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "hist_sum".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "hist_avg".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "metrics_export".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "metrics_export_prom".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "metrics_export_otlp_json".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "share_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("ShareInt".into()))),
        );
        fns.insert(
            "share_set".into(),
            Type::Fn(
                vec![Type::Named("ShareInt".into()), Type::Int],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "share_clone".into(),
            Type::Fn(
                vec![Type::Named("ShareInt".into())],
                Box::new(Type::Named("ShareInt".into())),
            ),
        );
        fns.insert(
            "share_get".into(),
            Type::Fn(vec![Type::Named("ShareInt".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "share_drop".into(),
            Type::Fn(vec![Type::Named("ShareInt".into())], Box::new(Type::Void)),
        );
        fns.insert(
            "slice_ints".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int, Type::Int],
                Box::new(Type::Named("Slice".into())),
            ),
        );
        fns.insert(
            "slice_len".into(),
            Type::Fn(vec![Type::Named("Slice".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "slice_get".into(),
            Type::Fn(
                vec![Type::Named("Slice".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "as_bytes".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::Byte))),
            ),
        );
        fns.insert(
            "bytes_as_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Byte))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "bytes_is_view".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Byte))], Box::new(Type::Int)),
        );
        fns.insert(
            "bytes_view".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Array(Box::new(Type::Byte))),
            ),
        );
        fns.insert(
            "buf_get".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Array(Box::new(Type::Byte)))),
        );
        fns.insert(
            "buf_put".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Byte))],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "simd_xor_bytes".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Byte))], Box::new(Type::Int)),
        );
        fns.insert(
            "http_req_method".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_req_path".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_req_body".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_request_parse".into(),
            Type::Fn(vec![Type::String], Box::new(Type::HttpRequest)),
        );
        fns.insert(
            "http_request_from_conn".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::HttpRequest)),
        );
        fns.insert(
            "http_request_method".into(),
            Type::Fn(vec![Type::HttpRequest], Box::new(Type::String)),
        );
        fns.insert(
            "http_request_path".into(),
            Type::Fn(vec![Type::HttpRequest], Box::new(Type::String)),
        );
        fns.insert(
            "http_request_body".into(),
            Type::Fn(vec![Type::HttpRequest], Box::new(Type::String)),
        );
        fns.insert(
            "http_route_match".into(),
            Type::Fn(
                vec![Type::HttpRequest, Type::String, Type::String],
                Box::new(Type::Bool),
            ),
        );
        fns.insert(
            "http_route_param".into(),
            Type::Fn(
                vec![Type::HttpRequest, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert("router_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "router_group".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "router_add".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "router_match".into(),
            Type::Fn(vec![Type::Int, Type::HttpRequest], Box::new(Type::String)),
        );
        fns.insert(
            "router_match_path".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "router_param".into(),
            Type::Fn(
                vec![Type::Int, Type::HttpRequest, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "router_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert("reqctx_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "reqctx_set".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "reqctx_get".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "reqctx_has".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "reqctx_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "middleware_allow_methods".into(),
            Type::Fn(vec![Type::HttpRequest, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "middleware_next".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "middleware_ran".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "middleware_trace".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "middleware_require_context".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "http_health_json".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_respond_health".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "buf_reader_new".into(),
            Type::Fn(vec![Type::String], Box::new(Type::BufReader)),
        );
        fns.insert(
            "buf_reader_from_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::BufReader)),
        );
        fns.insert(
            "buf_read_line".into(),
            Type::Fn(vec![Type::BufReader], Box::new(Type::String)),
        );
        fns.insert(
            "buf_read".into(),
            Type::Fn(vec![Type::BufReader, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "buf_reader_close".into(),
            Type::Fn(vec![Type::BufReader], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_writer_new".into(),
            Type::Fn(vec![Type::String], Box::new(Type::BufWriter)),
        );
        fns.insert(
            "buf_write".into(),
            Type::Fn(vec![Type::BufWriter, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_write_byte".into(),
            Type::Fn(vec![Type::BufWriter, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_flush".into(),
            Type::Fn(vec![Type::BufWriter], Box::new(Type::Int)),
        );
        fns.insert(
            "buf_writer_close".into(),
            Type::Fn(vec![Type::BufWriter], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_open_sqlite".into(),
            Type::Fn(vec![Type::String], Box::new(Type::SqlDB)),
        );
        fns.insert(
            "sql_open_postgres".into(),
            Type::Fn(vec![Type::String], Box::new(Type::SqlDB)),
        );
        fns.insert(
            "sql_ok".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_close".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_query_int".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_exec".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_exec_plain".into(),
            Type::Fn(vec![Type::SqlDB, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_exec_str4".into(),
            Type::Fn(
                vec![
                    Type::SqlDB,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_query_str".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sql_query_str2".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sql_query_str3".into(),
            Type::Fn(
                vec![
                    Type::SqlDB,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sql_query_str4".into(),
            Type::Fn(
                vec![
                    Type::SqlDB,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "sql_last_insert_id".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rows_affected".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_query_rows".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_query_rows_str".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_rows_ok".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rows_next".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rows_int".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rows_str".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "sql_rows_cols".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rows_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_query_col_int".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "sql_query_col_str".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::String, Type::Int],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "sql_begin".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_commit".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_rollback".into(),
            Type::Fn(vec![Type::SqlDB], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_prepare".into(),
            Type::Fn(vec![Type::SqlDB, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_stmt_query_int".into(),
            Type::Fn(
                vec![Type::Int, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_stmt_exec".into(),
            Type::Fn(
                vec![Type::Int, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_stmt_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_migration_applied".into(),
            Type::Fn(vec![Type::SqlDB, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_migrate".into(),
            Type::Fn(
                vec![Type::SqlDB, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_check_typed".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_pool_open_sqlite".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_open_postgres".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_ok".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_size".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_opened".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_next_slot".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "sql_pool_query_int".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_pool_exec".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sql_pool_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "wait_group_new".into(),
            Type::Fn(vec![], Box::new(Type::WaitGroup)),
        );
        fns.insert(
            "wait_group_add".into(),
            Type::Fn(vec![Type::WaitGroup, Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "wait_group_done".into(),
            Type::Fn(vec![Type::WaitGroup], Box::new(Type::Void)),
        );
        fns.insert(
            "wait_group_wait".into(),
            Type::Fn(vec![Type::WaitGroup], Box::new(Type::Void)),
        );
        fns.insert(
            "flag_string".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "flag_int".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "flag_bool".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "exec_output".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "exec_run".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "url_query_escape".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "url_scheme".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "url_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "url_path".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "url_query".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "csv_split_line".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "csv_join_row".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "xml_escape".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "html_escape".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "xml_tag_text".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gzip_compress".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gzip_decompress".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gzip_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tar_write_file".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tar_first_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mime_type".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "context_with_timeout".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "context_expired".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "context_remaining".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "bytes_buffer".into(),
            Type::Fn(vec![], Box::new(Type::Named("BytesBuffer".into()))),
        );
        fns.insert(
            "bytes_buffer_write".into(),
            Type::Fn(
                vec![Type::Named("BytesBuffer".into()), Type::String],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "bytes_buffer_string".into(),
            Type::Fn(
                vec![Type::Named("BytesBuffer".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "bytes_buffer_len".into(),
            Type::Fn(vec![Type::Named("BytesBuffer".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "bytes_buffer_reset".into(),
            Type::Fn(
                vec![Type::Named("BytesBuffer".into())],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "rand_seed".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "rand_intn".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert("rand_float".into(), Type::Fn(vec![], Box::new(Type::Float)));
        fns.insert(
            "template_execute".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        /* Go-style template engine */
        fns.insert(
            "tmpl_data_new".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tmpl_data_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tmpl_data_set".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tmpl_data_set_list".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tmpl_data_set_int".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tmpl_new".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "tmpl_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tmpl_execute".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tmpl_html_execute".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tmpl_text".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "tmpl_html".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "base32_encode".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sha1".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sha512".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "lookup_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dns_lookup_count".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dns_lookup_all".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dns_lookup_ipv4".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dns_lookup_ipv6".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "parse_ip_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dns_ip_family".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dns_is_loopback".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dns_is_private".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "dns_normalize_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dns_join_host_port".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "dns_split_host".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "dns_split_port".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "signal_notify".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "signal_received".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        // Per-signal, name-based hooks ("HUP"/"TERM"/"INT"/"USR1"/"USR2"/"PIPE"/…).
        fns.insert(
            "signal_watch".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "signal_fired".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "signal_ignore".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        // File-system watch (kqueue / inotify): watch paths, poll for changes.
        fns.insert(
            "watch_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "watch_new".into(),
            Type::Fn(vec![], Box::new(Type::Named("Watcher".into()))),
        );
        fns.insert(
            "watch_add".into(),
            Type::Fn(
                vec![Type::Named("Watcher".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "watch_poll".into(),
            Type::Fn(
                vec![Type::Named("Watcher".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "watch_close".into(),
            Type::Fn(vec![Type::Named("Watcher".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "atomic_new".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("AtomicInt".into()))),
        );
        fns.insert(
            "atomic_load".into(),
            Type::Fn(vec![Type::Named("AtomicInt".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "atomic_store".into(),
            Type::Fn(
                vec![Type::Named("AtomicInt".into()), Type::Int],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "atomic_add".into(),
            Type::Fn(
                vec![Type::Named("AtomicInt".into()), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "atomic_cas".into(),
            Type::Fn(
                vec![Type::Named("AtomicInt".into()), Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "utf8_valid".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_rune_len".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_valid_rune".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_decode_rune".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_decode_size".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_decode_last_rune".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_decode_last_size".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_encode_rune".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "utf8_full_rune".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_rune_start".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_rune_error".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_rune_self".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "utf8_max_rune".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert("utf8_utf_max".into(), Type::Fn(vec![], Box::new(Type::Int)));
        // unicode UCD seed (range tables)
        for name in [
            "unicode_is_letter",
            "unicode_is_digit",
            "unicode_is_space",
            "unicode_is_punct",
            "unicode_is_symbol",
            "unicode_is_control",
            "unicode_is_print",
            "unicode_is_graphic",
            "unicode_is_upper",
            "unicode_is_lower",
            "unicode_is_title",
            "unicode_to_lower",
            "unicode_to_upper",
            "unicode_to_title",
            "unicode_simple_fold",
        ] {
            fns.insert(name.into(), Type::Fn(vec![Type::Int], Box::new(Type::Int)));
        }
        fns.insert(
            "unicode_is".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "filepath_walk".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "slices_reverse".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "slices_unique".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "slices_reverse_strs".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "slices_unique_strs".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "strings_index".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "strings_copy".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        // List[int] / List[string] helpers (List[T] aliases []T)
        fns.insert(
            "list_new_int".into(),
            Type::Fn(vec![], Box::new(Type::Array(Box::new(Type::Int)))),
        );
        fns.insert(
            "list_new_str".into(),
            Type::Fn(vec![], Box::new(Type::Array(Box::new(Type::String)))),
        );
        fns.insert(
            "list_push_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_push_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "list_pop_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_popped_int".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "list_pop_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "list_popped_str".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "list_get_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_get_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "list_len_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "list_len_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_clear_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_clear_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "list_insert_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int, Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_insert_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::Int, Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "list_remove_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_remove_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String)), Type::Int],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "stack_peek_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "stack_peek_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "queue_pop_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "queue_popped_int".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "queue_pop_str".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "queue_popped_str".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        // Richer collections: set / heap / ring / stats
        fns.insert(
            "list_sum_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "list_min_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "list_max_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "list_concat_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_concat_str".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::String)),
                    Type::Array(Box::new(Type::String)),
                ],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "list_binary_search_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "set_union_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "set_intersect_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "set_diff_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "set_has_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "heap_push_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "heap_pop_int".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "heap_popped_int".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "heap_peek_int".into(),
            Type::Fn(vec![Type::Array(Box::new(Type::Int))], Box::new(Type::Int)),
        );
        fns.insert(
            "list_eq_int".into(),
            Type::Fn(
                vec![
                    Type::Array(Box::new(Type::Int)),
                    Type::Array(Box::new(Type::Int)),
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "list_fill_int".into(),
            Type::Fn(
                vec![Type::Int, Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "list_range_int".into(),
            Type::Fn(
                vec![Type::Int, Type::Int],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "embed_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "filepath_walk_n".into(),
            Type::Fn(
                vec![Type::String, Type::Int],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "zip_write_file".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "zip_first_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "zip_read_file".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "png_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "png_encode_gray".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "png_encode_rgb".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "png_decode_gray".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "png_width".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "png_height".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "maps_keys".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "maps_values".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::Int))],
                Box::new(Type::Array(Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "maps_clear".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::Int))],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "maps_clone".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::Int))],
                Box::new(Type::Map(Box::new(Type::String), Box::new(Type::Int))),
            ),
        );
        fns.insert(
            "maps_equal".into(),
            Type::Fn(
                vec![
                    Type::Map(Box::new(Type::String), Box::new(Type::Int)),
                    Type::Map(Box::new(Type::String), Box::new(Type::Int)),
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "maps_copy".into(),
            Type::Fn(
                vec![
                    Type::Map(Box::new(Type::String), Box::new(Type::Int)),
                    Type::Map(Box::new(Type::String), Box::new(Type::Int)),
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "reflect_type_of_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_type_of_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_kind_of_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_kind_of_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_value_string_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_value_string_str".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_len_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "httptest_serve_once".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "httptest_get".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "httptest_status".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "httptest_header".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "aead_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "argon2_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "argon2id_hash".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "argon2id_verify".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "bcrypt_hash".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "bcrypt_verify".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "bcrypt_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "aes_gcm_seal".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "aes_gcm_open".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        // Encryption at rest (AES-GCM + random nonce prefix)
        fns.insert(
            "seal_at_rest".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "open_at_rest".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "seal_file_at_rest".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "open_file_at_rest".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "aes_ctr".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "hmac_sha1".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "hmac_sha1_raw".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "chacha20_poly1305_seal".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "chacha20_poly1305_open".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "multipart_boundary".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "multipart_form_value".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "multipart_file_name".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "multipart_file_content_type".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "multipart_file_value".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "multipart_file_size".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "multipart_file_allowed".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "regex_find_all".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "regex_replace".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "regex_replace_all".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "zip_deflate_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "gif_encode_rgb".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gif_decode_rgb".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gif_width".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "gif_height".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_encode_gray".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_decode_gray".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "jpeg_width".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_height".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "reflect_struct_num_fields".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "reflect_struct_field_name".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_struct_field_type".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_struct_has_field".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "html_template_execute".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_encode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gob_decode_string".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "gob_encode_int".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "gob_decode_int".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_parse_address".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mail_header_get".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mail_address_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "slog_set_level".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_info".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_warn".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_error".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_debug".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_with".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "slog_with2".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "slog_with3".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "slog_with_int".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::Void),
            ),
        );
        fns.insert(
            "slog_set_json".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert("slog_is_json".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "slog_set_service".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Void)),
        );
        fns.insert(
            "slog_set_output".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert("slog_flush".into(), Type::Fn(vec![], Box::new(Type::Void)));
        fns.insert(
            "slog_get_level".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "regex_valid".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "regex_quote_meta".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "zip_create".into(),
            Type::Fn(vec![], Box::new(Type::Named("ZipWriter".into()))),
        );
        fns.insert(
            "zip_add".into(),
            Type::Fn(
                vec![Type::Named("ZipWriter".into()), Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "zip_write_to".into(),
            Type::Fn(
                vec![Type::Named("ZipWriter".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "zip_close".into(),
            Type::Fn(vec![Type::Named("ZipWriter".into())], Box::new(Type::Void)),
        );
        fns.insert(
            "zip_list".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "html_template_execute2".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "html_template_execute3".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "html_template_if".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_encode_map_ss".into(),
            Type::Fn(
                vec![Type::Map(Box::new(Type::String), Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_decode_map_ss".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Map(Box::new(Type::String), Box::new(Type::String))),
            ),
        );
        fns.insert(
            "binary_put_u16le".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_put_u32le".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_put_u64le".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_u16le".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "binary_u32le".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "binary_u64le".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "binary_put_u16be".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_put_u32be".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_put_u64be".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "binary_u16be".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "binary_u32be".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "binary_u64be".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "html_template_range".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "html_template_with".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_encode_struct".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_decode_struct".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "smtp_format_message".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "smtp_send_soft".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "smtp_send_dialog".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "reflect_value_new".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "reflect_value_from_2".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "reflect_value_from_2_int".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "reflect_value_set".into(),
            Type::Fn(
                vec![
                    Type::Named("ReflectValue".into()),
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "reflect_value_get".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "reflect_value_num_fields".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "reflect_value_field_at".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "reflect_value_set_at".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into()), Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "reflect_value_schema".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_encode_gray_dct".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_dct_dc".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "gif_encode_rgb_lzw".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gif_decode_rgb_lzw".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "html_template_nested".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_encode_strs".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::String))],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "gob_decode_strs".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "smtp_auth_plain".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "smtp_send_auth".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "smtp_starttls_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "reflect_value_clone".into(),
            Type::Fn(
                vec![Type::Named("ReflectValue".into())],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "reflect_value_equal".into(),
            Type::Fn(
                vec![
                    Type::Named("ReflectValue".into()),
                    Type::Named("ReflectValue".into()),
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "jpeg_encode_gray_huff".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_encode_gray_baseline".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_is_baseline_huff".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_huff_block".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "jpeg_encode_gray_jfif".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "jpeg_is_jfif".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_sof0".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_width".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_height".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_precision".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_components".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_baseline_gray".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_major".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_minor".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_sampling".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_component_id".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_density_units".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_x_density".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_y_density".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_app7".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_quant_table".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_thumb_width".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_thumb_height".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_mako_jfif".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_eoi".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_sof0_matches_app7".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_mako_complete".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_mako_raw".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_jfif_app0_length".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_app7_payload_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_app8".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_app9".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_mako_dct".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_is_mako_huff".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_app8_length".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_app9_length".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_roundtrip_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_app7_length".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_has_soi".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "jpeg_app7_len_matches_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_send_starttls".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        /* Full mail builder + SMTP session */
        fns.insert("mail_msg_new".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "mail_msg_free".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_set_from".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_add_to".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_add_cc".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_add_bcc".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_set_subject".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_set_text".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_set_html".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_add_header".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mail_msg_attach".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "mail_msg_build".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "mail_msg_envelope_from".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "mail_msg_rcpt_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "mail_msg_rcpt_at".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "mail_simple".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "smtp_new".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_set_timeout_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_connect".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_ehlo".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_starttls".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_auth".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "smtp_mail_from".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_rcpt_to".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_data".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_quit".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_last_reply".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "smtp_last_code".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_send_built".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_send_msg".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "smtp_mock_start".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_mock_port".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_mock_serve_once".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "smtp_mock_last_message".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "smtp_mock_last_from".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "smtp_mock_last_rcpt".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "smtp_mock_stop".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "reflect_type_schema".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "reflect_value_of_type".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("ReflectValue".into())),
            ),
        );
        fns.insert(
            "reflect_type_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "reflect_type_name_at".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "str_cut".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Array(Box::new(Type::String))),
            ),
        );
        fns.insert(
            "str_count".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );

        fns.insert(
            "detached_join_all".into(),
            Type::Fn(vec![], Box::new(Type::Void)),
        );
        fns.insert(
            "await_timeout".into(),
            Type::Fn(
                vec![Type::Job(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "safe_add".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "dlopen_probe".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_open".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_call".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "plugin_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_close_all".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_alive".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_path".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_name".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_version".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_kind".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_plugin_abi".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert("plugin_count".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "plugin_max_slots".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_abi_version".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_api_version".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_last_error".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_last_error_str".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_call_ok".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_last_log".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_last_log_level".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_log_count".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_reload".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_find".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_manifest_artifact".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_manifest_lib_path".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_open_manifest".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "plugin_call1".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "plugin_info_json".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "ffi_abi_name".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );

        fns.insert(
            "http_listen".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_echo_once".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_echo".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_accept_key".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "ws_upgrade_request_ok".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_client_request".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "ws_client_accept_ok".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_accept".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_recv".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "ws_last_opcode".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_send_text".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_send_binary".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_send_ping".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_send_close".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "ws_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_client_connect".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "ws_client_send_text".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_client_send_binary".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_client_send_ping".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_client_send_close".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "ws_client_recv".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "ws_send_pong".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "ws_last_close_code".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert("ws_last_fin".into(), Type::Fn(vec![], Box::new(Type::Int)));
        fns.insert(
            "ws_last_status".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "pg_connect".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Named("PgConn".into()))),
        );
        fns.insert(
            "pg_ok".into(),
            Type::Fn(vec![Type::Named("PgConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pg_close".into(),
            Type::Fn(vec![Type::Named("PgConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "pg_exec".into(),
            Type::Fn(
                vec![Type::Named("PgConn".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pg_exec_row_count".into(),
            Type::Fn(
                vec![Type::Named("PgConn".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pg_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mysql_connect".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("MysqlConn".into())),
            ),
        );
        fns.insert(
            "mysql_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mysql_ok".into(),
            Type::Fn(vec![Type::Named("MysqlConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "mysql_close".into(),
            Type::Fn(vec![Type::Named("MysqlConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "mysql_is_mariadb".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "mysql_driver_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "redis_connect".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("RedisConn".into())),
            ),
        );
        fns.insert(
            "redis_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "redis_ok".into(),
            Type::Fn(vec![Type::Named("RedisConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "redis_close".into(),
            Type::Fn(vec![Type::Named("RedisConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "redis_conn_ping".into(),
            Type::Fn(
                vec![Type::Named("RedisConn".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_conn_set".into(),
            Type::Fn(
                vec![Type::Named("RedisConn".into()), Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_conn_get".into(),
            Type::Fn(
                vec![Type::Named("RedisConn".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_conn_del".into(),
            Type::Fn(
                vec![Type::Named("RedisConn".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_conn_exists".into(),
            Type::Fn(
                vec![Type::Named("RedisConn".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "mongo_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "mongo_find_one_request".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cassandra_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "cassandra_select".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "clickhouse_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "clickhouse_select".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "elastic_connect_url".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "elastic_search_request".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_encode_message".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_message_len".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_message_within_limit".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_default_max_message".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_message_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_unary_request".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_unary_name".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_unary_id".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_content_type".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_status_trailer".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_status_code".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_http2_unary".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "grpc_http2_unary_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_http2_unary_response".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "grpc_http2_unary_response_status".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "grpc_http2_response_payload".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "grpc_http2_response_status".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_http2_stream_data".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "grpc_http2_stream_two".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "grpc_http2_stream_data_count".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "grpc_http2_client_stream_flow".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        // HTTP handler surface
        fns.insert(
            "http_bind".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_bind_addr".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_accept".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_method".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_path".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_body".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "http_respond".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_respond_ct".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_respond_json".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_header".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "http_next".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_keepalive".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_close_listener".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_begin".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_reset".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_requested".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_ready".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_deadline".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_remaining".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_expired".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_active_connections".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_drain_conn".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "http_shutdown_from_signal".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_get_insecure".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        // Socket-style TLS server API (own the accept loop; upgrade a TCP fd to TLS).
        fns.insert(
            "tls_server_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_client_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_client_new".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("TlsClient".into())),
            ),
        );
        fns.insert(
            "tls_client_new_insecure".into(),
            Type::Fn(vec![], Box::new(Type::Named("TlsClient".into()))),
        );
        fns.insert(
            "tls_client_free".into(),
            Type::Fn(vec![Type::Named("TlsClient".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_connect".into(),
            Type::Fn(
                vec![Type::Named("TlsClient".into()), Type::Int, Type::String],
                Box::new(Type::Named("TlsConn".into())),
            ),
        );
        fns.insert(
            "tls_connect_start".into(),
            Type::Fn(
                vec![Type::Named("TlsClient".into()), Type::Int, Type::String],
                Box::new(Type::Named("TlsConn".into())),
            ),
        );
        fns.insert(
            "tls_peer_cn".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::String)),
        );
        fns.insert(
            "tls_conn_version".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::String)),
        );
        fns.insert(
            "tls_server_new".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Named("TlsServer".into())),
            ),
        );
        fns.insert(
            "tls_server_new_tls13".into(),
            Type::Fn(
                vec![Type::String, Type::String],
                Box::new(Type::Named("TlsServer".into())),
            ),
        );
        fns.insert(
            "tls_server_new_mtls".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Named("TlsServer".into())),
            ),
        );
        fns.insert(
            "tls_server_sni_add".into(),
            Type::Fn(
                vec![
                    Type::Named("TlsServer".into()),
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_server_sni_update".into(),
            Type::Fn(
                vec![
                    Type::Named("TlsServer".into()),
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_server_sni_remove".into(),
            Type::Fn(
                vec![Type::Named("TlsServer".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_client_new_mtls".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String],
                Box::new(Type::Named("TlsClient".into())),
            ),
        );
        fns.insert(
            "tls_unique".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::String)),
        );
        fns.insert(
            "scram_tls_unique_cbind".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::String)),
        );
        fns.insert(
            "scram_plus_client_final_bare".into(),
            Type::Fn(
                vec![Type::Named("TlsConn".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_server_reload".into(),
            Type::Fn(
                vec![Type::Named("TlsServer".into()), Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_make_self_signed".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_make_csr".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "pem_count_blocks".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pem_has_block".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "pem_extract_block".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "pem_load_file".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "tls_accept".into(),
            Type::Fn(
                vec![Type::Named("TlsServer".into()), Type::Int],
                Box::new(Type::Named("TlsConn".into())),
            ),
        );
        fns.insert(
            "tls_accept_start".into(),
            Type::Fn(
                vec![Type::Named("TlsServer".into()), Type::Int],
                Box::new(Type::Named("TlsConn".into())),
            ),
        );
        fns.insert(
            "tls_handshake_step".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_is_init_finished".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_want_read".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_want_write".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_conn_fd".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_read_nb".into(),
            Type::Fn(
                vec![Type::Named("TlsConn".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_write_nb".into(),
            Type::Fn(
                vec![Type::Named("TlsConn".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_read".into(),
            Type::Fn(
                vec![Type::Named("TlsConn".into()), Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_write".into(),
            Type::Fn(
                vec![Type::Named("TlsConn".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_conn_alpn".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::String)),
        );
        fns.insert(
            "tls_conn_close".into(),
            Type::Fn(vec![Type::Named("TlsConn".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_server_free".into(),
            Type::Fn(vec![Type::Named("TlsServer".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "tls_get".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_handshake_ok".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_handshake_version".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_serve_once_h2".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_h2_settings_exchange".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_h2_get".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_h2_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_h2_get_twice".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_h2_mux".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_serve_h2_wu".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_h2_window_get".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "nghttp2_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "quiche_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "quiche_version".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        fns.insert(
            "quiche_handshake".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quiche_h3_get".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quiche_h3_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quiche_h3_get_two".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "quiche_start_server".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "quiche_stop_server".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_server_available".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_server_new".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_server_bind".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "h3_server_fd".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_server_poll".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_accept_stream".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "h3_stream_read".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "h3_stream_write".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "h3_stream_method".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "h3_stream_path".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "h3_stream_body".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "h3_stream_authority".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "h3_response".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "h3_server_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "nghttp2_get".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "nghttp2_post".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "nghttp2_get_two".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_serve_grpc_once".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_grpc_unary".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_serve_grpc_stream".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_grpc_stream".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::Int,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "tls_serve_h2_n".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_serve_h2_routes".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "chan_select3".into(),
            Type::Fn(
                vec![
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "sqlite_query_int".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "sqlite_query_text".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::String)),
        );
        fns.insert(
            "sqlite_query_int_params".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::Array(Box::new(Type::Int))],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "const_eq".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "crypto_eq".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "secret_from_str".into(),
            Type::Fn(vec![Type::String], Box::new(Type::Named("Secret".into()))),
        );
        fns.insert(
            "secret_drop".into(),
            Type::Fn(vec![Type::Named("Secret".into())], Box::new(Type::Void)),
        );
        fns.insert(
            "secret_len".into(),
            Type::Fn(vec![Type::Named("Secret".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "secret_eq_str".into(),
            Type::Fn(
                vec![Type::Named("Secret".into()), Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "hkdf_sha256".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_header_ok".into(),
            Type::Fn(vec![Type::String, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "unsafe_index".into(),
            Type::Fn(
                vec![Type::Array(Box::new(Type::Int)), Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "redis_ping".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        fns.insert(
            "redis_set".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_get".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_del".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_exists".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "redis_mock_once".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "redis_mock_kv".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "json_si".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::Int],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "json_ss".into(),
            Type::Fn(
                vec![Type::String, Type::String, Type::String, Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "json_i".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::String)),
        );
        // TLS
        fns.insert(
            "tls_serve".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_serve_once".into(),
            Type::Fn(
                vec![Type::Int, Type::String, Type::String, Type::String],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tls_serve_n".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        // Async I/O seed (sync-looking)
        fns.insert(
            "io_wait".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_poll2".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_poll3".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "io_poll4".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "io_kq_poll2".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_epoll_poll2".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_native_poll2".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_read_ready".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_write_ready".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_set_nonblocking".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_try_write".into(),
            Type::Fn(vec![Type::Int, Type::String], Box::new(Type::Int)),
        );
        fns.insert(
            "io_backoff_ms".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "io_should_pause".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "chan_select4".into(),
            Type::Fn(
                vec![
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_accept_nb".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_connect".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_connect_timeout".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_set_he_delay_ms".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Void)),
        );
        fns.insert(
            "tcp_get_he_delay_ms".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_connect_nb".into(),
            Type::Fn(vec![Type::String, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_connect_check".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_connect_wait".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_pool_open".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_pool_acquire".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_pool_release".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_pool_close".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_pool_idle".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_pool_open_count".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_fd_copy".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_splice".into(),
            Type::Fn(vec![Type::Int, Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_proxy_pump".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_set_recv_buf".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_set_send_buf".into(),
            Type::Fn(vec![Type::Int, Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_reuseport".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        fns.insert(
            "tcp_listen_reuseport".into(),
            Type::Fn(
                vec![Type::String, Type::Int, Type::Int],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "tcp_accept4".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Int)),
        );
        // Reverse-proxy upstream forward: (host, port, method, path, body) -> body.
        fns.insert(
            "http_forward".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                ],
                Box::new(Type::String),
            ),
        );
        // Full forward: status + body + byte counts.
        fns.insert(
            "http_forward_full".into(),
            Type::Fn(
                vec![
                    Type::String,
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Named("HttpForwardResult".into())),
            ),
        );
        fns.insert(
            "http_forward_fd".into(),
            Type::Fn(
                vec![
                    Type::Int,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::String,
                    Type::Int,
                ],
                Box::new(Type::Named("HttpForwardResult".into())),
            ),
        );
        fns.insert(
            "http_forward_ok".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_forward_status".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_forward_body_len".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_forward_total_bytes".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_forward_body".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_forward_headers".into(),
            Type::Fn(
                vec![Type::Named("HttpForwardResult".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_proxy_raw".into(),
            Type::Fn(
                vec![Type::Int, Type::Int, Type::String, Type::Int],
                Box::new(Type::Named("ProxyIoResult".into())),
            ),
        );
        fns.insert(
            "proxy_io_ok".into(),
            Type::Fn(
                vec![Type::Named("ProxyIoResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "proxy_io_bytes_written".into(),
            Type::Fn(
                vec![Type::Named("ProxyIoResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "proxy_io_bytes_read".into(),
            Type::Fn(
                vec![Type::Named("ProxyIoResult".into())],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "http_parse".into(),
            Type::Fn(
                vec![Type::String],
                Box::new(Type::Named("HttpParsed".into())),
            ),
        );
        fns.insert(
            "http_parsed_ok".into(),
            Type::Fn(vec![Type::Named("HttpParsed".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "http_parsed_content_length".into(),
            Type::Fn(vec![Type::Named("HttpParsed".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "http_parsed_chunked".into(),
            Type::Fn(vec![Type::Named("HttpParsed".into())], Box::new(Type::Int)),
        );
        fns.insert(
            "http_parsed_method".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_parsed_path".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_parsed_host".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_parsed_headers".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_parsed_body".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into())],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_parsed_header".into(),
            Type::Fn(
                vec![Type::Named("HttpParsed".into()), Type::String],
                Box::new(Type::String),
            ),
        );
        fns.insert(
            "http_decode_chunked".into(),
            Type::Fn(vec![Type::String], Box::new(Type::String)),
        );
        // Channel select (int family; string via chan_str_select2 / select syntax)
        fns.insert(
            "chan_select2".into(),
            Type::Fn(
                vec![
                    Type::Chan(Box::new(Type::Int)),
                    Type::Chan(Box::new(Type::Int)),
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "chan_select_value".into(),
            Type::Fn(vec![], Box::new(Type::Int)),
        );
        fns.insert(
            "chan_str_select2".into(),
            Type::Fn(
                vec![
                    Type::Chan(Box::new(Type::String)),
                    Type::Chan(Box::new(Type::String)),
                    Type::Int,
                ],
                Box::new(Type::Int),
            ),
        );
        fns.insert(
            "chan_select_value_str".into(),
            Type::Fn(vec![], Box::new(Type::String)),
        );
        // share_of: Mako-native alias for share_int (extensible later)
        fns.insert(
            "share_of".into(),
            Type::Fn(vec![Type::Int], Box::new(Type::Named("ShareInt".into()))),
        );
        Self {
            types: HashMap::new(),
            fns,
            variants: HashMap::new(),
            scopes: vec![HashMap::new()],
            current_ret: Type::Void,
            current_expected: None,
            interfaces: Vec::new(),
            moved_holds: HashMap::new(),
            hold_vars: HashMap::new(),
            hold_moved_fields: HashMap::new(),
            share_vars: HashMap::new(),
            shared_borrows: HashMap::new(),
            share_sources: HashMap::new(),
            share_scope_depth: HashMap::new(),
            fn_mut_params: HashMap::new(),
            const_ints: HashMap::new(),
            const_strs: HashMap::new(),
            const_fns: HashMap::new(),
            arena_depth: 0,
            arena_owned: HashSet::new(),
            binding_depth: HashMap::new(),
            loop_depth: 0,
            loop_labels: Vec::new(),
            loop_continue_moved: Vec::new(),
            loop_break_moved: Vec::new(),
            pending_defers: Vec::new(),
            unsafe_depth: 0,
            gc_requested: false,
            race_outstanding: HashSet::new(),
            race_stack: Vec::new(),
            race_sync_locals: HashSet::new(),
            fn_capture_scopes: vec![HashMap::new()],
            explicit_visibility: false,
            bounds_checks_always: false,
            generic_fns: HashMap::new(),
            generic_structs: HashMap::new(),
            mono_struct_generated: HashSet::new(),
            mono_structs: Vec::new(),
            generic_enums: HashMap::new(),
            mono_enum_generated: HashSet::new(),
            mono_enums: Vec::new(),
            deprecated_fns: HashMap::new(),
            active_type_params: HashSet::new(),
            mono_fns: Vec::new(),
            mono_generated: HashSet::new(),
            struct_field_defaults: HashMap::new(),
            lsp_bindings: Vec::new(),
            lsp_mode: false,
        }
    }

    fn push_loop(&mut self, label: Option<String>) {
        self.loop_depth += 1;
        self.loop_labels.push(label);
        self.loop_continue_moved
            .push((HashMap::new(), HashMap::new()));
        self.loop_break_moved.push((HashMap::new(), HashMap::new()));
    }

    fn pop_loop(&mut self) {
        self.loop_break_moved.pop();
        self.loop_continue_moved.pop();
        self.loop_labels.pop();
        self.loop_depth -= 1;
    }

    /// Resolve `break`/`continue` label: unlabeled → innermost; labeled → matching loop.
    fn resolve_loop_label(&self, label: &Option<String>) -> Result<(), TypeError> {
        if self.loop_depth == 0 {
            return Err(TypeError::new(if label.is_some() {
                "`break`/`continue` outside of loop"
            } else {
                "`break` outside of loop"
            }));
        }
        match label {
            None => Ok(()),
            Some(name) => {
                if self
                    .loop_labels
                    .iter()
                    .any(|l| l.as_deref() == Some(name.as_str()))
                {
                    Ok(())
                } else {
                    Err(TypeError::new(format!("unknown loop label `{name}`"))
                        .hint("label a loop with `name: while` / `name: for`"))
                }
            }
        }
    }

    /// Record current hold-move state onto a loop's continue accumulator.
    /// Unlabeled → innermost. Labeled → the matching outer loop (not only the
    /// innermost frame), so `continue outer` poisons the outer next-iter / exit.
    fn record_continue_path_moves(&mut self, label: &Option<String>) {
        if self.loop_continue_moved.is_empty() {
            return;
        }
        let target = match label {
            None => self.loop_continue_moved.len() - 1,
            Some(name) => match self
                .loop_labels
                .iter()
                .position(|l| l.as_deref() == Some(name.as_str()))
            {
                Some(i) => i,
                None => self.loop_continue_moved.len() - 1,
            },
        };
        let Some((acc_moved, acc_fields)) = self.loop_continue_moved.get_mut(target) else {
            return;
        };
        for (name, was) in &self.moved_holds {
            if *was {
                acc_moved.insert(name.clone(), true);
            }
        }
        for (name, fields) in &self.hold_moved_fields {
            if !fields.is_empty() {
                acc_fields
                    .entry(name.clone())
                    .or_default()
                    .extend(fields.iter().cloned());
            }
        }
    }

    /// Union continue-path moves into the live hold-move state (next iter / post-loop).
    fn apply_continue_path_moves(&mut self) {
        let Some((acc_moved, acc_fields)) = self.loop_continue_moved.last() else {
            return;
        };
        for (name, was) in acc_moved {
            if *was {
                self.moved_holds.insert(name.clone(), true);
            }
        }
        for (name, fields) in acc_fields {
            if !fields.is_empty() {
                self.hold_moved_fields
                    .entry(name.clone())
                    .or_default()
                    .extend(fields.iter().cloned());
            }
        }
        self.hold_moved_fields.retain(|_, u| !u.is_empty());
    }

    /// Record current hold-move state onto a loop's break accumulator.
    /// Unlabeled → innermost. Labeled → matching outer loop frame.
    fn record_break_path_moves(&mut self, label: &Option<String>) {
        if self.loop_break_moved.is_empty() {
            return;
        }
        let target = match label {
            None => self.loop_break_moved.len() - 1,
            Some(name) => match self
                .loop_labels
                .iter()
                .position(|l| l.as_deref() == Some(name.as_str()))
            {
                Some(i) => i,
                None => self.loop_break_moved.len() - 1,
            },
        };
        let Some((acc_moved, acc_fields)) = self.loop_break_moved.get_mut(target) else {
            return;
        };
        for (name, was) in &self.moved_holds {
            if *was {
                acc_moved.insert(name.clone(), true);
            }
        }
        for (name, fields) in &self.hold_moved_fields {
            if !fields.is_empty() {
                acc_fields
                    .entry(name.clone())
                    .or_default()
                    .extend(fields.iter().cloned());
            }
        }
    }

    /// Union break-path moves into the live hold-move state (post-loop only).
    fn apply_break_path_moves(&mut self) {
        let Some((acc_moved, acc_fields)) = self.loop_break_moved.last() else {
            return;
        };
        for (name, was) in acc_moved {
            if *was {
                self.moved_holds.insert(name.clone(), true);
            }
        }
        for (name, fields) in acc_fields {
            if !fields.is_empty() {
                self.hold_moved_fields
                    .entry(name.clone())
                    .or_default()
                    .extend(fields.iter().cloned());
            }
        }
        self.hold_moved_fields.retain(|_, u| !u.is_empty());
    }

    /// True if `stmt` never falls through to the next statement in a block.
    fn stmt_always_diverges(stmt: &Stmt) -> bool {
        match stmt {
            Stmt::Return(_) | Stmt::Break(_) | Stmt::Continue(_) => true,
            Stmt::If {
                init: _,
                cond,
                then_block,
                else_block,
            } => {
                // A constant-condition `if` only takes one arm — keeps the
                // `if init; cond` desugar's `if true { … }` scope transparent.
                match const_bool(cond) {
                    Some(true) => block_always_diverges(then_block),
                    Some(false) => else_block
                        .as_ref()
                        .map(block_always_diverges)
                        .unwrap_or(false),
                    None => {
                        let then_d = block_always_diverges(then_block);
                        match else_block {
                            Some(eb) => then_d && block_always_diverges(eb),
                            None => false,
                        }
                    }
                }
            }
            _ => false,
        }
    }

    /// LSP helper: run type checking and collect symbol types.
    /// Returns partial results even when the program has type errors.
    /// Returns (binding_names_with_types, all_symbol_types).
    pub fn check_for_lsp(&mut self, program: &Program) -> HashMap<String, String> {
        self.lsp_mode = true;
        let _ = self.check(program);
        self.lsp_mode = false;
        let mut symbols: HashMap<String, String> = HashMap::new();
        for (name, ty) in &self.fns {
            symbols.insert(name.clone(), ty.display());
        }
        for (name, ty) in &self.types {
            symbols.insert(name.clone(), ty.display());
        }
        symbols
    }

    pub fn check(&mut self, program: &Program) -> Result<(), TypeError> {
        if self.gc_requested {
            return Err(TypeError::new(
                "garbage collection was removed from Mako; `[package] gc = true` is unsupported",
            )
            .hint("use let/hold/share/arena for deterministic ownership instead"));
        }
        // First pass: collect type and function signatures
        for item in &program.items {
            match item {
                Item::Struct(s) => {
                    if !s.type_params.is_empty() {
                        // Generic struct template — monomorphized on use.
                        self.generic_structs.insert(s.name.clone(), s.clone());
                        continue;
                    }
                    let fields: Result<Vec<_>, _> = s
                        .fields
                        .iter()
                        .map(|(n, t, _)| Ok((n.clone(), self.resolve_type(t)?)))
                        .collect();
                    let fields = fields?;
                    // Register field defaults for struct-lit fill.
                    for (n, _, def) in &s.fields {
                        if let Some(d) = def {
                            self.struct_field_defaults
                                .entry(s.name.clone())
                                .or_default()
                                .insert(n.clone(), d.clone());
                        }
                    }
                    self.types.insert(
                        s.name.clone(),
                        Type::Struct {
                            name: s.name.clone(),
                            fields,
                        },
                    );
                }
                Item::Enum(e) => {
                    if !e.type_params.is_empty() {
                        // Generic enum template — monomorphized on use.
                        self.generic_enums.insert(e.name.clone(), e.clone());
                        continue;
                    }
                    let mut variants = Vec::new();
                    for v in &e.variants {
                        let fields: Result<Vec<_>, _> =
                            v.fields.iter().map(|t| self.resolve_type(t)).collect();
                        let fields = fields?;
                        self.variants.insert(
                            v.name.clone(),
                            VariantCtor {
                                enum_name: e.name.clone(),
                                fields: fields.clone(),
                            },
                        );
                        variants.push((v.name.clone(), fields));
                    }
                    self.types.insert(
                        e.name.clone(),
                        Type::Enum {
                            name: e.name.clone(),
                            variants,
                        },
                    );
                }
                Item::Fn(f) => {
                    if f.is_const {
                        self.const_fns.insert(f.name.clone(), f.clone());
                    }
                    if let crate::ast::ApiStability::Deprecated { message } = &f.stability {
                        self.deprecated_fns.insert(f.name.clone(), message.clone());
                    }
                    if !f.type_params.is_empty() {
                        // Generic template — monomorphized on call.
                        self.generic_fns.insert(f.name.clone(), f.clone());
                        self.fn_mut_params
                            .insert(f.name.clone(), f.params.iter().map(|p| p.mutable).collect());
                    } else {
                        let params: Result<Vec<_>, _> =
                            f.params.iter().map(|p| self.resolve_type(&p.ty)).collect();
                        let ret = f
                            .ret
                            .as_ref()
                            .map(|t| self.resolve_type(t))
                            .transpose()?
                            .unwrap_or(Type::Void);
                        self.fn_mut_params
                            .insert(f.name.clone(), f.params.iter().map(|p| p.mutable).collect());
                        self.fns
                            .insert(f.name.clone(), Type::Fn(params?, Box::new(ret)));
                    }
                }
                Item::Actor(_) | Item::On(_) | Item::Package { .. } => {
                    // Desugared / informational; ignore.
                }
                Item::Interface(iface) => {
                    // Validate method type expressions; presence checked after fns collected.
                    for (_mname, params, ret) in &iface.methods {
                        for t in params {
                            let _ = self.resolve_type(t)?;
                        }
                        let _ = self.resolve_type(ret)?;
                    }
                    self.interfaces.push(iface.clone());
                }
                Item::ExternC(ext) => {
                    let params: Result<Vec<_>, _> = ext
                        .params
                        .iter()
                        .map(|p| self.resolve_type(&p.ty))
                        .collect();
                    let ret = ext
                        .ret
                        .as_ref()
                        .map(|t| self.resolve_type(t))
                        .transpose()?
                        .unwrap_or(Type::Void);
                    self.fns
                        .insert(ext.name.clone(), Type::Fn(params?, Box::new(ret)));
                }
                Item::Import { .. } => {}
                Item::Const(c) => {
                    if let Ok(v) = fold_const_expr_with(
                        &c.value,
                        &self.const_ints,
                        &self.const_strs,
                        &self.const_fns,
                    ) {
                        self.const_ints.insert(c.name.clone(), v);
                        self.define(&c.name, Type::Int, false);
                    } else if let Ok(s) = fold_const_str_with(
                        &c.value,
                        &self.const_ints,
                        &self.const_strs,
                        &self.const_fns,
                    ) {
                        self.const_strs.insert(c.name.clone(), s);
                        self.define(&c.name, Type::String, false);
                    } else {
                        return Err(TypeError::new(format!(
                            "const `{}` initializer is not a compile-time int or string",
                            c.name
                        ))
                        .hint(
                            "use integer ops / const fn, or string literals / `+` concat / const string names",
                        ));
                    }
                }
            }
        }

        // Instantiate any generic enums referenced in function signatures.
        // Must happen before body checking so variant constructors are available.
        let fn_ret_names: Vec<String> = self
            .fns
            .values()
            .filter_map(|t| {
                if let Type::Fn(_, ret) = t {
                    if let Type::Named(n) = ret.as_ref() {
                        if n.contains("__") {
                            return Some(n.clone());
                        }
                    }
                }
                None
            })
            .collect();
        for mono_name in fn_ret_names {
            let _ = self.try_instantiate_generic_enum(&mono_name);
        }

        // Second pass: check bodies (skip generic templates — specialized on call)
        for item in &program.items {
            if let Item::Fn(f) = item {
                if f.type_params.is_empty() {
                    self.check_fn(f)?;
                    if f.is_const {
                        // Ensure body is const-evaluable with dummy zeros / empty strings.
                        let mut env = self.const_ints.clone();
                        let mut str_env = self.const_strs.clone();
                        for p in &f.params {
                            let pty = self.resolve_type(&p.ty)?;
                            if pty == Type::String {
                                str_env.insert(p.name.clone(), String::new());
                            } else {
                                env.insert(p.name.clone(), 0);
                            }
                        }
                        let ret_is_str = f
                            .ret
                            .as_ref()
                            .map(|t| self.resolve_type(t).ok() == Some(Type::String))
                            .unwrap_or(false);
                        let fold_err = if ret_is_str {
                            eval_const_fn_body_str(&f.body, &env, &str_env, &self.const_fns).err()
                        } else {
                            eval_const_fn_body(&f.body, &env, &str_env, &self.const_fns).err()
                        };
                        if let Some(e) = fold_err {
                            return Err(TypeError::new(format!(
                                "const fn `{}` body is not compile-time foldable: {}",
                                f.name,
                                e.message()
                            )));
                        }
                    }
                }
            }
        }

        // Collect interface names used as generic bounds — these don't need
        // standalone implementations (they're verified at call sites).
        let bound_ifaces: HashSet<String> = self
            .generic_fns
            .values()
            .flat_map(|f| f.type_bounds.values().cloned())
            .collect();
        // Interface method presence: `Iface_method`, `Iface_Concrete_method`,
        // or Go-style `Concrete_method` method set.
        for iface in &self.interfaces.clone() {
            if bound_ifaces.contains(&iface.name) {
                continue; // bound-only interfaces are checked at generic call sites
            }
            for (mname, params, ret) in &iface.methods {
                let expected_params: Result<Vec<_>, _> =
                    params.iter().map(|t| self.resolve_type(t)).collect();
                let expected_params = expected_params?;
                let expected_ret = self.resolve_type(ret)?;
                let prefix = format!("{}_", iface.name);
                let suffix = format!("_{mname}");
                let mut candidates: Vec<String> = Vec::new();
                let direct = format!("{}_{}", iface.name, mname);
                if self.fns.contains_key(&direct) {
                    candidates.push(direct);
                }
                for fname in self.fns.keys() {
                    if let Some(rest) = fname.strip_prefix(&prefix) {
                        if let Some(cn) = rest.strip_suffix(&suffix) {
                            if !cn.is_empty() && !cn.contains('_') {
                                candidates.push(fname.clone());
                            }
                        }
                    }
                }
                // Go method set: `Struct_method` with self receiver.
                for (sname, sty) in &self.types {
                    if !matches!(sty, Type::Struct { .. }) {
                        continue;
                    }
                    let free = format!("{sname}_{mname}");
                    if candidates.iter().any(|c| c == &free) {
                        continue;
                    }
                    if let Some(Type::Fn(fp, _)) = self.fns.get(&free) {
                        if fp.len() == expected_params.len() + 1
                            && self.types_equal_iface_self(&fp[0], &Type::Named(sname.clone()))
                        {
                            candidates.push(free);
                        }
                    }
                }
                if candidates.is_empty() {
                    return Err(TypeError::new(format!(
                        "interface {}: missing implementation for `{mname}`",
                        iface.name
                    ))
                    .hint(format!(
                        "define `on SomeType {{ fn {mname}… }}`, `fn SomeType_{mname}…`, \
                         `fn {}_{mname}…`, or `fn {}_{{Concrete}}_{mname}…`",
                        iface.name, iface.name
                    )));
                }
                for key in candidates {
                    let Some(Type::Fn(p, r)) = self.fns.get(&key) else {
                        continue;
                    };
                    let (impl_args, _has_self) = if p.len() == expected_params.len() + 1 {
                        (&p[1..], true)
                    } else {
                        (p.as_slice(), false)
                    };
                    if impl_args.len() != expected_params.len() {
                        return Err(TypeError::new(format!(
                            "interface {}: method `{mname}` expects {} params (found fn `{key}` with {})",
                            iface.name,
                            expected_params.len(),
                            impl_args.len()
                        )));
                    }
                    for (i, (got, exp)) in impl_args.iter().zip(expected_params.iter()).enumerate()
                    {
                        if !self.compatible(got, exp) {
                            return Err(TypeError::new(format!(
                                "interface {}: method `{mname}` param {i} type mismatch on `{key}`: expected {}, got {}",
                                iface.name,
                                exp.display(),
                                got.display()
                            )));
                        }
                    }
                    if !self.compatible(r, &expected_ret) {
                        return Err(TypeError::new(format!(
                            "interface {}: method `{mname}` return type mismatch on `{key}`: expected {}, got {}",
                            iface.name,
                            expected_ret.display(),
                            r.display()
                        )));
                    }
                }
            }
        }
        Ok(())
    }

    fn check_map_kv(k: &Type, v: &Type) -> Result<(), TypeError> {
        match (k, v) {
            (Type::String, Type::Int)
            | (Type::Int, Type::Int)
            | (Type::String, Type::String)
            | (Type::Int, Type::Float)
            | (Type::String, Type::Float)
            | (Type::Float, Type::Int)
            | (Type::Float, Type::String)
            | (Type::Float, Type::Float)
            // bool values: map[int|string|float|bool|Struct]bool
            | (Type::Int, Type::Bool)
            | (Type::String, Type::Bool)
            | (Type::Float, Type::Bool)
            | (Type::Bool, Type::Bool)
            // bool keys: map[bool]int|string|float|bool|Struct
            | (Type::Bool, Type::Int)
            | (Type::Bool, Type::String)
            | (Type::Bool, Type::Float) => Ok(()),
            // Named user structs / enums as values and/or keys (scalar or each other).
            (Type::Int | Type::String | Type::Float | Type::Bool, Type::Struct { .. }) => Ok(()),
            (Type::Int | Type::String | Type::Float | Type::Bool, Type::Enum { .. }) => Ok(()),
            (Type::Struct { .. }, Type::Int | Type::String | Type::Float | Type::Bool) => Ok(()),
            (Type::Enum { .. }, Type::Int | Type::String | Type::Float | Type::Bool) => Ok(()),
            (Type::Struct { .. }, Type::Struct { .. }) => Ok(()),
            (Type::Struct { .. }, Type::Enum { .. }) => Ok(()),
            (Type::Enum { .. }, Type::Struct { .. }) => Ok(()),
            (Type::Enum { .. }, Type::Enum { .. }) => Ok(()),
            // Slice values: map[K][]T and map[K][][]T for supported element types;
            // also map[K][]Option[T] / map[K][]Result[T,E] (bag element slices).
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Array(inner),
            ) if matches!(
                inner.as_ref(),
                Type::Int
                    | Type::Int64
                    | Type::Int32
                    | Type::Int8
                    | Type::Byte
                    | Type::String
                    | Type::Float
                    | Type::Bool
                    | Type::Struct { .. }
                    | Type::Enum { .. }
            ) || matches!(
                inner.as_ref(),
                Type::Array(elem) if matches!(
                    elem.as_ref(),
                    Type::Int
                        | Type::Int64
                        | Type::Int32
                        | Type::Int8
                        | Type::Byte
                        | Type::String
                        | Type::Float
                        | Type::Bool
                        | Type::Struct { .. }
                        | Type::Enum { .. }
                ) || matches!(
                    // map[K][][]chan[T]
                    elem.as_ref(),
                    Type::Chan(ch)
                        if matches!(
                            ch.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::Bool
                                | Type::Float
                                | Type::String
                                | Type::Struct { .. }
                        )
                )
            ) || matches!(
                inner.as_ref(),
                Type::Option(payload)
                    if matches!(
                        payload.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        // map[K][]Option[chan[T]]
                        payload.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    ) || matches!(
                        // map[K][]Option[Option[T]] / []Option[Option[chan]]
                        payload.as_ref(),
                        Type::Option(inner2)
                            if matches!(
                                inner2.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner2.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    ) || matches!(
                        // map[K][]Option[Result[T,E]] / []Option[Result[chan]]
                        payload.as_ref(),
                        Type::Result(inner2, _)
                            if matches!(
                                inner2.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner2.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    )
            ) || matches!(
                inner.as_ref(),
                Type::Result(payload, _)
                    if matches!(
                        payload.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        // map[K][]Result[chan[T],E]
                        payload.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    ) || matches!(
                        // map[K][]Result[Option[T],E] / []Result[Option[chan]]
                        payload.as_ref(),
                        Type::Option(inner2)
                            if matches!(
                                inner2.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner2.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    ) || matches!(
                        // map[K][]Result[Result[T,E2],E]
                        payload.as_ref(),
                        Type::Result(inner2, _)
                            if matches!(
                                inner2.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner2.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    )
            ) || matches!(
                // map[K][]chan[T] — same channel element set as chan_open / map[K]chan[T]
                inner.as_ref(),
                Type::Chan(payload)
                    if matches!(
                        payload.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::Bool
                            | Type::Float
                            | Type::String
                            | Type::Struct { .. }
                    )
            ) =>
            {
                Ok(())
            }
            // Nested maps: map[K]map[K2]V (depth 2) or map[K]map[K2]map[K3]V (depth 3).
            // Depth 4+ rejected (leaf of the mid map must not itself be a map).
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Map(_, mid),
            ) if match mid.as_ref() {
                Type::Map(_, leaf) => !matches!(leaf.as_ref(), Type::Map(_, _)),
                _ => true,
            } =>
            {
                Ok(())
            }
            // Slice of maps: map[K][]map[K2]V
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Array(inner),
            ) if matches!(inner.as_ref(), Type::Map(_, _)) => Ok(()),
            // map[K]Option[T] / map[K]Result[T, E] for scalar / struct / enum / []T / map payloads
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Option(inner),
            ) if matches!(
                inner.as_ref(),
                Type::Int
                    | Type::Int64
                    | Type::Int32
                    | Type::Int8
                    | Type::Byte
                    | Type::String
                    | Type::Float
                    | Type::Bool
                    | Type::Struct { .. }
                    | Type::Enum { .. }
            ) || matches!(
                inner.as_ref(),
                Type::Array(elem) if matches!(
                    elem.as_ref(),
                    Type::Int
                        | Type::Int64
                        | Type::Int32
                        | Type::Int8
                        | Type::Byte
                        | Type::String
                        | Type::Float
                        | Type::Bool
                        | Type::Struct { .. }
                        | Type::Enum { .. }
                ) || matches!(
                    // Option[[]Option[T]] / Option[[]Result[T,E]]
                    elem.as_ref(),
                    Type::Option(p)
                        if matches!(
                            p.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::String
                                | Type::Float
                                | Type::Bool
                                | Type::Struct { .. }
                                | Type::Enum { .. }
                        ) || matches!(
                            p.as_ref(),
                            Type::Chan(ch)
                                if matches!(
                                    ch.as_ref(),
                                    Type::Int
                                        | Type::Int64
                                        | Type::Int32
                                        | Type::Int8
                                        | Type::Byte
                                        | Type::Bool
                                        | Type::Float
                                        | Type::String
                                        | Type::Struct { .. }
                                )
                        )
                ) || matches!(
                    elem.as_ref(),
                    Type::Result(p, _)
                        if matches!(
                            p.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::String
                                | Type::Float
                                | Type::Bool
                                | Type::Struct { .. }
                                | Type::Enum { .. }
                        ) || matches!(
                            p.as_ref(),
                            Type::Chan(ch)
                                if matches!(
                                    ch.as_ref(),
                                    Type::Int
                                        | Type::Int64
                                        | Type::Int32
                                        | Type::Int8
                                        | Type::Byte
                                        | Type::Bool
                                        | Type::Float
                                        | Type::String
                                        | Type::Struct { .. }
                                )
                        )
                )
            ) || matches!(
                // Option[map[K2]V] — depth-2 maps only (not map-of-map)
                inner.as_ref(),
                Type::Map(_, v) if !matches!(v.as_ref(), Type::Map(_, _))
            ) || matches!(
                // Option[chan[T]] — same element set as chan_open
                inner.as_ref(),
                Type::Chan(payload)
                    if matches!(
                        payload.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::Bool
                            | Type::Float
                            | Type::String
                            | Type::Struct { .. }
                    )
            ) || matches!(
                // Option[[]chan[T]]
                inner.as_ref(),
                Type::Array(elem)
                    if matches!(
                        elem.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    )
            ) || matches!(
                // Option[Option[T]] / Option[Option[chan[T]]] / Option[Option[Option[…]]]
                inner.as_ref(),
                Type::Option(inner2)
                    if matches!(
                        inner2.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    ) || matches!(
                        // Option[Option[Option[T]]] / Option[Option[Option[chan[T]]]]
                        inner2.as_ref(),
                        Type::Option(inner3)
                            if matches!(
                                inner3.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner3.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    )
            ) || matches!(
                // Option[Result[T,E]] / Option[Result[chan[T],E]] /
                // Option[Result[Option[T],E]]
                inner.as_ref(),
                Type::Result(inner2, _)
                    if matches!(
                        inner2.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Option(inner3)
                            if matches!(
                                inner3.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner3.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    )
            ) =>
            {
                Ok(())
            }
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Result(inner, _),
            ) if matches!(
                inner.as_ref(),
                Type::Int
                    | Type::Int64
                    | Type::Int32
                    | Type::Int8
                    | Type::Byte
                    | Type::String
                    | Type::Float
                    | Type::Bool
                    | Type::Struct { .. }
                    | Type::Enum { .. }
            ) || matches!(
                inner.as_ref(),
                Type::Array(elem) if matches!(
                    elem.as_ref(),
                    Type::Int
                        | Type::Int64
                        | Type::Int32
                        | Type::Int8
                        | Type::Byte
                        | Type::String
                        | Type::Float
                        | Type::Bool
                        | Type::Struct { .. }
                        | Type::Enum { .. }
                ) || matches!(
                    elem.as_ref(),
                    Type::Chan(ch)
                        if matches!(
                            ch.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::Bool
                                | Type::Float
                                | Type::String
                                | Type::Struct { .. }
                        )
                ) || matches!(
                    // Result[[]Option[T],E] / Result[[]Result[T,E2],E]
                    elem.as_ref(),
                    Type::Option(p)
                        if matches!(
                            p.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::String
                                | Type::Float
                                | Type::Bool
                                | Type::Struct { .. }
                                | Type::Enum { .. }
                        ) || matches!(
                            p.as_ref(),
                            Type::Chan(ch)
                                if matches!(
                                    ch.as_ref(),
                                    Type::Int
                                        | Type::Int64
                                        | Type::Int32
                                        | Type::Int8
                                        | Type::Byte
                                        | Type::Bool
                                        | Type::Float
                                        | Type::String
                                        | Type::Struct { .. }
                                )
                        )
                ) || matches!(
                    elem.as_ref(),
                    Type::Result(p, _)
                        if matches!(
                            p.as_ref(),
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::Byte
                                | Type::String
                                | Type::Float
                                | Type::Bool
                                | Type::Struct { .. }
                                | Type::Enum { .. }
                        ) || matches!(
                            p.as_ref(),
                            Type::Chan(ch)
                                if matches!(
                                    ch.as_ref(),
                                    Type::Int
                                        | Type::Int64
                                        | Type::Int32
                                        | Type::Int8
                                        | Type::Byte
                                        | Type::Bool
                                        | Type::Float
                                        | Type::String
                                        | Type::Struct { .. }
                                )
                        )
                )
            ) || matches!(
                inner.as_ref(),
                Type::Map(_, v) if !matches!(v.as_ref(), Type::Map(_, _))
            ) || matches!(
                inner.as_ref(),
                Type::Chan(payload)
                    if matches!(
                        payload.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::Bool
                            | Type::Float
                            | Type::String
                            | Type::Struct { .. }
                    )
            ) || matches!(
                // Result[Result[T,E2],E] / Result[Result[chan[T],E2],E]
                inner.as_ref(),
                Type::Result(inner2, _)
                    if matches!(
                        inner2.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    )
            ) || matches!(
                // Result[Option[T],E] / Result[Option[chan[T]],E] /
                // Result[Option[Option[T]],E]
                inner.as_ref(),
                Type::Option(inner2)
                    if matches!(
                        inner2.as_ref(),
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Option(inner3)
                            if matches!(
                                inner3.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                inner3.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    ) || matches!(
                        inner2.as_ref(),
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    )
            ) =>
            {
                Ok(())
            }
            // map[K](T, U[, …]) — tuple values (arity 2–4).
            // Leaf elements: scalar / struct / enum, channel handles, or bag
            // Option[T] / Result[T,E] (scalar / struct / enum / chan payloads).
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Tuple(elems),
            ) if (2..=4).contains(&elems.len())
                && elems.iter().all(|e| {
                    matches!(
                        e,
                        Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                    ) || matches!(
                        e,
                        Type::Chan(ch)
                            if matches!(
                                ch.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                                    | Type::Float
                                    | Type::String
                                    | Type::Struct { .. }
                            )
                    ) || matches!(
                        e,
                        Type::Option(payload)
                            if matches!(
                                payload.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                payload.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    ) || matches!(
                        e,
                        Type::Result(payload, _)
                            if matches!(
                                payload.as_ref(),
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::String
                                    | Type::Float
                                    | Type::Bool
                                    | Type::Struct { .. }
                                    | Type::Enum { .. }
                            ) || matches!(
                                payload.as_ref(),
                                Type::Chan(ch)
                                    if matches!(
                                        ch.as_ref(),
                                        Type::Int
                                            | Type::Int64
                                            | Type::Int32
                                            | Type::Int8
                                            | Type::Byte
                                            | Type::Bool
                                            | Type::Float
                                            | Type::String
                                            | Type::Struct { .. }
                                    )
                            )
                    )
                }) =>
            {
                Ok(())
            }
            // map[K]chan[T] — channel pointers as values (same element set as chan_open).
            (
                Type::Int | Type::String | Type::Float | Type::Bool | Type::Struct { .. } | Type::Enum { .. },
                Type::Chan(inner),
            ) if matches!(
                inner.as_ref(),
                Type::Int
                    | Type::Int64
                    | Type::Int32
                    | Type::Int8
                    | Type::Byte
                    | Type::Bool
                    | Type::Float
                    | Type::String
                    | Type::Struct { .. }
            ) =>
            {
                Ok(())
            }
            _ => Err(TypeError::new(format!(
                "unsupported map[{}]{} — keys: int|string|float|bool|Struct|Enum; \
                 values: int|string|float|bool|Struct|Enum|[]T|[][]T|[]Option|[]Result|[]Option[Option]|[]Option[Result]|[]Result[Option]|[]Result[Result]|[]Option[chan]|[]Result[chan]|[]chan|[][]chan|[]map|map[K2]V|map[K2]map[K3]V|Option[T]|Option[[]T]|Option[[]Option]|Option[[]Result]|Option[[]chan]|Option[map]|Option[chan]|Option[Option[…]]|Option[Result[…]]|Result[T,E]|Result[[]T,E]|Result[[]Option]|Result[[]Result]|Result[[]chan]|Result[map]|Result[chan]|Result[Option[…]]|Result[Result[…]]|(T,U)| (Option|Result|chan fields)|chan[T]",
                k.display(),
                v.display()
            ))),
        }
    }

    fn resolve_type(&self, t: &TypeExpr) -> Result<Type, TypeError> {
        self.resolve_type_with(t, &self.active_type_params)
    }

    fn resolve_type_with(
        &self,
        t: &TypeExpr,
        type_params: &HashSet<String>,
    ) -> Result<Type, TypeError> {
        match t {
            TypeExpr::Tuple(elems) => {
                let mut out = Vec::new();
                for e in elems {
                    out.push(self.resolve_type_with(e, type_params)?);
                }
                return Ok(Type::Tuple(out));
            }
            TypeExpr::Named(n) if type_params.contains(n) => {
                return Ok(Type::Named(n.clone()));
            }
            _ => {}
        }
        self.resolve_type_inner(t)
    }

    fn resolve_type_inner(&self, t: &TypeExpr) -> Result<Type, TypeError> {
        match t {
            TypeExpr::Named(n) => match n.as_str() {
                "int" => Ok(Type::Int),
                "StrBuilder" => Ok(Type::StrBuilder),
                "Mutex" => Ok(Type::Mutex),
                "RWMutex" => Ok(Type::RWMutex),
                "CMap" => Ok(Type::CMap),
                "MMap" => Ok(Type::MMap),
                "EvLoop" => Ok(Type::EvLoop),
                "Buf" => Ok(Type::Buf),
                "GameUDP" => Ok(Type::GameUDP),
                "CHash" => Ok(Type::CHash),
                "RateLimiter" => Ok(Type::RateLimiter),
                "CircuitBreaker" => Ok(Type::CircuitBreaker),
                "HttpEngine" => Ok(Type::HttpEngine),
                "BufReader" => Ok(Type::BufReader),
                "BufWriter" => Ok(Type::BufWriter),
                "HttpRequest" => Ok(Type::HttpRequest),
                "HttpForwardResult" => Ok(Type::Named("HttpForwardResult".into())),
                "ProxyIoResult" => Ok(Type::Named("ProxyIoResult".into())),
                "HttpParsed" => Ok(Type::Named("HttpParsed".into())),
                "SqlDB" => Ok(Type::SqlDB),
                "WaitGroup" => Ok(Type::WaitGroup),
                "AtomicInt" => Ok(Type::Named("AtomicInt".into())),
                "BytesBuffer" => Ok(Type::Named("BytesBuffer".into())),
                "ZipWriter" => Ok(Type::Named("ZipWriter".into())),
                "ReflectValue" => Ok(Type::Named("ReflectValue".into())),
                "Uuid" | "uuid" => Ok(Type::Uuid),
                "int64" => Ok(Type::Int64),
                "int32" => Ok(Type::Int32),
                "int8" => Ok(Type::Int8),
                "uint64" => Ok(Type::UInt64),
                "byte" => Ok(Type::Byte),
                "float" | "float64" => Ok(Type::Float),
                "bool" => Ok(Type::Bool),
                "string" => Ok(Type::String),
                "string_view" => Ok(Type::Named("string_view".into())),
                "void" => Ok(Type::Void),
                "Arena" => Ok(Type::Arena),
                "Crew" => Ok(Type::Crew),
                "Graphql" | "GraphQL" => Ok(Type::Graphql),
                "ShareInt" => Ok(Type::Named("ShareInt".into())),
                "Slice" => Ok(Type::Named("Slice".into())),
                "PgConn" => Ok(Type::Named("PgConn".into())),
                "Secret" => Ok(Type::Named("Secret".into())),
                "MysqlConn" => Ok(Type::Named("MysqlConn".into())),
                "RedisConn" => Ok(Type::Named("RedisConn".into())),
                other => {
                    if let Some(ty) = self.types.get(other) {
                        Ok(ty.clone())
                    } else if self.interfaces.iter().any(|i| i.name == other) {
                        Ok(Type::Interface {
                            name: other.to_string(),
                        })
                    } else {
                        Ok(Type::Named(other.to_string()))
                    }
                }
            },
            TypeExpr::Generic(name, args) => match name.as_str() {
                "Option" => {
                    if args.len() != 1 {
                        return Err(TypeError::new("Option needs 1 type arg"));
                    }
                    Ok(Type::Option(Box::new(self.resolve_type(&args[0])?)))
                }
                "Result" => {
                    if args.len() != 2 {
                        return Err(TypeError::new("Result needs 2 type args"));
                    }
                    Ok(Type::Result(
                        Box::new(self.resolve_type(&args[0])?),
                        Box::new(self.resolve_type(&args[1])?),
                    ))
                }
                "Job" => {
                    if args.len() != 1 {
                        return Err(TypeError::new("Job needs 1 type arg"));
                    }
                    Ok(Type::Job(Box::new(self.resolve_type(&args[0])?)))
                }
                "chan" => {
                    if args.len() != 1 {
                        return Err(TypeError::new("chan needs 1 type arg"));
                    }
                    Ok(Type::Chan(Box::new(self.resolve_type(&args[0])?)))
                }
                "queue" => {
                    if args.len() != 1 {
                        return Err(TypeError::new("queue needs 1 type arg"));
                    }
                    let elem = self.resolve_type(&args[0])?;
                    // Seed: string payloads (mq_*). Expand monomorphs later.
                    if !matches!(elem, Type::String) {
                        return Err(TypeError::new(format!(
                            "queue[T] currently supports string (got {})",
                            elem.display()
                        ))
                        .hint("use queue[string]; more element types planned"));
                    }
                    Ok(Type::Queue(Box::new(elem)))
                }
                "List" => {
                    if args.len() != 1 {
                        return Err(TypeError::new("List needs 1 type arg"));
                    }
                    // List[T] / List<T> aliases []T (rich helpers in std/collections)
                    Ok(Type::Array(Box::new(self.resolve_type(&args[0])?)))
                }
                "Map" | "map" => {
                    if args.len() != 2 {
                        return Err(TypeError::new("map needs 2 type args"));
                    }
                    let k = self.resolve_type(&args[0])?;
                    let v = self.resolve_type(&args[1])?;
                    Self::check_map_kv(&k, &v)?;
                    Ok(Type::Map(Box::new(k), Box::new(v)))
                }
                other => {
                    let resolved_args: Result<Vec<_>, _> =
                        args.iter().map(|a| self.resolve_type(a)).collect();
                    let resolved_args = resolved_args?;
                    // Check if this is a generic struct or enum instantiation.
                    if self.generic_structs.contains_key(other) {
                        let tags: Vec<String> =
                            resolved_args.iter().map(|t| t.mono_tag()).collect();
                        let mono_name = format!("{other}__{}", tags.join("__"));
                        return Ok(Type::Named(mono_name));
                    }
                    if self.generic_enums.contains_key(other) {
                        let tags: Vec<String> =
                            resolved_args.iter().map(|t| t.mono_tag()).collect();
                        let mono_name = format!("{other}__{}", tags.join("__"));
                        return Ok(Type::Named(mono_name));
                    }
                    Ok(Type::Named(other.to_string()))
                }
            },
            TypeExpr::Map(k, v) => {
                let k = self.resolve_type(k)?;
                let v = self.resolve_type(v)?;
                Self::check_map_kv(&k, &v)?;
                Ok(Type::Map(Box::new(k), Box::new(v)))
            }
            TypeExpr::Array(inner) => Ok(Type::Array(Box::new(self.resolve_type(inner)?))),
            TypeExpr::Fn(params, ret) => {
                let params: Result<Vec<_>, _> =
                    params.iter().map(|p| self.resolve_type(p)).collect();
                Ok(Type::Fn(params?, Box::new(self.resolve_type(ret)?)))
            }
            TypeExpr::Tuple(elems) => {
                // Handled in resolve_type_with; keep arm for exhaustiveness.
                let mut out = Vec::new();
                for e in elems {
                    out.push(self.resolve_type(e)?);
                }
                Ok(Type::Tuple(out))
            }
        }
    }

    fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
        self.fn_capture_scopes.push(HashMap::new());
    }

    fn pop_scope(&mut self) {
        let depth = self.scopes.len();
        // NLL seed: shares introduced in this scope end when the scope ends.
        let ending: Vec<String> = self
            .share_scope_depth
            .iter()
            .filter(|(_, d)| **d == depth)
            .map(|(n, _)| n.clone())
            .collect();
        for name in ending {
            self.share_scope_depth.remove(&name);
            self.share_vars.remove(&name);
            if let Some(src) = self.share_sources.remove(&name) {
                // Drop borrow of source if no other share still points at it.
                let still = self.share_sources.values().any(|s| s == &src);
                if !still {
                    self.shared_borrows.remove(&src);
                }
            }
        }
        // Drop binding depths for names that lived only in this scope.
        if let Some(scope) = self.scopes.last() {
            for name in scope.keys() {
                self.binding_depth.remove(name);
            }
        }
        self.scopes.pop();
        self.fn_capture_scopes.pop();
    }

    fn define(&mut self, name: &str, ty: Type, mutable: bool) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), (ty, mutable));
        }
        self.binding_depth
            .insert(name.to_string(), self.scopes.len());
    }

    fn set_fn_capture_info(&mut self, name: &str, info: FnCaptureInfo) {
        for scope in self.fn_capture_scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), info);
                return;
            }
        }
        if let Some(scope) = self.fn_capture_scopes.last_mut() {
            scope.insert(name.to_string(), info);
        }
    }

    fn clear_fn_capture_info(&mut self, name: &str) {
        for scope in self.fn_capture_scopes.iter_mut().rev() {
            if scope.remove(name).is_some() {
                return;
            }
        }
    }

    fn fn_capture_info(&self, name: &str) -> Option<&FnCaptureInfo> {
        self.fn_capture_scopes
            .iter()
            .rev()
            .find_map(|scope| scope.get(name))
    }

    fn lookup(&self, name: &str) -> Option<&(Type, bool)> {
        for scope in self.scopes.iter().rev() {
            if let Some(v) = scope.get(name) {
                return Some(v);
            }
        }
        None
    }

    /// Pack-qualified enum variant: after `pull`, enum types are `eng__Color` and
    /// variants stay bare (`Red`, `Green`). `eng.Green` / `eng.Color.Red` only
    /// apply when `eng` is not a value binding and the variant belongs to an
    /// enum whose mangled name starts with `eng__`.
    fn pack_variant_ctor(&self, alias: &str, variant: &str) -> Option<&VariantCtor> {
        if self.lookup(alias).is_some() {
            return None;
        }
        let ctor = self.variants.get(variant)?;
        let prefix = format!("{alias}__");
        if ctor.enum_name.starts_with(&prefix) {
            Some(ctor)
        } else {
            None
        }
    }

    /// `eng.Color` type path → mangled enum name when `eng` is a pack alias.
    fn pack_enum_type_name(&self, alias: &str, ty_name: &str) -> Option<String> {
        if self.lookup(alias).is_some() {
            return None;
        }
        let mangled = format!("{alias}__{ty_name}");
        match self.types.get(&mangled) {
            Some(Type::Enum { .. }) => Some(mangled),
            _ => None,
        }
    }

    fn check_fn(&mut self, f: &FnDef) -> Result<(), TypeError> {
        let Some(fn_ty) = self.fns.get(&f.name).cloned() else {
            return Err(TypeError::new(format!(
                "internal typecheck error: function `{}` was not registered",
                f.name
            )));
        };
        let Type::Fn(params, ret) = fn_ty else {
            return Err(TypeError::new(format!(
                "internal typecheck error: `{}` is not a function",
                f.name
            )));
        };
        self.current_ret = *ret;
        self.pending_defers.clear();
        self.push_scope();
        for (p, ty) in f.params.iter().zip(params) {
            self.define(&p.name, ty, p.mutable);
        }
        let stmts = &f.body.stmts;
        let ret_void = self.current_ret == Type::Void;
        // A non-void function is satisfied by a top-level `return`, a trailing
        // expression, or a body that provably returns on every path (e.g. an
        // `if cond { return a } else { return b }` with no fall-through) — the
        // last matching Go's rule and making `if init; cond { … }` bodies usable.
        let has_explicit_return =
            stmts.iter().any(|s| matches!(s, Stmt::Return(_))) || nll::stmts_always_diverges(stmts);
        let trailing =
            !ret_void && !has_explicit_return && matches!(stmts.last(), Some(Stmt::Expr(_)));

        let check_n = if trailing {
            stmts.len().saturating_sub(1)
        } else {
            stmts.len()
        };
        for (i, stmt) in stmts[..check_n].iter().enumerate() {
            self.check_stmt(stmt)?;
            // Mid-scope NLL: end shares unused in remaining stmts (incl. trailing expr).
            self.end_unused_shares_after(stmts, i + 1);
        }
        if trailing {
            if let Some(Stmt::Expr(e)) = stmts.last() {
                let got = self.check_expr(e)?;
                if !self.compatible(&got, &self.current_ret) {
                    return Err(TypeError::new(format!(
                        "return type mismatch: expected {}, got {}",
                        self.current_ret.display(),
                        got.display()
                    ))
                    .hint("trailing expression is the function return value"));
                }
            }
        } else if !ret_void && !has_explicit_return {
            return Err(TypeError::new(format!(
                "function `{}` must return {} (missing `return` or trailing expression)",
                f.name,
                self.current_ret.display()
            )));
        }
        // NLL: defer bodies run at function exit — check against final move state.
        let defers = std::mem::take(&mut self.pending_defers);
        for body in defers.iter().rev() {
            self.check_block(body)?;
        }
        self.pop_scope();
        Ok(())
    }

    fn check_block(&mut self, block: &Block) -> Result<(), TypeError> {
        self.push_scope();
        let stmts = &block.stmts;
        for (i, stmt) in stmts.iter().enumerate() {
            self.check_stmt(stmt)?;
            // Mid-scope NLL seed (straight-line): end share borrows whose share
            // binding and source are not mentioned in any later statement.
            self.end_unused_shares_after(stmts, i + 1);
            // `break` / `continue` / `return` (and if both arms diverge): no fall-through.
            if Self::stmt_always_diverges(stmt) {
                break;
            }
        }
        self.pop_scope();
        Ok(())
    }

    /// Check a block and return the type of its trailing expression (the block's
    /// value), or `Void` if it does not end in an expression. Used by if-expressions.
    fn check_block_value(&mut self, block: &Block) -> Result<Type, TypeError> {
        self.push_scope();
        let stmts = &block.stmts;
        let n = stmts.len();
        let mut value = Type::Void;
        for (i, stmt) in stmts.iter().enumerate() {
            if i + 1 == n {
                match stmt {
                    Stmt::Expr(e) => {
                        value = self.check_expr(e)?;
                        break;
                    }
                    // A trailing `if … else …` statement yields the block value.
                    Stmt::If {
                        init: None,
                        cond,
                        then_block,
                        else_block: Some(eb),
                    } => {
                        value = self.check_expr(&Expr::IfExpr {
                            cond: Box::new(cond.clone()),
                            then_block: then_block.clone(),
                            else_block: eb.clone(),
                        })?;
                        break;
                    }
                    _ => {}
                }
            }
            self.check_stmt(stmt)?;
            if Self::stmt_always_diverges(stmt) {
                break;
            }
        }
        self.pop_scope();
        Ok(value)
    }

    /// Check loop body stmts with the same diverge / mid-scope share rules as `check_block`,
    /// but without an extra scope (for-loop binders live in the caller scope).
    fn check_loop_body_stmts(&mut self, stmts: &[Stmt]) -> Result<(), TypeError> {
        for (i, stmt) in stmts.iter().enumerate() {
            self.check_stmt(stmt)?;
            self.end_unused_shares_after(stmts, i + 1);
            if Self::stmt_always_diverges(stmt) {
                break;
            }
        }
        Ok(())
    }

    /// Drop live shares that are not referenced in `stmts[from..]`.
    /// Only the share binding name keeps the borrow alive — mentions of the
    /// source (including assigns) do not; that is the mid-scope NLL point.
    fn end_unused_shares_after(&mut self, stmts: &[Stmt], from: usize) {
        let live: Vec<String> = self.share_vars.keys().cloned().collect();
        for name in live {
            let used = stmts[from..]
                .iter()
                .any(|stmt| Self::stmt_mentions(stmt, &name));
            if used {
                continue;
            }
            self.share_vars.remove(&name);
            self.share_scope_depth.remove(&name);
            if let Some(src) = self.share_sources.remove(&name) {
                let still = self.share_sources.values().any(|s| s == &src);
                if !still {
                    self.shared_borrows.remove(&src);
                }
            }
        }
    }

    fn stmt_mentions(stmt: &Stmt, name: &str) -> bool {
        match stmt {
            Stmt::Let { init, .. } => Self::expr_mentions(init, name),
            Stmt::LetMulti { init, .. } => Self::expr_mentions(init, name),
            Stmt::LetCommaOk { base, index, .. } => {
                Self::expr_mentions(base, name) || Self::expr_mentions(index, name)
            }
            Stmt::Assign { name: n, value } => n == name || Self::expr_mentions(value, name),
            Stmt::IndexAssign { base, index, value } => {
                Self::expr_mentions(base, name)
                    || Self::expr_mentions(index, name)
                    || Self::expr_mentions(value, name)
            }
            Stmt::FieldAssign { base, value, .. } => {
                Self::expr_mentions(base, name) || Self::expr_mentions(value, name)
            }
            Stmt::Expr(e) | Stmt::Return(Some(e)) => Self::expr_mentions(e, name),
            Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => false,
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                init.as_ref()
                    .map(|s| Self::stmt_mentions(s, name))
                    .unwrap_or(false)
                    || Self::expr_mentions(cond, name)
                    || then_block
                        .stmts
                        .iter()
                        .any(|s| Self::stmt_mentions(s, name))
                    || else_block
                        .as_ref()
                        .map(|b| b.stmts.iter().any(|s| Self::stmt_mentions(s, name)))
                        .unwrap_or(false)
            }
            Stmt::While { cond, body, .. } => {
                Self::expr_mentions(cond, name)
                    || body.stmts.iter().any(|s| Self::stmt_mentions(s, name))
            }
            Stmt::For { iter, body, .. } => {
                Self::expr_mentions(iter, name)
                    || body.stmts.iter().any(|s| Self::stmt_mentions(s, name))
            }
            Stmt::CFor {
                init,
                cond,
                post,
                body,
                ..
            } => {
                Self::stmt_mentions(init, name)
                    || Self::expr_mentions(cond, name)
                    || Self::stmt_mentions(post, name)
                    || body.stmts.iter().any(|s| Self::stmt_mentions(s, name))
            }
            Stmt::Defer { body }
            | Stmt::Crew { body, .. }
            | Stmt::Arena { body, .. }
            | Stmt::Unsafe { body } => body.stmts.iter().any(|s| Self::stmt_mentions(s, name)),
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                Self::expr_mentions(timeout_ms, name)
                    || arms.iter().any(|(ch, b)| {
                        ch == name || b.stmts.iter().any(|s| Self::stmt_mentions(s, name))
                    })
                    || default_arm
                        .as_ref()
                        .map(|b| b.stmts.iter().any(|s| Self::stmt_mentions(s, name)))
                        .unwrap_or(false)
            }
        }
    }

    fn expr_mentions(expr: &Expr, name: &str) -> bool {
        match expr {
            Expr::Ident(n) => n == name,
            Expr::Call { callee, args } => {
                Self::expr_mentions(callee, name)
                    || args.iter().any(|a| Self::expr_mentions(a, name))
            }
            Expr::Method { receiver, args, .. } => {
                Self::expr_mentions(receiver, name)
                    || args.iter().any(|a| Self::expr_mentions(a, name))
            }
            Expr::Binary { left, right, .. } => {
                Self::expr_mentions(left, name) || Self::expr_mentions(right, name)
            }
            Expr::Unary { expr: e, .. }
            | Expr::Field { base: e, .. }
            | Expr::Try(e)
            | Expr::Join(e) => Self::expr_mentions(e, name),
            Expr::Index { base, index } => {
                Self::expr_mentions(base, name) || Self::expr_mentions(index, name)
            }
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                Self::expr_mentions(base, name)
                    || low
                        .as_ref()
                        .map(|e| Self::expr_mentions(e, name))
                        .unwrap_or(false)
                    || high
                        .as_ref()
                        .map(|e| Self::expr_mentions(e, name))
                        .unwrap_or(false)
                    || max
                        .as_ref()
                        .map(|e| Self::expr_mentions(e, name))
                        .unwrap_or(false)
            }
            Expr::Array(elems) => elems.iter().any(|e| Self::expr_mentions(e, name)),
            Expr::Tuple(elems) => elems.iter().any(|e| Self::expr_mentions(e, name)),
            Expr::ChanOpen { cap, .. } => Self::expr_mentions(cap, name),
            Expr::StructLit { fields, update, .. } => {
                fields.iter().any(|(_, e)| Self::expr_mentions(e, name))
                    || update
                        .as_ref()
                        .map(|e| Self::expr_mentions(e, name))
                        .unwrap_or(false)
            }
            Expr::StringInterp(parts) => parts.iter().any(|p| match p {
                InterpPart::Expr(e, _) => Self::expr_mentions(e, name),
                InterpPart::Lit(_) => false,
            }),
            Expr::StructLitPos { values, .. } => {
                values.iter().any(|e| Self::expr_mentions(e, name))
            }
            Expr::Block(b) => b.stmts.iter().any(|s| Self::stmt_mentions(s, name)),
            Expr::Lambda { body, .. } => Self::expr_mentions(body, name),
            Expr::Match { scrutinee, arms } => {
                Self::expr_mentions(scrutinee, name)
                    || arms.iter().any(|a| Self::expr_mentions(&a.body, name))
            }
            Expr::IfExpr {
                cond,
                then_block,
                else_block,
            } => {
                Self::expr_mentions(cond, name)
                    || then_block
                        .stmts
                        .iter()
                        .any(|s| Self::stmt_mentions(s, name))
                    || else_block
                        .stmts
                        .iter()
                        .any(|s| Self::stmt_mentions(s, name))
            }
            Expr::Fan { collection, mapper } => {
                Self::expr_mentions(collection, name) || Self::expr_mentions(mapper, name)
            }
            Expr::Convert { args, .. } => args.iter().any(|a| Self::expr_mentions(a, name)),
            Expr::Make { len, cap, .. } => {
                len.as_ref()
                    .map(|e| Self::expr_mentions(e, name))
                    .unwrap_or(false)
                    || cap
                        .as_ref()
                        .map(|e| Self::expr_mentions(e, name))
                        .unwrap_or(false)
            }
            Expr::Kick { crew, expr } => crew == name || Self::expr_mentions(expr, name),
            Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => false,
        }
    }

    fn check_stmt(&mut self, stmt: &Stmt) -> Result<(), TypeError> {
        match stmt {
            Stmt::Let {
                name,
                mutable,
                ownership,
                ty,
                init,
            } => {
                // Move seed: rebinding a non-Copy hold moves it; Copy holds copy.
                let moving_from = if let Expr::Ident(src) = init {
                    if self.hold_vars.contains_key(src) {
                        if self.moved_holds.get(src).copied().unwrap_or(false) {
                            return Err(TypeError::new(format!(
                                "use of moved value `{src}`"
                            ))
                            .hint("use-after-move: `hold` values are consumed on rebind, into calls, and on any full read — bind once or use `share` / Copy types"));
                        }
                        let src_is_copy = self
                            .lookup(src)
                            .map(|(t, _)| is_copy_type(t))
                            .unwrap_or(false);
                        if src_is_copy {
                            None
                        } else {
                            Some(src.clone())
                        }
                    } else {
                        None
                    }
                } else {
                    None
                };
                // Push annotation as expected type so `None` / `Some` / `Ok` / `Err` and
                // untyped literals inhabit `Option[map[…]]`, `Result[map[…], E]`, etc.
                let saved_expected = self.current_expected.clone();
                let ann_ty = if let Some(ann) = ty {
                    let t = self.resolve_type(ann)?;
                    self.current_expected = Some(t.clone());
                    Some(t)
                } else {
                    None
                };
                let inferred = if let Some(ref src) = moving_from {
                    // Type of moved-from binding without treating this read as use-after-move
                    self.lookup(src)
                        .map(|(t, _)| t.clone())
                        .ok_or_else(|| TypeError::new(format!("undefined variable `{src}`")))?
                } else {
                    self.check_expr(init)?
                };
                self.current_expected = saved_expected;
                if let Some(src) = moving_from {
                    self.moved_holds.insert(src, true);
                }
                if name == "_" {
                    return Ok(());
                }
                let final_ty = if let Some(expected) = ann_ty {
                    // Go-like untyped integer literals may inhabit int / int64 / int32 annotations.
                    // Empty or all-literal arrays may inhabit []int / []int64 / []int32.
                    let inferred = match (&expected, init) {
                        (
                            Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::Byte,
                            Expr::Int(_),
                        ) => expected.clone(),
                        (Type::Array(inner), Expr::Array(elems))
                            if matches!(
                                inner.as_ref(),
                                Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::Byte
                            ) && elems.iter().all(|e| matches!(e, Expr::Int(_))) =>
                        {
                            expected.clone()
                        }
                        _ => inferred,
                    };
                    if !self.compatible(&inferred, &expected) {
                        return Err(TypeError::new(format!(
                            "let `{name}`: expected {}, got {}",
                            expected.display(),
                            inferred.display()
                        ))
                        .hint("annotated bindings are strict — use int64(x) / int32(x) / int(x) to convert"));
                    }
                    expected
                } else {
                    inferred
                };
                self.define(name, final_ty.clone(), *mutable);
                if self.lsp_mode && ty.is_none() {
                    self.lsp_bindings.push((name.clone(), final_ty.display()));
                }
                // Sub-slice stored in this binding must not outlive its base.
                self.assert_slice_view_lifetime(name, init)?;
                if matches!(self.lookup(name).map(|(t, _)| t), Some(Type::Fn(_, _))) {
                    let info = self.fn_capture_info_for_expr(init);
                    self.set_fn_capture_info(name, info);
                } else {
                    self.clear_fn_capture_info(name);
                }
                if *ownership == Ownership::Hold {
                    self.hold_vars.insert(name.clone(), true);
                    self.moved_holds.insert(name.clone(), false);
                    self.hold_moved_fields.insert(name.clone(), HashSet::new());
                }
                if *ownership == Ownership::Share {
                    self.share_vars.insert(name.clone(), true);
                    self.share_scope_depth
                        .insert(name.clone(), self.scopes.len());
                    if let Expr::Call { callee, args } = init {
                        if let Expr::Ident(cn) = callee.as_ref() {
                            if cn == "share_int" {
                                if let Some(Expr::Ident(src)) = args.first() {
                                    self.share_sources.insert(name.clone(), src.clone());
                                }
                            }
                        }
                    }
                }
                Ok(())
            }
            Stmt::LetMulti {
                names,
                mutable,
                init,
            } => {
                // Go multi-return: `a, b := f()` (declare) or `a, b = f()` (assign existing)
                let t = self.check_expr(init)?;
                let Type::Tuple(elems) = t else {
                    return Err(TypeError::new(format!(
                        "multi-name binding needs a tuple value, got {}",
                        t.display()
                    ))
                    .hint("return (a, b) from the function, Go-style"));
                };
                if elems.len() != names.len() {
                    return Err(TypeError::new(format!(
                        "multi-name binding: {} names but tuple has {} elements",
                        names.len(),
                        elems.len()
                    )));
                }
                for (n, et) in names.iter().zip(elems.iter()) {
                    if n == "_" {
                        continue;
                    }
                    if let Some((existing, is_mut)) = self.lookup(n).cloned() {
                        // Reassignment path (`a, b = f()`)
                        if !is_mut {
                            return Err(TypeError::new(format!(
                                "cannot assign to immutable `{n}`"
                            ))
                            .hint("use `var {n}` / `let mut {n}` or `:=` on first declaration"));
                        }
                        if !self.compatible(et, &existing) {
                            return Err(TypeError::new(format!(
                                "cannot assign {} to `{n}` (type {})",
                                et.display(),
                                existing.display()
                            )));
                        }
                        if matches!(existing, Type::Fn(_, _)) {
                            let info = match init {
                                Expr::Tuple(values) => values
                                    .get(
                                        names
                                            .iter()
                                            .position(|name| name == n)
                                            .unwrap_or(usize::MAX),
                                    )
                                    .map(|value| self.fn_capture_info_for_expr(value))
                                    .unwrap_or_else(|| FnCaptureInfo {
                                        unknown: true,
                                        ..FnCaptureInfo::default()
                                    }),
                                _ => FnCaptureInfo {
                                    unknown: true,
                                    ..FnCaptureInfo::default()
                                },
                            };
                            self.set_fn_capture_info(n, info);
                        } else {
                            self.clear_fn_capture_info(n);
                        }
                    } else {
                        // Fresh binding (`a, b := f()` or `let a, b = …`)
                        self.define(n, et.clone(), *mutable);
                        if matches!(et, Type::Fn(_, _)) {
                            self.set_fn_capture_info(
                                n,
                                FnCaptureInfo {
                                    unknown: true,
                                    ..FnCaptureInfo::default()
                                },
                            );
                        }
                    }
                }
                Ok(())
            }
            Stmt::LetCommaOk {
                value,
                ok,
                mutable,
                base,
                index,
            } => {
                let bt = self.check_expr(base)?;
                let it = self.check_expr(index)?;
                let Type::Map(k, v) = bt else {
                    return Err(TypeError::new(
                        "comma-ok only works on maps: `let v, ok = m[k]`",
                    ));
                };
                if !self.compatible(&it, &k) {
                    return Err(TypeError::new(format!(
                        "map key type mismatch: expected {}, got {}",
                        k.display(),
                        it.display()
                    )));
                }
                if value != "_" {
                    self.define(value, *v, *mutable);
                }
                if ok != "_" {
                    self.define(ok, Type::Bool, *mutable);
                }
                Ok(())
            }
            Stmt::Assign { name, value } => {
                if name == "_" {
                    self.check_expr(value)?;
                    return Ok(());
                }
                if self.moved_holds.get(name).copied().unwrap_or(false) {
                    return Err(TypeError::new(format!("use of moved value `{name}`"))
                        .hint("cannot assign to a hold binding after it was moved"));
                }
                let Some((ty, mutable)) = self.lookup(name).cloned() else {
                    return Err(TypeError::new(format!("undefined variable `{name}`")));
                };
                if self.share_vars.contains_key(name) {
                    return Err(TypeError::new(format!(
                        "cannot assign to shared `{name}`"
                    ))
                    .hint("share bindings are immutable shared borrows — clone with share_clone if needed"));
                }
                if self.shared_borrows.contains_key(name) {
                    return Err(TypeError::new(format!(
                        "cannot assign to `{name}` while shared"
                    ))
                    .hint("a live `share_int` borrow conflicts with mutation — drop the share first"));
                }
                if !mutable {
                    return Err(TypeError::new(format!(
                        "cannot assign to immutable `{name}` (use `let mut`)"
                    )));
                }
                self.assert_no_race_write(name)?;
                self.assert_slice_view_lifetime(name, value)?;
                self.assert_no_arena_value_store(value)?;
                let vt = self.check_expr(value)?;
                if !self.compatible(&vt, &ty) {
                    return Err(TypeError::new(format!(
                        "cannot assign {} to `{}` of type {}",
                        vt.display(),
                        name,
                        ty.display()
                    )));
                }
                if matches!(ty, Type::Fn(_, _)) {
                    let info = self.fn_capture_info_for_expr(value);
                    self.set_fn_capture_info(name, info);
                } else {
                    self.clear_fn_capture_info(name);
                }
                // Assigning into a live hold binding re-establishes ownership of the new value.
                if self.hold_vars.contains_key(name) {
                    self.moved_holds.insert(name.clone(), false);
                    self.hold_moved_fields.insert(name.clone(), HashSet::new());
                }
                // Track Sync handles for race model (allowed concurrent mut).
                if self.is_sync_ty(&ty) {
                    self.race_sync_locals.insert(name.clone());
                }
                Ok(())
            }
            Stmt::IndexAssign { base, index, value } => {
                self.assert_mutable_write_root(base, "index-assign")?;
                if let Some(name) = Self::race_write_root(base) {
                    if self.shared_borrows.contains_key(name) {
                        return Err(TypeError::new(format!(
                            "cannot index-assign `{name}` while shared"
                        ))
                        .hint("drop the share first"));
                    }
                }
                self.assert_no_race_write_expr(base)?;
                let bt = self.check_expr(base)?;
                let it = self.check_expr(index)?;
                match bt {
                    Type::Array(inner) => {
                        if it != Type::Int {
                            return Err(TypeError::new("slice index must be int"));
                        }
                        let saved_expected = self.current_expected.clone();
                        self.current_expected = Some(inner.as_ref().clone());
                        let vt = if matches!(value, Expr::Int(_))
                            && is_literal_int_kind(inner.as_ref())
                        {
                            (*inner).clone()
                        } else {
                            self.check_expr(value)?
                        };
                        self.current_expected = saved_expected;
                        if !self.compatible(&vt, &inner) {
                            return Err(TypeError::new(format!(
                                "cannot assign {} into []{}",
                                vt.display(),
                                inner.display()
                            )));
                        }
                        Ok(())
                    }
                    Type::Map(k, v) => {
                        if !self.compatible(&it, &k) {
                            return Err(TypeError::new(format!(
                                "map key type mismatch: expected {}, got {}",
                                k.display(),
                                it.display()
                            )));
                        }
                        // Push map value type so None/Some/Ok/Err and untyped literals
                        // inhabit Option/Result (and other) map values correctly.
                        let saved_expected = self.current_expected.clone();
                        self.current_expected = Some(v.as_ref().clone());
                        let vt = if matches!(value, Expr::Int(_)) && is_literal_int_kind(v.as_ref())
                        {
                            (*v).clone()
                        } else if matches!(value, Expr::String(_)) && *v.as_ref() == Type::String {
                            Type::String
                        } else {
                            self.check_expr(value)?
                        };
                        self.current_expected = saved_expected;
                        if !self.compatible(&vt, &v) {
                            return Err(TypeError::new(format!(
                                "map value type mismatch: expected {}, got {}",
                                v.display(),
                                vt.display()
                            )));
                        }
                        Ok(())
                    }
                    other => Err(TypeError::new(format!(
                        "cannot index-assign into {}",
                        other.display()
                    ))),
                }
            }
            Stmt::FieldAssign { base, field, value } => {
                // Same mutability contract as index assign: named mutable root only.
                // Zero runtime cost — compile-time check; C field store stays a single write.
                self.assert_mutable_write_root(base, "field-assign")?;
                // Hold field assign: mutate in place without consuming the binding.
                if let Some(name) = Self::race_write_root(base) {
                    if self.shared_borrows.contains_key(name) {
                        return Err(TypeError::new(format!(
                            "cannot assign field on `{name}` while shared"
                        ))
                        .hint("drop the share first"));
                    }
                }
                self.assert_no_race_write_expr(base)?;
                // SAFE-007: do not store arena-backed values into longer-lived structs.
                self.assert_no_arena_value_store(value)?;
                let bt = if let Expr::Ident(name) = base {
                    if self.hold_vars.contains_key(name) {
                        if self.moved_holds.get(name).copied().unwrap_or(false) {
                            return Err(TypeError::new(format!("use of moved value `{name}`")));
                        }
                        if self
                            .hold_moved_fields
                            .get(name)
                            .map(|s| s.contains(field))
                            .unwrap_or(false)
                        {
                            return Err(TypeError::new(format!(
                                "cannot assign to moved field `{name}.{field}`"
                            )));
                        }
                        self.lookup(name)
                            .map(|(t, _)| t.clone())
                            .ok_or_else(|| TypeError::new(format!("undefined variable `{name}`")))?
                    } else {
                        self.check_expr(base)?
                    }
                } else {
                    self.check_expr(base)?
                };
                match bt {
                    Type::Struct { fields, name } => {
                        let Some((_, fty)) = fields.iter().find(|(n, _)| n == field) else {
                            return Err(TypeError::new(format!(
                                "no field `{field}` on struct {name}"
                            )));
                        };
                        let vt = if matches!(value, Expr::Int(_)) && is_literal_int_kind(fty) {
                            fty.clone()
                        } else if matches!(value, Expr::String(_)) && *fty == Type::String {
                            Type::String
                        } else {
                            self.check_expr(value)?
                        };
                        if !self.compatible(&vt, fty) {
                            return Err(TypeError::new(format!(
                                "cannot assign {} to field `{field}` of type {}",
                                vt.display(),
                                fty.display()
                            )));
                        }
                        Ok(())
                    }
                    other => Err(TypeError::new(format!(
                        "cannot assign field on {}",
                        other.display()
                    ))),
                }
            }
            Stmt::Expr(e) => {
                let t = self.check_expr(e)?;
                if matches!(t, Type::Result(_, _)) {
                    return Err(TypeError::new(
                        "Result value is unused — failures must be handled",
                    )
                    .hint(
                        "use `?`, `match`, `let x = ...`, or `let _ = ...` to discard explicitly",
                    ));
                }
                Ok(())
            }
            Stmt::Return(e) => {
                let got = match e {
                    Some(Expr::Int(_))
                        if matches!(
                            self.current_ret,
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::UInt64
                                | Type::Byte
                        ) =>
                    {
                        self.current_ret.clone()
                    }
                    Some(e) => {
                        if self.arena_depth > 0 {
                            self.assert_no_arena_escape(e)?;
                        }
                        self.assert_no_slice_view_return(e)?;
                        self.check_expr(e)?
                    }
                    None => Type::Void,
                };
                if !self.compatible(&got, &self.current_ret) {
                    return Err(TypeError::new(format!(
                        "return type mismatch: expected {}, got {}",
                        self.current_ret.display(),
                        got.display()
                    )));
                }
                Ok(())
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                // Go-style init (`if x := f(); cond`): check it in a fresh scope so
                // the binding is visible to cond/arms but not after the if.
                let has_init = init.is_some();
                if let Some(init) = init {
                    self.push_scope();
                    self.check_stmt(init)?;
                }
                let ct = self.check_expr(cond)?;
                if ct != Type::Bool {
                    return Err(TypeError::new("if condition must be bool"));
                }
                // CFG edge prune: const false → only else/skip; const true → only then.
                let const_c = const_bool(cond);
                let take_then = const_c != Some(false);
                let take_else_or_skip = const_c != Some(true);

                // NLL join: snapshot hold-move + share state; check arms independently; merge.
                let before_moved = self.moved_holds.clone();
                let before_fields = self.hold_moved_fields.clone();
                let before_share_vars = self.share_vars.clone();
                let before_share_sources = self.share_sources.clone();
                let before_share_depth = self.share_scope_depth.clone();
                let before_borrows = self.shared_borrows.clone();

                let (
                    then_moved,
                    then_fields,
                    then_borrows,
                    then_share_vars,
                    then_share_sources,
                    then_share_depth,
                    then_diverges,
                ) = if take_then {
                    self.check_block(then_block)?;
                    (
                        self.moved_holds.clone(),
                        self.hold_moved_fields.clone(),
                        self.shared_borrows.clone(),
                        self.share_vars.clone(),
                        self.share_sources.clone(),
                        self.share_scope_depth.clone(),
                        block_always_diverges(then_block),
                    )
                } else {
                    (
                        before_moved.clone(),
                        before_fields.clone(),
                        before_borrows.clone(),
                        before_share_vars.clone(),
                        before_share_sources.clone(),
                        before_share_depth.clone(),
                        true, // dead then — treat as diverging for join
                    )
                };

                self.moved_holds = before_moved.clone();
                self.hold_moved_fields = before_fields.clone();
                self.share_vars = before_share_vars.clone();
                self.share_sources = before_share_sources.clone();
                self.share_scope_depth = before_share_depth.clone();
                self.shared_borrows = before_borrows.clone();

                if let Some(eb) = else_block {
                    let (
                        else_moved,
                        else_fields,
                        else_borrows,
                        else_share_vars,
                        else_share_sources,
                        else_share_depth,
                        else_diverges,
                    ) = if take_else_or_skip {
                        self.check_block(eb)?;
                        (
                            self.moved_holds.clone(),
                            self.hold_moved_fields.clone(),
                            self.shared_borrows.clone(),
                            self.share_vars.clone(),
                            self.share_sources.clone(),
                            self.share_scope_depth.clone(),
                            block_always_diverges(eb),
                        )
                    } else {
                        (
                            before_moved.clone(),
                            before_fields.clone(),
                            before_borrows.clone(),
                            before_share_vars.clone(),
                            before_share_sources.clone(),
                            before_share_depth.clone(),
                            true,
                        )
                    };

                    // Join holds: moves from a diverging / dead arm do not reach the join.
                    let mut joined_moved = before_moved;
                    if take_then && !then_diverges {
                        for (name, was) in &then_moved {
                            if *was {
                                joined_moved.insert(name.clone(), true);
                            }
                        }
                    }
                    if take_else_or_skip && !else_diverges {
                        for (name, was) in &else_moved {
                            if *was {
                                joined_moved.insert(name.clone(), true);
                            }
                        }
                    }
                    self.moved_holds = joined_moved;
                    let mut joined_fields = before_fields;
                    if take_then && !then_diverges {
                        for (name, fields) in &then_fields {
                            joined_fields
                                .entry(name.clone())
                                .or_default()
                                .extend(fields.iter().cloned());
                        }
                    }
                    if take_else_or_skip && !else_diverges {
                        for (name, fields) in &else_fields {
                            joined_fields
                                .entry(name.clone())
                                .or_default()
                                .extend(fields.iter().cloned());
                        }
                    }
                    joined_fields.retain(|_, u| !u.is_empty());
                    self.hold_moved_fields = joined_fields;

                    // Share join: borrow lives after if live on every *reachable* arm.
                    let mut joined_borrows = HashMap::new();
                    let mut joined_vars = HashMap::new();
                    let mut joined_sources = HashMap::new();
                    let mut joined_depth = HashMap::new();
                    let then_live = take_then && !then_diverges;
                    let else_live = take_else_or_skip && !else_diverges;
                    match (then_live, else_live) {
                        (false, false) => {
                            joined_borrows = before_borrows;
                            joined_vars = before_share_vars;
                            joined_sources = before_share_sources;
                            joined_depth = before_share_depth;
                        }
                        (false, true) => {
                            joined_borrows = else_borrows;
                            joined_vars = else_share_vars;
                            joined_sources = else_share_sources;
                            joined_depth = else_share_depth;
                        }
                        (true, false) => {
                            joined_borrows = then_borrows;
                            joined_vars = then_share_vars;
                            joined_sources = then_share_sources;
                            joined_depth = then_share_depth;
                        }
                        (true, true) => {
                            for (src, _) in &then_borrows {
                                if else_borrows.contains_key(src) {
                                    joined_borrows.insert(src.clone(), true);
                                }
                            }
                            for name in then_share_vars.keys() {
                                if else_share_vars.contains_key(name) {
                                    joined_vars.insert(name.clone(), true);
                                    if let Some(src) = then_share_sources
                                        .get(name)
                                        .or_else(|| else_share_sources.get(name))
                                    {
                                        if joined_borrows.contains_key(src) {
                                            joined_sources.insert(name.clone(), src.clone());
                                        }
                                    }
                                    if let Some(d) = then_share_depth
                                        .get(name)
                                        .or_else(|| else_share_depth.get(name))
                                    {
                                        joined_depth.insert(name.clone(), *d);
                                    }
                                }
                            }
                        }
                    }
                    self.shared_borrows = joined_borrows;
                    self.share_vars = joined_vars;
                    self.share_sources = joined_sources;
                    self.share_scope_depth = joined_depth;
                } else if !take_then || then_diverges {
                    // No else: skip-then only, or then diverges → continuation = before.
                    // Const-true + diverge: nothing after (keep before for soundness).
                    // Const-false: then dead → before.
                    self.moved_holds = before_moved;
                    self.hold_moved_fields = before_fields;
                    self.share_vars = before_share_vars;
                    self.share_sources = before_share_sources;
                    self.share_scope_depth = before_share_depth;
                    self.shared_borrows = before_borrows;
                } else if !take_else_or_skip {
                    // Const-true, then falls through — only then state.
                    self.moved_holds = then_moved;
                    self.hold_moved_fields = then_fields;
                    self.shared_borrows = then_borrows;
                    self.share_vars = then_share_vars;
                    self.share_sources = then_share_sources;
                    self.share_scope_depth = then_share_depth;
                } else {
                    // No else, then may or may not run: union moves; union shares.
                    self.moved_holds = then_moved;
                    self.hold_moved_fields = then_fields;
                    let mut joined_borrows = before_borrows;
                    for (src, _) in &then_borrows {
                        joined_borrows.insert(src.clone(), true);
                    }
                    let mut joined_vars = before_share_vars;
                    for (n, _) in &then_share_vars {
                        joined_vars.insert(n.clone(), true);
                    }
                    let mut joined_sources = before_share_sources;
                    for (n, src) in &then_share_sources {
                        joined_sources.insert(n.clone(), src.clone());
                    }
                    let mut joined_depth = before_share_depth;
                    for (n, d) in &then_share_depth {
                        joined_depth.insert(n.clone(), *d);
                    }
                    self.shared_borrows = joined_borrows;
                    self.share_vars = joined_vars;
                    self.share_sources = joined_sources;
                    self.share_scope_depth = joined_depth;
                }
                if has_init {
                    self.pop_scope();
                }
                Ok(())
            }
            Stmt::While { label, cond, body } => {
                let ct = self.check_expr(cond)?;
                if ct != Type::Bool {
                    return Err(TypeError::new("while condition must be bool"));
                }
                // CFG: while false → body unreachable; while true → no cond-false exit.
                if const_bool(cond) == Some(false) {
                    return Ok(());
                }
                // Snapshot share state entering the loop — each iteration starts fresh
                // for shares introduced/ended inside the body (re-borrow next iter OK).
                let loop_share_vars = self.share_vars.clone();
                let loop_share_sources = self.share_sources.clone();
                let loop_share_depth = self.share_scope_depth.clone();
                let loop_borrows = self.shared_borrows.clone();
                let entry_moved = self.moved_holds.clone();
                let entry_fields = self.hold_moved_fields.clone();
                self.push_loop(label.clone());
                self.check_block(body)?;
                // End shares that were only live inside the body (not entering the loop).
                self.share_vars = loop_share_vars.clone();
                self.share_sources = loop_share_sources.clone();
                self.share_scope_depth = loop_share_depth.clone();
                self.shared_borrows = loop_borrows.clone();
                // Continue-path moves join into next-iteration entry (if-join skips them).
                self.apply_continue_path_moves();
                // Loop-carried fixpoint: only when some path can re-enter the header.
                // Always-break / always-return bodies skip the second pass (CFG).
                if loop_body_may_reach_header(body) {
                    let ct2 = self.check_expr(cond)?;
                    if ct2 != Type::Bool {
                        return Err(TypeError::new("while condition must be bool"));
                    }
                    self.check_block(body)?;
                    self.share_vars = loop_share_vars;
                    self.share_sources = loop_share_sources;
                    self.share_scope_depth = loop_share_depth;
                    self.shared_borrows = loop_borrows;
                    // Continue-path moves also poison post-loop.
                    self.apply_continue_path_moves();
                } else {
                    // No re-entry: post-loop state = break-exit state from the single pass.
                    self.share_vars = loop_share_vars;
                    self.share_sources = loop_share_sources;
                    self.share_scope_depth = loop_share_depth;
                    self.shared_borrows = loop_borrows;
                    let _ = (&entry_moved, &entry_fields);
                }
                // Break-path moves (if-join drops diverging arms) poison post-loop.
                self.apply_break_path_moves();
                self.pop_loop();
                Ok(())
            }
            Stmt::CFor {
                label,
                init,
                cond,
                post,
                body,
            } => {
                // `init` is scoped to the loop (like Go). Loop-carried NLL:
                // when the body can re-enter, run a second pass so field/hold
                // moves on iteration N poison iteration N+1 and the exit.
                self.push_scope();
                self.check_stmt(init)?;
                let ct = self.check_expr(cond)?;
                if ct != Type::Bool {
                    self.pop_scope();
                    return Err(TypeError::new("for condition must be bool"));
                }
                self.push_loop(label.clone());
                self.check_block(body)?;
                self.check_stmt(post)?;
                self.apply_continue_path_moves();
                if loop_body_may_reach_header(body) {
                    let ct2 = self.check_expr(cond)?;
                    if ct2 != Type::Bool {
                        self.pop_loop();
                        self.pop_scope();
                        return Err(TypeError::new("for condition must be bool"));
                    }
                    self.check_block(body)?;
                    self.check_stmt(post)?;
                    self.apply_continue_path_moves();
                }
                self.apply_break_path_moves();
                self.pop_loop();
                self.pop_scope();
                Ok(())
            }
            Stmt::Break(label) => {
                self.resolve_loop_label(label)?;
                // Capture moves on break path for post-loop join (if-join skips them).
                self.record_break_path_moves(label);
                Ok(())
            }
            Stmt::Continue(label) => {
                self.resolve_loop_label(label)?;
                // Capture moves on this path for next-iter / post-loop join.
                // Labeled `continue outer` writes the outer frame's accumulator.
                self.record_continue_path_moves(label);
                Ok(())
            }
            Stmt::Defer { body } => {
                // Queue for exit-time NLL check (defer runs after the rest of the function).
                self.pending_defers.push(body.clone());
                Ok(())
            }
            Stmt::For {
                label,
                binders,
                is_range,
                iter,
                body,
            } => {
                let it = self.check_expr(iter)?;
                // Classify iterable
                // String + `range` → Go-like runes (int code points); legacy `for b in s` → bytes.
                let (index_ty, value_ty, is_int_count, is_chan) = match &it {
                    Type::Array(e) => (Type::Int, (**e).clone(), false, false),
                    Type::Map(k, v) => ((**k).clone(), (**v).clone(), false, false),
                    Type::Chan(e) => (Type::Int, (**e).clone(), false, true),
                    Type::String if *is_range => (Type::Int, Type::Int, false, false), // rune
                    Type::String => (Type::Int, Type::Byte, false, false), // legacy byte values
                    Type::Int if *is_range || binders.len() <= 1 => {
                        (Type::Int, Type::Int, true, false)
                    }
                    Type::Int => {
                        return Err(TypeError::new(
                            "cannot use two binders over an int count; use `for i in n` or `for i in range n`",
                        ));
                    }
                    // Iterator protocol: struct with a `next` method returning Option[T].
                    // Prefer `fn next(mut self)` so the loop advances the binding in place.
                    Type::Struct { name, .. } | Type::Named(name) => {
                        let next_fn = format!("{name}_next");
                        if let Some(Type::Fn(_, ret)) = self.fns.get(&next_fn) {
                            match ret.as_ref() {
                                Type::Option(inner) => {
                                    let mut_self = self
                                        .fn_mut_params
                                        .get(&next_fn)
                                        .and_then(|m| m.first().copied())
                                        .unwrap_or(false);
                                    if mut_self {
                                        match iter {
                                            Expr::Ident(n) => {
                                                let Some((_, is_mut)) = self.lookup(n) else {
                                                    return Err(TypeError::new(format!(
                                                        "cannot iterate over unknown binding `{n}`"
                                                    )));
                                                };
                                                if !is_mut {
                                                    return Err(TypeError::new(format!(
                                                        "`{name}_next` takes mut self — iterate a `let mut` binding"
                                                    ))
                                                    .hint(format!(
                                                        "use `let mut {n} = …` then `for … in {n}`"
                                                    )));
                                                }
                                            }
                                            _ => {
                                                return Err(TypeError::new(format!(
                                                    "`{name}_next` takes mut self — iterable must be a mutable local"
                                                ))
                                                .hint(
                                                    "bind the iterator first: `let mut it = …; for v in it { … }`",
                                                ));
                                            }
                                        }
                                    }
                                    (Type::Int, (**inner).clone(), false, false)
                                }
                                _ => {
                                    return Err(TypeError::new(format!(
                                        "`{name}_next` must return Option[T] for iterator protocol"
                                    )));
                                }
                            }
                        } else {
                            return Err(TypeError::new(format!(
                                "cannot iterate over {}; implement `on {name} {{ fn next(mut self) -> Option[T] }}` for iterator support",
                                name
                            )));
                        }
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "cannot iterate over {}",
                            other.display()
                        )));
                    }
                };
                if matches!(it, Type::Map(_, _)) && !*is_range {
                    return Err(TypeError::new(
                        "map iteration requires `range` (e.g. `for k, v in range m`)",
                    ));
                }
                if is_chan {
                    if !*is_range {
                        return Err(TypeError::new(
                            "channel iteration requires `range` (e.g. `for v in range ch`)",
                        ));
                    }
                    if binders.len() == 2 {
                        return Err(TypeError::new(
                            "channel range yields values only — use `for v in range ch` (not `i, v`)",
                        ));
                    }
                }
                if binders.len() == 2 && is_int_count {
                    return Err(TypeError::new(
                        "integer range yields indices only — use `for i in range n` (not `i, v`)",
                    ));
                }
                if !*is_range && binders.len() == 2 {
                    return Err(TypeError::new(
                        "two binders require `range` (e.g. `for i, v in range s`)",
                    ));
                }
                self.push_scope();
                match binders.as_slice() {
                    [] => {
                        // `for range s` — no binders
                        if !is_range {
                            return Err(TypeError::new("empty binders require `range`"));
                        }
                    }
                    [a] => {
                        // Single binder:
                        //   range over array → index (Go: `for i := range s`)
                        //   range over chan → value until close
                        //   legacy `for v in arr` → value
                        //   int count → index
                        let bind_ty = if is_chan {
                            value_ty
                        } else if *is_range {
                            index_ty
                        } else if is_int_count {
                            index_ty
                        } else {
                            value_ty
                        };
                        if a != "_" {
                            self.define(a, bind_ty, false);
                        }
                    }
                    [a, b] => {
                        if a != "_" {
                            self.define(a, index_ty, false);
                        }
                        if b != "_" {
                            self.define(b, value_ty, false);
                        }
                    }
                    _ => {
                        return Err(TypeError::new("for supports at most two binders"));
                    }
                }
                // Snapshot share state — each iteration restores (like while).
                let loop_share_vars = self.share_vars.clone();
                let loop_share_sources = self.share_sources.clone();
                let loop_share_depth = self.share_scope_depth.clone();
                let loop_borrows = self.shared_borrows.clone();
                self.push_loop(label.clone());
                self.check_loop_body_stmts(&body.stmts)?;
                self.share_vars = loop_share_vars.clone();
                self.share_sources = loop_share_sources.clone();
                self.share_scope_depth = loop_share_depth.clone();
                self.shared_borrows = loop_borrows.clone();
                // Continue-path moves join into next-iteration entry.
                self.apply_continue_path_moves();
                // Loop-carried fixpoint only when body can re-enter (CFG).
                if loop_body_may_reach_header(body) {
                    self.check_loop_body_stmts(&body.stmts)?;
                    self.share_vars = loop_share_vars;
                    self.share_sources = loop_share_sources;
                    self.share_scope_depth = loop_share_depth;
                    self.shared_borrows = loop_borrows;
                    self.apply_continue_path_moves();
                } else {
                    self.share_vars = loop_share_vars;
                    self.share_sources = loop_share_sources;
                    self.share_scope_depth = loop_share_depth;
                    self.shared_borrows = loop_borrows;
                }
                self.apply_break_path_moves();
                self.pop_loop();
                self.pop_scope();
                Ok(())
            }
            Stmt::Crew { name, body } => {
                // NLL: outer hold moves inside crew survive after the block
                // (crew body always runs synchronously to cancel_join). Snapshot
                // only for share/borrow join; hold moves use normal sequential flow.
                let before_moved = self.moved_holds.clone();
                let before_fields = self.hold_moved_fields.clone();
                self.push_scope();
                self.define(name, Type::Crew, false);
                for stmt in &body.stmts {
                    self.check_stmt(stmt)?;
                }
                self.pop_scope();
                // Union: moves that existed before or happened inside crew.
                for (n, m) in before_moved {
                    if m {
                        self.moved_holds.insert(n, true);
                    }
                }
                for (n, fields) in before_fields {
                    if !fields.is_empty() {
                        self.hold_moved_fields.entry(n).or_default().extend(fields);
                    }
                }
                Ok(())
            }
            Stmt::Arena { name, body } => {
                self.push_scope();
                self.define(name, Type::Arena, false);
                self.arena_depth += 1;
                let owned_before = self.arena_owned.len();
                for stmt in &body.stmts {
                    self.check_stmt(stmt)?;
                    // Track arena-backed locals for escape checks (SAFE-007).
                    if let Stmt::Let {
                        name: bind, init, ..
                    } = stmt
                    {
                        if Self::expr_is_arena_alloc(init) {
                            self.arena_owned.insert(bind.clone());
                        }
                    }
                }
                // Drop names introduced in this arena (best-effort: clear all new).
                if self.arena_owned.len() > owned_before {
                    // Rebuild without bindings defined only for this block: clear all
                    // tracked names when leaving an arena — they cannot outlive it.
                    self.arena_owned.clear();
                }
                self.arena_depth -= 1;
                self.pop_scope();
                Ok(())
            }
            Stmt::Unsafe { body } => {
                self.unsafe_depth += 1;
                self.push_scope();
                for stmt in &body.stmts {
                    self.check_stmt(stmt)?;
                }
                self.pop_scope();
                self.unsafe_depth -= 1;
                Ok(())
            }
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                let tt = self.check_expr(timeout_ms)?;
                if tt != Type::Int {
                    return Err(TypeError::new("select timeout must be int (milliseconds)"));
                }
                if arms.is_empty() || arms.len() > 16 {
                    return Err(TypeError::new("select needs 1..16 channel arms"));
                }
                // NLL: snapshot; check each arm independently; join moves from
                // reachable (non-diverging) arms — same model as match.
                let before_moved = self.moved_holds.clone();
                let before_fields = self.hold_moved_fields.clone();
                let mut joined_moved = before_moved.clone();
                let mut joined_fields = before_fields.clone();
                // All arms must share the same channel element family (int/float
                // share the int ring; string uses str select).
                let mut arm_kind: Option<&'static str> = None;
                for (ch, body) in arms {
                    let kind = match self.lookup(ch) {
                        Some((Type::Chan(inner), _)) => match inner.as_ref() {
                            Type::String => "str",
                            Type::Float => "float",
                            Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::Bool => "int",
                            Type::Named(n) if self.structs_named(n) => "ptr",
                            Type::Struct { .. } => "ptr",
                            _ => "other",
                        },
                        Some((t, _)) => {
                            return Err(TypeError::new(format!(
                                "select arm `{ch}` must be a channel, got {}",
                                t.display()
                            )));
                        }
                        None => {
                            return Err(TypeError::new(format!(
                                "select arm `{ch}`: undefined channel"
                            )));
                        }
                    };
                    if kind == "other" {
                        return Err(TypeError::new(format!(
                            "select arm `{ch}`: select supports int/float/string/struct channels"
                        )));
                    }
                    match arm_kind {
                        None => arm_kind = Some(kind),
                        Some(k) if k != kind => {
                            return Err(TypeError::new(
                                "select arms must use the same channel element family (all int/float or all string)",
                            ));
                        }
                        _ => {}
                    }
                    self.moved_holds = before_moved.clone();
                    self.hold_moved_fields = before_fields.clone();
                    let diverges = block_always_diverges(body);
                    self.check_block(body)?;
                    if !diverges {
                        for (name, moved) in &self.moved_holds {
                            if *moved {
                                joined_moved.insert(name.clone(), true);
                            }
                        }
                        for (name, fields) in &self.hold_moved_fields {
                            joined_fields
                                .entry(name.clone())
                                .or_default()
                                .extend(fields.iter().cloned());
                        }
                    }
                }
                if let Some(def) = default_arm {
                    self.moved_holds = before_moved.clone();
                    self.hold_moved_fields = before_fields.clone();
                    let diverges = block_always_diverges(def);
                    self.check_block(def)?;
                    if !diverges {
                        for (name, moved) in &self.moved_holds {
                            if *moved {
                                joined_moved.insert(name.clone(), true);
                            }
                        }
                        for (name, fields) in &self.hold_moved_fields {
                            joined_fields
                                .entry(name.clone())
                                .or_default()
                                .extend(fields.iter().cloned());
                        }
                    }
                }
                self.moved_holds = joined_moved;
                self.hold_moved_fields = joined_fields;
                Ok(())
            }
        }
    }

    /// Walk `base.field` chain to `(hold_name, ["inner","a"])` when rooted at Ident.
    fn hold_field_path(base: &Expr, field: &str) -> Option<(String, Vec<String>)> {
        let mut fields = vec![field.to_string()];
        let mut cur = base;
        loop {
            match cur {
                Expr::Field { base, field } => {
                    fields.push(field.clone());
                    cur = base.as_ref();
                }
                Expr::Ident(name) => {
                    fields.reverse();
                    return Some((name.clone(), fields));
                }
                _ => return None,
            }
        }
    }

    fn check_ident_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Ident(name) => {
                if self.moved_holds.get(name).copied().unwrap_or(false) {
                    return Err(TypeError::new(format!("use of moved value `{name}`"))
                        .hint("use-after-move: `hold` values are consumed on rebind, into calls, and on any full read — bind once or use `share` / Copy types"));
                }
                // Partial-move conflict: cannot use whole binding after a field was moved.
                if self.hold_vars.contains_key(name) {
                    if let Some(fields) = self.hold_moved_fields.get(name) {
                        if !fields.is_empty() {
                            return Err(TypeError::new(format!(
                                "use of partially moved value `{name}`"
                            ))
                            .hint("some fields were moved — use remaining fields, or stop using the whole value"));
                        }
                    }
                }
                // Non-Copy `hold` reads consume the binding. Copy holds may be re-read.
                // `hold let y = x` bypasses this via the Let moving_from path.
                if self.hold_vars.contains_key(name) {
                    if let Some((ty, _)) = self.lookup(name).map(|(t, m)| (t.clone(), *m)) {
                        if !is_copy_type(&ty) {
                            self.moved_holds.insert(name.clone(), true);
                        }
                        return Ok(ty);
                    }
                }
                if let Some((ty, _)) = self.lookup(name) {
                    return Ok(ty.clone());
                }
                if let Some(ty) = self.fns.get(name) {
                    return Ok(ty.clone());
                }
                if name == "None" {
                    // Bare `None` (not `None()`): use expected Option[T] from return/context.
                    if let Some(Type::Option(i)) = &self.current_expected {
                        return Ok(Type::Option(i.clone()));
                    }
                    if let Type::Option(i) = &self.current_ret {
                        return Ok(Type::Option(i.clone()));
                    }
                    return Ok(Type::Option(Box::new(Type::Int)));
                }
                // Try qualified variant lookup first using return type context
                let qctor = if let Type::Named(ret_name) = &self.current_ret {
                    self.variants.get(&format!("{ret_name}::{name}")).cloned()
                } else {
                    None
                };
                if let Some(ctor) = qctor.as_ref().or_else(|| self.variants.get(name)) {
                    if ctor.fields.is_empty() {
                        return Ok(Type::Enum {
                            name: ctor.enum_name.clone(),
                            variants: self
                                .types
                                .get(&ctor.enum_name)
                                .and_then(|t| match t {
                                    Type::Enum { variants, .. } => Some(variants.clone()),
                                    _ => None,
                                })
                                .unwrap_or_default(),
                        });
                    }
                    return Ok(Type::Named(name.clone())); // constructor fn-like
                }
                match name.as_str() {
                    "Ok" | "Err" | "Some" => Ok(Type::Named(name.clone())),
                    _ => {
                        let mut candidates: Vec<String> = Vec::new();
                        for scope in self.scopes.iter().rev() {
                            candidates.extend(scope.keys().cloned());
                        }
                        candidates.extend(self.fns.keys().cloned());
                        candidates.extend(self.variants.keys().cloned());
                        let mut err = TypeError::new(format!("cannot find `{name}` in this scope"));
                        if let Some(s) = crate::diag::suggest_name(name, &candidates) {
                            err = err.hint(format!("a name with a similar spelling exists: `{s}`"));
                        } else {
                            err = err
                                .hint("check the spelling, or declare it with `let` / `fn` first");
                        }
                        Err(err)
                    }
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_binary_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Binary { op, left, right } => {
                let lt = self.check_expr(left)?;
                let rt = self.check_expr(right)?;
                match op {
                    BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                        if lt == Type::Int && rt == Type::Int {
                            Ok(Type::Int)
                        } else if lt == Type::Int64 && rt == Type::Int64 {
                            Ok(Type::Int64)
                        } else if lt == Type::Int32 && rt == Type::Int32 {
                            Ok(Type::Int32)
                        } else if lt == Type::Int8 && rt == Type::Int8 {
                            Ok(Type::Int8)
                        } else if lt == Type::UInt64 && rt == Type::UInt64 {
                            Ok(Type::UInt64)
                        } else if lt == Type::Byte && rt == Type::Byte {
                            Ok(Type::Byte)
                        } else if lt == Type::Float && rt == Type::Float {
                            Ok(Type::Float)
                        } else if *op == BinOp::Add && lt == Type::String && rt == Type::String {
                            Ok(Type::String)
                        } else if matches!(lt, Type::Option(_)) || matches!(rt, Type::Option(_)) {
                            Err(TypeError::new(format!(
                                "cannot use Option in arithmetic (got {} and {})",
                                lt.display(),
                                rt.display()
                            ))
                            .hint("unwrap with `match` / `?` first — no nil, no silent Option coercion"))
                        } else if matches!(lt, Type::Result(_, _))
                            || matches!(rt, Type::Result(_, _))
                        {
                            Err(TypeError::new(
                                "cannot use Result in arithmetic — handle with `?` or `match`",
                            ))
                        } else if is_int_family(&lt) && is_int_family(&rt) && lt != rt {
                            Err(TypeError::new(format!(
                                "cannot mix {} and {} in arithmetic",
                                lt.display(),
                                rt.display()
                            ))
                            .hint("convert explicitly: int64(x), int32(x), or int(x)"))
                        } else {
                            Err(TypeError::new(format!(
                                "type error: cannot apply arithmetic to {} and {}",
                                lt.display(),
                                rt.display()
                            ))
                            .hint("no implicit coercion between int/int32/int64/float/bool/string"))
                        }
                    }
                    BinOp::Eq | BinOp::Ne => {
                        if self.compatible(&lt, &rt) {
                            Ok(Type::Bool)
                        } else {
                            Err(TypeError::new(format!(
                                "equality type mismatch: {} vs {}",
                                lt.display(),
                                rt.display()
                            )))
                        }
                    }
                    BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                        if (lt == Type::Int && rt == Type::Int)
                            || (lt == Type::Int64 && rt == Type::Int64)
                            || (lt == Type::Int32 && rt == Type::Int32)
                            || (lt == Type::Int8 && rt == Type::Int8)
                            || (lt == Type::UInt64 && rt == Type::UInt64)
                            || (lt == Type::Byte && rt == Type::Byte)
                            || (lt == Type::Float && rt == Type::Float)
                        {
                            Ok(Type::Bool)
                        } else if is_int_family(&lt) && is_int_family(&rt) && lt != rt {
                            Err(TypeError::new(format!(
                                "cannot compare {} with {} — convert with int64(x) / int32(x) / int(x)",
                                lt.display(),
                                rt.display()
                            )))
                        } else {
                            Err(TypeError::new(format!(
                                "comparison needs matching numbers, got {} and {}",
                                lt.display(),
                                rt.display()
                            ))
                            .hint("compare same integer kinds, or float with float"))
                        }
                    }
                    BinOp::And | BinOp::Or => {
                        if lt == Type::Bool && rt == Type::Bool {
                            Ok(Type::Bool)
                        } else {
                            Err(TypeError::new("logical ops need bool"))
                        }
                    }
                    BinOp::BitAnd
                    | BinOp::BitOr
                    | BinOp::BitXor
                    | BinOp::BitClear
                    | BinOp::Shl
                    | BinOp::Shr => {
                        if lt == Type::Int && rt == Type::Int {
                            Ok(Type::Int)
                        } else if lt == Type::Int64 && rt == Type::Int64 {
                            Ok(Type::Int64)
                        } else if lt == Type::Int32 && rt == Type::Int32 {
                            Ok(Type::Int32)
                        } else if lt == Type::Int8 && rt == Type::Int8 {
                            Ok(Type::Int8)
                        } else if lt == Type::UInt64 && rt == Type::UInt64 {
                            Ok(Type::UInt64)
                        } else if lt == Type::Byte && rt == Type::Byte {
                            Ok(Type::Byte)
                        } else if is_int_family(&lt) && is_int_family(&rt) && lt != rt {
                            Err(TypeError::new(format!(
                                "cannot mix {} and {} in bitwise op",
                                lt.display(),
                                rt.display()
                            ))
                            .hint("convert explicitly: int64(x), int32(x), or int(x)"))
                        } else {
                            Err(TypeError::new(format!(
                                "bitwise ops need matching integers, got {} and {}",
                                lt.display(),
                                rt.display()
                            )))
                        }
                    }
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_unary_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Unary { op, expr } => {
                let t = self.check_expr(expr)?;
                match op {
                    UnaryOp::Neg
                        if matches!(
                            t,
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::UInt64
                                | Type::Byte
                                | Type::Float
                        ) =>
                    {
                        Ok(t)
                    }
                    UnaryOp::Not if t == Type::Bool => Ok(Type::Bool),
                    UnaryOp::BitNot
                        if matches!(
                            t,
                            Type::Int
                                | Type::Int64
                                | Type::Int32
                                | Type::Int8
                                | Type::UInt64
                                | Type::Byte
                        ) =>
                    {
                        Ok(t)
                    }
                    _ => Err(TypeError::new("invalid unary operand")),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_tuple_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Tuple(elems) => {
                if elems.len() < 2 {
                    return Err(TypeError::new("tuple needs at least 2 elements"));
                }
                // Propagate expected element types so None / Ok / Some refine correctly
                // inside map[K](Option[T], U) and similar bag-field tuples.
                let expected_elems: Option<Vec<Type>> = match &self.current_expected {
                    Some(Type::Tuple(ts)) if ts.len() == elems.len() => Some(ts.clone()),
                    _ => None,
                };
                let mut tys = Vec::new();
                for (i, e) in elems.iter().enumerate() {
                    if let Some(ref exp) = expected_elems {
                        let saved = self.current_expected.clone();
                        self.current_expected = Some(exp[i].clone());
                        let t = self.check_expr(e)?;
                        self.current_expected = saved;
                        tys.push(t);
                    } else {
                        tys.push(self.check_expr(e)?);
                    }
                }
                Ok(Type::Tuple(tys))
            }
            _ => unreachable!(),
        }
    }

    fn check_chan_open_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::ChanOpen { elem, cap } => {
                let cap_t = self.check_expr(cap)?;
                if cap_t != Type::Int {
                    return Err(TypeError::new("chan_open capacity must be int")
                        .hint("use chan_open[T](n) where n is int"));
                }
                let et = self.resolve_type(elem)?;
                match &et {
                    // int family + bool → int ring; float → int ring bitcast;
                    // string → str ring; named structs → ptr ring
                    Type::Int
                    | Type::Int64
                    | Type::Int32
                    | Type::Int8
                    | Type::Byte
                    | Type::Bool
                    | Type::Float
                    | Type::String => Ok(Type::Chan(Box::new(et))),
                    Type::Named(n)
                        if n != "ShareInt"
                            && n != "Arena"
                            && n != "Crew"
                            && (self.structs_named(n) || self.enums_named(n)) =>
                    {
                        Ok(Type::Chan(Box::new(et)))
                    }
                    Type::Struct { .. } => Ok(Type::Chan(Box::new(et))),
                    Type::Enum { .. } => Ok(Type::Chan(Box::new(et))),
                    Type::Tuple(elems)
                        if elems.iter().all(|e| self.is_chan_element_ty(e)) =>
                    {
                        Ok(Type::Chan(Box::new(et)))
                    }
                    other => Err(TypeError::new(format!(
                        "chan_open supports int family, bool, float, string, named structs, enums, and tuples of those, got {}",
                        other.display()
                    ))
                    .hint("chan_new(n) remains the int channel API (backward compatible)")),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_builtin_call_00(
        &mut self,
        name: &str,
        args: &[Expr],
    ) -> Option<Result<Type, TypeError>> {
        check_builtin_match!(name, {
            "gc_arena_new" | "gc_alloc" | "gc_collect" | "gc_live"
            | "gc_enabled" | "gc_root" | "gc_unroot" | "gc_link" | "gc_mark"
            | "gc_root_count" => {
                return Err(TypeError::new(format!(
                    "{name} is unavailable: Mako has no garbage collector"
                ))
                .hint("use let/hold/share/arena for deterministic ownership"));
            }
            "Ok" if args.len() == 1 => {
                // Prefer expected Result shape from current_expected (nested
                // constructors) or function return type.
                let expected = match &self.current_expected {
                    Some(Type::Result(ok, err)) => {
                        Some((ok.as_ref().clone(), err.as_ref().clone()))
                    }
                    _ => match &self.current_ret {
                        Type::Result(ok, err) => {
                            Some((ok.as_ref().clone(), err.as_ref().clone()))
                        }
                        _ => None,
                    },
                };
                if let Some((ok, err)) = expected {
                    let saved_ret = self.current_ret.clone();
                    let saved_exp = self.current_expected.clone();
                    self.current_expected = Some(ok.clone());
                    if matches!(&ok, Type::Result(_, _)) {
                        self.current_ret = ok.clone();
                    }
                    let t = self.check_expr(&args[0])?;
                    self.current_ret = saved_ret;
                    self.current_expected = saved_exp;
                    if !self.compatible(&t, &ok) {
                        return Err(TypeError::new(format!(
                            "Ok(...) type mismatch: expected {}, got {}",
                            ok.display(),
                            t.display()
                        )));
                    }
                    return Ok(Type::Result(Box::new(ok), Box::new(err)));
                }
                let t = self.check_expr(&args[0])?;
                return Ok(Type::Result(Box::new(t), Box::new(Type::String)));
            }
            "Err" if args.len() == 1 => {
                let expected = match &self.current_expected {
                    Some(Type::Result(ok, err)) => {
                        Some((ok.as_ref().clone(), err.as_ref().clone()))
                    }
                    _ => match &self.current_ret {
                        Type::Result(ok, err) => {
                            Some((ok.as_ref().clone(), err.as_ref().clone()))
                        }
                        _ => None,
                    },
                };
                if let Some((ok, err)) = expected {
                    let e = self.check_expr(&args[0])?;
                    if !self.compatible(&e, &err) {
                        return Err(TypeError::new(format!(
                            "Err(...) type mismatch: expected {}, got {}",
                            err.display(),
                            e.display()
                        )));
                    }
                    return Ok(Type::Result(Box::new(ok), Box::new(err)));
                }
                let e = self.check_expr(&args[0])?;
                return Ok(Type::Result(Box::new(Type::Int), Box::new(e)));
            }
            "error" if args.len() == 1 => {
                let e = self.check_expr(&args[0])?;
                if e != Type::String {
                    return Err(TypeError::new("error(...) expects a string message"));
                }
                return Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)));
            }
            // error_context(r, msg) — richer error seed (alias of wrap_err)
            "error_context" | "wrap_err" if args.len() == 2 => {
                let r = self.check_expr(&args[0])?;
                let msg = self.check_expr(&args[1])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        if msg != Type::String {
                            return Err(TypeError::new(
                                "wrap_err / error_context prefix must be string",
                            ));
                        }
                        return Ok(r);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "wrap_err expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
            "errorf" if args.len() == 2 => {
                let fmt = self.check_expr(&args[0])?;
                let arg = self.check_expr(&args[1])?;
                if fmt != Type::String || arg != Type::String {
                    return Err(TypeError::new(
                        "errorf(fmt, arg) expects two strings (Go-like fmt.Errorf)",
                    ));
                }
                return Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)));
            }
            // error_join(a, b): if either Err, return Err with both messages; else Ok(a)
            "error_join" if args.len() == 2 => {
                let a = self.check_expr(&args[0])?;
                let b = self.check_expr(&args[1])?;
                match (&a, &b) {
                    (Type::Result(_, e1), Type::Result(_, e2))
                        if **e1 == Type::String && **e2 == Type::String =>
                    {
                        return Ok(a);
                    }
                    _ => {
                        return Err(TypeError::new(
                            "error_join expects two Result[_, string] values",
                        ));
                    }
                }
            }
            "error_tag" if args.len() == 2 => {
                let tag = self.check_expr(&args[0])?;
                let msg = self.check_expr(&args[1])?;
                if tag != Type::String || msg != Type::String {
                    return Err(TypeError::new(
                        "error_tag(tag, msg) expects two strings",
                    ));
                }
                if let Type::Result(ok, err) = &self.current_ret {
                    if **err == Type::String {
                        return Ok(Type::Result(ok.clone(), err.clone()));
                    }
                }
                return Ok(Type::Result(
                    Box::new(Type::Int),
                    Box::new(Type::String),
                ));
            }
            "error_is" if args.len() == 2 => {
                let r = self.check_expr(&args[0])?;
                let needle = self.check_expr(&args[1])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        if needle != Type::String {
                            return Err(TypeError::new(
                                "error_is needle must be string",
                            ));
                        }
                        return Ok(Type::Bool);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "error_is expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
            "error_string" if args.len() == 1 => {
                let r = self.check_expr(&args[0])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        return Ok(Type::String);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "error_string expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
        })
    }

    fn check_builtin_call_01(
        &mut self,
        name: &str,
        args: &[Expr],
    ) -> Option<Result<Type, TypeError>> {
        check_builtin_match!(name, {
            "error_unwrap" | "error_root" if args.len() == 1 => {
                let r = self.check_expr(&args[0])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        return Ok(r);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "error_unwrap/error_root expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
            "error_as_tag" if args.len() == 1 => {
                let r = self.check_expr(&args[0])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        return Ok(Type::String);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "error_as_tag expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
            "error_has_tag" if args.len() == 2 => {
                let r = self.check_expr(&args[0])?;
                let tag = self.check_expr(&args[1])?;
                match &r {
                    Type::Result(_, e) if **e == Type::String => {
                        if tag != Type::String {
                            return Err(TypeError::new(
                                "error_has_tag tag must be string",
                            ));
                        }
                        return Ok(Type::Bool);
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "error_has_tag expects Result[_, string], got {}",
                            other.display()
                        )));
                    }
                }
            }
            "dbg" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if !matches!(
                    t,
                    Type::Int
                        | Type::Int64
                        | Type::Int32
                        | Type::Int8
                        | Type::Byte
                        | Type::Bool
                ) {
                    return Err(TypeError::new(
                        "dbg(x) expects an integer (use dbg_str for strings)",
                    ));
                }
                return Ok(t);
            }
            "dbg_str" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::String {
                    return Err(TypeError::new("dbg_str expects string"));
                }
                return Ok(Type::String);
            }
            "reflect_value_of" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match &t {
                    Type::Named(n) if self.is_reflectable_struct(n) => {
                        Ok(Type::Named("ReflectValue".into()))
                    }
                    Type::Struct { name, .. } if self.is_reflectable_struct(name) => {
                        Ok(Type::Named("ReflectValue".into()))
                    }
                    _ => Err(TypeError::new(
                        "reflect_value_of expects a reflectable struct (POD leaves, \
                         nested POD, Option/Result/array/map of reflectable fields)",
                    )),
                };
            }
            "share_int" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::Int {
                    return Err(TypeError::new(format!(
                        "share_int expects int, got {}",
                        t.display()
                    )));
                }
                if let Expr::Ident(src) = &args[0] {
                    if self.shared_borrows.contains_key(src) {
                        return Err(TypeError::new(format!(
                            "cannot share `{src}` — already shared"
                        ))
                        .hint("drop the existing share first (NLL: one live share of a local)"));
                    }
                    self.shared_borrows.insert(src.clone(), true);
                }
                return Ok(Type::Named("ShareInt".into()));
            }
            "share_drop" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                let _ = t;
                if let Expr::Ident(share_name) = &args[0] {
                    self.share_vars.remove(share_name);
                    self.share_scope_depth.remove(share_name);
                    if let Some(src) = self.share_sources.remove(share_name) {
                        let still = self.share_sources.values().any(|s| s == &src);
                        if !still {
                            self.shared_borrows.remove(&src);
                        }
                    } else {
                        // Unknown mapping — clear all (legacy).
                        self.shared_borrows.clear();
                    }
                } else {
                    self.shared_borrows.clear();
                }
                return Ok(Type::Void);
            }
            "Some" if args.len() == 1 => {
                // Nested Some under Ok(Some(...)) or Option return: use expected inner.
                let expected_inner = match &self.current_expected {
                    Some(Type::Option(i)) => Some(i.as_ref().clone()),
                    _ => match &self.current_ret {
                        Type::Option(i) => Some(i.as_ref().clone()),
                        _ => None,
                    },
                };
                if let Some(inner) = expected_inner {
                    let saved_ret = self.current_ret.clone();
                    let saved_exp = self.current_expected.clone();
                    self.current_expected = Some(inner.clone());
                    if matches!(&inner, Type::Result(_, _)) {
                        self.current_ret = inner.clone();
                    }
                    let t = self.check_expr(&args[0])?;
                    self.current_ret = saved_ret;
                    self.current_expected = saved_exp;
                    if !self.compatible(&t, &inner) {
                        return Err(TypeError::new(format!(
                            "Some(...) type mismatch: expected {}, got {}",
                            inner.display(),
                            t.display()
                        )));
                    }
                    return Ok(Type::Option(Box::new(inner)));
                }
                let t = self.check_expr(&args[0])?;
                return Ok(Type::Option(Box::new(t)));
            }
            "None" if args.is_empty() => {
                if let Some(Type::Option(i)) = &self.current_expected {
                    return Ok(Type::Option(i.clone()));
                }
                if let Type::Option(i) = &self.current_ret {
                    return Ok(Type::Option(i.clone()));
                }
                return Ok(Type::Option(Box::new(Type::Int)));
            }
            "print" => {
                if args.len() != 1 {
                    return Err(TypeError::new("print takes 1 arg"));
                }
                self.check_expr(&args[0])?;
                return Ok(Type::Void);
            }
        })
    }

    fn check_builtin_call_02(
        &mut self,
        name: &str,
        args: &[Expr],
    ) -> Option<Result<Type, TypeError>> {
        check_builtin_match!(name, {
            "print_int64" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::Int64 {
                    return Err(TypeError::new(format!(
                        "print_int64 expects int64, got {}",
                        t.display()
                    ))
                    .hint("use print_int for int, or int64(x) to convert"));
                }
                return Ok(Type::Void);
            }
            "print_int32" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::Int32 {
                    return Err(TypeError::new(format!(
                        "print_int32 expects int32, got {}",
                        t.display()
                    )));
                }
                return Ok(Type::Void);
            }
            "print_int8" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::Int8 {
                    return Err(TypeError::new(format!(
                        "print_int8 expects int8, got {}",
                        t.display()
                    )));
                }
                return Ok(Type::Void);
            }
            "print_uint64" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::UInt64 {
                    return Err(TypeError::new(format!(
                        "print_uint64 expects uint64, got {}",
                        t.display()
                    )));
                }
                return Ok(Type::Void);
            }
            "print_float" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if t != Type::Float {
                    return Err(TypeError::new(format!(
                        "print_float expects float, got {}",
                        t.display()
                    )));
                }
                return Ok(Type::Void);
            }
            // Go-like conversions between integer kinds + string/[]byte
            "int64" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Int64);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to int64",
                    t.display()
                )));
            }
            "int32" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Int32);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to int32",
                    t.display()
                )));
            }
            "int8" if args.len() == 1 => {
                if let Ok(v) = try_fold_const(&args[0]) {
                    if v < -128 || v > 127 {
                        return Err(TypeError::new(format!(
                            "int8({v}) out of range -128..127"
                        )));
                    }
                }
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Int8);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to int8",
                    t.display()
                )));
            }
            "uint64" if args.len() == 1 => {
                if let Ok(v) = try_fold_const(&args[0]) {
                    if v < 0 {
                        return Err(TypeError::new(format!(
                            "uint64({v}) rejects negative constant"
                        )));
                    }
                }
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::UInt64);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to uint64",
                    t.display()
                )));
            }
            "byte" if args.len() == 1 => {
                if let Ok(v) = try_fold_const(&args[0]) {
                    if v < 0 || v > 255 {
                        return Err(TypeError::new(format!(
                            "byte({v}) out of range 0..255"
                        )));
                    }
                }
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Byte);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to byte",
                    t.display()
                )));
            }
            "int" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Int);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to int",
                    t.display()
                )));
            }
            "float" | "float64" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                if is_int_family(&t) || t == Type::Float {
                    return Ok(Type::Float);
                }
                return Err(TypeError::new(format!(
                    "cannot convert {} to float",
                    t.display()
                )));
            }
            // string([]byte) / string(int…) / string(string)
            "string" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                match t {
                    Type::Array(inner) if *inner == Type::Byte => {
                        return Ok(Type::String);
                    }
                    Type::String => return Ok(Type::String),
                    t if is_int_family(&t) => return Ok(Type::String),
                    other => {
                        return Err(TypeError::new(format!(
                            "cannot convert {} to string (need []byte, int family, or string)",
                            other.display()
                        )));
                    }
                }
            }
            // Go `[]byte(s)` — Mako uses bytes(s) (type `[]byte` is not a call)
            "bytes" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                match t {
                    Type::String => {
                        return Ok(Type::Array(Box::new(Type::Byte)));
                    }
                    Type::Array(inner) if *inner == Type::Byte => {
                        return Ok(Type::Array(Box::new(Type::Byte)));
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "bytes(...) expects string or []byte, got {}",
                            other.display()
                        )));
                    }
                }
            }
        })
    }

    fn check_builtin_call_03(
        &mut self,
        name: &str,
        args: &[Expr],
    ) -> Option<Result<Type, TypeError>> {
        check_builtin_match!(name, {
            "assert_eq" if args.len() == 2 => {
                let a = self.check_expr(&args[0])?;
                let b = if matches!(args[1], Expr::Int(_)) && is_int_family(&a) {
                    a.clone()
                } else {
                    self.check_expr(&args[1])?
                };
                let a = if matches!(args[0], Expr::Int(_)) && is_int_family(&b) {
                    b.clone()
                } else {
                    a
                };
                let ok = a == b && is_int_family(&a);
                if !ok {
                    return Err(TypeError::new(format!(
                        "assert_eq needs matching integer kinds, got {} and {}",
                        a.display(),
                        b.display()
                    )));
                }
                return Ok(Type::Void);
            }
            "copy" if args.len() == 2 => {
                let dt = self.check_expr(&args[0])?;
                let st = self.check_expr(&args[1])?;
                match (&dt, &st) {
                    (Type::Array(a), Type::Array(b)) if self.compatible(a, b) => {
                        return Ok(Type::Int);
                    }
                    (Type::Array(_), Type::Array(_)) => {
                        return Err(TypeError::new(format!(
                            "copy element type mismatch: {} vs {}",
                            dt.display(),
                            st.display()
                        )));
                    }
                    _ => {
                        return Err(TypeError::new(format!(
                            "copy needs two slices, got {} and {}",
                            dt.display(),
                            st.display()
                        )));
                    }
                }
            }
            "len" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Array(_)
                    | Type::String
                    | Type::Map(_, _)
                    | Type::StrBuilder => Ok(Type::Int),
                    Type::Named(n) if n == "string_view" => Ok(Type::Int),
                    other => Err(TypeError::new(format!(
                        "len needs slice/array/string/map/string_view, got {}",
                        other.display()
                    ))),
                };
            }
            "delete" if args.len() == 2 => {
                let mt = self.check_expr(&args[0])?;
                let kt = self.check_expr(&args[1])?;
                return match mt {
                    Type::Map(k, _) => {
                        if !self.compatible(&kt, &k) {
                            return Err(TypeError::new(format!(
                                "delete key type mismatch: expected {}, got {}",
                                k.display(),
                                kt.display()
                            )));
                        }
                        Ok(Type::Void)
                    }
                    other => Err(TypeError::new(format!(
                        "delete needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "chan_len" | "chan_cap" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Chan(_) => Ok(Type::Int),
                    other => Err(TypeError::new(format!(
                        "{} needs a channel, got {}",
                        name, other.display()
                    ))),
                };
            }
            "maps_keys" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Map(k, _) if matches!(k.as_ref(), Type::String) => {
                        Ok(Type::Array(Box::new(Type::String)))
                    }
                    Type::Map(k, _) if matches!(k.as_ref(), Type::Int) => {
                        Ok(Type::Array(Box::new(Type::Int)))
                    }
                    Type::Map(k, _) if matches!(k.as_ref(), Type::Float) => {
                        Ok(Type::Array(Box::new(Type::Float)))
                    }
                    Type::Map(k, _) if matches!(k.as_ref(), Type::Bool) => {
                        Ok(Type::Array(Box::new(Type::Bool)))
                    }
                    Type::Map(k, _) if matches!(k.as_ref(), Type::Struct { .. }) => {
                        Ok(Type::Array(k))
                    }
                    Type::Map(k, _) if matches!(k.as_ref(), Type::Enum { .. }) => {
                        Ok(Type::Array(k))
                    }
                    other => Err(TypeError::new(format!(
                        "maps_keys needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "maps_values" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Map(_, v) => Ok(Type::Array(v)),
                    other => Err(TypeError::new(format!(
                        "maps_values needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "maps_clear" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Map(_, _) => Ok(Type::Void),
                    other => Err(TypeError::new(format!(
                        "maps_clear needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "maps_clone" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Map(_, _) => Ok(t),
                    other => Err(TypeError::new(format!(
                        "maps_clone needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "maps_equal" if args.len() == 2 => {
                let a = self.check_expr(&args[0])?;
                let b = self.check_expr(&args[1])?;
                return match (&a, &b) {
                    (Type::Map(k1, v1), Type::Map(k2, v2))
                        if self.compatible(k1, k2) && self.compatible(v1, v2) =>
                    {
                        Ok(Type::Int)
                    }
                    _ => Err(TypeError::new(format!(
                        "maps_equal needs two maps of the same type, got {} and {}",
                        a.display(),
                        b.display()
                    ))),
                };
            }
            "maps_copy" if args.len() == 2 => {
                let a = self.check_expr(&args[0])?;
                let b = self.check_expr(&args[1])?;
                return match (&a, &b) {
                    (Type::Map(k1, v1), Type::Map(k2, v2))
                        if self.compatible(k1, k2) && self.compatible(v1, v2) =>
                    {
                        Ok(Type::Void)
                    }
                    _ => Err(TypeError::new(format!(
                        "maps_copy needs two maps of the same type, got {} and {}",
                        a.display(),
                        b.display()
                    ))),
                };
            }
        })
    }

    fn check_builtin_call_04(
        &mut self,
        name: &str,
        args: &[Expr],
    ) -> Option<Result<Type, TypeError>> {
        check_builtin_match!(name, {
            "has" if args.len() == 2 => {
                let mt = self.check_expr(&args[0])?;
                let kt = self.check_expr(&args[1])?;
                return match mt {
                    Type::Map(k, _) => {
                        if !self.compatible(&kt, &k) {
                            return Err(TypeError::new(format!(
                                "has key type mismatch: expected {}, got {}",
                                k.display(),
                                kt.display()
                            )));
                        }
                        Ok(Type::Bool)
                    }
                    other => Err(TypeError::new(format!(
                        "has needs a map, got {}",
                        other.display()
                    ))),
                };
            }
            "cap" if args.len() == 1 => {
                let t = self.check_expr(&args[0])?;
                return match t {
                    Type::Array(_) => Ok(Type::Int),
                    other => Err(TypeError::new(format!(
                        "cap needs slice ([]int), got {}",
                        other.display()
                    ))),
                };
            }
            "append" if args.len() == 2 => {
                let st = self.check_expr(&args[0])?;
                match st {
                    Type::Array(inner) => {
                        // Push element type so None/Some/Ok/Err match []Option / []Result.
                        let saved_expected = self.current_expected.clone();
                        self.current_expected = Some(inner.as_ref().clone());
                        let vt = if matches!(args[1], Expr::Int(_))
                            && is_literal_int_kind(inner.as_ref())
                        {
                            (*inner).clone()
                        } else {
                            self.check_expr(&args[1])?
                        };
                        self.current_expected = saved_expected;
                        if !self.compatible(&vt, &inner) {
                            return Err(TypeError::new(format!(
                                "append element type mismatch: expected {}, got {}",
                                inner.display(),
                                vt.display()
                            )));
                        }
                        return Ok(Type::Array(inner));
                    }
                    other => {
                        return Err(TypeError::new(format!(
                            "append needs slice, got {}",
                            other.display()
                        )));
                    }
                }
            }
        })
    }

    fn check_call_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Call { callee, args } => {
                if let Expr::Ident(name) = callee.as_ref() {
                    if name == "unsafe_index" && self.unsafe_depth == 0 {
                        return Err(TypeError::new(
                            "unsafe_index may only be used inside an unsafe block",
                        )
                        .hint("wrap the proven-safe access in `unsafe { ... }`"));
                    }
                    // fn_drop / fn_has_env accept any fn value (arity-independent).
                    if name == "fn_drop" {
                        if args.len() != 1 {
                            return Err(TypeError::new("fn_drop expects 1 argument"));
                        }
                        let t = self.check_expr(&args[0])?;
                        if !matches!(t, Type::Fn(_, _)) {
                            return Err(TypeError::new(format!(
                                "fn_drop expects a function value, got {}",
                                t.display()
                            )));
                        }
                        return Ok(Type::Void);
                    }
                    if name == "fn_has_env" {
                        if args.len() != 1 {
                            return Err(TypeError::new("fn_has_env expects 1 argument"));
                        }
                        let t = self.check_expr(&args[0])?;
                        if !matches!(t, Type::Fn(_, _)) {
                            return Err(TypeError::new(format!(
                                "fn_has_env expects a function value, got {}",
                                t.display()
                            )));
                        }
                        return Ok(Type::Int);
                    }
                    // task_* accept any Job[T]
                    if name == "task_done" || name == "task_joined" || name == "task_id" {
                        if args.len() != 1 {
                            return Err(TypeError::new(format!("{name} expects 1 argument")));
                        }
                        let t = self.check_expr(&args[0])?;
                        if !matches!(t, Type::Job(_)) {
                            return Err(TypeError::new(format!(
                                "{name} expects a Job, got {}",
                                t.display()
                            )));
                        }
                        return Ok(Type::Int);
                    }
                    // API stability: `#[deprecated]` is a hard error at call sites.
                    if let Some(msg) = self.deprecated_fns.get(name) {
                        let detail = if msg.is_empty() {
                            format!("call to deprecated function `{name}`")
                        } else {
                            format!("call to deprecated function `{name}`: {msg}")
                        };
                        return Err(TypeError::new(detail)
                            .hint("remove the call or drop `#[deprecated]` from the definition"));
                    }
                    // User-defined generic: monomorphize on call
                    if self.generic_fns.contains_key(name) {
                        return self.check_generic_call(name, args);
                    }
                    match name.as_str() {
                        "gc_arena_new" | "gc_alloc" | "gc_collect" | "gc_live" | "gc_enabled"
                        | "gc_root" | "gc_unroot" | "gc_link" | "gc_mark" | "gc_root_count" => {
                            return Err(TypeError::new(format!(
                                "{name} is unavailable: Mako has no garbage collector"
                            ))
                            .hint("use let/hold/share/arena for deterministic ownership"));
                        }
                        "Ok" if args.len() == 1 => {
                            // Prefer expected Result shape from current_expected (nested
                            // constructors) or function return type.
                            let expected = match &self.current_expected {
                                Some(Type::Result(ok, err)) => {
                                    Some((ok.as_ref().clone(), err.as_ref().clone()))
                                }
                                _ => match &self.current_ret {
                                    Type::Result(ok, err) => {
                                        Some((ok.as_ref().clone(), err.as_ref().clone()))
                                    }
                                    _ => None,
                                },
                            };
                            if let Some((ok, err)) = expected {
                                let saved_ret = self.current_ret.clone();
                                let saved_exp = self.current_expected.clone();
                                self.current_expected = Some(ok.clone());
                                if matches!(&ok, Type::Result(_, _)) {
                                    self.current_ret = ok.clone();
                                }
                                let t = self.check_expr(&args[0])?;
                                self.current_ret = saved_ret;
                                self.current_expected = saved_exp;
                                if !self.compatible(&t, &ok) {
                                    return Err(TypeError::new(format!(
                                        "Ok(...) type mismatch: expected {}, got {}",
                                        ok.display(),
                                        t.display()
                                    )));
                                }
                                return Ok(Type::Result(Box::new(ok), Box::new(err)));
                            }
                            let t = self.check_expr(&args[0])?;
                            return Ok(Type::Result(Box::new(t), Box::new(Type::String)));
                        }
                        "Err" if args.len() == 1 => {
                            let expected = match &self.current_expected {
                                Some(Type::Result(ok, err)) => {
                                    Some((ok.as_ref().clone(), err.as_ref().clone()))
                                }
                                _ => match &self.current_ret {
                                    Type::Result(ok, err) => {
                                        Some((ok.as_ref().clone(), err.as_ref().clone()))
                                    }
                                    _ => None,
                                },
                            };
                            if let Some((ok, err)) = expected {
                                let e = self.check_expr(&args[0])?;
                                if !self.compatible(&e, &err) {
                                    return Err(TypeError::new(format!(
                                        "Err(...) type mismatch: expected {}, got {}",
                                        err.display(),
                                        e.display()
                                    )));
                                }
                                return Ok(Type::Result(Box::new(ok), Box::new(err)));
                            }
                            let e = self.check_expr(&args[0])?;
                            return Ok(Type::Result(Box::new(Type::Int), Box::new(e)));
                        }
                        "error" if args.len() == 1 => {
                            let e = self.check_expr(&args[0])?;
                            if e != Type::String {
                                return Err(TypeError::new("error(...) expects a string message"));
                            }
                            return Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)));
                        }
                        // error_context(r, msg) — richer error seed (alias of wrap_err)
                        "error_context" | "wrap_err" if args.len() == 2 => {
                            let r = self.check_expr(&args[0])?;
                            let msg = self.check_expr(&args[1])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    if msg != Type::String {
                                        return Err(TypeError::new(
                                            "wrap_err / error_context prefix must be string",
                                        ));
                                    }
                                    return Ok(r);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "wrap_err expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "errorf" if args.len() == 2 => {
                            let fmt = self.check_expr(&args[0])?;
                            let arg = self.check_expr(&args[1])?;
                            if fmt != Type::String || arg != Type::String {
                                return Err(TypeError::new(
                                    "errorf(fmt, arg) expects two strings (Go-like fmt.Errorf)",
                                ));
                            }
                            return Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)));
                        }
                        // error_join(a, b): if either Err, return Err with both messages; else Ok(a)
                        "error_join" if args.len() == 2 => {
                            let a = self.check_expr(&args[0])?;
                            let b = self.check_expr(&args[1])?;
                            match (&a, &b) {
                                (Type::Result(_, e1), Type::Result(_, e2))
                                    if **e1 == Type::String && **e2 == Type::String =>
                                {
                                    return Ok(a);
                                }
                                _ => {
                                    return Err(TypeError::new(
                                        "error_join expects two Result[_, string] values",
                                    ));
                                }
                            }
                        }
                        "error_tag" if args.len() == 2 => {
                            let tag = self.check_expr(&args[0])?;
                            let msg = self.check_expr(&args[1])?;
                            if tag != Type::String || msg != Type::String {
                                return Err(TypeError::new(
                                    "error_tag(tag, msg) expects two strings",
                                ));
                            }
                            if let Type::Result(ok, err) = &self.current_ret {
                                if **err == Type::String {
                                    return Ok(Type::Result(ok.clone(), err.clone()));
                                }
                            }
                            return Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)));
                        }
                        "error_is" if args.len() == 2 => {
                            let r = self.check_expr(&args[0])?;
                            let needle = self.check_expr(&args[1])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    if needle != Type::String {
                                        return Err(TypeError::new(
                                            "error_is needle must be string",
                                        ));
                                    }
                                    return Ok(Type::Bool);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "error_is expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "error_string" if args.len() == 1 => {
                            let r = self.check_expr(&args[0])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    return Ok(Type::String);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "error_string expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "error_unwrap" | "error_root" if args.len() == 1 => {
                            let r = self.check_expr(&args[0])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    return Ok(r);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "error_unwrap/error_root expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "error_as_tag" if args.len() == 1 => {
                            let r = self.check_expr(&args[0])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    return Ok(Type::String);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "error_as_tag expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "error_has_tag" if args.len() == 2 => {
                            let r = self.check_expr(&args[0])?;
                            let tag = self.check_expr(&args[1])?;
                            match &r {
                                Type::Result(_, e) if **e == Type::String => {
                                    if tag != Type::String {
                                        return Err(TypeError::new(
                                            "error_has_tag tag must be string",
                                        ));
                                    }
                                    return Ok(Type::Bool);
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "error_has_tag expects Result[_, string], got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "dbg" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if !matches!(
                                t,
                                Type::Int
                                    | Type::Int64
                                    | Type::Int32
                                    | Type::Int8
                                    | Type::Byte
                                    | Type::Bool
                            ) {
                                return Err(TypeError::new(
                                    "dbg(x) expects an integer (use dbg_str for strings)",
                                ));
                            }
                            return Ok(t);
                        }
                        "dbg_str" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::String {
                                return Err(TypeError::new("dbg_str expects string"));
                            }
                            return Ok(Type::String);
                        }
                        "reflect_value_of" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match &t {
                                Type::Named(n) if self.is_reflectable_struct(n) => {
                                    Ok(Type::Named("ReflectValue".into()))
                                }
                                Type::Struct { name, .. } if self.is_reflectable_struct(name) => {
                                    Ok(Type::Named("ReflectValue".into()))
                                }
                                _ => Err(TypeError::new(
                                    "reflect_value_of expects a reflectable struct (POD leaves, \
                                     nested POD, Option/Result/array/map of reflectable fields)",
                                )),
                            };
                        }
                        "share_int" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::Int {
                                return Err(TypeError::new(format!(
                                    "share_int expects int, got {}",
                                    t.display()
                                )));
                            }
                            if let Expr::Ident(src) = &args[0] {
                                if self.shared_borrows.contains_key(src) {
                                    return Err(TypeError::new(format!(
                                        "cannot share `{src}` — already shared"
                                    ))
                                    .hint("drop the existing share first (NLL: one live share of a local)"));
                                }
                                self.shared_borrows.insert(src.clone(), true);
                            }
                            return Ok(Type::Named("ShareInt".into()));
                        }
                        "share_drop" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            let _ = t;
                            if let Expr::Ident(share_name) = &args[0] {
                                self.share_vars.remove(share_name);
                                self.share_scope_depth.remove(share_name);
                                if let Some(src) = self.share_sources.remove(share_name) {
                                    let still = self.share_sources.values().any(|s| s == &src);
                                    if !still {
                                        self.shared_borrows.remove(&src);
                                    }
                                } else {
                                    // Unknown mapping — clear all (legacy).
                                    self.shared_borrows.clear();
                                }
                            } else {
                                self.shared_borrows.clear();
                            }
                            return Ok(Type::Void);
                        }
                        "Some" if args.len() == 1 => {
                            // Nested Some under Ok(Some(...)) or Option return: use expected inner.
                            let expected_inner = match &self.current_expected {
                                Some(Type::Option(i)) => Some(i.as_ref().clone()),
                                _ => match &self.current_ret {
                                    Type::Option(i) => Some(i.as_ref().clone()),
                                    _ => None,
                                },
                            };
                            if let Some(inner) = expected_inner {
                                let saved_ret = self.current_ret.clone();
                                let saved_exp = self.current_expected.clone();
                                self.current_expected = Some(inner.clone());
                                if matches!(&inner, Type::Result(_, _)) {
                                    self.current_ret = inner.clone();
                                }
                                let t = self.check_expr(&args[0])?;
                                self.current_ret = saved_ret;
                                self.current_expected = saved_exp;
                                if !self.compatible(&t, &inner) {
                                    return Err(TypeError::new(format!(
                                        "Some(...) type mismatch: expected {}, got {}",
                                        inner.display(),
                                        t.display()
                                    )));
                                }
                                return Ok(Type::Option(Box::new(inner)));
                            }
                            let t = self.check_expr(&args[0])?;
                            return Ok(Type::Option(Box::new(t)));
                        }
                        "None" if args.is_empty() => {
                            if let Some(Type::Option(i)) = &self.current_expected {
                                return Ok(Type::Option(i.clone()));
                            }
                            if let Type::Option(i) = &self.current_ret {
                                return Ok(Type::Option(i.clone()));
                            }
                            return Ok(Type::Option(Box::new(Type::Int)));
                        }
                        "print" => {
                            if args.len() != 1 {
                                return Err(TypeError::new("print takes 1 arg"));
                            }
                            self.check_expr(&args[0])?;
                            return Ok(Type::Void);
                        }
                        "print_int64" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::Int64 {
                                return Err(TypeError::new(format!(
                                    "print_int64 expects int64, got {}",
                                    t.display()
                                ))
                                .hint("use print_int for int, or int64(x) to convert"));
                            }
                            return Ok(Type::Void);
                        }
                        "print_int32" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::Int32 {
                                return Err(TypeError::new(format!(
                                    "print_int32 expects int32, got {}",
                                    t.display()
                                )));
                            }
                            return Ok(Type::Void);
                        }
                        "print_int8" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::Int8 {
                                return Err(TypeError::new(format!(
                                    "print_int8 expects int8, got {}",
                                    t.display()
                                )));
                            }
                            return Ok(Type::Void);
                        }
                        "print_uint64" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::UInt64 {
                                return Err(TypeError::new(format!(
                                    "print_uint64 expects uint64, got {}",
                                    t.display()
                                )));
                            }
                            return Ok(Type::Void);
                        }
                        "print_float" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if t != Type::Float {
                                return Err(TypeError::new(format!(
                                    "print_float expects float, got {}",
                                    t.display()
                                )));
                            }
                            return Ok(Type::Void);
                        }
                        // Go-like conversions between integer kinds + string/[]byte
                        "int64" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Int64);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to int64",
                                t.display()
                            )));
                        }
                        "int32" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Int32);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to int32",
                                t.display()
                            )));
                        }
                        "int8" if args.len() == 1 => {
                            if let Ok(v) = try_fold_const(&args[0]) {
                                if v < -128 || v > 127 {
                                    return Err(TypeError::new(format!(
                                        "int8({v}) out of range -128..127"
                                    )));
                                }
                            }
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Int8);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to int8",
                                t.display()
                            )));
                        }
                        "uint64" if args.len() == 1 => {
                            if let Ok(v) = try_fold_const(&args[0]) {
                                if v < 0 {
                                    return Err(TypeError::new(format!(
                                        "uint64({v}) rejects negative constant"
                                    )));
                                }
                            }
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::UInt64);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to uint64",
                                t.display()
                            )));
                        }
                        "byte" if args.len() == 1 => {
                            if let Ok(v) = try_fold_const(&args[0]) {
                                if v < 0 || v > 255 {
                                    return Err(TypeError::new(format!(
                                        "byte({v}) out of range 0..255"
                                    )));
                                }
                            }
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Byte);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to byte",
                                t.display()
                            )));
                        }
                        "int" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Int);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to int",
                                t.display()
                            )));
                        }
                        "float" | "float64" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            if is_int_family(&t) || t == Type::Float {
                                return Ok(Type::Float);
                            }
                            return Err(TypeError::new(format!(
                                "cannot convert {} to float",
                                t.display()
                            )));
                        }
                        // string([]byte) / string(int…) / string(string)
                        "string" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            match t {
                                Type::Array(inner) if *inner == Type::Byte => {
                                    return Ok(Type::String);
                                }
                                Type::String => return Ok(Type::String),
                                t if is_int_family(&t) => return Ok(Type::String),
                                other => {
                                    return Err(TypeError::new(format!(
                                        "cannot convert {} to string (need []byte, int family, or string)",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        // Go `[]byte(s)` — Mako uses bytes(s) (type `[]byte` is not a call)
                        "bytes" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            match t {
                                Type::String => {
                                    return Ok(Type::Array(Box::new(Type::Byte)));
                                }
                                Type::Array(inner) if *inner == Type::Byte => {
                                    return Ok(Type::Array(Box::new(Type::Byte)));
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "bytes(...) expects string or []byte, got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        "assert_eq" if args.len() == 2 => {
                            let a = self.check_expr(&args[0])?;
                            let b = if matches!(args[1], Expr::Int(_)) && is_int_family(&a) {
                                a.clone()
                            } else {
                                self.check_expr(&args[1])?
                            };
                            let a = if matches!(args[0], Expr::Int(_)) && is_int_family(&b) {
                                b.clone()
                            } else {
                                a
                            };
                            let ok = a == b && is_int_family(&a);
                            if !ok {
                                return Err(TypeError::new(format!(
                                    "assert_eq needs matching integer kinds, got {} and {}",
                                    a.display(),
                                    b.display()
                                )));
                            }
                            return Ok(Type::Void);
                        }
                        "copy" if args.len() == 2 => {
                            let dt = self.check_expr(&args[0])?;
                            let st = self.check_expr(&args[1])?;
                            match (&dt, &st) {
                                (Type::Array(a), Type::Array(b)) if self.compatible(a, b) => {
                                    return Ok(Type::Int);
                                }
                                (Type::Array(_), Type::Array(_)) => {
                                    return Err(TypeError::new(format!(
                                        "copy element type mismatch: {} vs {}",
                                        dt.display(),
                                        st.display()
                                    )));
                                }
                                _ => {
                                    return Err(TypeError::new(format!(
                                        "copy needs two slices, got {} and {}",
                                        dt.display(),
                                        st.display()
                                    )));
                                }
                            }
                        }
                        "len" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Array(_)
                                | Type::String
                                | Type::Map(_, _)
                                | Type::StrBuilder => Ok(Type::Int),
                                Type::Named(n) if n == "string_view" => Ok(Type::Int),
                                other => Err(TypeError::new(format!(
                                    "len needs slice/array/string/map/string_view, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "delete" if args.len() == 2 => {
                            let mt = self.check_expr(&args[0])?;
                            let kt = self.check_expr(&args[1])?;
                            return match mt {
                                Type::Map(k, _) => {
                                    if !self.compatible(&kt, &k) {
                                        return Err(TypeError::new(format!(
                                            "delete key type mismatch: expected {}, got {}",
                                            k.display(),
                                            kt.display()
                                        )));
                                    }
                                    Ok(Type::Void)
                                }
                                other => Err(TypeError::new(format!(
                                    "delete needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "chan_len" | "chan_cap" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Chan(_) => Ok(Type::Int),
                                other => Err(TypeError::new(format!(
                                    "{} needs a channel, got {}",
                                    name,
                                    other.display()
                                ))),
                            };
                        }
                        "maps_keys" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Map(k, _) if matches!(k.as_ref(), Type::String) => {
                                    Ok(Type::Array(Box::new(Type::String)))
                                }
                                Type::Map(k, _) if matches!(k.as_ref(), Type::Int) => {
                                    Ok(Type::Array(Box::new(Type::Int)))
                                }
                                Type::Map(k, _) if matches!(k.as_ref(), Type::Float) => {
                                    Ok(Type::Array(Box::new(Type::Float)))
                                }
                                Type::Map(k, _) if matches!(k.as_ref(), Type::Bool) => {
                                    Ok(Type::Array(Box::new(Type::Bool)))
                                }
                                Type::Map(k, _) if matches!(k.as_ref(), Type::Struct { .. }) => {
                                    Ok(Type::Array(k))
                                }
                                Type::Map(k, _) if matches!(k.as_ref(), Type::Enum { .. }) => {
                                    Ok(Type::Array(k))
                                }
                                other => Err(TypeError::new(format!(
                                    "maps_keys needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "maps_values" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Map(_, v) => Ok(Type::Array(v)),
                                other => Err(TypeError::new(format!(
                                    "maps_values needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "maps_clear" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Map(_, _) => Ok(Type::Void),
                                other => Err(TypeError::new(format!(
                                    "maps_clear needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "maps_clone" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Map(_, _) => Ok(t),
                                other => Err(TypeError::new(format!(
                                    "maps_clone needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "maps_equal" if args.len() == 2 => {
                            let a = self.check_expr(&args[0])?;
                            let b = self.check_expr(&args[1])?;
                            return match (&a, &b) {
                                (Type::Map(k1, v1), Type::Map(k2, v2))
                                    if self.compatible(k1, k2) && self.compatible(v1, v2) =>
                                {
                                    Ok(Type::Int)
                                }
                                _ => Err(TypeError::new(format!(
                                    "maps_equal needs two maps of the same type, got {} and {}",
                                    a.display(),
                                    b.display()
                                ))),
                            };
                        }
                        "maps_copy" if args.len() == 2 => {
                            let a = self.check_expr(&args[0])?;
                            let b = self.check_expr(&args[1])?;
                            return match (&a, &b) {
                                (Type::Map(k1, v1), Type::Map(k2, v2))
                                    if self.compatible(k1, k2) && self.compatible(v1, v2) =>
                                {
                                    Ok(Type::Void)
                                }
                                _ => Err(TypeError::new(format!(
                                    "maps_copy needs two maps of the same type, got {} and {}",
                                    a.display(),
                                    b.display()
                                ))),
                            };
                        }
                        "has" if args.len() == 2 => {
                            let mt = self.check_expr(&args[0])?;
                            let kt = self.check_expr(&args[1])?;
                            return match mt {
                                Type::Map(k, _) => {
                                    if !self.compatible(&kt, &k) {
                                        return Err(TypeError::new(format!(
                                            "has key type mismatch: expected {}, got {}",
                                            k.display(),
                                            kt.display()
                                        )));
                                    }
                                    Ok(Type::Bool)
                                }
                                other => Err(TypeError::new(format!(
                                    "has needs a map, got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "cap" if args.len() == 1 => {
                            let t = self.check_expr(&args[0])?;
                            return match t {
                                Type::Array(_) => Ok(Type::Int),
                                other => Err(TypeError::new(format!(
                                    "cap needs slice ([]int), got {}",
                                    other.display()
                                ))),
                            };
                        }
                        "append" if args.len() == 2 => {
                            let st = self.check_expr(&args[0])?;
                            match st {
                                Type::Array(inner) => {
                                    // Push element type so None/Some/Ok/Err match []Option / []Result.
                                    let saved_expected = self.current_expected.clone();
                                    self.current_expected = Some(inner.as_ref().clone());
                                    let vt = if matches!(args[1], Expr::Int(_))
                                        && is_literal_int_kind(inner.as_ref())
                                    {
                                        (*inner).clone()
                                    } else {
                                        self.check_expr(&args[1])?
                                    };
                                    self.current_expected = saved_expected;
                                    if !self.compatible(&vt, &inner) {
                                        return Err(TypeError::new(format!(
                                            "append element type mismatch: expected {}, got {}",
                                            inner.display(),
                                            vt.display()
                                        )));
                                    }
                                    return Ok(Type::Array(inner));
                                }
                                other => {
                                    return Err(TypeError::new(format!(
                                        "append needs slice, got {}",
                                        other.display()
                                    )));
                                }
                            }
                        }
                        _ => {}
                    }
                    // Try qualified lookup first using return type context
                    let qualified_ctor = if let Type::Named(ret_name) = &self.current_ret {
                        let qname = format!("{ret_name}::{name}");
                        self.variants.get(&qname).cloned()
                    } else {
                        None
                    };
                    if let Some(ctor) = qualified_ctor.or_else(|| self.variants.get(name).cloned())
                    {
                        if ctor.fields.len() != args.len() {
                            return Err(TypeError::new(format!(
                                "variant `{name}` expects {} fields, got {}",
                                ctor.fields.len(),
                                args.len()
                            )));
                        }
                        for (ft, a) in ctor.fields.iter().zip(args) {
                            let at = self.check_expr(a)?;
                            if !self.compatible(&at, ft) {
                                return Err(TypeError::new(format!(
                                    "variant field type mismatch: expected {}, got {}",
                                    ft.display(),
                                    at.display()
                                )));
                            }
                        }
                        let enum_ty =
                            self.types.get(&ctor.enum_name).cloned().ok_or_else(|| {
                                TypeError::new(format!("unknown enum {}", ctor.enum_name))
                            })?;
                        return Ok(enum_ty);
                    }
                }
                let ft = self.check_expr(callee)?;
                match ft {
                    Type::Fn(params, ret) => {
                        if params.len() != args.len() {
                            let callee_name = match callee.as_ref() {
                                Expr::Ident(name) => name.clone(),
                                _ => "<expression>".to_owned(),
                            };
                            return Err(TypeError::new(format!(
                                "call to `{}`: expected {} args, got {}",
                                callee_name,
                                params.len(),
                                args.len()
                            )));
                        }
                        // Borrowck: cannot pass a live-shared local into a `mut` parameter.
                        if let Expr::Ident(fname) = callee.as_ref() {
                            if let Some(muts) = self.fn_mut_params.get(fname).cloned() {
                                for (i, a) in args.iter().enumerate() {
                                    if muts.get(i).copied().unwrap_or(false) {
                                        if let Expr::Ident(an) = a {
                                            if self.shared_borrows.contains_key(an) {
                                                return Err(TypeError::new(format!(
                                                    "cannot pass `{an}` to mut parameter while shared"
                                                ))
                                                .hint("drop the share first, or pass a copy"));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        for (p, a) in params.iter().zip(args) {
                            let at = if matches!(a, Expr::Int(_)) && is_literal_int_kind(p) {
                                p.clone()
                            } else {
                                // Push expected type so lambdas / constructors refine.
                                let saved = self.current_expected.clone();
                                self.current_expected = Some(p.clone());
                                let t = self.check_expr(a)?;
                                self.current_expected = saved;
                                t
                            };
                            if !self.compatible(&at, p) {
                                return Err(TypeError::new(format!(
                                    "argument type mismatch: expected {}, got {}",
                                    p.display(),
                                    at.display()
                                ))
                                .hint("integer kinds are distinct — convert with int64(x) / int32(x) / int(x)"));
                            }
                        }
                        Ok(*ret)
                    }
                    other => Err(TypeError::new(format!("cannot call {}", other.display()))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_method_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Method {
                receiver,
                method,
                args,
            } => {
                // Import alias: `foo.bar(...)` → `foo__bar(...)` when that fn exists.
                if let Expr::Ident(alias) = receiver.as_ref() {
                    let mangled = format!("{alias}__{method}");
                    if self.fns.contains_key(&mangled) {
                        return self.check_expr(&Expr::Call {
                            callee: Box::new(Expr::Ident(mangled)),
                            args: args.clone(),
                        });
                    }
                    // Pack-qualified variant: `eng.Green(7)` → same as bare `Green(7)`.
                    if self.pack_variant_ctor(alias, method).is_some() {
                        return self.check_expr(&Expr::Call {
                            callee: Box::new(Expr::Ident(method.clone())),
                            args: args.clone(),
                        });
                    }
                }
                // Type-qualified pack path: `eng.Color.Green(7)`.
                if let Expr::Field {
                    base,
                    field: enum_short,
                } = receiver.as_ref()
                {
                    if let Expr::Ident(alias) = base.as_ref() {
                        if let Some(enum_name) = self.pack_enum_type_name(alias, enum_short) {
                            if let Some(ctor) = self.variants.get(method) {
                                if ctor.enum_name == enum_name {
                                    return self.check_expr(&Expr::Call {
                                        callee: Box::new(Expr::Ident(method.clone())),
                                        args: args.clone(),
                                    });
                                }
                            }
                        }
                    }
                }
                let rt = self.check_expr(receiver)?;
                // Typed methods check args themselves; others typecheck args once here.
                let typed_send = matches!(
                    (&rt, method.as_str()),
                    (Type::Chan(_), "send")
                        | (Type::Queue(_), "publish" | "push")
                        | (
                            Type::Graphql,
                            "has" | "has_field" | "arg" | "data" | "error"
                        )
                );
                let mut arg_tys = Vec::new();
                if !typed_send {
                    for a in args {
                        arg_tys.push(self.check_expr(a)?);
                    }
                }
                match (&rt, method.as_str()) {
                    (Type::Array(_), "len") => Ok(Type::Int),
                    (Type::String, "len") => Ok(Type::Int),
                    (Type::Chan(inner), "send") => {
                        if args.len() != 1 {
                            return Err(
                                TypeError::new("chan.send takes 1 value").hint("ch.send(x)")
                            );
                        }
                        let at = self.check_expr(&args[0])?;
                        if !self.compatible(&at, inner) {
                            return Err(TypeError::new(format!(
                                "chan.send type mismatch: channel is chan[{}], got {}",
                                inner.display(),
                                at.display()
                            )));
                        }
                        Ok(Type::Bool)
                    }
                    (Type::Chan(inner), "recv") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("chan.recv takes no arguments"));
                        }
                        Ok(*inner.clone())
                    }
                    (Type::Chan(_), "close") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("chan.close takes no arguments"));
                        }
                        Ok(Type::Void)
                    }
                    // queue[T] methods (language-level message queue)
                    (Type::Queue(inner), "publish" | "push") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("queue.publish takes 1 value")
                                .hint("q.publish(x)"));
                        }
                        let at = self.check_expr(&args[0])?;
                        if !self.compatible(&at, inner) {
                            return Err(TypeError::new(format!(
                                "queue.publish type mismatch: queue is queue[{}], got {}",
                                inner.display(),
                                at.display()
                            )));
                        }
                        Ok(Type::Int) // 1 ok / 0 full
                    }
                    (Type::Queue(inner), "try_take" | "take") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("queue.try_take takes no arguments"));
                        }
                        Ok(*inner.clone())
                    }
                    (Type::Queue(_), "len") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("queue.len takes no arguments"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Queue(_), "free" | "close") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("queue.free takes no arguments"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Queue(_), "purge") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("queue.purge takes no arguments"));
                        }
                        Ok(Type::Int)
                    }
                    // Graphql document methods
                    (Type::Graphql, "query") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("graphql.query takes no arguments"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Graphql, "fields") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("graphql.fields takes no arguments"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Graphql, "has" | "has_field") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("graphql.has takes 1 field name"));
                        }
                        let at = self.check_expr(&args[0])?;
                        if at != Type::String {
                            return Err(TypeError::new("graphql.has expects string field name"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Graphql, "arg") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("graphql.arg takes 1 arg name"));
                        }
                        let at = self.check_expr(&args[0])?;
                        if at != Type::String {
                            return Err(TypeError::new("graphql.arg expects string"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Graphql, "data") => {
                        if args.len() != 2 {
                            return Err(TypeError::new("graphql.data(field, json)")
                                .hint("g.data(\"health\", \"{\\\"ok\\\":true}\")"));
                        }
                        let a0 = self.check_expr(&args[0])?;
                        let a1 = self.check_expr(&args[1])?;
                        if a0 != Type::String || a1 != Type::String {
                            return Err(TypeError::new("graphql.data expects two strings"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Graphql, "error") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("graphql.error takes 1 message"));
                        }
                        let at = self.check_expr(&args[0])?;
                        if at != Type::String {
                            return Err(TypeError::new("graphql.error expects string"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Graphql, "is_query" | "is_mutation") => {
                        if !args.is_empty() {
                            return Err(TypeError::new(format!(
                                "graphql.{method} takes no arguments"
                            )));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Crew, "cancel") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("crew.cancel takes no arguments"));
                        }
                        Ok(Type::Void)
                    }
                    (Type::Crew, "drain") => {
                        if args.len() != 1 {
                            return Err(TypeError::new(
                                "crew.drain(timeout_ms) takes one argument",
                            ));
                        }
                        let t = self.check_expr(&args[0])?;
                        if !matches!(t, Type::Int | Type::Int64 | Type::Int32) {
                            return Err(TypeError::new("crew.drain expects int timeout_ms"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Crew, "cancelled") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("crew.cancelled takes no arguments"));
                        }
                        Ok(Type::Bool)
                    }
                    (Type::Crew, "err_count") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("crew.err_count takes no arguments"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Crew, "first_err") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("crew.first_err takes no arguments"));
                        }
                        Ok(Type::String)
                    }
                    (Type::Crew, "wait") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("crew.wait takes no arguments"));
                        }
                        // Ok(err_count) or Err(first child error message)
                        Ok(Type::Result(Box::new(Type::Int), Box::new(Type::String)))
                    }
                    (Type::Job(inner), "join") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("job.join takes no arguments")
                                .hint("use join_timeout(ms) for a deadline"));
                        }
                        Ok(*inner.clone())
                    }
                    (Type::Job(inner), "join_timeout") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("job.join_timeout takes milliseconds"));
                        }
                        let _ = self.check_expr(&args[0])?;
                        // Ok(value) or Err("timeout"). When the job returns Result[T, E]
                        // with string Err, flatten so timeout is Err("timeout") at the
                        // same type (no Result[Result[…], string] nest).
                        match inner.as_ref() {
                            Type::Result(ok, err) if matches!(err.as_ref(), Type::String) => {
                                Ok(Type::Result(ok.clone(), err.clone()))
                            }
                            other => Ok(Type::Result(
                                Box::new(other.clone()),
                                Box::new(Type::String),
                            )),
                        }
                    }
                    (Type::Job(inner), "join_deadline") => {
                        if args.len() != 1 {
                            return Err(TypeError::new(
                                "job.join_deadline takes a mono deadline (from deadline_ms/ns)",
                            ));
                        }
                        let _ = self.check_expr(&args[0])?;
                        match inner.as_ref() {
                            Type::Result(ok, err) if matches!(err.as_ref(), Type::String) => {
                                Ok(Type::Result(ok.clone(), err.clone()))
                            }
                            other => Ok(Type::Result(
                                Box::new(other.clone()),
                                Box::new(Type::String),
                            )),
                        }
                    }
                    (Type::Chan(elem), "send_timeout") => {
                        if args.len() != 2 {
                            return Err(TypeError::new(
                                "ch.send_timeout(val, timeout_ms) takes value and ms",
                            ));
                        }
                        let vt = self.check_expr(&args[0])?;
                        let mt = self.check_expr(&args[1])?;
                        if !self.compatible(&vt, elem) {
                            return Err(TypeError::new(format!(
                                "send_timeout value: expected {}, got {}",
                                elem.display(),
                                vt.display()
                            )));
                        }
                        if !matches!(mt, Type::Int | Type::Int64 | Type::Int32) {
                            return Err(TypeError::new("send_timeout ms must be int"));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Chan(elem), "recv_timeout") => {
                        if args.len() != 1 {
                            return Err(TypeError::new(
                                "ch.recv_timeout(timeout_ms) takes milliseconds",
                            ));
                        }
                        let mt = self.check_expr(&args[0])?;
                        if !matches!(mt, Type::Int | Type::Int64 | Type::Int32) {
                            return Err(TypeError::new("recv_timeout ms must be int"));
                        }
                        // Ok(value) / Err("timeout"|"closed")
                        Ok(Type::Result(
                            Box::new(elem.as_ref().clone()),
                            Box::new(Type::String),
                        ))
                    }
                    (Type::Chan(elem), "try_send") => {
                        if args.len() != 1 {
                            return Err(TypeError::new("ch.try_send(val) takes one value"));
                        }
                        let vt = self.check_expr(&args[0])?;
                        if !self.compatible(&vt, elem) {
                            return Err(TypeError::new(format!(
                                "try_send value: expected {}, got {}",
                                elem.display(),
                                vt.display()
                            )));
                        }
                        Ok(Type::Int)
                    }
                    (Type::Chan(elem), "try_recv") => {
                        if !args.is_empty() {
                            return Err(TypeError::new("ch.try_recv() takes no arguments"));
                        }
                        Ok(Type::Result(
                            Box::new(elem.as_ref().clone()),
                            Box::new(Type::String),
                        ))
                    }
                    (Type::Interface { name: iname }, m) => {
                        let Some(iface) = self.interfaces.iter().find(|i| i.name == *iname) else {
                            return Err(TypeError::new(format!("unknown interface `{iname}`")));
                        };
                        let Some((_, params, ret)) = iface.methods.iter().find(|(n, _, _)| n == m)
                        else {
                            return Err(TypeError::new(format!(
                                "interface `{iname}` has no method `{m}`"
                            )));
                        };
                        if arg_tys.len() != params.len() {
                            return Err(TypeError::new(format!(
                                "interface method `{iname}.{m}` expects {} args, got {}",
                                params.len(),
                                arg_tys.len()
                            )));
                        }
                        for (i, (at, pt)) in arg_tys.iter().zip(params.iter()).enumerate() {
                            let exp = self.resolve_type(pt)?;
                            if !self.compatible(at, &exp) {
                                return Err(TypeError::new(format!(
                                    "interface method `{iname}.{m}` arg {i}: expected {}, got {}",
                                    exp.display(),
                                    at.display()
                                )));
                            }
                        }
                        Ok(self.resolve_type(ret)?)
                    }
                    (other, m) => {
                        // Struct / enum associated method: `Point_distance(self, …)` → `p.distance(…)`
                        // Also covers `on Point { fn distance(self) … }` desugar.
                        let type_name = match other {
                            Type::Enum { name, .. } | Type::Struct { name, .. } => {
                                Some(name.as_str())
                            }
                            Type::Named(n) if self.types.contains_key(n) => Some(n.as_str()),
                            _ => None,
                        };
                        if let Some(tname) = type_name {
                            let key = format!("{tname}_{m}");
                            if let Some(Type::Fn(fp, fr)) = self.fns.get(&key).cloned() {
                                let has_self =
                                    !fp.is_empty() && self.types_equal_iface_self(&fp[0], other);
                                let arg_expected = if has_self {
                                    fp[1..].to_vec()
                                } else {
                                    fp.clone()
                                };
                                if arg_tys.len() != arg_expected.len() {
                                    return Err(TypeError::new(format!(
                                        "method `{tname}.{m}` expects {} args, got {}",
                                        arg_expected.len(),
                                        arg_tys.len()
                                    )));
                                }
                                for (i, (at, exp)) in
                                    arg_tys.iter().zip(arg_expected.iter()).enumerate()
                                {
                                    if !self.compatible(at, exp) {
                                        return Err(TypeError::new(format!(
                                            "method `{tname}.{m}` arg {i}: expected {}, got {}",
                                            exp.display(),
                                            at.display()
                                        )));
                                    }
                                }
                                return Ok(*fr);
                            }
                        }
                        // Interface method sugar:
                        //   `recv.write(s)` → `Writer_write(s)` (no self)
                        //   or `Writer_write(self, s)` when impl takes receiver first.
                        for iface in &self.interfaces.clone() {
                            if let Some((_, params, ret)) =
                                iface.methods.iter().find(|(n, _, _)| n == m)
                            {
                                let mut key = format!("{}_{}", iface.name, m);
                                if let Type::Struct { name: sn, .. } = other {
                                    let alt = format!("{}_{}_{}", iface.name, sn, m);
                                    if self.fns.contains_key(&alt) {
                                        key = alt;
                                    }
                                }
                                if let Some(Type::Fn(fp, _fr)) = self.fns.get(&key).cloned() {
                                    let expected: Result<Vec<_>, _> =
                                        params.iter().map(|t| self.resolve_type(t)).collect();
                                    let expected = expected?;
                                    // Prefer concrete impl signature when it has self.
                                    let has_self = fp.len() == arg_tys.len() + 1;
                                    let (self_ty, arg_expected) = if has_self {
                                        (Some(fp[0].clone()), fp[1..].to_vec())
                                    } else if fp.len() == expected.len()
                                        && arg_tys.len() == expected.len()
                                    {
                                        (None, expected.clone())
                                    } else if expected.len() == arg_tys.len() {
                                        (None, expected)
                                    } else {
                                        return Err(TypeError::new(format!(
                                            "interface method `{}.{}` expects {} args, got {}",
                                            iface.name,
                                            m,
                                            expected.len(),
                                            arg_tys.len()
                                        )));
                                    };
                                    if let Some(st) = self_ty {
                                        if !self.compatible(other, &st) {
                                            return Err(TypeError::new(format!(
                                                "interface method `{}.{}` self: expected {}, got {}",
                                                iface.name,
                                                m,
                                                st.display(),
                                                other.display()
                                            )));
                                        }
                                    }
                                    if arg_tys.len() != arg_expected.len() {
                                        return Err(TypeError::new(format!(
                                            "interface method `{}.{}` expects {} args, got {}",
                                            iface.name,
                                            m,
                                            arg_expected.len(),
                                            arg_tys.len()
                                        )));
                                    }
                                    for (i, (at, exp)) in
                                        arg_tys.iter().zip(arg_expected.iter()).enumerate()
                                    {
                                        if !self.compatible(at, exp) {
                                            return Err(TypeError::new(format!(
                                                "interface method `{}.{}` arg {i}: expected {}, got {}",
                                                iface.name,
                                                m,
                                                exp.display(),
                                                at.display()
                                            )));
                                        }
                                    }
                                    return Ok(self.resolve_type(ret)?);
                                }
                            }
                        }
                        Err(TypeError::new(format!(
                            "unknown method `{m}` on {}",
                            other.display()
                        )))
                    }
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_index_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Index { base, index } => {
                let bt = self.check_expr(base)?;
                let it = self.check_expr(index)?;
                match bt {
                    Type::Array(e) => {
                        if it != Type::Int {
                            return Err(TypeError::new("index must be int"));
                        }
                        Ok(*e)
                    }
                    Type::String => {
                        if it != Type::Int {
                            return Err(TypeError::new("string index must be int"));
                        }
                        Ok(Type::Byte) // Go-like: s[i] is a byte
                    }
                    Type::Map(k, v) => {
                        if !self.compatible(&it, &k) {
                            return Err(TypeError::new(format!(
                                "map key type mismatch: expected {}, got {}",
                                k.display(),
                                it.display()
                            )));
                        }
                        Ok(*v)
                    }
                    other => Err(TypeError::new(format!("cannot index {}", other.display()))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_slice_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                let bt = self.check_expr(base)?;
                if let Some(l) = low {
                    if self.check_expr(l)? != Type::Int {
                        return Err(TypeError::new("slice low bound must be int"));
                    }
                }
                if let Some(h) = high {
                    if self.check_expr(h)? != Type::Int {
                        return Err(TypeError::new("slice high bound must be int"));
                    }
                }
                if let Some(m) = max {
                    if self.check_expr(m)? != Type::Int {
                        return Err(TypeError::new("slice max bound must be int"));
                    }
                }
                match bt {
                    Type::Array(e) => Ok(Type::Array(e)),
                    Type::String => {
                        if max.is_some() {
                            return Err(TypeError::new(
                                "string slice does not support three-index form (use s[i:j])",
                            ));
                        }
                        Ok(Type::String)
                    }
                    other => Err(TypeError::new(format!(
                        "cannot slice {} (need []T or string)",
                        other.display()
                    ))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_field_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Field { base, field } => {
                // Pack-qualified unit variant: `eng.Red` (alias is not a value).
                if let Expr::Ident(alias) = base.as_ref() {
                    if let Some(ctor) = self.pack_variant_ctor(alias, field) {
                        if ctor.fields.is_empty() {
                            return self.check_expr(&Expr::Ident(field.clone()));
                        }
                        return Err(TypeError::new(format!(
                            "variant `{alias}.{field}` needs {} field(s) — use `{alias}.{field}(...)`",
                            ctor.fields.len()
                        )));
                    }
                }
                // Type path unit variant: `eng.Color.Red`.
                if let Expr::Field {
                    base: inner,
                    field: enum_short,
                } = base.as_ref()
                {
                    if let Expr::Ident(alias) = inner.as_ref() {
                        if let Some(enum_name) = self.pack_enum_type_name(alias, enum_short) {
                            if let Some(ctor) = self.variants.get(field) {
                                if ctor.enum_name == enum_name {
                                    if ctor.fields.is_empty() {
                                        return self.check_expr(&Expr::Ident(field.clone()));
                                    }
                                    return Err(TypeError::new(format!(
                                        "variant `{alias}.{enum_short}.{field}` needs {} field(s)",
                                        ctor.fields.len()
                                    )));
                                }
                            }
                            return Err(TypeError::new(format!(
                                "no variant `{field}` on `{alias}.{enum_short}`"
                            )));
                        }
                    }
                }
                // Nested partial moves: `p.inner.a` moves path "inner.a" only.
                if let Some((name, path_fields)) = Self::hold_field_path(base, field) {
                    if self.hold_vars.contains_key(&name) {
                        if self.moved_holds.get(&name).copied().unwrap_or(false) {
                            return Err(TypeError::new(format!("use of moved value `{name}`"))
                                .hint("hold binding was fully moved"));
                        }
                        let path = path_fields.join(".");
                        let prior = self
                            .hold_moved_fields
                            .get(&name)
                            .cloned()
                            .unwrap_or_default();
                        if prior.contains(&path) {
                            return Err(TypeError::new(format!(
                                "use of moved field `{name}.{path}`"
                            )));
                        }
                        for m in &prior {
                            if path.starts_with(&format!("{m}."))
                                || m.starts_with(&format!("{path}."))
                            {
                                return Err(TypeError::new(format!(
                                    "cannot use `{name}.{path}` — related field `{name}.{m}` was moved"
                                )));
                            }
                        }
                        let Some((ty, _)) = self.lookup(&name).cloned() else {
                            return Err(TypeError::new(format!("undefined variable `{name}`")));
                        };
                        let mut cur = ty;
                        for (i, fname) in path_fields.iter().enumerate() {
                            let Type::Struct { fields, .. } = cur else {
                                return Err(TypeError::new(format!(
                                    "cannot access field `{fname}` on {}",
                                    cur.display()
                                )));
                            };
                            let Some((_, fty)) = fields.iter().find(|(n, _)| n == fname) else {
                                return Err(TypeError::new(format!("no field `{fname}`")));
                            };
                            if i + 1 == path_fields.len() {
                                let fty = fty.clone();
                                // Copy fields may be read repeatedly without moving.
                                if !is_copy_type(&fty) {
                                    self.hold_moved_fields
                                        .entry(name.clone())
                                        .or_default()
                                        .insert(path.clone());
                                }
                                return Ok(fty);
                            }
                            cur = fty.clone();
                        }
                        unreachable!();
                    }
                }
                let bt = self.check_expr(base)?;
                // Resolve Named types to their struct definition.
                let resolved_bt = match &bt {
                    Type::Named(n) => {
                        // Try to instantiate generic struct if needed.
                        if !self.types.contains_key(n) {
                            let _ = self.try_instantiate_generic_struct(n);
                        }
                        self.types.get(n).cloned().unwrap_or(bt.clone())
                    }
                    other => other.clone(),
                };
                match resolved_bt {
                    Type::Struct { fields, .. } => fields
                        .iter()
                        .find(|(n, _)| n == field)
                        .map(|(_, t)| t.clone())
                        .ok_or_else(|| TypeError::new(format!("no field `{field}`"))),
                    other => Err(TypeError::new(format!(
                        "cannot access field on {}",
                        other.display()
                    ))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_struct_lit_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::StructLit {
                name,
                fields,
                update,
            } => {
                // If this is a monomorphized generic struct (e.g. Pair__int),
                // instantiate the template on first use.
                if !self.types.contains_key(name) {
                    self.try_instantiate_generic_struct(name)?;
                }
                let Some(Type::Struct {
                    name: sname,
                    fields: decl,
                }) = self.types.get(name).cloned()
                else {
                    return Err(TypeError::new(format!("unknown struct `{name}`")));
                };
                // Functional update `S { field: v, ..base }` fills missing fields from base.
                // Field defaults fill omitted fields when present on the struct def.
                if let Some(base) = update {
                    let bt = self.check_expr(base)?;
                    let same = match &bt {
                        Type::Struct { name: bn, .. } => bn == &sname,
                        Type::Named(n) => n == &sname,
                        _ => false,
                    };
                    if !same {
                        return Err(TypeError::new(format!(
                            "struct update base must be `{sname}`, got {}",
                            bt.display()
                        )));
                    }
                }
                let mut seen = std::collections::HashSet::new();
                for (fname, fexpr) in fields {
                    if !seen.insert(fname.clone()) {
                        return Err(TypeError::new(format!(
                            "struct `{sname}` duplicate field `{fname}`"
                        )));
                    }
                    let Some((_, expected)) = decl.iter().find(|(n, _)| n == fname) else {
                        return Err(TypeError::new(format!(
                            "struct `{sname}` has no field `{fname}`"
                        )));
                    };
                    let got = if matches!(fexpr, Expr::Int(_)) && is_literal_int_kind(expected) {
                        expected.clone()
                    } else if matches!(fexpr, Expr::String(_)) && *expected == Type::String {
                        Type::String
                    } else if matches!(fexpr, Expr::Float(_)) && *expected == Type::Float {
                        Type::Float
                    } else if matches!(fexpr, Expr::Bool(_)) && *expected == Type::Bool {
                        Type::Bool
                    } else {
                        self.check_expr(fexpr)?
                    };
                    if !self.compatible(&got, expected) {
                        return Err(TypeError::new(format!(
                            "struct `{sname}` field `{fname}`: expected {}, got {}",
                            expected.display(),
                            got.display()
                        )));
                    }
                }
                // Without update, require all declared fields present (or a default).
                if update.is_none() {
                    let defaults = self.struct_field_defaults.get(&sname);
                    for (dn, _) in &decl {
                        if fields.iter().any(|(n, _)| n == dn) {
                            continue;
                        }
                        if defaults.map(|d| d.contains_key(dn)).unwrap_or(false) {
                            continue;
                        }
                        return Err(TypeError::new(format!(
                            "struct `{sname}` missing field `{dn}`"
                        )));
                    }
                }
                Ok(Type::Struct {
                    name: sname,
                    fields: decl,
                })
            }
            _ => unreachable!(),
        }
    }

    fn check_struct_lit_pos_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::StructLitPos { name, values } => {
                let Some(Type::Struct {
                    name: sname,
                    fields: decl,
                }) = self.types.get(name).cloned()
                else {
                    return Err(TypeError::new(format!("unknown struct `{name}`")));
                };
                // `Point{}` is the zero value (all fields default), like Go.
                // Otherwise every field must be given, in order.
                if !values.is_empty() && values.len() != decl.len() {
                    return Err(TypeError::new(format!(
                        "struct `{sname}` expects {} positional field(s), got {}",
                        decl.len(),
                        values.len()
                    )));
                }
                for (vexpr, (fname, expected)) in values.iter().zip(decl.iter()) {
                    let got = if matches!(vexpr, Expr::Int(_)) && is_literal_int_kind(expected) {
                        expected.clone()
                    } else if matches!(vexpr, Expr::String(_)) && *expected == Type::String {
                        Type::String
                    } else if matches!(vexpr, Expr::Float(_)) && *expected == Type::Float {
                        Type::Float
                    } else if matches!(vexpr, Expr::Bool(_)) && *expected == Type::Bool {
                        Type::Bool
                    } else {
                        self.check_expr(vexpr)?
                    };
                    if !self.compatible(&got, expected) {
                        return Err(TypeError::new(format!(
                            "struct `{sname}` field `{fname}` (position {}): expected {}, got {}",
                            decl.iter().position(|(n, _)| n == fname).unwrap_or(0) + 1,
                            expected.display(),
                            got.display()
                        )));
                    }
                }
                Ok(Type::Struct {
                    name: sname,
                    fields: decl,
                })
            }
            _ => unreachable!(),
        }
    }

    fn check_array_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Array(elems) => {
                // Prefer element type from expected `[]T` (Option/Result bags, annotated lits).
                let expected_elem = match &self.current_expected {
                    Some(Type::Array(inner)) => Some(inner.as_ref().clone()),
                    _ => None,
                };
                if elems.is_empty() {
                    if let Some(elem) = expected_elem {
                        return Ok(Type::Array(Box::new(elem)));
                    }
                    return Ok(Type::Array(Box::new(Type::Int)));
                }
                let saved_expected = self.current_expected.clone();
                if let Some(ref elem) = expected_elem {
                    self.current_expected = Some(elem.clone());
                }
                let first = self.check_expr(&elems[0])?;
                for e in elems.iter().skip(1) {
                    let t = self.check_expr(e)?;
                    if !self.compatible(&t, &first) {
                        self.current_expected = saved_expected;
                        return Err(TypeError::new("array element type mismatch"));
                    }
                }
                self.current_expected = saved_expected;
                if let Some(elem) = expected_elem {
                    // Untyped int literals may inhabit []byte / []int64 / []int32 / []int8
                    // (same Go-like rule as annotated let bindings).
                    let lit_ok = matches!(first, Type::Int)
                        && is_literal_int_kind(&elem)
                        && elems.iter().all(|e| matches!(e, Expr::Int(_)));
                    if !self.compatible(&first, &elem) && !lit_ok {
                        return Err(TypeError::new(format!(
                            "array element type mismatch: expected {}, got {}",
                            elem.display(),
                            first.display()
                        )));
                    }
                    return Ok(Type::Array(Box::new(elem)));
                }
                Ok(Type::Array(Box::new(first)))
            }
            _ => unreachable!(),
        }
    }

    fn check_convert_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Convert { ty, args } => {
                let target = self.resolve_type(ty)?;
                match &target {
                    Type::Array(inner) if **inner == Type::Byte => {
                        if args.len() != 1 {
                            return Err(TypeError::new("[]byte(...) takes 1 argument"));
                        }
                        let t = self.check_expr(&args[0])?;
                        match t {
                            Type::String => Ok(Type::Array(Box::new(Type::Byte))),
                            Type::Array(i) if *i == Type::Byte => {
                                Ok(Type::Array(Box::new(Type::Byte)))
                            }
                            other => Err(TypeError::new(format!(
                                "[]byte(...) expects string or []byte, got {}",
                                other.display()
                            ))),
                        }
                    }
                    other => Err(TypeError::new(format!(
                        "[]T(...) conversion only supports []byte today, got {}",
                        other.display()
                    ))
                    .hint("use bytes(s) or []byte(s) for string→bytes")),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_make_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Make { ty, len, cap } => {
                let target = self.resolve_type(ty)?;
                match target {
                    Type::Chan(elem) => {
                        // make(chan[T], cap) — additive typed open (int/string)
                        let Some(l) = len else {
                            return Err(TypeError::new(
                                "make(chan[T]) needs capacity: make(chan[int], n)",
                            ));
                        };
                        let lt = if matches!(l.as_ref(), Expr::Int(_)) {
                            Type::Int
                        } else {
                            self.check_expr(l)?
                        };
                        if lt != Type::Int {
                            return Err(TypeError::new("make(chan[T]) capacity must be int"));
                        }
                        if cap.is_some() {
                            return Err(TypeError::new(
                                "make(chan[T], n) takes one capacity argument",
                            ));
                        }
                        // Same element set as `chan_open[T]` (int family, string, float,
                        // bool, named structs / enums / pack types).
                        match elem.as_ref() {
                            Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::Bool
                            | Type::Float
                            | Type::String => Ok(Type::Chan(elem)),
                            Type::Named(n)
                                if n != "ShareInt"
                                    && n != "Arena"
                                    && n != "Crew"
                                    && (self.structs_named(n) || self.enums_named(n)) =>
                            {
                                Ok(Type::Chan(elem))
                            }
                            Type::Struct { .. } => Ok(Type::Chan(elem)),
                            Type::Enum { .. } => Ok(Type::Chan(elem)),
                            Type::Tuple(elems)
                                if elems.iter().all(|e| self.is_chan_element_ty(e)) =>
                            {
                                Ok(Type::Chan(elem))
                            }
                            other => Err(TypeError::new(format!(
                                "make(chan[T]) supports int family, bool, float, string, named structs, enums, and tuples of those, got {}",
                                other.display()
                            ))
                            .hint("use make(chan[T], n) or chan_open[T](n)")),
                        }
                    }
                    Type::Queue(elem) => {
                        // make(queue[T], cap) — typed FIFO (seed: string)
                        let Some(l) = len else {
                            return Err(TypeError::new(
                                "make(queue[T]) needs capacity: make(queue[string], n)",
                            ));
                        };
                        let lt = if matches!(l.as_ref(), Expr::Int(_)) {
                            Type::Int
                        } else {
                            self.check_expr(l)?
                        };
                        if lt != Type::Int {
                            return Err(TypeError::new("make(queue[T]) capacity must be int"));
                        }
                        if cap.is_some() {
                            return Err(TypeError::new(
                                "make(queue[T], n) takes one capacity argument",
                            ));
                        }
                        if !matches!(elem.as_ref(), Type::String) {
                            return Err(TypeError::new(
                                "make(queue[T]) currently supports queue[string]",
                            ));
                        }
                        Ok(Type::Queue(elem))
                    }
                    Type::Map(k, v) => {
                        if let Some(l) = len {
                            let lt = if matches!(l.as_ref(), Expr::Int(_)) {
                                Type::Int
                            } else {
                                self.check_expr(l)?
                            };
                            if !is_int_family(&lt) && lt != Type::Int {
                                return Err(TypeError::new("make map hint must be int"));
                            }
                        }
                        if cap.is_some() {
                            return Err(TypeError::new(
                                "make(map[K]V) takes at most one size hint",
                            ));
                        }
                        Ok(Type::Map(k, v))
                    }
                    Type::Array(inner) => {
                        let Some(l) = len else {
                            return Err(TypeError::new(
                                "make([]T) needs len: make([]int, n) or make([]int, n, cap)",
                            ));
                        };
                        let lt = if matches!(l.as_ref(), Expr::Int(_)) {
                            Type::Int
                        } else {
                            self.check_expr(l)?
                        };
                        if !is_int_family(&lt) && lt != Type::Int {
                            return Err(TypeError::new("make len must be int"));
                        }
                        if let Some(c) = cap {
                            let ct = if matches!(c.as_ref(), Expr::Int(_)) {
                                Type::Int
                            } else {
                                self.check_expr(c)?
                            };
                            if !is_int_family(&ct) && ct != Type::Int {
                                return Err(TypeError::new("make cap must be int"));
                            }
                        }
                        match inner.as_ref() {
                            Type::Int
                            | Type::Int64
                            | Type::Int32
                            | Type::Int8
                            | Type::Byte
                            | Type::String
                            | Type::Float
                            | Type::Bool
                            | Type::Struct { .. }
                            | Type::Enum { .. }
                            | Type::Array(_)
                            | Type::Map(_, _)
                            | Type::Option(_)
                            | Type::Result(_, _)
                            | Type::Chan(_) => Ok(Type::Array(inner)),
                            other => Err(TypeError::new(format!(
                                "make([]{}) not supported yet",
                                other.display()
                            ))),
                        }
                    }
                    other => Err(TypeError::new(format!(
                        "make expects []T or map[K]V, got {}",
                        other.display()
                    ))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_lambda_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Lambda { params, body } => {
                // Prefer expected `fn(T, …) -> R` for param/return types (first-class seed).
                // Fall back to all-int params (fan / untyped) when no expectation.
                let (param_tys, expected_ret): (Vec<Type>, Option<Type>) =
                    match &self.current_expected {
                        Some(Type::Fn(ps, r)) if ps.len() == params.len() => {
                            (ps.clone(), Some(r.as_ref().clone()))
                        }
                        Some(Type::Fn(ps, _)) if ps.len() != params.len() => {
                            return Err(TypeError::new(format!(
                                "lambda has {} params, expected fn type has {}",
                                params.len(),
                                ps.len()
                            )));
                        }
                        _ => (params.iter().map(|_| Type::Int).collect(), None),
                    };
                self.push_scope();
                for (p, ty) in params.iter().zip(param_tys.iter()) {
                    self.define(p, ty.clone(), false);
                }
                let saved_exp = self.current_expected.clone();
                if let Some(ref er) = expected_ret {
                    self.current_expected = Some(er.clone());
                }
                // `fn(x) { x * 2 }` / `fn(x) { return x * 2 }` — block last value is ret type
                let ret = match body.as_ref() {
                    Expr::Block(b) => {
                        let mut last = Type::Void;
                        for stmt in &b.stmts {
                            match stmt {
                                Stmt::Return(Some(e)) | Stmt::Expr(e) => {
                                    last = self.check_expr(e)?;
                                }
                                other => self.check_stmt(other)?,
                            }
                        }
                        last
                    }
                    other => self.check_expr(other)?,
                };
                self.current_expected = saved_exp;
                self.pop_scope();
                if let Some(ref er) = expected_ret {
                    if ret != Type::Void && !self.compatible(&ret, er) {
                        return Err(TypeError::new(format!(
                            "lambda return type: expected {}, got {}",
                            er.display(),
                            ret.display()
                        )));
                    }
                    Ok(Type::Fn(param_tys, Box::new(er.clone())))
                } else {
                    Ok(Type::Fn(param_tys, Box::new(ret)))
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_if_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::IfExpr {
                cond,
                then_block,
                else_block,
            } => {
                let ct = self.check_expr(cond)?;
                if ct != Type::Bool {
                    return Err(TypeError::new("if condition must be bool"));
                }
                let tty = self.check_block_value(then_block)?;
                let ety = self.check_block_value(else_block)?;
                // A branch that diverges (ends in `return`/`break`) yields Void and
                // takes its type from the other branch. Two live branches must agree.
                if tty != Type::Void
                    && ety != Type::Void
                    && !self.compatible(&tty, &ety)
                    && !self.compatible(&ety, &tty)
                {
                    return Err(TypeError::new(format!(
                        "if/else branches yield different types: {} and {}",
                        tty.display(),
                        ety.display()
                    )));
                }
                Ok(if tty != Type::Void { tty } else { ety })
            }
            _ => unreachable!(),
        }
    }

    fn check_match_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Match { scrutinee, arms } => {
                let st = self.check_expr(scrutinee)?;
                if arms.is_empty() {
                    return Err(TypeError::new("match needs arms"));
                }
                self.check_exhaustiveness(&st, arms)?;
                // NLL match-arm join: snapshot; check each arm independently.
                // Diverging arms (`return`/`break`/`continue`) do not poison the join.
                let before_moved = self.moved_holds.clone();
                let before_fields = self.hold_moved_fields.clone();
                let before_share_vars = self.share_vars.clone();
                let before_share_sources = self.share_sources.clone();
                let before_share_depth = self.share_scope_depth.clone();
                let before_borrows = self.shared_borrows.clone();
                let mut joined_moved = before_moved.clone();
                let mut joined_fields = before_fields.clone();
                let mut arm_share_vars: Vec<HashMap<String, bool>> = Vec::new();
                let mut arm_share_sources: Vec<HashMap<String, String>> = Vec::new();
                let mut arm_share_depth: Vec<HashMap<String, usize>> = Vec::new();
                let mut arm_borrows: Vec<HashMap<String, bool>> = Vec::new();
                let mut result: Option<Type> = None;
                let mut reachable_arms = 0usize;
                for arm in arms {
                    self.moved_holds = before_moved.clone();
                    self.hold_moved_fields = before_fields.clone();
                    self.share_vars = before_share_vars.clone();
                    self.share_sources = before_share_sources.clone();
                    self.share_scope_depth = before_share_depth.clone();
                    self.shared_borrows = before_borrows.clone();
                    self.push_scope();
                    self.bind_pattern(&arm.pattern, &st)?;
                    let diverges = expr_always_diverges(&arm.body);
                    let t = self.check_expr(&arm.body)?;
                    self.pop_scope();
                    if diverges {
                        // Arm never falls through to after-match — skip join + type.
                        continue;
                    }
                    reachable_arms += 1;
                    for (name, was) in &self.moved_holds {
                        if *was {
                            joined_moved.insert(name.clone(), true);
                        }
                    }
                    for (name, fields) in &self.hold_moved_fields {
                        let e = joined_fields.entry(name.clone()).or_default();
                        e.extend(fields.iter().cloned());
                    }
                    arm_share_vars.push(self.share_vars.clone());
                    arm_share_sources.push(self.share_sources.clone());
                    arm_share_depth.push(self.share_scope_depth.clone());
                    arm_borrows.push(self.shared_borrows.clone());
                    match &result {
                        None => result = Some(t),
                        Some(prev) if self.compatible(&t, prev) => {}
                        Some(prev) => {
                            return Err(TypeError::new(format!(
                                "match arm type mismatch: {} vs {}",
                                prev.display(),
                                t.display()
                            )))
                        }
                    }
                }
                if reachable_arms == 0 {
                    // All arms diverge — restore pre-match (successor may be dead).
                    self.moved_holds = before_moved;
                    self.hold_moved_fields = before_fields;
                    self.share_vars = before_share_vars;
                    self.share_sources = before_share_sources;
                    self.share_scope_depth = before_share_depth;
                    self.shared_borrows = before_borrows;
                    // No value type from arms; prefer void for empty join.
                    return Ok(result.unwrap_or(Type::Void));
                }
                self.moved_holds = joined_moved;
                self.hold_moved_fields = joined_fields;
                // Share bindings / borrows: keep only those still live on every *reachable* arm.
                if let Some(first) = arm_share_vars.first() {
                    let mut joined_vars = HashMap::new();
                    for name in first.keys() {
                        if arm_share_vars.iter().all(|m| m.contains_key(name)) {
                            joined_vars.insert(name.clone(), true);
                        }
                    }
                    let mut joined_sources = HashMap::new();
                    let mut joined_depth = HashMap::new();
                    for name in joined_vars.keys() {
                        if let Some(src) = arm_share_sources[0].get(name) {
                            if arm_share_sources.iter().all(|m| m.get(name) == Some(src)) {
                                joined_sources.insert(name.clone(), src.clone());
                            }
                        }
                        if let Some(d) = arm_share_depth[0].get(name) {
                            joined_depth.insert(name.clone(), *d);
                        }
                    }
                    let mut joined_borrows = HashMap::new();
                    if let Some(fb) = arm_borrows.first() {
                        for src in fb.keys() {
                            if arm_borrows.iter().all(|m| m.contains_key(src)) {
                                joined_borrows.insert(src.clone(), true);
                            }
                        }
                    }
                    self.share_vars = joined_vars;
                    self.share_sources = joined_sources;
                    self.share_scope_depth = joined_depth;
                    self.shared_borrows = joined_borrows;
                }
                result.ok_or_else(|| TypeError::new("match has no reachable value arm"))
            }
            _ => unreachable!(),
        }
    }

    fn check_try_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Try(inner) => {
                let t = self.check_expr(inner)?;
                match t {
                    Type::Result(ok, err) => match &self.current_ret {
                        // Same-family Result: peel Ok, early-return Err (including nested Result).
                        Type::Result(ret_ok, ret_err) => {
                            // Err types must be compatible for direct rethrow.
                            if !self.compatible(&err, ret_err)
                                && !matches!(
                                    (err.as_ref(), ret_err.as_ref()),
                                    (Type::String, Type::String)
                                )
                            {
                                // Still allow when both are string-ish or identical after display
                                if err.display() != ret_err.display() {
                                    return Err(TypeError::new(format!(
                                        "`?` Result error type {} is not compatible with function Err {}",
                                        err.display(),
                                        ret_err.display()
                                    )));
                                }
                            }
                            // Ok payload may be nested Result/Option — peel one layer only.
                            let _ = ret_ok;
                            Ok(*ok)
                        }
                        // Exotic: Result? in Option-returning fn → Ok unwrap, Err → None.
                        Type::Option(ret_inner) => {
                            if !self.compatible(&ok, ret_inner)
                                && !matches!(
                                    (ok.as_ref(), ret_inner.as_ref()),
                                    (Type::Int, Type::Int)
                                        | (Type::String, Type::String)
                                        | (Type::Bool, Type::Bool)
                                        | (Type::Float, Type::Float)
                                )
                            {
                                // Allow when Ok type matches Option payload loosely.
                                if ok.display() != ret_inner.display() {
                                    return Err(TypeError::new(format!(
                                        "`?` on Result in Option-returning function: Ok {} vs Option[{}]",
                                        ok.display(),
                                        ret_inner.display()
                                    )));
                                }
                            }
                            Ok(*ok)
                        }
                        _ => Err(TypeError::new(
                            "`?` on Result only allowed in functions returning Result or Option",
                        )),
                    },
                    Type::Option(inner) => match &self.current_ret {
                        Type::Option(_) => Ok(*inner),
                        // Exotic: Option? in Result[T, string] (or Result[T, E]) → None becomes Err.
                        Type::Result(ret_ok, ret_err) => {
                            if !self.compatible(&inner, ret_ok)
                                && inner.display() != ret_ok.display()
                            {
                                return Err(TypeError::new(format!(
                                    "`?` on Option in Result-returning function: Some {} vs Ok {}",
                                    inner.display(),
                                    ret_ok.display()
                                )));
                            }
                            // Prefer string errors for auto-None conversion.
                            if !matches!(ret_err.as_ref(), Type::String | Type::Named(_))
                                && !matches!(ret_err.as_ref(), Type::Enum { .. })
                            {
                                return Err(TypeError::new(
                                    "`?` on Option in Result-returning function needs string or enum Err type",
                                ));
                            }
                            Ok(*inner)
                        }
                        _ => Err(TypeError::new(
                            "`?` on Option only allowed in functions returning Option or Result",
                        )),
                    },
                    other => Err(TypeError::new(format!(
                        "`?` needs Result or Option, got {}",
                        other.display()
                    ))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_kick_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Kick { crew, expr } => {
                // Process-scoped detach uses a sentinel crew name (no local nursery).
                if crew != "__detached__" {
                    let Some((cty, _)) = self.lookup(crew).cloned() else {
                        return Err(TypeError::new(format!("unknown crew `{crew}`")));
                    };
                    if cty != Type::Crew {
                        return Err(TypeError::new(format!("`{crew}` is not a crew")));
                    }
                }
                let t = self.check_expr(expr)?;
                // Send-like seed after typecheck (no second check_expr — avoids double hold moves).
                self.assert_kick_sendable(expr)?;
                // Static race seed: track mut non-Copy captures until join.
                self.note_kick_race_captures(expr);
                Ok(Type::Job(Box::new(t)))
            }
            _ => unreachable!(),
        }
    }

    fn check_join_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Join(inner) => {
                let t = self.check_expr(inner)?;
                match t {
                    Type::Job(inner) => {
                        // Pop latest kick race frame (nested kicks supported via stack).
                        self.race_join_clear();
                        Ok(*inner)
                    }
                    other => Err(TypeError::new(format!(
                        "join needs Job, got {}",
                        other.display()
                    ))),
                }
            }
            _ => unreachable!(),
        }
    }

    fn check_fan_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Fan { collection, mapper } => {
                let ct = self.check_expr(collection)?;
                let elem = match ct {
                    Type::Array(e) => *e,
                    other => {
                        return Err(TypeError::new(format!(
                            "fan needs array, got {}",
                            other.display()
                        )))
                    }
                };
                // Codegen: int family, float, string, named struct parallel maps.
                let elem_ok = matches!(
                    elem,
                    Type::Int
                        | Type::Int64
                        | Type::Int32
                        | Type::Int8
                        | Type::Byte
                        | Type::Bool
                        | Type::Float
                        | Type::String
                ) || matches!(&elem, Type::Named(n) if self.structs_named(n))
                    || matches!(&elem, Type::Struct { .. });
                if !elem_ok {
                    return Err(TypeError::new(format!(
                        "fan supports []int, []float, []string, and []Struct, got []{}",
                        elem.display()
                    )));
                }
                // Type mapper with param type = element type (not always int).
                let ret = match mapper.as_ref() {
                    Expr::Lambda { params, body } => {
                        if params.len() != 1 {
                            return Err(TypeError::new("fan lambda needs exactly one parameter"));
                        }
                        self.push_scope();
                        self.define(&params[0], elem.clone(), false);
                        let r = match body.as_ref() {
                            Expr::Block(b) => {
                                let mut last = Type::Void;
                                for stmt in &b.stmts {
                                    match stmt {
                                        Stmt::Return(Some(e)) | Stmt::Expr(e) => {
                                            last = self.check_expr(e)?;
                                        }
                                        other => self.check_stmt(other)?,
                                    }
                                }
                                last
                            }
                            other => self.check_expr(other)?,
                        };
                        self.pop_scope();
                        r
                    }
                    _ => {
                        let mt = self.check_expr(mapper)?;
                        match mt {
                            Type::Fn(params, ret) => {
                                if params.len() != 1 || !self.compatible(&elem, &params[0]) {
                                    return Err(TypeError::new("fan mapper arity/type mismatch"));
                                }
                                *ret
                            }
                            other => {
                                return Err(TypeError::new(format!(
                                    "fan needs function, got {}",
                                    other.display()
                                )))
                            }
                        }
                    }
                };
                if matches!(elem, Type::Float) && !matches!(ret, Type::Float) {
                    return Err(TypeError::new(
                        "fan on []float expects a float→float mapper",
                    ));
                }
                if matches!(elem, Type::String) && !matches!(ret, Type::String) {
                    return Err(TypeError::new(
                        "fan on []string expects a string→string mapper",
                    ));
                }
                if matches!(
                    elem,
                    Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::Byte | Type::Bool
                ) && matches!(ret, Type::Float | Type::String)
                {
                    return Err(TypeError::new("fan on []int expects an int→int mapper"));
                }
                self.assert_fan_mapper_safe(mapper)?;
                Ok(Type::Array(Box::new(ret)))
            }
            _ => unreachable!(),
        }
    }

    // Variant handlers are deliberately separate so recursive type checking does
    // not reserve every variant's locals in one Windows debug stack frame.
    fn check_expr(&mut self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Int(_) => Ok(Type::Int),
            Expr::Float(_) => Ok(Type::Float),
            Expr::Bool(_) => Ok(Type::Bool),
            Expr::String(_) => Ok(Type::String),
            Expr::Ident(..) => self.check_ident_expr(expr),
            Expr::Binary { .. } => self.check_binary_expr(expr),
            Expr::Unary { .. } => self.check_unary_expr(expr),
            Expr::Tuple(..) => self.check_tuple_expr(expr),
            Expr::ChanOpen { .. } => self.check_chan_open_expr(expr),
            Expr::Call { .. } => self.check_call_expr(expr),
            Expr::Method { .. } => self.check_method_expr(expr),
            Expr::Index { .. } => self.check_index_expr(expr),
            Expr::Slice { .. } => self.check_slice_expr(expr),
            Expr::Field { .. } => self.check_field_expr(expr),
            Expr::StructLit { .. } => self.check_struct_lit_expr(expr),
            Expr::StringInterp(parts) => {
                for p in parts {
                    if let InterpPart::Expr(e, _) = p {
                        let _ = self.check_expr(e)?;
                    }
                }
                Ok(Type::String)
            }
            Expr::StructLitPos { .. } => self.check_struct_lit_pos_expr(expr),
            Expr::Array(..) => self.check_array_expr(expr),
            Expr::Convert { .. } => self.check_convert_expr(expr),
            Expr::Make { .. } => self.check_make_expr(expr),
            Expr::Lambda { .. } => self.check_lambda_expr(expr),
            Expr::IfExpr { .. } => self.check_if_expr(expr),
            Expr::Match { .. } => self.check_match_expr(expr),
            Expr::Try(..) => self.check_try_expr(expr),
            Expr::Block(b) => {
                self.check_block(b)?;
                Ok(Type::Void)
            }
            Expr::Kick { .. } => self.check_kick_expr(expr),
            Expr::Join(..) => self.check_join_expr(expr),
            Expr::Fan { .. } => self.check_fan_expr(expr),
        }
    }

    fn compatible(&self, got: &Type, expected: &Type) -> bool {
        if got == expected {
            return true;
        }
        match (got, expected) {
            // SAFE-005: owned string may be viewed as string_view (no clone).
            (Type::String, Type::Named(n)) if n == "string_view" => true,
            (Type::Named(n), Type::String) if n == "string_view" => true,
            // string_view result of str_as_view / annotated lit.
            (Type::Named(n), Type::Named(m)) if n == "string_view" && m == "string_view" => true,
            (Type::Named(a), Type::Named(b)) if a == b => true,
            (Type::Array(a), Type::Array(b)) => self.compatible(a, b),
            (Type::Map(k1, v1), Type::Map(k2, v2)) => {
                self.compatible(k1, k2) && self.compatible(v1, v2)
            }
            (Type::Option(a), Type::Option(b)) => self.compatible(a, b),
            (Type::Result(a1, _), Type::Result(a2, _)) => self.compatible(a1, a2),
            (Type::Job(a), Type::Job(b)) => self.compatible(a, b),
            (Type::Chan(a), Type::Chan(b)) => self.compatible(a, b),
            (Type::Tuple(a), Type::Tuple(b)) if a.len() == b.len() => {
                a.iter().zip(b.iter()).all(|(x, y)| self.compatible(x, y))
            }
            (Type::Fn(pa, ra), Type::Fn(pb, rb)) if pa.len() == pb.len() => {
                pa.iter().zip(pb.iter()).all(|(a, b)| self.compatible(a, b))
                    && self.compatible(ra, rb)
            }
            (Type::Enum { name: a, .. }, Type::Enum { name: b, .. }) if a == b => true,
            (Type::Interface { name: a }, Type::Interface { name: b }) if a == b => true,
            (got, Type::Interface { name: iname }) => self.implements_iface(got, iname),
            // Struct/enum name aliases (existing)
            (Type::Struct { name: a, .. }, Type::Named(b)) => a == b,
            (Type::Named(a), Type::Struct { name: b, .. }) => a == b,
            (Type::Enum { name: a, .. }, Type::Named(b)) => a == b,
            (Type::Named(a), Type::Enum { name: b, .. }) => a == b,
            _ => false,
        }
    }

    /// Infer type args and monomorphize a generic function call.
    /// Try to instantiate a generic struct from a monomorphized name like `Pair__int`.
    /// Parses the name to find the template and type args, substitutes, and registers.
    fn try_instantiate_generic_struct(&mut self, mono_name: &str) -> Result<(), TypeError> {
        if self.mono_struct_generated.contains(mono_name) {
            return Ok(());
        }
        // Find the base generic struct name by trying known templates.
        // e.g. for "Wrapper__Pair__int", try "Wrapper" first (it's a known template).
        // The rest ("Pair__int") is the tag for type param T.
        let mut base = "";
        let mut tag_str = "";
        for name in self.generic_structs.keys() {
            if let Some(rest) = mono_name.strip_prefix(name.as_str()) {
                if let Some(rest) = rest.strip_prefix("__") {
                    if base.is_empty() || name.len() > base.len() {
                        base = unsafe { &*(name.as_str() as *const str) };
                        tag_str = unsafe { &*(rest as *const str) };
                    }
                }
            }
        }
        if base.is_empty() {
            return Ok(()); // no matching generic template
        }
        let template = self.generic_structs.get(base).cloned().unwrap();
        // Split tag_str into N parts matching the number of type params.
        // For 1 param, the entire tag_str is the tag (even if it contains __).
        // For N params, split on __ but only N-1 times.
        let tag_parts: Vec<&str> = if template.type_params.len() == 1 {
            vec![tag_str]
        } else {
            tag_str.splitn(template.type_params.len(), "__").collect()
        };
        if tag_parts.len() != template.type_params.len() {
            return Err(TypeError::new(format!(
                "generic struct `{base}` has {} type params, got {} in `{mono_name}`",
                template.type_params.len(),
                tag_parts.len()
            )));
        }
        // Build substitution map: T → resolved type
        let mut subst: HashMap<String, Type> = HashMap::new();
        for (tp, tag) in template.type_params.iter().zip(tag_parts.iter()) {
            let te = self.tag_to_type_expr(tag);
            let ty = self.resolve_type(&te)?;
            subst.insert(tp.clone(), ty);
        }
        // Substitute type params in fields
        let mono_fields: Vec<(String, TypeExpr, Option<Expr>)> = template
            .fields
            .iter()
            .map(|(n, t, d)| (n.clone(), subst_type_expr(t, &subst), d.clone()))
            .collect();
        let mono_struct = StructDef {
            name: mono_name.to_string(),
            type_params: Vec::new(),
            fields: mono_fields,
            derives: template.derives.clone(),
            exported: template.exported,
        };
        // Register the concrete struct type
        let fields: Result<Vec<_>, _> = mono_struct
            .fields
            .iter()
            .map(|(n, t, _)| Ok((n.clone(), self.resolve_type(t)?)))
            .collect();
        let fields = fields?;
        self.types.insert(
            mono_name.to_string(),
            Type::Struct {
                name: mono_name.to_string(),
                fields,
            },
        );
        self.mono_struct_generated.insert(mono_name.to_string());
        self.mono_structs.push(mono_struct);
        Ok(())
    }

    /// Convert a mono tag string back to a TypeExpr for substitution.
    /// Handles both primitive tags and monomorphized struct names.
    fn tag_to_type_expr(&mut self, tag: &str) -> TypeExpr {
        match tag {
            "int" | "int64" => TypeExpr::Named("int".into()),
            "float" | "float64" => TypeExpr::Named("float".into()),
            "bool" => TypeExpr::Named("bool".into()),
            "string" => TypeExpr::Named("string".into()),
            "byte" => TypeExpr::Named("byte".into()),
            other => {
                // If this looks like a monomorphized generic struct (contains __),
                // try to instantiate it first so it's available as a type.
                if other.contains("__") {
                    let _ = self.try_instantiate_generic_struct(other);
                }
                TypeExpr::Named(other.to_string())
            }
        }
    }

    /// Try to instantiate a generic enum from a monomorphized name.
    fn try_instantiate_generic_enum(&mut self, mono_name: &str) -> Result<(), TypeError> {
        if self.mono_enum_generated.contains(mono_name) {
            return Ok(());
        }
        let mut base = "";
        let mut tag_str = "";
        for name in self.generic_enums.keys() {
            if let Some(rest) = mono_name.strip_prefix(name.as_str()) {
                if let Some(rest) = rest.strip_prefix("__") {
                    if base.is_empty() || name.len() > base.len() {
                        base = unsafe { &*(name.as_str() as *const str) };
                        tag_str = unsafe { &*(rest as *const str) };
                    }
                }
            }
        }
        if base.is_empty() {
            return Ok(());
        }
        let template = self.generic_enums.get(base).cloned().unwrap();
        let tag_parts: Vec<&str> = if template.type_params.len() == 1 {
            vec![tag_str]
        } else {
            tag_str.splitn(template.type_params.len(), "__").collect()
        };
        if tag_parts.len() != template.type_params.len() {
            return Err(TypeError::new(format!(
                "generic enum `{base}` has {} type params, got {} in `{mono_name}`",
                template.type_params.len(),
                tag_parts.len()
            )));
        }
        let mut subst: HashMap<String, Type> = HashMap::new();
        for (tp, tag) in template.type_params.iter().zip(tag_parts.iter()) {
            let te = self.tag_to_type_expr(tag);
            let ty = self.resolve_type(&te)?;
            subst.insert(tp.clone(), ty);
        }
        let mono_variants: Vec<EnumVariant> = template
            .variants
            .iter()
            .map(|v| EnumVariant {
                name: v.name.clone(),
                fields: v
                    .fields
                    .iter()
                    .map(|t| subst_type_expr(t, &subst))
                    .collect(),
            })
            .collect();
        let mono_enum = EnumDef {
            name: mono_name.to_string(),
            type_params: Vec::new(),
            variants: mono_variants,
            exported: template.exported,
        };
        // Register variants and the concrete enum type
        let mut typed_variants = Vec::new();
        for v in &mono_enum.variants {
            let fields: Result<Vec<_>, _> = v.fields.iter().map(|t| self.resolve_type(t)).collect();
            let fields = fields?;
            // Register by bare name only if not already taken (first-wins)
            if !self.variants.contains_key(&v.name) {
                self.variants.insert(
                    v.name.clone(),
                    VariantCtor {
                        enum_name: mono_name.to_string(),
                        fields: fields.clone(),
                    },
                );
            }
            // Always register by qualified name for disambiguation
            self.variants.insert(
                format!("{}::{}", mono_name, v.name),
                VariantCtor {
                    enum_name: mono_name.to_string(),
                    fields: fields.clone(),
                },
            );
            typed_variants.push((v.name.clone(), fields));
        }
        self.types.insert(
            mono_name.to_string(),
            Type::Enum {
                name: mono_name.to_string(),
                variants: typed_variants,
            },
        );
        self.mono_enum_generated.insert(mono_name.to_string());
        self.mono_enums.push(mono_enum);
        Ok(())
    }

    /// Verify that `concrete_ty` satisfies the interface `iface_name`.
    /// Checks that the concrete type has methods matching all interface methods.
    fn check_interface_bound(
        &self,
        concrete_ty: &Type,
        iface_name: &str,
        fn_name: &str,
        tp_name: &str,
    ) -> Result<(), TypeError> {
        // Find the interface definition
        let iface = self.interfaces.iter().find(|d| d.name == iface_name);
        let Some(iface_def) = iface else {
            return Err(TypeError::new(format!(
                "unknown interface `{iface_name}` used as bound on `{tp_name}` in `{fn_name}`"
            )));
        };
        let methods = iface_def.methods.clone();
        // Get the concrete type name for method lookup
        let type_name = match concrete_ty {
            Type::Struct { name, .. } => name.clone(),
            Type::Named(n) => n.clone(),
            Type::Int => "int".into(),
            Type::Float => "float".into(),
            Type::String => "string".into(),
            Type::Bool => "bool".into(),
            other => other.display(),
        };
        // Check each interface method exists on the concrete type
        for (method_name, _param_tys, _ret_ty) in &methods {
            let impl_name = format!("{type_name}_{method_name}");
            if !self.fns.contains_key(&impl_name) {
                return Err(TypeError::new(format!(
                    "type `{}` does not satisfy interface `{iface_name}`: missing method `{method_name}`",
                    concrete_ty.display()
                ))
                .hint(format!(
                    "add `on {type_name} {{ fn {method_name}(self, ...) {{ ... }} }}`"
                )));
            }
        }
        Ok(())
    }

    fn check_generic_call(&mut self, name: &str, args: &[Expr]) -> Result<Type, TypeError> {
        let template = self
            .generic_fns
            .get(name)
            .cloned()
            .ok_or_else(|| TypeError::new(format!("unknown generic function `{name}`")))?;
        if args.len() != template.params.len() {
            return Err(TypeError::new(format!(
                "generic `{name}` expects {} args, got {}",
                template.params.len(),
                args.len()
            )));
        }
        let mut arg_tys = Vec::new();
        for a in args {
            arg_tys.push(self.check_expr(a)?);
        }
        let mut subst: HashMap<String, Type> = HashMap::new();
        let tp_set: HashSet<String> = template.type_params.iter().cloned().collect();
        for (param, aty) in template.params.iter().zip(arg_tys.iter()) {
            let pty = self.resolve_type_with(&param.ty, &tp_set)?;
            self.unify_generic(&pty, aty, &tp_set, &mut subst)?;
        }
        // All type params must be inferred
        for tp in &template.type_params {
            if !subst.contains_key(tp) {
                return Err(TypeError::new(format!(
                    "cannot infer type parameter `{tp}` for `{name}`"
                ))
                .hint("pass arguments that determine T, or use a concrete helper"));
            }
        }
        // Check interface bounds: if T: Stringer, verify the concrete type has the methods.
        for (tp, iface_name) in &template.type_bounds {
            if let Some(concrete_ty) = subst.get(tp) {
                self.check_interface_bound(concrete_ty, iface_name, name, tp)?;
            }
        }
        let mono_name = {
            let tags: Vec<String> = template
                .type_params
                .iter()
                .map(|tp| subst[tp].mono_tag())
                .collect();
            format!("{name}__{}", tags.join("__"))
        };
        let ret_te = template
            .ret
            .clone()
            .unwrap_or(TypeExpr::Named("void".into()));
        let ret_ty = {
            let subst_te = subst_type_expr(&ret_te, &subst);
            self.resolve_type(&subst_te)?
        };
        if !self.mono_generated.contains(&mono_name) {
            let mono = specialize_fn(&template, &mono_name, &subst);
            // Register signature before checking body (recursion / mutual)
            let params: Result<Vec<_>, _> = mono
                .params
                .iter()
                .map(|p| self.resolve_type(&p.ty))
                .collect();
            self.fn_mut_params.insert(
                mono_name.clone(),
                mono.params.iter().map(|p| p.mutable).collect(),
            );
            self.fns.insert(
                mono_name.clone(),
                Type::Fn(params?, Box::new(ret_ty.clone())),
            );
            self.mono_generated.insert(mono_name.clone());
            self.mono_fns.push(mono.clone());
            self.check_fn(&mono)?;
        }
        // Mark call target for codegen: store mapping original→mono via mono_fns only;
        // codegen monomorphizes by the same naming scheme.
        let _ = mono_name;
        Ok(ret_ty)
    }

    fn unify_generic(
        &self,
        pattern: &Type,
        concrete: &Type,
        type_params: &HashSet<String>,
        subst: &mut HashMap<String, Type>,
    ) -> Result<(), TypeError> {
        match pattern {
            Type::Named(n) if type_params.contains(n) => {
                if let Some(prev) = subst.get(n) {
                    if !self.compatible(concrete, prev) {
                        return Err(TypeError::new(format!(
                            "type parameter `{n}` cannot be both {} and {}",
                            prev.display(),
                            concrete.display()
                        )));
                    }
                } else {
                    subst.insert(n.clone(), concrete.clone());
                }
                Ok(())
            }
            Type::Array(a) => match concrete {
                Type::Array(b) => self.unify_generic(a, b, type_params, subst),
                _ => Err(TypeError::new(format!(
                    "expected {}, got {}",
                    pattern.display(),
                    concrete.display()
                ))),
            },
            Type::Chan(a) => match concrete {
                Type::Chan(b) => self.unify_generic(a, b, type_params, subst),
                _ => Err(TypeError::new(format!(
                    "expected {}, got {}",
                    pattern.display(),
                    concrete.display()
                ))),
            },
            Type::Tuple(ps) => match concrete {
                Type::Tuple(cs) if ps.len() == cs.len() => {
                    for (p, c) in ps.iter().zip(cs.iter()) {
                        self.unify_generic(p, c, type_params, subst)?;
                    }
                    Ok(())
                }
                _ => Err(TypeError::new(format!(
                    "expected {}, got {}",
                    pattern.display(),
                    concrete.display()
                ))),
            },
            _ => {
                if self.compatible(concrete, pattern) {
                    Ok(())
                } else {
                    Err(TypeError::new(format!(
                        "type mismatch: expected {}, got {}",
                        pattern.display(),
                        concrete.display()
                    )))
                }
            }
        }
    }

    /// True if concrete type has iface method impls.
    /// Naming (any of):
    /// - `Iface_method` (self matches) or `Iface_Concrete_method`
    /// - **Go-style implicit:** `Concrete_method` from `on Concrete { fn method… }` / free `fn Concrete_method`
    /// No-self-only interfaces are implemented by any concrete (unit dyn).
    fn implements_iface(&self, concrete: &Type, iface_name: &str) -> bool {
        let Some(iface) = self.interfaces.iter().find(|i| i.name == iface_name) else {
            return false;
        };
        let cname = match concrete {
            Type::Struct { name, .. } => Some(name.as_str()),
            Type::Named(n) if self.types.contains_key(n) => Some(n.as_str()),
            Type::Named(n) if self.structs_named(n) => Some(n.as_str()),
            Type::Int | Type::Int64 | Type::Int32 => None, // unit/no-self coerce
            _ => None,
        };
        let mut all_no_self = true;
        for (mname, params, ret) in &iface.methods {
            let expected: Vec<Type> = params
                .iter()
                .filter_map(|t| self.resolve_type(t).ok())
                .collect();
            let expected_ret = self.resolve_type(ret).ok();
            if let Some(cn) = cname {
                if let Some((fp, fr)) = self.lookup_iface_impl(iface_name, mname, cn) {
                    if let Some(ref er) = expected_ret {
                        if !self.compatible(&fr, er) {
                            return false;
                        }
                    }
                    if fp.len() == expected.len() + 1 {
                        if !self.types_equal_iface_self(&fp[0], concrete) {
                            return false;
                        }
                        // remaining args
                        for (got, exp) in fp[1..].iter().zip(expected.iter()) {
                            if !self.compatible(got, exp) {
                                return false;
                            }
                        }
                        all_no_self = false;
                        continue;
                    } else if fp.len() == expected.len() {
                        for (got, exp) in fp.iter().zip(expected.iter()) {
                            if !self.compatible(got, exp) {
                                return false;
                            }
                        }
                        continue;
                    }
                }
                return false;
            } else {
                // int → only no-self iface
                let key = format!("{iface_name}_{mname}");
                let Some(Type::Fn(fp, _)) = self.fns.get(&key) else {
                    return false;
                };
                if fp.len() != expected.len() {
                    return false;
                }
            }
        }
        let _ = all_no_self;
        true
    }

    fn structs_named(&self, n: &str) -> bool {
        self.types
            .get(n)
            .map(|t| matches!(t, Type::Struct { .. }))
            .unwrap_or(false)
            || self.interfaces.is_empty() && false
    }

    /// Resolve impl fn params + return for iface method on a concrete type.
    /// Order: `Iface_Concrete_method` → `Iface_method` → **`Concrete_method`** (Go implicit).
    fn lookup_iface_impl(
        &self,
        iface: &str,
        method: &str,
        concrete: &str,
    ) -> Option<(Vec<Type>, Type)> {
        let keyed = format!("{iface}_{concrete}_{method}");
        if let Some(Type::Fn(fp, fr)) = self.fns.get(&keyed) {
            return Some((fp.clone(), (**fr).clone()));
        }
        let key = format!("{iface}_{method}");
        if let Some(Type::Fn(fp, fr)) = self.fns.get(&key) {
            // Accept if self matches concrete or no-self
            if fp.is_empty() {
                return Some((fp.clone(), (**fr).clone()));
            }
            if self.types_equal_iface_self(&fp[0], &Type::Named(concrete.to_string()))
                || matches!(
                    &fp[0],
                    Type::Struct { name, .. } if name == concrete
                )
            {
                return Some((fp.clone(), (**fr).clone()));
            }
            // no-self: first param is not a struct matching anything for this concrete
            if !matches!(&fp[0], Type::Struct { .. }) && !matches!(&fp[0], Type::Named(_)) {
                return Some((fp.clone(), (**fr).clone()));
            }
            // no-self string/int first arg
            if matches!(&fp[0], Type::String | Type::Int | Type::Bool | Type::Float) {
                return Some((fp.clone(), (**fr).clone()));
            }
        }
        // Go-style method set: `on Concrete { fn method… }` → `Concrete_method`.
        let free = format!("{concrete}_{method}");
        if let Some(Type::Fn(fp, fr)) = self.fns.get(&free) {
            if !fp.is_empty()
                && (self.types_equal_iface_self(&fp[0], &Type::Named(concrete.to_string()))
                    || matches!(&fp[0], Type::Struct { name, .. } if name == concrete))
            {
                return Some((fp.clone(), (**fr).clone()));
            }
        }
        None
    }

    fn types_equal_iface_self(&self, self_ty: &Type, concrete: &Type) -> bool {
        if self_ty == concrete {
            return true;
        }
        match (self_ty, concrete) {
            (Type::Struct { name: a, .. }, Type::Struct { name: b, .. }) => a == b,
            (Type::Named(a), Type::Struct { name: b, .. }) => a == b,
            (Type::Struct { name: a, .. }, Type::Named(b)) => a == b,
            (Type::Named(a), Type::Named(b)) => a == b,
            (Type::Enum { name: a, .. }, Type::Enum { name: b, .. }) => a == b,
            (Type::Named(a), Type::Enum { name: b, .. }) => a == b,
            (Type::Enum { name: a, .. }, Type::Named(b)) => a == b,
            _ => false,
        }
    }

    fn bind_pattern(&mut self, pattern: &Pattern, scrut: &Type) -> Result<(), TypeError> {
        match pattern {
            Pattern::Wildcard => Ok(()),
            Pattern::Ident(n) if n == "None" => {
                if !matches!(scrut, Type::Option(_)) {
                    return Err(TypeError::new("None pattern only valid on Option"));
                }
                Ok(())
            }
            Pattern::Ident(n) => {
                // Unit enum variant pattern (e.g. Point)
                if let Type::Enum { variants, .. } = scrut {
                    if let Some((_, fields)) = variants.iter().find(|(vn, _)| vn == n) {
                        if !fields.is_empty() {
                            return Err(TypeError::new(format!(
                                "variant `{n}` needs {} bindings",
                                fields.len()
                            )));
                        }
                        return Ok(());
                    }
                }
                // Irrefutable binding
                self.define(n, scrut.clone(), false);
                Ok(())
            }
            Pattern::Literal(_) => Ok(()),
            Pattern::Or(alts) => {
                for a in alts {
                    self.bind_pattern(a, scrut)?;
                }
                Ok(())
            }
            Pattern::Tuple(parts) => match scrut {
                Type::Tuple(elems) if elems.len() == parts.len() => {
                    for (p, t) in parts.iter().zip(elems.iter()) {
                        self.bind_pattern(p, t)?;
                    }
                    Ok(())
                }
                other => Err(TypeError::new(format!(
                    "tuple pattern does not match {}",
                    other.display()
                ))),
            },
            Pattern::Variant { name, bindings } => match scrut {
                Type::Result(ok, err) => {
                    if name == "Ok" {
                        if bindings.len() != 1 {
                            return Err(TypeError::new("Ok(v) needs one binding"));
                        }
                        self.bind_pattern(&bindings[0], ok)?;
                    } else if name == "Err" {
                        if bindings.len() != 1 {
                            return Err(TypeError::new("Err(e) needs one binding"));
                        }
                        self.bind_pattern(&bindings[0], err)?;
                    } else {
                        return Err(TypeError::new(format!("unknown Result variant `{name}`")));
                    }
                    Ok(())
                }
                Type::Option(inner) => {
                    if name == "Some" {
                        if bindings.len() != 1 {
                            return Err(TypeError::new("Some(v) needs one binding"));
                        }
                        self.bind_pattern(&bindings[0], inner)?;
                    } else if name == "None" {
                        if !bindings.is_empty() {
                            return Err(TypeError::new("None takes no bindings"));
                        }
                    } else {
                        return Err(TypeError::new(format!("unknown Option variant `{name}`")));
                    }
                    Ok(())
                }
                Type::Enum { variants, .. } => {
                    let Some((_, fields)) = variants.iter().find(|(n, _)| n == name) else {
                        return Err(TypeError::new(format!("unknown variant `{name}`")));
                    };
                    if fields.len() != bindings.len() {
                        return Err(TypeError::new(format!(
                            "variant `{name}` has {} fields, pattern has {}",
                            fields.len(),
                            bindings.len()
                        )));
                    }
                    for (b, ft) in bindings.iter().zip(fields) {
                        self.bind_pattern(b, ft)?;
                    }
                    Ok(())
                }
                Type::Named(n) => {
                    // Resolve Named to its underlying Enum type.
                    if let Some(Type::Enum { variants, .. }) = self.types.get(n) {
                        let variants = variants.clone();
                        let Some((_, fields)) = variants.iter().find(|(vn, _)| vn == name) else {
                            return Err(TypeError::new(format!("unknown variant `{name}`")));
                        };
                        if fields.len() != bindings.len() {
                            return Err(TypeError::new(format!(
                                "variant `{name}` has {} fields, pattern has {}",
                                fields.len(),
                                bindings.len()
                            )));
                        }
                        for (b, ft) in bindings.iter().zip(fields) {
                            self.bind_pattern(b, ft)?;
                        }
                        Ok(())
                    } else {
                        Err(TypeError::new(format!("cannot match variant on {}", n)))
                    }
                }
                other => Err(TypeError::new(format!(
                    "cannot match variant on {}",
                    other.display()
                ))),
            },
            Pattern::Struct { name, fields } => {
                let st = match scrut {
                    Type::Named(n) if n == name => self.types.get(n).cloned(),
                    Type::Struct { name: sn, .. } if sn == name => Some(scrut.clone()),
                    Type::Named(n) => self.types.get(n).cloned(),
                    Type::Struct { .. } => Some(scrut.clone()),
                    _ => None,
                };
                let Some(Type::Struct {
                    name: sn,
                    fields: sfields,
                }) = st
                else {
                    return Err(TypeError::new(format!(
                        "struct pattern `{name}` does not match {}",
                        scrut.display()
                    )));
                };
                if sn != *name && *name != sn {
                    // allow if scrut is the named struct
                    if !matches!(scrut, Type::Named(n) if n == name) {
                        return Err(TypeError::new(format!(
                            "struct pattern `{name}` does not match {sn}"
                        )));
                    }
                }
                for (fname, pat) in fields {
                    let Some((_, ft)) = sfields.iter().find(|(n, _)| n == fname) else {
                        return Err(TypeError::new(format!(
                            "struct `{name}` has no field `{fname}`"
                        )));
                    };
                    self.bind_pattern(pat, ft)?;
                }
                Ok(())
            }
        }
    }

    fn cover_pattern(
        pattern: &Pattern,
        scrut: &Type,
        has_wildcard: &mut bool,
        covered: &mut Vec<String>,
    ) {
        match pattern {
            Pattern::Wildcard => *has_wildcard = true,
            Pattern::Ident(n) if n == "None" => covered.push("None".into()),
            Pattern::Ident(n) => {
                let is_unit_variant = match scrut {
                    Type::Enum { variants, .. } => variants
                        .iter()
                        .any(|(vn, fields)| vn == n && fields.is_empty()),
                    _ => false,
                };
                if is_unit_variant {
                    covered.push(n.clone());
                } else {
                    *has_wildcard = true;
                }
            }
            Pattern::Variant { name, .. } => covered.push(name.clone()),
            Pattern::Literal(Expr::Bool(true)) => covered.push("true".into()),
            Pattern::Literal(Expr::Bool(false)) => covered.push("false".into()),
            Pattern::Literal(_) => {}
            Pattern::Or(alts) => {
                for a in alts {
                    Self::cover_pattern(a, scrut, has_wildcard, covered);
                }
            }
            Pattern::Tuple(_) | Pattern::Struct { .. } => {
                // Struct/tuple patterns are treated as covering when present with wildcard fields.
                *has_wildcard = true;
            }
        }
    }

    fn check_exhaustiveness(&self, scrut: &Type, arms: &[MatchArm]) -> Result<(), TypeError> {
        let mut has_wildcard = false;
        let mut covered: Vec<String> = Vec::new();

        for arm in arms {
            // Guarded arms do not cover their pattern: a failed guard falls
            // through, so `Some(n) if n > 0` does not exhaust Option.
            if arm.guard.is_some() {
                continue;
            }
            Self::cover_pattern(&arm.pattern, scrut, &mut has_wildcard, &mut covered);
        }

        if has_wildcard {
            return Ok(());
        }

        let required: Vec<&str> = match scrut {
            Type::Result(_, _) => vec!["Ok", "Err"],
            Type::Option(_) => vec!["Some", "None"],
            Type::Enum { variants, .. } => variants.iter().map(|(n, _)| n.as_str()).collect(),
            Type::Bool => vec!["true", "false"],
            Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::UInt64
            | Type::Byte
            | Type::String
            | Type::Float => {
                return Err(TypeError::new("this match is not exhaustive")
                    .hint("add a `_` arm to cover remaining values"));
            }
            // Resolve Named types to their underlying enum for exhaustiveness
            Type::Named(n) => {
                if let Some(Type::Enum { variants, .. }) = self.types.get(n) {
                    variants.iter().map(|(vn, _)| vn.as_str()).collect()
                } else {
                    return Ok(());
                }
            }
            _ => return Ok(()),
        };

        let missing: Vec<_> = required
            .iter()
            .filter(|r| !covered.iter().any(|c| c == *r))
            .copied()
            .collect();
        if !missing.is_empty() {
            let list = missing.join(", ");
            return Err(
                TypeError::new(format!("this match is not exhaustive — missing {list}"))
                    .hint(format!("add arm(s) for {list}, or a `_` catch-all")),
            );
        }
        Ok(())
    }

    /// Send-like rules for `crew.kick(f(args…))` after the kick expr is typed.
    /// Fuller model: Copy, string, channels, deep-POD structs, ShareInt/sync handles,
    /// and Option/Result/tuple of sendable payloads (heap-boxed across spawn).
    fn assert_kick_sendable(&self, expr: &Expr) -> Result<(), TypeError> {
        match expr {
            Expr::Call { callee, args } => {
                if let Expr::Ident(name) = callee.as_ref() {
                    if matches!(self.lookup(name).map(|(t, _)| t), Some(Type::Fn(_, _))) {
                        return Err(TypeError::new(
                            "kick requires a named function call; a function-valued callee is not spawnable",
                        )
                        .hint("pass the function value to a named worker function"));
                    }
                }
                self.assert_function_value_safe(callee, "kick")?;
                for a in args {
                    let t = self.peek_type(a)?;
                    if matches!(a, Expr::Lambda { .. }) || matches!(t, Type::Fn(_, _)) {
                        self.assert_function_value_safe(a, "kick")?;
                    }
                    if !self.is_send_ty(&t) {
                        return Err(TypeError::new(format!(
                            "cannot kick value of type {} across a crew task (not Send)",
                            t.display()
                        ))
                        .hint(
                            "Send: Copy, string, channels, deep-POD structs, \
                             Option/Result/tuple of sendables, ShareInt (auto-cloned), \
                             CMap / Mutex / RWMutex / AtomicInt — not arrays/maps/arenas/non-POD",
                        ));
                    }
                }
                Ok(())
            }
            _ => Ok(()),
        }
    }

    /// A function value is only Send when its environment is known and does
    /// not contain unsynchronized mutable state. Named functions have no
    /// closure environment; local function values carry metadata recorded at
    /// their binding or assignment site.
    fn assert_function_value_safe(&self, expr: &Expr, boundary: &str) -> Result<(), TypeError> {
        let is_fn_expr = matches!(expr, Expr::Lambda { .. })
            || matches!(self.peek_type(expr), Ok(Type::Fn(_, _)));
        if !is_fn_expr {
            return Ok(());
        }
        let info = self.fn_capture_info_for_expr(expr);
        if info.unknown {
            return Err(TypeError::new(format!(
                "cannot prove function environment is race-free across {boundary}"
            ))
            .hint(
                "use a named function, a non-capturing lambda, or an explicit Sync handle (Mutex / RWMutex / CMap / AtomicInt / ShareInt / channel)",
            ));
        }
        if let Some(name) = info.unsafe_mut.iter().next() {
            return Err(TypeError::new(format!(
                "mutable capture `{name}` cannot cross a {boundary} boundary"
            ))
            .hint(
                "join before mutation, or capture/use an explicit Sync handle (Mutex / RWMutex / CMap / AtomicInt / ShareInt / channel)",
            ));
        }
        Ok(())
    }

    /// `fan` currently lowers a mapper to a static worker function, so there
    /// is no closure environment to copy or synchronize. Reject every local
    /// capture until the lowering grows an explicit, checked environment.
    fn assert_fan_mapper_safe(&self, mapper: &Expr) -> Result<(), TypeError> {
        if let Expr::Ident(name) = mapper {
            if matches!(self.lookup(name).map(|(t, _)| t), Some(Type::Fn(_, _))) {
                return Err(TypeError::new(
                    "fan needs a named function or an inline non-capturing lambda",
                )
                .hint("pass function values through a named worker before calling fan"));
            }
        }
        let info = self.fn_capture_info_for_expr(mapper);
        if info.unknown {
            return Err(
                TypeError::new("fan mapper has an unknown capture environment")
                    .hint("use a non-capturing mapper; pass all data through the collection"),
            );
        }
        if let Some(name) = info.captured.iter().next() {
            return Err(TypeError::new(format!(
                "fan mapper captures `{name}` across a parallel boundary"
            ))
            .hint("use a non-capturing mapper; pass all data through the collection"));
        }
        Ok(())
    }

    fn fn_capture_info_for_expr(&self, expr: &Expr) -> FnCaptureInfo {
        match expr {
            Expr::Lambda { params, body } => self.analyze_lambda_captures(params, body),
            Expr::Ident(name) => {
                if let Some((ty, _)) = self.lookup(name) {
                    if matches!(ty, Type::Fn(_, _)) {
                        return self
                            .fn_capture_info(name)
                            .cloned()
                            .unwrap_or(FnCaptureInfo {
                                unknown: true,
                                ..FnCaptureInfo::default()
                            });
                    }
                }
                if self.fns.contains_key(name) {
                    FnCaptureInfo::default()
                } else {
                    FnCaptureInfo {
                        unknown: true,
                        ..FnCaptureInfo::default()
                    }
                }
            }
            _ => {
                if matches!(self.peek_type(expr), Ok(Type::Fn(_, _))) {
                    FnCaptureInfo {
                        unknown: true,
                        ..FnCaptureInfo::default()
                    }
                } else {
                    FnCaptureInfo::default()
                }
            }
        }
    }

    fn analyze_lambda_captures(&self, params: &[String], body: &Expr) -> FnCaptureInfo {
        let free = Self::lambda_free_identifiers(params, body);
        let mut info = FnCaptureInfo::default();
        for name in free {
            let Some((ty, is_mut)) = self.lookup(&name).cloned() else {
                // Function names, builtins, and constants are not closure captures.
                continue;
            };
            info.captured.insert(name.clone());
            if matches!(ty, Type::Fn(_, _)) {
                // The closure emitter does not yet clone nested MakoFn values
                // into another environment. Treat this as unknown rather than
                // allowing a function-valued alias to smuggle an environment
                // across the boundary.
                info.unknown = true;
                if let Some(nested) = self.fn_capture_info(&name) {
                    info.captured.extend(nested.captured.iter().cloned());
                    info.unsafe_mut.extend(nested.unsafe_mut.iter().cloned());
                    info.unknown |= nested.unknown;
                } else if !self.fns.contains_key(&name) {
                    info.unknown = true;
                }
                continue;
            }
            if !self.is_lambda_capture_supported(&ty) {
                info.unknown = true;
            }
            if is_mut && !self.is_sync_ty(&ty) {
                info.unsafe_mut.insert(name);
            }
        }
        info
    }

    fn is_lambda_capture_supported(&self, ty: &Type) -> bool {
        match ty {
            Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::UInt64
            | Type::Byte
            | Type::Bool
            | Type::Float
            | Type::String => true,
            Type::Named(name) => name == "ShareInt" || self.structs_named(name),
            Type::Struct { name, .. } => self.structs_named(name),
            _ => false,
        }
    }

    fn lambda_free_identifiers(params: &[String], body: &Expr) -> HashSet<String> {
        let mut bound: HashSet<String> = params.iter().cloned().collect();
        let mut out = HashSet::new();
        Self::collect_free_expr(body, &mut bound, &mut out);
        out
    }

    fn collect_pattern_bindings(pattern: &Pattern, bound: &mut HashSet<String>) {
        match pattern {
            Pattern::Ident(name) if name != "_" => {
                bound.insert(name.clone());
            }
            Pattern::Variant { bindings, .. }
            | Pattern::Or(bindings)
            | Pattern::Tuple(bindings) => {
                for binding in bindings {
                    Self::collect_pattern_bindings(binding, bound);
                }
            }
            Pattern::Struct { fields, .. } => {
                for (_, binding) in fields {
                    Self::collect_pattern_bindings(binding, bound);
                }
            }
            Pattern::Literal(expr) => Self::collect_free_expr(expr, bound, &mut HashSet::new()),
            Pattern::Wildcard | Pattern::Ident(_) => {}
        }
    }

    fn collect_free_expr(expr: &Expr, bound: &mut HashSet<String>, out: &mut HashSet<String>) {
        match expr {
            Expr::Ident(name) => {
                if name != "_" && !bound.contains(name) {
                    out.insert(name.clone());
                }
            }
            Expr::Call { callee, args } => {
                Self::collect_free_expr(callee, bound, out);
                for arg in args {
                    Self::collect_free_expr(arg, bound, out);
                }
            }
            Expr::Method { receiver, args, .. } => {
                Self::collect_free_expr(receiver, bound, out);
                for arg in args {
                    Self::collect_free_expr(arg, bound, out);
                }
            }
            Expr::Binary { left, right, .. } => {
                Self::collect_free_expr(left, bound, out);
                Self::collect_free_expr(right, bound, out);
            }
            Expr::Unary { expr, .. }
            | Expr::Field { base: expr, .. }
            | Expr::Try(expr)
            | Expr::Join(expr) => Self::collect_free_expr(expr, bound, out),
            Expr::Index { base, index } => {
                Self::collect_free_expr(base, bound, out);
                Self::collect_free_expr(index, bound, out);
            }
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                Self::collect_free_expr(base, bound, out);
                for part in [low, high, max].into_iter().flatten() {
                    Self::collect_free_expr(part, bound, out);
                }
            }
            Expr::StructLit { fields, update, .. } => {
                for (_, value) in fields {
                    Self::collect_free_expr(value, bound, out);
                }
                if let Some(update) = update {
                    Self::collect_free_expr(update, bound, out);
                }
            }
            Expr::StringInterp(parts) => {
                for part in parts {
                    if let InterpPart::Expr(value, _) = part {
                        Self::collect_free_expr(value, bound, out);
                    }
                }
            }
            Expr::StructLitPos { values, .. } | Expr::Array(values) | Expr::Tuple(values) => {
                for value in values {
                    Self::collect_free_expr(value, bound, out);
                }
            }
            Expr::Convert { args, .. } => {
                for arg in args {
                    Self::collect_free_expr(arg, bound, out);
                }
            }
            Expr::Make { len, cap, .. } => {
                if let Some(len) = len {
                    Self::collect_free_expr(len, bound, out);
                }
                if let Some(cap) = cap {
                    Self::collect_free_expr(cap, bound, out);
                }
            }
            Expr::ChanOpen { cap, .. } => Self::collect_free_expr(cap, bound, out),
            Expr::Lambda { params, body } => {
                let mut nested_bound = bound.clone();
                nested_bound.extend(params.iter().cloned());
                Self::collect_free_expr(body, &mut nested_bound, out);
            }
            Expr::Match { scrutinee, arms } => {
                Self::collect_free_expr(scrutinee, bound, out);
                for arm in arms {
                    let mut arm_bound = bound.clone();
                    Self::collect_pattern_bindings(&arm.pattern, &mut arm_bound);
                    if let Some(guard) = &arm.guard {
                        Self::collect_free_expr(guard, &mut arm_bound, out);
                    }
                    Self::collect_free_expr(&arm.body, &mut arm_bound, out);
                }
            }
            Expr::IfExpr {
                cond,
                then_block,
                else_block,
            } => {
                Self::collect_free_expr(cond, bound, out);
                Self::collect_free_block(then_block, &mut bound.clone(), out);
                Self::collect_free_block(else_block, &mut bound.clone(), out);
            }
            Expr::Block(block) => Self::collect_free_block(block, &mut bound.clone(), out),
            Expr::Kick { crew, expr } => {
                if !bound.contains(crew) {
                    out.insert(crew.clone());
                }
                Self::collect_free_expr(expr, bound, out);
            }
            Expr::Fan { collection, mapper } => {
                Self::collect_free_expr(collection, bound, out);
                Self::collect_free_expr(mapper, bound, out);
            }
            Expr::Int(_) | Expr::Float(_) | Expr::Bool(_) | Expr::String(_) => {}
        }
    }

    fn collect_free_block(block: &Block, bound: &mut HashSet<String>, out: &mut HashSet<String>) {
        for stmt in &block.stmts {
            Self::collect_free_stmt(stmt, bound, out);
        }
    }

    fn collect_free_stmt(stmt: &Stmt, bound: &mut HashSet<String>, out: &mut HashSet<String>) {
        match stmt {
            Stmt::Let { name, init, .. } => {
                Self::collect_free_expr(init, bound, out);
                if name != "_" {
                    bound.insert(name.clone());
                }
            }
            Stmt::LetMulti { names, init, .. } => {
                Self::collect_free_expr(init, bound, out);
                for name in names {
                    if name != "_" {
                        bound.insert(name.clone());
                    }
                }
            }
            Stmt::LetCommaOk {
                value,
                ok,
                base,
                index,
                ..
            } => {
                Self::collect_free_expr(base, bound, out);
                Self::collect_free_expr(index, bound, out);
                if value != "_" {
                    bound.insert(value.clone());
                }
                if ok != "_" {
                    bound.insert(ok.clone());
                }
            }
            Stmt::Assign { name, value } => {
                if !bound.contains(name) && name != "_" {
                    out.insert(name.clone());
                }
                Self::collect_free_expr(value, bound, out);
            }
            Stmt::IndexAssign { base, index, value } => {
                Self::collect_free_expr(base, bound, out);
                Self::collect_free_expr(index, bound, out);
                Self::collect_free_expr(value, bound, out);
            }
            Stmt::FieldAssign { base, value, .. } => {
                Self::collect_free_expr(base, bound, out);
                Self::collect_free_expr(value, bound, out);
            }
            Stmt::Expr(expr) | Stmt::Return(Some(expr)) => {
                Self::collect_free_expr(expr, bound, out)
            }
            Stmt::Return(None) | Stmt::Break(_) | Stmt::Continue(_) => {}
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                let mut local = bound.clone();
                if let Some(init) = init {
                    Self::collect_free_stmt(init, &mut local, out);
                }
                Self::collect_free_expr(cond, &mut local, out);
                Self::collect_free_block(then_block, &mut local.clone(), out);
                if let Some(else_block) = else_block {
                    Self::collect_free_block(else_block, &mut local, out);
                }
            }
            Stmt::While { cond, body, .. } => {
                Self::collect_free_expr(cond, bound, out);
                Self::collect_free_block(body, &mut bound.clone(), out);
            }
            Stmt::For {
                binders,
                iter,
                body,
                ..
            } => {
                Self::collect_free_expr(iter, bound, out);
                let mut local = bound.clone();
                for binder in binders {
                    if binder != "_" {
                        local.insert(binder.clone());
                    }
                }
                Self::collect_free_block(body, &mut local, out);
            }
            Stmt::CFor {
                init,
                cond,
                post,
                body,
                ..
            } => {
                let mut local = bound.clone();
                Self::collect_free_stmt(init, &mut local, out);
                Self::collect_free_expr(cond, &mut local, out);
                Self::collect_free_stmt(post, &mut local, out);
                Self::collect_free_block(body, &mut local, out);
            }
            Stmt::Defer { body } | Stmt::Unsafe { body } | Stmt::Arena { body, .. } => {
                Self::collect_free_block(body, &mut bound.clone(), out)
            }
            Stmt::Crew { name, body } => {
                let mut local = bound.clone();
                local.insert(name.clone());
                Self::collect_free_block(body, &mut local, out);
            }
            Stmt::Select {
                timeout_ms,
                arms,
                default_arm,
            } => {
                Self::collect_free_expr(timeout_ms, bound, out);
                for (channel, body) in arms {
                    if !bound.contains(channel) {
                        out.insert(channel.clone());
                    }
                    Self::collect_free_block(body, &mut bound.clone(), out);
                }
                if let Some(default_arm) = default_arm {
                    Self::collect_free_block(default_arm, &mut bound.clone(), out);
                }
            }
        }
    }

    fn race_write_root(expr: &Expr) -> Option<&str> {
        match expr {
            Expr::Ident(name) => Some(name),
            Expr::Field { base, .. } | Expr::Index { base, .. } | Expr::Slice { base, .. } => {
                Self::race_write_root(base)
            }
            Expr::Unary { expr, .. } | Expr::Try(expr) => Self::race_write_root(expr),
            _ => None,
        }
    }

    /// SAFE-007: arena-backed values and the arena handle must not escape the block.
    fn expr_is_arena_alloc(expr: &Expr) -> bool {
        match expr {
            Expr::Call { callee, .. } => matches!(
                callee.as_ref(),
                Expr::Ident(n) if n == "arena_ints"
                    || n == "arena_text"
                    || n == "arena_stamp"
                    || n.starts_with("arena_")
            ),
            _ => false,
        }
    }

    /// SAFE-003/007: a sub-slice view must not outlive its base binding.
    /// Target depth must be >= base depth (same or inner scope).
    fn assert_slice_view_lifetime(&self, target_name: &str, value: &Expr) -> Result<(), TypeError> {
        let Expr::Slice { base, .. } = value else {
            // Also chained index of a slice: s[1:3][0] is Index, not a view store.
            return Ok(());
        };
        let Some(base_name) = Self::race_write_root(base) else {
            return Ok(());
        };
        let target_d = self
            .binding_depth
            .get(target_name)
            .copied()
            .unwrap_or(self.scopes.len());
        let base_d = self
            .binding_depth
            .get(base_name)
            .copied()
            .unwrap_or(self.scopes.len());
        // Base defined in a deeper (inner) scope than the target → view escapes.
        if base_d > target_d {
            return Err(TypeError::new(format!(
                "slice view of `{base_name}` cannot be stored in `{target_name}` — base would free first"
            ))
            .hint("keep the view in the same block as its base, or copy elements out"));
        }
        Ok(())
    }

    /// SAFE-007 residual: arena-backed values must not be stored into fields
    /// or outer bindings that outlive the arena.
    fn assert_no_arena_value_store(&self, value: &Expr) -> Result<(), TypeError> {
        if self.arena_depth == 0 {
            return Ok(());
        }
        if let Some(n) = Self::race_write_root(value) {
            if self.arena_owned.contains(n) {
                return Err(TypeError::new(format!(
                    "cannot store arena-backed `{n}` outside the arena"
                ))
                .hint("copy POD/string data out before the arena ends"));
            }
        }
        if let Expr::Call { callee, .. } = value {
            if matches!(
                callee.as_ref(),
                Expr::Ident(cn) if cn.starts_with("arena_")
            ) {
                return Err(
                    TypeError::new("cannot store arena allocation outside the arena")
                        .hint("keep arena-backed values inside the `arena` block"),
                );
            }
        }
        Ok(())
    }

    /// Returning a slice expression of a local is a view that dangles after free.
    fn assert_no_slice_view_return(&self, expr: &Expr) -> Result<(), TypeError> {
        match expr {
            Expr::Slice { base, .. } => {
                if let Some(base_name) = Self::race_write_root(base) {
                    // String slices always return owned copies (mako_str_slice
                    // does malloc+memcpy). Allow return — no dangling reference.
                    if self
                        .lookup(&base_name)
                        .map(|(ty, _)| matches!(ty, Type::String))
                        .unwrap_or(true)
                    {
                        // unwrap_or(true): if the name isn't found in scopes
                        // (e.g. function parameter), assume string is safe.
                        return Ok(());
                    }
                    return Err(TypeError::new(format!(
                        "cannot return a slice view of local `{base_name}`"
                    ))
                    .hint("return the whole slice (moves ownership) or copy into a new slice"));
                }
                Ok(())
            }
            _ => Ok(()),
        }
    }

    fn assert_no_arena_escape(&self, expr: &Expr) -> Result<(), TypeError> {
        match expr {
            Expr::Ident(n) if self.arena_owned.contains(n) => {
                return Err(TypeError::new(format!(
                    "cannot return arena-backed `{n}` out of `arena` block"
                ))
                .hint("copy out POD/string values before the block ends, or use heap `make`"));
            }
            Expr::Ident(n) => {
                if matches!(self.lookup(n), Some((Type::Arena, _))) {
                    return Err(
                        TypeError::new("cannot return arena handle out of `arena` block")
                            .hint("use the arena only inside its scope"),
                    );
                }
            }
            Expr::Index { base, .. } | Expr::Slice { base, .. } | Expr::Field { base, .. } => {
                self.assert_no_arena_escape(base)?;
            }
            Expr::Array(elems) | Expr::Tuple(elems) => {
                for e in elems {
                    self.assert_no_arena_escape(e)?;
                }
            }
            Expr::Call { args, .. } => {
                for a in args {
                    self.assert_no_arena_escape(a)?;
                }
            }
            _ => {}
        }
        Ok(())
    }

    /// Field/index/slice writes need a named mutable root (`let mut` / `mut self`).
    /// Temporaries (`get()[0] = …`, `mk().x = …`) are rejected so we never mutate
    /// a stack value that is about to die — memory-safe by construction, free at runtime.
    fn assert_mutable_write_root(&self, expr: &Expr, kind: &str) -> Result<(), TypeError> {
        let Some(name) = Self::race_write_root(expr) else {
            return Err(
                TypeError::new(format!("cannot {kind} into a temporary value"))
                    .hint("bind the target with `let mut` first"),
            );
        };
        if let Some((_, mutable)) = self.lookup(name) {
            if !*mutable {
                return Err(
                    TypeError::new(format!("cannot {kind} into immutable `{name}`"))
                        .hint(format!("use `let mut {name}`")),
                );
            }
        }
        Ok(())
    }

    fn assert_no_race_write_expr(&self, expr: &Expr) -> Result<(), TypeError> {
        if let Some(name) = Self::race_write_root(expr) {
            self.assert_no_race_write(name)?;
        }
        Ok(())
    }

    /// Deep POD (kick Send): scalars/string fields, nested POD structs, or POD enums.
    fn is_pod_struct(&self, name: &str) -> bool {
        self.is_pod_struct_depth(name, 0)
    }

    fn is_pod_leaf_ty(&self, t: &Type, depth: usize) -> bool {
        if depth > 16 {
            return false;
        }
        match t {
            Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::Byte
            | Type::Bool
            | Type::Float
            | Type::String => true,
            Type::Named(n) => {
                self.is_pod_struct_depth(n, depth + 1) || self.is_pod_enum_depth(n, depth + 1)
            }
            Type::Struct { name: sn, .. } => self.is_pod_struct_depth(sn, depth + 1),
            Type::Enum { variants, .. } => variants
                .iter()
                .all(|(_, fields)| fields.iter().all(|f| self.is_pod_leaf_ty(f, depth + 1))),
            _ => false,
        }
    }

    fn is_pod_enum_depth(&self, name: &str, depth: usize) -> bool {
        if depth > 16 {
            return false;
        }
        match self.types.get(name) {
            Some(Type::Enum { variants, .. }) => variants
                .iter()
                .all(|(_, fields)| fields.iter().all(|f| self.is_pod_leaf_ty(f, depth + 1))),
            _ => false,
        }
    }

    fn is_pod_struct_depth(&self, name: &str, depth: usize) -> bool {
        if depth > 16 {
            return false;
        }
        match self.types.get(name) {
            Some(Type::Struct { fields, .. }) => fields
                .iter()
                .all(|(_, t)| self.is_pod_leaf_ty(t, depth + 1)),
            _ => false,
        }
    }

    fn enums_named(&self, n: &str) -> bool {
        self.types
            .get(n)
            .map(|t| matches!(t, Type::Enum { .. }))
            .unwrap_or(false)
    }

    /// Element types allowed in `chan[T]` / `make(chan[T], n)`.
    fn is_chan_element_ty(&self, t: &Type) -> bool {
        match t {
            Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::Byte
            | Type::Bool
            | Type::Float
            | Type::String => true,
            Type::Named(n)
                if n != "ShareInt"
                    && n != "Arena"
                    && n != "Crew"
                    && (self.structs_named(n) || self.enums_named(n)) =>
            {
                true
            }
            Type::Struct { .. } | Type::Enum { .. } => true,
            Type::Tuple(elems) => elems.iter().all(|e| self.is_chan_element_ty(e)),
            _ => false,
        }
    }

    /// Reflect bag: POD leaves, nested POD structs, Option/Result/array/map of reflectable.
    fn is_reflectable_struct(&self, name: &str) -> bool {
        match self.types.get(name) {
            Some(Type::Struct { fields, .. }) => {
                fields.iter().all(|(_, t)| self.is_reflectable_ty(t))
            }
            _ => false,
        }
    }

    fn is_reflectable_ty(&self, t: &Type) -> bool {
        match t {
            Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::Byte
            | Type::Bool
            | Type::Float
            | Type::String => true,
            Type::Named(n) => self.is_reflectable_struct(n),
            Type::Struct { name: sn, .. } => self.is_reflectable_struct(sn),
            Type::Option(inner) => self.is_reflectable_ty(inner),
            Type::Result(ok, err) => self.is_reflectable_ty(ok) && self.is_reflectable_ty(err),
            Type::Array(elem) => self.is_reflectable_ty(elem),
            Type::Map(k, v) => {
                // Reflect maps with string or int keys and reflectable values.
                matches!(
                    k.as_ref(),
                    Type::String | Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::Byte
                ) && self.is_reflectable_ty(v)
            }
            Type::Tuple(elems) => elems.iter().all(|e| self.is_reflectable_ty(e)),
            _ => false,
        }
    }

    fn is_kick_sendable_ty(&self, t: &Type) -> bool {
        if is_kick_sendable(t) {
            return true;
        }
        match t {
            Type::Named(n) if self.is_pod_struct(n) || self.is_pod_enum_depth(n, 0) => true,
            Type::Struct { name, .. } if self.is_pod_struct(name) => true,
            // Fuller Send: sum types and products of sendable payloads.
            Type::Option(inner) => self.is_kick_sendable_ty(inner),
            Type::Result(ok, err) => self.is_kick_sendable_ty(ok) && self.is_kick_sendable_ty(err),
            Type::Tuple(elems) => elems.iter().all(|e| self.is_kick_sendable_ty(e)),
            Type::Enum { name, variants, .. } => {
                // Named enum: prefer depth helper (covers unit + POD payloads).
                if !name.is_empty() && self.is_pod_enum_depth(name, 0) {
                    return true;
                }
                variants
                    .iter()
                    .all(|(_, fields)| fields.iter().all(|f| self.is_kick_sendable_ty(f)))
            }
            _ => false,
        }
    }

    /// Type-level Sync: safe concurrent shared mutation (internal locking/atomics).
    fn is_sync_ty(&self, t: &Type) -> bool {
        matches!(t, Type::Chan(_) | Type::CMap | Type::Mutex | Type::RWMutex)
            || matches!(
                t,
                Type::Named(n) if n == "ShareInt"
                    || n == "AtomicInt"
                    || n == "Mutex"
                    || n == "RWMutex"
                    || n == "CMap"
                    || n == "WaitGroup"
                    || n == "TlsServer"
            )
    }

    /// Type-level Send: may cross a kick boundary (same as kick sendable rules).
    fn is_send_ty(&self, t: &Type) -> bool {
        self.is_kick_sendable_ty(t)
    }

    /// Static race model: reject concurrent mutation of a `let mut` local whose
    /// value was passed into a kicked task before join. Copy types are value-copied
    /// at kick; string/POD are cloned/boxed. Remaining mut captures are race-prone
    /// unless they are explicit Sync handles.
    fn note_kick_race_captures(&mut self, expr: &Expr) {
        let Expr::Call { args, .. } = expr else {
            return;
        };
        let mut frame = HashSet::new();
        for a in args {
            if let Expr::Ident(name) = a {
                if let Some((t, is_mut)) = self.lookup(name).cloned() {
                    if !is_mut {
                        continue;
                    }
                    if is_copy_type(&t) {
                        continue;
                    }
                    // Explicit Sync handles: intentional shared mutable state.
                    if self.is_sync_ty(&t) {
                        self.race_sync_locals.insert(name.clone());
                        continue;
                    }
                    // String and deep-POD structs are cloned/boxed at kick — no shared race.
                    if matches!(t, Type::String)
                        || matches!(&t, Type::Named(n) if self.is_pod_struct(n))
                        || matches!(&t, Type::Struct { name: sn, .. } if self.is_pod_struct(sn))
                    {
                        continue;
                    }
                    // Option/Result/tuple/enum: heap-boxed at kick, but parent still owns
                    // the original binding — mutating it before join is a logical data race.
                    if matches!(
                        t,
                        Type::Option(_)
                            | Type::Result(_, _)
                            | Type::Tuple(_)
                            | Type::Enum { .. }
                            | Type::Array(_)
                            | Type::Map(_, _)
                    ) {
                        frame.insert(name.clone());
                        continue;
                    }
                    frame.insert(name.clone());
                }
            }
        }
        for n in &frame {
            self.race_outstanding.insert(n.clone());
        }
        self.race_stack.push(frame);
    }

    fn race_join_clear(&mut self) {
        // Pop latest kick frame; rebuild outstanding as union of remaining frames.
        let _ = self.race_stack.pop();
        self.race_outstanding.clear();
        for frame in &self.race_stack {
            self.race_outstanding.extend(frame.iter().cloned());
        }
    }

    fn assert_no_race_write(&self, name: &str) -> Result<(), TypeError> {
        if self.race_outstanding.contains(name) {
            return Err(TypeError::new(format!(
                "potential data race: mutating `{name}` while a kicked task may still use it"
            ))
            .hint(
                "join the job before writing, or use Mutex / AtomicInt / ShareInt / channels \
                 for shared mutable state (Sync handles)",
            ));
        }
        Ok(())
    }

    /// Best-effort type of an expression without re-running ownership effects.
    fn peek_type(&self, expr: &Expr) -> Result<Type, TypeError> {
        match expr {
            Expr::Int(_) => Ok(Type::Int),
            Expr::Float(_) => Ok(Type::Float),
            Expr::Bool(_) => Ok(Type::Bool),
            Expr::String(_) => Ok(Type::String),
            Expr::Ident(n) => {
                if let Some((t, _)) = self.lookup(n) {
                    Ok(t.clone())
                } else if let Some(t) = self.fns.get(n) {
                    Ok(t.clone())
                } else {
                    Ok(Type::Int) // fallback; already typechecked
                }
            }
            Expr::Unary { expr, .. } => self.peek_type(expr),
            Expr::Binary { left, .. } => self.peek_type(left),
            Expr::Call { callee, .. } => {
                if let Expr::Ident(name) = callee.as_ref() {
                    if let Some(Type::Fn(_, ret)) = self.lookup(name).map(|(t, _)| t) {
                        return Ok((**ret).clone());
                    }
                    if let Some(Type::Fn(_, ret)) = self.fns.get(name) {
                        return Ok((**ret).clone());
                    }
                }
                if let Ok(Type::Fn(_, ret)) = self.peek_type(callee) {
                    return Ok(*ret);
                }
                Ok(Type::Int)
            }
            Expr::Array(xs) => {
                let e = if let Some(x) = xs.first() {
                    self.peek_type(x)?
                } else {
                    Type::Int
                };
                Ok(Type::Array(Box::new(e)))
            }
            Expr::Make { ty, .. } | Expr::Convert { ty, .. } => {
                // Fall through — Make already typechecked
                let _ = ty;
                Ok(Type::Int)
            }
            Expr::ChanOpen { elem, .. } => {
                let et = match elem {
                    TypeExpr::Named(n) if n == "string" => Type::String,
                    TypeExpr::Named(n) if n == "int" || n == "int64" => Type::Int,
                    TypeExpr::Named(n) if n == "bool" => Type::Bool,
                    TypeExpr::Named(n) if n == "float" => Type::Float,
                    _ => Type::Int,
                };
                Ok(Type::Chan(Box::new(et)))
            }
            _ => Ok(Type::Int),
        }
    }
}

/// Types that may cross a `crew.kick` boundary (Send-like seed).
/// POD structs are handled in `TypeChecker::is_kick_sendable_ty`.
fn is_kick_sendable(t: &Type) -> bool {
    if is_copy_type(t) {
        return true;
    }
    match t {
        Type::String | Type::Chan(_) | Type::Job(_) | Type::Void => true,
        // Thread-safe shared handles: sending the handle shares the same object
        // across tasks, which is their whole purpose (internal locking / atomics).
        Type::CMap | Type::Mutex | Type::RWMutex => true,
        // ShareInt: atomic RC + kick auto-clones onto the heap (see codegen).
        Type::Named(n) if n == "ShareInt" || n == "AtomicInt" => true,
        // TlsConn: exclusive SSL* handle (void*). Send moves ownership to the worker —
        // caller must not use the conn after kick (same model as raw TCP fds).
        Type::Named(n) if n == "TlsConn" => true,
        // TlsServer: synchronized shared SSL_CTX/SNI configuration handle. The
        // runtime retains it while accepted connections are handshaking.
        Type::Named(n) if n == "TlsServer" => true,
        Type::Named(n) if n == "Arena" || n == "Crew" => false,
        Type::Named(_) => false, // non-POD / handled in TypeChecker::is_kick_sendable_ty
        // Option/Result/tuple/enum handled in TypeChecker::is_kick_sendable_ty (fuller Send).
        Type::Array(_)
        | Type::Map(_, _)
        | Type::Tuple(_)
        | Type::Option(_)
        | Type::Result(_, _) => false,
        // First-class fn values (MakoFn fat pointer) may cross kick — env is heap.
        Type::Fn(_, _) => true,
        Type::Interface { .. } | Type::Enum { .. } => false,
        _ => false,
    }
}

fn is_int_family(t: &Type) -> bool {
    matches!(
        t,
        Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::UInt64 | Type::Byte
    )
}

/// Integer kinds that may be inhabited by untyped int literals (excl. uint64 for now).
fn is_copy_type(t: &Type) -> bool {
    matches!(
        t,
        Type::Int
            | Type::Int64
            | Type::Int32
            | Type::Int8
            | Type::UInt64
            | Type::Byte
            | Type::Bool
            | Type::Float
            // Uuid/ULID: 16-byte POD — free to copy, kick, re-read (hot path).
            | Type::Uuid
    )
}

fn is_literal_int_kind(t: &Type) -> bool {
    matches!(
        t,
        Type::Int | Type::Int64 | Type::Int32 | Type::Int8 | Type::Byte
    )
}

fn try_fold_const(expr: &Expr) -> Result<i64, ()> {
    fold_const_expr(expr).map_err(|_| ())
}

fn fold_const_expr(expr: &Expr) -> Result<i64, TypeError> {
    fold_const_expr_with(expr, &HashMap::new(), &HashMap::new(), &HashMap::new())
}

/// Fold with const bindings and const-fn table (for `const fn` evaluation).
/// Shared with the native IR so `const MAX = …` resolves at lower time.
pub fn fold_const_expr_with(
    expr: &Expr,
    consts: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<i64, TypeError> {
    match expr {
        Expr::Int(n) => Ok(*n),
        Expr::Ident(n) => consts
            .get(n)
            .copied()
            .ok_or_else(|| TypeError::new(format!("`{n}` is not a const integer"))),
        Expr::Binary { op, left, right } => {
            // Short-circuit logical ops (0 = false, nonzero = true).
            if matches!(op, BinOp::And | BinOp::Or) {
                let l = fold_const_expr_with(left, consts, const_strs, const_fns)?;
                match op {
                    BinOp::And => {
                        if l == 0 {
                            return Ok(0);
                        }
                        let r = fold_const_expr_with(right, consts, const_strs, const_fns)?;
                        return Ok(if r != 0 { 1 } else { 0 });
                    }
                    BinOp::Or => {
                        if l != 0 {
                            return Ok(1);
                        }
                        let r = fold_const_expr_with(right, consts, const_strs, const_fns)?;
                        return Ok(if r != 0 { 1 } else { 0 });
                    }
                    _ => unreachable!(),
                }
            }
            // String equality seed before int path (idents may be const strings).
            if matches!(op, BinOp::Eq | BinOp::Ne) {
                if let (Ok(ls), Ok(rs)) = (
                    fold_const_str_with(left, consts, const_strs, const_fns),
                    fold_const_str_with(right, consts, const_strs, const_fns),
                ) {
                    return Ok(match op {
                        BinOp::Eq => {
                            if ls == rs {
                                1
                            } else {
                                0
                            }
                        }
                        BinOp::Ne => {
                            if ls != rs {
                                1
                            } else {
                                0
                            }
                        }
                        _ => unreachable!(),
                    });
                }
            }
            let l = fold_const_expr_with(left, consts, const_strs, const_fns)?;
            let r = fold_const_expr_with(right, consts, const_strs, const_fns)?;
            match op {
                BinOp::Add => Ok(l.wrapping_add(r)),
                BinOp::Sub => Ok(l.wrapping_sub(r)),
                BinOp::Mul => Ok(l.wrapping_mul(r)),
                BinOp::Div => {
                    if r == 0 {
                        Err(TypeError::new("const division by zero"))
                    } else {
                        Ok(l / r)
                    }
                }
                BinOp::Mod => {
                    if r == 0 {
                        Err(TypeError::new("const modulo by zero"))
                    } else {
                        Ok(l % r)
                    }
                }
                BinOp::BitAnd => Ok(l & r),
                BinOp::BitOr => Ok(l | r),
                BinOp::BitXor => Ok(l ^ r),
                BinOp::BitClear => Ok(l & !r),
                BinOp::Shl => Ok(l.wrapping_shl((r as u32) & 63)),
                BinOp::Shr => Ok(l.wrapping_shr((r as u32) & 63)),
                BinOp::Eq => Ok(if l == r { 1 } else { 0 }),
                BinOp::Ne => Ok(if l != r { 1 } else { 0 }),
                BinOp::Lt => Ok(if l < r { 1 } else { 0 }),
                BinOp::Le => Ok(if l <= r { 1 } else { 0 }),
                BinOp::Gt => Ok(if l > r { 1 } else { 0 }),
                BinOp::Ge => Ok(if l >= r { 1 } else { 0 }),
                BinOp::And | BinOp::Or => unreachable!(),
            }
        }
        // `s[i]` on a const string → byte value 0..255 (matches runtime string index).
        Expr::Index { base, index } => {
            let s = fold_const_str_with(base, consts, const_strs, const_fns)?;
            let i = fold_const_expr_with(index, consts, const_strs, const_fns)?;
            if i < 0 || (i as usize) >= s.len() {
                return Err(TypeError::new(format!(
                    "const string index out of bounds: index {i}, len {}",
                    s.len()
                )));
            }
            Ok(s.as_bytes()[i as usize] as i64)
        }
        Expr::Unary { op, expr } => {
            let v = fold_const_expr_with(expr, consts, const_strs, const_fns)?;
            match op {
                UnaryOp::Neg => Ok(v.wrapping_neg()),
                UnaryOp::BitNot => Ok(!v),
                UnaryOp::Not => Ok(if v == 0 { 1 } else { 0 }),
            }
        }
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            let c = fold_const_expr_with(cond, consts, const_strs, const_fns)?;
            if c != 0 {
                eval_const_fn_body(then_block, consts, const_strs, const_fns)
            } else {
                eval_const_fn_body(else_block, consts, const_strs, const_fns)
            }
        }
        Expr::Match { scrutinee, arms } => {
            let scrut = fold_const_expr_with(scrutinee, consts, const_strs, const_fns)?;
            for arm in arms {
                if let Some(binds) =
                    match_const_pattern(&arm.pattern, scrut, consts, const_strs, const_fns)?
                {
                    let mut env = consts.clone();
                    for (k, v) in binds {
                        env.insert(k, v);
                    }
                    return fold_const_expr_with(&arm.body, &env, const_strs, const_fns);
                }
            }
            Err(TypeError::new(
                "const match is non-exhaustive for this value (no arm matched)",
            ))
        }
        Expr::Call { callee, args } => {
            let Expr::Ident(fname) = callee.as_ref() else {
                return Err(TypeError::new("const call must name a const fn"));
            };
            // String builtins foldable to int.
            if (fname == "str_len" || fname == "len") && args.len() == 1 {
                let s = fold_const_str_with(&args[0], consts, const_strs, const_fns)?;
                return Ok(s.len() as i64);
            }
            if fname == "str_eq" && args.len() == 2 {
                let a = fold_const_str_with(&args[0], consts, const_strs, const_fns)?;
                let b = fold_const_str_with(&args[1], consts, const_strs, const_fns)?;
                return Ok(if a == b { 1 } else { 0 });
            }
            let Some(f) = const_fns.get(fname) else {
                return Err(TypeError::new(format!(
                    "`{fname}` is not a const fn (mark with `const fn`)"
                )));
            };
            if f.params.len() != args.len() {
                return Err(TypeError::new(format!(
                    "const fn `{fname}` expects {} args",
                    f.params.len()
                )));
            }
            let (locals_i, locals_s) = bind_const_fn_args(f, args, consts, const_strs, const_fns)?;
            // Int-returning const fn (string-returning goes through fold_const_str_with).
            eval_const_fn_body(&f.body, &locals_i, &locals_s, const_fns)
        }
        _ => Err(TypeError::new(
            "const initializer must be a comptime-foldable integer expression",
        )),
    }
}

/// Bind const-fn arguments into int/string local maps.
fn bind_const_fn_args(
    f: &FnDef,
    args: &[Expr],
    consts: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<(HashMap<String, i64>, HashMap<String, String>), TypeError> {
    let mut locals_i = consts.clone();
    let mut locals_s = const_strs.clone();
    for (p, a) in f.params.iter().zip(args.iter()) {
        let is_str = matches!(&p.ty, TypeExpr::Named(n) if n == "string");
        if is_str {
            let s = fold_const_str_with(a, consts, const_strs, const_fns)?;
            locals_s.insert(p.name.clone(), s);
        } else {
            let v = fold_const_expr_with(a, consts, const_strs, const_fns)?;
            locals_i.insert(p.name.clone(), v);
        }
    }
    Ok((locals_i, locals_s))
}

/// Fold a compile-time string expression (literals, const names, `+` concat, const fn).
/// Shared with the native IR const table.
pub fn fold_const_str_with(
    expr: &Expr,
    consts: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<String, TypeError> {
    match expr {
        Expr::String(s) => Ok(s.clone()),
        Expr::Ident(n) => const_strs
            .get(n)
            .cloned()
            .ok_or_else(|| TypeError::new(format!("`{n}` is not a const string"))),
        Expr::Binary {
            op: BinOp::Add,
            left,
            right,
        } => {
            let l = fold_const_str_with(left, consts, const_strs, const_fns)?;
            let r = fold_const_str_with(right, consts, const_strs, const_fns)?;
            Ok(l + &r)
        }
        Expr::IfExpr {
            cond,
            then_block,
            else_block,
        } => {
            let c = fold_const_expr_with(cond, consts, const_strs, const_fns)?;
            if c != 0 {
                eval_const_fn_body_str(then_block, consts, const_strs, const_fns)
            } else {
                eval_const_fn_body_str(else_block, consts, const_strs, const_fns)
            }
        }
        Expr::Call { callee, args } => {
            let Expr::Ident(fname) = callee.as_ref() else {
                return Err(TypeError::new("const string call must name a helper"));
            };
            if fname == "str_concat" && args.len() == 2 {
                let a = fold_const_str_with(&args[0], consts, const_strs, const_fns)?;
                let b = fold_const_str_with(&args[1], consts, const_strs, const_fns)?;
                return Ok(a + &b);
            }
            let Some(f) = const_fns.get(fname) else {
                return Err(TypeError::new(format!(
                    "const string expression cannot call `{fname}`"
                )));
            };
            if f.params.len() != args.len() {
                return Err(TypeError::new(format!(
                    "const fn `{fname}` expects {} args",
                    f.params.len()
                )));
            }
            let (locals_i, locals_s) = bind_const_fn_args(f, args, consts, const_strs, const_fns)?;
            eval_const_fn_body_str(&f.body, &locals_i, &locals_s, const_fns)
        }
        _ => Err(TypeError::new(
            "const string must be a literal, const name, `+`/concat, if-expr, or string const fn",
        )),
    }
}

/// Match an int scrutinee against a pattern. Returns `Some(bindings)` on match,
/// `None` if this arm does not match (try next). Errors on non-int patterns.
fn match_const_pattern(
    pat: &Pattern,
    scrut: i64,
    consts: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<Option<HashMap<String, i64>>, TypeError> {
    match pat {
        Pattern::Wildcard => Ok(Some(HashMap::new())),
        Pattern::Ident(n) if n == "_" => Ok(Some(HashMap::new())),
        Pattern::Ident(n) => {
            // Bare ident binds (or matches unit variant names — treat as bind for int seed).
            let mut m = HashMap::new();
            m.insert(n.clone(), scrut);
            Ok(Some(m))
        }
        Pattern::Literal(e) => {
            let v = fold_const_expr_with(e, consts, const_strs, const_fns)?;
            Ok(if v == scrut {
                Some(HashMap::new())
            } else {
                None
            })
        }
        Pattern::Or(alts) => {
            for a in alts {
                if let Some(b) = match_const_pattern(a, scrut, consts, const_strs, const_fns)? {
                    return Ok(Some(b));
                }
            }
            Ok(None)
        }
        Pattern::Variant { .. } | Pattern::Tuple(_) | Pattern::Struct { .. } => Err(
            TypeError::new("const match seed supports int / `_` / or-patterns only"),
        ),
    }
}

fn eval_const_fn_body(
    body: &Block,
    locals: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<i64, TypeError> {
    // Support: let/assign/return, trailing expr, if, match (via expr), while (bounded).
    let mut env = locals.clone();
    let mut str_env = const_strs.clone();
    let mut i = 0;
    while i < body.stmts.len() {
        let stmt = &body.stmts[i];
        let is_last = i + 1 == body.stmts.len();
        match stmt {
            Stmt::Return(Some(e)) => return fold_const_expr_with(e, &env, &str_env, const_fns),
            Stmt::Return(None) => {
                return Err(TypeError::new("const fn cannot return void"));
            }
            Stmt::Let { name, init, .. } => {
                if let Ok(v) = fold_const_expr_with(init, &env, &str_env, const_fns) {
                    env.insert(name.clone(), v);
                } else if let Ok(s) = fold_const_str_with(init, &env, &str_env, const_fns) {
                    str_env.insert(name.clone(), s);
                } else {
                    return Err(TypeError::new(format!(
                        "const let `{name}` init is not a const int or string"
                    )));
                }
            }
            Stmt::Assign { name, value } => {
                if env.contains_key(name) {
                    let v = fold_const_expr_with(value, &env, &str_env, const_fns)?;
                    env.insert(name.clone(), v);
                } else if str_env.contains_key(name) {
                    let s = fold_const_str_with(value, &env, &str_env, const_fns)?;
                    str_env.insert(name.clone(), s);
                } else {
                    return Err(TypeError::new(format!(
                        "const assign to unknown `{name}` (declare with let first)"
                    )));
                }
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                if let Some(init_stmt) = init {
                    match init_stmt.as_ref() {
                        Stmt::Let { name, init, .. } => {
                            if let Ok(v) = fold_const_expr_with(init, &env, &str_env, const_fns) {
                                env.insert(name.clone(), v);
                            } else if let Ok(s) =
                                fold_const_str_with(init, &env, &str_env, const_fns)
                            {
                                str_env.insert(name.clone(), s);
                            } else {
                                return Err(TypeError::new(
                                    "const if init may only be a let of a const expression",
                                ));
                            }
                        }
                        _ => {
                            return Err(TypeError::new(
                                "const if init may only be a let of a const expression",
                            ));
                        }
                    }
                }
                let c = fold_const_expr_with(cond, &env, &str_env, const_fns)?;
                if c != 0 {
                    return eval_const_fn_body(then_block, &env, &str_env, const_fns);
                } else if let Some(eb) = else_block {
                    return eval_const_fn_body(eb, &env, &str_env, const_fns);
                }
            }
            Stmt::While {
                cond, body: wbody, ..
            } => match run_const_while(cond, wbody, &mut env, &str_env, const_fns)? {
                Some(ret) => return Ok(ret),
                None => {}
            },
            Stmt::For {
                binders,
                iter,
                body: fbody,
                ..
            } => match run_const_for_count(binders, iter, fbody, &mut env, &str_env, const_fns)? {
                Some(ret) => return Ok(ret),
                None => {}
            },
            Stmt::CFor {
                init,
                cond,
                post,
                body: fbody,
                ..
            } => match run_const_cfor(init, cond, post, fbody, &mut env, &str_env, const_fns)? {
                Some(ret) => return Ok(ret),
                None => {}
            },
            Stmt::Expr(e) if is_last => {
                return fold_const_expr_with(e, &env, &str_env, const_fns);
            }
            Stmt::Expr(_) => {
                let e = match stmt {
                    Stmt::Expr(e) => e,
                    _ => unreachable!(),
                };
                if fold_const_expr_with(e, &env, &str_env, const_fns).is_err() {
                    let _ = fold_const_str_with(e, &env, &str_env, const_fns)?;
                }
            }
            _ => {
                return Err(TypeError::new(
                    "const fn body may only use let/assign/return/if/while/for of const expressions",
                ));
            }
        }
        i += 1;
    }
    Err(TypeError::new("const fn body has no return value"))
}

/// Evaluate a string-returning const fn / block (if arms, let, return).
fn eval_const_fn_body_str(
    body: &Block,
    locals: &HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<String, TypeError> {
    let mut env = locals.clone();
    let mut str_env = const_strs.clone();
    let mut i = 0;
    while i < body.stmts.len() {
        let stmt = &body.stmts[i];
        let is_last = i + 1 == body.stmts.len();
        match stmt {
            Stmt::Return(Some(e)) => {
                return fold_const_str_with(e, &env, &str_env, const_fns);
            }
            Stmt::Return(None) => {
                return Err(TypeError::new("const fn cannot return void"));
            }
            Stmt::Let { name, init, .. } => {
                if let Ok(v) = fold_const_expr_with(init, &env, &str_env, const_fns) {
                    env.insert(name.clone(), v);
                } else if let Ok(s) = fold_const_str_with(init, &env, &str_env, const_fns) {
                    str_env.insert(name.clone(), s);
                } else {
                    return Err(TypeError::new(format!(
                        "const let `{name}` init is not a const int or string"
                    )));
                }
            }
            Stmt::Assign { name, value } => {
                if env.contains_key(name) {
                    let v = fold_const_expr_with(value, &env, &str_env, const_fns)?;
                    env.insert(name.clone(), v);
                } else if str_env.contains_key(name) {
                    let s = fold_const_str_with(value, &env, &str_env, const_fns)?;
                    str_env.insert(name.clone(), s);
                } else {
                    return Err(TypeError::new(format!("const assign to unknown `{name}`")));
                }
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                if let Some(init_stmt) = init {
                    if let Stmt::Let { name, init, .. } = init_stmt.as_ref() {
                        if let Ok(v) = fold_const_expr_with(init, &env, &str_env, const_fns) {
                            env.insert(name.clone(), v);
                        } else if let Ok(s) = fold_const_str_with(init, &env, &str_env, const_fns) {
                            str_env.insert(name.clone(), s);
                        }
                    }
                }
                let c = fold_const_expr_with(cond, &env, &str_env, const_fns)?;
                if c != 0 {
                    return eval_const_fn_body_str(then_block, &env, &str_env, const_fns);
                } else if let Some(eb) = else_block {
                    return eval_const_fn_body_str(eb, &env, &str_env, const_fns);
                }
            }
            Stmt::Expr(e) if is_last => {
                return fold_const_str_with(e, &env, &str_env, const_fns);
            }
            Stmt::Expr(e) => {
                if fold_const_expr_with(e, &env, &str_env, const_fns).is_err() {
                    let _ = fold_const_str_with(e, &env, &str_env, const_fns)?;
                }
            }
            _ => {
                return Err(TypeError::new(
                    "string const fn body may only use let/assign/return/if (no loops yet)",
                ));
            }
        }
        i += 1;
    }
    Err(TypeError::new("const string fn body has no return value"))
}

/// Control flow from a const loop body iteration.
enum ConstLoopCtl {
    /// `return expr` — exit the whole const fn.
    Return(i64),
    /// `break` — leave the innermost loop.
    Break,
    /// `continue` — next iteration (still run C-for post).
    Continue,
    /// Finished the body statements normally.
    Fallthrough,
}

fn run_const_while(
    cond: &Expr,
    body: &Block,
    env: &mut HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<Option<i64>, TypeError> {
    const MAX_ITERS: i64 = 100_000;
    let mut iters = 0i64;
    while fold_const_expr_with(cond, env, const_strs, const_fns)? != 0 {
        iters += 1;
        if iters > MAX_ITERS {
            return Err(TypeError::new(
                "const while exceeded 100000 iterations (runaway loop?)",
            ));
        }
        match eval_const_loop_body(body, env, const_strs, const_fns)? {
            ConstLoopCtl::Return(v) => return Ok(Some(v)),
            ConstLoopCtl::Break => break,
            ConstLoopCtl::Continue | ConstLoopCtl::Fallthrough => {}
        }
    }
    Ok(None)
}

fn run_const_for_count(
    binders: &[String],
    iter: &Expr,
    body: &Block,
    env: &mut HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<Option<i64>, TypeError> {
    let n = fold_const_expr_with(iter, env, const_strs, const_fns)?;
    if n < 0 {
        return Err(TypeError::new("const for count must be non-negative"));
    }
    if n > 100_000 {
        return Err(TypeError::new(
            "const for count exceeds 100000 (runaway loop?)",
        ));
    }
    let binder = binders.iter().find(|b| b.as_str() != "_").cloned();
    for idx in 0..n {
        if let Some(ref b) = binder {
            env.insert(b.clone(), idx);
        }
        match eval_const_loop_body(body, env, const_strs, const_fns)? {
            ConstLoopCtl::Return(v) => return Ok(Some(v)),
            ConstLoopCtl::Break => break,
            ConstLoopCtl::Continue | ConstLoopCtl::Fallthrough => {}
        }
    }
    Ok(None)
}

fn run_const_cfor(
    init: &Stmt,
    cond: &Expr,
    post: &Stmt,
    body: &Block,
    env: &mut HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<Option<i64>, TypeError> {
    match init {
        Stmt::Let { name, init: iv, .. } => {
            let v = fold_const_expr_with(iv, env, const_strs, const_fns)?;
            env.insert(name.clone(), v);
        }
        Stmt::Assign { name, value } => {
            let v = fold_const_expr_with(value, env, const_strs, const_fns)?;
            env.insert(name.clone(), v);
        }
        _ => {
            return Err(TypeError::new(
                "const C-style for init may only be let or assign",
            ));
        }
    }
    const MAX_ITERS: i64 = 100_000;
    let mut iters = 0i64;
    while fold_const_expr_with(cond, env, const_strs, const_fns)? != 0 {
        iters += 1;
        if iters > MAX_ITERS {
            return Err(TypeError::new(
                "const C-style for exceeded 100000 iterations (runaway loop?)",
            ));
        }
        let ctl = eval_const_loop_body(body, env, const_strs, const_fns)?;
        match ctl {
            ConstLoopCtl::Return(v) => return Ok(Some(v)),
            ConstLoopCtl::Break => break, // do not run post
            ConstLoopCtl::Continue | ConstLoopCtl::Fallthrough => {
                // `continue` still runs the post clause (Go/C semantics).
                match post {
                    Stmt::Let { name, init: iv, .. } => {
                        let v = fold_const_expr_with(iv, env, const_strs, const_fns)?;
                        env.insert(name.clone(), v);
                    }
                    Stmt::Assign { name, value } => {
                        let v = fold_const_expr_with(value, env, const_strs, const_fns)?;
                        env.insert(name.clone(), v);
                    }
                    Stmt::Expr(e) => {
                        let _ = fold_const_expr_with(e, env, const_strs, const_fns)?;
                    }
                    _ => {
                        return Err(TypeError::new(
                            "const C-style for post may only be let/assign/expr",
                        ));
                    }
                }
            }
        }
    }
    Ok(None)
}

/// Run one iteration of a const loop body.
fn eval_const_loop_body(
    body: &Block,
    env: &mut HashMap<String, i64>,
    const_strs: &HashMap<String, String>,
    const_fns: &HashMap<String, FnDef>,
) -> Result<ConstLoopCtl, TypeError> {
    for stmt in &body.stmts {
        match stmt {
            Stmt::Return(Some(e)) => {
                return Ok(ConstLoopCtl::Return(fold_const_expr_with(
                    e, env, const_strs, const_fns,
                )?));
            }
            Stmt::Return(None) => {
                return Err(TypeError::new("const fn cannot return void"));
            }
            Stmt::Break(None) => return Ok(ConstLoopCtl::Break),
            Stmt::Break(Some(_)) => {
                return Err(TypeError::new(
                    "const break labels are not supported yet (use bare `break`)",
                ));
            }
            Stmt::Continue(None) => return Ok(ConstLoopCtl::Continue),
            Stmt::Continue(Some(_)) => {
                return Err(TypeError::new(
                    "const continue labels are not supported yet (use bare `continue`)",
                ));
            }
            Stmt::Let { name, init, .. } => {
                let v = fold_const_expr_with(init, env, const_strs, const_fns)?;
                env.insert(name.clone(), v);
            }
            Stmt::Assign { name, value } => {
                if !env.contains_key(name) {
                    return Err(TypeError::new(format!(
                        "const assign to unknown `{name}` (declare with let first)"
                    )));
                }
                let v = fold_const_expr_with(value, env, const_strs, const_fns)?;
                env.insert(name.clone(), v);
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                if let Some(init_stmt) = init {
                    if let Stmt::Let { name, init, .. } = init_stmt.as_ref() {
                        let v = fold_const_expr_with(init, env, const_strs, const_fns)?;
                        env.insert(name.clone(), v);
                    }
                }
                let c = fold_const_expr_with(cond, env, const_strs, const_fns)?;
                let branch = if c != 0 {
                    Some(then_block)
                } else {
                    else_block.as_ref()
                };
                if let Some(b) = branch {
                    match eval_const_loop_body(b, env, const_strs, const_fns)? {
                        ConstLoopCtl::Fallthrough => {}
                        other => return Ok(other),
                    }
                }
            }
            Stmt::Expr(e) => {
                let _ = fold_const_expr_with(e, env, const_strs, const_fns)?;
            }
            Stmt::While {
                cond, body: wbody, ..
            } => {
                if let Some(v) = run_const_while(cond, wbody, env, const_strs, const_fns)? {
                    return Ok(ConstLoopCtl::Return(v));
                }
            }
            Stmt::For {
                binders,
                iter,
                body: fbody,
                ..
            } => {
                if let Some(v) =
                    run_const_for_count(binders, iter, fbody, env, const_strs, const_fns)?
                {
                    return Ok(ConstLoopCtl::Return(v));
                }
            }
            Stmt::CFor {
                init,
                cond,
                post,
                body: fbody,
                ..
            } => {
                if let Some(v) =
                    run_const_cfor(init, cond, post, fbody, env, const_strs, const_fns)?
                {
                    return Ok(ConstLoopCtl::Return(v));
                }
            }
            _ => {
                return Err(TypeError::new(
                    "const loop body may only use let/assign/return/break/continue/if/while/for",
                ));
            }
        }
    }
    Ok(ConstLoopCtl::Fallthrough)
}

/// Substitute concrete types for type parameters in a type expression.
pub fn subst_type_expr(t: &TypeExpr, subst: &HashMap<String, Type>) -> TypeExpr {
    match t {
        TypeExpr::Named(n) => {
            if let Some(ty) = subst.get(n) {
                type_to_type_expr(ty)
            } else {
                TypeExpr::Named(n.clone())
            }
        }
        TypeExpr::Array(inner) => TypeExpr::Array(Box::new(subst_type_expr(inner, subst))),
        TypeExpr::Map(k, v) => TypeExpr::Map(
            Box::new(subst_type_expr(k, subst)),
            Box::new(subst_type_expr(v, subst)),
        ),
        TypeExpr::Generic(n, args) => TypeExpr::Generic(
            n.clone(),
            args.iter().map(|a| subst_type_expr(a, subst)).collect(),
        ),
        TypeExpr::Fn(params, ret) => TypeExpr::Fn(
            params.iter().map(|p| subst_type_expr(p, subst)).collect(),
            Box::new(subst_type_expr(ret, subst)),
        ),
        TypeExpr::Tuple(elems) => {
            TypeExpr::Tuple(elems.iter().map(|e| subst_type_expr(e, subst)).collect())
        }
    }
}

pub fn type_to_type_expr(t: &Type) -> TypeExpr {
    match t {
        Type::Int => TypeExpr::Named("int".into()),
        Type::Int64 => TypeExpr::Named("int64".into()),
        Type::Int32 => TypeExpr::Named("int32".into()),
        Type::Int8 => TypeExpr::Named("int8".into()),
        Type::UInt64 => TypeExpr::Named("uint64".into()),
        Type::Byte => TypeExpr::Named("byte".into()),
        Type::Float => TypeExpr::Named("float".into()),
        Type::Bool => TypeExpr::Named("bool".into()),
        Type::String => TypeExpr::Named("string".into()),
        Type::Void => TypeExpr::Named("void".into()),
        Type::Array(inner) => TypeExpr::Array(Box::new(type_to_type_expr(inner))),
        Type::Chan(inner) => TypeExpr::Generic("chan".into(), vec![type_to_type_expr(inner)]),
        Type::Named(n) => TypeExpr::Named(n.clone()),
        Type::Tuple(elems) => TypeExpr::Tuple(elems.iter().map(type_to_type_expr).collect()),
        Type::Option(inner) => TypeExpr::Generic("Option".into(), vec![type_to_type_expr(inner)]),
        Type::Result(a, b) => TypeExpr::Generic(
            "Result".into(),
            vec![type_to_type_expr(a), type_to_type_expr(b)],
        ),
        Type::Job(inner) => TypeExpr::Generic("Job".into(), vec![type_to_type_expr(inner)]),
        Type::Map(k, v) => TypeExpr::Map(
            Box::new(type_to_type_expr(k)),
            Box::new(type_to_type_expr(v)),
        ),
        other => TypeExpr::Named(other.display()),
    }
}

/// Produce a concrete monomorphization of a generic function.
pub fn specialize_fn(template: &FnDef, mono_name: &str, subst: &HashMap<String, Type>) -> FnDef {
    FnDef {
        name: mono_name.to_string(),
        type_params: Vec::new(),
        type_bounds: HashMap::new(),
        params: template
            .params
            .iter()
            .map(|p| Param {
                name: p.name.clone(),
                ty: subst_type_expr(&p.ty, subst),
                mutable: p.mutable,
            })
            .collect(),
        ret: template.ret.as_ref().map(|t| subst_type_expr(t, subst)),
        body: subst_block(&template.body, subst),
        exported: template.exported,
        is_const: template.is_const,
        stability: template.stability.clone(),
    }
}

fn subst_block(b: &Block, subst: &HashMap<String, Type>) -> Block {
    Block {
        stmts: b.stmts.iter().map(|s| subst_stmt(s, subst)).collect(),
    }
}

fn subst_stmt(s: &Stmt, subst: &HashMap<String, Type>) -> Stmt {
    match s {
        Stmt::Let {
            name,
            mutable,
            ownership,
            ty,
            init,
        } => Stmt::Let {
            name: name.clone(),
            mutable: *mutable,
            ownership: *ownership,
            ty: ty.as_ref().map(|t| subst_type_expr(t, subst)),
            init: subst_expr(init, subst),
        },
        Stmt::LetMulti {
            names,
            mutable,
            init,
        } => Stmt::LetMulti {
            names: names.clone(),
            mutable: *mutable,
            init: subst_expr(init, subst),
        },
        Stmt::Return(e) => Stmt::Return(e.as_ref().map(|e| subst_expr(e, subst))),
        Stmt::Expr(e) => Stmt::Expr(subst_expr(e, subst)),
        Stmt::If {
            init,
            cond,
            then_block,
            else_block,
        } => Stmt::If {
            init: init.as_ref().map(|s| Box::new(subst_stmt(s, subst))),
            cond: subst_expr(cond, subst),
            then_block: subst_block(then_block, subst),
            else_block: else_block.as_ref().map(|b| subst_block(b, subst)),
        },
        Stmt::While { label, cond, body } => Stmt::While {
            label: label.clone(),
            cond: subst_expr(cond, subst),
            body: subst_block(body, subst),
        },
        Stmt::For {
            label,
            binders,
            is_range,
            iter,
            body,
        } => Stmt::For {
            label: label.clone(),
            binders: binders.clone(),
            is_range: *is_range,
            iter: subst_expr(iter, subst),
            body: subst_block(body, subst),
        },
        Stmt::Assign { name, value } => Stmt::Assign {
            name: name.clone(),
            value: subst_expr(value, subst),
        },
        Stmt::Defer { body } => Stmt::Defer {
            body: subst_block(body, subst),
        },
        Stmt::Crew { name, body } => Stmt::Crew {
            name: name.clone(),
            body: subst_block(body, subst),
        },
        Stmt::Arena { name, body } => Stmt::Arena {
            name: name.clone(),
            body: subst_block(body, subst),
        },
        Stmt::Unsafe { body } => Stmt::Unsafe {
            body: subst_block(body, subst),
        },
        other => other.clone(),
    }
}

fn subst_expr(e: &Expr, subst: &HashMap<String, Type>) -> Expr {
    match e {
        Expr::Call { callee, args } => Expr::Call {
            callee: Box::new(subst_expr(callee, subst)),
            args: args.iter().map(|a| subst_expr(a, subst)).collect(),
        },
        Expr::Binary { op, left, right } => Expr::Binary {
            op: *op,
            left: Box::new(subst_expr(left, subst)),
            right: Box::new(subst_expr(right, subst)),
        },
        Expr::Unary { op, expr } => Expr::Unary {
            op: *op,
            expr: Box::new(subst_expr(expr, subst)),
        },
        Expr::Tuple(elems) => Expr::Tuple(elems.iter().map(|x| subst_expr(x, subst)).collect()),
        Expr::Array(elems) => Expr::Array(elems.iter().map(|x| subst_expr(x, subst)).collect()),
        Expr::Match { scrutinee, arms } => Expr::Match {
            scrutinee: Box::new(subst_expr(scrutinee, subst)),
            arms: arms
                .iter()
                .map(|a| crate::ast::MatchArm {
                    pattern: a.pattern.clone(),
                    guard: a.guard.as_ref().map(|g| subst_expr(g, subst)),
                    body: subst_expr(&a.body, subst),
                })
                .collect(),
        },
        Expr::Block(b) => Expr::Block(subst_block(b, subst)),
        Expr::Lambda { params, body } => Expr::Lambda {
            params: params.clone(),
            body: Box::new(subst_expr(body, subst)),
        },
        Expr::Try(inner) => Expr::Try(Box::new(subst_expr(inner, subst))),
        Expr::Field { base, field } => Expr::Field {
            base: Box::new(subst_expr(base, subst)),
            field: field.clone(),
        },
        Expr::Index { base, index } => Expr::Index {
            base: Box::new(subst_expr(base, subst)),
            index: Box::new(subst_expr(index, subst)),
        },
        Expr::Method {
            receiver,
            method,
            args,
        } => Expr::Method {
            receiver: Box::new(subst_expr(receiver, subst)),
            method: method.clone(),
            args: args.iter().map(|a| subst_expr(a, subst)).collect(),
        },
        Expr::ChanOpen { elem, cap } => Expr::ChanOpen {
            elem: subst_type_expr(elem, subst),
            cap: Box::new(subst_expr(cap, subst)),
        },
        Expr::Make { ty, len, cap } => Expr::Make {
            ty: subst_type_expr(ty, subst),
            len: len.as_ref().map(|e| Box::new(subst_expr(e, subst))),
            cap: cap.as_ref().map(|e| Box::new(subst_expr(e, subst))),
        },
        Expr::StructLit {
            name,
            fields,
            update,
        } => {
            // Substitute type param tags in monomorphized struct names.
            // e.g. "Pair__T" with {T: int} → "Pair__int"
            let mono_name = subst_mono_name(name, subst);
            Expr::StructLit {
                name: mono_name,
                fields: fields
                    .iter()
                    .map(|(n, e)| (n.clone(), subst_expr(e, subst)))
                    .collect(),
                update: update.as_ref().map(|e| Box::new(subst_expr(e, subst))),
            }
        }
        Expr::StructLitPos { name, values } => {
            let mono_name = subst_mono_name(name, subst);
            Expr::StructLitPos {
                name: mono_name,
                values: values.iter().map(|e| subst_expr(e, subst)).collect(),
            }
        }
        other => other.clone(),
    }
}

/// Replace type param tags in a monomorphized name. e.g. "Pair__T" with {T→int} → "Pair__int".
fn subst_mono_name(name: &str, subst: &HashMap<String, Type>) -> String {
    if !name.contains("__") {
        return name.to_string();
    }
    let mut result = name.to_string();
    for (param, ty) in subst {
        result = result.replace(&format!("__{param}"), &format!("__{}", ty.mono_tag()));
        // Also handle the param appearing as the entire tag after the last __
        if result.ends_with(&format!("__{param}")) {
            let prefix_len = result.len() - param.len() - 2;
            result = format!("{}__{}", &result[..prefix_len], ty.mono_tag());
        }
    }
    result
}
