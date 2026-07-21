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
        Type::StrSlice => context.ptr_type(Default::default()).into(),
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
            let out = match instruction {
                Inst::SliceLiteral { out, .. }
                | Inst::SliceMake { out, .. }
                | Inst::SliceAppend { out, .. }
                | Inst::SliceSlice { out, .. }
                | Inst::SliceClone { out, .. } => Some(*out),
                _ => None,
            };
            if let Some(out) = out {
                let slot = builder
                    .build_alloca(
                        llvm_type(context, Type::IntSlice),
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
                Type::Str | Type::IntSlice | Type::StrSlice | Type::Struct(_) => {
                    return Err(LlvmError::new("integer constant has aggregate type"))
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
        Inst::Call {
            out,
            function,
            args,
            ret,
        } => {
            let arguments: Vec<BasicMetadataValueEnum<'ctx>> =
                args.iter().map(|value| values[value].into()).collect();
            let call = builder
                .build_call(functions[function], &arguments, "call")
                .map_err(builder_error)?;
            if let (Some(out), Some(_)) = (out, ret) {
                let value = call
                    .try_as_basic_value()
                    .basic()
                    .ok_or_else(|| LlvmError::new("LLVM call did not return a value"))?;
                values.insert(*out, value);
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
            let value = pack_string(
                context,
                builder,
                global.as_pointer_value(),
                context.i64_type().const_int(bytes.len() as u64, false),
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
            let len = external_function(
                module,
                "mako_native_int_slice_len",
                context.i64_type().fn_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                ),
            );
            let args = slice_parts(builder, values[slice].into_struct_value())?;
            let result = builder
                .build_call(len, &args, &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("slice len returned void"))?;
            values.insert(*out, result);
        }
        Inst::SliceIndex { out, slice, index } => {
            let get = external_function(
                module,
                "mako_native_int_slice_get",
                context.i64_type().fn_type(
                    &[
                        context.ptr_type(Default::default()).into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                        context.i64_type().into(),
                    ],
                    false,
                ),
            );
            let mut args = slice_parts(builder, values[slice].into_struct_value())?;
            args.push(values[index].into());
            let result = builder
                .build_call(get, &args, &value_name(*out))
                .map_err(builder_error)?
                .try_as_basic_value()
                .basic()
                .ok_or_else(|| LlvmError::new("slice index returned void"))?;
            values.insert(*out, result);
        }
        Inst::SliceStore {
            slice,
            index,
            value,
        } => {
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
            let mut args = slice_parts(builder, values[slice].into_struct_value())?;
            args.push(values[index].into());
            args.push(values[value].into());
            builder
                .build_call(set, &args, "slice.set")
                .map_err(builder_error)?;
        }
        Inst::SliceAppend { out, slice, value } => {
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
            let result = call_slice_return(
                context,
                module,
                builder,
                slice_result_slots[out],
                "mako_native_int_slice_append",
                function_type,
                &{
                    let mut args = slice_parts(builder, values[slice].into_struct_value())?;
                    args.push(values[value].into());
                    args
                },
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
            // Deep copy: allocate a fresh block and copy each field, recursively
            // cloning owned (string) fields. A shallow memcpy would make the copy
            // and the original share (and double-free) the same string buffer.
            let struct_ty = struct_types[*struct_id as usize];
            let fields = &layouts[*struct_id as usize].fields;
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
            let base_ptr = values[base].into_pointer_value();
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
                                context.ptr_type(Default::default()).into(),
                                context.ptr_type(Default::default()).into(),
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
                    _ => loaded,
                };
                let dst = builder
                    .build_struct_gep(struct_ty, ptr, index as u32, "clone.dst")
                    .map_err(|_| LlvmError::new("LLVM backend: struct clone GEP failed"))?;
                builder.build_store(dst, stored).map_err(builder_error)?;
            }
            values.insert(*out, ptr.into());
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
            // Drop owned (string) fields, then free the block.
            let struct_ty = struct_types[*struct_id as usize];
            let fields = &layouts[*struct_id as usize].fields;
            let base_ptr = values[value].into_pointer_value();
            for (index, (_, field_ty)) in fields.iter().enumerate() {
                match field_ty {
                    Type::Str => {
                        let gep = builder
                            .build_struct_gep(struct_ty, base_ptr, index as u32, "drop.field")
                            .map_err(|_| LlvmError::new("LLVM backend: struct drop GEP failed"))?;
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
                        let gep = builder
                            .build_struct_gep(struct_ty, base_ptr, index as u32, "drop.field")
                            .map_err(|_| LlvmError::new("LLVM backend: struct drop GEP failed"))?;
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
        }
        Inst::StrSliceLiteral { .. }
        | Inst::StrSliceMake { .. }
        | Inst::StrSliceLen { .. }
        | Inst::StrSliceIndex { .. }
        | Inst::StrSliceStore { .. }
        | Inst::StrSliceAppend { .. }
        | Inst::StrSliceSlice { .. }
        | Inst::StrSliceClone { .. }
        | Inst::DropStrSlice { .. } => {
            return Err(LlvmError::new("LLVM backend: []string ABI is not implemented yet"));
        }
    }
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
    let slice_type = llvm_type(context, Type::IntSlice);
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
