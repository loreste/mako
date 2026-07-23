//! Optimizing LLVM emitter for backend-neutral native IR.

use crate::ast::{BinOp, UnaryOp};
use crate::native_ir::{self, Inst, Terminator, Type, Value};
use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::passes::PassBuilderOptions;
use inkwell::targets::{
    CodeModel, FileType, InitializationConfig, RelocMode, Target, TargetMachine, TargetTriple,
};
use inkwell::types::{BasicMetadataTypeEnum, BasicType, BasicTypeEnum};
use inkwell::values::{
    BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, IntValue, PointerValue,
    StructValue,
};
use inkwell::{FloatPredicate, IntPredicate, OptimizationLevel};
use std::collections::HashMap;
use std::fmt;

#[derive(Debug)]
pub struct LlvmError(String);

impl LlvmError {
    fn new(message: impl Into<String>) -> Self {
        Self(message.into())
    }
}

impl fmt::Display for LlvmError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for LlvmError {}

fn llvm_type<'ctx>(context: &'ctx Context, ty: Type) -> BasicTypeEnum<'ctx> {
    match ty {
        Type::I1 => context.bool_type().into(),
        Type::I32 => context.i32_type().into(),
        Type::I64 => context.i64_type().into(),
        Type::F64 => context.f64_type().into(),
        Type::Str => context
            .struct_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                ],
                false,
            )
            .into(),
        Type::IntSlice => context
            .struct_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            )
            .into(),
        // Value ABI matching MakoNativeStrSliceValue { data, len, cap, owned }.
        Type::StrSlice => context
            .struct_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            )
            .into(),
        // Maps, ShareInt, slices (non-int), channels, arenas use pointer ABI.
        Type::MapII
        | Type::MapSI
        | Type::MapSS
        | Type::MapIF
        | Type::MapFI
        | Type::MapIPtr(_)
        | Type::MapSPtr(_)
        | Type::PtrSlice(_)
        | Type::ChanI
        | Type::ChanS
        | Type::ChanF
        | Type::ChanP(_)
        | Type::Arena
        | Type::Nursery
        | Type::Task
        | Type::Opaque
        | Type::FnPtr
        | Type::StructSlice(_)
        | Type::ShareInt
        | Type::FloatSlice
        | Type::ByteSlice
        | Type::BoolSlice => context.ptr_type(Default::default()).into(),
        // A struct value is an owning heap pointer; its concrete layout is
        // carried separately (see `struct_layouts`) for field GEPs.
        Type::Struct(_) => context.ptr_type(Default::default()).into(),
    }
}

/// Build the concrete LLVM struct type for each user struct, indexed by id, so
/// field GEPs and allocation sizes use the target data layout.
fn struct_layouts<'ctx>(
    context: &'ctx Context,
    ir: &native_ir::Module,
) -> Vec<inkwell::types::StructType<'ctx>> {
    ir.structs
        .iter()
        .map(|layout| {
            let fields: Vec<BasicTypeEnum<'ctx>> = layout
                .fields
                .iter()
                .map(|(_, ty)| llvm_type(context, *ty))
                .collect();
            context.struct_type(&fields, false)
        })
        .collect()
}

fn value_name(value: Value) -> String {
    format!("v{}", value.0)
}

pub fn compile_object(source: &crate::ast::Program) -> Result<Vec<u8>, LlvmError> {
    let ir = native_ir::lower(source).map_err(|error| LlvmError::new(error.to_string()))?;
    compile_ir(&ir)
}

pub fn compile_ir(ir: &native_ir::Module) -> Result<Vec<u8>, LlvmError> {
    Target::initialize_native(&InitializationConfig::default())
        .map_err(|error| LlvmError::new(format!("LLVM target initialization failed: {error}")))?;
    let context = Context::create();
    let module = context.create_module("mako");
    let builder = context.create_builder();
    #[cfg(all(target_os = "macos", target_arch = "aarch64"))]
    let triple = TargetTriple::create("arm64-apple-macosx13.0.0");
    #[cfg(all(target_os = "macos", target_arch = "x86_64"))]
    let triple = TargetTriple::create("x86_64-apple-macosx13.0.0");
    #[cfg(not(target_os = "macos"))]
    let triple = TargetMachine::get_default_triple();
    let target = Target::from_triple(&triple)
        .map_err(|error| LlvmError::new(format!("LLVM target lookup failed: {error}")))?;
    let cpu = TargetMachine::get_host_cpu_name();
    let features = TargetMachine::get_host_cpu_features();
    let machine = target
        .create_target_machine(
            &triple,
            cpu.to_str()
                .map_err(|_| LlvmError::new("LLVM CPU name is not UTF-8"))?,
            features
                .to_str()
                .map_err(|_| LlvmError::new("LLVM CPU features are not UTF-8"))?,
            OptimizationLevel::Aggressive,
            RelocMode::PIC,
            CodeModel::Default,
        )
        .ok_or_else(|| LlvmError::new("LLVM could not create the host target machine"))?;
    module.set_triple(&triple);
    module.set_data_layout(&machine.get_target_data().get_data_layout());

    let mut functions = HashMap::new();
    for function in &ir.functions {
        let params: Vec<BasicMetadataTypeEnum<'_>> = function
            .params
            .iter()
            .map(|(_, _, ty)| llvm_type(&context, *ty).into())
            .collect();
        let function_type = match function.ret {
            Some(ty) => llvm_type(&context, ty).fn_type(&params, false),
            None => context.void_type().fn_type(&params, false),
        };
        functions.insert(
            function.name.clone(),
            module.add_function(&function.name, function_type, None),
        );
    }
    let pointer = context.ptr_type(Default::default());
    let printf_type = context.i32_type().fn_type(&[pointer.into()], true);
    let printf = module.add_function("printf", printf_type, None);
    let struct_types = struct_layouts(&context, ir);

    for function in &ir.functions {
        emit_function(
            &context,
            &module,
            &builder,
            function,
            &functions,
            printf,
            &struct_types,
            &ir.structs,
        )?;
    }
    module
        .verify()
        .map_err(|error| LlvmError::new(format!("LLVM module verification failed: {error}")))?;
    module
        .run_passes("default<O3>", &machine, PassBuilderOptions::create())
        .map_err(|error| LlvmError::new(format!("LLVM optimization failed: {error}")))?;
    let object = machine
        .write_to_memory_buffer(&module, FileType::Object)
        .map_err(|error| LlvmError::new(format!("LLVM object emission failed: {error}")))?;
    Ok(object.as_slice().to_vec())
}

fn emit_function<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    function: &native_ir::Function,
    functions: &HashMap<String, FunctionValue<'ctx>>,
    printf: FunctionValue<'ctx>,
    struct_types: &[inkwell::types::StructType<'ctx>],
    layouts: &[native_ir::StructLayout],
) -> Result<(), LlvmError> {
    let llvm_function = functions[&function.name];
    debug_assert_eq!(function.entry.0, 0);
    let blocks: Vec<BasicBlock<'ctx>> = function
        .blocks
        .iter()
        .enumerate()
        .map(|(index, _)| context.append_basic_block(llvm_function, &format!("b{index}")))
        .collect();
    let mut values = HashMap::new();
    let mut slice_result_slots = HashMap::new();
    builder.position_at_end(blocks[0]);
    for block in &function.blocks {
        for instruction in &block.instructions {
            let out_ty = match instruction {
                Inst::SliceLiteral { out, .. }
                | Inst::SliceMake { out, .. }
                | Inst::SliceAppend { out, .. }
                | Inst::SliceSlice { out, .. }
                | Inst::SliceClone { out, .. } => Some((*out, Type::IntSlice)),
                Inst::StrSliceLiteral { out, .. }
                | Inst::StrSliceMake { out, .. }
                | Inst::StrSliceAppend { out, .. }
                | Inst::StrSliceSlice { out, .. }
                | Inst::StrSliceClone { out, .. } => Some((*out, Type::StrSlice)),
                _ => None,
            };
            if let Some((out, ty)) = out_ty {
                let slot = builder
                    .build_alloca(
                        llvm_type(context, ty),
                        &format!("{}.result", value_name(out)),
                    )
                    .map_err(builder_error)?;
                slice_result_slots.insert(out, slot);
            }
        }
    }
    for (index, (_, value, _)) in function.params.iter().enumerate() {
        values.insert(
            *value,
            llvm_function
                .get_nth_param(index as u32)
                .ok_or_else(|| LlvmError::new("LLVM function parameter missing"))?,
        );
    }

    for (index, block) in function.blocks.iter().enumerate() {
        builder.position_at_end(blocks[index]);
        for instruction in &block.instructions {
            emit_instruction(
                context,
                module,
                builder,
                instruction,
                functions,
                printf,
                &mut values,
                &slice_result_slots,
                struct_types,
                layouts,
            )?;
        }
        match block
            .terminator
            .as_ref()
            .ok_or_else(|| LlvmError::new("native IR block has no terminator"))?
        {
            Terminator::Jump(target) => {
                builder
                    .build_unconditional_branch(blocks[target.0 as usize])
                    .map_err(builder_error)?;
            }
            Terminator::Branch {
                condition,
                then_block,
                else_block,
            } => {
                builder
                    .build_conditional_branch(
                        values[condition].into_int_value(),
                        blocks[then_block.0 as usize],
                        blocks[else_block.0 as usize],
                    )
                    .map_err(builder_error)?;
            }
            Terminator::Return(value) => {
                let value: Option<&dyn BasicValue<'ctx>> = value
                    .as_ref()
                    .map(|value| &values[value] as &dyn BasicValue<'ctx>);
                builder.build_return(value).map_err(builder_error)?;
            }
        }
    }
    Ok(())
}

fn emit_instruction<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    instruction: &Inst,
    functions: &HashMap<String, FunctionValue<'ctx>>,
    printf: FunctionValue<'ctx>,
    values: &mut HashMap<Value, BasicValueEnum<'ctx>>,
    slice_result_slots: &HashMap<Value, PointerValue<'ctx>>,
    struct_types: &[inkwell::types::StructType<'ctx>],
    layouts: &[native_ir::StructLayout],
) -> Result<(), LlvmError> {
    match instruction {
        Inst::ConstInt { out, value, ty } => {
            let value = match ty {
                Type::I1 => context.bool_type().const_int(*value as u64, false).into(),
                Type::I32 => context.i32_type().const_int(*value as u64, true).into(),
                Type::I64 => context.i64_type().const_int(*value as u64, true).into(),
                Type::F64 => return Err(LlvmError::new("integer constant has float type")),
                Type::Str
                | Type::IntSlice
                | Type::FloatSlice
                | Type::ByteSlice
                | Type::StrSlice
                | Type::MapII
                | Type::MapSI
                | Type::MapSS
                | Type::MapIF
                | Type::MapFI
                | Type::MapIPtr(_)
                | Type::MapSPtr(_)
        | Type::PtrSlice(_)
                | Type::ChanI
                | Type::ChanS
                | Type::ChanF
                | Type::ChanP(_)
                | Type::Arena
                | Type::Nursery
                | Type::Task
                | Type::Opaque
                | Type::FnPtr
                | Type::StructSlice(_)
                | Type::BoolSlice
                | Type::ShareInt
                | Type::Struct(_) => {
                    // Null pointer constants for pointer-ABI types.
                    if *value == 0 {
                        context
                            .ptr_type(Default::default())
                            .const_null()
                            .into()
                    } else {
                        return Err(LlvmError::new("integer constant has aggregate type"));
                    }
                }
            };
            values.insert(*out, value);
        }
        Inst::ConstFloat { out, value } => {
            values.insert(*out, context.f64_type().const_float(*value).into());
        }
        Inst::Alloca { out, ty } => {
            let value = builder
                .build_alloca(llvm_type(context, *ty), &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, value.into());
        }
        Inst::Load { out, ptr, ty } => {
            let value = builder
                .build_load(
                    llvm_type(context, *ty),
                    values[ptr].into_pointer_value(),
                    &value_name(*out),
                )
                .map_err(builder_error)?;
            values.insert(*out, value);
        }
        Inst::Store { ptr, value } => {
            builder
                .build_store(values[ptr].into_pointer_value(), values[value])
                .map_err(builder_error)?;
        }
        Inst::Binary {
            out,
            op,
            left,
            right,
            ty,
        } => {
            let value = emit_binary(builder, *op, values[left], values[right], *ty, *out)?;
            values.insert(*out, value);
        }
        Inst::Unary { out, op, value, ty } => {
            let name = value_name(*out);
            let result = match (op, ty) {
                (UnaryOp::Neg, Type::I64) => builder
                    .build_int_neg(values[value].into_int_value(), &name)
                    .map(BasicValueEnum::from),
                (UnaryOp::Neg, Type::F64) => builder
                    .build_float_neg(values[value].into_float_value(), &name)
                    .map(BasicValueEnum::from),
                (UnaryOp::Not | UnaryOp::BitNot, Type::I1 | Type::I64) => builder
                    .build_not(values[value].into_int_value(), &name)
                    .map(BasicValueEnum::from),
                _ => return Err(LlvmError::new("LLVM unary operation is not implemented")),
            }
            .map_err(builder_error)?;
            values.insert(*out, result);
        }
        Inst::Cast {
            out,
            value,
            from,
            to,
        } => {
            let name = value_name(*out);
            let result = match (from, to) {
                (Type::I64, Type::F64) => builder
                    .build_signed_int_to_float(
                        values[value].into_int_value(),
                        context.f64_type(),
                        &name,
                    )
                    .map(BasicValueEnum::from)
                    .map_err(builder_error)?,
                (Type::F64, Type::I64) => builder
                    .build_float_to_signed_int(
                        values[value].into_float_value(),
                        context.i64_type(),
                        &name,
                    )
                    .map(BasicValueEnum::from)
                    .map_err(builder_error)?,
                (Type::I64, Type::I1) => builder
                    .build_int_compare(
                        IntPredicate::NE,
                        values[value].into_int_value(),
                        context.i64_type().const_zero(),
                        &name,
                    )
                    .map(BasicValueEnum::from)
                    .map_err(builder_error)?,
                (Type::I1, Type::I64) => builder
                    .build_int_z_extend(
                        values[value].into_int_value(),
                        context.i64_type(),
                        &name,
                    )
                    .map(BasicValueEnum::from)
                    .map_err(builder_error)?,
                (Type::F64, Type::I1) => builder
                    .build_float_compare(
                        FloatPredicate::ONE,
                        values[value].into_float_value(),
                        context.f64_type().const_float(0.0),
                        &name,
                    )
                    .map(BasicValueEnum::from)
                    .map_err(builder_error)?,
                (Type::I1, Type::F64) => {
                    let w = builder
                        .build_int_z_extend(
                            values[value].into_int_value(),
                            context.i64_type(),
                            "bool.i64",
                        )
                        .map_err(builder_error)?;
                    builder
                        .build_signed_int_to_float(w, context.f64_type(), &name)
                        .map(BasicValueEnum::from)
                        .map_err(builder_error)?
                }
                _ => return Err(LlvmError::new("LLVM backend: unsupported cast")),
            };
            values.insert(*out, result);
        }
        Inst::Call {
            out,
            function,
            args,
            ret,
        } => {
            // Hot map[int]int path: inline open-addressing probe (avoids 200k
            // external calls on the map bench). Rehash still calls runtime.
            if function == "mako_native_map_ii_set_ptr" && args.len() == 3 {
                return emit_map_ii_set_inline(
                    context,
                    module,
                    builder,
                    values[&args[0]].into_pointer_value(),
                    values[&args[1]].into_int_value(),
                    values[&args[2]].into_int_value(),
                );
            }
            if function == "mako_native_map_ii_get_ptr" && args.len() == 2 {
                if let Some(outv) = out {
                    let got = emit_map_ii_get_inline(
                        context,
                        builder,
                        values[&args[0]].into_pointer_value(),
                        values[&args[1]].into_int_value(),
                        &value_name(*outv),
                    )?;
                    values.insert(*outv, got.into());
                }
                return Ok(());
            }
            // Pointer-ABI runtime names (`*_ptr`) map to value-ABI entry points
            // when the LLVM string ABI is a struct (not a header pointer).
            let function_name = match function.as_str() {
                "mako_native_parse_int_ptr" => "mako_native_parse_int",
                "mako_native_path_join_ptr" => "mako_native_path_join",
                "mako_native_str_len_ptr" => "mako_native_str_len",
                "mako_native_arg_get_ptr" => "mako_native_arg_get",
                "mako_native_env_get_ptr" => "mako_native_env_get",
                "mako_native_env_set_ptr" => "mako_native_env_set",
                "mako_native_read_file_ptr" => "mako_native_read_file",
                "mako_native_write_file_ptr" => "mako_native_write_file",
                "mako_native_append_file_ptr" => "mako_native_append_file",
                "mako_native_atomic_write_file_ptr" => "mako_native_atomic_write_file",
                "mako_native_assert_eq_str_ptr" => "mako_native_assert_eq_str",
                "mako_native_path_size_ptr" => "mako_native_path_size",
                "mako_native_base64_encode_ptr" => "mako_native_base64_encode",
                "mako_native_base64_decode_ptr" => "mako_native_base64_decode",
                "mako_native_str_repeat_ptr" => "mako_native_str_repeat",
                "mako_native_str_contains_ptr" => "mako_native_str_contains",
                "mako_native_str_has_prefix_ptr" => "mako_native_str_has_prefix",
                "mako_native_str_has_suffix_ptr" => "mako_native_str_has_suffix",
                "mako_native_mkdir_ptr" => "mako_native_mkdir",
                "mako_native_mkdir_all_ptr" => "mako_native_mkdir_all",
                "mako_native_rmdir_ptr" => "mako_native_rmdir",
                "mako_native_remove_all_ptr" => "mako_native_remove_all",
                "mako_native_file_exists_ptr" => "mako_native_file_exists",
                "mako_native_remove_file_ptr" => "mako_native_remove_file",
                // Byte→string: LLVM uses value-ABI string returns.
                "mako_native_string_from_bytes_ptr" => "mako_native_string_from_bytes_val",
                "mako_native_bytes_from_string_ptr" => "mako_native_bytes_from_string_val",
                "mako_native_rune_count_ptr" => "mako_native_rune_count",
                "mako_native_str_get_ptr" => "mako_native_str_get",
                "mako_native_str_slice_ptr" => "mako_native_str_slice",
                "mako_native_utf8_decode_rune_ptr" => "mako_native_utf8_decode_rune",
                "mako_native_utf8_decode_width_ptr" => "mako_native_utf8_decode_width",
                // Map string returns: LLVM uses value-ABI string structs.
                "mako_native_map_ss_get_ptr" => "mako_native_map_ss_get_val",
                "mako_native_map_ss_slot_key_ptr" => "mako_native_map_ss_slot_key_val",
                "mako_native_map_ss_slot_val_ptr" => "mako_native_map_ss_slot_val_val",
                "mako_native_map_si_slot_key_ptr" => "mako_native_map_si_slot_key_val",
                other => other,
            };
            // Pointer-ABI map[string]* helpers expect *MakoNativeString for
            // string keys (and MapSS set values). Value-ABI `*_val` helpers
            // take/return MakoNativeString by value — no boxing.
            // Pointer-ABI string helpers: box LLVM value-ABI strings to stack slots.
            // Prefer value-ABI remaps in the match above for hot I/O/fs helpers.
            let needs_str_box = !function_name.ends_with("_val")
                && (function_name.contains("map_si_")
                    || function_name.contains("map_ss_")
                    || function_name.contains("tcp_connect")
                    || function_name.contains("tcp_listen_addr")
                    || function_name.contains("http_get")
                    || function_name.contains("http_serve")
                    || function_name.contains("fmt_")
                    || function_name.contains("print_raw")
                    || function_name.contains("log_info")
                    || function_name.contains("arena_text")
                    || function_name.contains("parse_int")
                    || function_name.contains("json_")
                    || function_name.contains("regex_"));
            // []int is value-ABI on LLVM but many runtime helpers take *header.
            let needs_int_slice_box = function_name.contains("int_slice_copy");
            let mut arguments: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
            for (i, arg) in args.iter().enumerate() {
                let v = values[arg];
                // Box value-ABI strings for pointer-ABI callees.
                if needs_str_box && v.is_struct_value() {
                    // Skip arg0 when it's a map/chan pointer (not a string).
                    let skip = i == 0
                        && (function_name.contains("map_si_")
                            || function_name.contains("map_ss_")
                            || function_name.contains("arena_text"));
                    if !skip {
                        let slot = builder
                            .build_alloca(llvm_type(context, Type::Str), "str.arg")
                            .map_err(builder_error)?;
                        builder.build_store(slot, v).map_err(builder_error)?;
                        arguments.push(slot.into());
                        continue;
                    }
                }
                if needs_int_slice_box && v.is_struct_value() {
                    let slot = builder
                        .build_alloca(llvm_type(context, Type::IntSlice), "slice.copy.arg")
                        .map_err(builder_error)?;
                    builder.build_store(slot, v).map_err(builder_error)?;
                    arguments.push(slot.into());
                } else {
                    arguments.push(v.into());
                }
            }
            let llvm_fn = if let Some(f) = functions.get(function_name) {
                *f
            } else {
                let ptr = context.ptr_type(Default::default());
                let i64t = context.i64_type();
                let params: Vec<BasicMetadataTypeEnum> = if needs_str_box {
                    if function_name.contains("make") {
                        vec![i64t.into()]
                    } else if function_name.contains("set") {
                        if function_name.contains("map_ss_") {
                            // (map*, key*, val*)
                            vec![ptr.into(), ptr.into(), ptr.into()]
                        } else {
                            // map_si set: (map*, key*, i64)
                            vec![ptr.into(), ptr.into(), i64t.into()]
                        }
                    } else if function_name.contains("len")
                        || function_name.contains("drop")
                        || function_name.contains("clone")
                        || function_name.contains("cap")
                    {
                        vec![ptr.into()]
                    } else if function_name.contains("slot_") {
                        vec![ptr.into(), i64t.into()]
                    } else {
                        // get_ptr / has / delete with boxed key pointer
                        vec![ptr.into(), ptr.into()]
                    }
                } else {
                    arguments
                        .iter()
                        .map(|a| match a {
                            BasicMetadataValueEnum::PointerValue(p) => p.get_type().into(),
                            BasicMetadataValueEnum::IntValue(i) => i.get_type().into(),
                            BasicMetadataValueEnum::FloatValue(f) => f.get_type().into(),
                            BasicMetadataValueEnum::StructValue(s) => s.get_type().into(),
                            _ => ptr.into(),
                        })
                        .collect()
                };
                let fty = if function_name.contains("maps_keys")
                    || function_name.contains("arena_ints")
                {
                    // C returns *MakoNativeIntSlice; convert to value ABI after call.
                    ptr.fn_type(&params, false)
                } else if ret == &Some(Type::Str)
                    && function_name.ends_with("_ptr")
                    && !function_name.contains("map_ss_get")
                {
                    // Bridge string returns are *MakoNativeString (pointer ABI).
                    ptr.fn_type(&params, false)
                } else if matches!(
                    ret,
                    Some(Type::Str) if function_name.contains("uuid")
                        || function_name.contains("http_get")
                        || function_name.contains("sprintf")
                        || function_name.contains("errorf")
                        || function_name.contains("empty_settings")
                        || function_name.contains("arena_text")
                ) {
                    ptr.fn_type(&params, false)
                } else {
                    match ret {
                        Some(ty) => llvm_type(context, *ty).fn_type(&params, false),
                        None => context.void_type().fn_type(&params, false),
                    }
                };
                external_function(module, function_name, fty)
            };
            let call = builder
                .build_call(llvm_fn, &arguments, "call")
                .map_err(builder_error)?;
            if let (Some(out), Some(rty)) = (out, ret) {
                let value = call
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("LLVM call did not return a value"))?;
                // Cranelift keeps []int / strings as heap pointers; LLVM uses
                // value-ABI structs. Convert pointer returns when needed.
                let value = if *rty == Type::IntSlice
                    && (function_name.contains("maps_keys")
                        || function_name.contains("arena_ints"))
                    && value.is_pointer_value()
                {
                    let hdr = value.into_pointer_value();
                    let loaded = builder
                        .build_load(llvm_type(context, Type::IntSlice), hdr, "maps_keys.load")
                        .map_err(builder_error)?;
                    let free_fn = external_function(
                        module,
                        "free",
                        context
                            .void_type()
                            .fn_type(&[context.ptr_type(Default::default()).into()], false),
                    );
                    builder
                        .build_call(free_fn, &[hdr.into()], "maps_keys.free_hdr")
                        .map_err(builder_error)?;
                    loaded
                } else if *rty == Type::Str && value.is_pointer_value() {
                    // Bridge returns *MakoNativeString; convert to value ABI.
                    let hdr = value.into_pointer_value();
                    let loaded = builder
                        .build_load(llvm_type(context, Type::Str), hdr, "str.ptr.load")
                        .map_err(builder_error)?;
                    let free_fn = external_function(
                        module,
                        "free",
                        context
                            .void_type()
                            .fn_type(&[context.ptr_type(Default::default()).into()], false),
                    );
                    builder
                        .build_call(free_fn, &[hdr.into()], "str.hdr.free")
                        .map_err(builder_error)?;
                    loaded
                } else {
                    value
                };
                values.insert(*out, value);
            }
        }
        Inst::FuncAddr { out, function } => {
            let f = functions.get(function.as_str()).ok_or_else(|| {
                LlvmError::new(format!("LLVM FuncAddr unknown function `{function}`"))
            })?;
            values.insert(*out, f.as_global_value().as_pointer_value().into());
        }
        Inst::CallIndirect {
            out,
            callee,
            args,
            param_tys,
            ret,
        } => {
            let arg_tys: Vec<BasicTypeEnum> = param_tys
                .iter()
                .map(|t| llvm_type(context, *t))
                .collect();
            let fn_ty = match ret {
                Some(r) => {
                    let rt: BasicTypeEnum = llvm_type(context, *r);
                    rt.fn_type(&arg_tys.iter().map(|t| (*t).into()).collect::<Vec<_>>(), false)
                }
                None => context.void_type().fn_type(
                    &arg_tys.iter().map(|t| (*t).into()).collect::<Vec<_>>(),
                    false,
                ),
            };
            let callee_v = values[callee].into_pointer_value();
            let call_args: Vec<_> = args.iter().map(|a| values[a].into()).collect();
            let call = builder
                .build_indirect_call(fn_ty, callee_v, &call_args, "icall")
                .map_err(builder_error)?;
            if let Some(out) = out {
                let result = call
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("LLVM indirect call returned no value"))?;
                values.insert(*out, result);
            }
        }
        Inst::PrintInt { value } => {
            let format = builder
                .build_global_string_ptr("%lld\n", "print_int_format")
                .map_err(builder_error)?;
            builder
                .build_call(
                    printf,
                    &[format.as_pointer_value().into(), values[value].into()],
                    "printf",
                )
                .map_err(builder_error)?;
        }
        Inst::PrintBool { value } => {
            let true_text = builder
                .build_global_string_ptr("true", "bool_true")
                .map_err(builder_error)?;
            let false_text = builder
                .build_global_string_ptr("false", "bool_false")
                .map_err(builder_error)?;
            let text = builder
                .build_select(
                    values[value].into_int_value(),
                    true_text.as_pointer_value(),
                    false_text.as_pointer_value(),
                    "bool_text",
                )
                .map_err(builder_error)?;
            let puts = external_function(
                module,
                "puts",
                context
                    .i32_type()
                    .fn_type(&[context.ptr_type(Default::default()).into()], false),
            );
            builder
                .build_call(puts, &[text.into()], "puts")
                .map_err(builder_error)?;
        }
        Inst::StringLiteral { out, bytes } => {
            let constant = context.const_string(bytes, true);
            let global = module.add_global(constant.get_type(), None, &format!("str.{}", out.0));
            global.set_initializer(&constant);
            global.set_constant(true);
            // High bit of len marks immortal static payload (clone free, drop no-op).
            let static_len = (bytes.len() as u64) | (1u64 << 63);
            let value = pack_string(
                context,
                builder,
                global.as_pointer_value(),
                context.i64_type().const_int(static_len, false),
                &value_name(*out),
            )?;
            values.insert(*out, value.into());
        }
        Inst::StringClone { out, value } => {
            let string_type = llvm_type(context, Type::Str);
            let clone = external_function(
                module,
                "mako_native_string_clone",
                string_type.fn_type(&[string_type.into()], false),
            );
            let cloned = builder
                .build_call(clone, &[values[value].into()], &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("string clone returned void"))?;
            values.insert(*out, cloned);
        }
        Inst::StringConcat { out, left, right } => {
            let string_type = llvm_type(context, Type::Str);
            let concat = external_function(
                module,
                "mako_native_string_concat",
                string_type.fn_type(&[string_type.into(), string_type.into()], false),
            );
            let concatenated = builder
                .build_call(
                    concat,
                    &[values[left].into(), values[right].into()],
                    &value_name(*out),
                )
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("string concat returned void"))?;
            values.insert(*out, concatenated);
        }
        Inst::StringEqual {
            out,
            left,
            right,
            negated,
        } => {
            let string_type = llvm_type(context, Type::Str);
            let equal_function = external_function(
                module,
                "mako_native_string_equal",
                context
                    .i32_type()
                    .fn_type(&[string_type.into(), string_type.into()], false),
            );
            let equal = builder
                .build_call(
                    equal_function,
                    &[values[left].into(), values[right].into()],
                    "string.equal.i32",
                )
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("string equality returned void"))?
                .into_int_value();
            let equal = builder
                .build_int_compare(
                    IntPredicate::NE,
                    equal,
                    context.i32_type().const_zero(),
                    "string.equal",
                )
                .map_err(builder_error)?;
            let result = if *negated {
                builder
                    .build_not(equal, "string.not_equal")
                    .map_err(builder_error)?
            } else {
                equal
            };
            values.insert(*out, result.into());
        }
        Inst::PrintString { value } => {
            let string_type = llvm_type(context, Type::Str);
            let print = external_function(
                module,
                "mako_native_string_print",
                context.void_type().fn_type(&[string_type.into()], false),
            );
            builder
                .build_call(print, &[values[value].into()], "print_string")
                .map_err(builder_error)?;
        }
        Inst::DropString { value } => {
            let string_type = llvm_type(context, Type::Str);
            let drop = external_function(
                module,
                "mako_native_string_drop",
                context.void_type().fn_type(&[string_type.into()], false),
            );
            builder
                .build_call(drop, &[values[value].into()], "drop_string")
                .map_err(builder_error)?;
        }
        Inst::StringLen { out, value } => {
            // Value ABI: string is { data, len }; extract field 1 and clear the
            // immortal/static high bit so callers see the true byte length.
            let raw = builder
                .build_extract_value(values[value].into_struct_value(), 1, &format!("{}.raw", value_name(*out)))
                .map_err(builder_error)?
                .into_int_value();
            let mask = context.i64_type().const_int(!(1u64 << 63), false);
            let len = builder
                .build_and(raw, mask, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, len.into());
        }
        Inst::NullHeap { out, ty } => {
            let value = match ty {
                Type::Str => pack_string(
                    context,
                    builder,
                    context.ptr_type(Default::default()).const_null(),
                    context.i64_type().const_zero(),
                    &value_name(*out),
                )?
                .into(),
                Type::IntSlice | Type::StrSlice => {
                    let st = llvm_type(context, *ty).into_struct_type();
                    let mut agg = st.get_undef();
                    for i in 0..4 {
                        let zero: BasicValueEnum = if i == 0 {
                            context.ptr_type(Default::default()).const_null().into()
                        } else {
                            context.i64_type().const_zero().into()
                        };
                        agg = builder
                            .build_insert_value(agg, zero, i, &format!("null.{}", i))
                            .map_err(builder_error)?
                            .into_struct_value();
                    }
                    agg.into()
                }
                Type::Struct(_)
                | Type::MapII
                | Type::MapSI
                | Type::MapSS
                | Type::MapIF
                | Type::MapFI
                | Type::ChanI
        | Type::ChanS
                | Type::Arena
                | Type::Nursery
                | Type::ShareInt
                | Type::FloatSlice
                | Type::ByteSlice
                | Type::BoolSlice => {
                    context.ptr_type(Default::default()).const_null().into()
                }
                _ => {
                    return Err(LlvmError::new(
                        "LLVM backend: NullHeap only applies to heap types",
                    ))
                }
            };
            values.insert(*out, value);
        }
        Inst::IntToString { out, value } => {
            let string_type = llvm_type(context, Type::Str);
            let f = external_function(
                module,
                "mako_native_int_to_string",
                string_type.fn_type(&[context.i64_type().into()], false),
            );
            let s = builder
                .build_call(f, &[values[value].into()], &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("int_to_string returned void"))?;
            values.insert(*out, s);
        }
        Inst::BoolToString { out, value } => {
            let string_type = llvm_type(context, Type::Str);
            let f = external_function(
                module,
                "mako_native_bool_to_string",
                string_type.fn_type(&[context.i64_type().into()], false),
            );
            // Bool is i1 in IR; widen to i64 for the C ABI.
            let as_i64 = builder
                .build_int_z_extend(
                    values[value].into_int_value(),
                    context.i64_type(),
                    "bool.i64",
                )
                .map_err(builder_error)?;
            let s = builder
                .build_call(f, &[as_i64.into()], &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("bool_to_string returned void"))?;
            values.insert(*out, s);
        }
        Inst::SliceMake { out, len, cap } => {
            let capacity = cap.map(|value| values[&value]).unwrap_or(values[len]);
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let result = call_slice_return(
                context,
                module,
                builder,
                slice_result_slots[out],
                "mako_native_int_slice_make",
                function_type,
                &[values[len].into(), capacity.into()],
                &value_name(*out),
            )?;
            values.insert(*out, result);
        }
        Inst::SliceLiteral { out, elements } => {
            let count = context.i64_type().const_int(elements.len() as u64, false);
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let result = call_slice_return(
                context,
                module,
                builder,
                slice_result_slots[out],
                "mako_native_int_slice_make",
                function_type,
                &[count.into(), count.into()],
                &value_name(*out),
            )?;
            let set = external_function(
                module,
                "mako_native_int_slice_set",
                context.void_type().fn_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                ),
            );
            let parts = slice_parts(builder, result.into_struct_value())?;
            for (index, element) in elements.iter().enumerate() {
                let mut args = parts.clone();
                args.push(context.i64_type().const_int(index as u64, false).into());
                args.push(values[element].into());
                builder
                    .build_call(set, &args, "slice.literal.set")
                    .map_err(builder_error)?;
            }
            values.insert(*out, result);
        }
        Inst::SliceLen { out, slice } => {
            // Inline: len is field 1 of the value-ABI header — no runtime call.
            let sv = values[slice].into_struct_value();
            let len = builder
                .build_extract_value(sv, 1, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, len);
        }
        Inst::SliceIndex { out, slice, index } => {
            // Inline load with bounds check: beats an opaque runtime call so
            // LLVM can schedule/hoist and keep the hot loop in registers.
            let result = emit_int_slice_index_inline(
                context,
                module,
                builder,
                values[slice].into_struct_value(),
                values[index].into_int_value(),
                &value_name(*out),
            )?;
            values.insert(*out, result.into());
        }
        Inst::SliceStore {
            slice,
            index,
            value,
        } => {
            emit_int_slice_store_inline(
                context,
                module,
                builder,
                values[slice].into_struct_value(),
                values[index].into_int_value(),
                values[value].into_int_value(),
            )?;
        }
        Inst::SliceAppend { out, slice, value } => {
            // Fast path when the header is owned and len < cap: store in place
            // and return the updated header. Slow path keeps the runtime grow.
            let result = emit_int_slice_append_inline(
                context,
                module,
                builder,
                slice_result_slots[out],
                values[slice].into_struct_value(),
                values[value].into_int_value(),
                &value_name(*out),
            )?;
            values.insert(*out, result);
        }
        Inst::SliceSlice {
            out,
            slice,
            low,
            high,
            max,
        } => {
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let max = max
                .map(|value| values[&value])
                .unwrap_or_else(|| context.i64_type().const_int(u64::MAX, true).into());
            let result = call_slice_return(
                context,
                module,
                builder,
                slice_result_slots[out],
                "mako_native_int_slice_slice",
                function_type,
                &{
                    let mut args = slice_parts(builder, values[slice].into_struct_value())?;
                    args.push(values[low].into());
                    args.push(values[high].into());
                    args.push(max.into());
                    args
                },
                &value_name(*out),
            )?;
            values.insert(*out, result);
        }
        Inst::SliceClone { out, slice } => {
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let result = call_slice_return(
                context,
                module,
                builder,
                slice_result_slots[out],
                "mako_native_int_slice_clone",
                function_type,
                &slice_parts(builder, values[slice].into_struct_value())?,
                &value_name(*out),
            )?;
            values.insert(*out, result);
        }
        Inst::DropSlice { value } => {
            let drop = external_function(
                module,
                "mako_native_int_slice_drop",
                context.void_type().fn_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                ),
            );
            let args = slice_parts(builder, values[value].into_struct_value())?;
            builder
                .build_call(drop, &args, "slice.drop")
                .map_err(builder_error)?;
        }
        Inst::StructMake { out, struct_id, fields } => {
            let struct_ty = struct_types[*struct_id as usize];
            let size = struct_ty
                .size_of()
                .ok_or_else(|| LlvmError::new("LLVM backend: unsized struct"))?;
            let malloc = external_function(
                module,
                "malloc",
                context
                    .ptr_type(Default::default())
                    .fn_type(&[context.i64_type().into()], false),
            );
            let ptr = builder
                .build_call(malloc, &[size.into()], &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("malloc returned void"))?
                .into_pointer_value();
            for (index, field) in fields.iter().enumerate() {
                let field_ptr = builder
                    .build_struct_gep(struct_ty, ptr, index as u32, "struct.field")
                    .map_err(|_| LlvmError::new("LLVM backend: struct field GEP failed"))?;
                builder
                    .build_store(field_ptr, values[field])
                    .map_err(builder_error)?;
            }
            values.insert(*out, ptr.into());
        }
        Inst::StructField { out, base, struct_id, index, ty } => {
            let struct_ty = struct_types[*struct_id as usize];
            let field_ptr = builder
                .build_struct_gep(struct_ty, values[base].into_pointer_value(), *index, "struct.field")
                .map_err(|_| LlvmError::new("LLVM backend: struct field GEP failed"))?;
            let loaded = builder
                .build_load(llvm_type(context, *ty), field_ptr, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, loaded);
        }
        Inst::StructFieldStore { base, struct_id, index, value } => {
            let struct_ty = struct_types[*struct_id as usize];
            let field_ptr = builder
                .build_struct_gep(struct_ty, values[base].into_pointer_value(), *index, "struct.field")
                .map_err(|_| LlvmError::new("LLVM backend: struct field GEP failed"))?;
            builder
                .build_store(field_ptr, values[value])
                .map_err(builder_error)?;
        }
        Inst::StructClone { out, base, struct_id } => {
            let cloned = llvm_emit_struct_clone(
                context,
                module,
                builder,
                *struct_id,
                values[base].into_pointer_value(),
                struct_types,
                layouts,
                &value_name(*out),
            )?;
            values.insert(*out, cloned.into());
        }
        Inst::EnumMake { out, enum_id, tag, slot_base, payload } => {
            // calloc a zeroed block so inactive owned slots are null; store the
            // tag and the variant's payload.
            let struct_ty = struct_types[*enum_id as usize];
            let size = struct_ty
                .size_of()
                .ok_or_else(|| LlvmError::new("LLVM backend: unsized enum"))?;
            let calloc = external_function(
                module,
                "calloc",
                context.ptr_type(Default::default()).fn_type(
                    &[context.i64_type().into(), context.i64_type().into()],
                    false,
                ),
            );
            let ptr = builder
                .build_call(
                    calloc,
                    &[context.i64_type().const_int(1, false).into(), size.into()],
                    &value_name(*out),
                )
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("calloc returned void"))?
                .into_pointer_value();
            let tag_gep = builder
                .build_struct_gep(struct_ty, ptr, 0, "enum.tag")
                .map_err(|_| LlvmError::new("LLVM backend: enum tag GEP failed"))?;
            builder
                .build_store(tag_gep, context.i64_type().const_int(*tag as u64, true))
                .map_err(builder_error)?;
            for (i, field) in payload.iter().enumerate() {
                let gep = builder
                    .build_struct_gep(struct_ty, ptr, *slot_base + i as u32, "enum.payload")
                    .map_err(|_| LlvmError::new("LLVM backend: enum payload GEP failed"))?;
                builder
                    .build_store(gep, values[field])
                    .map_err(builder_error)?;
            }
            values.insert(*out, ptr.into());
        }
        Inst::DropStruct { value, struct_id } => {
            llvm_emit_struct_drop(
                context,
                module,
                builder,
                *struct_id,
                values[value].into_pointer_value(),
                struct_types,
                layouts,
            )?;
        }
        // []string value ABI (MakoNativeStrSliceValue), parallel to []int.
        Inst::StrSliceMake { out, len, cap } => {
            let capacity = cap.map(|v| values[&v]).unwrap_or(values[len]);
            let slot = slice_result_slots[out];
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let f = external_function(module, "mako_native_str_slice_make", function_type);
            builder
                .build_call(
                    f,
                    &[slot.into(), values[len].into(), capacity.into()],
                    "strslice.make",
                )
                .map_err(builder_error)?;
            let loaded = builder
                .build_load(llvm_type(context, Type::StrSlice), slot, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, loaded);
        }
        Inst::StrSliceLiteral { out, elements } => {
            let string_ty = llvm_type(context, Type::Str);
            let array_ty = string_ty
                .into_struct_type()
                .array_type(elements.len().max(1) as u32);
            // Use an array of string values; for empty, still allocate one slot.
            let array_ty = if elements.is_empty() {
                context.struct_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                    ],
                    false,
                )
                .array_type(1)
            } else {
                array_ty
            };
            let slot_elems = builder
                .build_alloca(array_ty, "strslice.lit")
                .map_err(builder_error)?;
            for (i, element) in elements.iter().enumerate() {
                let gep = unsafe {
                    builder.build_in_bounds_gep(
                        string_ty,
                        slot_elems,
                        &[context.i64_type().const_int(i as u64, false)],
                        "strslice.lit.elem",
                    )
                }
                .map_err(builder_error)?;
                builder
                    .build_store(gep, values[element])
                    .map_err(builder_error)?;
            }
            let result_slot = slice_result_slots[out];
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let f = external_function(module, "mako_native_str_slice_literal", function_type);
            builder
                .build_call(
                    f,
                    &[
                        result_slot.into(),
                        slot_elems.into(),
                        context
                            .i64_type()
                            .const_int(elements.len() as u64, false)
                            .into(),
                    ],
                    "strslice.literal",
                )
                .map_err(builder_error)?;
            let loaded = builder
                .build_load(
                    llvm_type(context, Type::StrSlice),
                    result_slot,
                    &value_name(*out),
                )
                .map_err(builder_error)?;
            values.insert(*out, loaded);
        }
        Inst::StrSliceLen { out, slice } => {
            let sv = values[slice].into_struct_value();
            let len = builder
                .build_extract_value(sv, 1, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, len);
        }
        Inst::StrSliceIndex { out, slice, index } => {
            let element = emit_str_slice_index_inline(
                context,
                module,
                builder,
                values[slice].into_struct_value(),
                values[index].into_int_value(),
                &value_name(*out),
            )?;
            values.insert(*out, element.into());
        }
        Inst::StrSliceStore { slice, index, value } => {
            emit_str_slice_store_inline(
                context,
                module,
                builder,
                values[slice].into_struct_value(),
                values[index].into_int_value(),
                values[value].into_struct_value(),
            )?;
        }
        Inst::StrSliceAppend { out, slice, value } => {
            let loaded = emit_str_slice_append_inline(
                context,
                module,
                builder,
                slice_result_slots[out],
                values[slice].into_struct_value(),
                values[value].into_struct_value(),
                &value_name(*out),
            )?;
            values.insert(*out, loaded);
        }
        Inst::StrSliceSlice {
            out,
            slice,
            low,
            high,
            max,
        } => {
            let parts = slice_parts(builder, values[slice].into_struct_value())?;
            let max_v = max
                .map(|v| values[&v])
                .unwrap_or_else(|| context.i64_type().const_int((-1i64) as u64, true).into());
            let slot = slice_result_slots[out];
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let f = external_function(module, "mako_native_str_slice_slice", function_type);
            let mut args: Vec<BasicMetadataValueEnum> = vec![slot.into()];
            args.extend_from_slice(&parts);
            args.push(values[low].into());
            args.push(values[high].into());
            args.push(max_v.into());
            builder
                .build_call(f, &args, "strslice.slice")
                .map_err(builder_error)?;
            let loaded = builder
                .build_load(llvm_type(context, Type::StrSlice), slot, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, loaded);
        }
        Inst::StrSliceClone { out, slice } => {
            let parts = slice_parts(builder, values[slice].into_struct_value())?;
            let slot = slice_result_slots[out];
            let function_type = context.void_type().fn_type(
                &[
                    context.ptr_type(Default::default()).into(),
                    context.ptr_type(Default::default()).into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                    context.i64_type().into(),
                ],
                false,
            );
            let f = external_function(module, "mako_native_str_slice_clone", function_type);
            let mut args: Vec<BasicMetadataValueEnum> = vec![slot.into()];
            args.extend_from_slice(&parts);
            builder
                .build_call(f, &args, "strslice.clone")
                .map_err(builder_error)?;
            let loaded = builder
                .build_load(llvm_type(context, Type::StrSlice), slot, &value_name(*out))
                .map_err(builder_error)?;
            values.insert(*out, loaded);
        }
        Inst::DropStrSlice { value } => {
            let parts = slice_parts(builder, values[value].into_struct_value())?;
            let f = external_function(
                module,
                "mako_native_str_slice_drop",
                context.void_type().fn_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                ),
            );
            builder
                .build_call(f, &parts, "strslice.drop")
                .map_err(builder_error)?;
        }
    }
    Ok(())
}

/// Null-safe deep clone of a heap struct (recursive for nested aggregates and
/// owned string/slice fields). Result is written through a stack slot so nested
/// clones do not fight over phi nodes.
fn llvm_emit_struct_clone<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    struct_id: u32,
    base_ptr: PointerValue<'ctx>,
    struct_types: &[inkwell::types::StructType<'ctx>],
    layouts: &[native_ir::StructLayout],
    result_name: &str,
) -> Result<PointerValue<'ctx>, LlvmError> {
    let function = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("LLVM backend: no insert block for struct clone"))?;
    let ptr_ty = context.ptr_type(Default::default());
    let result_slot = builder
        .build_alloca(ptr_ty, "clone.result.slot")
        .map_err(builder_error)?;
    builder
        .build_store(result_slot, ptr_ty.const_null())
        .map_err(builder_error)?;

    let is_null = builder
        .build_int_compare(
            inkwell::IntPredicate::EQ,
            builder
                .build_ptr_to_int(base_ptr, context.i64_type(), "clone.base.int")
                .map_err(builder_error)?,
            context.i64_type().const_zero(),
            "clone.is_null",
        )
        .map_err(builder_error)?;
    let clone_bb = context.append_basic_block(function, "clone.body");
    let merge_bb = context.append_basic_block(function, "clone.merge");
    builder
        .build_conditional_branch(is_null, merge_bb, clone_bb)
        .map_err(builder_error)?;

    builder.position_at_end(clone_bb);
    let struct_ty = struct_types[struct_id as usize];
    let fields = &layouts[struct_id as usize].fields;
    let size = struct_ty
        .size_of()
        .ok_or_else(|| LlvmError::new("LLVM backend: unsized struct"))?;
    let malloc = external_function(
        module,
        "malloc",
        ptr_ty.fn_type(&[context.i64_type().into()], false),
    );
    let ptr = builder
        .build_call(malloc, &[size.into()], result_name)
        .map_err(builder_error)?
        .try_as_basic_value()
        .basic()
        .ok_or_else(|| LlvmError::new("malloc returned void"))?
        .into_pointer_value();
    for (index, (_, field_ty)) in fields.iter().enumerate() {
        let src = builder
            .build_struct_gep(struct_ty, base_ptr, index as u32, "clone.src")
            .map_err(|_| LlvmError::new("LLVM backend: struct clone GEP failed"))?;
        let loaded = builder
            .build_load(llvm_type(context, *field_ty), src, "clone.field")
            .map_err(builder_error)?;
        let stored = match field_ty {
            Type::Str => {
                let string_ty = llvm_type(context, Type::Str);
                let clone = external_function(
                    module,
                    "mako_native_string_clone",
                    string_ty.fn_type(&[string_ty.into()], false),
                );
                builder
                    .build_call(clone, &[loaded.into()], "clone.string")
                    .map_err(builder_error)?
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("string clone returned void"))?
            }
            Type::IntSlice => {
                let slot = builder
                    .build_alloca(llvm_type(context, Type::IntSlice), "clone.slice.slot")
                    .map_err(builder_error)?;
                let function_type = context.void_type().fn_type(
                    &[
                        ptr_ty.into(),
                        ptr_ty.into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                );
                call_slice_return(
                    context,
                    module,
                    builder,
                    slot,
                    "mako_native_int_slice_clone",
                    function_type,
                    &slice_parts(builder, loaded.into_struct_value())?,
                    "clone.slice",
                )?
            }
            Type::StrSlice => {
                let slot = builder
                    .build_alloca(llvm_type(context, Type::StrSlice), "clone.strslice.slot")
                    .map_err(builder_error)?;
                let function_type = context.void_type().fn_type(
                    &[
                        ptr_ty.into(),
                        ptr_ty.into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                );
                call_slice_return_ty(
                    context,
                    module,
                    builder,
                    slot,
                    "mako_native_str_slice_clone",
                    function_type,
                    &slice_parts(builder, loaded.into_struct_value())?,
                    "clone.strslice",
                    Type::StrSlice,
                )?
            }
            Type::Struct(nested_id) => llvm_emit_struct_clone(
                context,
                module,
                builder,
                *nested_id,
                loaded.into_pointer_value(),
                struct_types,
                layouts,
                "clone.nested",
            )?
            .into(),
            Type::ShareInt => {
                let ptr_ty = context.ptr_type(Default::default());
                let clone = external_function(
                    module,
                    "mako_native_share_clone_ptr",
                    ptr_ty.fn_type(&[ptr_ty.into()], false),
                );
                builder
                    .build_call(clone, &[loaded.into()], "clone.share")
                    .map_err(builder_error)?
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("share clone returned void"))?
            }
            Type::MapII
            | Type::MapSI
            | Type::MapSS
            | Type::MapIF
            | Type::MapFI => {
                let ptr_ty = context.ptr_type(Default::default());
                let name = match field_ty {
                    Type::MapII => "mako_native_map_ii_clone_ptr",
                    Type::MapSI => "mako_native_map_si_clone_ptr",
                    Type::MapSS => "mako_native_map_ss_clone_ptr",
                    Type::MapIF => "mako_native_map_if_clone_ptr",
                    Type::MapFI => "mako_native_map_fi_clone_ptr",
                    _ => unreachable!(),
                };
                let clone = external_function(module, name, ptr_ty.fn_type(&[ptr_ty.into()], false));
                builder
                    .build_call(clone, &[loaded.into()], "clone.map")
                    .map_err(builder_error)?
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("map clone returned void"))?
            }
            _ => loaded,
        };
        let dst = builder
            .build_struct_gep(struct_ty, ptr, index as u32, "clone.dst")
            .map_err(|_| LlvmError::new("LLVM backend: struct clone GEP failed"))?;
        builder.build_store(dst, stored).map_err(builder_error)?;
    }
    builder.build_store(result_slot, ptr).map_err(builder_error)?;
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(merge_bb);
    builder
        .build_load(ptr_ty, result_slot, result_name)
        .map_err(builder_error)
        .map(|v| v.into_pointer_value())
}

fn llvm_emit_struct_drop<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    struct_id: u32,
    base_ptr: PointerValue<'ctx>,
    struct_types: &[inkwell::types::StructType<'ctx>],
    layouts: &[native_ir::StructLayout],
) -> Result<(), LlvmError> {
    let function = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("LLVM backend: no insert block for struct drop"))?;
    let is_null = builder
        .build_int_compare(
            inkwell::IntPredicate::EQ,
            builder
                .build_ptr_to_int(base_ptr, context.i64_type(), "drop.base.int")
                .map_err(builder_error)?,
            context.i64_type().const_zero(),
            "drop.is_null",
        )
        .map_err(builder_error)?;
    let drop_bb = context.append_basic_block(function, "drop.body");
    let cont_bb = context.append_basic_block(function, "drop.cont");
    builder
        .build_conditional_branch(is_null, cont_bb, drop_bb)
        .map_err(builder_error)?;
    builder.position_at_end(drop_bb);
    let struct_ty = struct_types[struct_id as usize];
    let fields = &layouts[struct_id as usize].fields;
    for (index, (_, field_ty)) in fields.iter().enumerate() {
        let gep = builder
            .build_struct_gep(struct_ty, base_ptr, index as u32, "drop.field")
            .map_err(|_| LlvmError::new("LLVM backend: struct drop GEP failed"))?;
        match field_ty {
            Type::Str => {
                let string_ty = llvm_type(context, Type::Str);
                let loaded = builder
                    .build_load(string_ty, gep, "drop.string")
                    .map_err(builder_error)?;
                let drop = external_function(
                    module,
                    "mako_native_string_drop",
                    context.void_type().fn_type(&[string_ty.into()], false),
                );
                builder
                    .build_call(drop, &[loaded.into()], "drop.string.call")
                    .map_err(builder_error)?;
            }
            Type::IntSlice => {
                let loaded = builder
                    .build_load(llvm_type(context, Type::IntSlice), gep, "drop.slice")
                    .map_err(builder_error)?;
                let drop = external_function(
                    module,
                    "mako_native_int_slice_drop",
                    context.void_type().fn_type(
                        &[
                            context.ptr_type(Default::default()).into(),
                            context.i64_type().into(),
                            context.i64_type().into(),
                            context.i64_type().into(),
                        ],
                        false,
                    ),
                );
                builder
                    .build_call(
                        drop,
                        &slice_parts(builder, loaded.into_struct_value())?,
                        "drop.slice.call",
                    )
                    .map_err(builder_error)?;
            }
            Type::StrSlice => {
                let loaded = builder
                    .build_load(llvm_type(context, Type::StrSlice), gep, "drop.strslice")
                    .map_err(builder_error)?;
                let drop = external_function(
                    module,
                    "mako_native_str_slice_drop",
                    context.void_type().fn_type(
                        &[
                            context.ptr_type(Default::default()).into(),
                            context.i64_type().into(),
                            context.i64_type().into(),
                            context.i64_type().into(),
                        ],
                        false,
                    ),
                );
                builder
                    .build_call(
                        drop,
                        &slice_parts(builder, loaded.into_struct_value())?,
                        "drop.strslice.call",
                    )
                    .map_err(builder_error)?;
            }
            Type::ShareInt => {
                let loaded = builder
                    .build_load(llvm_type(context, Type::ShareInt), gep, "drop.share")
                    .map_err(builder_error)?;
                let ptr_ty = context.ptr_type(Default::default());
                let drop = external_function(
                    module,
                    "mako_native_share_drop_ptr",
                    context.void_type().fn_type(&[ptr_ty.into()], false),
                );
                builder
                    .build_call(drop, &[loaded.into()], "drop.share.call")
                    .map_err(builder_error)?;
            }
            Type::MapII
            | Type::MapSI
            | Type::MapSS
            | Type::MapIF
            | Type::MapFI => {
                let loaded = builder
                    .build_load(llvm_type(context, *field_ty), gep, "drop.map")
                    .map_err(builder_error)?;
                let ptr_ty = context.ptr_type(Default::default());
                let name = match field_ty {
                    Type::MapII => "mako_native_map_ii_drop_ptr",
                    Type::MapSI => "mako_native_map_si_drop_ptr",
                    Type::MapSS => "mako_native_map_ss_drop_ptr",
                    Type::MapIF => "mako_native_map_if_drop_ptr",
                    Type::MapFI => "mako_native_map_fi_drop_ptr",
                    _ => unreachable!(),
                };
                let drop = external_function(
                    module,
                    name,
                    context.void_type().fn_type(&[ptr_ty.into()], false),
                );
                builder
                    .build_call(drop, &[loaded.into()], "drop.map.call")
                    .map_err(builder_error)?;
            }
            Type::Struct(nested_id) => {
                let loaded = builder
                    .build_load(llvm_type(context, Type::Struct(*nested_id)), gep, "drop.nested")
                    .map_err(builder_error)?;
                llvm_emit_struct_drop(
                    context,
                    module,
                    builder,
                    *nested_id,
                    loaded.into_pointer_value(),
                    struct_types,
                    layouts,
                )?;
            }
            _ => {}
        }
    }
    let free = external_function(
        module,
        "free",
        context
            .void_type()
            .fn_type(&[context.ptr_type(Default::default()).into()], false),
    );
    builder
        .build_call(free, &[base_ptr.into()], "struct.drop")
        .map_err(builder_error)?;
    builder
        .build_unconditional_branch(cont_bb)
        .map_err(builder_error)?;
    builder.position_at_end(cont_bb);
    Ok(())
}

fn external_function<'ctx>(
    module: &Module<'ctx>,
    name: &str,
    ty: inkwell::types::FunctionType<'ctx>,
) -> FunctionValue<'ctx> {
    module
        .get_function(name)
        .unwrap_or_else(|| module.add_function(name, ty, None))
}

fn call_slice_return<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    result_slot: PointerValue<'ctx>,
    name: &str,
    function_type: inkwell::types::FunctionType<'ctx>,
    arguments: &[BasicMetadataValueEnum<'ctx>],
    result_name: &str,
) -> Result<BasicValueEnum<'ctx>, LlvmError> {
    // Int and string slice value ABIs share the same four-field layout.
    call_slice_return_ty(
        context,
        module,
        builder,
        result_slot,
        name,
        function_type,
        arguments,
        result_name,
        Type::IntSlice,
    )
}

fn call_slice_return_ty<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    result_slot: PointerValue<'ctx>,
    name: &str,
    function_type: inkwell::types::FunctionType<'ctx>,
    arguments: &[BasicMetadataValueEnum<'ctx>],
    result_name: &str,
    result_ty: Type,
) -> Result<BasicValueEnum<'ctx>, LlvmError> {
    let slice_type = llvm_type(context, result_ty);
    let function = external_function(module, name, function_type);
    let mut call_arguments = Vec::with_capacity(arguments.len() + 1);
    call_arguments.push(result_slot.into());
    call_arguments.extend_from_slice(arguments);
    builder
        .build_call(function, &call_arguments, result_name)
        .map_err(builder_error)?;
    builder
        .build_load(slice_type, result_slot, result_name)
        .map_err(builder_error)
}

fn slice_parts<'ctx>(
    builder: &Builder<'ctx>,
    value: StructValue<'ctx>,
) -> Result<Vec<BasicMetadataValueEnum<'ctx>>, LlvmError> {
    (0..4)
        .map(|index| {
            builder
                .build_extract_value(value, index, "slice.part")
                .map(|value| value.into())
                .map_err(builder_error)
        })
        .collect()
}

/// Extract (data, len, cap, owned) as typed values.
fn slice_fields<'ctx>(
    builder: &Builder<'ctx>,
    value: StructValue<'ctx>,
) -> Result<(PointerValue<'ctx>, IntValue<'ctx>, IntValue<'ctx>, IntValue<'ctx>), LlvmError> {
    let data = builder
        .build_extract_value(value, 0, "slice.data")
        .map_err(builder_error)?
        .into_pointer_value();
    let len = builder
        .build_extract_value(value, 1, "slice.len")
        .map_err(builder_error)?
        .into_int_value();
    let cap = builder
        .build_extract_value(value, 2, "slice.cap")
        .map_err(builder_error)?
        .into_int_value();
    let owned = builder
        .build_extract_value(value, 3, "slice.owned")
        .map_err(builder_error)?
        .into_int_value();
    Ok((data, len, cap, owned))
}

fn emit_slice_bounds_check<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    index: IntValue<'ctx>,
    len: IntValue<'ctx>,
) -> Result<(), LlvmError> {
    // Single unsigned compare: (u64)index < (u64)len rejects negatives and OOB.
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("slice bounds: no insert block"))?;
    let ok_bb = context.append_basic_block(parent, "slice.ok");
    let bad_bb = context.append_basic_block(parent, "slice.oob");
    let ok = builder
        .build_int_compare(IntPredicate::ULT, index, len, "idx.ok")
        .map_err(builder_error)?;
    builder
        .build_conditional_branch(ok, ok_bb, bad_bb)
        .map_err(builder_error)?;
    builder.position_at_end(bad_bb);
    let abort = external_function(module, "abort", context.void_type().fn_type(&[], false));
    builder
        .build_call(abort, &[], "slice.abort")
        .map_err(builder_error)?;
    builder.build_unreachable().map_err(builder_error)?;
    builder.position_at_end(ok_bb);
    Ok(())
}

fn emit_int_slice_index_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    slice: StructValue<'ctx>,
    index: IntValue<'ctx>,
    name: &str,
) -> Result<IntValue<'ctx>, LlvmError> {
    let (data, len, _cap, _owned) = slice_fields(builder, slice)?;
    emit_slice_bounds_check(context, module, builder, index, len)?;
    let elem_ptr = unsafe {
        builder
            .build_gep(context.i64_type(), data, &[index], &format!("{name}.ptr"))
            .map_err(builder_error)?
    };
    builder
        .build_load(context.i64_type(), elem_ptr, name)
        .map(|v| v.into_int_value())
        .map_err(builder_error)
}

fn emit_int_slice_store_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    slice: StructValue<'ctx>,
    index: IntValue<'ctx>,
    value: IntValue<'ctx>,
) -> Result<(), LlvmError> {
    let (data, len, _cap, _owned) = slice_fields(builder, slice)?;
    emit_slice_bounds_check(context, module, builder, index, len)?;
    let elem_ptr = unsafe {
        builder
            .build_gep(context.i64_type(), data, &[index], "slice.set.ptr")
            .map_err(builder_error)?
    };
    builder
        .build_store(elem_ptr, value)
        .map_err(builder_error)?;
    Ok(())
}

fn emit_str_slice_index_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    slice: StructValue<'ctx>,
    index: IntValue<'ctx>,
    name: &str,
) -> Result<StructValue<'ctx>, LlvmError> {
    let (data, len, _cap, _owned) = slice_fields(builder, slice)?;
    emit_slice_bounds_check(context, module, builder, index, len)?;
    let string_ty = llvm_type(context, Type::Str);
    let elem_ptr = unsafe {
        builder
            .build_gep(string_ty, data, &[index], &format!("{name}.ptr"))
            .map_err(builder_error)?
    };
    let raw = builder
        .build_load(string_ty, elem_ptr, &format!("{name}.raw"))
        .map_err(builder_error)?
        .into_struct_value();
    // Match runtime: return an owned clone of the element. Immortal/static
    // payloads share without a runtime call.
    emit_string_clone_or_share(context, module, builder, raw, name)
}

fn emit_str_slice_store_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    slice: StructValue<'ctx>,
    index: IntValue<'ctx>,
    value: StructValue<'ctx>,
) -> Result<(), LlvmError> {
    let (data, len, _cap, _owned) = slice_fields(builder, slice)?;
    emit_slice_bounds_check(context, module, builder, index, len)?;
    let string_ty = llvm_type(context, Type::Str);
    let elem_ptr = unsafe {
        builder
            .build_gep(string_ty, data, &[index], "strslice.set.ptr")
            .map_err(builder_error)?
    };
    let old = builder
        .build_load(string_ty, elem_ptr, "strslice.set.old")
        .map_err(builder_error)?;
    let drop_fn = external_function(
        module,
        "mako_native_string_drop",
        context.void_type().fn_type(&[string_ty.into()], false),
    );
    builder
        .build_call(drop_fn, &[old.into()], "strslice.set.drop")
        .map_err(builder_error)?;
    let clone_fn = external_function(
        module,
        "mako_native_string_clone",
        string_ty.fn_type(&[string_ty.into()], false),
    );
    let cloned = builder
        .build_call(clone_fn, &[value.into()], "strslice.set.clone")
        .map_err(builder_error)?
        .try_as_basic_value()
        .basic()
        .ok_or_else(|| LlvmError::new("string clone returned void"))?;
    builder
        .build_store(elem_ptr, cloned)
        .map_err(builder_error)?;
    Ok(())
}

fn emit_str_slice_append_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    result_slot: PointerValue<'ctx>,
    slice: StructValue<'ctx>,
    element: StructValue<'ctx>,
    name: &str,
) -> Result<BasicValueEnum<'ctx>, LlvmError> {
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("strslice append: no insert block"))?;
    let (data, len, cap, owned) = slice_fields(builder, slice)?;
    let zero = context.i64_type().const_zero();
    let one = context.i64_type().const_int(1, false);
    let owned_nz = builder
        .build_int_compare(IntPredicate::NE, owned, zero, "strslice.owned")
        .map_err(builder_error)?;
    let room = builder
        .build_int_compare(IntPredicate::ULT, len, cap, "strslice.room")
        .map_err(builder_error)?;
    let fast = builder
        .build_and(owned_nz, room, "strslice.append.fast")
        .map_err(builder_error)?;
    let fast_bb = context.append_basic_block(parent, "strappend.fast");
    let slow_bb = context.append_basic_block(parent, "strappend.slow");
    let merge_bb = context.append_basic_block(parent, "strappend.merge");
    builder
        .build_conditional_branch(fast, fast_bb, slow_bb)
        .map_err(builder_error)?;

    builder.position_at_end(fast_bb);
    let string_ty = llvm_type(context, Type::Str);
    let slice_ty = llvm_type(context, Type::StrSlice);
    let elem_ptr = unsafe {
        builder
            .build_gep(string_ty, data, &[len], "strappend.ptr")
            .map_err(builder_error)?
    };
    // Immortal/static strings (high bit of len): share the value without a
    // runtime call — the append-literal hot path (string_slice bench).
    // When the element is a constant with the high bit set, LLVM folds the
    // branch away and this becomes a plain store.
    let cloned = emit_string_clone_or_share(context, module, builder, element, "strappend.clone")?;
    builder
        .build_store(elem_ptr, cloned)
        .map_err(builder_error)?;
    let new_len = builder
        .build_int_add(len, one, "strappend.len")
        .map_err(builder_error)?;
    // Keep the updated header in registers on the fast path; phi at merge
    // instead of store/load through the result slot (hot loop traffic).
    let mut fast_hdr = slice_ty.into_struct_type().get_undef();
    fast_hdr = builder
        .build_insert_value(fast_hdr, data, 0, "strappend.data")
        .map_err(builder_error)?
        .into_struct_value();
    fast_hdr = builder
        .build_insert_value(fast_hdr, new_len, 1, "strappend.newlen")
        .map_err(builder_error)?
        .into_struct_value();
    fast_hdr = builder
        .build_insert_value(fast_hdr, cap, 2, "strappend.cap")
        .map_err(builder_error)?
        .into_struct_value();
    fast_hdr = builder
        .build_insert_value(fast_hdr, owned, 3, "strappend.owned")
        .map_err(builder_error)?
        .into_struct_value();
    let fast_end = builder.get_insert_block().unwrap();
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(slow_bb);
    let parts = slice_parts(builder, slice)?;
    let function_type = context.void_type().fn_type(
        &[
            context.ptr_type(Default::default()).into(),
            context.ptr_type(Default::default()).into(),
            context.i64_type().into(),
            context.i64_type().into(),
            context.i64_type().into(),
            string_ty.into(),
        ],
        false,
    );
    let f = external_function(module, "mako_native_str_slice_append", function_type);
    let mut args: Vec<BasicMetadataValueEnum> = vec![result_slot.into()];
    args.extend_from_slice(&parts);
    args.push(element.into());
    builder
        .build_call(f, &args, &format!("{name}.slow"))
        .map_err(builder_error)?;
    let slow_hdr = builder
        .build_load(slice_ty, result_slot, &format!("{name}.slow.hdr"))
        .map_err(builder_error)?
        .into_struct_value();
    let slow_end = builder.get_insert_block().unwrap();
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(merge_bb);
    let phi = builder.build_phi(slice_ty, name).map_err(builder_error)?;
    phi.add_incoming(&[(&fast_hdr, fast_end), (&slow_hdr, slow_end)]);
    Ok(phi.as_basic_value())
}

fn emit_int_slice_append_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    result_slot: PointerValue<'ctx>,
    slice: StructValue<'ctx>,
    element: IntValue<'ctx>,
    name: &str,
) -> Result<BasicValueEnum<'ctx>, LlvmError> {
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("slice append: no insert block"))?;
    let (data, len, cap, owned) = slice_fields(builder, slice)?;
    let zero = context.i64_type().const_zero();
    let one = context.i64_type().const_int(1, false);
    let owned_nz = builder
        .build_int_compare(IntPredicate::NE, owned, zero, "slice.owned")
        .map_err(builder_error)?;
    let room = builder
        .build_int_compare(IntPredicate::ULT, len, cap, "slice.room")
        .map_err(builder_error)?;
    let fast = builder
        .build_and(owned_nz, room, "slice.append.fast")
        .map_err(builder_error)?;
    let fast_bb = context.append_basic_block(parent, "append.fast");
    let slow_bb = context.append_basic_block(parent, "append.slow");
    let merge_bb = context.append_basic_block(parent, "append.merge");
    builder
        .build_conditional_branch(fast, fast_bb, slow_bb)
        .map_err(builder_error)?;

    // Fast: data[len] = element; return {data, len+1, cap, owned}
    builder.position_at_end(fast_bb);
    let elem_ptr = unsafe {
        builder
            .build_gep(context.i64_type(), data, &[len], "append.ptr")
            .map_err(builder_error)?
    };
    builder
        .build_store(elem_ptr, element)
        .map_err(builder_error)?;
    let new_len = builder
        .build_int_add(len, one, "append.len")
        .map_err(builder_error)?;
    let slice_ty = llvm_type(context, Type::IntSlice).into_struct_type();
    let mut hdr = slice_ty.get_undef();
    hdr = builder
        .build_insert_value(hdr, data, 0, "append.data")
        .map_err(builder_error)?
        .into_struct_value();
    hdr = builder
        .build_insert_value(hdr, new_len, 1, "append.newlen")
        .map_err(builder_error)?
        .into_struct_value();
    hdr = builder
        .build_insert_value(hdr, cap, 2, "append.cap")
        .map_err(builder_error)?
        .into_struct_value();
    hdr = builder
        .build_insert_value(hdr, owned, 3, "append.owned")
        .map_err(builder_error)?
        .into_struct_value();
    builder
        .build_store(result_slot, hdr)
        .map_err(builder_error)?;
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    // Slow: runtime realloc/grow.
    builder.position_at_end(slow_bb);
    let function_type = context.void_type().fn_type(
        &[
            context.ptr_type(Default::default()).into(),
            context.ptr_type(Default::default()).into(),
            context.i64_type().into(),
            context.i64_type().into(),
            context.i64_type().into(),
            context.i64_type().into(),
        ],
        false,
    );
    let mut args = slice_parts(builder, slice)?;
    args.push(element.into());
    let _ = call_slice_return(
        context,
        module,
        builder,
        result_slot,
        "mako_native_int_slice_append",
        function_type,
        &args,
        &format!("{name}.slow"),
    )?;
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(merge_bb);
    builder
        .build_load(llvm_type(context, Type::IntSlice), result_slot, name)
        .map_err(builder_error)
}

fn pack_string<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    data: PointerValue<'ctx>,
    len: IntValue<'ctx>,
    name: &str,
) -> Result<StructValue<'ctx>, LlvmError> {
    let string_type = llvm_type(context, Type::Str).into_struct_type();
    let with_data = builder
        .build_insert_value(string_type.get_undef(), data, 0, &format!("{name}.data"))
        .map_err(builder_error)?
        .into_struct_value();
    Ok(builder
        .build_insert_value(with_data, len, 1, &format!("{name}.len"))
        .map_err(builder_error)?
        .into_struct_value())
}

/// Layout of `MakoNativeMapII` (hand-C matched + separate len):
/// `{ keys*, vals*, state*, cap, lenp* }` — 40 bytes. `*lenp` is off-header
/// so insert stores do not alias table-pointer loads (LICM in fill loops).
const MAP_II_OFF_KEYS: u64 = 0;
const MAP_II_OFF_VALS: u64 = 8;
const MAP_II_OFF_STATE: u64 = 16;
const MAP_II_OFF_CAP: u64 = 24;
const MAP_II_OFF_LENP: u64 = 32;
const MAP_II_FULL: i64 = 1;
const MAP_II_TOMB: i64 = 2;

/// Load hand-C-style table pointers from the map header.
/// Returns (state, keys, vals, len_p, cap) where `len_p` is `*m->lenp`.
fn map_ii_load_ptrs<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    map: PointerValue<'ctx>,
) -> Result<
    (
        PointerValue<'ctx>,
        PointerValue<'ctx>,
        PointerValue<'ctx>,
        PointerValue<'ctx>,
        IntValue<'ctx>,
    ),
    LlvmError,
> {
    let i64t = context.i64_type();
    let i8t = context.i8_type();
    let ptr_ty = context.ptr_type(Default::default());
    let load_ptr = |off: u64, name: &str| -> Result<PointerValue<'ctx>, LlvmError> {
        Ok(builder
            .build_load(
                ptr_ty,
                unsafe {
                    builder
                        .build_gep(i8t, map, &[i64t.const_int(off, false)], &format!("{name}.p"))
                        .map_err(builder_error)?
                },
                name,
            )
            .map_err(builder_error)?
            .into_pointer_value())
    };
    let keys = load_ptr(MAP_II_OFF_KEYS, "m.keys")?;
    let vals = load_ptr(MAP_II_OFF_VALS, "m.vals")?;
    let state = load_ptr(MAP_II_OFF_STATE, "m.state")?;
    // len lives in a separate allocation; load the pointer once (hoistable).
    let len_p = load_ptr(MAP_II_OFF_LENP, "m.lenp")?;
    let cap = builder
        .build_load(
            i64t,
            unsafe {
                builder
                    .build_gep(i8t, map, &[i64t.const_int(MAP_II_OFF_CAP, false)], "m.cap.p")
                    .map_err(builder_error)?
            },
            "m.cap",
        )
        .map_err(builder_error)?
        .into_int_value();
    Ok((state, keys, vals, len_p, cap))
}

/// Inline `map[int]int` set — hand-C open-addressing probe.
/// Empty → insert; full+match → update; else linear probe. Table full → cold
/// runtime `set_ptr` (rehash+insert). Pre-sized maps never take the cold edge.
fn emit_map_ii_set_inline<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    map: PointerValue<'ctx>,
    key: IntValue<'ctx>,
    val: IntValue<'ctx>,
) -> Result<(), LlvmError> {
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("map set: no insert block"))?;
    let i64t = context.i64_type();
    let i8t = context.i8_type();
    let ptr_ty = context.ptr_type(Default::default());
    let one = i64t.const_int(1, false);
    let zero = i64t.const_zero();

    let (state, keys, vals, len_p, cap) = map_ii_load_ptrs(context, builder, map)?;
    let mask = builder
        .build_int_sub(cap, one, "m.mask")
        .map_err(builder_error)?;
    let start = builder.build_and(key, mask, "m.i0").map_err(builder_error)?;

    let loop_bb = context.append_basic_block(parent, "map.set.loop");
    let empty_bb = context.append_basic_block(parent, "map.set.empty");
    let full_bb = context.append_basic_block(parent, "map.set.full");
    let next_bb = context.append_basic_block(parent, "map.set.next");
    let grow_bb = context.append_basic_block(parent, "map.set.grow");
    let done_bb = context.append_basic_block(parent, "map.set.done");

    let entry = builder.get_insert_block().unwrap();
    builder
        .build_unconditional_branch(loop_bb)
        .map_err(builder_error)?;
    builder.position_at_end(loop_bb);
    let i_phi = builder.build_phi(i64t, "m.i").map_err(builder_error)?;
    let left_phi = builder.build_phi(i64t, "m.left").map_err(builder_error)?;
    i_phi.add_incoming(&[(&start, entry)]);
    left_phi.add_incoming(&[(&cap, entry)]);
    let i = i_phi.as_basic_value().into_int_value();
    let left = left_phi.as_basic_value().into_int_value();

    let st = builder
        .build_load(
            i8t,
            unsafe {
                builder
                    .build_gep(i8t, state, &[i], "m.st.s")
                    .map_err(builder_error)?
            },
            "m.st",
        )
        .map_err(builder_error)?
        .into_int_value();
    // Hand-C: empty → insert; non-empty → full check / probe.
    let is_empty = builder
        .build_int_compare(IntPredicate::EQ, st, i8t.const_zero(), "m.empty")
        .map_err(builder_error)?;
    builder
        .build_conditional_branch(is_empty, empty_bb, full_bb)
        .map_err(builder_error)?;

    builder.position_at_end(empty_bb);
    builder
        .build_store(
            unsafe {
                builder
                    .build_gep(i8t, state, &[i], "m.st.w")
                    .map_err(builder_error)?
            },
            i8t.const_int(MAP_II_FULL as u64, false),
        )
        .map_err(builder_error)?;
    builder
        .build_store(
            unsafe {
                builder
                    .build_gep(i64t, keys, &[i], "m.k.w")
                    .map_err(builder_error)?
            },
            key,
        )
        .map_err(builder_error)?;
    builder
        .build_store(
            unsafe {
                builder
                    .build_gep(i64t, vals, &[i], "m.v.w")
                    .map_err(builder_error)?
            },
            val,
        )
        .map_err(builder_error)?;
    let len = builder
        .build_load(i64t, len_p, "m.len")
        .map_err(builder_error)?
        .into_int_value();
    let new_len = builder
        .build_int_add(len, one, "m.newlen")
        .map_err(builder_error)?;
    builder
        .build_store(len_p, new_len)
        .map_err(builder_error)?;
    builder
        .build_unconditional_branch(done_bb)
        .map_err(builder_error)?;

    builder.position_at_end(full_bb);
    let is_full = builder
        .build_int_compare(
            IntPredicate::EQ,
            st,
            i8t.const_int(MAP_II_FULL as u64, false),
            "m.isfull",
        )
        .map_err(builder_error)?;
    let check_key = context.append_basic_block(parent, "map.set.ck");
    builder
        .build_conditional_branch(is_full, check_key, next_bb)
        .map_err(builder_error)?;
    builder.position_at_end(check_key);
    let ek = builder
        .build_load(
            i64t,
            unsafe {
                builder
                    .build_gep(i64t, keys, &[i], "m.k.s")
                    .map_err(builder_error)?
            },
            "m.ek",
        )
        .map_err(builder_error)?
        .into_int_value();
    let same = builder
        .build_int_compare(IntPredicate::EQ, ek, key, "m.same")
        .map_err(builder_error)?;
    let upd_bb = context.append_basic_block(parent, "map.set.upd");
    builder
        .build_conditional_branch(same, upd_bb, next_bb)
        .map_err(builder_error)?;
    builder.position_at_end(upd_bb);
    builder
        .build_store(
            unsafe {
                builder
                    .build_gep(i64t, vals, &[i], "m.v.s")
                    .map_err(builder_error)?
            },
            val,
        )
        .map_err(builder_error)?;
    builder
        .build_unconditional_branch(done_bb)
        .map_err(builder_error)?;

    builder.position_at_end(next_bb);
    let left1 = builder
        .build_int_sub(left, one, "m.left1")
        .map_err(builder_error)?;
    let exhausted = builder
        .build_int_compare(IntPredicate::EQ, left1, zero, "m.fulltab")
        .map_err(builder_error)?;
    let cont_probe = context.append_basic_block(parent, "map.set.cont");
    builder
        .build_conditional_branch(exhausted, grow_bb, cont_probe)
        .map_err(builder_error)?;
    builder.position_at_end(cont_probe);
    let next_i = builder
        .build_and(
            builder
                .build_int_add(i, one, "m.i1")
                .map_err(builder_error)?,
            mask,
            "m.inext",
        )
        .map_err(builder_error)?;
    i_phi.add_incoming(&[(&next_i, cont_probe)]);
    left_phi.add_incoming(&[(&left1, cont_probe)]);
    builder
        .build_unconditional_branch(loop_bb)
        .map_err(builder_error)?;

    builder.position_at_end(grow_bb);
    let set_fn = external_function(
        module,
        "mako_native_map_ii_set_ptr",
        context
            .void_type()
            .fn_type(&[ptr_ty.into(), i64t.into(), i64t.into()], false),
    );
    builder
        .build_call(
            set_fn,
            &[map.into(), key.into(), val.into()],
            "map.set.rehash",
        )
        .map_err(builder_error)?;
    builder
        .build_unconditional_branch(done_bb)
        .map_err(builder_error)?;

    builder.position_at_end(done_bb);
    let _ = MAP_II_TOMB;
    Ok(())
}

/// Inline `map[int]int` get — hand-C probe (FULL+key hit / EMPTY miss / else next).
fn emit_map_ii_get_inline<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    map: PointerValue<'ctx>,
    key: IntValue<'ctx>,
    name: &str,
) -> Result<IntValue<'ctx>, LlvmError> {
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("map get: no insert block"))?;
    let i64t = context.i64_type();
    let i8t = context.i8_type();
    let one = i64t.const_int(1, false);
    let zero = i64t.const_zero();

    let (state, keys, vals, _len_p, cap) = map_ii_load_ptrs(context, builder, map)?;
    let mask = builder
        .build_int_sub(cap, one, "g.mask")
        .map_err(builder_error)?;
    let start = builder.build_and(key, mask, "g.i0").map_err(builder_error)?;

    // Hand-C get: empty → 0; full+same key → val; else probe (covers tomb).
    let entry = builder.get_insert_block().unwrap();
    let loop_bb = context.append_basic_block(parent, "map.get.loop");
    let full_bb = context.append_basic_block(parent, "map.get.full");
    let next_bb = context.append_basic_block(parent, "map.get.next");
    let hit_bb = context.append_basic_block(parent, "map.get.hit");
    let miss_bb = context.append_basic_block(parent, "map.get.miss");
    let merge_bb = context.append_basic_block(parent, "map.get.merge");

    builder
        .build_unconditional_branch(loop_bb)
        .map_err(builder_error)?;
    builder.position_at_end(loop_bb);
    let i_phi = builder.build_phi(i64t, "g.i").map_err(builder_error)?;
    let left_phi = builder.build_phi(i64t, "g.left").map_err(builder_error)?;
    i_phi.add_incoming(&[(&start, entry)]);
    left_phi.add_incoming(&[(&cap, entry)]);
    let i = i_phi.as_basic_value().into_int_value();
    let left = left_phi.as_basic_value().into_int_value();

    let st = builder
        .build_load(
            i8t,
            unsafe {
                builder
                    .build_gep(i8t, state, &[i], "g.st.s")
                    .map_err(builder_error)?
            },
            "g.st",
        )
        .map_err(builder_error)?
        .into_int_value();
    let is_empty = builder
        .build_int_compare(IntPredicate::EQ, st, i8t.const_zero(), "g.empty")
        .map_err(builder_error)?;
    builder
        .build_conditional_branch(is_empty, miss_bb, full_bb)
        .map_err(builder_error)?;

    builder.position_at_end(full_bb);
    // full or tomb: only FULL+match hits (hand-C has no tomb; same for no-delete)
    let is_full = builder
        .build_int_compare(
            IntPredicate::EQ,
            st,
            i8t.const_int(MAP_II_FULL as u64, false),
            "g.isfull",
        )
        .map_err(builder_error)?;
    let check_key = context.append_basic_block(parent, "map.get.ck");
    builder
        .build_conditional_branch(is_full, check_key, next_bb)
        .map_err(builder_error)?;
    builder.position_at_end(check_key);
    let ek = builder
        .build_load(
            i64t,
            unsafe {
                builder
                    .build_gep(i64t, keys, &[i], "g.k.s")
                    .map_err(builder_error)?
            },
            "g.ek",
        )
        .map_err(builder_error)?
        .into_int_value();
    let same = builder
        .build_int_compare(IntPredicate::EQ, ek, key, "g.same")
        .map_err(builder_error)?;
    builder
        .build_conditional_branch(same, hit_bb, next_bb)
        .map_err(builder_error)?;

    builder.position_at_end(hit_bb);
    let hit_val = builder
        .build_load(
            i64t,
            unsafe {
                builder
                    .build_gep(i64t, vals, &[i], "g.v.s")
                    .map_err(builder_error)?
            },
            "g.hit",
        )
        .map_err(builder_error)?
        .into_int_value();
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(next_bb);
    let left1 = builder
        .build_int_sub(left, one, "g.left1")
        .map_err(builder_error)?;
    let exhausted = builder
        .build_int_compare(IntPredicate::EQ, left1, zero, "g.exh")
        .map_err(builder_error)?;
    let cont = context.append_basic_block(parent, "map.get.cont");
    builder
        .build_conditional_branch(exhausted, miss_bb, cont)
        .map_err(builder_error)?;
    builder.position_at_end(cont);
    let next_i = builder
        .build_and(
            builder
                .build_int_add(i, one, "g.i1")
                .map_err(builder_error)?,
            mask,
            "g.inext",
        )
        .map_err(builder_error)?;
    i_phi.add_incoming(&[(&next_i, cont)]);
    left_phi.add_incoming(&[(&left1, cont)]);
    builder
        .build_unconditional_branch(loop_bb)
        .map_err(builder_error)?;

    builder.position_at_end(miss_bb);
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(merge_bb);
    let phi = builder.build_phi(i64t, name).map_err(builder_error)?;
    phi.add_incoming(&[(&zero, miss_bb), (&hit_val, hit_bb)]);
    let _ = MAP_II_TOMB;
    Ok(phi.as_basic_value().into_int_value())
}

/// Clone a string value, or share it when the immortal/static high bit is set.
/// Avoids a runtime call on the literal-append hot path.
fn emit_string_clone_or_share<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    builder: &Builder<'ctx>,
    value: StructValue<'ctx>,
    name: &str,
) -> Result<StructValue<'ctx>, LlvmError> {
    let parent = builder
        .get_insert_block()
        .and_then(|b| b.get_parent())
        .ok_or_else(|| LlvmError::new("string clone: no insert block"))?;
    let string_ty = llvm_type(context, Type::Str);
    let raw_len = builder
        .build_extract_value(value, 1, &format!("{name}.len"))
        .map_err(builder_error)?
        .into_int_value();
    // High bit of len marks immortal/static payload (see native_runtime.c).
    let high = context.i64_type().const_int(1u64 << 63, false);
    let is_static = builder
        .build_int_compare(
            IntPredicate::NE,
            builder
                .build_and(raw_len, high, &format!("{name}.hi"))
                .map_err(builder_error)?,
            context.i64_type().const_zero(),
            &format!("{name}.static"),
        )
        .map_err(builder_error)?;
    let share_bb = context.append_basic_block(parent, &format!("{name}.share"));
    let clone_bb = context.append_basic_block(parent, &format!("{name}.heap"));
    let merge_bb = context.append_basic_block(parent, &format!("{name}.merge"));
    builder
        .build_conditional_branch(is_static, share_bb, clone_bb)
        .map_err(builder_error)?;

    builder.position_at_end(share_bb);
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(clone_bb);
    let clone_fn = external_function(
        module,
        "mako_native_string_clone",
        string_ty.fn_type(&[string_ty.into()], false),
    );
    let heap_clone = builder
        .build_call(clone_fn, &[value.into()], &format!("{name}.call"))
        .map_err(builder_error)?
        .try_as_basic_value()
        .basic()
        .ok_or_else(|| LlvmError::new("string clone returned void"))?
        .into_struct_value();
    builder
        .build_unconditional_branch(merge_bb)
        .map_err(builder_error)?;

    builder.position_at_end(merge_bb);
    let phi = builder
        .build_phi(string_ty, name)
        .map_err(builder_error)?;
    phi.add_incoming(&[(&value, share_bb), (&heap_clone, clone_bb)]);
    Ok(phi.as_basic_value().into_struct_value())
}

fn emit_binary<'ctx>(
    builder: &Builder<'ctx>,
    op: BinOp,
    left: BasicValueEnum<'ctx>,
    right: BasicValueEnum<'ctx>,
    ty: Type,
    out: Value,
) -> Result<BasicValueEnum<'ctx>, LlvmError> {
    let name = value_name(out);
    if ty == Type::F64 {
        let (left, right) = (left.into_float_value(), right.into_float_value());
        let value = match op {
            BinOp::Add => builder.build_float_add(left, right, &name).map(Into::into),
            BinOp::Sub => builder.build_float_sub(left, right, &name).map(Into::into),
            BinOp::Mul => builder.build_float_mul(left, right, &name).map(Into::into),
            BinOp::Div => builder.build_float_div(left, right, &name).map(Into::into),
            BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                let predicate = match op {
                    BinOp::Eq => FloatPredicate::OEQ,
                    BinOp::Ne => FloatPredicate::ONE,
                    BinOp::Lt => FloatPredicate::OLT,
                    BinOp::Le => FloatPredicate::OLE,
                    BinOp::Gt => FloatPredicate::OGT,
                    BinOp::Ge => FloatPredicate::OGE,
                    _ => unreachable!(),
                };
                builder
                    .build_float_compare(predicate, left, right, &name)
                    .map(Into::into)
            }
            _ => return Err(LlvmError::new("LLVM float operation is not implemented")),
        };
        return value.map_err(builder_error);
    }
    let (left, right) = (left.into_int_value(), right.into_int_value());
    let value = match op {
        BinOp::Add => builder.build_int_add(left, right, &name).map(Into::into),
        BinOp::Sub => builder.build_int_sub(left, right, &name).map(Into::into),
        BinOp::Mul => builder.build_int_mul(left, right, &name).map(Into::into),
        BinOp::Div => builder
            .build_int_signed_div(left, right, &name)
            .map(Into::into),
        BinOp::Mod => builder
            .build_int_signed_rem(left, right, &name)
            .map(Into::into),
        BinOp::BitAnd | BinOp::And => builder.build_and(left, right, &name).map(Into::into),
        BinOp::BitOr | BinOp::Or => builder.build_or(left, right, &name).map(Into::into),
        BinOp::BitXor => builder.build_xor(left, right, &name).map(Into::into),
        BinOp::BitClear => {
            let inverted = builder
                .build_not(right, "bitclear.not")
                .map_err(builder_error)?;
            builder.build_and(left, inverted, &name).map(Into::into)
        }
        BinOp::Shl => builder.build_left_shift(left, right, &name).map(Into::into),
        BinOp::Shr => builder
            .build_right_shift(left, right, true, &name)
            .map(Into::into),
        BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
            let predicate = match op {
                BinOp::Eq => IntPredicate::EQ,
                BinOp::Ne => IntPredicate::NE,
                BinOp::Lt => IntPredicate::SLT,
                BinOp::Le => IntPredicate::SLE,
                BinOp::Gt => IntPredicate::SGT,
                BinOp::Ge => IntPredicate::SGE,
                _ => unreachable!(),
            };
            builder
                .build_int_compare(predicate, left, right, &name)
                .map(Into::into)
        }
    };
    value.map_err(builder_error)
}

fn builder_error(error: inkwell::builder::BuilderError) -> LlvmError {
    LlvmError::new(format!("LLVM builder failed: {error}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    #[test]
    fn emits_a_host_object_for_scalar_cfg() {
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
        let object = compile_object(&program).unwrap();
        assert!(object.len() > 100);
    }
}
