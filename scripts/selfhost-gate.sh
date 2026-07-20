#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
stage0="${MAKO_STAGE0:-$repo_dir/target/debug/mako}"
stage1="${MAKO_STAGE1_OUT:-/tmp/makoc-stage1}"
export MAKO_RUNTIME="${MAKO_SELFHOST_RUNTIME:-$repo_dir/runtime}"
# Stage 0 still performs recursive typed/codegen walks over this monolithic
# bootstrap package. Stage 1 uses flat arenas and does not inherit this setting.
export MAKO_COMPILER_STACK_MB="${MAKO_SELFHOST_STACK_MB:-64}"

if [[ ! -x "$stage0" ]]; then
  cargo build --manifest-path "$repo_dir/Cargo.toml"
fi

"$stage0" check "$repo_dir/compiler/main.mko" --no-incremental
"$stage0" check "$repo_dir/compiler/testdata/literals.mko" --no-incremental
"$stage0" build "$repo_dir/compiler/main.mko" --no-incremental -o "$stage1"

for source in \
  "$repo_dir/examples/hello.mko" \
  "$repo_dir/examples/integers.mko" \
  "$repo_dir/examples/concurrency.mko" \
  "$repo_dir/compiler/testdata/structure.mko" \
  "$repo_dir/compiler/testdata/literals.mko" \
  "$repo_dir/compiler/lexer.mko" \
  "$repo_dir/compiler/parser.mko" \
  "$repo_dir/compiler/z_expression.mko" \
  "$repo_dir/compiler/z_statement.mko" \
  "$repo_dir/compiler/z_type.mko" \
  "$repo_dir/compiler/zz_expression_attach.mko" \
  "$repo_dir/compiler/zz_symbols.mko" \
  "$repo_dir/compiler/zzz_resolution.mko" \
  "$repo_dir/compiler/zzzz_locals.mko" \
  "$repo_dir/compiler/zzzzz_types.mko" \
  "$repo_dir/compiler/zzzzzz_ir.mko" \
  "$repo_dir/compiler/main.mko"
do
  result="$($stage1 "$source")"
  if [[ "$result" != tokens\ *\ items\ * ]]; then
    echo "selfhost frontend gate: unexpected output for $source: $result" >&2
    exit 1
  fi
done

literal_result="$($stage1 "$repo_dir/compiler/testdata/literals.mko")"
if [[ "$literal_result" != "tokens 172 items 3 types 0 signatures 1 parameters 0 statements 13 bodies 1 expressions 79 symbols 3 resolved 3 locals 10 local_refs 10 typed 38 ir_functions 0 ir_blocks 0 ir_instructions 0 ir_constants 0 ir_values 0 ir_skipped 1 ir_verified 0" ]]; then
  echo "selfhost frontend gate: literal arena shape changed: $literal_result" >&2
  exit 1
fi

typed_ir_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir.mko")"
if [[ "$typed_ir_result" != "tokens 42 items 1 types 3 signatures 1 parameters 2 statements 6 bodies 1 expressions 10 symbols 1 resolved 0 locals 7 local_refs 4 typed 10 ir_functions 1 ir_blocks 1 ir_instructions 10 ir_constants 4 ir_values 8 ir_skipped 0 ir_verified 8" ]]; then
  echo "selfhost frontend gate: typed IR shape changed: $typed_ir_result" >&2
  exit 1
fi

typed_ir_cfg_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_cfg.mko")"
if [[ "$typed_ir_cfg_result" != "tokens 22 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 5 symbols 1 resolved 0 locals 1 local_refs 2 typed 5 ir_functions 1 ir_blocks 3 ir_instructions 7 ir_constants 2 ir_values 4 ir_skipped 0 ir_verified 4" ]]; then
  echo "selfhost frontend gate: typed IR CFG shape changed: $typed_ir_cfg_result" >&2
  exit 1
fi

typed_ir_if_else_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_if_else.mko")"
if [[ "$typed_ir_if_else_result" != "tokens 25 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 5 symbols 1 resolved 0 locals 1 local_refs 2 typed 5 ir_functions 1 ir_blocks 4 ir_instructions 9 ir_constants 2 ir_values 5 ir_skipped 0 ir_verified 5" ]]; then
  echo "selfhost frontend gate: typed IR if/else shape changed: $typed_ir_if_else_result" >&2
  exit 1
fi

typed_ir_merge_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_merge.mko")"
if [[ "$typed_ir_merge_result" != "tokens 35 items 1 types 2 signatures 1 parameters 1 statements 5 bodies 1 expressions 7 symbols 1 resolved 0 locals 4 local_refs 2 typed 7 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 4 ir_values 6 ir_skipped 0 ir_verified 6" ]]; then
  echo "selfhost frontend gate: typed IR merge shape changed: $typed_ir_merge_result" >&2
  exit 1
fi

typed_ir_statement_merge_owned_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_statement_merge_owned.mko")"
if [[ "$typed_ir_statement_merge_owned_result" != "tokens 43 items 2 types 3 signatures 2 parameters 1 statements 5 bodies 2 expressions 7 symbols 2 resolved 2 locals 3 local_refs 1 typed 5 ir_functions 2 ir_blocks 5 ir_instructions 13 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" ]]; then
  echo "selfhost frontend gate: typed IR owned statement-merge shape changed: $typed_ir_statement_merge_owned_result" >&2
  exit 1
fi

typed_ir_owned_drop_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_drop.mko")"
if [[ "$typed_ir_owned_drop_result" != "tokens 21 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 8 symbols 1 resolved 0 locals 1 local_refs 0 typed 7 ir_functions 1 ir_blocks 1 ir_instructions 9 ir_constants 4 ir_values 7 ir_skipped 0 ir_verified 7" ]]; then
  echo "selfhost frontend gate: typed IR owned-drop shape changed: $typed_ir_owned_drop_result" >&2
  exit 1
fi

typed_ir_owned_move_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_move.mko")"
if [[ "$typed_ir_owned_move_result" != "tokens 23 items 1 types 2 signatures 1 parameters 0 statements 2 bodies 1 expressions 8 symbols 1 resolved 0 locals 1 local_refs 1 typed 7 ir_functions 1 ir_blocks 1 ir_instructions 8 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7" ]]; then
  echo "selfhost frontend gate: typed IR owned-move shape changed: $typed_ir_owned_move_result" >&2
  exit 1
fi

typed_ir_owned_cfg_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_cfg.mko")"
if [[ "$typed_ir_owned_cfg_result" != "tokens 37 items 1 types 7 signatures 1 parameters 3 statements 3 bodies 1 expressions 3 symbols 1 resolved 0 locals 3 local_refs 3 typed 3 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 0 ir_values 4 ir_skipped 0 ir_verified 4" ]]; then
  echo "selfhost frontend gate: typed IR owned-CFG shape changed: $typed_ir_owned_cfg_result" >&2
  exit 1
fi

typed_ir_owned_string_drop_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_string_drop.mko")"
if [[ "$typed_ir_owned_string_drop_result" != "tokens 15 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 2 symbols 1 resolved 0 locals 1 local_refs 0 typed 2 ir_functions 1 ir_blocks 1 ir_instructions 4 ir_constants 2 ir_values 2 ir_skipped 0 ir_verified 2" ]]; then
  echo "selfhost frontend gate: typed IR string-drop shape changed: $typed_ir_owned_string_drop_result" >&2
  exit 1
fi

typed_ir_owned_string_move_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_string_move.mko")"
if [[ "$typed_ir_owned_string_move_result" != "tokens 15 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 2 symbols 1 resolved 0 locals 1 local_refs 1 typed 2 ir_functions 1 ir_blocks 1 ir_instructions 3 ir_constants 1 ir_values 2 ir_skipped 0 ir_verified 2" ]]; then
  echo "selfhost frontend gate: typed IR string-move shape changed: $typed_ir_owned_string_move_result" >&2
  exit 1
fi

typed_ir_owned_string_array_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_string_array.mko")"
if [[ "$typed_ir_owned_string_array_result" != "tokens 19 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 6 symbols 1 resolved 0 locals 1 local_refs 0 typed 5 ir_functions 1 ir_blocks 1 ir_instructions 7 ir_constants 3 ir_values 5 ir_skipped 0 ir_verified 5" ]]; then
  echo "selfhost frontend gate: typed IR string-array ownership shape changed: $typed_ir_owned_string_array_result" >&2
  exit 1
fi

typed_ir_call_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_call.mko")"
if [[ "$typed_ir_call_result" != "tokens 27 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 4 symbols 2 resolved 1 locals 1 local_refs 1 typed 3 ir_functions 2 ir_blocks 2 ir_instructions 5 ir_constants 1 ir_values 3 ir_skipped 0 ir_verified 3" ]]; then
  echo "selfhost frontend gate: typed IR call shape changed: $typed_ir_call_result" >&2
  exit 1
fi

typed_ir_owned_call_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_call_result.mko")"
if [[ "$typed_ir_owned_call_result" != "tokens 23 items 2 types 2 signatures 2 parameters 0 statements 2 bodies 2 expressions 3 symbols 2 resolved 1 locals 0 local_refs 0 typed 2 ir_functions 2 ir_blocks 2 ir_instructions 6 ir_constants 1 ir_values 4 ir_skipped 0 ir_verified 4" ]]; then
  echo "selfhost frontend gate: typed IR owned call-result shape changed: $typed_ir_owned_call_result" >&2
  exit 1
fi

typed_ir_owned_call_argument_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_owned_call_argument.mko")"
if [[ "$typed_ir_owned_call_argument_result" != "tokens 27 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 4 symbols 2 resolved 1 locals 1 local_refs 1 typed 3 ir_functions 2 ir_blocks 2 ir_instructions 7 ir_constants 1 ir_values 5 ir_skipped 0 ir_verified 5" ]]; then
  echo "selfhost frontend gate: typed IR owned call-argument policy changed: $typed_ir_owned_call_argument_result" >&2
  exit 1
fi

typed_ir_multi_call_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_multi_call.mko")"
if [[ "$typed_ir_multi_call_result" != "tokens 35 items 2 types 4 signatures 2 parameters 2 statements 2 bodies 2 expressions 8 symbols 2 resolved 1 locals 2 local_refs 2 typed 7 ir_functions 2 ir_blocks 2 ir_instructions 12 ir_constants 2 ir_values 8 ir_skipped 0 ir_verified 8" ]]; then
  echo "selfhost frontend gate: typed IR multi-call shape changed: $typed_ir_multi_call_result" >&2
  exit 1
fi

typed_ir_call_clone_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_call_clone.mko")"
if [[ "$typed_ir_call_clone_result" != "tokens 35 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 6 symbols 2 resolved 1 locals 3 local_refs 3 typed 5 ir_functions 2 ir_blocks 2 ir_instructions 9 ir_constants 1 ir_values 6 ir_skipped 0 ir_verified 6" ]]; then
  echo "selfhost frontend gate: typed IR call-clone policy changed: $typed_ir_call_clone_result" >&2
  exit 1
fi

typed_ir_cfg_call_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_cfg_call.mko")"
if [[ "$typed_ir_cfg_call_result" != "tokens 39 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 8 symbols 2 resolved 2 locals 2 local_refs 2 typed 6 ir_functions 2 ir_blocks 4 ir_instructions 10 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" ]]; then
  echo "selfhost frontend gate: typed IR CFG-call shape changed: $typed_ir_cfg_call_result" >&2
  exit 1
fi

typed_ir_cfg_owned_call_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_cfg_owned_call_result.mko")"
if [[ "$typed_ir_cfg_owned_call_result" != "tokens 34 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 6 symbols 2 resolved 2 locals 1 local_refs 1 typed 4 ir_functions 2 ir_blocks 4 ir_instructions 9 ir_constants 1 ir_values 5 ir_skipped 0 ir_verified 5" ]]; then
  echo "selfhost frontend gate: typed IR owned CFG-call result shape changed: $typed_ir_cfg_owned_call_result" >&2
  exit 1
fi

typed_ir_cfg_owned_call_argument="$($stage1 "$repo_dir/compiler/testdata/typed_ir_cfg_owned_call_argument.mko")"
if [[ "$typed_ir_cfg_owned_call_argument" != "tokens 40 items 2 types 5 signatures 2 parameters 3 statements 4 bodies 2 expressions 6 symbols 2 resolved 1 locals 3 local_refs 2 typed 5 ir_functions 2 ir_blocks 4 ir_instructions 14 ir_constants 2 ir_values 7 ir_skipped 0 ir_verified 7" ]]; then
  echo "selfhost frontend gate: typed IR owned CFG-call argument shape changed: $typed_ir_cfg_owned_call_argument" >&2
  exit 1
fi

typed_ir_cfg_hold_call_argument="$($stage1 "$repo_dir/compiler/testdata/typed_ir_cfg_hold_call_argument.mko")"
if [[ "$typed_ir_cfg_hold_call_argument" != "tokens 41 items 2 types 4 signatures 2 parameters 2 statements 5 bodies 2 expressions 7 symbols 2 resolved 1 locals 3 local_refs 2 typed 6 ir_functions 2 ir_blocks 4 ir_instructions 12 ir_constants 3 ir_values 6 ir_skipped 0 ir_verified 6" ]]; then
  echo "selfhost frontend gate: typed IR hold CFG-call argument shape changed: $typed_ir_cfg_hold_call_argument" >&2
  exit 1
fi

if typed_ir_hold_after_move_result="$($stage1 "$repo_dir/compiler/testdata/typed_ir_call_hold_after_move.mko" 2>&1)"; then
  echo "selfhost frontend gate: hold binding unexpectedly remained usable after call transfer" >&2
  exit 1
fi
if [[ "$typed_ir_hold_after_move_result" != *": typed IR verification failed: IR instruction uses consumed owned value" ]]; then
  echo "selfhost frontend gate: unexpected hold use-after-move diagnostic: $typed_ir_hold_after_move_result" >&2
  exit 1
fi

if oversized_ir_result="$("$stage1" "$repo_dir/compiler/testdata/oversized_ir_integer.mko" 2>&1)"; then
  echo "selfhost frontend gate: oversized IR integer unexpectedly accepted" >&2
  exit 1
fi
if [[ "$oversized_ir_result" != *": typed IR lowering failed: integer constant is out of range" ]]; then
  echo "selfhost frontend gate: unexpected oversized IR integer diagnostic: $oversized_ir_result" >&2
  exit 1
fi

if "$stage1" "$repo_dir/compiler/testdata/unbalanced.mko" >/dev/null 2>&1; then
  echo "selfhost frontend gate: unbalanced source unexpectedly parsed" >&2
  exit 1
fi

if "$stage1" "$repo_dir/compiler/testdata/bad_signature.mko" >/dev/null 2>&1; then
  echo "selfhost frontend gate: malformed function signature unexpectedly parsed" >&2
  exit 1
fi

if "$stage1" "$repo_dir/compiler/testdata/bad_statement.mko" >/dev/null 2>&1; then
  echo "selfhost frontend gate: malformed statement unexpectedly parsed" >&2
  exit 1
fi

if "$stage1" "$repo_dir/compiler/testdata/duplicate_symbol.mko" >/dev/null 2>&1; then
  echo "selfhost frontend gate: duplicate top-level symbol unexpectedly accepted" >&2
  exit 1
fi

if "$stage1" "$repo_dir/compiler/testdata/duplicate_parameter.mko" >/dev/null 2>&1; then
  echo "selfhost frontend gate: duplicate function parameter unexpectedly accepted" >&2
  exit 1
fi

if call_type_result="$("$stage1" "$repo_dir/compiler/testdata/bad_call_type.mko" 2>&1)"; then
  echo "selfhost frontend gate: mismatched call argument type unexpectedly accepted" >&2
  exit 1
fi
if [[ "$call_type_result" != *":4:13: function call argument type does not match parameter" ]]; then
  echo "selfhost frontend gate: unexpected call type diagnostic: $call_type_result" >&2
  exit 1
fi

if call_arity_result="$("$stage1" "$repo_dir/compiler/testdata/bad_call_arity.mko" 2>&1)"; then
  echo "selfhost frontend gate: mismatched call arity unexpectedly accepted" >&2
  exit 1
fi
if [[ "$call_arity_result" != *":4:9: function call argument count does not match declaration" ]]; then
  echo "selfhost frontend gate: unexpected call arity diagnostic: $call_arity_result" >&2
  exit 1
fi

if return_type_result="$("$stage1" "$repo_dir/compiler/testdata/bad_return_type.mko" 2>&1)"; then
  echo "selfhost frontend gate: mismatched return type unexpectedly accepted" >&2
  exit 1
fi
if [[ "$return_type_result" != *":3:16: return value type does not match function result" ]]; then
  echo "selfhost frontend gate: unexpected return type diagnostic: $return_type_result" >&2
  exit 1
fi

if return_value_result="$("$stage1" "$repo_dir/compiler/testdata/bad_return_value.mko" 2>&1)"; then
  echo "selfhost frontend gate: value returned from result-less function unexpectedly accepted" >&2
  exit 1
fi
if [[ "$return_value_result" != *":2:12: function without result type cannot return a value" ]]; then
  echo "selfhost frontend gate: unexpected result-less return diagnostic: $return_value_result" >&2
  exit 1
fi

if return_missing_result="$("$stage1" "$repo_dir/compiler/testdata/bad_return_missing.mko" 2>&1)"; then
  echo "selfhost frontend gate: missing return value unexpectedly accepted" >&2
  exit 1
fi
if [[ "$return_missing_result" != *":2:5: return value required by function declaration" ]]; then
  echo "selfhost frontend gate: unexpected missing return diagnostic: $return_missing_result" >&2
  exit 1
fi

if binary_operator_result="$("$stage1" "$repo_dir/compiler/testdata/bad_binary_operator.mko" 2>&1)"; then
  echo "selfhost frontend gate: invalid binary operands unexpectedly accepted" >&2
  exit 1
fi
if [[ "$binary_operator_result" != *":2:21: type error: cannot apply arithmetic to operands" ]]; then
  echo "selfhost frontend gate: unexpected binary operator diagnostic: $binary_operator_result" >&2
  exit 1
fi

if unary_operator_result="$("$stage1" "$repo_dir/compiler/testdata/bad_unary_operator.mko" 2>&1)"; then
  echo "selfhost frontend gate: invalid unary operand unexpectedly accepted" >&2
  exit 1
fi
if [[ "$unary_operator_result" != *":2:19: invalid unary operand" ]]; then
  echo "selfhost frontend gate: unexpected unary operator diagnostic: $unary_operator_result" >&2
  exit 1
fi

if if_condition_result="$("$stage1" "$repo_dir/compiler/testdata/bad_if_condition.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-boolean if condition unexpectedly accepted" >&2
  exit 1
fi
if [[ "$if_condition_result" != *":2:8: if condition must be bool" ]]; then
  echo "selfhost frontend gate: unexpected if condition diagnostic: $if_condition_result" >&2
  exit 1
fi

if while_condition_result="$("$stage1" "$repo_dir/compiler/testdata/bad_while_condition.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-boolean while condition unexpectedly accepted" >&2
  exit 1
fi
if [[ "$while_condition_result" != *":2:11: while condition must be bool" ]]; then
  echo "selfhost frontend gate: unexpected while condition diagnostic: $while_condition_result" >&2
  exit 1
fi

if array_element_result="$("$stage1" "$repo_dir/compiler/testdata/bad_array_element.mko" 2>&1)"; then
  echo "selfhost frontend gate: heterogeneous array unexpectedly accepted" >&2
  exit 1
fi
if [[ "$array_element_result" != *":2:23: array element type mismatch" ]]; then
  echo "selfhost frontend gate: unexpected array element diagnostic: $array_element_result" >&2
  exit 1
fi

if array_index_result="$("$stage1" "$repo_dir/compiler/testdata/bad_array_index.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-integer array index unexpectedly accepted" >&2
  exit 1
fi
if [[ "$array_index_result" != *":2:26: index must be int" ]]; then
  echo "selfhost frontend gate: unexpected array index diagnostic: $array_index_result" >&2
  exit 1
fi

if slice_bound_result="$("$stage1" "$repo_dir/compiler/testdata/bad_slice_bound.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-integer slice bound unexpectedly accepted" >&2
  exit 1
fi
if [[ "$slice_bound_result" != *":2:26: slice low bound must be int" ]]; then
  echo "selfhost frontend gate: unexpected slice bound diagnostic: $slice_bound_result" >&2
  exit 1
fi

if make_length_result="$("$stage1" "$repo_dir/compiler/testdata/bad_make_length.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-integer make length unexpectedly accepted" >&2
  exit 1
fi
if [[ "$make_length_result" != *":2:31: make len must be int" ]]; then
  echo "selfhost frontend gate: unexpected make length diagnostic: $make_length_result" >&2
  exit 1
fi

if make_capacity_result="$("$stage1" "$repo_dir/compiler/testdata/bad_make_capacity.mko" 2>&1)"; then
  echo "selfhost frontend gate: non-integer make capacity unexpectedly accepted" >&2
  exit 1
fi
if [[ "$make_capacity_result" != *":2:34: make cap must be int" ]]; then
  echo "selfhost frontend gate: unexpected make capacity diagnostic: $make_capacity_result" >&2
  exit 1
fi

if make_missing_result="$("$stage1" "$repo_dir/compiler/testdata/bad_make_missing_length.mko" 2>&1)"; then
  echo "selfhost frontend gate: array make without length unexpectedly accepted" >&2
  exit 1
fi
if [[ "$make_missing_result" != *":2:19: make([]T) needs len: make([]int, n) or make([]int, n, cap)" ]]; then
  echo "selfhost frontend gate: unexpected missing make length diagnostic: $make_missing_result" >&2
  exit 1
fi

if make_map_result="$("$stage1" "$repo_dir/compiler/testdata/bad_make_map_capacity.mko" 2>&1)"; then
  echo "selfhost frontend gate: map make with capacity unexpectedly accepted" >&2
  exit 1
fi
if [[ "$make_map_result" != *":2:19: make(map[K]V) takes at most one size hint" ]]; then
  echo "selfhost frontend gate: unexpected map make diagnostic: $make_map_result" >&2
  exit 1
fi

if len_arity_result="$("$stage1" "$repo_dir/compiler/testdata/bad_len_arity.mko" 2>&1)"; then
  echo "selfhost frontend gate: len without argument unexpectedly accepted" >&2
  exit 1
fi
if [[ "$len_arity_result" != *":2:22: expected 1 args, got 0" ]]; then
  echo "selfhost frontend gate: unexpected len arity diagnostic: $len_arity_result" >&2
  exit 1
fi

if len_argument_result="$("$stage1" "$repo_dir/compiler/testdata/bad_len_argument.mko" 2>&1)"; then
  echo "selfhost frontend gate: scalar len argument unexpectedly accepted" >&2
  exit 1
fi
if [[ "$len_argument_result" != *":2:23: len needs slice/array/string/map/string_view, got int" ]]; then
  echo "selfhost frontend gate: unexpected len argument diagnostic: $len_argument_result" >&2
  exit 1
fi

if cap_argument_result="$("$stage1" "$repo_dir/compiler/testdata/bad_cap_argument.mko" 2>&1)"; then
  echo "selfhost frontend gate: string cap argument unexpectedly accepted" >&2
  exit 1
fi
if [[ "$cap_argument_result" != *":2:23: cap needs slice ([]int), got string" ]]; then
  echo "selfhost frontend gate: unexpected cap argument diagnostic: $cap_argument_result" >&2
  exit 1
fi

if format_argument_result="$("$stage1" "$repo_dir/compiler/testdata/bad_format_int_argument.mko" 2>&1)"; then
  echo "selfhost frontend gate: string format_int argument unexpectedly accepted" >&2
  exit 1
fi
if [[ "$format_argument_result" != *":2:30: argument type mismatch: expected int, got string" ]]; then
  echo "selfhost frontend gate: unexpected format_int diagnostic: $format_argument_result" >&2
  exit 1
fi

echo "selfhost frontend gate: ok"
