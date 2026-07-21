//! Backend-neutral native IR.
//!
//! Heap ownership is resolved here, before machine-code selection: string
//! allocation, clone, move, and drop operations are explicit instructions that
//! every backend must preserve.

use crate::ast::{
    BinOp, Block as AstBlock, Expr, FnDef, Item, Pattern, Program, Stmt, TypeExpr, UnaryOp,
};
use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Type {
    I1,
    I32,
    I64,
    F64,
    Str,
    /// Owned `[]int` slice represented by a `(data, len, cap)` runtime value.
    IntSlice,
    /// Owned `[]string` slice; elements require recursive clone/drop.
    StrSlice,
    /// A user struct with scalar fields, indexed into `Module::structs`. The
    /// value is an owned heap block of one 8-byte slot per field (value
    /// semantics: bindings copy, so both source and destination stay live).
    Struct(u32),
}

impl Type {
    /// Heap-owned value types: they carry an owning pointer that must be
    /// cloned/moved/dropped explicitly by the ownership machinery.
    fn is_heap(self) -> bool {
        matches!(
            self,
            Type::Str | Type::IntSlice | Type::StrSlice | Type::Struct(_)
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Value(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct BlockId(pub u32);

#[allow(dead_code)]
#[derive(Clone, Debug)]
pub enum Inst {
    ConstInt {
        out: Value,
        value: i64,
        ty: Type,
    },
    ConstFloat {
        out: Value,
        value: f64,
    },
    Alloca {
        out: Value,
        ty: Type,
    },
    Load {
        out: Value,
        ptr: Value,
        ty: Type,
    },
    Store {
        ptr: Value,
        value: Value,
    },
    Binary {
        out: Value,
        op: BinOp,
        left: Value,
        right: Value,
        ty: Type,
    },
    Unary {
        out: Value,
        op: UnaryOp,
        value: Value,
        ty: Type,
    },
    Call {
        out: Option<Value>,
        function: String,
        args: Vec<Value>,
        ret: Option<Type>,
    },
    PrintInt {
        value: Value,
    },
    PrintBool {
        value: Value,
    },
    StringLiteral {
        out: Value,
        bytes: Vec<u8>,
    },
    StringClone {
        out: Value,
        value: Value,
    },
    StringConcat {
        out: Value,
        left: Value,
        right: Value,
    },
    StringEqual {
        out: Value,
        left: Value,
        right: Value,
        negated: bool,
    },
    PrintString {
        value: Value,
    },
    DropString {
        value: Value,
    },
    SliceLiteral {
        out: Value,
        elements: Vec<Value>,
    },
    SliceMake {
        out: Value,
        len: Value,
        cap: Option<Value>,
    },
    SliceLen {
        out: Value,
        slice: Value,
    },
    SliceIndex {
        out: Value,
        slice: Value,
        index: Value,
    },
    SliceStore {
        slice: Value,
        index: Value,
        value: Value,
    },
    SliceAppend {
        out: Value,
        slice: Value,
        value: Value,
    },
    SliceSlice {
        out: Value,
        slice: Value,
        low: Value,
        high: Value,
        max: Option<Value>,
    },
    SliceClone {
        out: Value,
        slice: Value,
    },
    DropSlice {
        value: Value,
    },
    StrSliceLiteral { out: Value, elements: Vec<Value> },
    StrSliceMake { out: Value, len: Value, cap: Option<Value> },
    StrSliceLen { out: Value, slice: Value },
    StrSliceIndex { out: Value, slice: Value, index: Value },
    StrSliceStore { slice: Value, index: Value, value: Value },
    StrSliceAppend { out: Value, slice: Value, value: Value },
    StrSliceSlice { out: Value, slice: Value, low: Value, high: Value, max: Option<Value> },
    StrSliceClone { out: Value, slice: Value },
    DropStrSlice { value: Value },
    /// Allocate a fresh owned struct block and store each scalar field into its
    /// slot (`fields[i]` → slot `i`).
    StructMake {
        out: Value,
        struct_id: u32,
        fields: Vec<Value>,
    },
    /// Load scalar field `index` from a struct block.
    StructField {
        out: Value,
        base: Value,
        struct_id: u32,
        index: u32,
        ty: Type,
    },
    /// Store a scalar field `index` into a struct block (mutation).
    StructFieldStore {
        base: Value,
        struct_id: u32,
        index: u32,
        value: Value,
    },
    /// Deep-copy a struct block (value-copy semantics for scalar fields).
    StructClone {
        out: Value,
        base: Value,
        struct_id: u32,
    },
    /// Free an owned struct block, recursively dropping owned fields first.
    DropStruct {
        value: Value,
        struct_id: u32,
    },
    /// Allocate a zero-initialised enum block, store the discriminant into slot
    /// 0, and store the variant's payload into its dedicated slots. Slots the
    /// active variant does not use stay zero (null for owned fields), which the
    /// null-safe recursive clone/drop handles.
    EnumMake {
        out: Value,
        enum_id: u32,
        tag: i64,
        slot_base: u32,
        payload: Vec<Value>,
    },
}

#[derive(Clone, Debug)]
pub enum Terminator {
    Jump(BlockId),
    Branch {
        condition: Value,
        then_block: BlockId,
        else_block: BlockId,
    },
    Return(Option<Value>),
}

#[derive(Clone, Debug)]
pub struct BasicBlock {
    pub instructions: Vec<Inst>,
    pub terminator: Option<Terminator>,
}

#[allow(dead_code)]
#[derive(Clone, Debug)]
pub struct Function {
    pub name: String,
    pub params: Vec<(String, Value, Type)>,
    pub ret: Option<Type>,
    pub blocks: Vec<BasicBlock>,
    pub entry: BlockId,
    pub next_value: u32,
}

/// Layout of a user struct: fields in declaration order. Every scalar field
/// occupies one 8-byte slot; slot `i` lives at byte offset `i * 8`.
#[derive(Clone, Debug)]
pub struct StructLayout {
    /// Struct name, retained for diagnostics.
    #[allow(dead_code)]
    pub name: String,
    pub fields: Vec<(String, Type)>,
}

#[derive(Clone, Debug)]
pub struct Module {
    pub functions: Vec<Function>,
    pub structs: Vec<StructLayout>,
}

/// Registry of aggregate layouts. Named structs are resolved up front;
/// anonymous tuple shapes are interned on demand during lowering (hence the
/// interior mutability). Both share one `id` space and one `StructLayout` list,
/// so tuples reuse the entire struct heap-ownership machinery — a tuple is just
/// an anonymous struct whose fields are named `"0"`, `"1"`, … .
struct StructRegistry {
    by_name: HashMap<String, u32>,
    layouts: RefCell<Vec<StructLayout>>,
    tuple_shapes: RefCell<HashMap<Vec<Type>, u32>>,
    /// Variant name → its enum. Enums are laid out as a heap block
    /// `[tag, p0, p1, …]`; slot 0 is the discriminant and slots `1..=arity`
    /// carry the (scalar) payload. This reuses the struct heap machinery.
    variants: HashMap<String, VariantInfo>,
    /// Which layout ids are enums (as opposed to structs/tuples).
    enum_ids: std::collections::HashSet<u32>,
}

#[derive(Clone, Copy)]
struct VariantInfo {
    enum_id: u32,
    tag: i64,
    arity: usize,
    /// First payload slot index for this variant. Each variant owns dedicated,
    /// non-overlapping slots, so every slot has a single fixed type and the
    /// flat recursive clone/drop is correct (inactive owned slots hold null and
    /// are cloned/dropped as no-ops).
    slot_base: usize,
}

impl StructRegistry {
    fn field_count(&self, id: u32) -> usize {
        self.layouts.borrow()[id as usize].fields.len()
    }

    fn field_type(&self, id: u32, index: usize) -> Type {
        self.layouts.borrow()[id as usize].fields[index].1
    }

    fn field_name(&self, id: u32, index: usize) -> String {
        self.layouts.borrow()[id as usize].fields[index].0.clone()
    }

    fn field_types(&self, id: u32) -> Vec<Type> {
        self.layouts.borrow()[id as usize]
            .fields
            .iter()
            .map(|(_, t)| *t)
            .collect()
    }

    fn field(&self, id: u32, name: &str) -> Option<(u32, Type)> {
        self.layouts.borrow()[id as usize]
            .fields
            .iter()
            .position(|(n, _)| n == name)
            .map(|index| (index as u32, self.layouts.borrow()[id as usize].fields[index].1))
    }

    /// Find or create the anonymous struct layout for a tuple shape.
    fn intern_tuple(&self, fields: Vec<Type>) -> u32 {
        if let Some(&id) = self.tuple_shapes.borrow().get(&fields) {
            return id;
        }
        let mut layouts = self.layouts.borrow_mut();
        let id = layouts.len() as u32;
        let named = fields
            .iter()
            .enumerate()
            .map(|(i, ty)| (i.to_string(), *ty))
            .collect();
        layouts.push(StructLayout {
            name: format!("tuple.{id}"),
            fields: named,
        });
        self.tuple_shapes.borrow_mut().insert(fields, id);
        id
    }

    fn into_layouts(self) -> Vec<StructLayout> {
        self.layouts.into_inner()
    }

    fn variant(&self, name: &str) -> Option<VariantInfo> {
        self.variants.get(name).copied()
    }

    fn is_enum(&self, id: u32) -> bool {
        self.enum_ids.contains(&id)
    }
}

#[derive(Debug)]
pub struct IrError(String);

impl IrError {
    fn new(message: impl Into<String>) -> Self {
        Self(message.into())
    }
}

impl fmt::Display for IrError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for IrError {}

fn scalar_type(ty: &TypeExpr) -> Result<Type, IrError> {
    match ty {
        TypeExpr::Named(name) if name == "int" || name == "int64" => Ok(Type::I64),
        TypeExpr::Named(name) if name == "bool" => Ok(Type::I1),
        TypeExpr::Named(name) if name == "float" || name == "float64" => Ok(Type::F64),
        TypeExpr::Named(name) if name == "string" => Ok(Type::Str),
        TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(name) if name == "int" || name == "int64") => {
            Ok(Type::IntSlice)
        }
        TypeExpr::Array(inner) if matches!(inner.as_ref(), TypeExpr::Named(name) if name == "string") => {
            Ok(Type::StrSlice)
        }
        _ => Err(IrError::new(format!(
            "native IR: type `{ty}` is not in the scalar increment"
        ))),
    }
}

/// Resolve a type expression, allowing user struct names in addition to the
/// scalar set. Struct names bind to their registry index.
fn resolve_type(ty: &TypeExpr, structs: &StructRegistry) -> Result<Type, IrError> {
    match ty {
        TypeExpr::Named(name) => {
            if let Some(&id) = structs.by_name.get(name) {
                return Ok(Type::Struct(id));
            }
            scalar_type(ty)
        }
        TypeExpr::Tuple(elements) => {
            let fields = elements
                .iter()
                .map(|element| {
                    let field_ty = resolve_type(element, structs)?;
                    if !matches!(
                        field_ty,
                        Type::I1 | Type::I32 | Type::I64 | Type::F64 | Type::Str | Type::IntSlice
                    ) {
                        return Err(IrError::new(
                            "native IR: tuple element type is not supported in the current increment",
                        ));
                    }
                    Ok(field_ty)
                })
                .collect::<Result<Vec<_>, _>>()?;
            Ok(Type::Struct(structs.intern_tuple(fields)))
        }
        _ => scalar_type(ty),
    }
}

/// Collect scalar-field struct definitions before lowering any function. Fields
/// must be scalar (`int`/`bool`/`float`); string, slice, and nested-struct
/// fields need drops/nested layout and are deferred with an explicit error.
fn build_structs(program: &Program) -> Result<StructRegistry, IrError> {
    let mut by_name = HashMap::new();
    let mut layouts: Vec<StructLayout> = Vec::new();
    for item in &program.items {
        if let Item::Struct(def) = item {
            if !def.type_params.is_empty() {
                return Err(IrError::new(format!(
                    "native IR: generic struct `{}` is not implemented yet",
                    def.name
                )));
            }
            let id = layouts.len() as u32;
            if by_name.insert(def.name.clone(), id).is_some() {
                return Err(IrError::new(format!(
                    "native IR: duplicate struct `{}`",
                    def.name
                )));
            }
            let mut fields = Vec::with_capacity(def.fields.len());
            for (fname, fty, _default) in &def.fields {
                let ty = scalar_type(fty).map_err(|_| {
                    IrError::new(format!(
                        "native IR: struct field `{}.{fname}` type is not in the scalar-field increment",
                        def.name
                    ))
                })?;
                // Scalars, owned `string`, and owned `[]int` fields are
                // supported; `[]string` and nested-aggregate fields still need
                // recursive layout and are deferred with an explicit error.
                if !matches!(
                    ty,
                    Type::I1 | Type::I32 | Type::I64 | Type::F64 | Type::Str | Type::IntSlice
                ) {
                    return Err(IrError::new(format!(
                        "native IR: struct field `{}.{fname}` of type `{fty}` needs nested layout (deferred)",
                        def.name
                    )));
                }
                fields.push((fname.clone(), ty));
            }
            layouts.push(StructLayout {
                name: def.name.clone(),
                fields,
            });
        }
    }

    // Enums: laid out as `[tag]` followed by each variant's dedicated payload
    // slots (non-overlapping). `int` and owned `string` payloads are supported.
    // Reuses the struct heap block, so ownership/clone/drop come for free; only
    // construction and `match` are enum-specific.
    let mut variants: HashMap<String, VariantInfo> = HashMap::new();
    let mut enum_ids = std::collections::HashSet::new();
    for item in &program.items {
        if let Item::Enum(def) = item {
            if !def.type_params.is_empty() {
                return Err(IrError::new(format!(
                    "native IR: generic enum `{}` is not implemented yet",
                    def.name
                )));
            }
            let id = layouts.len() as u32;
            if by_name.insert(def.name.clone(), id).is_some() {
                return Err(IrError::new(format!(
                    "native IR: duplicate type `{}`",
                    def.name
                )));
            }
            let mut fields = vec![("tag".to_string(), Type::I64)];
            let mut slot_bases = Vec::with_capacity(def.variants.len());
            for variant in &def.variants {
                slot_bases.push(fields.len());
                for (index, field) in variant.fields.iter().enumerate() {
                    let ty = scalar_type(field).map_err(|_| {
                        IrError::new(format!(
                            "native IR: enum `{}` variant `{}` has an unsupported payload",
                            def.name, variant.name
                        ))
                    })?;
                    if !matches!(ty, Type::I64 | Type::Str) {
                        return Err(IrError::new(format!(
                            "native IR: enum `{}` variant `{}` payload must be `int` or `string` in the current increment",
                            def.name, variant.name
                        )));
                    }
                    fields.push((format!("{}_{index}", variant.name), ty));
                }
            }
            layouts.push(StructLayout {
                name: def.name.clone(),
                fields,
            });
            enum_ids.insert(id);
            for (tag, variant) in def.variants.iter().enumerate() {
                if variants
                    .insert(
                        variant.name.clone(),
                        VariantInfo {
                            enum_id: id,
                            tag: tag as i64,
                            arity: variant.fields.len(),
                            slot_base: slot_bases[tag],
                        },
                    )
                    .is_some()
                {
                    return Err(IrError::new(format!(
                        "native IR: duplicate enum variant `{}`",
                        variant.name
                    )));
                }
            }
        }
    }

    Ok(StructRegistry {
        by_name,
        layouts: RefCell::new(layouts),
        tuple_shapes: RefCell::new(HashMap::new()),
        variants,
        enum_ids,
    })
}

pub fn lower(program: &Program) -> Result<Module, IrError> {
    let structs = build_structs(program)?;
    let mut signatures = HashMap::new();
    for item in &program.items {
        match item {
            Item::Fn(function) => {
                let params = function
                    .params
                    .iter()
                    .map(|param| resolve_type(&param.ty, &structs))
                    .collect::<Result<Vec<_>, _>>()?;
                let ret = function
                    .ret
                    .as_ref()
                    .map(|ty| resolve_type(ty, &structs))
                    .transpose()?;
                signatures.insert(function.name.clone(), (params, ret));
            }
            Item::Struct(_) | Item::Enum(_) => {}
            _ => return Err(IrError::new("native IR: non-function top-level item")),
        }
    }
    if !signatures.contains_key("main") {
        return Err(IrError::new("native IR: program has no `main` function"));
    }

    let mut functions = Vec::new();
    for item in &program.items {
        if let Item::Fn(function) = item {
            functions
                .push(FunctionLowerer::new(function, &signatures, &structs)?.lower(function)?);
        }
    }
    Ok(Module {
        functions,
        structs: structs.into_layouts(),
    })
}

struct FunctionLowerer<'a> {
    signatures: &'a HashMap<String, (Vec<Type>, Option<Type>)>,
    structs: &'a StructRegistry,
    function: Function,
    current: BlockId,
    locals: HashMap<String, (Value, Type)>,
    heap_owned: HashMap<String, bool>,
}

impl<'a> FunctionLowerer<'a> {
    fn new(
        source: &FnDef,
        signatures: &'a HashMap<String, (Vec<Type>, Option<Type>)>,
        structs: &'a StructRegistry,
    ) -> Result<Self, IrError> {
        let entry = BlockId(0);
        let ret = signatures[&source.name].1;
        let mut function = Function {
            name: source.name.clone(),
            params: Vec::new(),
            ret,
            blocks: vec![BasicBlock {
                instructions: Vec::new(),
                terminator: None,
            }],
            entry,
            next_value: 0,
        };
        let mut locals = HashMap::new();
        let mut heap_owned = HashMap::new();
        for (index, param) in source.params.iter().enumerate() {
            let ty = signatures[&source.name].0[index];
            let incoming = Value(function.next_value);
            function.next_value += 1;
            function.params.push((param.name.clone(), incoming, ty));
            let slot = Value(function.next_value);
            function.next_value += 1;
            function.blocks[0]
                .instructions
                .push(Inst::Alloca { out: slot, ty });
            function.blocks[0].instructions.push(Inst::Store {
                ptr: slot,
                value: incoming,
            });
            locals.insert(param.name.clone(), (slot, ty));
            if ty.is_heap() {
                // Heap parameters are borrows. The caller retains ownership
                // (for structs the caller passes an owned copy it drops).
                heap_owned.insert(param.name.clone(), false);
            }
        }
        Ok(Self {
            signatures,
            structs,
            function,
            current: entry,
            locals,
            heap_owned,
        })
    }

    fn lower(mut self, source: &FnDef) -> Result<Function, IrError> {
        self.lower_block(&source.body)?;
        if self.block().terminator.is_none() {
            if source.name == "main" {
                let zero = self.const_int(0, Type::I32);
                self.drop_owned_locals();
                self.terminate(Terminator::Return(Some(zero)))?;
                self.function.ret = Some(Type::I32);
            } else if self.function.ret.is_none() {
                self.drop_owned_locals();
                self.terminate(Terminator::Return(None))?;
            } else {
                return Err(IrError::new(format!(
                    "native IR: function `{}` can reach its end",
                    source.name
                )));
            }
        }
        Ok(self.function)
    }

    fn value(&mut self) -> Value {
        let value = Value(self.function.next_value);
        self.function.next_value += 1;
        value
    }

    fn block(&self) -> &BasicBlock {
        &self.function.blocks[self.current.0 as usize]
    }

    fn block_mut(&mut self) -> &mut BasicBlock {
        &mut self.function.blocks[self.current.0 as usize]
    }

    fn new_block(&mut self) -> BlockId {
        let id = BlockId(self.function.blocks.len() as u32);
        self.function.blocks.push(BasicBlock {
            instructions: Vec::new(),
            terminator: None,
        });
        id
    }

    fn emit(&mut self, instruction: Inst) {
        self.block_mut().instructions.push(instruction);
    }

    fn terminate(&mut self, terminator: Terminator) -> Result<(), IrError> {
        if self.block().terminator.is_some() {
            return Err(IrError::new("native IR: block already terminated"));
        }
        self.block_mut().terminator = Some(terminator);
        Ok(())
    }

    fn const_int(&mut self, value: i64, ty: Type) -> Value {
        let out = self.value();
        self.emit(Inst::ConstInt { out, value, ty });
        out
    }

    fn lower_block(&mut self, block: &AstBlock) -> Result<(), IrError> {
        for statement in &block.stmts {
            if self.block().terminator.is_some() {
                break;
            }
            self.lower_stmt(statement)?;
        }
        Ok(())
    }

    fn lower_stmt(&mut self, statement: &Stmt) -> Result<(), IrError> {
        match statement {
            Stmt::Let { name, ty, init, .. } => {
                let (mut value, inferred, mut owned) = self.lower_expr(init)?;
                match inferred {
                    Type::Str | Type::IntSlice | Type::StrSlice => {
                        owned = self.take_bare_string_local(init, owned);
                    }
                    Type::Struct(id) => {
                        // Value-copy semantics: a borrowed source must be cloned
                        // so the binding owns independent storage.
                        if !owned {
                            let clone = self.value();
                            self.emit(Inst::StructClone {
                                out: clone,
                                base: value,
                                struct_id: id,
                            });
                            value = clone;
                            owned = true;
                        }
                    }
                    _ => {}
                }
                let ty = ty
                    .as_ref()
                    .map(|t| resolve_type(t, self.structs))
                    .transpose()?
                    .unwrap_or(inferred);
                if ty != inferred {
                    return Err(IrError::new("native IR: initializer type mismatch"));
                }
                let slot = self.value();
                self.emit(Inst::Alloca { out: slot, ty });
                self.emit(Inst::Store { ptr: slot, value });
                self.locals.insert(name.clone(), (slot, ty));
                if ty.is_heap() {
                    self.heap_owned.insert(name.clone(), owned);
                }
            }
            Stmt::LetMulti { names, init, .. } => {
                // `let a, b = f()` — bind each tuple field to a fresh local.
                let (value, ty, owned) = self.lower_expr(init)?;
                let Type::Struct(id) = ty else {
                    return Err(IrError::new(
                        "native IR: multi-binding requires a tuple result",
                    ));
                };
                if self.structs.field_count(id) != names.len() {
                    return Err(IrError::new("native IR: multi-binding arity mismatch"));
                }
                for (i, name) in names.iter().enumerate() {
                    let field_ty = self.structs.field_type(id, i);
                    let field_value = self.value();
                    self.emit(Inst::StructField {
                        out: field_value,
                        base: value,
                        struct_id: id,
                        index: i as u32,
                        ty: field_ty,
                    });
                    // An owned field is cloned so the binding owns an independent
                    // value; the tuple still owns (and drops) its own field.
                    // Scalars are plain copies.
                    let bound = if field_ty.is_heap() {
                        self.emit_clone(field_value, field_ty)
                    } else {
                        field_value
                    };
                    let slot = self.value();
                    self.emit(Inst::Alloca { out: slot, ty: field_ty });
                    self.emit(Inst::Store {
                        ptr: slot,
                        value: bound,
                    });
                    self.locals.insert(name.clone(), (slot, field_ty));
                    if field_ty.is_heap() {
                        self.heap_owned.insert(name.clone(), true);
                    }
                }
                // Drop the tuple: an owned temp frees now (recursively dropping
                // its owned fields); a borrowed tuple stays with its owner.
                if owned {
                    self.emit(Inst::DropStruct {
                        value,
                        struct_id: id,
                    });
                }
            }
            Stmt::Assign { name, value: rhs } => {
                let (slot, expected) =
                    self.locals.get(name).copied().ok_or_else(|| {
                        IrError::new(format!("native IR: unknown local `{name}`"))
                    })?;
                let (mut value, actual, mut owned) = self.lower_expr(rhs)?;
                match actual {
                    Type::Str | Type::IntSlice | Type::StrSlice => {
                        owned = self.take_bare_string_local(rhs, owned);
                    }
                    Type::Struct(id) => {
                        if !owned {
                            let clone = self.value();
                            self.emit(Inst::StructClone {
                                out: clone,
                                base: value,
                                struct_id: id,
                            });
                            value = clone;
                            owned = true;
                        }
                    }
                    _ => {}
                }
                if actual != expected {
                    return Err(IrError::new("native IR: assignment type mismatch"));
                }
                let consumes_old_slice = matches!(expected, Type::IntSlice | Type::StrSlice)
                    && matches!(rhs, Expr::Call { callee, args }
                        if args.len() == 2
                            && matches!(callee.as_ref(), Expr::Ident(name) if name == "append")
                            && matches!(&args[0], Expr::Ident(source) if source == name));
                if expected.is_heap()
                    && self.heap_owned.get(name) == Some(&true)
                    && !consumes_old_slice
                {
                    let old = self.value();
                    self.emit(Inst::Load {
                        out: old,
                        ptr: slot,
                        ty: expected,
                    });
                    self.emit_drop(old, expected);
                }
                self.emit(Inst::Store { ptr: slot, value });
                if expected.is_heap() {
                    self.heap_owned.insert(name.clone(), owned);
                }
            }
            Stmt::IndexAssign { base, index, value } => {
                let (slice, ty, owned) = self.lower_expr(base)?;
                if ty != Type::IntSlice || owned {
                    return Err(IrError::new(
                        "native IR: index assignment requires borrowed []int local",
                    ));
                }
                let (idx, it, io) = self.lower_expr(index)?;
                let (val, vt, vo) = self.lower_expr(value)?;
                if it != Type::I64 || io || vt != Type::I64 || vo {
                    return Err(IrError::new("native IR: []int index assignment types"));
                }
                self.emit(Inst::SliceStore {
                    slice,
                    index: idx,
                    value: val,
                });
            }
            Stmt::FieldAssign { base, field, value } => {
                let (base_ptr, base_ty, base_owned) = self.lower_expr(base)?;
                let Type::Struct(id) = base_ty else {
                    return Err(IrError::new("native IR: field assignment on non-struct"));
                };
                let (index, field_ty) = self
                    .structs
                    .field(id, field)
                    .ok_or_else(|| IrError::new(format!("native IR: unknown field `{field}`")))?;
                let (val, vt, vo) = self.lower_expr(value)?;
                if vt != field_ty {
                    return Err(IrError::new("native IR: field assignment type mismatch"));
                }
                // Overwriting an owned field drops the previous value first, then
                // stores an owned copy of the new one.
                if field_ty.is_heap() {
                    let old = self.value();
                    self.emit(Inst::StructField {
                        out: old,
                        base: base_ptr,
                        struct_id: id,
                        index,
                        ty: field_ty,
                    });
                    self.emit_drop(old, field_ty);
                }
                let stored = self.own_field_value(val, field_ty, vo)?;
                self.emit(Inst::StructFieldStore {
                    base: base_ptr,
                    struct_id: id,
                    index,
                    value: stored,
                });
                // A field assignment targets an lvalue (a live local), never an
                // owned temporary; nothing to drop.
                debug_assert!(!base_owned);
            }
            Stmt::Expr(expr) => {
                let (value, ty, owned) = self.lower_expr(expr)?;
                if owned {
                    self.emit_drop(value, ty);
                }
            }
            Stmt::Return(value) => {
                let value = match value {
                    Some(expr) => {
                        let (mut value, ty, mut owned) = self.lower_expr(expr)?;
                        if ty.is_heap() {
                            // The function is ending, so moving a bare owned
                            // local out is safe (it is never used afterwards);
                            // a borrow is cloned so the caller receives an owner.
                            owned = self.take_bare_string_local(expr, owned);
                            if !owned {
                                value = self.emit_clone(value, ty);
                            }
                        }
                        Some(value)
                    }
                    None => None,
                };
                self.drop_owned_locals();
                self.terminate(Terminator::Return(value))?;
            }
            Stmt::If {
                init: None,
                cond,
                then_block,
                else_block,
            } => {
                let owned_before = self.heap_owned.clone();
                let (condition, ty, condition_owned) = self.lower_expr(cond)?;
                if condition_owned {
                    return Err(IrError::new("native IR: owned string condition"));
                }
                if ty != Type::I1 {
                    return Err(IrError::new("native IR: condition must be bool"));
                }
                let then_id = self.new_block();
                let else_id = self.new_block();
                let merge_id = self.new_block();
                self.terminate(Terminator::Branch {
                    condition,
                    then_block: then_id,
                    else_block: else_id,
                })?;
                self.current = then_id;
                self.lower_block(then_block)?;
                if self.heap_owned != owned_before {
                    return Err(IrError::new(
                        "native IR: string ownership changes inside a branch",
                    ));
                }
                if self.block().terminator.is_none() {
                    self.terminate(Terminator::Jump(merge_id))?;
                }
                self.current = else_id;
                self.heap_owned = owned_before.clone();
                if let Some(block) = else_block {
                    self.lower_block(block)?;
                }
                if self.block().terminator.is_none() {
                    self.terminate(Terminator::Jump(merge_id))?;
                }
                if self.heap_owned != owned_before {
                    return Err(IrError::new(
                        "native IR: string ownership changes inside a branch",
                    ));
                }
                self.heap_owned = owned_before;
                self.current = merge_id;
            }
            Stmt::While {
                label: None,
                cond,
                body,
            } => {
                let header = self.new_block();
                let body_id = self.new_block();
                let done = self.new_block();
                self.terminate(Terminator::Jump(header))?;
                self.current = header;
                let owned_before = self.heap_owned.clone();
                let (condition, ty, condition_owned) = self.lower_expr(cond)?;
                if condition_owned {
                    return Err(IrError::new("native IR: owned string loop condition"));
                }
                if ty != Type::I1 {
                    return Err(IrError::new("native IR: condition must be bool"));
                }
                self.terminate(Terminator::Branch {
                    condition,
                    then_block: body_id,
                    else_block: done,
                })?;
                self.current = body_id;
                self.lower_block(body)?;
                if self.heap_owned != owned_before {
                    return Err(IrError::new(
                        "native IR: string ownership changes inside a loop",
                    ));
                }
                if self.block().terminator.is_none() {
                    self.terminate(Terminator::Jump(header))?;
                }
                self.current = done;
            }
            _ => return Err(IrError::new("native IR: statement not implemented yet")),
        }
        Ok(())
    }

    fn lower_expr(&mut self, expr: &Expr) -> Result<(Value, Type, bool), IrError> {
        match expr {
            Expr::Int(value) => Ok((self.const_int(*value, Type::I64), Type::I64, false)),
            Expr::Bool(value) => Ok((self.const_int(i64::from(*value), Type::I1), Type::I1, false)),
            Expr::Float(value) => {
                let out = self.value();
                self.emit(Inst::ConstFloat { out, value: *value });
                Ok((out, Type::F64, false))
            }
            Expr::String(bytes) => {
                let out = self.value();
                self.emit(Inst::StringLiteral {
                    out,
                    bytes: bytes.as_bytes().to_vec(),
                });
                Ok((out, Type::Str, false))
            }
            Expr::Array(items) => {
                let mut values = Vec::with_capacity(items.len());
                let mut element_type = None;
                for item in items {
                    let (v, ty, owned) = self.lower_expr(item)?;
                    if element_type.is_none() { element_type = Some(ty); }
                    if Some(ty) != element_type || owned {
                        return Err(IrError::new(
                            "native IR: array literal requires uniform non-owned elements",
                        ));
                    }
                    values.push(v);
                }
                let out = self.value();
                if element_type == Some(Type::I64) {
                    self.emit(Inst::SliceLiteral { out, elements: values });
                    Ok((out, Type::IntSlice, true))
                } else if element_type == Some(Type::Str) {
                    self.emit(Inst::StrSliceLiteral { out, elements: values });
                    Ok((out, Type::StrSlice, true))
                } else {
                    Err(IrError::new("native IR: unsupported array element type"))
                }
            }
            Expr::Ident(name) => {
                // A bare identifier that names a nullary enum variant (`Point`,
                // `None`) constructs that variant.
                if !self.locals.contains_key(name) {
                    if let Some(info) = self.structs.variant(name) {
                        if info.arity == 0 {
                            return self.build_variant(info, &[]);
                        }
                    }
                }
                let (ptr, ty) =
                    self.locals.get(name).copied().ok_or_else(|| {
                        IrError::new(format!("native IR: unknown local `{name}`"))
                    })?;
                let out = self.value();
                self.emit(Inst::Load { out, ptr, ty });
                Ok((out, ty, false))
            }
            Expr::Binary { op, left, right } => {
                let (left, left_ty, left_owned) = self.lower_expr(left)?;
                let (right, right_ty, right_owned) = self.lower_expr(right)?;
                if left_ty != right_ty {
                    return Err(IrError::new("native IR: binary type mismatch"));
                }
                if left_ty == Type::Str {
                    let out = self.value();
                    match op {
                        BinOp::Add => self.emit(Inst::StringConcat { out, left, right }),
                        BinOp::Eq | BinOp::Ne => self.emit(Inst::StringEqual {
                            out,
                            left,
                            right,
                            negated: *op == BinOp::Ne,
                        }),
                        _ => {
                            return Err(IrError::new("native IR: string operation not implemented"))
                        }
                    }
                    if left_owned {
                        self.emit(Inst::DropString { value: left });
                    }
                    if right_owned {
                        self.emit(Inst::DropString { value: right });
                    }
                    return Ok((
                        out,
                        if *op == BinOp::Add {
                            Type::Str
                        } else {
                            Type::I1
                        },
                        *op == BinOp::Add,
                    ));
                }
                let result_ty = match op {
                    BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                        Type::I1
                    }
                    _ => left_ty,
                };
                let out = self.value();
                self.emit(Inst::Binary {
                    out,
                    op: *op,
                    left,
                    right,
                    ty: left_ty,
                });
                Ok((out, result_ty, false))
            }
            Expr::Index { base, index } => {
                let (slice, sty, owned) = self.lower_expr(base)?;
                if sty != Type::IntSlice && sty != Type::StrSlice {
                    return Err(IrError::new("native IR: indexing requires []int"));
                }
                let (idx, ity, iowned) = self.lower_expr(index)?;
                if ity != Type::I64 || iowned {
                    return Err(IrError::new("native IR: slice index must be int"));
                }
                let out = self.value();
                if sty == Type::IntSlice {
                    self.emit(Inst::SliceIndex { out, slice, index: idx });
                } else {
                    self.emit(Inst::StrSliceIndex { out, slice, index: idx });
                }
                if owned {
                    if sty == Type::IntSlice { self.emit(Inst::DropSlice { value: slice }); }
                    else { self.emit(Inst::DropStrSlice { value: slice }); }
                }
                Ok((out, if sty == Type::IntSlice { Type::I64 } else { Type::Str }, sty == Type::StrSlice))
            }
            Expr::Slice {
                base,
                low,
                high,
                max,
            } => {
                let (slice, sty, owned) = self.lower_expr(base)?;
                if sty != Type::IntSlice && sty != Type::StrSlice {
                    return Err(IrError::new("native IR: slicing requires []int"));
                }
                let make_bound =
                    |this: &mut Self, e: Option<&Expr>, default: i64| -> Result<Value, IrError> {
                        if let Some(e) = e {
                            let (v, t, o) = this.lower_expr(e)?;
                            if t != Type::I64 || o {
                                return Err(IrError::new("native IR: slice bound must be int"));
                            }
                            Ok(v)
                        } else {
                            Ok(this.const_int(default, Type::I64))
                        }
                    };
                let lowv = make_bound(self, low.as_deref(), 0)?;
                let highv = make_bound(self, high.as_deref(), -1);
                let highv = highv?;
                let maxv = if let Some(m) = max {
                    Some(make_bound(self, Some(m), -1)?)
                } else {
                    None
                };
                let out = self.value();
                if sty == Type::IntSlice { self.emit(Inst::SliceSlice { out, slice, low: lowv, high: highv, max: maxv }); }
                else { self.emit(Inst::StrSliceSlice { out, slice, low: lowv, high: highv, max: maxv }); }
                if owned {
                    let clone = self.value();
                    if sty == Type::IntSlice { self.emit(Inst::SliceClone { out: clone, slice: out }); self.emit(Inst::DropSlice { value: slice }); self.emit(Inst::DropSlice { value: out }); return Ok((clone, Type::IntSlice, true)); }
                    self.emit(Inst::StrSliceClone { out: clone, slice: out }); self.emit(Inst::DropStrSlice { value: slice }); self.emit(Inst::DropStrSlice { value: out }); return Ok((clone, Type::StrSlice, true));
                }
                // Slicing a borrowed base produces a fresh **owned handle**: for
                // `[]int` a non-owning view header (freed on drop; the shared
                // data is not, because the header's `owned` flag is 0), and for
                // `[]string` an owned element copy. Either way the handle must be
                // dropped, so it is owned here — otherwise its header/copy leaks.
                Ok((out, sty, true))
            }
            Expr::Make { ty, len, cap } => {
                let made_type = scalar_type(ty)?;
                if made_type != Type::IntSlice && made_type != Type::StrSlice {
                    return Err(IrError::new("native IR: unsupported slice type"));
                }
                let len = len
                    .as_deref()
                    .ok_or_else(|| IrError::new("native IR: make requires length"))?;
                let (lv, lt, lo) = self.lower_expr(len)?;
                if lt != Type::I64 || lo {
                    return Err(IrError::new("native IR: make length must int"));
                }
                let cv = if let Some(c) = cap {
                    let (v, t, o) = self.lower_expr(c)?;
                    if t != Type::I64 || o {
                        return Err(IrError::new("native IR: make capacity must int"));
                    }
                    Some(v)
                } else {
                    None
                };
                let out = self.value();
                if made_type == Type::IntSlice { self.emit(Inst::SliceMake { out, len: lv, cap: cv }); }
                else { self.emit(Inst::StrSliceMake { out, len: lv, cap: cv }); }
                Ok((out, made_type, true))
            }
            Expr::Unary { op, expr } => {
                let (value, ty, owned) = self.lower_expr(expr)?;
                if owned {
                    return Err(IrError::new("native IR: unary operation on owned string"));
                }
                let out = self.value();
                self.emit(Inst::Unary {
                    out,
                    op: *op,
                    value,
                    ty,
                });
                Ok((out, ty, false))
            }
            Expr::Call { callee, args } => {
                let Expr::Ident(function) = callee.as_ref() else {
                    return Err(IrError::new("native IR: indirect call"));
                };
                // Enum variant construction with a payload: `Circle(5)`, `Ok(v)`.
                if let Some(info) = self.structs.variant(function) {
                    return self.build_variant(info, args);
                }
                if function == "len" && args.len() == 1 {
                    let (s, t, o) = self.lower_expr(&args[0])?;
                    if t != Type::IntSlice && t != Type::StrSlice {
                        return Err(IrError::new("native IR: len expects []int"));
                    }
                    let out = self.value();
                    if t == Type::IntSlice { self.emit(Inst::SliceLen { out, slice: s }); }
                    else { self.emit(Inst::StrSliceLen { out, slice: s }); }
                    if o {
                        if t == Type::IntSlice { self.emit(Inst::DropSlice { value: s }); }
                        else { self.emit(Inst::DropStrSlice { value: s }); }
                    }
                    return Ok((out, Type::I64, false));
                }
                if function == "append" && args.len() == 2 {
                    let (s, t, _) = self.lower_expr(&args[0])?;
                    if t != Type::IntSlice && t != Type::StrSlice {
                        return Err(IrError::new("native IR: append expects []int"));
                    }
                    let (v, vt, o) = self.lower_expr(&args[1])?;
                    if (t == Type::IntSlice && (vt != Type::I64 || o)) || (t == Type::StrSlice && (vt != Type::Str || o)) {
                        return Err(IrError::new("native IR: append element must int"));
                    }
                    // `append` consumes its slice source — it grows the buffer in
                    // place or reallocs and frees the old one — so an owned source
                    // local must not be dropped again. (The Ident read reports
                    // `owned=false`; consult the local's real ownership instead.)
                    if let Expr::Ident(name) = &args[0] {
                        if self.heap_owned.get(name).copied().unwrap_or(false) {
                            self.heap_owned.insert(name.clone(), false);
                        }
                    }
                    let out = self.value();
                    if t == Type::IntSlice { self.emit(Inst::SliceAppend { out, slice: s, value: v }); return Ok((out, Type::IntSlice, true)); }
                    self.emit(Inst::StrSliceAppend { out, slice: s, value: v });
                    return Ok((out, Type::StrSlice, true));
                }
                if function == "print_int" || function == "print_int64" {
                    if args.len() != 1 {
                        return Err(IrError::new("native IR: print_int arity"));
                    }
                    let (value, ty, owned) = self.lower_expr(&args[0])?;
                    if ty != Type::I64 {
                        return Err(IrError::new("native IR: print_int expects int"));
                    }
                    self.emit(Inst::PrintInt { value });
                    if owned {
                        self.emit(Inst::DropString { value });
                    }
                    return Ok((self.const_int(0, Type::I64), Type::I64, false));
                }
                if function == "print" {
                    if args.len() != 1 {
                        return Err(IrError::new("native IR: print arity"));
                    }
                    let (value, ty, owned) = self.lower_expr(&args[0])?;
                    if ty == Type::Str {
                        self.emit(Inst::PrintString { value });
                    } else if ty == Type::I1 {
                        self.emit(Inst::PrintBool { value });
                    } else {
                        return Err(IrError::new("native IR: print type not implemented"));
                    }
                    if owned {
                        self.emit(Inst::DropString { value });
                    }
                    return Ok((self.const_int(0, Type::I64), Type::I64, false));
                }
                let (params, ret) =
                    self.signatures.get(function).cloned().ok_or_else(|| {
                        IrError::new(format!("native IR: unknown call `{function}`"))
                    })?;
                if params.len() != args.len() {
                    return Err(IrError::new("native IR: call arity mismatch"));
                }
                let mut lowered = Vec::with_capacity(args.len());
                let mut temporary_owned = Vec::new();
                for (argument, expected) in args.iter().zip(params) {
                    let (mut value, actual, owned) = self.lower_expr(argument)?;
                    if actual != expected {
                        return Err(IrError::new("native IR: call type mismatch"));
                    }
                    if let Type::Struct(id) = actual {
                        // By-value argument: hand the callee an owned copy so a
                        // mutating callee cannot observe the caller's storage,
                        // and drop that copy once the call returns.
                        if !owned {
                            let clone = self.value();
                            self.emit(Inst::StructClone {
                                out: clone,
                                base: value,
                                struct_id: id,
                            });
                            value = clone;
                        }
                        temporary_owned.push((value, actual));
                        lowered.push(value);
                        continue;
                    }
                    lowered.push(value);
                    if owned {
                        // An owned string/slice temporary is handed to the callee
                        // as a borrow (a parameter is never freed by the callee)
                        // and dropped here after the call returns — no clone, so
                        // nothing leaks.
                        temporary_owned.push((value, actual));
                    }
                }
                let out = ret.map(|_| self.value());
                self.emit(Inst::Call {
                    out,
                    function: function.clone(),
                    args: lowered,
                    ret,
                });
                for (value, ty) in temporary_owned {
                    self.emit_drop(value, ty);
                }
                match (out, ret) {
                    (Some(value), Some(ty)) => Ok((value, ty, ty.is_heap())),
                    _ => Ok((self.const_int(0, Type::I64), Type::I64, false)),
                }
            }
            Expr::Field { base, field } => {
                let structs = self.structs;
                let (base_ptr, base_ty, base_owned) = self.lower_expr(base)?;
                let Type::Struct(id) = base_ty else {
                    return Err(IrError::new("native IR: field access on non-struct"));
                };
                let (index, field_ty) = structs
                    .field(id, field)
                    .ok_or_else(|| IrError::new(format!("native IR: unknown field `{field}`")))?;
                let out = self.value();
                self.emit(Inst::StructField {
                    out,
                    base: base_ptr,
                    struct_id: id,
                    index,
                    ty: field_ty,
                });
                if base_owned {
                    // The base temporary is freed here (with its owned fields),
                    // so an owned field must be cloned out first; scalars are
                    // already value copies.
                    if field_ty.is_heap() {
                        let cloned = self.emit_clone(out, field_ty);
                        self.emit(Inst::DropStruct {
                            value: base_ptr,
                            struct_id: id,
                        });
                        return Ok((cloned, field_ty, true));
                    }
                    self.emit(Inst::DropStruct {
                        value: base_ptr,
                        struct_id: id,
                    });
                }
                Ok((out, field_ty, false))
            }
            Expr::StructLit {
                name,
                fields,
                update,
            } => {
                let structs = self.structs;
                let id = *structs.by_name.get(name).ok_or_else(|| {
                    IrError::new(format!("native IR: unknown struct `{name}`"))
                })?;
                let nfields = structs.field_count(id);
                let mut slots: Vec<Option<Value>> = vec![None; nfields];
                for (fname, fexpr) in fields {
                    let (index, field_ty) = structs.field(id, fname).ok_or_else(|| {
                        IrError::new(format!("native IR: unknown field `{fname}`"))
                    })?;
                    let (v, vt, vo) = self.lower_expr(fexpr)?;
                    if vt != field_ty {
                        return Err(IrError::new("native IR: struct field type mismatch"));
                    }
                    if slots[index as usize].is_some() {
                        return Err(IrError::new(format!("native IR: duplicate field `{fname}`")));
                    }
                    // The struct owns its fields: move an owned temporary in,
                    // clone a borrow, and reject an owned scalar (impossible).
                    let stored = self.own_field_value(v, field_ty, vo)?;
                    slots[index as usize] = Some(stored);
                }
                if let Some(base) = update {
                    let (base_ptr, base_ty, base_owned) = self.lower_expr(base)?;
                    if base_ty != Type::Struct(id) {
                        return Err(IrError::new("native IR: struct update base type mismatch"));
                    }
                    for i in 0..nfields {
                        if slots[i].is_none() {
                            let field_ty = structs.field_type(id, i);
                            let out = self.value();
                            self.emit(Inst::StructField {
                                out,
                                base: base_ptr,
                                struct_id: id,
                                index: i as u32,
                                ty: field_ty,
                            });
                            // An owned field carried over from the base must be
                            // cloned: the new struct and the base must not share
                            // (and later both free) the same heap value.
                            let value = if field_ty.is_heap() {
                                self.emit_clone(out, field_ty)
                            } else {
                                out
                            };
                            slots[i] = Some(value);
                        }
                    }
                    if base_owned {
                        self.emit(Inst::DropStruct {
                            value: base_ptr,
                            struct_id: id,
                        });
                    }
                }
                let mut field_values = Vec::with_capacity(nfields);
                for (i, slot) in slots.into_iter().enumerate() {
                    match slot {
                        Some(v) => field_values.push(v),
                        None => {
                            let fname = structs.field_name(id, i);
                            return Err(IrError::new(format!("native IR: missing field `{fname}`")));
                        }
                    }
                }
                let out = self.value();
                self.emit(Inst::StructMake {
                    out,
                    struct_id: id,
                    fields: field_values,
                });
                Ok((out, Type::Struct(id), true))
            }
            Expr::StructLitPos { name, values } => {
                let structs = self.structs;
                let id = *structs.by_name.get(name).ok_or_else(|| {
                    IrError::new(format!("native IR: unknown struct `{name}`"))
                })?;
                let field_types: Vec<Type> = structs.field_types(id);
                if values.len() != field_types.len() {
                    return Err(IrError::new("native IR: struct literal arity mismatch"));
                }
                let mut field_values = Vec::with_capacity(values.len());
                for (v_expr, field_ty) in values.iter().zip(field_types) {
                    let (v, vt, vo) = self.lower_expr(v_expr)?;
                    if vt != field_ty {
                        return Err(IrError::new("native IR: struct literal field type mismatch"));
                    }
                    field_values.push(self.own_field_value(v, field_ty, vo)?);
                }
                let out = self.value();
                self.emit(Inst::StructMake {
                    out,
                    struct_id: id,
                    fields: field_values,
                });
                Ok((out, Type::Struct(id), true))
            }
            Expr::Tuple(elements) => {
                // A tuple is an anonymous positional struct. Intern its shape
                // and lower exactly like a positional struct literal.
                let structs = self.structs;
                let mut field_types = Vec::with_capacity(elements.len());
                let mut field_values = Vec::with_capacity(elements.len());
                for element in elements {
                    let (v, vt, vo) = self.lower_expr(element)?;
                    if !matches!(
                        vt,
                        Type::I1 | Type::I32 | Type::I64 | Type::F64 | Type::Str | Type::IntSlice
                    ) {
                        return Err(IrError::new(
                            "native IR: tuple element type is not supported in the current increment",
                        ));
                    }
                    // The tuple owns its elements: move an owned temporary in,
                    // clone a borrow.
                    let stored = self.own_field_value(v, vt, vo)?;
                    field_types.push(vt);
                    field_values.push(stored);
                }
                let id = structs.intern_tuple(field_types);
                let out = self.value();
                self.emit(Inst::StructMake {
                    out,
                    struct_id: id,
                    fields: field_values,
                });
                Ok((out, Type::Struct(id), true))
            }
            Expr::Match { scrutinee, arms } => self.lower_match(scrutinee, arms),
            _ => Err(IrError::new("native IR: expression not implemented yet")),
        }
    }

    /// Lower a `match` on an enum scrutinee. The result is merged through a
    /// stack slot because the IR has no block parameters. The scrutinee is
    /// dropped exactly once on the taken arm (after its payload is read), so an
    /// owned scrutinee neither leaks nor double-frees.
    fn lower_match(
        &mut self,
        scrutinee: &Expr,
        arms: &[crate::ast::MatchArm],
    ) -> Result<(Value, Type, bool), IrError> {
        if arms.is_empty() {
            return Err(IrError::new("native IR: empty match"));
        }
        let (sptr, sty, sowned) = self.lower_expr(scrutinee)?;
        let Type::Struct(enum_id) = sty else {
            return Err(IrError::new("native IR: match scrutinee must be an enum"));
        };
        if !self.structs.is_enum(enum_id) {
            return Err(IrError::new("native IR: match scrutinee must be an enum"));
        }

        // Discriminant (slot 0), then the result slot (type patched once known).
        let tag = self.value();
        self.emit(Inst::StructField {
            out: tag,
            base: sptr,
            struct_id: enum_id,
            index: 0,
            ty: Type::I64,
        });
        let result_slot = self.value();
        let result_block = self.current;
        let result_index = self.block().instructions.len();
        self.emit(Inst::Alloca {
            out: result_slot,
            ty: Type::I64,
        });
        let mut result_ty: Option<Type> = None;

        let merge = self.new_block();
        let saved_locals = self.locals.clone();
        let saved_owned = self.heap_owned.clone();

        let mut test_block = self.current;
        for (i, arm) in arms.iter().enumerate() {
            if arm.guard.is_some() {
                return Err(IrError::new("native IR: match guards are not implemented yet"));
            }
            let (arm_tag, bindings, catch_all) =
                self.resolve_match_pattern(enum_id, &arm.pattern)?;
            let is_last = i == arms.len() - 1;

            self.current = test_block;
            let arm_block = self.new_block();
            let next_block = if is_last { None } else { Some(self.new_block()) };
            if is_last || catch_all {
                // Exhaustiveness (frontend-guaranteed) makes the final arm the
                // fallthrough for whatever tag was not explicitly tested.
                self.terminate(Terminator::Jump(arm_block))?;
            } else {
                let expected = self.const_int(arm_tag, Type::I64);
                let cmp = self.value();
                self.emit(Inst::Binary {
                    out: cmp,
                    op: BinOp::Eq,
                    left: tag,
                    right: expected,
                    ty: Type::I64,
                });
                self.terminate(Terminator::Branch {
                    condition: cmp,
                    then_block: arm_block,
                    else_block: next_block.unwrap(),
                })?;
            }

            // Arm body in its own lexical scope.
            self.current = arm_block;
            self.locals = saved_locals.clone();
            self.heap_owned = saved_owned.clone();
            for (name, slot, ty) in &bindings {
                let payload = self.value();
                self.emit(Inst::StructField {
                    out: payload,
                    base: sptr,
                    struct_id: enum_id,
                    index: *slot,
                    ty: *ty,
                });
                let local = self.value();
                self.emit(Inst::Alloca { out: local, ty: *ty });
                self.emit(Inst::Store {
                    ptr: local,
                    value: payload,
                });
                self.locals.insert(name.clone(), (local, *ty));
            }
            let (body_value, body_ty, _body_owned) = self.lower_expr(&arm.body)?;
            // A heap result would either leak (owned temp) or dangle (a borrow of
            // the scrutinee's payload, freed by the scrutinee drop below), so
            // owned match results are restricted to scalars for now.
            if body_ty.is_heap() {
                return Err(IrError::new(
                    "native IR: owned match results are not implemented yet",
                ));
            }
            match result_ty {
                None => result_ty = Some(body_ty),
                Some(t) if t != body_ty => {
                    return Err(IrError::new("native IR: match arms disagree on result type"));
                }
                _ => {}
            }
            self.emit(Inst::Store {
                ptr: result_slot,
                value: body_value,
            });
            if sowned {
                self.emit(Inst::DropStruct {
                    value: sptr,
                    struct_id: enum_id,
                });
            }
            self.terminate(Terminator::Jump(merge))?;

            if let Some(next) = next_block {
                test_block = next;
            }
        }

        self.locals = saved_locals;
        self.heap_owned = saved_owned;
        self.current = merge;
        let result_ty = result_ty.expect("non-empty match sets a result type");
        if let Inst::Alloca { ty, .. } =
            &mut self.function.blocks[result_block.0 as usize].instructions[result_index]
        {
            *ty = result_ty;
        }
        let out = self.value();
        self.emit(Inst::Load {
            out,
            ptr: result_slot,
            ty: result_ty,
        });
        Ok((out, result_ty, false))
    }

    /// Resolve a match arm pattern to `(variant tag, payload bindings, is
    /// catch-all)`. Payload bindings are `(name, slot index, type)`.
    #[allow(clippy::type_complexity)]
    fn resolve_match_pattern(
        &self,
        enum_id: u32,
        pattern: &Pattern,
    ) -> Result<(i64, Vec<(String, u32, Type)>, bool), IrError> {
        match pattern {
            Pattern::Wildcard => Ok((0, Vec::new(), true)),
            Pattern::Ident(name) => match self.structs.variant(name) {
                // A bare capitalized nullary variant (`Point`, `None`).
                Some(info) if info.enum_id == enum_id && info.arity == 0 => {
                    Ok((info.tag, Vec::new(), false))
                }
                Some(_) => Err(IrError::new("native IR: variant pattern arity mismatch")),
                // Binding the whole scrutinee needs enum-move handling; defer.
                None => Err(IrError::new(
                    "native IR: identifier match patterns are not implemented yet",
                )),
            },
            Pattern::Variant { name, bindings } => {
                let info = self.structs.variant(name).ok_or_else(|| {
                    IrError::new(format!("native IR: unknown variant `{name}`"))
                })?;
                if info.enum_id != enum_id {
                    return Err(IrError::new(
                        "native IR: match variant belongs to a different enum",
                    ));
                }
                if bindings.len() != info.arity {
                    return Err(IrError::new("native IR: variant pattern arity mismatch"));
                }
                let mut binds = Vec::new();
                for (index, binding) in bindings.iter().enumerate() {
                    let slot = (info.slot_base + index) as u32;
                    let slot_ty = self.structs.field_type(enum_id, slot as usize);
                    match binding {
                        Pattern::Ident(name) => binds.push((name.clone(), slot, slot_ty)),
                        Pattern::Wildcard => {}
                        _ => {
                            return Err(IrError::new(
                                "native IR: nested payload patterns are not implemented yet",
                            ))
                        }
                    }
                }
                Ok((info.tag, binds, false))
            }
            _ => Err(IrError::new("native IR: unsupported match pattern")),
        }
    }

    fn take_bare_string_local(&mut self, expr: &Expr, already_owned: bool) -> bool {
        let Expr::Ident(name) = expr else {
            return already_owned;
        };
        if self.heap_owned.get(name) == Some(&true) {
            self.heap_owned.insert(name.clone(), false);
            true
        } else {
            false
        }
    }

    fn drop_owned_locals(&mut self) {
        let owned = self
            .heap_owned
            .iter()
            .filter_map(|(name, owned)| owned.then_some(name.clone()))
            .collect::<Vec<_>>();
        for name in owned {
            if let Some((ptr, ty)) = self.locals.get(&name).copied() {
                let value = self.value();
                self.emit(Inst::Load {
                    out: value,
                    ptr,
                    ty,
                });
                self.emit_drop(value, ty);
                self.heap_owned.insert(name, false);
            }
        }
    }

    /// Emit the type-appropriate drop for an owned heap value.
    fn emit_drop(&mut self, value: Value, ty: Type) {
        match ty {
            Type::Str => self.emit(Inst::DropString { value }),
            Type::IntSlice => self.emit(Inst::DropSlice { value }),
            Type::StrSlice => self.emit(Inst::DropStrSlice { value }),
            Type::Struct(id) => self.emit(Inst::DropStruct {
                value,
                struct_id: id,
            }),
            _ => {}
        }
    }

    /// Take ownership of a value stored into an aggregate field: move an owned
    /// temporary, clone a borrow, and reject an owned scalar (impossible).
    fn own_field_value(
        &mut self,
        value: Value,
        ty: Type,
        owned: bool,
    ) -> Result<Value, IrError> {
        if ty.is_heap() {
            Ok(if owned {
                value
            } else {
                self.emit_clone(value, ty)
            })
        } else if owned {
            Err(IrError::new("native IR: scalar field cannot be owned"))
        } else {
            Ok(value)
        }
    }

    /// Construct an enum variant as a heap block `[tag, payload…, 0…]`.
    fn build_variant(
        &mut self,
        info: VariantInfo,
        args: &[Expr],
    ) -> Result<(Value, Type, bool), IrError> {
        if args.len() != info.arity {
            return Err(IrError::new("native IR: enum variant arity mismatch"));
        }
        let mut payload = Vec::with_capacity(args.len());
        for (index, arg) in args.iter().enumerate() {
            let slot_ty = self.structs.field_type(info.enum_id, info.slot_base + index);
            let (value, ty, owned) = self.lower_expr(arg)?;
            if ty != slot_ty {
                return Err(IrError::new("native IR: enum payload type mismatch"));
            }
            // The enum owns its payload: move an owned temp in, clone a borrow.
            payload.push(self.own_field_value(value, slot_ty, owned)?);
        }
        let out = self.value();
        self.emit(Inst::EnumMake {
            out,
            enum_id: info.enum_id,
            tag: info.tag,
            slot_base: info.slot_base as u32,
            payload,
        });
        Ok((out, Type::Struct(info.enum_id), true))
    }

    /// Emit the type-appropriate clone for a borrowed heap value, returning the
    /// fresh owned value.
    fn emit_clone(&mut self, value: Value, ty: Type) -> Value {
        let out = self.value();
        match ty {
            Type::Str => self.emit(Inst::StringClone { out, value }),
            Type::IntSlice => self.emit(Inst::SliceClone { out, slice: value }),
            Type::StrSlice => self.emit(Inst::StrSliceClone { out, slice: value }),
            Type::Struct(id) => self.emit(Inst::StructClone {
                out,
                base: value,
                struct_id: id,
            }),
            _ => return value,
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    #[test]
    fn lowers_scalar_control_flow() {
        let source = r#"
            fn twice(n: int) -> int { return n + n }
            fn main() {
                let mut i = 0
                while i < 3 { i = i + 1 }
                if i == 3 { print_int(twice(i)) }
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        assert_eq!(module.functions.len(), 2);
        assert!(module.functions.iter().all(|function| {
            function
                .blocks
                .iter()
                .all(|block| block.terminator.is_some())
        }));
    }

    #[test]
    fn makes_string_ownership_explicit() {
        let source = r#"
            fn main() {
                let greeting = "hello, " + "world"
                let moved = greeting
                print(moved)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let instructions = module.functions[0]
            .blocks
            .iter()
            .flat_map(|block| &block.instructions)
            .collect::<Vec<_>>();
        assert!(instructions
            .iter()
            .any(|instruction| matches!(instruction, Inst::StringConcat { .. })));
        assert_eq!(
            instructions
                .iter()
                .filter(|instruction| matches!(instruction, Inst::DropString { .. }))
                .count(),
            1
        );
    }

    #[test]
    fn lowers_scalar_structs_with_copy_semantics() {
        let source = r#"
            struct Point { x: int, y: int }
            fn add(a: Point, b: Point) -> Point {
                return Point { x: a.x + b.x, y: a.y + b.y }
            }
            fn main() {
                let p = Point { x: 3, y: 4 }
                var q = p
                q.x = q.x + 1
                let r = add(p, q)
                print_int(r.x)
                let s = Point { x: 100, ..r }
                print_int(s.y)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        assert_eq!(module.structs.len(), 1);
        assert_eq!(module.structs[0].name, "Point");
        let main = module
            .functions
            .iter()
            .find(|f| f.name == "main")
            .expect("main");
        let instructions = main
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .collect::<Vec<_>>();
        // `var q = p` clones (copy semantics); every owned struct is dropped.
        let makes = instructions
            .iter()
            .filter(|i| matches!(i, Inst::StructMake { .. }))
            .count();
        let clones = instructions
            .iter()
            .filter(|i| matches!(i, Inst::StructClone { .. }))
            .count();
        let drops = instructions
            .iter()
            .filter(|i| matches!(i, Inst::DropStruct { .. }))
            .count();
        assert!(makes >= 1, "expected struct literal lowering");
        assert!(clones >= 1, "expected a copy-on-bind clone for `var q = p`");
        assert!(drops >= 1, "expected owned struct drops");
    }

    #[test]
    fn lowers_scalar_tuples_and_destructuring() {
        let source = r#"
            fn swap(a: int, b: int) -> (int, int) { return (b, a) }
            fn main() {
                let t = (3, 4)
                let a, b = t
                let s = swap(a, b)
                let c, d = s
                print_int(c)
                print_int(d)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        // The `(int,int)` shape is interned exactly once and reused.
        let tuple_layouts = module
            .structs
            .iter()
            .filter(|layout| layout.fields.len() == 2 && layout.fields[0].0 == "0")
            .count();
        assert_eq!(tuple_layouts, 1, "the (int,int) shape must be interned once");
        // Every owned tuple is dropped exactly once (no leaks, no double-free).
        let main = module.functions.iter().find(|f| f.name == "main").unwrap();
        let instructions = main
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .collect::<Vec<_>>();
        let makes = instructions
            .iter()
            .filter(|i| matches!(i, Inst::StructMake { .. }))
            .count();
        let drops = instructions
            .iter()
            .filter(|i| matches!(i, Inst::DropStruct { .. }))
            .count();
        // `t` and `s` are constructed and both dropped after destructuring.
        assert_eq!(makes, 1, "one tuple literal `(3,4)` built in main");
        assert_eq!(drops, 2, "both owned tuples (`t`, `s`) dropped once each");
    }

    #[test]
    fn lowers_enum_match_with_single_scrutinee_drop() {
        let source = r#"
            enum Shape { Circle(int), Rect(int, int), Point }
            fn describe(s: Shape) -> int {
                return match s {
                    Circle(r) => r * r,
                    Rect(w, h) => w * h,
                    Point => 0,
                }
            }
            fn main() {
                let owned = Circle(5)
                print_int(describe(owned))
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        // Shape is laid out as [tag, p0, p1] (max payload arity 2).
        let shape = module
            .structs
            .iter()
            .find(|l| l.name == "Shape")
            .expect("Shape layout");
        // Non-overlapping slots: [tag, Circle_0, Rect_0, Rect_1].
        assert_eq!(shape.fields.len(), 4);
        assert_eq!(shape.fields[0].0, "tag");
        // `describe`'s borrowed param must not be dropped inside the match (the
        // caller owns the copy); its scrutinee is a borrow, so zero drops here.
        let describe = module.functions.iter().find(|f| f.name == "describe").unwrap();
        let describe_drops = describe
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .filter(|i| matches!(i, Inst::DropStruct { .. }))
            .count();
        assert_eq!(describe_drops, 0, "borrowed enum param is not dropped by callee");
        // The match dispatches on the tag with `Eq` compares and Branches.
        let branches = describe
            .blocks
            .iter()
            .filter(|b| matches!(b.terminator, Some(Terminator::Branch { .. })))
            .count();
        assert!(branches >= 2, "expected a tag-dispatch decision chain");
    }

    #[test]
    fn clones_and_drops_string_struct_fields() {
        let source = r#"
            struct User { name: string, age: int }
            fn main() {
                let u = User { name: "Ada", age: 36 }
                var v = u
                v.name = "Grace"
                print(u.name)
                print(v.name)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let user = module.structs.iter().find(|l| l.name == "User").unwrap();
        assert_eq!(user.fields[0].1, Type::Str);
        let main = module.functions.iter().find(|f| f.name == "main").unwrap();
        let insts = main.blocks.iter().flat_map(|b| &b.instructions).collect::<Vec<_>>();
        // `var v = u` deep-copies the struct (StructClone); `v.name = "Grace"`
        // clones the literal and drops the old field; both structs drop at exit.
        assert!(insts.iter().any(|i| matches!(i, Inst::StructClone { .. })));
        // Old-field drop on reassignment + two struct drops at scope exit.
        let drop_strings = insts.iter().filter(|i| matches!(i, Inst::DropString { .. })).count();
        assert!(drop_strings >= 1, "field reassignment must drop the old string");
        let drop_structs = insts.iter().filter(|i| matches!(i, Inst::DropStruct { .. })).count();
        assert_eq!(drop_structs, 2, "both owned Users dropped once at scope exit");
    }

    #[test]
    fn clones_and_drops_int_slice_struct_fields() {
        let source = r#"
            struct Bag { items: []int, tag: int }
            fn main() {
                let b = Bag { items: [1, 2, 3], tag: 9 }
                var c = b
                print_int(c.items[0])
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let bag = module.structs.iter().find(|l| l.name == "Bag").unwrap();
        assert_eq!(bag.fields[0].1, Type::IntSlice);
        let main = module.functions.iter().find(|f| f.name == "main").unwrap();
        let insts = main.blocks.iter().flat_map(|b| &b.instructions).collect::<Vec<_>>();
        // `var c = b` deep-copies (StructClone); both bags drop at scope exit,
        // and the backend recursively clones/drops the []int field.
        assert!(insts.iter().any(|i| matches!(i, Inst::StructClone { .. })));
        assert_eq!(
            insts.iter().filter(|i| matches!(i, Inst::DropStruct { .. })).count(),
            2,
            "both owned Bags dropped once at scope exit"
        );
    }

    #[test]
    fn destructures_owned_tuple_with_per_binding_clone() {
        let source = r#"
            fn split(s: string) -> (string, string) {
                return ("Hi, " + s, "Bye, " + s)
            }
            fn main() {
                let a, b = split("Ada")
                print(a)
                print(b)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let main = module.functions.iter().find(|f| f.name == "main").unwrap();
        let insts = main.blocks.iter().flat_map(|b| &b.instructions).collect::<Vec<_>>();
        // Each owned tuple element is cloned into its binding, and the owned
        // tuple temp is dropped once (recursively freeing its two strings).
        let clones = insts.iter().filter(|i| matches!(i, Inst::StringClone { .. })).count();
        assert_eq!(clones, 2, "both owned string bindings are cloned out");
        assert_eq!(
            insts.iter().filter(|i| matches!(i, Inst::DropStruct { .. })).count(),
            1,
            "the owned tuple temp is dropped exactly once"
        );
    }

    #[test]
    fn enum_string_payloads_use_non_overlapping_slots() {
        let source = r#"
            enum Msg { Text(string), Code(int), Quit }
            fn main() {
                let a = Text("hi")
                var b = a
                print_int(0)
            }
        "#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let msg = module.structs.iter().find(|l| l.name == "Msg").unwrap();
        // [tag, Text_0:string, Code_0:int]; Quit has no payload slots.
        assert_eq!(msg.fields.len(), 3);
        assert_eq!(msg.fields[1].1, Type::Str);
        assert_eq!(msg.fields[2].1, Type::I64);
        // `var b = a` deep-copies the enum (StructClone recurses over the string
        // slot null-safely); both enums drop once at scope exit.
        let main = module.functions.iter().find(|f| f.name == "main").unwrap();
        let insts = main.blocks.iter().flat_map(|b| &b.instructions).collect::<Vec<_>>();
        assert!(insts.iter().any(|i| matches!(i, Inst::EnumMake { .. })));
        assert!(insts.iter().any(|i| matches!(i, Inst::StructClone { .. })));
    }

    #[test]
    fn makes_slice_ownership_explicit() {
        let source = r#"fn main() { let xs = [1, 2]; let n = len(xs); print_int(xs[0]); }"#;
        let tokens = Lexer::new(source).tokenize().unwrap();
        let program = Parser::new(tokens).parse().unwrap();
        let module = lower(&program).unwrap();
        let instructions = module.functions[0]
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .collect::<Vec<_>>();
        assert!(instructions
            .iter()
            .any(|i| matches!(i, Inst::SliceLiteral { .. })));
        assert!(instructions
            .iter()
            .any(|i| matches!(i, Inst::SliceLen { .. })));
        assert!(instructions
            .iter()
            .any(|i| matches!(i, Inst::SliceIndex { .. })));
        assert!(instructions
            .iter()
            .any(|i| matches!(i, Inst::DropSlice { .. })));
    }
}
