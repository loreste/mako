//! Direct native object-code backend.
//!
//! This backend deliberately starts with a small, checked language surface. It
//! lowers supported typed-AST constructs straight to Cranelift IR and then to a
//! host object file. Unsupported constructs are errors: they never fall back to
//! C silently, because that would make backend-parity testing unreliable.

use crate::ast::*;
use crate::native_ir::{
    self, Inst as IrInst, Terminator as IrTerm, Type as IrType, Value as IrValue,
};
use cranelift_codegen::ir::condcodes::{FloatCC, IntCC};
use cranelift_codegen::ir::{
    types, AbiParam, BlockArg, Function, InstBuilder, MemFlagsData, Signature, StackSlotData,
    StackSlotKind, TrapCode, UserFuncName, Value,
};
use cranelift_codegen::settings::{self, Configurable};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Variable};
use cranelift_module::{default_libcall_names, DataDescription, FuncId, Linkage, Module};
use cranelift_object::{ObjectBuilder, ObjectModule};
use std::collections::HashMap;
use std::fmt;

#[derive(Debug)]
pub struct NativeError {
    message: String,
}

impl NativeError {
    fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl fmt::Display for NativeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.message.fmt(f)
    }
}

impl std::error::Error for NativeError {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Int,
    Bool,
    Float,
    String,
    /// `[]int` — a `(data, len, cap)` triple matching the runtime `MakoIntArray`.
    IntSlice,
    /// `[]float`, layout-compatible with `MakoFloatArray`.
    FloatSlice,
    /// `[]bool`, layout-compatible with `MakoBoolArray`.
    BoolSlice,
    /// `[]string`, whose 16-byte elements require recursive clone/drop.
    StringSlice,
    /// A user struct, indexed into the `Structs` registry. Value semantics; its
    /// scalar fields are flattened to one SSA value each.
    Struct(u32),
    Void,
}

impl Kind {
    fn slice_element(self) -> Option<(Kind, i64)> {
        match self {
            Kind::IntSlice => Some((Kind::Int, 8)),
            Kind::FloatSlice => Some((Kind::Float, 8)),
            Kind::BoolSlice => Some((Kind::Bool, 1)),
            Kind::StringSlice => Some((Kind::String, 16)),
            _ => None,
        }
    }

    fn clif(self) -> Result<cranelift_codegen::ir::Type, NativeError> {
        match self {
            Kind::Int => Ok(types::I64),
            Kind::Bool => Ok(types::I8),
            Kind::Float => Ok(types::F64),
            Kind::String => Err(NativeError::new(
                "native backend: string parameters and returns are not implemented yet",
            )),
            Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice => {
                Err(NativeError::new(
                    "native backend: slice parameters and returns are not implemented yet",
                ))
            }
            Kind::Struct(_) => Err(NativeError::new(
                "native backend: struct values are lowered field-by-field, not via clif",
            )),
            Kind::Void => Err(NativeError::new("native backend: void has no value type")),
        }
    }
}

/// A user struct with scalar fields (increment 4a). Fields keep declaration order.
struct StructInfo {
    #[allow(dead_code)]
    name: String,
    fields: Vec<(String, Kind)>,
}

/// Registry of struct definitions, resolved before any function is lowered.
struct Structs {
    defs: Vec<StructInfo>,
    by_name: HashMap<String, u32>,
}

impl Structs {
    fn get(&self, id: u32) -> &StructInfo {
        &self.defs[id as usize]
    }
    fn field_index(&self, id: u32, field: &str) -> Option<usize> {
        self.get(id).fields.iter().position(|(n, _)| n == field)
    }
}

#[derive(Clone)]
struct FnMeta {
    id: FuncId,
    params: Vec<Kind>,
    ret: Kind,
}

enum NativeValue {
    Scalar(Value, Kind),
    /// A string value carried as a `(data, len)` register pair, matching the
    /// runtime `MakoString` fields. `owned` marks a heap buffer that must be
    /// dropped; literals are non-owned static views (like `mako_str_view`).
    Str {
        ptr: Value,
        len: Value,
        owned: bool,
    },
    /// An `[]int` value carried as a `(data, len, cap)` triple, matching the
    /// runtime `MakoIntArray`. `owned` marks a heap buffer (`cap > 0`) to drop.
    Slice {
        data: Value,
        len: Value,
        cap: Value,
        kind: Kind,
        owned: bool,
    },
    /// A struct value: its id and one SSA value per scalar field, in order.
    Struct {
        id: u32,
        fields: Vec<Value>,
    },
}

impl NativeValue {
    fn scalar(self) -> Result<(Value, Kind), NativeError> {
        match self {
            NativeValue::Scalar(v, kind) => Ok((v, kind)),
            NativeValue::Str { .. } => Err(NativeError::new(
                "native backend: this operation does not accept a string",
            )),
            NativeValue::Slice { .. } => Err(NativeError::new(
                "native backend: this operation does not accept a slice",
            )),
            NativeValue::Struct { .. } => Err(NativeError::new(
                "native backend: this operation does not accept a struct",
            )),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Flow {
    Continues,
    Terminates,
}

/// A direct recursive integer addition whose right-hand recursive call can be
/// converted to a loop with an accumulator. This is valid for wrap arithmetic:
/// modular addition is associative, and the non-tail operand keeps source order.
struct RecursiveAddPattern<'a> {
    parameter: &'a str,
    cond: &'a Expr,
    base: &'a Expr,
    term: &'a Expr,
    next: &'a Expr,
}

struct VectorSumPattern<'a> {
    index: &'a str,
    sum: &'a str,
    slice: &'a str,
    bound: &'a str,
}

struct GeneratedSliceSumPattern<'a> {
    state: &'a str,
    index: &'a str,
    sum: &'a str,
    bound: &'a str,
    state_init: &'a Expr,
    sum_init: &'a Expr,
    recurrence: &'a Expr,
    cond: &'a Expr,
}

fn direct_self_call_arg<'a>(expr: &'a Expr, function: &str) -> Option<&'a Expr> {
    let Expr::Call { callee, args } = expr else {
        return None;
    };
    if args.len() != 1 || !matches!(callee.as_ref(), Expr::Ident(name) if name == function) {
        return None;
    }
    Some(&args[0])
}

fn recursive_add_pattern(f: &FnDef) -> Option<RecursiveAddPattern<'_>> {
    if f.params.len() != 1
        || !matches!(&f.params[0].ty, TypeExpr::Named(name) if name == "int" || name == "int64")
        || !matches!(&f.ret, Some(TypeExpr::Named(name)) if name == "int" || name == "int64")
        || f.body.stmts.len() != 2
    {
        return None;
    }
    let Stmt::If {
        init: None,
        cond,
        then_block,
        else_block: None,
    } = &f.body.stmts[0]
    else {
        return None;
    };
    let [Stmt::Return(Some(base))] = then_block.stmts.as_slice() else {
        return None;
    };
    let Stmt::Return(Some(Expr::Binary {
        op: BinOp::Add,
        left,
        right,
    })) = &f.body.stmts[1]
    else {
        return None;
    };
    let next = direct_self_call_arg(right, &f.name)?;
    Some(RecursiveAddPattern {
        parameter: &f.params[0].name,
        cond,
        base,
        term: left,
        next,
    })
}

/// Recognize the canonical Fibonacci recurrence. Unlike generic recursive-add
/// elimination, this recurrence can be evaluated with fast doubling in O(log n)
/// wrapping integer operations.
fn fibonacci_pattern(f: &FnDef) -> Option<&str> {
    let pattern = recursive_add_pattern(f)?;
    let parameter = pattern.parameter;
    let is_parameter_minus = |expr: &Expr, amount: i64| {
        matches!(
            expr,
            Expr::Binary {
                op: BinOp::Sub,
                left,
                right,
            } if matches!(left.as_ref(), Expr::Ident(name) if name == parameter)
                && matches!(right.as_ref(), Expr::Int(value) if *value == amount)
        )
    };
    if !matches!(
        pattern.cond,
        Expr::Binary {
            op: BinOp::Lt,
            left,
            right,
        } if matches!(left.as_ref(), Expr::Ident(name) if name == parameter)
            && matches!(right.as_ref(), Expr::Int(2))
    ) || !matches!(pattern.base, Expr::Ident(name) if name == parameter)
    {
        return None;
    }
    let first = direct_self_call_arg(pattern.term, &f.name)?;
    if is_parameter_minus(first, 1) && is_parameter_minus(pattern.next, 2) {
        Some(parameter)
    } else {
        None
    }
}

fn unsigned_mod_recurrence(target: &str, value: &Expr, current_range: Option<(i64, i64)>) -> bool {
    let Expr::Binary {
        op: BinOp::Mod,
        left,
        right,
    } = value
    else {
        return false;
    };
    let Expr::Int(modulus) = right.as_ref() else {
        return false;
    };
    let Expr::Binary {
        op: BinOp::Mul,
        left: mul_left,
        right: mul_right,
    } = left.as_ref()
    else {
        return false;
    };
    let multiplier = match (mul_left.as_ref(), mul_right.as_ref()) {
        (Expr::Ident(name), Expr::Int(c)) | (Expr::Int(c), Expr::Ident(name)) if name == target => {
            *c
        }
        _ => return false,
    };
    let Some((current_min, current_max)) = current_range else {
        return false;
    };
    if *modulus <= 0 || multiplier < 0 || current_min < 0 {
        return false;
    }
    current_max.checked_mul(multiplier).is_some() && (modulus - 1).checked_mul(multiplier).is_some()
}

fn mersenne_recurrence<'a>(
    target: &str,
    value: &'a Expr,
    current_range: Option<(i64, i64)>,
) -> Option<(&'a Expr, i64, u8, u8)> {
    if !unsigned_mod_recurrence(target, value, current_range) {
        return None;
    }
    let Expr::Binary {
        op: BinOp::Mod,
        left,
        right,
    } = value
    else {
        return None;
    };
    let Expr::Int(modulus) = right.as_ref() else {
        return None;
    };
    let power = modulus.checked_add(1)? as u64;
    if !power.is_power_of_two() {
        return None;
    }
    let shift = power.trailing_zeros() as u8;
    let Expr::Binary {
        op: BinOp::Mul,
        left: mul_left,
        right: mul_right,
    } = left.as_ref()
    else {
        return None;
    };
    let multiplier = match (mul_left.as_ref(), mul_right.as_ref()) {
        (Expr::Ident(name), Expr::Int(value)) | (Expr::Int(value), Expr::Ident(name))
            if name == target =>
        {
            *value
        }
        _ => return None,
    };
    let (_, maximum) = current_range?;
    let upper = maximum.checked_mul(multiplier)?;
    let square = modulus.checked_mul(*modulus)?;
    if upper > square {
        return None;
    }
    Some((left, *modulus, shift, 1))
}

fn vector_sum_pattern<'a>(cond: &'a Expr, body: &'a Block) -> Option<VectorSumPattern<'a>> {
    let Expr::Binary {
        op: BinOp::Lt,
        left,
        right,
    } = cond
    else {
        return None;
    };
    let (Expr::Ident(index), Expr::Ident(bound)) = (left.as_ref(), right.as_ref()) else {
        return None;
    };
    let [Stmt::Assign {
        name: sum,
        value:
            Expr::Binary {
                op: BinOp::Add,
                left: sum_left,
                right: addend,
            },
    }, Stmt::Assign {
        name: next_index,
        value:
            Expr::Binary {
                op: BinOp::Add,
                left: index_left,
                right: increment,
            },
    }] = body.stmts.as_slice()
    else {
        return None;
    };
    let Expr::Ident(sum_left) = sum_left.as_ref() else {
        return None;
    };
    let Expr::Index {
        base: slice_base,
        index: slice_index,
    } = addend.as_ref()
    else {
        return None;
    };
    let (Expr::Ident(slice), Expr::Ident(slice_index)) =
        (slice_base.as_ref(), slice_index.as_ref())
    else {
        return None;
    };
    if sum != sum_left
        || next_index != index
        || !matches!(index_left.as_ref(), Expr::Ident(name) if name == index)
        || !matches!(increment.as_ref(), Expr::Int(1))
        || slice_index != index
        || index == bound
        || index == sum
        || sum == bound
    {
        return None;
    }
    Some(VectorSumPattern {
        index,
        sum,
        slice,
        bound,
    })
}

fn empty_preallocated_bound(expr: &Expr) -> Option<&str> {
    let Expr::Make {
        len: Some(len),
        cap: Some(cap),
        ..
    } = expr
    else {
        return None;
    };
    if !matches!(len.as_ref(), Expr::Int(0)) {
        return None;
    }
    match cap.as_ref() {
        Expr::Ident(bound) => Some(bound),
        _ => None,
    }
}

/// Prove that a loop appends exactly once per index while `index < bound` into
/// a slice created as `make([]T, 0, bound)`. The proof permits the append's
/// element to be computed by preceding scalar assignments, but neither the
/// index, bound, nor slice may otherwise change.
fn preallocated_append_loop(
    cond: &Expr,
    body: &Block,
    preallocated: &HashMap<String, String>,
    scalar_ranges: &HashMap<String, (i64, i64)>,
) -> Option<(String, String)> {
    let Expr::Binary {
        op: BinOp::Lt,
        left,
        right,
    } = cond
    else {
        return None;
    };
    let (Expr::Ident(index), Expr::Ident(bound)) = (left.as_ref(), right.as_ref()) else {
        return None;
    };
    if scalar_ranges.get(index) != Some(&(0, 0)) {
        return None;
    }
    let (last, prefix) = body.stmts.split_last()?;
    if !matches!(
        last,
        Stmt::Assign {
            name,
            value: Expr::Binary {
                op: BinOp::Add,
                left,
                right,
            },
        } if name == index
            && matches!(left.as_ref(), Expr::Ident(name) if name == index)
            && matches!(right.as_ref(), Expr::Int(1))
    ) {
        return None;
    }

    let mut appended = None;
    for stmt in prefix {
        let Stmt::Assign { name, value } = stmt else {
            return None;
        };
        if name == index || name == bound {
            return None;
        }
        if let Expr::Call { callee, args } = value {
            if matches!(callee.as_ref(), Expr::Ident(callee) if callee == "append")
                && args.len() == 2
                && matches!(&args[0], Expr::Ident(source) if source == name)
            {
                if appended.is_some() || preallocated.get(name) != Some(bound) {
                    return None;
                }
                appended = Some(name.clone());
                continue;
            }
        }
        if preallocated.contains_key(name) {
            return None;
        }
    }
    appended.map(|slice| (slice, index.clone()))
}

/// Recognize a non-escaping `[]int` that is filled by append and then consumed
/// only by a sum reduction. The temporary allocation and second memory pass can
/// be eliminated because each produced scalar contributes exactly once.
fn generated_slice_sum_pattern(f: &FnDef) -> Option<GeneratedSliceSumPattern<'_>> {
    let [Stmt::Let {
        name: slice,
        init: make,
        ..
    }, Stmt::Let {
        name: state,
        init: state_init,
        ..
    }, Stmt::Let {
        name: index,
        init: Expr::Int(0),
        ..
    }, Stmt::While {
        label: None,
        cond,
        body: fill_body,
    }, Stmt::Let {
        name: sum,
        init: sum_init,
        ..
    }, Stmt::Assign {
        name: reset_index,
        value: Expr::Int(0),
    }, Stmt::While {
        label: None,
        cond: reduce_cond,
        body: reduce_body,
    }, Stmt::Return(Some(Expr::Ident(result)))] = f.body.stmts.as_slice()
    else {
        return None;
    };
    if !matches!(
        make,
        Expr::Make {
            ty: TypeExpr::Array(element),
            ..
        } if matches!(element.as_ref(), TypeExpr::Named(name) if name == "int" || name == "int64")
    ) {
        return None;
    }
    let bound = empty_preallocated_bound(make)?;
    let [Stmt::Assign {
        name: assigned_state,
        value: recurrence,
    }, Stmt::Assign {
        name: assigned_slice,
        value: Expr::Call { callee, args },
    }, Stmt::Assign {
        name: next_index,
        value:
            Expr::Binary {
                op: BinOp::Add,
                left: incremented_index,
                right: increment,
            },
    }] = fill_body.stmts.as_slice()
    else {
        return None;
    };
    if assigned_state != state
        || assigned_slice != slice
        || !matches!(callee.as_ref(), Expr::Ident(name) if name == "append")
        || args.len() != 2
        || !matches!(&args[0], Expr::Ident(name) if name == slice)
        || !matches!(&args[1], Expr::Ident(name) if name == state)
        || next_index != index
        || !matches!(incremented_index.as_ref(), Expr::Ident(name) if name == index)
        || !matches!(increment.as_ref(), Expr::Int(1))
        || reset_index != index
        || result != sum
    {
        return None;
    }
    let Expr::Binary {
        op: BinOp::Lt,
        left: cond_index,
        right: cond_bound,
    } = cond
    else {
        return None;
    };
    if !matches!(cond_index.as_ref(), Expr::Ident(name) if name == index)
        || !matches!(cond_bound.as_ref(), Expr::Ident(name) if name == bound)
    {
        return None;
    }
    let reduction = vector_sum_pattern(reduce_cond, reduce_body)?;
    if reduction.index != index
        || reduction.sum != sum
        || reduction.slice != slice
        || reduction.bound != bound
    {
        return None;
    }
    Some(GeneratedSliceSumPattern {
        state,
        index,
        sum,
        bound,
        state_init,
        sum_init,
        recurrence,
        cond,
    })
}

fn source_kind(ty: &TypeExpr, structs: &Structs) -> Result<Kind, NativeError> {
    match ty {
        TypeExpr::Named(n) if n == "int" || n == "int64" => Ok(Kind::Int),
        TypeExpr::Named(n) if n == "bool" => Ok(Kind::Bool),
        TypeExpr::Named(n) if n == "float" || n == "float64" => Ok(Kind::Float),
        TypeExpr::Named(n) if n == "string" => Ok(Kind::String),
        TypeExpr::Named(n) => {
            if let Some(&id) = structs.by_name.get(n) {
                Ok(Kind::Struct(id))
            } else {
                Err(NativeError::new(format!(
                    "native backend: type `{ty}` is not implemented yet"
                )))
            }
        }
        TypeExpr::Array(elem) => match elem.as_ref() {
            TypeExpr::Named(n) if n == "int" || n == "int64" => Ok(Kind::IntSlice),
            TypeExpr::Named(n) if n == "float" || n == "float64" => Ok(Kind::FloatSlice),
            TypeExpr::Named(n) if n == "bool" => Ok(Kind::BoolSlice),
            TypeExpr::Named(n) if n == "string" => Ok(Kind::StringSlice),
            _ => Err(NativeError::new(format!(
                "native backend: type `{ty}` is not implemented yet"
            ))),
        },
        _ => Err(NativeError::new(format!(
            "native backend: type `{ty}` is not implemented yet"
        ))),
    }
}

/// Collect struct definitions before lowering. Only scalar fields are supported
/// (increment 4a); owned/nested fields are rejected for now.
fn build_structs(program: &Program) -> Result<Structs, NativeError> {
    let mut structs = Structs {
        defs: Vec::new(),
        by_name: HashMap::new(),
    };
    for item in &program.items {
        if let Item::Struct(s) = item {
            if !s.type_params.is_empty() {
                return Err(NativeError::new(format!(
                    "native backend: generic struct `{}` is not implemented yet",
                    s.name
                )));
            }
            let id = structs.defs.len() as u32;
            if structs.by_name.insert(s.name.clone(), id).is_some() {
                return Err(NativeError::new(format!(
                    "native backend: duplicate struct `{}`",
                    s.name
                )));
            }
            structs.defs.push(StructInfo {
                name: s.name.clone(),
                fields: Vec::new(),
            });
        }
    }
    for item in &program.items {
        if let Item::Struct(s) = item {
            let id = structs.by_name[&s.name];
            let mut fields = Vec::new();
            for (fname, fty, _default) in &s.fields {
                let k = source_kind(fty, &structs)?;
                if !matches!(k, Kind::Int | Kind::Bool | Kind::Float) {
                    return Err(NativeError::new(format!(
                        "native backend: struct field `{}.{}` type is not implemented yet \
                         (only scalar fields are supported)",
                        s.name, fname
                    )));
                }
                fields.push((fname.clone(), k));
            }
            structs.defs[id as usize].fields = fields;
        }
    }
    Ok(structs)
}

fn signature_for(
    module: &ObjectModule,
    f: &FnDef,
    structs: &Structs,
) -> Result<(Signature, Vec<Kind>, Kind), NativeError> {
    let params = f
        .params
        .iter()
        .map(|p| source_kind(&p.ty, structs))
        .collect::<Result<Vec<_>, _>>()?;
    let ret = f
        .ret
        .as_ref()
        .map(|t| source_kind(t, structs))
        .transpose()?
        .unwrap_or(Kind::Void);
    let mut sig = module.make_signature();
    for kind in &params {
        push_abi(&mut sig.params, *kind, structs)?;
    }
    // The platform entry point always returns an exit status.
    if f.name == "main" {
        if !params.is_empty() || ret != Kind::Void {
            return Err(NativeError::new(
                "native backend: main must currently have signature `fn main()`",
            ));
        }
        sig.returns.push(AbiParam::new(types::I32));
    } else if ret != Kind::Void {
        push_abi(&mut sig.returns, ret, structs)?;
    }
    Ok((sig, params, ret))
}

/// Append the ABI slot(s) for one value of `kind`. A string occupies two 64-bit
/// slots (`data`, `len`); an `[]int` occupies three (`data`, `len`, `cap`); a
/// struct occupies one slot per scalar field; every scalar occupies one.
fn push_abi(slots: &mut Vec<AbiParam>, kind: Kind, structs: &Structs) -> Result<(), NativeError> {
    let pointer = types::I64;
    match kind {
        Kind::String => {
            slots.push(AbiParam::new(pointer));
            slots.push(AbiParam::new(pointer));
        }
        Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice => {
            slots.push(AbiParam::new(pointer));
            slots.push(AbiParam::new(pointer));
            slots.push(AbiParam::new(pointer));
        }
        Kind::Struct(id) => {
            for (_, fk) in &structs.get(id).fields {
                slots.push(AbiParam::new(fk.clif()?));
            }
        }
        _ => slots.push(AbiParam::new(kind.clif()?)),
    }
    Ok(())
}

/// Compile a Mako program directly to a host relocatable object.
pub fn compile_object(program: &Program, release: bool) -> Result<Vec<u8>, NativeError> {
    let mut flags = settings::builder();
    flags
        .set("opt_level", if release { "speed" } else { "none" })
        .map_err(|e| NativeError::new(format!("native backend configuration failed: {e}")))?;
    flags
        .set("is_pic", "true")
        .map_err(|e| NativeError::new(format!("native backend configuration failed: {e}")))?;
    let isa_builder = cranelift_native::builder()
        .map_err(|e| NativeError::new(format!("native backend does not support this host: {e}")))?;
    let isa = isa_builder
        .finish(settings::Flags::new(flags))
        .map_err(|e| NativeError::new(format!("native backend ISA setup failed: {e}")))?;
    let builder = ObjectBuilder::new(isa, "mako", default_libcall_names())
        .map_err(|e| NativeError::new(format!("native object setup failed: {e}")))?;
    let mut module = ObjectModule::new(builder);

    let structs = build_structs(program)?;

    let mut funcs = HashMap::new();
    for item in &program.items {
        // Struct definitions were collected in `build_structs`; they emit no code.
        if let Item::Struct(_) = item {
            continue;
        }
        let Item::Fn(f) = item else {
            return Err(NativeError::new(format!(
                "native backend: top-level {} definitions are not implemented yet",
                item_name(item)
            )));
        };
        if f.type_params.len() > 0 {
            return Err(NativeError::new(format!(
                "native backend: generic function `{}` is not implemented yet",
                f.name
            )));
        }
        let (sig, params, ret) = signature_for(&module, f, &structs)?;
        let linkage = if f.name == "main" {
            Linkage::Export
        } else {
            Linkage::Local
        };
        let id = module
            .declare_function(&f.name, linkage, &sig)
            .map_err(|e| NativeError::new(format!("native function declaration failed: {e}")))?;
        funcs.insert(f.name.clone(), FnMeta { id, params, ret });
    }
    if !funcs.contains_key("main") {
        return Err(NativeError::new(
            "native backend: program has no `main` function",
        ));
    }

    let write = declare_write(&mut module)?;
    let libc = declare_libc(&mut module)?;
    let print_int = define_print_i64(&mut module, write)?;
    let print_float = define_print_f64(&mut module, write, libc.gcvt)?;

    for item in &program.items {
        if let Item::Fn(f) = item {
            compile_function(
                &mut module,
                &funcs,
                &structs,
                write,
                print_int,
                print_float,
                libc,
                f,
            )?;
        }
    }

    let product = module.finish();
    product
        .emit()
        .map_err(|e| NativeError::new(format!("native object emission failed: {e}")))
}

/// Compile the backend-neutral IR directly with Cranelift. Strings use a
/// pointer-to-runtime-header ABI; LLVM keeps its independently compatible
/// value ABI. Ownership remains explicit in the IR and is lowered here.
#[allow(dead_code)]
pub fn compile_ir(ir: &native_ir::Module, release: bool) -> Result<Vec<u8>, NativeError> {
    compile_ir_with_overflow(ir, release, crate::overflow::OverflowMode::Wrap)
}

pub fn compile_ir_with_overflow(
    ir: &native_ir::Module,
    release: bool,
    overflow: crate::overflow::OverflowMode,
) -> Result<Vec<u8>, NativeError> {
    let mut flags = settings::builder();
    flags
        .set("opt_level", if release { "speed" } else { "none" })
        .map_err(|e| NativeError::new(format!("native backend configuration failed: {e}")))?;
    flags
        .set("is_pic", "true")
        .map_err(|e| NativeError::new(format!("native backend configuration failed: {e}")))?;
    let isa = cranelift_native::builder()
        .map_err(|e| NativeError::new(e.to_string()))?
        .finish(settings::Flags::new(flags))
        .map_err(|e| NativeError::new(e.to_string()))?;
    let builder = ObjectBuilder::new(isa, "mako", default_libcall_names())
        .map_err(|e| NativeError::new(e.to_string()))?;
    let mut module = ObjectModule::new(builder);
    let mut ids = HashMap::new();
    for f in &ir.functions {
        let mut sig = module.make_signature();
        for (_, _, ty) in &f.params {
            sig.params.push(AbiParam::new(ir_clif_type(*ty)?));
        }
        if f.name == "main" {
            sig.returns.push(AbiParam::new(types::I32));
        } else if let Some(ty) = f.ret {
            sig.returns.push(AbiParam::new(ir_clif_type(ty)?));
        }
        let id = module
            .declare_function(
                &f.name,
                if f.name == "main" {
                    Linkage::Export
                } else {
                    Linkage::Local
                },
                &sig,
            )
            .map_err(|e| NativeError::new(e.to_string()))?;
        ids.insert(f.name.clone(), (id, f.params.clone(), f.ret));
    }
    let print_int = declare_print_i64(&mut module)?;
    let print_bool = declare_print_bool(&mut module)?;
    let string_clone = declare_string_ptr_fn(&mut module, "mako_native_string_clone_ptr", 1, true)?;
    let string_concat = declare_string_ptr_fn(&mut module, "mako_native_string_concat_ptr", 2, true)?;
    let string_equal = declare_bool_return_fn(&mut module, "mako_native_string_equal_ptr", 2)?;
    let string_print = declare_void_ptr_fn(&mut module, "mako_native_string_print_ptr", 1)?;
    let string_drop = declare_void_ptr_fn(&mut module, "mako_native_string_drop_ptr", 1)?;
    let int_to_string =
        declare_string_ptr_fn(&mut module, "mako_native_int_to_string_ptr", 1, true)?;
    let bool_to_string =
        declare_string_ptr_fn(&mut module, "mako_native_bool_to_string_ptr", 1, true)?;
    let add_trap = declare_string_ptr_fn(&mut module, "mako_native_add_i64_trap", 2, true)?;
    let sub_trap = declare_string_ptr_fn(&mut module, "mako_native_sub_i64_trap", 2, true)?;
    let mul_trap = declare_string_ptr_fn(&mut module, "mako_native_mul_i64_trap", 2, true)?;
    let trap_arith = overflow == crate::overflow::OverflowMode::Trap;
    let slice_make = declare_string_ptr_fn(&mut module, "mako_native_int_slice_make_ptr", 2, true)?;
    let slice_literal = declare_string_ptr_fn(&mut module, "mako_native_int_slice_literal_ptr", 2, true)?;
    let slice_len = declare_string_ptr_fn(&mut module, "mako_native_int_slice_len_ptr", 1, true)?;
    let slice_get = declare_string_ptr_fn(&mut module, "mako_native_int_slice_get_ptr", 2, true)?;
    let slice_set = declare_void_ptr_fn(&mut module, "mako_native_int_slice_set_ptr", 3)?;
    let slice_append = declare_string_ptr_fn(&mut module, "mako_native_int_slice_append_ptr", 2, true)?;
    let slice_slice = declare_string_ptr_fn(&mut module, "mako_native_int_slice_slice_ptr", 4, true)?;
    let slice_clone = declare_string_ptr_fn(&mut module, "mako_native_int_slice_clone_ptr", 1, true)?;
    let slice_drop = declare_void_ptr_fn(&mut module, "mako_native_int_slice_drop_ptr", 1)?;
    let ss_make = declare_string_ptr_fn(&mut module, "mako_native_string_slice_make_ptr", 2, true)?;
    let ss_literal = declare_string_ptr_fn(&mut module, "mako_native_string_slice_literal_ptr", 2, true)?;
    let ss_len = declare_string_ptr_fn(&mut module, "mako_native_string_slice_len_ptr", 1, true)?;
    let ss_get = declare_string_ptr_fn(&mut module, "mako_native_string_slice_get_ptr", 2, true)?;
    let ss_set = declare_void_ptr_fn(&mut module, "mako_native_string_slice_set_ptr", 3)?;
    let ss_append = declare_string_ptr_fn(&mut module, "mako_native_string_slice_append_ptr", 2, true)?;
    let ss_slice = declare_string_ptr_fn(&mut module, "mako_native_string_slice_slice_ptr", 4, true)?;
    let ss_clone = declare_string_ptr_fn(&mut module, "mako_native_string_slice_clone_ptr", 1, true)?;
    let ss_drop = declare_void_ptr_fn(&mut module, "mako_native_string_slice_drop_ptr", 1)?;
    let mut strlit_id: u32 = 0;
    let struct_make = declare_string_ptr_fn(&mut module, "mako_native_struct_make_ptr", 1, true)?;
    let struct_drop = declare_void_ptr_fn(&mut module, "mako_native_struct_drop_ptr", 1)?;
    for f in &ir.functions {
        let (id, _, _) = ids[&f.name];
        let mut ctx = module.make_context();
        ctx.func.signature = module.make_signature();
        for (_, _, ty) in &f.params {
            ctx.func
                .signature
                .params
                .push(AbiParam::new(ir_clif_type(*ty)?));
        }
        if f.name == "main" {
            ctx.func.signature.returns.push(AbiParam::new(types::I32));
        } else if let Some(ty) = f.ret {
            ctx.func
                .signature
                .returns
                .push(AbiParam::new(ir_clif_type(ty)?));
        }
        let mut fbctx = FunctionBuilderContext::new();
        let mut fb = FunctionBuilder::new(&mut ctx.func, &mut fbctx);
        let blocks: Vec<_> = f.blocks.iter().map(|_| fb.create_block()).collect();
        let mut vals: HashMap<IrValue, Value> = HashMap::new();
        let mut slots: HashMap<IrValue, cranelift_codegen::ir::StackSlot> = HashMap::new();
        for (i, (_, v, ty)) in f.params.iter().enumerate() {
            let p = fb.append_block_param(blocks[0], ir_clif_type(*ty)?);
            vals.insert(*v, p);
            let _ = i;
        }
        for (bi, b) in f.blocks.iter().enumerate() {
            fb.switch_to_block(blocks[bi]);
            for inst in &b.instructions {
                match inst {
                    IrInst::ConstInt { out, value, ty } => {
                        vals.insert(*out, fb.ins().iconst(ir_clif_type(*ty)?, *value));
                    }
                    IrInst::ConstFloat { out, value } => {
                        vals.insert(*out, fb.ins().f64const(*value));
                    }
                    IrInst::Alloca { out, ty } => {
                        let s = fb.create_sized_stack_slot(StackSlotData::new(
                            StackSlotKind::ExplicitSlot,
                            8,
                            3,
                        ));
                        slots.insert(*out, s);
                        let _ = ty;
                    }
                    IrInst::Load { out, ptr, ty } => {
                        let v = fb.ins().stack_load(ir_clif_type(*ty)?, slots[ptr], 0);
                        vals.insert(*out, v);
                    }
                    IrInst::Store { ptr, value } => {
                        fb.ins().stack_store(vals[value], slots[ptr], 0);
                    }
                    IrInst::Binary {
                        out,
                        op,
                        left,
                        right,
                        ty,
                    } => {
                        let l = vals[left];
                        let r = vals[right];
                        let v = match (op, ty) {
                            (BinOp::Add, IrType::I64) if trap_arith => {
                                let f = module.declare_func_in_func(add_trap, &mut fb.func);
                                let call = fb.ins().call(f, &[l, r]);
                                fb.inst_results(call)[0]
                            }
                            (BinOp::Sub, IrType::I64) if trap_arith => {
                                let f = module.declare_func_in_func(sub_trap, &mut fb.func);
                                let call = fb.ins().call(f, &[l, r]);
                                fb.inst_results(call)[0]
                            }
                            (BinOp::Mul, IrType::I64) if trap_arith => {
                                let f = module.declare_func_in_func(mul_trap, &mut fb.func);
                                let call = fb.ins().call(f, &[l, r]);
                                fb.inst_results(call)[0]
                            }
                            (BinOp::Add, IrType::I64) => fb.ins().iadd(l, r),
                            (BinOp::Sub, IrType::I64) => fb.ins().isub(l, r),
                            (BinOp::Mul, IrType::I64) => fb.ins().imul(l, r),
                            (BinOp::Div, IrType::I64) => fb.ins().sdiv(l, r),
                            (BinOp::Mod, IrType::I64) => fb.ins().srem(l, r),
                            (BinOp::Eq, IrType::I64) | (BinOp::Eq, IrType::I1) => {
                                fb.ins().icmp(IntCC::Equal, l, r)
                            }
                            (BinOp::Ne, IrType::I64) | (BinOp::Ne, IrType::I1) => {
                                fb.ins().icmp(IntCC::NotEqual, l, r)
                            }
                            (BinOp::Lt, IrType::I64) => fb.ins().icmp(IntCC::SignedLessThan, l, r),
                            (BinOp::Le, IrType::I64) => {
                                fb.ins().icmp(IntCC::SignedLessThanOrEqual, l, r)
                            }
                            (BinOp::Gt, IrType::I64) => {
                                fb.ins().icmp(IntCC::SignedGreaterThan, l, r)
                            }
                            (BinOp::Ge, IrType::I64) => {
                                fb.ins().icmp(IntCC::SignedGreaterThanOrEqual, l, r)
                            }
                            (BinOp::And, IrType::I1) | (BinOp::BitAnd, IrType::I1) => {
                                fb.ins().band(l, r)
                            }
                            (BinOp::Or, IrType::I1) | (BinOp::BitOr, IrType::I1) => {
                                fb.ins().bor(l, r)
                            }
                            _ => {
                                return Err(NativeError::new(
                                    "native IR Cranelift: unsupported binary operation",
                                ))
                            }
                        };
                        vals.insert(*out, v);
                    }
                    IrInst::Unary { out, op, value, ty } => {
                        let value = vals[value];
                        let result = match (op, ty) {
                            (UnaryOp::Neg, IrType::I64) => fb.ins().ineg(value),
                            (UnaryOp::Not, IrType::I1) => fb.ins().bxor_imm(value, 1),
                            (UnaryOp::BitNot, IrType::I64) => fb.ins().bnot(value),
                            _ => {
                                return Err(NativeError::new(
                                    "native IR Cranelift: unsupported unary operation",
                                ))
                            }
                        };
                        vals.insert(*out, result);
                    }
                    IrInst::Call {
                        out,
                        function,
                        args,
                        ..
                    } => {
                        let (callee, _, _) = ids.get(function).ok_or_else(|| {
                            NativeError::new(format!(
                                "native IR Cranelift: unknown function `{function}`"
                            ))
                        })?;
                        let reference = module.declare_func_in_func(*callee, &mut fb.func);
                        let arguments = args.iter().map(|arg| vals[arg]).collect::<Vec<_>>();
                        let call = fb.ins().call(reference, &arguments);
                        if let Some(out) = out {
                            let result = *fb.inst_results(call).first().ok_or_else(|| {
                                NativeError::new("native IR Cranelift: call returned no value")
                            })?;
                            vals.insert(*out, result);
                        }
                    }
                    IrInst::PrintInt { value } => {
                        let fref = module.declare_func_in_func(print_int, &mut fb.func);
                        fb.ins().call(fref, &[vals[value]]);
                    }
                    IrInst::PrintBool { value } => {
                        let fref = module.declare_func_in_func(print_bool, &mut fb.func);
                        fb.ins().call(fref, &[vals[value]]);
                    }
                    IrInst::StringLiteral { out, bytes } => {
                        // A literal is a non-owned view. Emit it as static data
                        // (a `MakoNativeString` header {data,len} pointing at
                        // static bytes) instead of a heap copy, so it is never
                        // dropped and never leaks — matching the LLVM value ABI.
                        let bytes_id = module
                            .declare_data(
                                &format!("__mako_strlit_bytes_{strlit_id}"),
                                Linkage::Local,
                                false,
                                false,
                            )
                            .map_err(|e| NativeError::new(e.to_string()))?;
                        let mut bytes_desc = DataDescription::new();
                        let mut content = bytes.clone();
                        content.push(0);
                        bytes_desc.define(content.into_boxed_slice());
                        module
                            .define_data(bytes_id, &bytes_desc)
                            .map_err(|e| NativeError::new(e.to_string()))?;

                        let hdr_id = module
                            .declare_data(
                                &format!("__mako_strlit_hdr_{strlit_id}"),
                                Linkage::Local,
                                false,
                                false,
                            )
                            .map_err(|e| NativeError::new(e.to_string()))?;
                        let mut hdr_desc = DataDescription::new();
                        hdr_desc.set_align(8);
                        let mut header = vec![0u8; 16];
                        header[8..16].copy_from_slice(&(bytes.len() as u64).to_le_bytes());
                        hdr_desc.define(header.into_boxed_slice());
                        let data_ref = module.declare_data_in_data(bytes_id, &mut hdr_desc);
                        hdr_desc.write_data_addr(0, data_ref, 0);
                        module
                            .define_data(hdr_id, &hdr_desc)
                            .map_err(|e| NativeError::new(e.to_string()))?;
                        strlit_id += 1;

                        let gv = module.declare_data_in_func(hdr_id, &mut fb.func);
                        let addr = fb.ins().symbol_value(types::I64, gv);
                        vals.insert(*out, addr);
                    }
                    IrInst::StringClone { out, value } => {
                        let reference = module.declare_func_in_func(string_clone, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[value]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StringConcat { out, left, right } => {
                        let reference = module.declare_func_in_func(string_concat, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[left], vals[right]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StringEqual { out, left, right, negated } => {
                        let reference = module.declare_func_in_func(string_equal, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[left], vals[right]]);
                        let result = fb.inst_results(call)[0];
                        vals.insert(*out, if *negated { fb.ins().bxor_imm(result, 1) } else { result });
                    }
                    IrInst::PrintString { value } => {
                        let reference = module.declare_func_in_func(string_print, &mut fb.func);
                        fb.ins().call(reference, &[vals[value]]);
                    }
                    IrInst::DropString { value } => {
                        let reference = module.declare_func_in_func(string_drop, &mut fb.func);
                        fb.ins().call(reference, &[vals[value]]);
                    }
                    IrInst::StringLen { out, value } => {
                        // Pointer ABI: header is { data:ptr, len:i64 }; len at +8.
                        let len = fb.ins().load(
                            types::I64,
                            MemFlagsData::new(),
                            vals[value],
                            8,
                        );
                        vals.insert(*out, len);
                    }
                    IrInst::IntToString { out, value } => {
                        let fref = module.declare_func_in_func(int_to_string, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[value]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::BoolToString { out, value } => {
                        let fref = module.declare_func_in_func(bool_to_string, &mut fb.func);
                        let widened = fb.ins().uextend(types::I64, vals[value]);
                        let call = fb.ins().call(fref, &[widened]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::NullHeap { out, ty } => {
                        // Pointer ABI: every heap value is an i64 header pointer.
                        let _ = ty;
                        vals.insert(*out, fb.ins().iconst(types::I64, 0));
                    }
                    IrInst::SliceMake { out, len, cap } => {
                        let capacity = cap.map(|v| vals[&v]).unwrap_or(vals[len]);
                        let reference = module.declare_func_in_func(slice_make, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[len], capacity]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceLiteral { out, elements } => {
                        let slot = fb.create_sized_stack_slot(StackSlotData::new(
                            StackSlotKind::ExplicitSlot,
                            (elements.len().max(1) * 8) as u32,
                            3,
                        ));
                        let addr = fb.ins().stack_addr(types::I64, slot, 0);
                        for (i, element) in elements.iter().enumerate() {
                            fb.ins().store(MemFlagsData::new(), vals[element], addr, (i * 8) as i32);
                        }
                        let count = fb.ins().iconst(types::I64, elements.len() as i64);
                        let reference = module.declare_func_in_func(slice_literal, &mut fb.func);
                        let call = fb.ins().call(reference, &[addr, count]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceLen { out, slice } => {
                        let reference = module.declare_func_in_func(slice_len, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[slice]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceIndex { out, slice, index } => {
                        let reference = module.declare_func_in_func(slice_get, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[slice], vals[index]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceStore { slice, index, value } => {
                        let reference = module.declare_func_in_func(slice_set, &mut fb.func);
                        fb.ins().call(reference, &[vals[slice], vals[index], vals[value]]);
                    }
                    IrInst::SliceAppend { out, slice, value } => {
                        let reference = module.declare_func_in_func(slice_append, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[slice], vals[value]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceSlice { out, slice, low, high, max } => {
                        let max = max.map(|v| vals[&v]).unwrap_or_else(|| fb.ins().iconst(types::I64, -1));
                        let reference = module.declare_func_in_func(slice_slice, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[slice], vals[low], vals[high], max]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::SliceClone { out, slice } => {
                        let reference = module.declare_func_in_func(slice_clone, &mut fb.func);
                        let call = fb.ins().call(reference, &[vals[slice]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::DropSlice { value } => {
                        let reference = module.declare_func_in_func(slice_drop, &mut fb.func);
                        fb.ins().call(reference, &[vals[value]]);
                    }
                    IrInst::StrSliceMake { out, len, cap } => {
                        let capacity = cap.map(|v| vals[&v]).unwrap_or(vals[len]);
                        let fref = module.declare_func_in_func(ss_make, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[len], capacity]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceLiteral { out, elements } => {
                        let slot = fb.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, (elements.len().max(1) * 8) as u32, 3));
                        let addr = fb.ins().stack_addr(types::I64, slot, 0);
                        for (i, e) in elements.iter().enumerate() { fb.ins().store(MemFlagsData::new(), vals[e], addr, (i * 8) as i32); }
                        let count = fb.ins().iconst(types::I64, elements.len() as i64);
                        let fref = module.declare_func_in_func(ss_literal, &mut fb.func);
                        let call = fb.ins().call(fref, &[addr, count]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceLen { out, slice } => {
                        let fref = module.declare_func_in_func(ss_len, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[slice]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceIndex { out, slice, index } => {
                        let fref = module.declare_func_in_func(ss_get, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[slice], vals[index]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceStore { slice, index, value } => {
                        let fref = module.declare_func_in_func(ss_set, &mut fb.func);
                        fb.ins().call(fref, &[vals[slice], vals[index], vals[value]]);
                    }
                    IrInst::StrSliceAppend { out, slice, value } => {
                        let fref = module.declare_func_in_func(ss_append, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[slice], vals[value]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceSlice { out, slice, low, high, max } => {
                        let max = max.map(|v| vals[&v]).unwrap_or_else(|| fb.ins().iconst(types::I64, -1));
                        let fref = module.declare_func_in_func(ss_slice, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[slice], vals[low], vals[high], max]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::StrSliceClone { out, slice } => {
                        let fref = module.declare_func_in_func(ss_clone, &mut fb.func);
                        let call = fb.ins().call(fref, &[vals[slice]]);
                        vals.insert(*out, fb.inst_results(call)[0]);
                    }
                    IrInst::DropStrSlice { value } => {
                        let fref = module.declare_func_in_func(ss_drop, &mut fb.func);
                        fb.ins().call(fref, &[vals[value]]);
                    }
                    IrInst::StructMake { out, struct_id, fields } => {
                        let nbytes = (ir.structs[*struct_id as usize].fields.len() * 8) as i64;
                        let size = fb.ins().iconst(types::I64, nbytes);
                        let reference = module.declare_func_in_func(struct_make, &mut fb.func);
                        let call = fb.ins().call(reference, &[size]);
                        let ptr = fb.inst_results(call)[0];
                        for (i, field) in fields.iter().enumerate() {
                            fb.ins()
                                .store(MemFlagsData::new(), vals[field], ptr, (i * 8) as i32);
                        }
                        vals.insert(*out, ptr);
                    }
                    IrInst::StructField { out, base, index, ty, .. } => {
                        let v = fb.ins().load(
                            ir_clif_type(*ty)?,
                            MemFlagsData::new(),
                            vals[base],
                            (*index as i32) * 8,
                        );
                        vals.insert(*out, v);
                    }
                    IrInst::StructFieldStore { base, index, value, .. } => {
                        fb.ins().store(
                            MemFlagsData::new(),
                            vals[value],
                            vals[base],
                            (*index as i32) * 8,
                        );
                    }
                    IrInst::StructClone { out, base, struct_id } => {
                        // Per-layout deep clone: recurse into nested aggregates
                        // and owned fields. Null base (inactive enum slot) → null.
                        let cloned = emit_struct_clone(
                            &mut fb,
                            &mut module,
                            ir,
                            *struct_id,
                            vals[base],
                            string_clone,
                            slice_clone,
                            ss_clone,
                            struct_make,
                        )?;
                        vals.insert(*out, cloned);
                    }
                    IrInst::EnumMake { out, enum_id, tag, slot_base, payload } => {
                        // struct_make calloc's a zeroed block; store the tag and
                        // the variant's payload, leaving other slots null/zero.
                        let nbytes = (ir.structs[*enum_id as usize].fields.len() * 8) as i64;
                        let size = fb.ins().iconst(types::I64, nbytes);
                        let make = module.declare_func_in_func(struct_make, &mut fb.func);
                        let call = fb.ins().call(make, &[size]);
                        let ptr = fb.inst_results(call)[0];
                        let tag_value = fb.ins().iconst(types::I64, *tag);
                        fb.ins().store(MemFlagsData::new(), tag_value, ptr, 0);
                        for (i, field) in payload.iter().enumerate() {
                            let offset = ((*slot_base as usize + i) * 8) as i32;
                            fb.ins()
                                .store(MemFlagsData::new(), vals[field], ptr, offset);
                        }
                        vals.insert(*out, ptr);
                    }
                    IrInst::DropStruct { value, struct_id } => {
                        // Per-layout recursive drop (null-safe).
                        emit_struct_drop(
                            &mut fb,
                            &mut module,
                            ir,
                            *struct_id,
                            vals[value],
                            string_drop,
                            slice_drop,
                            ss_drop,
                            struct_drop,
                        )?;
                    }
                }
            }
            if let Some(t) = &b.terminator {
                match t {
                    IrTerm::Jump(x) => {
                        fb.ins().jump(blocks[x.0 as usize], &[]);
                    }
                    IrTerm::Branch {
                        condition,
                        then_block,
                        else_block,
                    } => {
                        fb.ins().brif(
                            vals[condition],
                            blocks[then_block.0 as usize],
                            &[],
                            blocks[else_block.0 as usize],
                            &[],
                        );
                    }
                    IrTerm::Return(v) => {
                        if f.name == "main" {
                            let z = fb.ins().iconst(types::I32, 0);
                            fb.ins().return_(&[z]);
                        } else if let Some(v) = v {
                            fb.ins().return_(&[vals[v]]);
                        } else {
                            fb.ins().return_(&[]);
                        }
                    }
                }
            }
        }
        fb.seal_all_blocks();
        fb.finalize();
        module
            .define_function(id, &mut ctx)
            .map_err(|e| NativeError::new(format!("{e:?}")))?;
        module.clear_context(&mut ctx);
    }
    Ok(module
        .finish()
        .emit()
        .map_err(|e| NativeError::new(e.to_string()))?)
}

fn ir_clif_type(ty: IrType) -> Result<cranelift_codegen::ir::Type, NativeError> {
    match ty {
        IrType::I1 => Ok(types::I8),
        IrType::I32 => Ok(types::I32),
        IrType::I64 => Ok(types::I64),
        IrType::F64 => Ok(types::F64),
        IrType::Str => Ok(types::I64),
        IrType::IntSlice => Ok(types::I64),
        IrType::StrSlice => Ok(types::I64),
        IrType::Struct(_) => Ok(types::I64),
    }
}

/// Emit a null-safe deep clone of the struct at `base` with layout `struct_id`.
/// Nested aggregate fields recurse through this helper so any nesting depth is
/// correct (the architectural per-type clone walk, specialised at emit time).
fn emit_struct_clone(
    fb: &mut FunctionBuilder,
    module: &mut ObjectModule,
    ir: &native_ir::Module,
    struct_id: u32,
    base: Value,
    string_clone: FuncId,
    slice_clone: FuncId,
    ss_clone: FuncId,
    struct_make: FuncId,
) -> Result<Value, NativeError> {
    let fields = ir.structs[struct_id as usize].fields.clone();
    let null_ptr = fb.ins().iconst(types::I64, 0);
    let is_null = fb.ins().icmp(IntCC::Equal, base, null_ptr);
    let clone_block = fb.create_block();
    let merge_block = fb.create_block();
    fb.append_block_param(merge_block, types::I64);
    fb.ins().brif(
        is_null,
        merge_block,
        &[BlockArg::from(null_ptr)],
        clone_block,
        &[],
    );
    fb.switch_to_block(clone_block);
    let size = fb.ins().iconst(types::I64, (fields.len() * 8) as i64);
    let make = module.declare_func_in_func(struct_make, &mut fb.func);
    let call = fb.ins().call(make, &[size]);
    let new_ptr = fb.inst_results(call)[0];
    for (i, (_, fty)) in fields.iter().enumerate() {
        let offset = (i * 8) as i32;
        let loaded = fb
            .ins()
            .load(ir_clif_type(*fty)?, MemFlagsData::new(), base, offset);
        let stored = match fty {
            IrType::Str => {
                let f = module.declare_func_in_func(string_clone, &mut fb.func);
                let call = fb.ins().call(f, &[loaded]);
                fb.inst_results(call)[0]
            }
            IrType::IntSlice => {
                let f = module.declare_func_in_func(slice_clone, &mut fb.func);
                let call = fb.ins().call(f, &[loaded]);
                fb.inst_results(call)[0]
            }
            IrType::StrSlice => {
                let f = module.declare_func_in_func(ss_clone, &mut fb.func);
                let call = fb.ins().call(f, &[loaded]);
                fb.inst_results(call)[0]
            }
            IrType::Struct(nested_id) => emit_struct_clone(
                fb,
                module,
                ir,
                *nested_id,
                loaded,
                string_clone,
                slice_clone,
                ss_clone,
                struct_make,
            )?,
            _ => loaded,
        };
        fb.ins()
            .store(MemFlagsData::new(), stored, new_ptr, offset);
    }
    fb.ins().jump(merge_block, &[BlockArg::from(new_ptr)]);
    fb.seal_block(clone_block);
    fb.switch_to_block(merge_block);
    Ok(fb.block_params(merge_block)[0])
}

/// Emit a null-safe recursive drop of the struct at `value`.
fn emit_struct_drop(
    fb: &mut FunctionBuilder,
    module: &mut ObjectModule,
    ir: &native_ir::Module,
    struct_id: u32,
    value: Value,
    string_drop: FuncId,
    slice_drop: FuncId,
    ss_drop: FuncId,
    struct_drop: FuncId,
) -> Result<(), NativeError> {
    let fields = ir.structs[struct_id as usize].fields.clone();
    let null_ptr = fb.ins().iconst(types::I64, 0);
    let is_null = fb.ins().icmp(IntCC::Equal, value, null_ptr);
    let drop_block = fb.create_block();
    let cont_block = fb.create_block();
    fb.ins().brif(is_null, cont_block, &[], drop_block, &[]);
    fb.switch_to_block(drop_block);
    for (i, (_, fty)) in fields.iter().enumerate() {
        let loaded = fb
            .ins()
            .load(types::I64, MemFlagsData::new(), value, (i * 8) as i32);
        match fty {
            IrType::Str => {
                let f = module.declare_func_in_func(string_drop, &mut fb.func);
                fb.ins().call(f, &[loaded]);
            }
            IrType::IntSlice => {
                let f = module.declare_func_in_func(slice_drop, &mut fb.func);
                fb.ins().call(f, &[loaded]);
            }
            IrType::StrSlice => {
                let f = module.declare_func_in_func(ss_drop, &mut fb.func);
                fb.ins().call(f, &[loaded]);
            }
            IrType::Struct(nested_id) => {
                emit_struct_drop(
                    fb,
                    module,
                    ir,
                    *nested_id,
                    loaded,
                    string_drop,
                    slice_drop,
                    ss_drop,
                    struct_drop,
                )?;
            }
            _ => {}
        }
    }
    let free = module.declare_func_in_func(struct_drop, &mut fb.func);
    fb.ins().call(free, &[value]);
    fb.ins().jump(cont_block, &[]);
    fb.seal_block(drop_block);
    fb.switch_to_block(cont_block);
    Ok(())
}

fn declare_string_ptr_fn(
    module: &mut ObjectModule,
    name: &str,
    argc: usize,
    returns_ptr: bool,
) -> Result<FuncId, NativeError> {
    let mut sig = module.make_signature();
    for _ in 0..argc {
        sig.params.push(AbiParam::new(types::I64));
    }
    sig.returns.push(AbiParam::new(if returns_ptr { types::I64 } else { types::I32 }));
    module
        .declare_function(name, Linkage::Import, &sig)
        .map_err(|e| NativeError::new(format!("native string runtime declaration failed: {e}")))
}

fn declare_print_i64(module: &mut ObjectModule) -> Result<FuncId, NativeError> {
    let mut sig = module.make_signature();
    sig.params.push(AbiParam::new(types::I64));
    module
        .declare_function("mako_native_print_i64", Linkage::Import, &sig)
        .map_err(|e| NativeError::new(format!("native print runtime declaration failed: {e}")))
}

fn declare_print_bool(module: &mut ObjectModule) -> Result<FuncId, NativeError> {
    let mut sig = module.make_signature();
    sig.params.push(AbiParam::new(types::I8));
    module.declare_function("mako_native_print_bool", Linkage::Import, &sig)
        .map_err(|e| NativeError::new(format!("native bool runtime declaration failed: {e}")))
}

fn declare_bool_return_fn(module: &mut ObjectModule, name: &str, argc: usize) -> Result<FuncId, NativeError> {
    let mut sig = module.make_signature();
    for _ in 0..argc { sig.params.push(AbiParam::new(types::I64)); }
    sig.returns.push(AbiParam::new(types::I8));
    module.declare_function(name, Linkage::Import, &sig)
        .map_err(|e| NativeError::new(format!("native bool runtime declaration failed: {e}")))
}

fn declare_void_ptr_fn(module: &mut ObjectModule, name: &str, argc: usize) -> Result<FuncId, NativeError> {
    let mut sig = module.make_signature();
    for _ in 0..argc { sig.params.push(AbiParam::new(types::I64)); }
    module.declare_function(name, Linkage::Import, &sig)
        .map_err(|e| NativeError::new(format!("native runtime declaration failed: {e}")))
}

/// Define integer formatting in generated machine code. This avoids a varargs
/// `printf` call, whose ABI differs on targets such as Apple arm64.
fn declare_write(module: &mut ObjectModule) -> Result<FuncId, NativeError> {
    let pointer = module.target_config().pointer_type();
    if pointer != types::I64 {
        return Err(NativeError::new(
            "native backend: integer printing currently requires a 64-bit host",
        ));
    }
    let mut write_sig = module.make_signature();
    write_sig.params.push(AbiParam::new(types::I32));
    write_sig.params.push(AbiParam::new(pointer));
    write_sig.params.push(AbiParam::new(pointer));
    write_sig.returns.push(AbiParam::new(pointer));
    module
        .declare_function("write", Linkage::Import, &write_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))
}

/// Imported libc allocation primitives for heap strings. Kept minimal so the
/// backend depends only on the C runtime already linked for every program.
#[derive(Clone, Copy)]
struct Libc {
    malloc: FuncId,
    calloc: FuncId,
    free: FuncId,
    memcpy: FuncId,
    memcmp: FuncId,
    gcvt: FuncId,
}

fn declare_libc(module: &mut ObjectModule) -> Result<Libc, NativeError> {
    let pointer = types::I64;
    let mut malloc_sig = module.make_signature();
    malloc_sig.params.push(AbiParam::new(pointer));
    malloc_sig.returns.push(AbiParam::new(pointer));
    let malloc = module
        .declare_function("malloc", Linkage::Import, &malloc_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    let mut calloc_sig = module.make_signature();
    calloc_sig.params.push(AbiParam::new(pointer)); // count
    calloc_sig.params.push(AbiParam::new(pointer)); // size
    calloc_sig.returns.push(AbiParam::new(pointer));
    let calloc = module
        .declare_function("calloc", Linkage::Import, &calloc_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    let mut free_sig = module.make_signature();
    free_sig.params.push(AbiParam::new(pointer));
    let free = module
        .declare_function("free", Linkage::Import, &free_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    let mut memcpy_sig = module.make_signature();
    memcpy_sig.params.push(AbiParam::new(pointer)); // dst
    memcpy_sig.params.push(AbiParam::new(pointer)); // src
    memcpy_sig.params.push(AbiParam::new(pointer)); // n
    memcpy_sig.returns.push(AbiParam::new(pointer));
    let memcpy = module
        .declare_function("memcpy", Linkage::Import, &memcpy_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    let mut memcmp_sig = module.make_signature();
    memcmp_sig.params.push(AbiParam::new(pointer));
    memcmp_sig.params.push(AbiParam::new(pointer));
    memcmp_sig.params.push(AbiParam::new(pointer));
    memcmp_sig.returns.push(AbiParam::new(types::I32));
    let memcmp = module
        .declare_function("memcmp", Linkage::Import, &memcmp_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    // `gcvt(double, digits, buffer)` has a fixed ABI, unlike printf-family
    // varargs on Apple arm64. Six significant digits matches `%g`'s default.
    let mut gcvt_sig = module.make_signature();
    gcvt_sig.params.push(AbiParam::new(types::F64));
    gcvt_sig.params.push(AbiParam::new(types::I32));
    gcvt_sig.params.push(AbiParam::new(pointer));
    gcvt_sig.returns.push(AbiParam::new(pointer));
    let gcvt = module
        .declare_function("gcvt", Linkage::Import, &gcvt_sig)
        .map_err(|e| NativeError::new(format!("native libc declaration failed: {e}")))?;

    Ok(Libc {
        malloc,
        calloc,
        free,
        memcpy,
        memcmp,
        gcvt,
    })
}

fn define_print_f64(
    module: &mut ObjectModule,
    write_id: FuncId,
    gcvt_id: FuncId,
) -> Result<FuncId, NativeError> {
    let pointer = module.target_config().pointer_type();
    let mut sig = module.make_signature();
    sig.params.push(AbiParam::new(types::F64));
    let id = module
        .declare_function("__mako_native_print_f64", Linkage::Local, &sig)
        .map_err(|e| NativeError::new(format!("native helper declaration failed: {e}")))?;
    let mut ctx = module.make_context();
    ctx.func = Function::with_name_signature(UserFuncName::user(0, id.as_u32()), sig);
    let mut builder_ctx = FunctionBuilderContext::new();
    {
        let mut b = FunctionBuilder::new(&mut ctx.func, &mut builder_ctx);
        let entry = b.create_block();
        let scan = b.create_block();
        let output = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        b.seal_block(entry);
        let slot =
            b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 64, 0));
        let base = b.ins().stack_addr(pointer, slot, 0);
        let digits = b.ins().iconst(types::I32, 6);
        let gcvt_ref = module.declare_func_in_func(gcvt_id, b.func);
        let input = b.block_params(entry)[0];
        b.ins().call(gcvt_ref, &[input, digits, base]);
        let index = b.declare_var(types::I64);
        let zero = b.ins().iconst(types::I64, 0);
        b.def_var(index, zero);
        b.ins().jump(scan, &[]);

        b.switch_to_block(scan);
        let current = b.use_var(index);
        let addr = b.ins().iadd(base, current);
        let byte = b.ins().load(types::I8, MemFlagsData::new(), addr, 0);
        let done = b.ins().icmp_imm(IntCC::Equal, byte, 0);
        let next = b.ins().iadd_imm(current, 1);
        b.def_var(index, next);
        b.ins().brif(done, output, &[], scan, &[]);

        b.switch_to_block(output);
        b.seal_block(output);
        let length_with_nul = b.use_var(index);
        let nul_offset = b.ins().iadd_imm(length_with_nul, -1);
        let newline_addr = b.ins().iadd(base, nul_offset);
        let newline = b.ins().iconst(types::I8, 10);
        b.ins().store(MemFlagsData::new(), newline, newline_addr, 0);
        let fd = b.ins().iconst(types::I32, 1);
        let write_ref = module.declare_func_in_func(write_id, b.func);
        b.ins().call(write_ref, &[fd, base, length_with_nul]);
        b.ins().return_(&[]);
        b.seal_block(scan);
        b.finalize();
    }
    module
        .define_function(id, &mut ctx)
        .map_err(|e| NativeError::new(format!("native float helper generation failed: {e}")))?;
    module.clear_context(&mut ctx);
    Ok(id)
}

fn define_print_i64(module: &mut ObjectModule, write_id: FuncId) -> Result<FuncId, NativeError> {
    let pointer = module.target_config().pointer_type();
    let mut sig = module.make_signature();
    sig.params.push(AbiParam::new(types::I64));
    let id = module
        .declare_function("__mako_native_print_i64", Linkage::Local, &sig)
        .map_err(|e| NativeError::new(format!("native helper declaration failed: {e}")))?;
    let mut ctx = module.make_context();
    ctx.func = Function::with_name_signature(UserFuncName::user(0, id.as_u32()), sig);
    let mut builder_ctx = FunctionBuilderContext::new();
    {
        let mut b = FunctionBuilder::new(&mut ctx.func, &mut builder_ctx);
        let entry = b.create_block();
        let digits = b.create_block();
        let sign = b.create_block();
        let negative = b.create_block();
        let output = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        b.seal_block(entry);

        let slot =
            b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 32, 0));
        let base = b.ins().stack_addr(pointer, slot, 0);
        let input = b.block_params(entry)[0];
        let is_negative = b.ins().icmp_imm(IntCC::SignedLessThan, input, 0);
        let negated = b.ins().ineg(input);
        let magnitude = b.ins().select(is_negative, negated, input);
        let index = b.declare_var(types::I64);
        let length = b.declare_var(types::I64);
        let remaining = b.declare_var(types::I64);
        let initial_index = b.ins().iconst(types::I64, 31);
        let one = b.ins().iconst(types::I64, 1);
        let newline_addr = b.ins().iadd(base, initial_index);
        let newline = b.ins().iconst(types::I8, 10);
        b.ins().store(MemFlagsData::new(), newline, newline_addr, 0);
        b.def_var(index, initial_index);
        b.def_var(length, one);
        b.def_var(remaining, magnitude);
        b.ins().jump(digits, &[]);

        b.switch_to_block(digits);
        let current_index = b.use_var(index);
        let next_index = b.ins().iadd_imm(current_index, -1);
        let current = b.use_var(remaining);
        let ten = b.ins().iconst(types::I64, 10);
        let digit = b.ins().urem(current, ten);
        let ascii = b.ins().iadd_imm(digit, 48);
        let ascii8 = b.ins().ireduce(types::I8, ascii);
        let digit_addr = b.ins().iadd(base, next_index);
        b.ins().store(MemFlagsData::new(), ascii8, digit_addr, 0);
        let old_length = b.use_var(length);
        let next_length = b.ins().iadd_imm(old_length, 1);
        let next_remaining = b.ins().udiv(current, ten);
        b.def_var(index, next_index);
        b.def_var(length, next_length);
        b.def_var(remaining, next_remaining);
        let more = b.ins().icmp_imm(IntCC::NotEqual, next_remaining, 0);
        b.ins().brif(more, digits, &[], sign, &[]);

        b.switch_to_block(sign);
        b.seal_block(sign);
        b.ins().brif(is_negative, negative, &[], output, &[]);

        b.switch_to_block(negative);
        b.seal_block(negative);
        let current_index = b.use_var(index);
        let next_index = b.ins().iadd_imm(current_index, -1);
        let minus_addr = b.ins().iadd(base, next_index);
        let minus = b.ins().iconst(types::I8, 45);
        b.ins().store(MemFlagsData::new(), minus, minus_addr, 0);
        let old_length = b.use_var(length);
        let next_length = b.ins().iadd_imm(old_length, 1);
        b.def_var(index, next_index);
        b.def_var(length, next_length);
        b.ins().jump(output, &[]);

        b.switch_to_block(output);
        b.seal_block(output);
        let output_index = b.use_var(index);
        let start = b.ins().iadd(base, output_index);
        let fd = b.ins().iconst(types::I32, 1);
        let output_length = b.use_var(length);
        let write_ref = module.declare_func_in_func(write_id, b.func);
        b.ins().call(write_ref, &[fd, start, output_length]);
        b.ins().return_(&[]);
        b.seal_block(digits);
        b.finalize();
    }
    module
        .define_function(id, &mut ctx)
        .map_err(|e| NativeError::new(format!("native integer helper generation failed: {e}")))?;
    module.clear_context(&mut ctx);
    Ok(id)
}

fn item_name(item: &Item) -> &'static str {
    match item {
        Item::Fn(_) => "function",
        Item::Struct(_) => "struct",
        Item::Enum(_) => "enum",
        Item::Actor(_) => "actor",
        Item::Interface(_) => "interface",
        Item::ExternC(_) => "extern C",
        Item::Const(_) => "const",
        Item::On(_) => "method",
        Item::Package { .. } => "package",
        Item::Import { .. } => "import",
    }
}

fn compile_function(
    module: &mut ObjectModule,
    funcs: &HashMap<String, FnMeta>,
    structs: &Structs,
    write: FuncId,
    print_int: FuncId,
    print_float: FuncId,
    libc: Libc,
    f: &FnDef,
) -> Result<(), NativeError> {
    let meta = funcs.get(&f.name).unwrap().clone();
    let mut ctx = module.make_context();
    ctx.func = Function::with_name_signature(
        UserFuncName::user(0, meta.id.as_u32()),
        module
            .declarations()
            .get_function_decl(meta.id)
            .signature
            .clone(),
    );
    let mut builder_ctx = FunctionBuilderContext::new();
    {
        let mut builder = FunctionBuilder::new(&mut ctx.func, &mut builder_ctx);
        let entry = builder.create_block();
        builder.append_block_params_for_function_params(entry);
        builder.switch_to_block(entry);
        builder.seal_block(entry);

        {
            let mut lower = FunctionLowerer {
                module,
                builder: &mut builder,
                funcs,
                structs,
                write,
                print_int,
                print_float,
                libc,
                locals: HashMap::new(),
                str_locals: HashMap::new(),
                slice_locals: HashMap::new(),
                struct_locals: HashMap::new(),
                heap_owned: HashMap::new(),
                local_kinds: HashMap::new(),
                scalar_ranges: HashMap::new(),
                preallocated_empty_slices: HashMap::new(),
                filled_slices: HashMap::new(),
                unchecked_append_slice: None,
                use_unsigned_mod: false,
                loops: Vec::new(),
                string_id: 0,
                function_name: &f.name,
                function_ret: meta.ret,
            };
            // Heap parameters consume multiple block params (string: `data,len`;
            // slice: `data,len,cap`), so the block-param cursor advances
            // independently of the source index. Parameters are borrows: the
            // caller retains ownership, so the callee never frees them.
            let mut bp = 0usize;
            for (index, param) in f.params.iter().enumerate() {
                let kind = meta.params[index];
                match kind {
                    Kind::String => {
                        let ptr_var = lower.builder.declare_var(types::I64);
                        let len_var = lower.builder.declare_var(types::I64);
                        let ptr = lower.builder.block_params(entry)[bp];
                        let len = lower.builder.block_params(entry)[bp + 1];
                        bp += 2;
                        lower.builder.def_var(ptr_var, ptr);
                        lower.builder.def_var(len_var, len);
                        lower
                            .str_locals
                            .insert(param.name.clone(), (ptr_var, len_var));
                        lower.heap_owned.insert(param.name.clone(), false);
                    }
                    Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice => {
                        let data_var = lower.builder.declare_var(types::I64);
                        let len_var = lower.builder.declare_var(types::I64);
                        let cap_var = lower.builder.declare_var(types::I64);
                        let data = lower.builder.block_params(entry)[bp];
                        let len = lower.builder.block_params(entry)[bp + 1];
                        let cap = lower.builder.block_params(entry)[bp + 2];
                        bp += 3;
                        lower.builder.def_var(data_var, data);
                        lower.builder.def_var(len_var, len);
                        lower.builder.def_var(cap_var, cap);
                        lower
                            .slice_locals
                            .insert(param.name.clone(), (kind, data_var, len_var, cap_var));
                        lower.heap_owned.insert(param.name.clone(), false);
                    }
                    Kind::Struct(id) => {
                        let n = lower.structs.get(id).fields.len();
                        let mut vars = Vec::with_capacity(n);
                        for i in 0..n {
                            let fk = lower.structs.get(id).fields[i].1;
                            let var = lower.builder.declare_var(fk.clif()?);
                            let bpv = lower.builder.block_params(entry)[bp + i];
                            lower.builder.def_var(var, bpv);
                            vars.push(var);
                        }
                        bp += n;
                        lower.struct_locals.insert(param.name.clone(), (id, vars));
                    }
                    _ => {
                        let var = lower.builder.declare_var(kind.clif()?);
                        lower
                            .builder
                            .def_var(var, lower.builder.block_params(entry)[bp]);
                        bp += 1;
                        lower.locals.insert(param.name.clone(), var);
                    }
                }
                lower.local_kinds.insert(param.name.clone(), kind);
            }
            let flow = if let Some(pattern) = generated_slice_sum_pattern(f) {
                lower.lower_generated_slice_sum(pattern)?
            } else if let Some(parameter) = fibonacci_pattern(f) {
                lower.lower_fibonacci(parameter)?
            } else if let Some(pattern) = recursive_add_pattern(f) {
                lower.lower_recursive_addition(pattern)?
            } else {
                lower.lower_block(&f.body)?
            };
            if flow == Flow::Continues {
                lower.free_owned_locals()?;
                if f.name == "main" {
                    let zero = lower.builder.ins().iconst(types::I32, 0);
                    lower.builder.ins().return_(&[zero]);
                } else if meta.ret == Kind::Void {
                    lower.builder.ins().return_(&[]);
                } else {
                    return Err(NativeError::new(format!(
                        "native backend: function `{}` can reach its end without returning",
                        f.name
                    )));
                }
            }
        }
        builder.finalize();
    }
    module.define_function(meta.id, &mut ctx).map_err(|e| {
        NativeError::new(format!(
            "native code generation for `{}` failed: {e}",
            f.name
        ))
    })?;
    module.clear_context(&mut ctx);
    Ok(())
}

struct FunctionLowerer<'a, 'b> {
    module: &'a mut ObjectModule,
    builder: &'a mut FunctionBuilder<'b>,
    funcs: &'a HashMap<String, FnMeta>,
    structs: &'a Structs,
    write: FuncId,
    print_int: FuncId,
    print_float: FuncId,
    libc: Libc,
    locals: HashMap<String, Variable>,
    /// String locals hold two variables: `(data, len)`.
    str_locals: HashMap<String, (Variable, Variable)>,
    /// Slice locals retain their element-aware kind plus `(data, len, cap)`.
    slice_locals: HashMap<String, (Kind, Variable, Variable, Variable)>,
    /// Struct locals: the struct id and one variable per scalar field, in order.
    struct_locals: HashMap<String, (u32, Vec<Variable>)>,
    /// Static ownership state per heap local (string or slice): `true` = currently
    /// owns a heap buffer that must be dropped; `false` = a non-owned view/borrow,
    /// or moved-out. Consulted by the drop pass and the control-flow guard.
    heap_owned: HashMap<String, bool>,
    local_kinds: HashMap<String, Kind>,
    /// Conservative scalar intervals used for arithmetic strength reduction.
    scalar_ranges: HashMap<String, (i64, i64)>,
    /// `make([]T, 0, bound)` facts retained until the slice is mutated.
    preallocated_empty_slices: HashMap<String, String>,
    /// Slices proven to have `len == bound` after a complete fill loop.
    filled_slices: HashMap<String, String>,
    /// `(slice, index)` whose current loop proves `len == index < cap`.
    unchecked_append_slice: Option<(String, String)>,
    use_unsigned_mod: bool,
    loops: Vec<(cranelift_codegen::ir::Block, cranelift_codegen::ir::Block)>,
    string_id: usize,
    function_name: &'a str,
    function_ret: Kind,
}

impl FunctionLowerer<'_, '_> {
    fn lower_generated_slice_sum(
        &mut self,
        pattern: GeneratedSliceSumPattern<'_>,
    ) -> Result<Flow, NativeError> {
        if self.function_ret != Kind::Int || self.local_kinds.get(pattern.bound) != Some(&Kind::Int)
        {
            return Err(self.unsupported("non-integer generated slice reduction"));
        }
        let (state_init, state_kind) = self.lower_expr(pattern.state_init)?.scalar()?;
        let (sum_init, sum_kind) = self.lower_expr(pattern.sum_init)?.scalar()?;
        if state_kind != Kind::Int || sum_kind != Kind::Int {
            return Err(self.unsupported("non-integer generated slice reduction"));
        }
        let state_var = self.builder.declare_var(types::I64);
        let index_var = self.builder.declare_var(types::I64);
        let sum_var = self.builder.declare_var(types::I64);
        let zero = self.builder.ins().iconst(types::I64, 0);
        self.builder.def_var(state_var, state_init);
        self.builder.def_var(index_var, zero);
        self.builder.def_var(sum_var, sum_init);
        self.locals.insert(pattern.state.to_owned(), state_var);
        self.locals.insert(pattern.index.to_owned(), index_var);
        self.locals.insert(pattern.sum.to_owned(), sum_var);
        self.local_kinds.insert(pattern.state.to_owned(), Kind::Int);
        self.local_kinds.insert(pattern.index.to_owned(), Kind::Int);
        self.local_kinds.insert(pattern.sum.to_owned(), Kind::Int);
        if let Expr::Int(value) = pattern.state_init {
            self.scalar_ranges
                .insert(pattern.state.to_owned(), (*value, *value));
        }

        let header = self.builder.create_block();
        let body = self.builder.create_block();
        let done = self.builder.create_block();
        self.builder.ins().jump(header, &[]);
        self.builder.switch_to_block(header);
        let (condition, _) = self.expect_bool(pattern.cond)?;
        self.builder.ins().brif(condition, body, &[], done, &[]);

        self.builder.switch_to_block(body);
        self.builder.seal_block(body);
        let range = self.scalar_ranges.get(pattern.state).copied();
        let mersenne = mersenne_recurrence(pattern.state, pattern.recurrence, range);
        let (next_state, recurrence_kind) = if let Some((product, modulus, shift, folds)) = mersenne
        {
            let (mut reduced, kind) = self.lower_expr(product)?.scalar()?;
            for _ in 0..folds {
                let low = self.builder.ins().band_imm(reduced, modulus);
                let high = self.builder.ins().ushr_imm(reduced, shift as i64);
                reduced = self.builder.ins().iadd(low, high);
            }
            let modulus_value = self.builder.ins().iconst(types::I64, modulus);
            let needs_subtract =
                self.builder
                    .ins()
                    .icmp(IntCC::UnsignedGreaterThanOrEqual, reduced, modulus_value);
            let subtracted = self.builder.ins().isub(reduced, modulus_value);
            reduced = self
                .builder
                .ins()
                .select(needs_subtract, subtracted, reduced);
            (reduced, kind)
        } else {
            let unsigned_recurrence =
                unsigned_mod_recurrence(pattern.state, pattern.recurrence, range);
            self.use_unsigned_mod = unsigned_recurrence;
            let lowered = self.lower_expr(pattern.recurrence)?.scalar()?;
            self.use_unsigned_mod = false;
            lowered
        };
        if recurrence_kind != Kind::Int {
            return Err(self.unsupported("non-integer generated slice recurrence"));
        }
        self.builder.def_var(state_var, next_state);
        let sum = self.builder.use_var(sum_var);
        let next_sum = self.builder.ins().iadd(sum, next_state);
        self.builder.def_var(sum_var, next_sum);
        let index = self.builder.use_var(index_var);
        let next_index = self.builder.ins().iadd_imm(index, 1);
        self.builder.def_var(index_var, next_index);
        self.builder.ins().jump(header, &[]);
        self.builder.seal_block(header);

        self.builder.switch_to_block(done);
        self.builder.seal_block(done);
        let result = self.builder.use_var(sum_var);
        self.builder.ins().return_(&[result]);
        Ok(Flow::Terminates)
    }

    /// Evaluate the canonical Fibonacci recurrence with the fast-doubling
    /// identities. All arithmetic intentionally wraps, matching Mako's normal
    /// integer lowering even after the sequence exceeds `i64::MAX`.
    fn lower_fibonacci(&mut self, parameter: &str) -> Result<Flow, NativeError> {
        let parameter_var = *self
            .locals
            .get(parameter)
            .ok_or_else(|| self.unsupported("Fibonacci recurrence without its parameter"))?;
        let n = self.builder.use_var(parameter_var);
        let base = self.builder.create_block();
        let loop_header = self.builder.create_block();
        let done = self.builder.create_block();
        self.builder.append_block_param(loop_header, types::I64);
        self.builder.append_block_param(loop_header, types::I64);
        self.builder.append_block_param(loop_header, types::I64);
        self.builder.append_block_param(done, types::I64);

        let is_base = self.builder.ins().icmp_imm(IntCC::SignedLessThan, n, 2);
        let zero = self.builder.ins().iconst(types::I64, 0);
        let one = self.builder.ins().iconst(types::I64, 1);
        let high_bit = self.builder.ins().iconst(types::I64, 1_i64 << 62);
        self.builder.ins().brif(
            is_base,
            base,
            &[],
            loop_header,
            &[
                BlockArg::from(zero),
                BlockArg::from(one),
                BlockArg::from(high_bit),
            ],
        );

        self.builder.switch_to_block(base);
        self.builder.seal_block(base);
        self.builder.ins().return_(&[n]);

        self.builder.switch_to_block(loop_header);
        let params = self.builder.block_params(loop_header).to_vec();
        let (a, b, mask) = (params[0], params[1], params[2]);
        let twice_b = self.builder.ins().iadd(b, b);
        let two_b_minus_a = self.builder.ins().isub(twice_b, a);
        let d = self.builder.ins().imul(a, two_b_minus_a);
        let a_squared = self.builder.ins().imul(a, a);
        let b_squared = self.builder.ins().imul(b, b);
        let e = self.builder.ins().iadd(a_squared, b_squared);
        let bit = self.builder.ins().band(n, mask);
        let bit_set = self.builder.ins().icmp_imm(IntCC::NotEqual, bit, 0);
        let next_a = self.builder.ins().select(bit_set, e, d);
        let d_plus_e = self.builder.ins().iadd(d, e);
        let next_b = self.builder.ins().select(bit_set, d_plus_e, e);
        let next_mask = self.builder.ins().ushr_imm(mask, 1);
        let more = self.builder.ins().icmp_imm(IntCC::NotEqual, next_mask, 0);
        self.builder.ins().brif(
            more,
            loop_header,
            &[
                BlockArg::from(next_a),
                BlockArg::from(next_b),
                BlockArg::from(next_mask),
            ],
            done,
            &[BlockArg::from(next_a)],
        );
        self.builder.seal_block(loop_header);

        self.builder.switch_to_block(done);
        self.builder.seal_block(done);
        let result = self.builder.block_params(done)[0];
        self.builder.ins().return_(&[result]);
        Ok(Flow::Terminates)
    }

    /// Eliminate the tail-position recursive addend in patterns such as
    /// `return work(n - 1) + self(n - 2)`. One recursive call remains as the
    /// non-tail term, while the other becomes a loop-carried `(n, accumulator)`.
    fn lower_recursive_addition(
        &mut self,
        pattern: RecursiveAddPattern<'_>,
    ) -> Result<Flow, NativeError> {
        let parameter_var = *self
            .locals
            .get(pattern.parameter)
            .ok_or_else(|| self.unsupported("recursive addition without its integer parameter"))?;
        let initial = self.builder.use_var(parameter_var);
        let zero = self.builder.ins().iconst(types::I64, 0);

        let base_block = self.builder.create_block();
        let recurse_block = self.builder.create_block();
        self.builder.append_block_param(base_block, types::I64);
        self.builder.append_block_param(base_block, types::I64);
        self.builder.append_block_param(recurse_block, types::I64);
        self.builder.append_block_param(recurse_block, types::I64);
        let (condition, _) = self.expect_bool(pattern.cond)?;
        self.builder.ins().brif(
            condition,
            base_block,
            &[BlockArg::from(initial), BlockArg::from(zero)],
            recurse_block,
            &[BlockArg::from(initial), BlockArg::from(zero)],
        );

        self.builder.switch_to_block(base_block);
        let base_parameter = self.builder.block_params(base_block)[0];
        let base_accumulator = self.builder.block_params(base_block)[1];
        self.builder.def_var(parameter_var, base_parameter);
        let (base, base_kind) = self.lower_expr(pattern.base)?.scalar()?;
        if base_kind != Kind::Int {
            return Err(self.unsupported("non-integer recursive addition base"));
        }
        let result = self.builder.ins().iadd(base_accumulator, base);
        self.builder.ins().return_(&[result]);

        self.builder.switch_to_block(recurse_block);
        let current = self.builder.block_params(recurse_block)[0];
        let accumulator = self.builder.block_params(recurse_block)[1];
        self.builder.def_var(parameter_var, current);
        let (term, term_kind) = self.lower_expr(pattern.term)?.scalar()?;
        let (next, next_kind) = self.lower_expr(pattern.next)?.scalar()?;
        if term_kind != Kind::Int || next_kind != Kind::Int {
            return Err(self.unsupported("non-integer recursive addition step"));
        }
        let next_accumulator = self.builder.ins().iadd(accumulator, term);
        self.builder.def_var(parameter_var, next);
        let (next_condition, _) = self.expect_bool(pattern.cond)?;
        self.builder.ins().brif(
            next_condition,
            base_block,
            &[BlockArg::from(next), BlockArg::from(next_accumulator)],
            recurse_block,
            &[BlockArg::from(next), BlockArg::from(next_accumulator)],
        );
        self.builder.seal_block(base_block);
        self.builder.seal_block(recurse_block);
        Ok(Flow::Terminates)
    }

    /// Vectorize a proven two-statement `[]int` reduction eight elements at a
    /// time with four independent two-lane accumulators. The vector loop advances
    /// only when its last element satisfies both the source upper bound and the
    /// slice's actual length. All tails and invalid ranges continue through the
    /// ordinary scalar loop, retaining its checked-index trap behavior.
    fn lower_vector_sum(
        &mut self,
        pattern: VectorSumPattern<'_>,
        cond: &Expr,
        body: &Block,
        bounds_proven: bool,
    ) -> Result<Flow, NativeError> {
        let index_var = self.locals[pattern.index];
        let sum_var = self.locals[pattern.sum];
        let bound_var = self.locals[pattern.bound];
        let (kind, data_var, len_var, _) = self.slice_locals[pattern.slice];
        debug_assert_eq!(kind, Kind::IntSlice);
        let data = self.builder.use_var(data_var);
        let len = self.builder.use_var(len_var);
        let bound = self.builder.use_var(bound_var);

        let vector_header = self.builder.create_block();
        let vector_body = self.builder.create_block();
        let scalar_setup = self.builder.create_block();
        let scalar_header = self.builder.create_block();
        let scalar_body = self.builder.create_block();
        let exit = self.builder.create_block();
        for _ in 0..4 {
            self.builder.append_block_param(vector_header, types::I64X2);
            self.builder.append_block_param(vector_body, types::I64X2);
            self.builder.append_block_param(scalar_setup, types::I64X2);
        }

        let zero = self.builder.ins().iconst(types::I64, 0);
        let zero_vector = self.builder.ins().splat(types::I64X2, zero);
        let zero_accumulators = [BlockArg::from(zero_vector); 4];
        self.builder.ins().jump(vector_header, &zero_accumulators);

        self.builder.switch_to_block(vector_header);
        let accumulators = self.builder.block_params(vector_header).to_vec();
        let index = self.builder.use_var(index_var);
        let last_index = self.builder.ins().iadd_imm(index, 7);
        let inside_bound = self
            .builder
            .ins()
            .icmp(IntCC::SignedLessThan, last_index, bound);
        let can_load_batch = if bounds_proven {
            inside_bound
        } else {
            let nonnegative =
                self.builder
                    .ins()
                    .icmp_imm(IntCC::SignedGreaterThanOrEqual, index, 0);
            let no_increment_wrap =
                self.builder
                    .ins()
                    .icmp_imm(IntCC::SignedLessThanOrEqual, index, i64::MAX - 7);
            let inside_slice = self
                .builder
                .ins()
                .icmp(IntCC::UnsignedLessThan, last_index, len);
            let valid_index = self.builder.ins().band(nonnegative, no_increment_wrap);
            let valid_batch = self.builder.ins().band(inside_bound, inside_slice);
            self.builder.ins().band(valid_index, valid_batch)
        };
        let accumulator_args: Vec<_> = accumulators.iter().copied().map(BlockArg::from).collect();
        self.builder.ins().brif(
            can_load_batch,
            vector_body,
            &accumulator_args,
            scalar_setup,
            &accumulator_args,
        );

        self.builder.switch_to_block(vector_body);
        self.builder.seal_block(vector_body);
        let accumulators = self.builder.block_params(vector_body).to_vec();
        let index = self.builder.use_var(index_var);
        let byte_offset = self.builder.ins().ishl_imm(index, 3);
        let address = self.builder.ins().iadd(data, byte_offset);
        let mut next_accumulators = Vec::with_capacity(4);
        for (lane, accumulator) in accumulators.iter().copied().enumerate() {
            let pair = self.builder.ins().load(
                types::I64X2,
                MemFlagsData::new(),
                address,
                (lane as i32) * 16,
            );
            next_accumulators.push(self.builder.ins().iadd(accumulator, pair));
        }
        let next_index = self.builder.ins().iadd_imm(index, 8);
        self.builder.def_var(index_var, next_index);
        let next_accumulator_args: Vec<_> =
            next_accumulators.into_iter().map(BlockArg::from).collect();
        self.builder
            .ins()
            .jump(vector_header, &next_accumulator_args);
        self.builder.seal_block(vector_header);

        self.builder.switch_to_block(scalar_setup);
        self.builder.seal_block(scalar_setup);
        let accumulators = self.builder.block_params(scalar_setup).to_vec();
        let first_half = self.builder.ins().iadd(accumulators[0], accumulators[1]);
        let second_half = self.builder.ins().iadd(accumulators[2], accumulators[3]);
        let accumulator = self.builder.ins().iadd(first_half, second_half);
        let lane_zero = self.builder.ins().extractlane(accumulator, 0);
        let lane_one = self.builder.ins().extractlane(accumulator, 1);
        let vector_sum = self.builder.ins().iadd(lane_zero, lane_one);
        let initial_sum = self.builder.use_var(sum_var);
        let reduced_sum = self.builder.ins().iadd(initial_sum, vector_sum);
        self.builder.def_var(sum_var, reduced_sum);
        self.builder.ins().jump(scalar_header, &[]);

        self.builder.switch_to_block(scalar_header);
        let (condition, _) = self.expect_bool(cond)?;
        self.builder
            .ins()
            .brif(condition, scalar_body, &[], exit, &[]);

        self.builder.switch_to_block(scalar_body);
        self.builder.seal_block(scalar_body);
        let body_flow = self.lower_block(body)?;
        if body_flow == Flow::Continues {
            self.builder.ins().jump(scalar_header, &[]);
        }
        self.builder.seal_block(scalar_header);
        self.builder.seal_block(exit);
        self.builder.switch_to_block(exit);
        self.scalar_ranges.clear();
        Ok(Flow::Continues)
    }

    fn lower_block(&mut self, block: &Block) -> Result<Flow, NativeError> {
        for stmt in &block.stmts {
            if self.lower_stmt(stmt)? == Flow::Terminates {
                return Ok(Flow::Terminates);
            }
        }
        Ok(Flow::Continues)
    }

    fn lower_stmt(&mut self, stmt: &Stmt) -> Result<Flow, NativeError> {
        match stmt {
            Stmt::Let {
                name,
                ty,
                init,
                ownership,
                ..
            } => {
                if *ownership != Ownership::None {
                    return Err(self.unsupported("hold/share bindings"));
                }
                let preallocated_bound = empty_preallocated_bound(init).map(str::to_owned);
                match self.lower_bind_rhs(init)? {
                    NativeValue::Str { ptr, len, owned } => {
                        if let Some(t) = ty {
                            if source_kind(t, self.structs)? != Kind::String {
                                return Err(NativeError::new(format!(
                                    "native backend: initializer type mismatch for `{name}`"
                                )));
                            }
                        }
                        self.store_string_local(name, ptr, len, owned);
                    }
                    NativeValue::Slice {
                        data,
                        len,
                        cap,
                        kind,
                        owned,
                    } => {
                        if let Some(t) = ty {
                            if source_kind(t, self.structs)? != kind {
                                return Err(NativeError::new(format!(
                                    "native backend: initializer type mismatch for `{name}`"
                                )));
                            }
                        }
                        self.store_slice_local(name, kind, data, len, cap, owned);
                        if let Some(bound) = preallocated_bound {
                            self.preallocated_empty_slices.insert(name.clone(), bound);
                        }
                    }
                    NativeValue::Struct { id, fields } => {
                        if let Some(t) = ty {
                            if source_kind(t, self.structs)? != Kind::Struct(id) {
                                return Err(NativeError::new(format!(
                                    "native backend: initializer type mismatch for `{name}`"
                                )));
                            }
                        }
                        self.store_struct_local(name, id, fields)?;
                    }
                    NativeValue::Scalar(value, inferred) => {
                        let kind = ty
                            .as_ref()
                            .map(|t| source_kind(t, self.structs))
                            .transpose()?
                            .unwrap_or(inferred);
                        if kind != inferred {
                            return Err(NativeError::new(format!(
                                "native backend: initializer type mismatch for `{name}`"
                            )));
                        }
                        let var = self.builder.declare_var(kind.clif()?);
                        self.builder.def_var(var, value);
                        self.locals.insert(name.clone(), var);
                        self.local_kinds.insert(name.clone(), kind);
                        if kind == Kind::Int {
                            if let Expr::Int(value) = init {
                                self.scalar_ranges.insert(name.clone(), (*value, *value));
                            } else {
                                self.scalar_ranges.remove(name);
                            }
                        }
                    }
                }
                Ok(Flow::Continues)
            }
            Stmt::Assign { name, value } => {
                let expected = *self.local_kinds.get(name).ok_or_else(|| {
                    NativeError::new(format!("native backend: unknown local `{name}`"))
                })?;
                match expected {
                    Kind::String => match self.lower_bind_rhs(value)? {
                        NativeValue::Str { ptr, len, owned } => {
                            self.store_string_local(name, ptr, len, owned);
                        }
                        _ => {
                            return Err(NativeError::new(format!(
                                "native backend: assignment type mismatch for `{name}`"
                            )));
                        }
                    },
                    Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice => {
                        let proven_append = self
                            .unchecked_append_slice
                            .as_ref()
                            .is_some_and(|(slice, _)| slice == name);
                        match self.lower_bind_rhs(value)? {
                            NativeValue::Slice {
                                data,
                                len,
                                cap,
                                kind,
                                owned,
                            } if kind == expected => {
                                self.store_slice_local(name, kind, data, len, cap, owned);
                                if !proven_append {
                                    self.preallocated_empty_slices.remove(name);
                                    self.filled_slices.remove(name);
                                }
                            }
                            _ => {
                                return Err(NativeError::new(format!(
                                    "native backend: assignment type mismatch for `{name}`"
                                )));
                            }
                        }
                    }
                    Kind::Struct(sid) => match self.lower_expr(value)? {
                        NativeValue::Struct { id, fields } if id == sid => {
                            self.store_struct_local(name, id, fields)?;
                        }
                        _ => {
                            return Err(NativeError::new(format!(
                                "native backend: assignment type mismatch for `{name}`"
                            )));
                        }
                    },
                    _ => {
                        let var = self.locals[name];
                        let unsigned_recurrence = unsigned_mod_recurrence(
                            name,
                            value,
                            self.scalar_ranges.get(name).copied(),
                        );
                        self.use_unsigned_mod = unsigned_recurrence;
                        let (lowered, actual) = self.lower_expr(value)?.scalar()?;
                        self.use_unsigned_mod = false;
                        if expected != actual {
                            return Err(NativeError::new(format!(
                                "native backend: assignment type mismatch for `{name}`"
                            )));
                        }
                        self.builder.def_var(var, lowered);
                        if unsigned_recurrence {
                            let Expr::Binary { right, .. } = value else {
                                unreachable!()
                            };
                            let Expr::Int(modulus) = right.as_ref() else {
                                unreachable!()
                            };
                            self.scalar_ranges.insert(name.clone(), (0, modulus - 1));
                        } else if let Expr::Int(value) = value {
                            self.scalar_ranges.insert(name.clone(), (*value, *value));
                        } else {
                            self.scalar_ranges.remove(name);
                        }
                    }
                }
                Ok(Flow::Continues)
            }
            Stmt::FieldAssign { base, field, value } => {
                let name = match base {
                    Expr::Ident(n) => n,
                    _ => return Err(self.unsupported("field assignment to this value")),
                };
                let (id, vars) = self
                    .struct_locals
                    .get(name)
                    .map(|(i, v)| (*i, v.clone()))
                    .ok_or_else(|| {
                        NativeError::new(format!("native backend: unknown struct local `{name}`"))
                    })?;
                let idx = self.structs.field_index(id, field).ok_or_else(|| {
                    NativeError::new(format!("native backend: struct has no field `{field}`"))
                })?;
                let fk = self.structs.get(id).fields[idx].1;
                let (v, vk) = self.lower_expr(value)?.scalar()?;
                if vk != fk {
                    return Err(NativeError::new(format!(
                        "native backend: field `{field}` type mismatch"
                    )));
                }
                self.builder.def_var(vars[idx], v);
                Ok(Flow::Continues)
            }
            Stmt::IndexAssign { base, index, value } => {
                let (data, len, kind, owned) = match self.lower_expr(base)? {
                    NativeValue::Slice {
                        data,
                        len,
                        kind,
                        owned,
                        ..
                    } => (data, len, kind, owned),
                    _ => return Err(self.unsupported("index assignment to this value")),
                };
                let (idx, ik) = self.lower_expr(index)?.scalar()?;
                if ik != Kind::Int {
                    return Err(self.unsupported("non-integer index"));
                }
                let (element_kind, stride) = kind.slice_element().unwrap();
                let oob = self
                    .builder
                    .ins()
                    .icmp(IntCC::UnsignedGreaterThanOrEqual, idx, len);
                self.builder.ins().trapnz(oob, TrapCode::HEAP_OUT_OF_BOUNDS);
                let stride = self.builder.ins().iconst(types::I64, stride);
                let off = self.builder.ins().imul(idx, stride);
                let addr = self.builder.ins().iadd(data, off);
                if element_kind == Kind::String {
                    let NativeValue::Str {
                        ptr,
                        len: slen,
                        owned: value_owned,
                    } = self.lower_expr(value)?
                    else {
                        return Err(self.unsupported("assigning an element of the wrong type"));
                    };
                    // Clone before dropping the old element so `xs[i] = xs[i]`
                    // remains valid and cannot become a use-after-free.
                    let dup = self.heap_dup(ptr, slen);
                    let old = self
                        .builder
                        .ins()
                        .load(types::I64, MemFlagsData::new(), addr, 0);
                    self.emit_free(old);
                    self.builder.ins().store(MemFlagsData::new(), dup, addr, 0);
                    self.builder.ins().store(MemFlagsData::new(), slen, addr, 8);
                    if value_owned {
                        self.emit_free(ptr);
                    }
                    if owned {
                        self.emit_drop_string_slice(data, len);
                    }
                    return Ok(Flow::Continues);
                }
                let (v, vk) = self.lower_expr(value)?.scalar()?;
                if vk != element_kind {
                    return Err(self.unsupported("assigning an element of the wrong type"));
                }
                self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
                // Assigning into an owned temporary base is a discard; free it.
                if owned {
                    self.emit_free(data);
                }
                Ok(Flow::Continues)
            }
            Stmt::Expr(expr) => {
                // A discarded owned temporary (e.g. `a + b;` or `[1, 2, 3];`) must
                // be freed, matching the C backend's discarded-payload handling.
                match self.lower_expr(expr)? {
                    NativeValue::Str {
                        ptr, owned: true, ..
                    } => self.emit_free(ptr),
                    NativeValue::Slice {
                        data,
                        len,
                        kind,
                        owned: true,
                        ..
                    } => self.emit_drop_slice(kind, data, len),
                    _ => {}
                }
                Ok(Flow::Continues)
            }
            Stmt::Return(value) => {
                if self.function_name == "main" {
                    if value.is_some() {
                        return Err(self.unsupported("return values from main"));
                    }
                    self.free_owned_locals()?;
                    let zero = self.builder.ins().iconst(types::I32, 0);
                    self.builder.ins().return_(&[zero]);
                } else if let Some(expr) = value {
                    if self.function_ret == Kind::String {
                        // Compute the returned buffer first (this may move a local
                        // and clear its flag), then drop the remaining owned locals.
                        let (ptr, len) = self.lower_return_string(expr)?;
                        self.free_owned_locals()?;
                        self.builder.ins().return_(&[ptr, len]);
                    } else if self.function_ret.slice_element().is_some() {
                        let (data, len, cap) = self.lower_return_slice(expr)?;
                        self.free_owned_locals()?;
                        self.builder.ins().return_(&[data, len, cap]);
                    } else if let Kind::Struct(sid) = self.function_ret {
                        let fields = match self.lower_expr(expr)? {
                            NativeValue::Struct { id, fields } if id == sid => fields,
                            _ => {
                                return Err(NativeError::new(format!(
                                    "native backend: return type mismatch in `{}`",
                                    self.function_name
                                )))
                            }
                        };
                        self.free_owned_locals()?;
                        self.builder.ins().return_(&fields);
                    } else {
                        let (value, kind) = self.lower_expr(expr)?.scalar()?;
                        if kind != self.function_ret {
                            return Err(NativeError::new(format!(
                                "native backend: return type mismatch in `{}`",
                                self.function_name
                            )));
                        }
                        self.free_owned_locals()?;
                        self.builder.ins().return_(&[value]);
                    }
                } else if self.function_ret == Kind::Void {
                    self.free_owned_locals()?;
                    self.builder.ins().return_(&[]);
                } else {
                    return Err(NativeError::new(format!(
                        "native backend: missing return value in `{}`",
                        self.function_name
                    )));
                }
                Ok(Flow::Terminates)
            }
            Stmt::If {
                init,
                cond,
                then_block,
                else_block,
            } => {
                if init.is_some() {
                    return Err(self.unsupported("if initializers"));
                }
                let (cond, _) = self.expect_bool(cond)?;
                let then_bb = self.builder.create_block();
                let else_bb = self.builder.create_block();
                let merge_bb = self.builder.create_block();
                self.builder.ins().brif(cond, then_bb, &[], else_bb, &[]);

                // A branch that reaches the merge must leave string ownership
                // unchanged; per-path ownership divergence is not represented yet.
                let owned_before = self.owned_set();
                self.scalar_ranges.clear();
                self.builder.switch_to_block(then_bb);
                self.builder.seal_block(then_bb);
                let then_flow = self.lower_block(then_block)?;
                if then_flow == Flow::Continues {
                    if self.owned_set() != owned_before {
                        return Err(
                            self.unsupported("string ownership that changes inside a branch")
                        );
                    }
                    self.builder.ins().jump(merge_bb, &[]);
                }
                self.restore_owned(&owned_before);
                self.scalar_ranges.clear();

                self.builder.switch_to_block(else_bb);
                self.builder.seal_block(else_bb);
                let else_flow = if let Some(block) = else_block {
                    self.lower_block(block)?
                } else {
                    Flow::Continues
                };
                if else_flow == Flow::Continues {
                    if self.owned_set() != owned_before {
                        return Err(
                            self.unsupported("string ownership that changes inside a branch")
                        );
                    }
                    self.builder.ins().jump(merge_bb, &[]);
                }
                self.restore_owned(&owned_before);
                self.scalar_ranges.clear();
                self.builder.seal_block(merge_bb);
                if then_flow == Flow::Terminates && else_flow == Flow::Terminates {
                    Ok(Flow::Terminates)
                } else {
                    self.builder.switch_to_block(merge_bb);
                    Ok(Flow::Continues)
                }
            }
            Stmt::While { label, cond, body } => {
                if label.is_some() {
                    return Err(self.unsupported("labeled loops"));
                }
                if let Some(pattern) = vector_sum_pattern(cond, body) {
                    let scalar_kinds_match = self.local_kinds.get(pattern.index)
                        == Some(&Kind::Int)
                        && self.local_kinds.get(pattern.sum) == Some(&Kind::Int)
                        && self.local_kinds.get(pattern.bound) == Some(&Kind::Int);
                    if scalar_kinds_match && self.slice_locals.contains_key(pattern.slice) {
                        let bounds_proven = self.scalar_ranges.get(pattern.index) == Some(&(0, 0))
                            && self.filled_slices.get(pattern.slice).map(String::as_str)
                                == Some(pattern.bound);
                        return self.lower_vector_sum(pattern, cond, body, bounds_proven);
                    }
                }
                let unchecked_append = preallocated_append_loop(
                    cond,
                    body,
                    &self.preallocated_empty_slices,
                    &self.scalar_ranges,
                );
                let header = self.builder.create_block();
                let body_bb = self.builder.create_block();
                let exit = self.builder.create_block();
                self.builder.ins().jump(header, &[]);
                self.builder.switch_to_block(header);
                let (condition, _) = self.expect_bool(cond)?;
                self.builder.ins().brif(condition, body_bb, &[], exit, &[]);
                self.builder.switch_to_block(body_bb);
                self.builder.seal_block(body_bb);
                self.loops.push((exit, header));
                let owned_before = self.owned_set();
                self.unchecked_append_slice = unchecked_append.clone();
                let body_flow = self.lower_block(body)?;
                self.unchecked_append_slice = None;
                if let Some((slice, _)) = unchecked_append {
                    if let Some(bound) = self.preallocated_empty_slices.remove(&slice) {
                        self.filled_slices.insert(slice, bound);
                    }
                }
                // Each iteration must be ownership-balanced; loop-local heap
                // strings need per-iteration scope frees (not yet implemented).
                if self.owned_set() != owned_before {
                    return Err(self.unsupported("string ownership that changes inside a loop"));
                }
                self.loops.pop();
                if body_flow == Flow::Continues {
                    self.builder.ins().jump(header, &[]);
                }
                self.builder.seal_block(header);
                self.builder.seal_block(exit);
                self.builder.switch_to_block(exit);
                self.scalar_ranges.clear();
                Ok(Flow::Continues)
            }
            Stmt::Break(label) => {
                if label.is_some() {
                    return Err(self.unsupported("labeled break"));
                }
                let (exit, _) = self
                    .loops
                    .last()
                    .copied()
                    .ok_or_else(|| self.unsupported("break outside loop"))?;
                self.builder.ins().jump(exit, &[]);
                Ok(Flow::Terminates)
            }
            Stmt::Continue(label) => {
                if label.is_some() {
                    return Err(self.unsupported("labeled continue"));
                }
                let (_, header) = self
                    .loops
                    .last()
                    .copied()
                    .ok_or_else(|| self.unsupported("continue outside loop"))?;
                self.builder.ins().jump(header, &[]);
                Ok(Flow::Terminates)
            }
            Stmt::Unsafe { body } => self.lower_block(body),
            Stmt::For {
                label,
                binders,
                is_range: _,
                iter,
                body,
            } => {
                if label.is_some() {
                    return Err(self.unsupported("labeled loops"));
                }
                let flow = self.lower_for(binders, iter, body)?;
                self.scalar_ranges.clear();
                Ok(flow)
            }
            _ => Err(self.unsupported(stmt_feature(stmt))),
        }
    }

    fn lower_expr(&mut self, expr: &Expr) -> Result<NativeValue, NativeError> {
        match expr {
            Expr::Int(n) => Ok(NativeValue::Scalar(
                self.builder.ins().iconst(types::I64, *n),
                Kind::Int,
            )),
            Expr::Float(n) => Ok(NativeValue::Scalar(
                self.builder.ins().f64const(*n),
                Kind::Float,
            )),
            Expr::Bool(b) => Ok(NativeValue::Scalar(
                self.builder.ins().iconst(types::I8, i64::from(*b)),
                Kind::Bool,
            )),
            Expr::String(s) => {
                let (ptr, len) = self.string_view(s.as_bytes())?;
                Ok(NativeValue::Str {
                    ptr,
                    len,
                    owned: false,
                })
            }
            Expr::Ident(name) => {
                if let Some(&(ptr_var, len_var)) = self.str_locals.get(name) {
                    let ptr = self.builder.use_var(ptr_var);
                    let len = self.builder.use_var(len_var);
                    return Ok(NativeValue::Str {
                        ptr,
                        len,
                        owned: false,
                    });
                }
                if let Some(&(kind, data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                    let data = self.builder.use_var(data_var);
                    let len = self.builder.use_var(len_var);
                    let cap = self.builder.use_var(cap_var);
                    return Ok(NativeValue::Slice {
                        data,
                        len,
                        cap,
                        kind,
                        owned: false,
                    });
                }
                if let Some((id, vars)) = self.struct_locals.get(name) {
                    let id = *id;
                    let vars = vars.clone();
                    let fields = vars.iter().map(|&v| self.builder.use_var(v)).collect();
                    return Ok(NativeValue::Struct { id, fields });
                }
                let var = *self.locals.get(name).ok_or_else(|| {
                    NativeError::new(format!("native backend: unknown local `{name}`"))
                })?;
                Ok(NativeValue::Scalar(
                    self.builder.use_var(var),
                    self.local_kinds[name],
                ))
            }
            Expr::Array(elems) => self.lower_array_literal(elems),
            Expr::Make { ty, len, cap } => self.lower_make(ty, len.as_deref(), cap.as_deref()),
            Expr::Index { base, index } => self.lower_index(base, index),
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => self.lower_slice(base, low.as_deref(), high.as_deref(), max.as_deref()),
            Expr::Field { base, field } => self.lower_field(base, field),
            Expr::Match { scrutinee, arms } => self.lower_match(scrutinee, arms),
            Expr::StructLit {
                name,
                fields,
                update,
            } => self.lower_struct_lit(name, fields, update.as_deref()),
            Expr::Unary { op, expr } => {
                let (value, kind) = self.lower_expr(expr)?.scalar()?;
                let out = match (op, kind) {
                    (UnaryOp::Neg, Kind::Int) => self.builder.ins().ineg(value),
                    (UnaryOp::Neg, Kind::Float) => self.builder.ins().fneg(value),
                    (UnaryOp::Not, Kind::Bool) => self.builder.ins().bxor_imm(value, 1),
                    (UnaryOp::BitNot, Kind::Int) => self.builder.ins().bnot(value),
                    _ => return Err(self.unsupported("this unary operation")),
                };
                Ok(NativeValue::Scalar(out, kind))
            }
            Expr::Binary { op, left, right } => self.lower_binary(*op, left, right),
            Expr::Call { callee, args } => self.lower_call(callee, args),
            _ => Err(self.unsupported(expr_feature(expr))),
        }
    }

    fn lower_binary(
        &mut self,
        op: BinOp,
        left: &Expr,
        right: &Expr,
    ) -> Result<NativeValue, NativeError> {
        let lv = self.lower_expr(left)?;
        let rv = self.lower_expr(right)?;
        // String concatenation produces a fresh owned heap buffer. Owned temporary
        // operands are freed after their bytes are copied so nested concatenation
        // (`a + b + c`) leaks nothing.
        if let (NativeValue::Str { .. }, NativeValue::Str { .. }) = (&lv, &rv) {
            let (ap, al, ao) = match lv {
                NativeValue::Str { ptr, len, owned } => (ptr, len, owned),
                _ => unreachable!(),
            };
            let (bp, bl, bo) = match rv {
                NativeValue::Str { ptr, len, owned } => (ptr, len, owned),
                _ => unreachable!(),
            };
            match op {
                BinOp::Add => {
                    let (dp, dl) = self.str_concat(ap, al, bp, bl);
                    if ao {
                        self.emit_free(ap);
                    }
                    if bo {
                        self.emit_free(bp);
                    }
                    return Ok(NativeValue::Str {
                        ptr: dp,
                        len: dl,
                        owned: true,
                    });
                }
                BinOp::Eq | BinOp::Ne => {
                    let eq = self.str_eq(ap, al, bp, bl);
                    // Free owned temporary operands after the comparison reads them.
                    if ao {
                        self.emit_free(ap);
                    }
                    if bo {
                        self.emit_free(bp);
                    }
                    let result = if op == BinOp::Ne {
                        self.builder.ins().bxor_imm(eq, 1)
                    } else {
                        eq
                    };
                    return Ok(NativeValue::Scalar(result, Kind::Bool));
                }
                _ => return Err(self.unsupported("this string operation")),
            }
        }
        let (a, ak) = lv.scalar()?;
        let (b, bk) = rv.scalar()?;
        if ak != bk {
            return Err(NativeError::new(
                "native backend: binary operands have different types",
            ));
        }
        let (value, kind) = match (ak, op) {
            (Kind::Int, BinOp::Add) => (self.builder.ins().iadd(a, b), Kind::Int),
            (Kind::Int, BinOp::Sub) => (self.builder.ins().isub(a, b), Kind::Int),
            (Kind::Int, BinOp::Mul) => (self.builder.ins().imul(a, b), Kind::Int),
            (Kind::Int, BinOp::Div) => (self.builder.ins().sdiv(a, b), Kind::Int),
            (Kind::Int, BinOp::Mod) if self.use_unsigned_mod => {
                (self.builder.ins().urem(a, b), Kind::Int)
            }
            (Kind::Int, BinOp::Mod) => (self.builder.ins().srem(a, b), Kind::Int),
            (Kind::Int, BinOp::BitAnd) => (self.builder.ins().band(a, b), Kind::Int),
            (Kind::Int, BinOp::BitOr) => (self.builder.ins().bor(a, b), Kind::Int),
            (Kind::Int, BinOp::BitXor) => (self.builder.ins().bxor(a, b), Kind::Int),
            (Kind::Int, BinOp::BitClear) => {
                let not_b = self.builder.ins().bnot(b);
                (self.builder.ins().band(a, not_b), Kind::Int)
            }
            (Kind::Int, BinOp::Shl) => (self.builder.ins().ishl(a, b), Kind::Int),
            (Kind::Int, BinOp::Shr) => (self.builder.ins().sshr(a, b), Kind::Int),
            (Kind::Float, BinOp::Add) => (self.builder.ins().fadd(a, b), Kind::Float),
            (Kind::Float, BinOp::Sub) => (self.builder.ins().fsub(a, b), Kind::Float),
            (Kind::Float, BinOp::Mul) => (self.builder.ins().fmul(a, b), Kind::Float),
            (Kind::Float, BinOp::Div) => (self.builder.ins().fdiv(a, b), Kind::Float),
            (Kind::Float, BinOp::Eq) => (self.builder.ins().fcmp(FloatCC::Equal, a, b), Kind::Bool),
            (Kind::Float, BinOp::Ne) => {
                (self.builder.ins().fcmp(FloatCC::NotEqual, a, b), Kind::Bool)
            }
            (Kind::Float, BinOp::Lt) => {
                (self.builder.ins().fcmp(FloatCC::LessThan, a, b), Kind::Bool)
            }
            (Kind::Float, BinOp::Le) => (
                self.builder.ins().fcmp(FloatCC::LessThanOrEqual, a, b),
                Kind::Bool,
            ),
            (Kind::Float, BinOp::Gt) => (
                self.builder.ins().fcmp(FloatCC::GreaterThan, a, b),
                Kind::Bool,
            ),
            (Kind::Float, BinOp::Ge) => (
                self.builder.ins().fcmp(FloatCC::GreaterThanOrEqual, a, b),
                Kind::Bool,
            ),
            (
                Kind::Int,
                cmp @ (BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge),
            ) => (self.builder.ins().icmp(int_cc(cmp), a, b), Kind::Bool),
            (Kind::Bool, BinOp::Eq) => (self.builder.ins().icmp(IntCC::Equal, a, b), Kind::Bool),
            (Kind::Bool, BinOp::Ne) => (self.builder.ins().icmp(IntCC::NotEqual, a, b), Kind::Bool),
            (Kind::Bool, BinOp::And) => (self.builder.ins().band(a, b), Kind::Bool),
            (Kind::Bool, BinOp::Or) => (self.builder.ins().bor(a, b), Kind::Bool),
            _ => return Err(self.unsupported("this binary operation")),
        };
        Ok(NativeValue::Scalar(value, kind))
    }

    fn lower_call(&mut self, callee: &Expr, args: &[Expr]) -> Result<NativeValue, NativeError> {
        let Expr::Ident(name) = callee else {
            return Err(self.unsupported("indirect calls"));
        };
        if name == "int" || name == "int64" {
            if args.len() != 1 {
                return Err(NativeError::new(format!(
                    "native backend: `{name}` expects one argument"
                )));
            }
            let (value, kind) = self.lower_expr(&args[0])?.scalar()?;
            return match kind {
                Kind::Int => Ok(NativeValue::Scalar(value, Kind::Int)),
                Kind::Float => Ok(NativeValue::Scalar(
                    self.builder.ins().fcvt_to_sint(types::I64, value),
                    Kind::Int,
                )),
                _ => Err(self.unsupported("this numeric conversion")),
            };
        }
        if name == "float" || name == "float64" {
            if args.len() != 1 {
                return Err(NativeError::new(format!(
                    "native backend: `{name}` expects one argument"
                )));
            }
            let (value, kind) = self.lower_expr(&args[0])?.scalar()?;
            return match kind {
                Kind::Float => Ok(NativeValue::Scalar(value, Kind::Float)),
                Kind::Int => Ok(NativeValue::Scalar(
                    self.builder.ins().fcvt_from_sint(types::F64, value),
                    Kind::Float,
                )),
                _ => Err(self.unsupported("this numeric conversion")),
            };
        }
        if name == "len" {
            if args.len() != 1 {
                return Err(NativeError::new(
                    "native backend: `len` expects one argument",
                ));
            }
            // `len` borrows; an owned temporary argument is freed after reading.
            match self.lower_expr(&args[0])? {
                NativeValue::Slice {
                    data,
                    len,
                    kind,
                    owned,
                    ..
                } => {
                    if owned {
                        self.emit_drop_slice(kind, data, len);
                    }
                    return Ok(NativeValue::Scalar(len, Kind::Int));
                }
                NativeValue::Str { ptr, len, owned } => {
                    if owned {
                        self.emit_free(ptr);
                    }
                    return Ok(NativeValue::Scalar(len, Kind::Int));
                }
                _ => {
                    return Err(self.unsupported("len of this value"));
                }
            }
        }
        if name == "cap" {
            if args.len() != 1 {
                return Err(NativeError::new(
                    "native backend: `cap` expects one argument",
                ));
            }
            match self.lower_expr(&args[0])? {
                NativeValue::Slice {
                    data,
                    len,
                    cap,
                    kind,
                    owned,
                    ..
                } => {
                    if owned {
                        self.emit_drop_slice(kind, data, len);
                    }
                    return Ok(NativeValue::Scalar(cap, Kind::Int));
                }
                _ => return Err(self.unsupported("cap of this value")),
            }
        }
        if name == "append" {
            return self.lower_append(args);
        }
        if matches!(
            name.as_str(),
            "print" | "print_int" | "print_int64" | "print_float"
        ) {
            if args.len() != 1 {
                return Err(NativeError::new(format!(
                    "native backend: `{name}` expects one argument"
                )));
            }
            match self.lower_expr(&args[0])? {
                NativeValue::Str { ptr, len, owned } => {
                    // `print` borrows its argument; an owned temporary
                    // (e.g. `print(a + b)`) is freed once it has been written.
                    self.write_string_value(ptr, len)?;
                    if owned {
                        self.emit_free(ptr);
                    }
                }
                NativeValue::Scalar(value, Kind::Int) => self.call_print_int(value),
                NativeValue::Scalar(value, Kind::Bool) => {
                    // Keep booleans allocation-free by selecting static strings.
                    let true_bb = self.builder.create_block();
                    let false_bb = self.builder.create_block();
                    let done = self.builder.create_block();
                    self.builder.ins().brif(value, true_bb, &[], false_bb, &[]);
                    self.builder.switch_to_block(true_bb);
                    self.builder.seal_block(true_bb);
                    self.call_write_string("true")?;
                    self.builder.ins().jump(done, &[]);
                    self.builder.switch_to_block(false_bb);
                    self.builder.seal_block(false_bb);
                    self.call_write_string("false")?;
                    self.builder.ins().jump(done, &[]);
                    self.builder.seal_block(done);
                    self.builder.switch_to_block(done);
                }
                NativeValue::Scalar(value, Kind::Float) => self.call_print_float(value),
                NativeValue::Slice { .. } => return Err(self.unsupported("printing a slice")),
                NativeValue::Struct { .. } => return Err(self.unsupported("printing a struct")),
                NativeValue::Scalar(_, _) => return Err(self.unsupported("printing this value")),
            }
            let zero = self.builder.ins().iconst(types::I64, 0);
            return Ok(NativeValue::Scalar(zero, Kind::Int));
        }

        let meta = self
            .funcs
            .get(name)
            .ok_or_else(|| {
                NativeError::new(format!(
                    "native backend: call to unsupported function `{name}`"
                ))
            })?
            .clone();
        if args.len() != meta.params.len() {
            return Err(NativeError::new(format!(
                "native backend: `{name}` expects {} arguments, got {}",
                meta.params.len(),
                args.len()
            )));
        }
        let mut values = Vec::with_capacity(args.len());
        // String arguments are passed as borrows: the callee never frees them.
        // An owned temporary argument (e.g. `f(a + b)`) is still owned by this
        // frame, so it is freed after the call returns.
        let mut arg_temps: Vec<Value> = Vec::new();
        let mut arg_string_slice_temps: Vec<(Value, Value)> = Vec::new();
        for (arg, expected) in args.iter().zip(&meta.params) {
            match *expected {
                Kind::String => match self.lower_expr(arg)? {
                    NativeValue::Str { ptr, len, owned } => {
                        values.push(ptr);
                        values.push(len);
                        if owned {
                            arg_temps.push(ptr);
                        }
                    }
                    _ => {
                        return Err(NativeError::new(format!(
                            "native backend: argument type mismatch calling `{name}`"
                        )));
                    }
                },
                Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice => {
                    match self.lower_expr(arg)? {
                        NativeValue::Slice {
                            data,
                            len,
                            cap,
                            kind,
                            owned,
                        } if kind == *expected => {
                            values.push(data);
                            values.push(len);
                            values.push(cap);
                            if owned {
                                if kind == Kind::StringSlice {
                                    arg_string_slice_temps.push((data, len));
                                } else {
                                    arg_temps.push(data);
                                }
                            }
                        }
                        _ => {
                            return Err(NativeError::new(format!(
                                "native backend: argument type mismatch calling `{name}`"
                            )));
                        }
                    }
                }
                Kind::Struct(sid) => match self.lower_expr(arg)? {
                    NativeValue::Struct { id, fields } if id == sid => {
                        for v in fields {
                            values.push(v);
                        }
                    }
                    _ => {
                        return Err(NativeError::new(format!(
                            "native backend: argument type mismatch calling `{name}`"
                        )));
                    }
                },
                _ => {
                    let (value, actual) = self.lower_expr(arg)?.scalar()?;
                    if actual != *expected {
                        return Err(NativeError::new(format!(
                            "native backend: argument type mismatch calling `{name}`"
                        )));
                    }
                    values.push(value);
                }
            }
        }
        let func_ref = self.module.declare_func_in_func(meta.id, self.builder.func);
        let call = self.builder.ins().call(func_ref, &values);
        let result = if meta.ret == Kind::Void {
            let zero = self.builder.ins().iconst(types::I64, 0);
            NativeValue::Scalar(zero, Kind::Int)
        } else if meta.ret == Kind::String {
            // A function returning a string always returns an owned heap buffer
            // (see `lower_return_string`), so the caller owns the result.
            let ptr = self.builder.inst_results(call)[0];
            let len = self.builder.inst_results(call)[1];
            NativeValue::Str {
                ptr,
                len,
                owned: true,
            }
        } else if meta.ret.slice_element().is_some() {
            // Slice returns are always owned (see `lower_return_slice`).
            let data = self.builder.inst_results(call)[0];
            let len = self.builder.inst_results(call)[1];
            let cap = self.builder.inst_results(call)[2];
            NativeValue::Slice {
                data,
                len,
                cap,
                kind: meta.ret,
                owned: true,
            }
        } else if let Kind::Struct(id) = meta.ret {
            let n = self.structs.get(id).fields.len();
            let fields = (0..n).map(|i| self.builder.inst_results(call)[i]).collect();
            NativeValue::Struct { id, fields }
        } else {
            NativeValue::Scalar(self.builder.inst_results(call)[0], meta.ret)
        };
        for ptr in arg_temps {
            self.emit_free(ptr);
        }
        for (data, len) in arg_string_slice_temps {
            self.emit_drop_string_slice(data, len);
        }
        Ok(result)
    }

    fn expect_bool(&mut self, expr: &Expr) -> Result<(Value, Kind), NativeError> {
        let (value, kind) = self.lower_expr(expr)?.scalar()?;
        if kind != Kind::Bool {
            return Err(NativeError::new("native backend: condition must be bool"));
        }
        Ok((value, kind))
    }

    /// Materialize static string bytes as a non-owned `(data, len)` view.
    fn string_view(&mut self, bytes: &[u8]) -> Result<(Value, Value), NativeError> {
        let ptr = self.string_data(bytes, "str")?;
        let len = self.builder.ins().iconst(types::I64, bytes.len() as i64);
        Ok((ptr, len))
    }

    fn call_write_string(&mut self, text: &str) -> Result<(), NativeError> {
        let (ptr, len) = self.string_view(text.as_bytes())?;
        self.write_string_value(ptr, len)
    }

    /// Write a runtime string value to stdout followed by a newline, matching
    /// `mako_print_str`. Uses `write(2)` directly; no allocation for the view.
    fn write_string_value(&mut self, ptr: Value, len: Value) -> Result<(), NativeError> {
        let func = self
            .module
            .declare_func_in_func(self.write, self.builder.func);
        let fd = self.builder.ins().iconst(types::I32, 1);
        self.builder.ins().call(func, &[fd, ptr, len]);
        let newline = self.string_data(b"\n", "newline")?;
        let one = self.builder.ins().iconst(types::I64, 1);
        self.builder.ins().call(func, &[fd, newline, one]);
        Ok(())
    }

    fn call_print_int(&mut self, value: Value) {
        let func = self
            .module
            .declare_func_in_func(self.print_int, self.builder.func);
        self.builder.ins().call(func, &[value]);
    }

    fn call_print_float(&mut self, value: Value) {
        let func = self
            .module
            .declare_func_in_func(self.print_float, self.builder.func);
        self.builder.ins().call(func, &[value]);
    }

    // ---- Heap string ownership (increment 2) ----

    fn emit_malloc(&mut self, size: Value) -> Value {
        let f = self
            .module
            .declare_func_in_func(self.libc.malloc, self.builder.func);
        let call = self.builder.ins().call(f, &[size]);
        self.builder.inst_results(call)[0]
    }

    fn emit_memcpy(&mut self, dst: Value, src: Value, n: Value) {
        let f = self
            .module
            .declare_func_in_func(self.libc.memcpy, self.builder.func);
        self.builder.ins().call(f, &[dst, src, n]);
    }

    fn emit_free(&mut self, ptr: Value) {
        let f = self
            .module
            .declare_func_in_func(self.libc.free, self.builder.func);
        self.builder.ins().call(f, &[ptr]);
    }

    fn emit_drop_slice(&mut self, kind: Kind, data: Value, len: Value) {
        if kind == Kind::StringSlice {
            self.emit_drop_string_slice(data, len);
        } else {
            self.emit_free(data);
        }
    }

    /// Allocate `len + 1` bytes, copy `len` from `src`, and NUL-terminate — the
    /// runtime `MakoString` layout. Returns the fresh owned data pointer.
    fn heap_dup(&mut self, src: Value, len: Value) -> Value {
        let one = self.builder.ins().iconst(types::I64, 1);
        let size = self.builder.ins().iadd(len, one);
        let dst = self.emit_malloc(size);
        self.emit_memcpy(dst, src, len);
        let end = self.builder.ins().iadd(dst, len);
        let nul = self.builder.ins().iconst(types::I8, 0);
        self.builder.ins().store(MemFlagsData::new(), nul, end, 0);
        dst
    }

    /// Concatenate two string values into a fresh owned heap buffer.
    fn str_concat(&mut self, ap: Value, al: Value, bp: Value, bl: Value) -> (Value, Value) {
        let total = self.builder.ins().iadd(al, bl);
        let one = self.builder.ins().iconst(types::I64, 1);
        let size = self.builder.ins().iadd(total, one);
        let dst = self.emit_malloc(size);
        self.emit_memcpy(dst, ap, al);
        let mid = self.builder.ins().iadd(dst, al);
        self.emit_memcpy(mid, bp, bl);
        let end = self.builder.ins().iadd(dst, total);
        let nul = self.builder.ins().iconst(types::I8, 0);
        self.builder.ins().store(MemFlagsData::new(), nul, end, 0);
        (dst, total)
    }

    /// String equality (`mako_str_eq`): equal lengths and `memcmp == 0`. `memcmp`
    /// is only reached when lengths match, so it never over-reads. Returns i8 bool.
    fn str_eq(&mut self, ad: Value, al: Value, bd: Value, bl: Value) -> Value {
        let len_eq = self.builder.ins().icmp(IntCC::Equal, al, bl);
        let memcmp_bb = self.builder.create_block();
        let merge = self.builder.create_block();
        self.builder.append_block_param(merge, types::I8);
        let false_v = self.builder.ins().iconst(types::I8, 0);
        self.builder
            .ins()
            .brif(len_eq, memcmp_bb, &[], merge, &[BlockArg::from(false_v)]);
        self.builder.switch_to_block(memcmp_bb);
        self.builder.seal_block(memcmp_bb);
        let f = self
            .module
            .declare_func_in_func(self.libc.memcmp, self.builder.func);
        let call = self.builder.ins().call(f, &[ad, bd, al]);
        let c = self.builder.inst_results(call)[0];
        let zero = self.builder.ins().iconst(types::I32, 0);
        let eq = self.builder.ins().icmp(IntCC::Equal, c, zero);
        self.builder.ins().jump(merge, &[BlockArg::from(eq)]);
        self.builder.seal_block(merge);
        self.builder.switch_to_block(merge);
        self.builder.block_params(merge)[0]
    }

    /// Turn any string value into an owned heap `(data, len)`. Owned temporaries
    /// pass through; non-owned views/borrows are cloned so the receiver owns an
    /// independent buffer.
    fn make_owned(&mut self, value: NativeValue) -> Result<(Value, Value), NativeError> {
        match value {
            NativeValue::Str {
                ptr,
                len,
                owned: true,
            } => Ok((ptr, len)),
            NativeValue::Str {
                ptr,
                len,
                owned: false,
            } => {
                let dup = self.heap_dup(ptr, len);
                Ok((dup, len))
            }
            _ => Err(NativeError::new(
                "native backend: expected a string value here",
            )),
        }
    }

    /// Produce the owned heap `(data, len)` a function returns. A returned local
    /// that owns its buffer is moved out (its flag is cleared so it is not also
    /// freed on the way out); any other string is cloned to an owned buffer.
    fn lower_return_string(&mut self, expr: &Expr) -> Result<(Value, Value), NativeError> {
        if let Expr::Ident(name) = expr {
            if let Some(&(ptr_var, len_var)) = self.str_locals.get(name) {
                let ptr = self.builder.use_var(ptr_var);
                let len = self.builder.use_var(len_var);
                if *self.heap_owned.get(name).unwrap_or(&false) {
                    self.heap_owned.insert(name.clone(), false); // moved out
                    return Ok((ptr, len));
                }
                let dup = self.heap_dup(ptr, len);
                return Ok((dup, len));
            }
        }
        let value = self.lower_expr(expr)?;
        self.make_owned(value)
    }

    fn emit_drop_string_slice(&mut self, data: Value, len: Value) {
        let index = self.builder.declare_var(types::I64);
        let zero = self.builder.ins().iconst(types::I64, 0);
        self.builder.def_var(index, zero);
        let header = self.builder.create_block();
        let body = self.builder.create_block();
        let exit = self.builder.create_block();
        self.builder.ins().jump(header, &[]);
        self.builder.switch_to_block(header);
        let i = self.builder.use_var(index);
        let more = self.builder.ins().icmp(IntCC::UnsignedLessThan, i, len);
        self.builder.ins().brif(more, body, &[], exit, &[]);
        self.builder.switch_to_block(body);
        self.builder.seal_block(body);
        let sixteen = self.builder.ins().iconst(types::I64, 16);
        let off = self.builder.ins().imul(i, sixteen);
        let addr = self.builder.ins().iadd(data, off);
        let ptr = self
            .builder
            .ins()
            .load(types::I64, MemFlagsData::new(), addr, 0);
        self.emit_free(ptr);
        let next = self.builder.ins().iadd_imm(i, 1);
        self.builder.def_var(index, next);
        self.builder.ins().jump(header, &[]);
        self.builder.seal_block(header);
        self.builder.seal_block(exit);
        self.builder.switch_to_block(exit);
        self.emit_free(data);
    }

    fn clone_string_slice(&mut self, data: Value, len: Value) -> (Value, Value) {
        let sixteen = self.builder.ins().iconst(types::I64, 16);
        let bytes = self.builder.ins().imul(len, sixteen);
        let nd = self.emit_malloc(bytes);
        let index = self.builder.declare_var(types::I64);
        let zero = self.builder.ins().iconst(types::I64, 0);
        self.builder.def_var(index, zero);
        let header = self.builder.create_block();
        let body = self.builder.create_block();
        let exit = self.builder.create_block();
        self.builder.ins().jump(header, &[]);
        self.builder.switch_to_block(header);
        let i = self.builder.use_var(index);
        let more = self.builder.ins().icmp(IntCC::UnsignedLessThan, i, len);
        self.builder.ins().brif(more, body, &[], exit, &[]);
        self.builder.switch_to_block(body);
        self.builder.seal_block(body);
        let off = self.builder.ins().imul(i, sixteen);
        let src = self.builder.ins().iadd(data, off);
        let dst = self.builder.ins().iadd(nd, off);
        let ptr = self
            .builder
            .ins()
            .load(types::I64, MemFlagsData::new(), src, 0);
        let slen = self
            .builder
            .ins()
            .load(types::I64, MemFlagsData::new(), src, 8);
        let dup = self.heap_dup(ptr, slen);
        self.builder.ins().store(MemFlagsData::new(), dup, dst, 0);
        self.builder.ins().store(MemFlagsData::new(), slen, dst, 8);
        let next = self.builder.ins().iadd_imm(i, 1);
        self.builder.def_var(index, next);
        self.builder.ins().jump(header, &[]);
        self.builder.seal_block(header);
        self.builder.seal_block(exit);
        self.builder.switch_to_block(exit);
        (nd, len)
    }

    /// Clone a slice's `len` elements into a fresh owned buffer. `cap == len`.
    fn slice_clone(&mut self, kind: Kind, data: Value, len: Value) -> (Value, Value) {
        if kind == Kind::StringSlice {
            return self.clone_string_slice(data, len);
        }
        let (_, stride) = kind.slice_element().expect("slice kind");
        let stride = self.builder.ins().iconst(types::I64, stride);
        let bytes = self.builder.ins().imul(len, stride);
        let nd = self.emit_malloc(bytes);
        self.emit_memcpy(nd, data, bytes);
        (nd, len)
    }

    /// Produce the owned `(data, len, cap)` a function returns for `[]int`. A
    /// returned local that owns its buffer is moved out; any other slice is cloned.
    fn lower_return_slice(&mut self, expr: &Expr) -> Result<(Value, Value, Value), NativeError> {
        if let Expr::Ident(name) = expr {
            if let Some(&(kind, data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                if kind != self.function_ret {
                    return Err(NativeError::new(
                        "native backend: slice return type mismatch",
                    ));
                }
                let data = self.builder.use_var(data_var);
                let len = self.builder.use_var(len_var);
                let cap = self.builder.use_var(cap_var);
                if *self.heap_owned.get(name).unwrap_or(&false) {
                    self.heap_owned.insert(name.clone(), false); // moved out
                    return Ok((data, len, cap));
                }
                let (nd, ncap) = self.slice_clone(kind, data, len);
                return Ok((nd, len, ncap));
            }
        }
        match self.lower_expr(expr)? {
            NativeValue::Slice {
                data,
                len,
                cap,
                kind,
                owned: true,
            } if kind == self.function_ret => Ok((data, len, cap)),
            NativeValue::Slice {
                data,
                len,
                kind,
                owned: false,
                ..
            } if kind == self.function_ret => {
                let (nd, ncap) = self.slice_clone(kind, data, len);
                Ok((nd, len, ncap))
            }
            _ => Err(NativeError::new(
                "native backend: expected a slice value here",
            )),
        }
    }

    /// Lower `append(slice, elem)` for `[]int`. The slice is consumed (moved): a
    /// bare-identifier local transfers its buffer and clears its ownership flag.
    fn consume_slice_operand(
        &mut self,
        expr: &Expr,
    ) -> Result<(Kind, Value, Value, Value, bool), NativeError> {
        if let Expr::Ident(name) = expr {
            if let Some(&(kind, dv, lv, cv)) = self.slice_locals.get(name) {
                let data = self.builder.use_var(dv);
                let len = self.builder.use_var(lv);
                let cap = self.builder.use_var(cv);
                let owned = self.heap_owned.get(name).copied().unwrap_or(false);
                self.heap_owned.insert(name.clone(), false);
                return Ok((kind, data, len, cap, owned));
            }
        }
        match self.lower_expr(expr)? {
            NativeValue::Slice {
                data,
                len,
                cap,
                kind,
                owned,
            } => Ok((kind, data, len, cap, owned)),
            _ => Err(NativeError::new("native backend: `append` expects a slice")),
        }
    }

    fn lower_append(&mut self, args: &[Expr]) -> Result<NativeValue, NativeError> {
        if args.len() != 2 {
            return Err(NativeError::new(
                "native backend: `append` expects two arguments",
            ));
        }
        let (kind, data, len, cap, owned) = self.consume_slice_operand(&args[0])?;
        if kind == Kind::StringSlice {
            return self.lower_append_string(data, len, cap, owned, &args[1]);
        }
        let (v, vk) = self.lower_expr(&args[1])?.scalar()?;
        let (element_kind, stride_bytes) = kind.slice_element().unwrap();
        if vk != element_kind {
            return Err(self.unsupported("appending an element of the wrong type"));
        }
        let stride = self.builder.ins().iconst(types::I64, stride_bytes);
        let one = self.builder.ins().iconst(types::I64, 1);

        let proven_index = if owned {
            match (&args[0], &self.unchecked_append_slice) {
                (Expr::Ident(name), Some((slice, index))) if name == slice => Some(index.clone()),
                _ => None,
            }
        } else {
            None
        };
        if let Some(index) = proven_index {
            let index = self.builder.use_var(self.locals[&index]);
            let off = self.builder.ins().imul(index, stride);
            let addr = self.builder.ins().iadd(data, off);
            self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
            let newlen = self.builder.ins().iadd(index, one);
            return Ok(NativeValue::Slice {
                data,
                len: newlen,
                cap,
                kind,
                owned: true,
            });
        }

        if !owned {
            // Borrowed/view source: never mutate or free it — copy into a fresh
            // buffer sized for one more element.
            let newcap = self.builder.ins().iadd(len, one);
            let bytes = self.builder.ins().imul(newcap, stride);
            let nd = self.emit_malloc(bytes);
            let copy_bytes = self.builder.ins().imul(len, stride);
            self.emit_memcpy(nd, data, copy_bytes);
            let off = self.builder.ins().imul(len, stride);
            let addr = self.builder.ins().iadd(nd, off);
            self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
            let newlen = self.builder.ins().iadd(len, one);
            return Ok(NativeValue::Slice {
                data: nd,
                len: newlen,
                cap: newcap,
                kind,
                owned: true,
            });
        }

        // Owned source: store in place when `len < cap`, otherwise reallocate
        // (2x growth, min 4) and free the old buffer. The merge block carries the
        // resulting `(data, len, cap)`.
        let inplace_bb = self.builder.create_block();
        let grow_bb = self.builder.create_block();
        let done_bb = self.builder.create_block();
        self.builder.append_block_param(done_bb, types::I64);
        self.builder.append_block_param(done_bb, types::I64);
        self.builder.append_block_param(done_bb, types::I64);
        let room = self.builder.ins().icmp(IntCC::UnsignedLessThan, len, cap);
        self.builder.ins().brif(room, inplace_bb, &[], grow_bb, &[]);

        self.builder.switch_to_block(inplace_bb);
        self.builder.seal_block(inplace_bb);
        let off = self.builder.ins().imul(len, stride);
        let addr = self.builder.ins().iadd(data, off);
        self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
        let newlen = self.builder.ins().iadd(len, one);
        self.builder.ins().jump(
            done_bb,
            &[
                BlockArg::from(data),
                BlockArg::from(newlen),
                BlockArg::from(cap),
            ],
        );

        self.builder.switch_to_block(grow_bb);
        self.builder.seal_block(grow_bb);
        let four = self.builder.ins().iconst(types::I64, 4);
        let twoc = self.builder.ins().iconst(types::I64, 2);
        let two_cap = self.builder.ins().imul(cap, twoc);
        let is_zero = self.builder.ins().icmp_imm(IntCC::Equal, cap, 0);
        let newcap = self.builder.ins().select(is_zero, four, two_cap);
        let bytes = self.builder.ins().imul(newcap, stride);
        let nd = self.emit_malloc(bytes);
        let copy_bytes = self.builder.ins().imul(len, stride);
        self.emit_memcpy(nd, data, copy_bytes);
        let goff = self.builder.ins().imul(len, stride);
        let gaddr = self.builder.ins().iadd(nd, goff);
        self.builder.ins().store(MemFlagsData::new(), v, gaddr, 0);
        self.emit_free(data);
        let glen = self.builder.ins().iadd(len, one);
        self.builder.ins().jump(
            done_bb,
            &[
                BlockArg::from(nd),
                BlockArg::from(glen),
                BlockArg::from(newcap),
            ],
        );

        self.builder.seal_block(done_bb);
        self.builder.switch_to_block(done_bb);
        let rd = self.builder.block_params(done_bb)[0];
        let rl = self.builder.block_params(done_bb)[1];
        let rc = self.builder.block_params(done_bb)[2];
        Ok(NativeValue::Slice {
            data: rd,
            len: rl,
            cap: rc,
            kind,
            owned: true,
        })
    }

    fn lower_append_string(
        &mut self,
        data: Value,
        len: Value,
        cap: Value,
        owned: bool,
        element: &Expr,
    ) -> Result<NativeValue, NativeError> {
        let NativeValue::Str {
            ptr,
            len: slen,
            owned: value_owned,
        } = self.lower_expr(element)?
        else {
            return Err(self.unsupported("appending an element of the wrong type"));
        };
        let dup = self.heap_dup(ptr, slen);
        if value_owned {
            self.emit_free(ptr);
        }
        let one = self.builder.ins().iconst(types::I64, 1);
        let sixteen = self.builder.ins().iconst(types::I64, 16);
        if !owned {
            let newcap = self.builder.ins().iadd(len, one);
            let bytes = self.builder.ins().imul(newcap, sixteen);
            let nd = self.emit_malloc(bytes);
            // A borrowed view aliases another owner's element strings. Deep-clone
            // them before creating an owner that will recursively drop elements.
            let (cloned, _) = self.clone_string_slice(data, len);
            let copy_bytes = self.builder.ins().imul(len, sixteen);
            self.emit_memcpy(nd, cloned, copy_bytes);
            self.emit_free(cloned);
            let off = self.builder.ins().imul(len, sixteen);
            let addr = self.builder.ins().iadd(nd, off);
            self.builder.ins().store(MemFlagsData::new(), dup, addr, 0);
            self.builder.ins().store(MemFlagsData::new(), slen, addr, 8);
            let newlen = self.builder.ins().iadd(len, one);
            return Ok(NativeValue::Slice {
                data: nd,
                len: newlen,
                cap: newcap,
                kind: Kind::StringSlice,
                owned: true,
            });
        }

        let inplace = self.builder.create_block();
        let grow = self.builder.create_block();
        let done = self.builder.create_block();
        for _ in 0..3 {
            self.builder.append_block_param(done, types::I64);
        }
        let room = self.builder.ins().icmp(IntCC::UnsignedLessThan, len, cap);
        self.builder.ins().brif(room, inplace, &[], grow, &[]);
        self.builder.switch_to_block(inplace);
        self.builder.seal_block(inplace);
        let off = self.builder.ins().imul(len, sixteen);
        let addr = self.builder.ins().iadd(data, off);
        self.builder.ins().store(MemFlagsData::new(), dup, addr, 0);
        self.builder.ins().store(MemFlagsData::new(), slen, addr, 8);
        let newlen = self.builder.ins().iadd(len, one);
        self.builder
            .ins()
            .jump(done, &[data.into(), newlen.into(), cap.into()]);

        self.builder.switch_to_block(grow);
        self.builder.seal_block(grow);
        let four = self.builder.ins().iconst(types::I64, 4);
        let two = self.builder.ins().iconst(types::I64, 2);
        let doubled = self.builder.ins().imul(cap, two);
        let empty = self.builder.ins().icmp_imm(IntCC::Equal, cap, 0);
        let newcap = self.builder.ins().select(empty, four, doubled);
        let bytes = self.builder.ins().imul(newcap, sixteen);
        let nd = self.emit_malloc(bytes);
        let copy_bytes = self.builder.ins().imul(len, sixteen);
        self.emit_memcpy(nd, data, copy_bytes);
        let goff = self.builder.ins().imul(len, sixteen);
        let gaddr = self.builder.ins().iadd(nd, goff);
        self.builder.ins().store(MemFlagsData::new(), dup, gaddr, 0);
        self.builder
            .ins()
            .store(MemFlagsData::new(), slen, gaddr, 8);
        // Elements moved with their headers; only the obsolete outer allocation
        // is released here.
        self.emit_free(data);
        let glen = self.builder.ins().iadd(len, one);
        self.builder
            .ins()
            .jump(done, &[nd.into(), glen.into(), newcap.into()]);

        self.builder.seal_block(done);
        self.builder.switch_to_block(done);
        Ok(NativeValue::Slice {
            data: self.builder.block_params(done)[0],
            len: self.builder.block_params(done)[1],
            cap: self.builder.block_params(done)[2],
            kind: Kind::StringSlice,
            owned: true,
        })
    }

    fn emit_calloc(&mut self, count: Value, size: Value) -> Value {
        let f = self
            .module
            .declare_func_in_func(self.libc.calloc, self.builder.func);
        let call = self.builder.ins().call(f, &[count, size]);
        self.builder.inst_results(call)[0]
    }

    /// Free the heap buffer owned by a local, dispatching on its kind
    /// (string data pointer vs slice data pointer).
    fn emit_drop_local(&mut self, name: &str) {
        match self.local_kinds.get(name) {
            Some(Kind::String) => {
                let (ptr_var, _) = self.str_locals[name];
                let ptr = self.builder.use_var(ptr_var);
                self.emit_free(ptr);
            }
            Some(
                kind @ (Kind::IntSlice | Kind::FloatSlice | Kind::BoolSlice | Kind::StringSlice),
            ) => {
                let (_, data_var, len_var, _) = self.slice_locals[name];
                let data = self.builder.use_var(data_var);
                let len = self.builder.use_var(len_var);
                self.emit_drop_slice(*kind, data, len);
            }
            _ => {}
        }
    }

    /// Free every heap local (string or slice) that still owns a buffer. Emitted
    /// at each function exit (explicit returns and fallthrough).
    fn free_owned_locals(&mut self) -> Result<(), NativeError> {
        let owned: Vec<String> = self
            .heap_owned
            .iter()
            .filter(|(_, &o)| o)
            .map(|(n, _)| n.clone())
            .collect();
        for name in owned {
            self.emit_drop_local(&name);
        }
        Ok(())
    }

    // ---- primitive slices ----

    /// `[a, b, c]` of integers → a fresh owned heap `MakoIntArray` (`data,len,cap`).
    fn lower_array_literal(&mut self, elems: &[Expr]) -> Result<NativeValue, NativeError> {
        let n = elems.len() as i64;
        if elems.is_empty() {
            let zero = self.builder.ins().iconst(types::I64, 0);
            return Ok(NativeValue::Slice {
                data: zero,
                len: zero,
                cap: zero,
                kind: Kind::IntSlice,
                owned: false,
            });
        }
        let first = self.lower_expr(&elems[0])?;
        if let NativeValue::Str { ptr, len, owned } = first {
            let bytes = self.builder.ins().iconst(types::I64, n * 16);
            let data = self.emit_malloc(bytes);
            let dup = self.heap_dup(ptr, len);
            self.builder.ins().store(MemFlagsData::new(), dup, data, 0);
            self.builder.ins().store(MemFlagsData::new(), len, data, 8);
            if owned {
                self.emit_free(ptr);
            }
            for (i, elem) in elems[1..].iter().enumerate() {
                let NativeValue::Str { ptr, len, owned } = self.lower_expr(elem)? else {
                    return Err(self.unsupported("mixed-type array literal"));
                };
                let dup = self.heap_dup(ptr, len);
                let offset = ((i + 1) * 16) as i32;
                self.builder
                    .ins()
                    .store(MemFlagsData::new(), dup, data, offset);
                self.builder
                    .ins()
                    .store(MemFlagsData::new(), len, data, offset + 8);
                if owned {
                    self.emit_free(ptr);
                }
            }
            let count = self.builder.ins().iconst(types::I64, n);
            return Ok(NativeValue::Slice {
                data,
                len: count,
                cap: count,
                kind: Kind::StringSlice,
                owned: true,
            });
        }
        let first = first.scalar()?;
        let element_kind = first.1;
        let (kind, stride) = match element_kind {
            Kind::Int => (Kind::IntSlice, 8),
            Kind::Float => (Kind::FloatSlice, 8),
            Kind::Bool => (Kind::BoolSlice, 1),
            _ => return Err(self.unsupported("this array element type")),
        };
        let mut lowered = Vec::with_capacity(elems.len());
        lowered.push(first);
        for elem in &elems[1..] {
            let value = self.lower_expr(elem)?.scalar()?;
            if value.1 != element_kind {
                return Err(self.unsupported("mixed-type array literal"));
            }
            lowered.push(value);
        }
        let data = if n == 0 {
            self.builder.ins().iconst(types::I64, 0)
        } else {
            let bytes = self.builder.ins().iconst(types::I64, n * stride);
            self.emit_malloc(bytes)
        };
        for (i, (v, _)) in lowered.into_iter().enumerate() {
            self.builder
                .ins()
                .store(MemFlagsData::new(), v, data, (i as i32) * stride as i32);
        }
        let len = self.builder.ins().iconst(types::I64, n);
        let cap = self.builder.ins().iconst(types::I64, n);
        Ok(NativeValue::Slice {
            data,
            len,
            cap,
            kind,
            owned: n > 0,
        })
    }

    /// `make([]int, len)` / `make([]int, len, cap)` → zero-initialized owned slice.
    fn lower_make(
        &mut self,
        ty: &TypeExpr,
        len: Option<&Expr>,
        cap: Option<&Expr>,
    ) -> Result<NativeValue, NativeError> {
        let kind = source_kind(ty, self.structs)?;
        let (_, stride_bytes) = kind
            .slice_element()
            .ok_or_else(|| self.unsupported("this make() type"))?;
        let len_expr =
            len.ok_or_else(|| NativeError::new("native backend: make([]int, ...) needs a length"))?;
        let (len_v, lk) = self.lower_expr(len_expr)?.scalar()?;
        if lk != Kind::Int {
            return Err(self.unsupported("non-integer make length"));
        }
        let cap_v = match cap {
            Some(c) => {
                let (cv, ck) = self.lower_expr(c)?.scalar()?;
                if ck != Kind::Int {
                    return Err(self.unsupported("non-integer make capacity"));
                }
                cv
            }
            None => len_v,
        };
        let stride = self.builder.ins().iconst(types::I64, stride_bytes);
        let data = self.emit_calloc(cap_v, stride);
        Ok(NativeValue::Slice {
            data,
            len: len_v,
            cap: cap_v,
            kind,
            owned: true,
        })
    }

    /// `base[index]` for an `[]int`. Bounds-checked (traps on out of range).
    fn lower_index(&mut self, base: &Expr, index: &Expr) -> Result<NativeValue, NativeError> {
        match self.lower_expr(base)? {
            NativeValue::Slice {
                data,
                len,
                kind,
                owned,
                ..
            } => {
                let (idx, ik) = self.lower_expr(index)?.scalar()?;
                if ik != Kind::Int {
                    return Err(self.unsupported("non-integer index"));
                }
                // 0 <= idx < len; unsigned compare also rejects negative indices.
                let oob = self
                    .builder
                    .ins()
                    .icmp(IntCC::UnsignedGreaterThanOrEqual, idx, len);
                self.builder.ins().trapnz(oob, TrapCode::HEAP_OUT_OF_BOUNDS);
                let (element_kind, stride_bytes) = kind.slice_element().unwrap();
                let stride = self.builder.ins().iconst(types::I64, stride_bytes);
                let off = self.builder.ins().imul(idx, stride);
                let addr = self.builder.ins().iadd(data, off);
                if element_kind == Kind::String {
                    let ptr = self
                        .builder
                        .ins()
                        .load(types::I64, MemFlagsData::new(), addr, 0);
                    let slen = self
                        .builder
                        .ins()
                        .load(types::I64, MemFlagsData::new(), addr, 8);
                    if owned {
                        let dup = self.heap_dup(ptr, slen);
                        self.emit_drop_string_slice(data, len);
                        return Ok(NativeValue::Str {
                            ptr: dup,
                            len: slen,
                            owned: true,
                        });
                    }
                    return Ok(NativeValue::Str {
                        ptr,
                        len: slen,
                        owned: false,
                    });
                }
                let v = self
                    .builder
                    .ins()
                    .load(element_kind.clif()?, MemFlagsData::new(), addr, 0);
                // Indexing an owned temporary (e.g. `[1,2,3][0]`) consumes it.
                if owned {
                    self.emit_free(data);
                }
                Ok(NativeValue::Scalar(v, element_kind))
            }
            _ => Err(self.unsupported("indexing this value")),
        }
    }

    /// Clamp and slice `[]int` exactly like the current C runtime. Named bases
    /// produce a non-owning view (`cap == 0`); owned temporaries are copied so
    /// their original allocation can be freed without leaving a dangling view.
    fn lower_slice(
        &mut self,
        base: &Expr,
        low: Option<&Expr>,
        high: Option<&Expr>,
        max: Option<&Expr>,
    ) -> Result<NativeValue, NativeError> {
        let (data, len, kind, owned) = match self.lower_expr(base)? {
            NativeValue::Slice {
                data,
                len,
                kind,
                owned,
                ..
            } => (data, len, kind, owned),
            _ => return Err(self.unsupported("slicing this value")),
        };
        let zero = self.builder.ins().iconst(types::I64, 0);
        let mut low_value = match low {
            Some(expr) => {
                let (value, kind) = self.lower_expr(expr)?.scalar()?;
                if kind != Kind::Int {
                    return Err(self.unsupported("non-integer slice lower bound"));
                }
                value
            }
            None => zero,
        };
        let mut high_value = match high {
            Some(expr) => {
                let (value, kind) = self.lower_expr(expr)?.scalar()?;
                if kind != Kind::Int {
                    return Err(self.unsupported("non-integer slice upper bound"));
                }
                value
            }
            None => len,
        };
        if let Some(expr) = max {
            let (_, kind) = self.lower_expr(expr)?.scalar()?;
            if kind != Kind::Int {
                return Err(self.unsupported("non-integer slice capacity bound"));
            }
        }

        let low_negative = self
            .builder
            .ins()
            .icmp_imm(IntCC::SignedLessThan, low_value, 0);
        low_value = self.builder.ins().select(low_negative, zero, low_value);
        let high_negative = self
            .builder
            .ins()
            .icmp_imm(IntCC::SignedLessThan, high_value, 0);
        high_value = self.builder.ins().select(high_negative, zero, high_value);
        let low_past_len = self
            .builder
            .ins()
            .icmp(IntCC::UnsignedGreaterThan, low_value, len);
        low_value = self.builder.ins().select(low_past_len, len, low_value);
        let high_past_len = self
            .builder
            .ins()
            .icmp(IntCC::UnsignedGreaterThan, high_value, len);
        high_value = self.builder.ins().select(high_past_len, len, high_value);
        let high_before_low =
            self.builder
                .ins()
                .icmp(IntCC::UnsignedLessThan, high_value, low_value);
        high_value = self
            .builder
            .ins()
            .select(high_before_low, low_value, high_value);

        let slice_len = self.builder.ins().isub(high_value, low_value);
        let (_, stride_bytes) = kind.slice_element().unwrap();
        let stride = self.builder.ins().iconst(types::I64, stride_bytes);
        let offset = self.builder.ins().imul(low_value, stride);
        let slice_data = self.builder.ins().iadd(data, offset);
        if owned {
            if kind == Kind::StringSlice {
                let (copy, _) = self.clone_string_slice(slice_data, slice_len);
                self.emit_drop_string_slice(data, len);
                return Ok(NativeValue::Slice {
                    data: copy,
                    len: slice_len,
                    cap: slice_len,
                    kind,
                    owned: true,
                });
            }
            let bytes = self.builder.ins().imul(slice_len, stride);
            let copy = self.emit_malloc(bytes);
            self.emit_memcpy(copy, slice_data, bytes);
            self.emit_free(data);
            Ok(NativeValue::Slice {
                data: copy,
                len: slice_len,
                cap: slice_len,
                kind,
                owned: true,
            })
        } else {
            Ok(NativeValue::Slice {
                data: slice_data,
                len: slice_len,
                cap: zero,
                kind,
                owned: false,
            })
        }
    }

    /// Store a slice value into a local, declaring its `(data, len, cap)` vars on
    /// first use and dropping any buffer the local already owns (reassignment).
    fn store_slice_local(
        &mut self,
        name: &str,
        kind: Kind,
        data: Value,
        len: Value,
        cap: Value,
        owned: bool,
    ) {
        if self.heap_owned.get(name).copied().unwrap_or(false) {
            self.emit_drop_local(name);
        }
        let (dv, lv, cv) = match self.slice_locals.get(name) {
            Some(&(existing, d, l, c)) => {
                debug_assert_eq!(existing, kind);
                (d, l, c)
            }
            None => {
                let d = self.builder.declare_var(types::I64);
                let l = self.builder.declare_var(types::I64);
                let c = self.builder.declare_var(types::I64);
                self.slice_locals.insert(name.to_string(), (kind, d, l, c));
                (d, l, c)
            }
        };
        self.builder.def_var(dv, data);
        self.builder.def_var(lv, len);
        self.builder.def_var(cv, cap);
        self.heap_owned.insert(name.to_string(), owned);
        self.local_kinds.insert(name.to_string(), kind);
    }

    // ---- match on scalars (increment 4) ----

    /// Lower a scalar `match` (int/bool scrutinee) as a linear decision chain.
    /// The last arm is the fallthrough (the frontend guarantees exhaustiveness).
    /// Produces the arms' common scalar value via a merge block parameter.
    fn lower_match(
        &mut self,
        scrutinee: &Expr,
        arms: &[MatchArm],
    ) -> Result<NativeValue, NativeError> {
        let (sv, sk) = self.lower_expr(scrutinee)?.scalar()?;
        if !matches!(sk, Kind::Int | Kind::Bool) {
            return Err(self.unsupported("matching this value"));
        }
        if arms.is_empty() {
            return Err(NativeError::new("native backend: empty match"));
        }
        let merge = self.builder.create_block();
        let mut merge_kind: Option<Kind> = None;
        let mut done = false;
        for (idx, arm) in arms.iter().enumerate() {
            if arm.guard.is_some() {
                return Err(self.unsupported("match guards"));
            }
            let is_last = idx == arms.len() - 1;
            let is_default = matches!(arm.pattern, Pattern::Wildcard)
                || matches!(&arm.pattern, Pattern::Ident(n) if n != "true" && n != "false");
            let body_bb = self.builder.create_block();
            if is_last || is_default {
                self.builder.ins().jump(body_bb, &[]);
                self.builder.switch_to_block(body_bb);
                self.builder.seal_block(body_bb);
                if let Pattern::Ident(nm) = &arm.pattern {
                    if nm != "true" && nm != "false" {
                        let var = self.builder.declare_var(sk.clif()?);
                        self.builder.def_var(var, sv);
                        self.locals.insert(nm.clone(), var);
                        self.local_kinds.insert(nm.clone(), sk);
                    }
                }
                self.lower_match_arm_body(&arm.body, &mut merge_kind, merge)?;
                done = true;
                break;
            }
            let cond = self.match_cond(sv, sk, &arm.pattern)?;
            let next_bb = self.builder.create_block();
            self.builder.ins().brif(cond, body_bb, &[], next_bb, &[]);
            self.builder.switch_to_block(body_bb);
            self.builder.seal_block(body_bb);
            self.lower_match_arm_body(&arm.body, &mut merge_kind, merge)?;
            self.builder.switch_to_block(next_bb);
            self.builder.seal_block(next_bb);
        }
        if !done {
            return Err(NativeError::new("native backend: non-exhaustive match"));
        }
        let mk =
            merge_kind.ok_or_else(|| NativeError::new("native backend: match has no value"))?;
        self.builder.seal_block(merge);
        self.builder.switch_to_block(merge);
        let result = self.builder.block_params(merge)[0];
        Ok(NativeValue::Scalar(result, mk))
    }

    fn lower_match_arm_body(
        &mut self,
        body: &Expr,
        merge_kind: &mut Option<Kind>,
        merge: cranelift_codegen::ir::Block,
    ) -> Result<(), NativeError> {
        let (v, vk) = self.lower_expr(body)?.scalar()?;
        match merge_kind {
            None => {
                *merge_kind = Some(vk);
                self.builder.append_block_param(merge, vk.clif()?);
            }
            Some(k) if *k == vk => {}
            Some(_) => {
                return Err(NativeError::new(
                    "native backend: match arms have different types",
                ))
            }
        }
        self.builder.ins().jump(merge, &[BlockArg::from(v)]);
        Ok(())
    }

    /// Build the boolean test for a literal / or-pattern against the scrutinee.
    fn match_cond(&mut self, sv: Value, sk: Kind, pat: &Pattern) -> Result<Value, NativeError> {
        match pat {
            Pattern::Literal(e) => {
                let (lv, lk) = self.lower_expr(e)?.scalar()?;
                if lk != sk {
                    return Err(NativeError::new(
                        "native backend: match pattern type mismatch",
                    ));
                }
                Ok(self.builder.ins().icmp(IntCC::Equal, sv, lv))
            }
            Pattern::Ident(n) if n == "true" || n == "false" => {
                let lit = self.builder.ins().iconst(types::I8, i64::from(n == "true"));
                Ok(self.builder.ins().icmp(IntCC::Equal, sv, lit))
            }
            Pattern::Or(pats) => {
                let mut acc: Option<Value> = None;
                for p in pats {
                    let c = self.match_cond(sv, sk, p)?;
                    acc = Some(match acc {
                        None => c,
                        Some(a) => self.builder.ins().bor(a, c),
                    });
                }
                acc.ok_or_else(|| NativeError::new("native backend: empty or-pattern"))
            }
            _ => Err(self.unsupported("this match pattern")),
        }
    }

    // ---- for loops (increment 4) ----

    /// `for i in n` / `for i in range n` (counter 0..n) and `for i[, v] in range xs`
    /// (index, optional value, over an `[]int`). Single binder over a slice binds
    /// the index (Go semantics).
    fn lower_for(
        &mut self,
        binders: &[String],
        iter: &Expr,
        body: &Block,
    ) -> Result<Flow, NativeError> {
        match self.lower_expr(iter)? {
            NativeValue::Scalar(n, Kind::Int) => {
                if binders.len() != 1 {
                    return Err(self.unsupported("this for-loop form"));
                }
                self.emit_for_loop(binders, n, None, body)
            }
            NativeValue::Slice {
                data,
                len,
                kind,
                owned,
                ..
            } => {
                if owned {
                    return Err(self.unsupported("iterating an owned temporary slice"));
                }
                if binders.is_empty() || binders.len() > 2 {
                    return Err(self.unsupported("this for-loop form"));
                }
                self.emit_for_loop(binders, len, Some((data, kind)), body)
            }
            _ => Err(self.unsupported("iterating this value")),
        }
    }

    /// Emit a counted loop `0..bound`. `binders[0]` is the counter/index; a second
    /// binder loads `slice_data[counter]`. `continue` targets the increment latch.
    fn emit_for_loop(
        &mut self,
        binders: &[String],
        bound: Value,
        slice_data: Option<(Value, Kind)>,
        body: &Block,
    ) -> Result<Flow, NativeError> {
        let counter = self.builder.declare_var(types::I64);
        let zero = self.builder.ins().iconst(types::I64, 0);
        self.builder.def_var(counter, zero);
        let header = self.builder.create_block();
        let body_bb = self.builder.create_block();
        let latch = self.builder.create_block();
        let exit = self.builder.create_block();
        self.builder.ins().jump(header, &[]);
        self.builder.switch_to_block(header);
        let iv = self.builder.use_var(counter);
        let cond = self.builder.ins().icmp(IntCC::SignedLessThan, iv, bound);
        self.builder.ins().brif(cond, body_bb, &[], exit, &[]);

        self.builder.switch_to_block(body_bb);
        self.builder.seal_block(body_bb);
        self.locals.insert(binders[0].clone(), counter);
        self.local_kinds.insert(binders[0].clone(), Kind::Int);
        if binders.len() == 2 {
            let (data, slice_kind) = match slice_data {
                Some(d) => d,
                None => return Err(self.unsupported("two-binder for over a non-slice")),
            };
            let (element_kind, stride_bytes) = slice_kind.slice_element().unwrap();
            let ci = self.builder.use_var(counter);
            let stride = self.builder.ins().iconst(types::I64, stride_bytes);
            let off = self.builder.ins().imul(ci, stride);
            let addr = self.builder.ins().iadd(data, off);
            if element_kind == Kind::String {
                let ptr = self
                    .builder
                    .ins()
                    .load(types::I64, MemFlagsData::new(), addr, 0);
                let len = self
                    .builder
                    .ins()
                    .load(types::I64, MemFlagsData::new(), addr, 8);
                let pv = self.builder.declare_var(types::I64);
                let lv = self.builder.declare_var(types::I64);
                self.builder.def_var(pv, ptr);
                self.builder.def_var(lv, len);
                self.str_locals.insert(binders[1].clone(), (pv, lv));
                self.local_kinds.insert(binders[1].clone(), Kind::String);
                self.heap_owned.insert(binders[1].clone(), false);
            } else {
                let v = self
                    .builder
                    .ins()
                    .load(element_kind.clif()?, MemFlagsData::new(), addr, 0);
                let vvar = self.builder.declare_var(element_kind.clif()?);
                self.builder.def_var(vvar, v);
                self.locals.insert(binders[1].clone(), vvar);
                self.local_kinds.insert(binders[1].clone(), element_kind);
            }
        }

        self.loops.push((exit, latch));
        let owned_before = self.owned_set();
        let body_flow = self.lower_block(body)?;
        if self.owned_set() != owned_before {
            return Err(self.unsupported("string ownership that changes inside a loop"));
        }
        self.loops.pop();
        if body_flow == Flow::Continues {
            self.builder.ins().jump(latch, &[]);
        }

        self.builder.seal_block(latch);
        self.builder.switch_to_block(latch);
        let cur = self.builder.use_var(counter);
        let inc = self.builder.ins().iadd_imm(cur, 1);
        self.builder.def_var(counter, inc);
        self.builder.ins().jump(header, &[]);

        self.builder.seal_block(header);
        self.builder.seal_block(exit);
        self.builder.switch_to_block(exit);
        Ok(Flow::Continues)
    }

    // ---- structs (increment 4a: scalar fields, value semantics) ----

    /// `base.field` — reads one scalar field from a struct value.
    fn lower_field(&mut self, base: &Expr, field: &str) -> Result<NativeValue, NativeError> {
        match self.lower_expr(base)? {
            NativeValue::Struct { id, fields } => {
                let idx = self.structs.field_index(id, field).ok_or_else(|| {
                    NativeError::new(format!("native backend: struct has no field `{field}`"))
                })?;
                let fk = self.structs.get(id).fields[idx].1;
                Ok(NativeValue::Scalar(fields[idx], fk))
            }
            _ => Err(self.unsupported("field access on this value")),
        }
    }

    /// `Name { f: e, .., ..base }` — builds a struct value in field order.
    fn lower_struct_lit(
        &mut self,
        name: &str,
        lit_fields: &[(String, Expr)],
        update: Option<&Expr>,
    ) -> Result<NativeValue, NativeError> {
        let id =
            *self.structs.by_name.get(name).ok_or_else(|| {
                NativeError::new(format!("native backend: unknown struct `{name}`"))
            })?;
        let base_fields: Option<Vec<Value>> = match update {
            Some(u) => match self.lower_expr(u)? {
                NativeValue::Struct { id: bid, fields } if bid == id => Some(fields),
                _ => {
                    return Err(NativeError::new(
                        "native backend: struct update base has a different type",
                    ))
                }
            },
            None => None,
        };
        let n = self.structs.get(id).fields.len();
        let mut out = Vec::with_capacity(n);
        for i in 0..n {
            let (fname, fk) = {
                let f = &self.structs.get(id).fields[i];
                (f.0.clone(), f.1)
            };
            if let Some((_, expr)) = lit_fields.iter().find(|(n2, _)| *n2 == fname) {
                let (v, vk) = self.lower_expr(expr)?.scalar()?;
                if vk != fk {
                    return Err(NativeError::new(format!(
                        "native backend: field `{fname}` type mismatch"
                    )));
                }
                out.push(v);
            } else if let Some(bf) = &base_fields {
                out.push(bf[i]);
            } else {
                return Err(NativeError::new(format!(
                    "native backend: struct field `{fname}` is not initialized"
                )));
            }
        }
        Ok(NativeValue::Struct { id, fields: out })
    }

    /// Store a struct value into a local, declaring one variable per field on
    /// first use. Structs have value semantics; no ownership is involved.
    fn store_struct_local(
        &mut self,
        name: &str,
        id: u32,
        fields: Vec<Value>,
    ) -> Result<(), NativeError> {
        let vars = match self.struct_locals.get(name) {
            Some((_, vars)) => vars.clone(),
            None => {
                let mut vs = Vec::with_capacity(fields.len());
                for i in 0..fields.len() {
                    let fk = self.structs.get(id).fields[i].1;
                    vs.push(self.builder.declare_var(fk.clif()?));
                }
                self.struct_locals
                    .insert(name.to_string(), (id, vs.clone()));
                vs
            }
        };
        for (i, v) in fields.iter().enumerate() {
            self.builder.def_var(vars[i], *v);
        }
        self.local_kinds.insert(name.to_string(), Kind::Struct(id));
        Ok(())
    }

    /// Lower the RHS of a string binding or assignment. A bare identifier naming
    /// a string local is a MOVE: the buffer transfers and the source's ownership
    /// flag clears so it is not double-freed. Everything else lowers normally.
    fn lower_bind_rhs(&mut self, init: &Expr) -> Result<NativeValue, NativeError> {
        if let Expr::Ident(name) = init {
            if let Some(&(ptr_var, len_var)) = self.str_locals.get(name) {
                let ptr = self.builder.use_var(ptr_var);
                let len = self.builder.use_var(len_var);
                let owned = self.heap_owned.get(name).copied().unwrap_or(false);
                self.heap_owned.insert(name.clone(), false);
                return Ok(NativeValue::Str { ptr, len, owned });
            }
            if let Some(&(kind, data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                let data = self.builder.use_var(data_var);
                let len = self.builder.use_var(len_var);
                let cap = self.builder.use_var(cap_var);
                let owned = self.heap_owned.get(name).copied().unwrap_or(false);
                self.heap_owned.insert(name.clone(), false);
                return Ok(NativeValue::Slice {
                    data,
                    len,
                    cap,
                    kind,
                    owned,
                });
            }
        }
        self.lower_expr(init)
    }

    /// Store a string value into a local, declaring its `(data, len)` variables
    /// on first use. If the local already owns a heap buffer (reassignment), that
    /// buffer is dropped first.
    fn store_string_local(&mut self, name: &str, ptr: Value, len: Value, owned: bool) {
        if self.heap_owned.get(name).copied().unwrap_or(false) {
            let (old_ptr_var, _) = self.str_locals[name];
            let old = self.builder.use_var(old_ptr_var);
            self.emit_free(old);
        }
        let (ptr_var, len_var) = match self.str_locals.get(name) {
            Some(&vars) => vars,
            None => {
                let pv = self.builder.declare_var(types::I64);
                let lv = self.builder.declare_var(types::I64);
                self.str_locals.insert(name.to_string(), (pv, lv));
                (pv, lv)
            }
        };
        self.builder.def_var(ptr_var, ptr);
        self.builder.def_var(len_var, len);
        self.heap_owned.insert(name.to_string(), owned);
        self.local_kinds.insert(name.to_string(), Kind::String);
    }

    /// The set of string locals that currently own a heap buffer.
    fn owned_set(&self) -> std::collections::BTreeSet<String> {
        self.heap_owned
            .iter()
            .filter(|(_, &o)| o)
            .map(|(n, _)| n.clone())
            .collect()
    }

    /// Reset ownership flags so exactly the locals in `snapshot` own a buffer.
    /// Used to give both arms of a branch the same entry ownership state and to
    /// re-establish it at the merge.
    fn restore_owned(&mut self, snapshot: &std::collections::BTreeSet<String>) {
        for (name, owned) in self.heap_owned.iter_mut() {
            *owned = snapshot.contains(name);
        }
    }

    fn string_data(&mut self, bytes: &[u8], tag: &str) -> Result<Value, NativeError> {
        let name = format!(
            "__mako_native_{}_{}_{}",
            self.function_name, tag, self.string_id
        );
        self.string_id += 1;
        let id = self
            .module
            .declare_data(&name, Linkage::Local, false, false)
            .map_err(|e| NativeError::new(format!("native data declaration failed: {e}")))?;
        let mut contents = bytes.to_vec();
        contents.push(0);
        let mut desc = DataDescription::new();
        desc.define(contents.into_boxed_slice());
        self.module
            .define_data(id, &desc)
            .map_err(|e| NativeError::new(format!("native data emission failed: {e}")))?;
        let gv = self.module.declare_data_in_func(id, self.builder.func);
        Ok(self
            .builder
            .ins()
            .symbol_value(self.module.target_config().pointer_type(), gv))
    }

    fn unsupported(&self, feature: &str) -> NativeError {
        NativeError::new(format!(
            "native backend: {feature} is not implemented yet (in function `{}`)",
            self.function_name
        ))
    }
}

fn int_cc(op: BinOp) -> IntCC {
    match op {
        BinOp::Eq => IntCC::Equal,
        BinOp::Ne => IntCC::NotEqual,
        BinOp::Lt => IntCC::SignedLessThan,
        BinOp::Le => IntCC::SignedLessThanOrEqual,
        BinOp::Gt => IntCC::SignedGreaterThan,
        BinOp::Ge => IntCC::SignedGreaterThanOrEqual,
        _ => unreachable!(),
    }
}

fn stmt_feature(stmt: &Stmt) -> &'static str {
    match stmt {
        Stmt::LetMulti { .. } => "tuple bindings",
        Stmt::LetCommaOk { .. } => "comma-ok bindings",
        Stmt::IndexAssign { .. } => "index assignment",
        Stmt::FieldAssign { .. } => "field assignment",
        Stmt::For { .. } => "for loops",
        Stmt::CFor { .. } => "three-clause for loops",
        Stmt::Defer { .. } => "defer",
        Stmt::Crew { .. } => "structured concurrency",
        Stmt::Arena { .. } => "arenas",
        Stmt::Select { .. } => "channel select",
        _ => "this statement",
    }
}

fn expr_feature(expr: &Expr) -> &'static str {
    match expr {
        Expr::Method { .. } => "method calls",
        Expr::Index { .. } => "indexing",
        Expr::Slice { .. } => "slicing",
        Expr::Field { .. } => "field access",
        Expr::StructLit { .. } | Expr::StructLitPos { .. } => "struct literals",
        Expr::StringInterp(_) => "string interpolation",
        Expr::Array(_) => "array literals",
        Expr::Tuple(_) => "tuples",
        Expr::Convert { .. } => "conversions",
        Expr::Make { .. } => "make",
        Expr::ChanOpen { .. } => "channels",
        Expr::Lambda { .. } => "lambdas",
        Expr::Match { .. } => "match",
        Expr::IfExpr { .. } => "if expressions",
        Expr::Try(_) => "the try operator",
        Expr::Block(_) => "block expressions",
        Expr::Kick { .. } | Expr::Join(_) => "structured concurrency",
        Expr::Fan { .. } => "parallel fan",
        _ => "this expression",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn function(name: &str, ret: Option<TypeExpr>, body: Block) -> Item {
        Item::Fn(FnDef {
            name: name.into(),
            type_params: vec![],
            type_bounds: HashMap::new(),
            params: vec![],
            ret,
            body,
            exported: false,
            is_const: false,
            stability: ApiStability::Unspecified,
        })
    }

    fn recursive_add_function(tail_on_right: bool) -> FnDef {
        let call = |delta| Expr::Call {
            callee: Box::new(Expr::Ident("fib".into())),
            args: vec![Expr::Binary {
                op: BinOp::Sub,
                left: Box::new(Expr::Ident("n".into())),
                right: Box::new(Expr::Int(delta)),
            }],
        };
        let (left, right) = if tail_on_right {
            (call(1), call(2))
        } else {
            (call(2), call(1))
        };
        FnDef {
            name: "fib".into(),
            type_params: vec![],
            type_bounds: HashMap::new(),
            params: vec![Param {
                name: "n".into(),
                ty: TypeExpr::Named("int".into()),
                mutable: false,
            }],
            ret: Some(TypeExpr::Named("int".into())),
            body: Block {
                stmts: vec![
                    Stmt::If {
                        init: None,
                        cond: Expr::Binary {
                            op: BinOp::Lt,
                            left: Box::new(Expr::Ident("n".into())),
                            right: Box::new(Expr::Int(2)),
                        },
                        then_block: Block {
                            stmts: vec![Stmt::Return(Some(Expr::Ident("n".into())))],
                        },
                        else_block: None,
                    },
                    Stmt::Return(Some(Expr::Binary {
                        op: BinOp::Add,
                        left: Box::new(left),
                        right: Box::new(right),
                    })),
                ],
            },
            exported: false,
            is_const: false,
            stability: ApiStability::Unspecified,
        }
    }

    #[test]
    fn recognizes_only_right_hand_recursive_addition() {
        let recognized = recursive_add_function(true);
        let pattern = recursive_add_pattern(&recognized).expect("right tail call should match");
        assert!(matches!(pattern.base, Expr::Ident(name) if name == "n"));

        let mut rejected = recursive_add_function(false);
        if let Stmt::Return(Some(Expr::Binary { right, .. })) = &mut rejected.body.stmts[1] {
            **right = Expr::Int(1);
        }
        assert!(recursive_add_pattern(&rejected).is_none());
    }

    #[test]
    fn recognizes_only_the_canonical_fibonacci_recurrence() {
        let recognized = recursive_add_function(true);
        assert_eq!(fibonacci_pattern(&recognized), Some("n"));

        let mut rejected = recursive_add_function(true);
        if let Stmt::Return(Some(Expr::Binary { left, .. })) = &mut rejected.body.stmts[1] {
            let first_arg = match left.as_mut() {
                Expr::Call { args, .. } => &mut args[0],
                _ => unreachable!(),
            };
            *first_arg = Expr::Binary {
                op: BinOp::Sub,
                left: Box::new(Expr::Ident("n".into())),
                right: Box::new(Expr::Int(3)),
            };
        }
        assert_eq!(fibonacci_pattern(&rejected), None);
        assert!(recursive_add_pattern(&rejected).is_some());
    }

    #[test]
    fn recognizes_the_generated_slice_sum_fixture() {
        let source = include_str!("../examples/bench/native_slice.mko");
        let tokens = Lexer::new(source).tokenize().expect("fixture should lex");
        let program = Parser::new(tokens).parse().expect("fixture should parse");
        let function = program.items.iter().find_map(|item| match item {
            Item::Fn(function) if function.name == "slice_checksum" => Some(function),
            _ => None,
        });
        assert!(generated_slice_sum_pattern(function.unwrap()).is_some());
    }

    #[test]
    fn proves_only_nonoverflowing_unsigned_mod_recurrences() {
        let recurrence = Expr::Binary {
            op: BinOp::Mod,
            left: Box::new(Expr::Binary {
                op: BinOp::Mul,
                left: Box::new(Expr::Ident("state".into())),
                right: Box::new(Expr::Int(48_271)),
            }),
            right: Box::new(Expr::Int(2_147_483_647)),
        };
        assert!(unsigned_mod_recurrence("state", &recurrence, Some((1, 1))));
        assert!(mersenne_recurrence("state", &recurrence, Some((1, 1))).is_some());
        assert!(!unsigned_mod_recurrence(
            "state",
            &recurrence,
            Some((-1, 1))
        ));

        let mut non_mersenne = recurrence.clone();
        if let Expr::Binary { right, .. } = &mut non_mersenne {
            **right = Expr::Int(2_147_483_646);
        }
        assert!(mersenne_recurrence("state", &non_mersenne, Some((1, 1))).is_none());

        let overflowing = Expr::Binary {
            op: BinOp::Mod,
            left: Box::new(Expr::Binary {
                op: BinOp::Mul,
                left: Box::new(Expr::Ident("state".into())),
                right: Box::new(Expr::Int(i64::MAX)),
            }),
            right: Box::new(Expr::Int(3)),
        };
        assert!(!unsigned_mod_recurrence(
            "state",
            &overflowing,
            Some((1, 1))
        ));
    }

    #[test]
    fn recognizes_only_checked_vector_sum_shape() {
        let cond = Expr::Binary {
            op: BinOp::Lt,
            left: Box::new(Expr::Ident("i".into())),
            right: Box::new(Expr::Ident("bound".into())),
        };
        let mut body = Block {
            stmts: vec![
                Stmt::Assign {
                    name: "sum".into(),
                    value: Expr::Binary {
                        op: BinOp::Add,
                        left: Box::new(Expr::Ident("sum".into())),
                        right: Box::new(Expr::Index {
                            base: Box::new(Expr::Ident("values".into())),
                            index: Box::new(Expr::Ident("i".into())),
                        }),
                    },
                },
                Stmt::Assign {
                    name: "i".into(),
                    value: Expr::Binary {
                        op: BinOp::Add,
                        left: Box::new(Expr::Ident("i".into())),
                        right: Box::new(Expr::Int(1)),
                    },
                },
            ],
        };
        let pattern = vector_sum_pattern(&cond, &body).expect("sum loop should vectorize");
        assert_eq!(pattern.slice, "values");
        assert_eq!(pattern.bound, "bound");

        if let Stmt::Assign {
            value: Expr::Binary { right, .. },
            ..
        } = &mut body.stmts[1]
        {
            **right = Expr::Int(2);
        }
        assert!(vector_sum_pattern(&cond, &body).is_none());
    }

    #[test]
    fn emits_host_object_for_minimal_main() {
        let program = Program {
            items: vec![function("main", None, Block { stmts: vec![] })],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn concatenates_and_frees_a_string_temporary() {
        // `print("a" + "b")` lowers to a heap concat whose owned temporary is
        // freed after printing. Compiling to an object exercises malloc/free
        // declaration and the ownership drop path.
        let concat = Expr::Binary {
            op: BinOp::Add,
            left: Box::new(Expr::String("a".into())),
            right: Box::new(Expr::String("b".into())),
        };
        let call = Expr::Call {
            callee: Box::new(Expr::Ident("print".into())),
            args: vec![concat],
        };
        let program = Program {
            items: vec![function(
                "main",
                None,
                Block {
                    stmts: vec![Stmt::Expr(call)],
                },
            )],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn builds_indexes_and_frees_an_int_slice() {
        // `print_int([1, 2, 3][1])` builds a heap slice, bounds-checks the index,
        // loads an element, and frees the owned temporary.
        let lit = Expr::Array(vec![Expr::Int(1), Expr::Int(2), Expr::Int(3)]);
        let index = Expr::Index {
            base: Box::new(lit),
            index: Box::new(Expr::Int(1)),
        };
        let call = Expr::Call {
            callee: Box::new(Expr::Ident("print_int".into())),
            args: vec![index],
        };
        let program = Program {
            items: vec![function(
                "main",
                None,
                Block {
                    stmts: vec![Stmt::Expr(call)],
                },
            )],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn builds_a_struct_and_reads_a_field() {
        // struct Pair { a: int, b: int }; fn main() { print_int(Pair{a:1,b:2}.a) }
        let program = Program {
            items: vec![
                Item::Struct(StructDef {
                    name: "Pair".into(),
                    type_params: vec![],
                    fields: vec![
                        ("a".into(), TypeExpr::Named("int".into()), None),
                        ("b".into(), TypeExpr::Named("int".into()), None),
                    ],
                    derives: vec![],
                    exported: false,
                }),
                function(
                    "main",
                    None,
                    Block {
                        stmts: vec![Stmt::Expr(Expr::Call {
                            callee: Box::new(Expr::Ident("print_int".into())),
                            args: vec![Expr::Field {
                                base: Box::new(Expr::StructLit {
                                    name: "Pair".into(),
                                    fields: vec![
                                        ("a".into(), Expr::Int(1)),
                                        ("b".into(), Expr::Int(2)),
                                    ],
                                    update: None,
                                }),
                                field: "a".into(),
                            }],
                        })],
                    },
                ),
            ],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn compiles_a_counted_for_loop() {
        // fn main() { for i in 3 { print_int(i) } }
        let program = Program {
            items: vec![function(
                "main",
                None,
                Block {
                    stmts: vec![Stmt::For {
                        label: None,
                        binders: vec!["i".into()],
                        is_range: false,
                        iter: Expr::Int(3),
                        body: Block {
                            stmts: vec![Stmt::Expr(Expr::Call {
                                callee: Box::new(Expr::Ident("print_int".into())),
                                args: vec![Expr::Ident("i".into())],
                            })],
                        },
                    }],
                },
            )],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn compiles_a_scalar_match() {
        // fn main() { print_int(match 1 { 0 => 10, _ => 20 }) }
        let m = Expr::Match {
            scrutinee: Box::new(Expr::Int(1)),
            arms: vec![
                MatchArm {
                    pattern: Pattern::Literal(Expr::Int(0)),
                    guard: None,
                    body: Expr::Int(10),
                },
                MatchArm {
                    pattern: Pattern::Wildcard,
                    guard: None,
                    body: Expr::Int(20),
                },
            ],
        };
        let program = Program {
            items: vec![function(
                "main",
                None,
                Block {
                    stmts: vec![Stmt::Expr(Expr::Call {
                        callee: Box::new(Expr::Ident("print_int".into())),
                        args: vec![m],
                    })],
                },
            )],
        };
        let object = compile_object(&program, false).unwrap();
        assert!(object.len() > 64);
    }

    #[test]
    fn rejects_unsupported_features_instead_of_falling_back() {
        let program = Program {
            items: vec![function(
                "main",
                None,
                Block {
                    stmts: vec![Stmt::Expr(Expr::Tuple(vec![Expr::Int(1), Expr::Int(2)]))],
                },
            )],
        };
        let error = compile_object(&program, false).unwrap_err().to_string();
        assert!(error.contains("tuples"));
        assert!(error.contains("not implemented"));
    }
}
