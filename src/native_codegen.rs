//! Direct native object-code backend.
//!
//! This backend deliberately starts with a small, checked language surface. It
//! lowers supported typed-AST constructs straight to Cranelift IR and then to a
//! host object file. Unsupported constructs are errors: they never fall back to
//! C silently, because that would make backend-parity testing unreliable.

use crate::ast::*;
use cranelift_codegen::ir::condcodes::IntCC;
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
    /// A user struct, indexed into the `Structs` registry. Value semantics; its
    /// scalar fields are flattened to one SSA value each.
    Struct(u32),
    Void,
}

impl Kind {
    fn clif(self) -> Result<cranelift_codegen::ir::Type, NativeError> {
        match self {
            Kind::Int => Ok(types::I64),
            Kind::Bool => Ok(types::I8),
            Kind::Float => Ok(types::F64),
            Kind::String => Err(NativeError::new(
                "native backend: string parameters and returns are not implemented yet",
            )),
            Kind::IntSlice => Err(NativeError::new(
                "native backend: slice parameters and returns are not implemented yet",
            )),
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
        Kind::IntSlice => {
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
    let isa = cranelift_native::builder()
        .map_err(|e| NativeError::new(format!("native backend does not support this host: {e}")))?
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
    let print_int = define_print_i64(&mut module, write)?;
    let libc = declare_libc(&mut module)?;

    for item in &program.items {
        if let Item::Fn(f) = item {
            compile_function(&mut module, &funcs, &structs, write, print_int, libc, f)?;
        }
    }

    let product = module.finish();
    product
        .emit()
        .map_err(|e| NativeError::new(format!("native object emission failed: {e}")))
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

    Ok(Libc {
        malloc,
        calloc,
        free,
        memcpy,
    })
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
                libc,
                locals: HashMap::new(),
                str_locals: HashMap::new(),
                slice_locals: HashMap::new(),
                struct_locals: HashMap::new(),
                heap_owned: HashMap::new(),
                local_kinds: HashMap::new(),
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
                        lower.str_locals.insert(param.name.clone(), (ptr_var, len_var));
                        lower.heap_owned.insert(param.name.clone(), false);
                    }
                    Kind::IntSlice => {
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
                            .insert(param.name.clone(), (data_var, len_var, cap_var));
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
                        lower.builder.def_var(var, lower.builder.block_params(entry)[bp]);
                        bp += 1;
                        lower.locals.insert(param.name.clone(), var);
                    }
                }
                lower.local_kinds.insert(param.name.clone(), kind);
            }
            let flow = lower.lower_block(&f.body)?;
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
    libc: Libc,
    locals: HashMap<String, Variable>,
    /// String locals hold two variables: `(data, len)`.
    str_locals: HashMap<String, (Variable, Variable)>,
    /// `[]int` locals hold three variables: `(data, len, cap)`.
    slice_locals: HashMap<String, (Variable, Variable, Variable)>,
    /// Struct locals: the struct id and one variable per scalar field, in order.
    struct_locals: HashMap<String, (u32, Vec<Variable>)>,
    /// Static ownership state per heap local (string or slice): `true` = currently
    /// owns a heap buffer that must be dropped; `false` = a non-owned view/borrow,
    /// or moved-out. Consulted by the drop pass and the control-flow guard.
    heap_owned: HashMap<String, bool>,
    local_kinds: HashMap<String, Kind>,
    loops: Vec<(cranelift_codegen::ir::Block, cranelift_codegen::ir::Block)>,
    string_id: usize,
    function_name: &'a str,
    function_ret: Kind,
}

impl FunctionLowerer<'_, '_> {
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
                        owned,
                    } => {
                        if let Some(t) = ty {
                            if source_kind(t, self.structs)? != Kind::IntSlice {
                                return Err(NativeError::new(format!(
                                    "native backend: initializer type mismatch for `{name}`"
                                )));
                            }
                        }
                        self.store_slice_local(name, data, len, cap, owned);
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
                    Kind::IntSlice => match self.lower_bind_rhs(value)? {
                        NativeValue::Slice {
                            data,
                            len,
                            cap,
                            owned,
                        } => {
                            self.store_slice_local(name, data, len, cap, owned);
                        }
                        _ => {
                            return Err(NativeError::new(format!(
                                "native backend: assignment type mismatch for `{name}`"
                            )));
                        }
                    },
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
                        let (value, actual) = self.lower_expr(value)?.scalar()?;
                        if expected != actual {
                            return Err(NativeError::new(format!(
                                "native backend: assignment type mismatch for `{name}`"
                            )));
                        }
                        self.builder.def_var(var, value);
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
                let (data, len, owned) = match self.lower_expr(base)? {
                    NativeValue::Slice {
                        data, len, owned, ..
                    } => (data, len, owned),
                    _ => return Err(self.unsupported("index assignment to this value")),
                };
                let (idx, ik) = self.lower_expr(index)?.scalar()?;
                if ik != Kind::Int {
                    return Err(self.unsupported("non-integer index"));
                }
                let (v, vk) = self.lower_expr(value)?.scalar()?;
                if vk != Kind::Int {
                    return Err(self.unsupported("assigning a non-integer element"));
                }
                let oob = self
                    .builder
                    .ins()
                    .icmp(IntCC::UnsignedGreaterThanOrEqual, idx, len);
                self.builder.ins().trapnz(oob, TrapCode::HEAP_OUT_OF_BOUNDS);
                let eight = self.builder.ins().iconst(types::I64, 8);
                let off = self.builder.ins().imul(idx, eight);
                let addr = self.builder.ins().iadd(data, off);
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
                        data, owned: true, ..
                    } => self.emit_free(data),
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
                    } else if self.function_ret == Kind::IntSlice {
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
                self.builder.switch_to_block(then_bb);
                self.builder.seal_block(then_bb);
                let then_flow = self.lower_block(then_block)?;
                if then_flow == Flow::Continues {
                    if self.owned_set() != owned_before {
                        return Err(self.unsupported("string ownership that changes inside a branch"));
                    }
                    self.builder.ins().jump(merge_bb, &[]);
                }
                self.restore_owned(&owned_before);

                self.builder.switch_to_block(else_bb);
                self.builder.seal_block(else_bb);
                let else_flow = if let Some(block) = else_block {
                    self.lower_block(block)?
                } else {
                    Flow::Continues
                };
                if else_flow == Flow::Continues {
                    if self.owned_set() != owned_before {
                        return Err(self.unsupported("string ownership that changes inside a branch"));
                    }
                    self.builder.ins().jump(merge_bb, &[]);
                }
                self.restore_owned(&owned_before);
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
                let body_flow = self.lower_block(body)?;
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
                self.lower_for(binders, iter, body)
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
                if let Some(&(data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                    let data = self.builder.use_var(data_var);
                    let len = self.builder.use_var(len_var);
                    let cap = self.builder.use_var(cap_var);
                    return Ok(NativeValue::Slice {
                        data,
                        len,
                        cap,
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
            Expr::Array(elems) => self.lower_int_array_literal(elems),
            Expr::Make { ty, len, cap } => self.lower_make(ty, len.as_deref(), cap.as_deref()),
            Expr::Index { base, index } => self.lower_index(base, index),
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
            if op != BinOp::Add {
                return Err(self.unsupported("this string operation"));
            }
            let (ap, al, ao) = match lv {
                NativeValue::Str { ptr, len, owned } => (ptr, len, owned),
                _ => unreachable!(),
            };
            let (bp, bl, bo) = match rv {
                NativeValue::Str { ptr, len, owned } => (ptr, len, owned),
                _ => unreachable!(),
            };
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
            if kind != Kind::Int {
                return Err(self.unsupported("non-integer numeric conversion"));
            }
            return Ok(NativeValue::Scalar(value, Kind::Int));
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
                    data, len, owned, ..
                } => {
                    if owned {
                        self.emit_free(data);
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
        if name == "append" {
            return self.lower_append(args);
        }
        if matches!(name.as_str(), "print" | "print_int" | "print_int64") {
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
                NativeValue::Scalar(_, Kind::Float) => {
                    return Err(self.unsupported("printing floats"))
                }
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
                Kind::IntSlice => match self.lower_expr(arg)? {
                    NativeValue::Slice {
                        data,
                        len,
                        cap,
                        owned,
                    } => {
                        values.push(data);
                        values.push(len);
                        values.push(cap);
                        if owned {
                            arg_temps.push(data);
                        }
                    }
                    _ => {
                        return Err(NativeError::new(format!(
                            "native backend: argument type mismatch calling `{name}`"
                        )));
                    }
                },
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
        } else if meta.ret == Kind::IntSlice {
            // Slice returns are always owned (see `lower_return_slice`).
            let data = self.builder.inst_results(call)[0];
            let len = self.builder.inst_results(call)[1];
            let cap = self.builder.inst_results(call)[2];
            NativeValue::Slice {
                data,
                len,
                cap,
                owned: true,
            }
        } else if let Kind::Struct(id) = meta.ret {
            let n = self.structs.get(id).fields.len();
            let fields = (0..n)
                .map(|i| self.builder.inst_results(call)[i])
                .collect();
            NativeValue::Struct { id, fields }
        } else {
            NativeValue::Scalar(self.builder.inst_results(call)[0], meta.ret)
        };
        for ptr in arg_temps {
            self.emit_free(ptr);
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

    /// Clone a slice's `len` elements into a fresh owned buffer. `cap == len`.
    fn slice_clone(&mut self, data: Value, len: Value) -> (Value, Value) {
        let eight = self.builder.ins().iconst(types::I64, 8);
        let bytes = self.builder.ins().imul(len, eight);
        let nd = self.emit_malloc(bytes);
        self.emit_memcpy(nd, data, bytes);
        (nd, len)
    }

    /// Produce the owned `(data, len, cap)` a function returns for `[]int`. A
    /// returned local that owns its buffer is moved out; any other slice is cloned.
    fn lower_return_slice(&mut self, expr: &Expr) -> Result<(Value, Value, Value), NativeError> {
        if let Expr::Ident(name) = expr {
            if let Some(&(data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                let data = self.builder.use_var(data_var);
                let len = self.builder.use_var(len_var);
                let cap = self.builder.use_var(cap_var);
                if *self.heap_owned.get(name).unwrap_or(&false) {
                    self.heap_owned.insert(name.clone(), false); // moved out
                    return Ok((data, len, cap));
                }
                let (nd, ncap) = self.slice_clone(data, len);
                return Ok((nd, len, ncap));
            }
        }
        match self.lower_expr(expr)? {
            NativeValue::Slice {
                data,
                len,
                cap,
                owned: true,
            } => Ok((data, len, cap)),
            NativeValue::Slice {
                data,
                len,
                owned: false,
                ..
            } => {
                let (nd, ncap) = self.slice_clone(data, len);
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
    ) -> Result<(Value, Value, Value, bool), NativeError> {
        if let Expr::Ident(name) = expr {
            if let Some(&(dv, lv, cv)) = self.slice_locals.get(name) {
                let data = self.builder.use_var(dv);
                let len = self.builder.use_var(lv);
                let cap = self.builder.use_var(cv);
                let owned = self.heap_owned.get(name).copied().unwrap_or(false);
                self.heap_owned.insert(name.clone(), false);
                return Ok((data, len, cap, owned));
            }
        }
        match self.lower_expr(expr)? {
            NativeValue::Slice {
                data,
                len,
                cap,
                owned,
            } => Ok((data, len, cap, owned)),
            _ => Err(NativeError::new("native backend: `append` expects a slice")),
        }
    }

    fn lower_append(&mut self, args: &[Expr]) -> Result<NativeValue, NativeError> {
        if args.len() != 2 {
            return Err(NativeError::new(
                "native backend: `append` expects two arguments",
            ));
        }
        let (data, len, cap, owned) = self.consume_slice_operand(&args[0])?;
        let (v, vk) = self.lower_expr(&args[1])?.scalar()?;
        if vk != Kind::Int {
            return Err(self.unsupported("appending a non-integer element"));
        }
        let eight = self.builder.ins().iconst(types::I64, 8);
        let one = self.builder.ins().iconst(types::I64, 1);

        if !owned {
            // Borrowed/view source: never mutate or free it — copy into a fresh
            // buffer sized for one more element.
            let newcap = self.builder.ins().iadd(len, one);
            let bytes = self.builder.ins().imul(newcap, eight);
            let nd = self.emit_malloc(bytes);
            let copy_bytes = self.builder.ins().imul(len, eight);
            self.emit_memcpy(nd, data, copy_bytes);
            let off = self.builder.ins().imul(len, eight);
            let addr = self.builder.ins().iadd(nd, off);
            self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
            let newlen = self.builder.ins().iadd(len, one);
            return Ok(NativeValue::Slice {
                data: nd,
                len: newlen,
                cap: newcap,
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
        let off = self.builder.ins().imul(len, eight);
        let addr = self.builder.ins().iadd(data, off);
        self.builder.ins().store(MemFlagsData::new(), v, addr, 0);
        let newlen = self.builder.ins().iadd(len, one);
        self.builder.ins().jump(
            done_bb,
            &[BlockArg::from(data), BlockArg::from(newlen), BlockArg::from(cap)],
        );

        self.builder.switch_to_block(grow_bb);
        self.builder.seal_block(grow_bb);
        let four = self.builder.ins().iconst(types::I64, 4);
        let twoc = self.builder.ins().iconst(types::I64, 2);
        let two_cap = self.builder.ins().imul(cap, twoc);
        let is_zero = self.builder.ins().icmp_imm(IntCC::Equal, cap, 0);
        let newcap = self.builder.ins().select(is_zero, four, two_cap);
        let bytes = self.builder.ins().imul(newcap, eight);
        let nd = self.emit_malloc(bytes);
        let copy_bytes = self.builder.ins().imul(len, eight);
        self.emit_memcpy(nd, data, copy_bytes);
        let goff = self.builder.ins().imul(len, eight);
        let gaddr = self.builder.ins().iadd(nd, goff);
        self.builder.ins().store(MemFlagsData::new(), v, gaddr, 0);
        self.emit_free(data);
        let glen = self.builder.ins().iadd(len, one);
        self.builder.ins().jump(
            done_bb,
            &[BlockArg::from(nd), BlockArg::from(glen), BlockArg::from(newcap)],
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
            Some(Kind::IntSlice) => {
                let (data_var, _, _) = self.slice_locals[name];
                let data = self.builder.use_var(data_var);
                self.emit_free(data);
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

    // ---- []int slices (increment 3) ----

    /// `[a, b, c]` of integers → a fresh owned heap `MakoIntArray` (`data,len,cap`).
    fn lower_int_array_literal(&mut self, elems: &[Expr]) -> Result<NativeValue, NativeError> {
        let n = elems.len() as i64;
        let data = if n == 0 {
            self.builder.ins().iconst(types::I64, 0)
        } else {
            let bytes = self.builder.ins().iconst(types::I64, n * 8);
            self.emit_malloc(bytes)
        };
        for (i, e) in elems.iter().enumerate() {
            let (v, k) = self.lower_expr(e)?.scalar()?;
            if k != Kind::Int {
                return Err(self.unsupported("non-integer array elements"));
            }
            self.builder
                .ins()
                .store(MemFlagsData::new(), v, data, (i as i32) * 8);
        }
        let len = self.builder.ins().iconst(types::I64, n);
        let cap = self.builder.ins().iconst(types::I64, n);
        Ok(NativeValue::Slice {
            data,
            len,
            cap,
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
        match ty {
            TypeExpr::Array(elem)
                if matches!(elem.as_ref(), TypeExpr::Named(n) if n == "int" || n == "int64") => {}
            _ => return Err(self.unsupported("this make() type")),
        }
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
        let eight = self.builder.ins().iconst(types::I64, 8);
        // calloc(cap, 8): zero-initialized, matching make() semantics.
        let data = self.emit_calloc(cap_v, eight);
        Ok(NativeValue::Slice {
            data,
            len: len_v,
            cap: cap_v,
            owned: true,
        })
    }

    /// `base[index]` for an `[]int`. Bounds-checked (traps on out of range).
    fn lower_index(&mut self, base: &Expr, index: &Expr) -> Result<NativeValue, NativeError> {
        match self.lower_expr(base)? {
            NativeValue::Slice {
                data, len, owned, ..
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
                let eight = self.builder.ins().iconst(types::I64, 8);
                let off = self.builder.ins().imul(idx, eight);
                let addr = self.builder.ins().iadd(data, off);
                let v = self
                    .builder
                    .ins()
                    .load(types::I64, MemFlagsData::new(), addr, 0);
                // Indexing an owned temporary (e.g. `[1,2,3][0]`) consumes it.
                if owned {
                    self.emit_free(data);
                }
                Ok(NativeValue::Scalar(v, Kind::Int))
            }
            _ => Err(self.unsupported("indexing this value")),
        }
    }

    /// Store a slice value into a local, declaring its `(data, len, cap)` vars on
    /// first use and dropping any buffer the local already owns (reassignment).
    fn store_slice_local(&mut self, name: &str, data: Value, len: Value, cap: Value, owned: bool) {
        if self.heap_owned.get(name).copied().unwrap_or(false) {
            self.emit_drop_local(name);
        }
        let (dv, lv, cv) = match self.slice_locals.get(name) {
            Some(&vars) => vars,
            None => {
                let d = self.builder.declare_var(types::I64);
                let l = self.builder.declare_var(types::I64);
                let c = self.builder.declare_var(types::I64);
                self.slice_locals.insert(name.to_string(), (d, l, c));
                (d, l, c)
            }
        };
        self.builder.def_var(dv, data);
        self.builder.def_var(lv, len);
        self.builder.def_var(cv, cap);
        self.heap_owned.insert(name.to_string(), owned);
        self.local_kinds.insert(name.to_string(), Kind::IntSlice);
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
        let mk = merge_kind.ok_or_else(|| NativeError::new("native backend: match has no value"))?;
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
                data, len, owned, ..
            } => {
                if owned {
                    return Err(self.unsupported("iterating an owned temporary slice"));
                }
                if binders.is_empty() || binders.len() > 2 {
                    return Err(self.unsupported("this for-loop form"));
                }
                self.emit_for_loop(binders, len, Some(data), body)
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
        slice_data: Option<Value>,
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
            let data = match slice_data {
                Some(d) => d,
                None => return Err(self.unsupported("two-binder for over a non-slice")),
            };
            let ci = self.builder.use_var(counter);
            let eight = self.builder.ins().iconst(types::I64, 8);
            let off = self.builder.ins().imul(ci, eight);
            let addr = self.builder.ins().iadd(data, off);
            let v = self
                .builder
                .ins()
                .load(types::I64, MemFlagsData::new(), addr, 0);
            let vvar = self.builder.declare_var(types::I64);
            self.builder.def_var(vvar, v);
            self.locals.insert(binders[1].clone(), vvar);
            self.local_kinds.insert(binders[1].clone(), Kind::Int);
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
        let id = *self.structs.by_name.get(name).ok_or_else(|| {
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
                self.struct_locals.insert(name.to_string(), (id, vs.clone()));
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
            if let Some(&(data_var, len_var, cap_var)) = self.slice_locals.get(name) {
                let data = self.builder.use_var(data_var);
                let len = self.builder.use_var(len_var);
                let cap = self.builder.use_var(cap_var);
                let owned = self.heap_owned.get(name).copied().unwrap_or(false);
                self.heap_owned.insert(name.clone(), false);
                return Ok(NativeValue::Slice {
                    data,
                    len,
                    cap,
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
