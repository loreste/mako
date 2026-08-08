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

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# Generated images are checksummed and (on Linux x86-64) executed; keep them in
# a private workspace so no local process can pre-plant or swap them.
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-selfhost-gate.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

"$stage0" check "$repo_dir/compiler/main.mko" --no-incremental
# Backend units are package members of main.mko; check them explicitly so a
# missing or broken file cannot hide behind an incomplete directory listing.
"$stage0" check "$repo_dir/compiler/x86_64.mko" --no-incremental
"$stage0" check "$repo_dir/compiler/elf64.mko" --no-incremental
"$stage0" check "$repo_dir/compiler/argv_native.mko" --no-incremental
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
  "$repo_dir/compiler/argv_native.mko" \
  "$repo_dir/compiler/elf64.mko" \
  "$repo_dir/compiler/main.mko"
do
  result="$("$stage1" "$source")"
  if [[ "$result" != *tokens\ *\ items\ * ]]; then
    echo "selfhost frontend gate: unexpected output for $source: $result" >&2
    exit 1
  fi
done

# `x86_64.mko` is typechecked and linked into stage 1 via the package build of
# main.mko (and `x86_64_smoke_test` runs at stage-1 startup). Single-file stage-1
# processing of that unit still fails multi-file call resolution — tracked in
# docs/SELF_HOSTING_PROGRESS.md, not re-run here (full-file stage-1 is minutes).

literal_result="$("$stage1" "$repo_dir/compiler/testdata/literals.mko")"
if [[ "$literal_result" != $'tokens 172 items 3 types 0 signatures 1 parameters 0 statements 13 bodies 1 expressions 79 symbols 3 resolved 3 locals 10 local_refs 10 typed 53 ir_functions 0 ir_blocks 0 ir_instructions 0 ir_constants 0 ir_values 0 ir_skipped 1 ir_verified 0' ]]; then
  echo "selfhost frontend gate: literal arena shape changed: $literal_result" >&2
  exit 1
fi

typed_ir_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir.mko")"
if [[ "$typed_ir_result" != $'tokens 42 items 1 types 3 signatures 1 parameters 2 statements 6 bodies 1 expressions 10 symbols 1 resolved 0 locals 7 local_refs 4 typed 10 ir_functions 1 ir_blocks 1 ir_instructions 10 ir_constants 4 ir_values 8 ir_skipped 0 ir_verified 8' ]]; then
  echo "selfhost frontend gate: typed IR shape changed: $typed_ir_result" >&2
  exit 1
fi

elf_exit_result="$("$stage1" "$repo_dir/compiler/testdata/elf_exit.mko")"
if [[ "$elf_exit_result" != $'elf_bytes 132\ntokens 11 items 1 types 1 signatures 1 parameters 0 statements 1 bodies 1 expressions 1 symbols 1 resolved 0 locals 0 local_refs 0 typed 1 ir_functions 1 ir_blocks 1 ir_instructions 2 ir_constants 1 ir_values 1 ir_skipped 0 ir_verified 1' ]]; then
  echo "selfhost backend gate: ELF exit image changed: $elf_exit_result" >&2
  exit 1
fi
elf_exit_path="$work_dir/elf-exit"
"$stage1" "$repo_dir/compiler/testdata/elf_exit.mko" "$elf_exit_path" >/dev/null
if [[ "$(sha256_of "$elf_exit_path")" != "d93aa7e7fd49e17fb09ac7cd6b0cd0776dea9e40503595ae28b1abf7c71c48c7" ]]; then
  echo "selfhost backend gate: ELF exit image bytes changed" >&2
  exit 1
fi
if ! file "$elf_exit_path" | grep -q "ELF 64-bit.*x86-64"; then
  echo "selfhost backend gate: generated image is not ELF64 x86-64" >&2
  file "$elf_exit_path" >&2 || true
  exit 1
fi

elf_add_result="$("$stage1" "$repo_dir/compiler/testdata/elf_add_exit.mko")"
if [[ "$elf_add_result" != $'elf_bytes 138\ntokens 13 items 1 types 1 signatures 1 parameters 0 statements 1 bodies 1 expressions 3 symbols 1 resolved 0 locals 0 local_refs 0 typed 3 ir_functions 1 ir_blocks 1 ir_instructions 4 ir_constants 2 ir_values 3 ir_skipped 0 ir_verified 3' ]]; then
  echo "selfhost backend gate: ELF add image changed: $elf_add_result" >&2
  exit 1
fi

elf_add_path="$work_dir/elf-add"
"$stage1" "$repo_dir/compiler/testdata/elf_add_exit.mko" "$elf_add_path" >/dev/null
if [[ "$(sha256_of "$elf_add_path")" != "07ded8bb7bba3ce548de33d7879b9db948a5bf0ba7e5aa83913074fe70f84286" ]]; then
  echo "selfhost backend gate: ELF add image bytes changed" >&2
  exit 1
fi

elf_syscall_result="$("$stage1" "$repo_dir/compiler/testdata/elf_syscall_exit.mko")"
if [[ "$elf_syscall_result" != $'elf_bytes 244\ntokens 26 items 1 types 1 signatures 1 parameters 0 statements 1 bodies 1 expressions 15 symbols 1 resolved 0 locals 0 local_refs 0 typed 14 ir_functions 1 ir_blocks 1 ir_instructions 9 ir_constants 7 ir_values 8 ir_skipped 0 ir_verified 8' ]]; then
  echo "selfhost backend gate: ELF syscall image changed: $elf_syscall_result" >&2
  exit 1
fi
elf_syscall_path="$work_dir/elf-syscall"
"$stage1" "$repo_dir/compiler/testdata/elf_syscall_exit.mko" "$elf_syscall_path" >/dev/null
if [[ "$(sha256_of "$elf_syscall_path")" != "291e9f18b1634fa15379f0ce4631c702ce6babe9b249adfd2b8253a3de2e551d" ]]; then
  echo "selfhost backend gate: ELF syscall image bytes changed" >&2
  exit 1
fi
if ! file "$elf_syscall_path" | grep -q "ELF 64-bit.*x86-64"; then
  echo "selfhost backend gate: generated syscall image is not ELF64 x86-64" >&2
  exit 1
fi

elf_byte_result="$("$stage1" "$repo_dir/compiler/testdata/elf_byte_cast_exit.mko")"
if [[ "$elf_byte_result" != $'elf_bytes 190\ntokens 21 items 1 types 2 signatures 1 parameters 1 statements 2 bodies 1 expressions 4 symbols 1 resolved 0 locals 2 local_refs 2 typed 3 ir_functions 1 ir_blocks 1 ir_instructions 3 ir_constants 0 ir_values 2 ir_skipped 0 ir_verified 2' ]]; then
  echo "selfhost backend gate: ELF byte-cast image changed: $elf_byte_result" >&2
  exit 1
fi
elf_byte_path="$work_dir/elf-byte"
"$stage1" "$repo_dir/compiler/testdata/elf_byte_cast_exit.mko" "$elf_byte_path" >/dev/null
if [[ "$(sha256_of "$elf_byte_path")" != "8d6a05c813fb28614a39ebb8289a8abe91bef3da968c1b7be14186ed6e5ca5f2" ]]; then
echo "selfhost backend gate: ELF byte-cast image bytes changed" >&2
exit 1
fi

elf_unsafe_result="$($stage1 "$repo_dir/compiler/testdata/elf_unsafe_memory.mko")"
if [[ "$elf_unsafe_result" != $'elf_bytes 284\ntokens 43 items 1 types 1 signatures 1 parameters 0 statements 3 bodies 1 expressions 24 symbols 1 resolved 0 locals 2 local_refs 2 typed 21 ir_functions 1 ir_blocks 1 ir_instructions 13 ir_constants 8 ir_values 12 ir_skipped 0 ir_verified 12' ]]; then
 echo "selfhost backend gate: ELF unsafe-memory image changed: $elf_unsafe_result" >&2
 exit 1
fi
elf_unsafe_path="$work_dir/elf-unsafe-memory"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_memory.mko" "$elf_unsafe_path" >/dev/null
if [[ "$(sha256_of "$elf_unsafe_path")" != "57078565ec4a3b24e480786f3e0befb411fd5f31fd3b09f74bc233e390e03f1d" ]]; then
 echo "selfhost backend gate: ELF unsafe-memory image bytes changed" >&2
 exit 1
fi
if ! file "$elf_unsafe_path" | grep -q "ELF 64-bit.*x86-64"; then
 echo "selfhost backend gate: unsafe-memory image not ELF64 x86-64" >&2
 exit 1
fi
elf_write_result="$($stage1 "$repo_dir/compiler/testdata/elf_unsafe_write.mko")"
if [[ "$elf_write_result" != $'elf_bytes 394\ntokens 49 items 1 types 1 signatures 1 parameters 0 statements 6 bodies 1 expressions 28 symbols 1 resolved 0 locals 2 local_refs 5 typed 23 ir_functions 1 ir_blocks 1 ir_instructions 14 ir_constants 7 ir_values 13 ir_skipped 0 ir_verified 13' ]]; then
 echo "selfhost backend gate: ELF unsafe-write image changed: $elf_write_result" >&2
 exit 1
fi
elf_write_path="$work_dir/elf-unsafe-write"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_write.mko" "$elf_write_path" >/dev/null
if [[ "$(sha256_of "$elf_write_path")" != "285daec49ff6342df8354f006dfa5153cc157fad07e016e09ad30116a93714b8" ]]; then
 echo "selfhost backend gate: ELF unsafe-write image bytes changed" >&2
 exit 1
fi
elf_make_byte_result="$($stage1 "$repo_dir/compiler/testdata/elf_make_byte_len.mko")"
if [[ "$elf_make_byte_result" != $'elf_bytes 278\ntokens 25 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 7 symbols 1 resolved 0 locals 1 local_refs 1 typed 5 ir_functions 1 ir_blocks 1 ir_instructions 5 ir_constants 1 ir_values 3 ir_skipped 0 ir_verified 3' ]]; then
 echo "selfhost backend gate: ELF byte-make image changed: $elf_make_byte_result" >&2
 exit 1
fi
elf_make_byte_path="$work_dir/elf-make-byte"
"$stage1" "$repo_dir/compiler/testdata/elf_make_byte_len.mko" "$elf_make_byte_path" >/dev/null
if [[ "$(sha256_of "$elf_make_byte_path")" != "bdaaa8623b96b39f41f641957f3555a0cf85c4e2f96d3fef0f9efffc60f04ed2" ]]; then
 echo "selfhost backend gate: ELF byte-make image bytes changed" >&2
 exit 1
fi
elf_make_index_result="$($stage1 "$repo_dir/compiler/testdata/elf_make_byte_index.mko")"
if [[ "$elf_make_index_result" != *"elf_bytes 342"* || "$elf_make_index_result" != *"ir_verified 4"* ]]; then
 echo "selfhost backend gate: ELF byte-index image changed: $elf_make_index_result" >&2
 exit 1
fi
elf_make_index_path="$work_dir/elf-make-byte-index"
"$stage1" "$repo_dir/compiler/testdata/elf_make_byte_index.mko" "$elf_make_index_path" >/dev/null
if [[ "$(sha256_of "$elf_make_index_path")" != "e3ce6063cb77464ba988b23e7c3384957daec226388d5c1b0cec8e02d6f4f4ac" ]]; then
 echo "selfhost backend gate: ELF byte-index image bytes changed" >&2
 exit 1
fi
elf_make_append_result="$($stage1 "$repo_dir/compiler/testdata/elf_make_byte_append.mko")"
if [[ "$elf_make_append_result" != *"elf_bytes 488"* || "$elf_make_append_result" != *"ir_verified 6"* ]]; then
 echo "selfhost backend gate: ELF byte-append image changed: $elf_make_append_result" >&2
 exit 1
fi
elf_make_append_path="$work_dir/elf-make-byte-append"
"$stage1" "$repo_dir/compiler/testdata/elf_make_byte_append.mko" "$elf_make_append_path" >/dev/null
if [[ "$(sha256_of "$elf_make_append_path")" != "14c4c447a7263fcab07b86c2545564500948f21696d146433a522300ddb3089d" ]]; then
 echo "selfhost backend gate: ELF byte-append image bytes changed" >&2
 exit 1
fi
elf_make_grow_result="$($stage1 "$repo_dir/compiler/testdata/elf_make_byte_grow.mko")"
if [[ "$elf_make_grow_result" != *"elf_bytes 488"* || "$elf_make_grow_result" != *"ir_verified 6"* ]]; then
 echo "selfhost backend gate: ELF byte-grow image changed: $elf_make_grow_result" >&2
 exit 1
fi
elf_make_grow_path="$work_dir/elf-make-byte-grow"
"$stage1" "$repo_dir/compiler/testdata/elf_make_byte_grow.mko" "$elf_make_grow_path" >/dev/null
if [[ "$(sha256_of "$elf_make_grow_path")" != "fffd6d6f7d31969ffe99a298235e81af6cc3a8a85230b3f0746a8022fd158bc3" ]]; then
 echo "selfhost backend gate: ELF byte-grow image bytes changed" >&2
 exit 1
fi
elf_make_grow_zero_result="$($stage1 "$repo_dir/compiler/testdata/elf_make_byte_grow_zero.mko")"
if [[ "$elf_make_grow_zero_result" != *"elf_bytes 702"* || "$elf_make_grow_zero_result" != *"ir_verified 9"* ]]; then
 echo "selfhost backend gate: ELF zero-capacity grow image changed: $elf_make_grow_zero_result" >&2
 exit 1
fi
elf_make_grow_zero_path="$work_dir/elf-make-byte-grow-zero"
"$stage1" "$repo_dir/compiler/testdata/elf_make_byte_grow_zero.mko" "$elf_make_grow_zero_path" >/dev/null
if [[ "$(sha256_of "$elf_make_grow_zero_path")" != "a3dd7c79c7121a8420989c3ea929874eb1453c0be9f262fd8445a804dc8b2b77" ]]; then
 echo "selfhost backend gate: ELF zero-capacity grow image bytes changed" >&2
 exit 1
fi
elf_write_bytes_result="$($stage1 "$repo_dir/compiler/testdata/elf_write_byte_slice.mko")"
if [[ "$elf_write_bytes_result" != *"elf_bytes 306"* || "$elf_write_bytes_result" != *"ir_verified 4"* ]]; then
 echo "selfhost backend gate: ELF byte-slice write image changed: $elf_write_bytes_result" >&2
 exit 1
fi
elf_write_bytes_path="$work_dir/elf-write-byte-slice"
"$stage1" "$repo_dir/compiler/testdata/elf_write_byte_slice.mko" "$elf_write_bytes_path" >/dev/null
if [[ "$(sha256_of "$elf_write_bytes_path")" != "cac4304ad3cb107a33b546c08fa6f66a9421214c6687fe5a4b879c27283fdf9f" ]]; then
 echo "selfhost backend gate: ELF byte-slice write image bytes changed" >&2
 exit 1
fi
elf_read_bytes_result="$($stage1 "$repo_dir/compiler/testdata/elf_read_byte_slice.mko")"
if [[ "$elf_read_bytes_result" != *"elf_bytes 303"* || "$elf_read_bytes_result" != *"ir_verified 4"* ]]; then
 echo "selfhost backend gate: ELF byte-slice read image changed: $elf_read_bytes_result" >&2
 exit 1
fi
elf_read_bytes_path="$work_dir/elf-read-byte-slice"
"$stage1" "$repo_dir/compiler/testdata/elf_read_byte_slice.mko" "$elf_read_bytes_path" >/dev/null
if [[ "$(sha256_of "$elf_read_bytes_path")" != "be952913191ddff2bc9f1de875ea072b07fa599d7daee1c3aed5891b08e3ca22" ]]; then
 echo "selfhost backend gate: ELF byte-slice read image bytes changed" >&2
 exit 1
fi
elf_open_bytes_result="$($stage1 "$repo_dir/compiler/testdata/elf_open_byte_slice.mko")"
if [[ "$elf_open_bytes_result" != *"elf_bytes 324"* || "$elf_open_bytes_result" != *"ir_verified 3"* ]]; then
 echo "selfhost backend gate: ELF byte-slice open image changed: $elf_open_bytes_result" >&2
 exit 1
fi
elf_open_bytes_path="$work_dir/elf-open-byte-slice"
"$stage1" "$repo_dir/compiler/testdata/elf_open_byte_slice.mko" "$elf_open_bytes_path" >/dev/null
if [[ "$(sha256_of "$elf_open_bytes_path")" != "6b7dd63f6d8643221306ed682c8c3c2c7e7485de84e906b94c64aabcc7d84e6a" ]]; then
 echo "selfhost backend gate: ELF byte-slice open image bytes changed" >&2
 exit 1
fi

elf_argv_ptr_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_ptr.mko")"
if [[ "$elf_argv_ptr_result" != *"elf_bytes 175"* || "$elf_argv_ptr_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF argv pointer image changed: $elf_argv_ptr_result" >&2
  exit 1
fi
elf_argv_ptr_path="$work_dir/elf-unsafe-argv-ptr"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_ptr.mko" "$elf_argv_ptr_path" >/dev/null
if [[ "$(sha256_of "$elf_argv_ptr_path")" != "192ad4f351a466347b11246820572f96fb6db3064adf2c18078911bef198d472" ]]; then
  echo "selfhost backend gate: ELF argv pointer image bytes changed" >&2
  exit 1
fi
elf_argv_len_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_len.mko")"
if [[ "$elf_argv_len_result" != *"elf_bytes 192"* || "$elf_argv_len_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF argv length image changed: $elf_argv_len_result" >&2
  exit 1
fi
elf_argv_len_path="$work_dir/elf-unsafe-argv-len"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_len.mko" "$elf_argv_len_path" >/dev/null
if [[ "$(sha256_of "$elf_argv_len_path")" != "ce43bc3f647a065b022ffd8855398b8bdda23c77bd727c4e5d96e5e321a0c970" ]]; then
  echo "selfhost backend gate: ELF argv length image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_argv_len_arity.mko" "$work_dir/bad-unsafe-argv-len" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid argv length arity accepted" >&2
  exit 1
fi
elf_open_string_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_open_string.mko")"
if [[ "$elf_open_string_result" != *"elf_bytes 225"* || "$elf_open_string_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF open-string image changed: $elf_open_string_result" >&2
  exit 1
fi
elf_open_string_path="$work_dir/elf-unsafe-open-string"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_open_string.mko" "$elf_open_string_path" >/dev/null
if [[ "$(sha256_of "$elf_open_string_path")" != "160669d045e79909cfccbda5631277ac1c5414c4de55d95ab517e3ae68708c82" ]]; then
  echo "selfhost backend gate: ELF open-string image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_open_string_arity.mko" "$work_dir/bad-open-string" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid open-string arity accepted" >&2
  exit 1
fi
elf_read_all_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_read_all.mko")"
if [[ "$elf_read_all_result" != *"elf_bytes 230"* || "$elf_read_all_result" != *"ir_verified 5"* ]]; then
  echo "selfhost backend gate: ELF read-all image changed: $elf_read_all_result" >&2
  exit 1
fi
elf_read_all_path="$work_dir/elf-unsafe-read-all"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_read_all.mko" "$elf_read_all_path" >/dev/null
if [[ "$(sha256_of "$elf_read_all_path")" != "8c4d158629b2f70a109cfc9b828fac2b59d3d4d3f15cec56a43b41634524ef75" ]]; then
  echo "selfhost backend gate: ELF read-all image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_read_all_arity.mko" "$work_dir/bad-read-all" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid read-all arity accepted" >&2
  exit 1
fi
elf_write_all_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_write_all.mko")"
if [[ "$elf_write_all_result" != *"elf_bytes 227"* || "$elf_write_all_result" != *"ir_verified 5"* ]]; then
  echo "selfhost backend gate: ELF write-all image changed: $elf_write_all_result" >&2
  exit 1
fi
elf_write_all_path="$work_dir/elf-unsafe-write-all"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_write_all.mko" "$elf_write_all_path" >/dev/null
if [[ "$(sha256_of "$elf_write_all_path")" != "805887a23359507e0927323c8af8a6121cdf86594072ff8da04f46d0f4810ac0" ]]; then
  echo "selfhost backend gate: ELF write-all image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_write_all_arity.mko" "$work_dir/bad-write-all" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid write-all arity accepted" >&2
  exit 1
fi
elf_write_file_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_write_file.mko")"
if [[ "$elf_write_file_result" != *"elf_bytes 364"* || "$elf_write_file_result" != *"ir_verified 4"* ]]; then
  echo "selfhost backend gate: ELF write-file image changed: $elf_write_file_result" >&2
  exit 1
fi
elf_write_file_path="$work_dir/elf-unsafe-write-file"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_write_file.mko" "$elf_write_file_path" >/dev/null
if [[ "$(sha256_of "$elf_write_file_path")" != "ef4dc994de9d0f5f704b2c2146663491edec017f955edb5b6518c3aa512f746c" ]]; then
  echo "selfhost backend gate: ELF write-file image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_write_file_arity.mko" "$work_dir/bad-write-file" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid write-file arity accepted" >&2
  exit 1
fi
elf_read_file_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_read_file.mko")"
if [[ "$elf_read_file_result" != *"elf_bytes 638"* || "$elf_read_file_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF read-file image changed: $elf_read_file_result" >&2
  exit 1
fi
elf_read_file_path="$work_dir/elf-unsafe-read-file"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_read_file.mko" "$elf_read_file_path" >/dev/null
if [[ "$(sha256_of "$elf_read_file_path")" != "682d6d26ce1de0e6b33f3a8a06a24c117fce9ad87477deb33f3425a242188dd1" ]]; then
  echo "selfhost backend gate: ELF read-file image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_read_file_arity.mko" "$work_dir/bad-read-file" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid read-file arity accepted" >&2
  exit 1
fi
elf_close_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_close.mko")"
if [[ "$elf_close_result" != *"elf_bytes 211"* || "$elf_close_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF close image changed: $elf_close_result" >&2
  exit 1
fi
elf_close_path="$work_dir/elf-unsafe-close"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_close.mko" "$elf_close_path" >/dev/null
if [[ "$(sha256_of "$elf_close_path")" != "de84ccff7e9b9fa7647b55bd1b11ebf64ac84536fa7673f4d61a74acf4c28cc8" ]]; then
  echo "selfhost backend gate: ELF close image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_close_arity.mko" "$work_dir/bad-close" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid close arity accepted" >&2
  exit 1
fi
elf_arg_get_result="$("$stage1" "$repo_dir/compiler/testdata/elf_arg_get.mko")"
if [[ "$elf_arg_get_result" != *"elf_bytes 373"* || "$elf_arg_get_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF arg_get image changed: $elf_arg_get_result" >&2
  exit 1
fi
elf_arg_get_path="$work_dir/elf-arg-get"
"$stage1" "$repo_dir/compiler/testdata/elf_arg_get.mko" "$elf_arg_get_path" >/dev/null
if [[ "$(sha256_of "$elf_arg_get_path")" != "ebc05ef6acabfabe31327f3d4112e7227de820520129750ca2ad9743fd08cc71" ]]; then
  echo "selfhost backend gate: ELF arg_get image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_arg_get_arity.mko" "$work_dir/bad-arg-get" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid arg_get arity accepted" >&2
  exit 1
fi
elf_bytes_string_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_bytes_to_string.mko")"
if [[ "$elf_bytes_string_result" != *"elf_bytes 467"* || "$elf_bytes_string_result" != *"ir_verified 4"* ]]; then
  echo "selfhost backend gate: ELF bytes-to-string image changed: $elf_bytes_string_result" >&2
  exit 1
fi
elf_bytes_string_path="$work_dir/elf-unsafe-bytes-to-string"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_bytes_to_string.mko" "$elf_bytes_string_path" >/dev/null
if [[ "$(sha256_of "$elf_bytes_string_path")" != "62889744acbe8df96363b401e2a882bb96aae3b5e1076e23cd6cf73ebdb4fbb6" ]]; then
  echo "selfhost backend gate: ELF bytes-to-string image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_bytes_to_string_arity.mko" "$work_dir/bad-bytes-to-string" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid bytes-to-string arity accepted" >&2
  exit 1
fi
elf_argv_copy_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_copy.mko")"
if [[ "$elf_argv_copy_result" != *"elf_bytes 351"* || "$elf_argv_copy_result" != *"ir_verified 4"* ]]; then
  echo "selfhost backend gate: ELF argv copy image changed: $elf_argv_copy_result" >&2
  exit 1
fi
elf_argv_copy_path="$work_dir/elf-unsafe-argv-copy"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_copy.mko" "$elf_argv_copy_path" >/dev/null
if [[ "$(sha256_of "$elf_argv_copy_path")" != "c0267299aaa26c695d00a17b6fdfaaa02aa40086307d557d16e697c6be4c4e3c" ]]; then
  echo "selfhost backend gate: ELF argv copy image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_argv_copy_arity.mko" "$work_dir/bad-argv-copy" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid argv copy arity accepted" >&2
  exit 1
fi
elf_argc_result="$("$stage1" "$repo_dir/compiler/testdata/elf_argc.mko")"
if [[ "$elf_argc_result" != *"elf_bytes 160"* || "$elf_argc_result" != *"ir_verified 2"* ]]; then
  echo "selfhost backend gate: ELF argc image changed: $elf_argc_result" >&2
  exit 1
fi
elf_argc_path="$work_dir/elf-argc"
"$stage1" "$repo_dir/compiler/testdata/elf_argc.mko" "$elf_argc_path" >/dev/null
if [[ "$(sha256_of "$elf_argc_path")" != "eca6519fc544fe6e1d89e2eff2eb9cb13452d3a70082a0469131b1e27b896d1f" ]]; then
  echo "selfhost backend gate: ELF argc image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_argc_arity.mko" "$work_dir/bad-argc" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid argc arity accepted" >&2
  exit 1
fi
elf_argv_byte_result="$("$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_byte.mko")"
if [[ "$elf_argv_byte_result" != *"elf_bytes 194"* || "$elf_argv_byte_result" != *"ir_verified 3"* ]]; then
  echo "selfhost backend gate: ELF argv byte image changed: $elf_argv_byte_result" >&2
  exit 1
fi
elf_argv_byte_path="$work_dir/elf-unsafe-argv-byte"
"$stage1" "$repo_dir/compiler/testdata/elf_unsafe_argv_byte.mko" "$elf_argv_byte_path" >/dev/null
if [[ "$(sha256_of "$elf_argv_byte_path")" != "e16f75789105aef7dd88870582cc8e2fc4f9155ca8d1c00659e667c5ad17f63c" ]]; then
  echo "selfhost backend gate: ELF argv byte image bytes changed" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_unsafe_argv_byte_arity.mko" "$work_dir/bad-unsafe-argv-byte" >/dev/null 2>&1; then
  echo "selfhost backend gate: invalid argv byte arity accepted" >&2
  exit 1
fi
elf_select_result="$("$stage1" "$repo_dir/compiler/testdata/elf_select_exit.mko")"
if [[ "$elf_select_result" != $'elf_bytes 153\ntokens 23 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 3 symbols 1 resolved 0 locals 1 local_refs 1 typed 3 ir_functions 1 ir_blocks 4 ir_instructions 8 ir_constants 2 ir_values 4 ir_skipped 0 ir_verified 4' ]]; then
  echo "selfhost backend gate: ELF select image changed: $elf_select_result" >&2
  exit 1
fi
elf_select_path="$work_dir/elf-select"
"$stage1" "$repo_dir/compiler/testdata/elf_select_exit.mko" "$elf_select_path" >/dev/null
if [[ "$(sha256_of "$elf_select_path")" != "cc104ac95770524d196d9ac8a029bc3ac806a737d36f7b1ac5aec7c3602065ae" ]]; then
  echo "selfhost backend gate: ELF select image bytes changed" >&2
  exit 1
fi
if ! file "$elf_select_path" | grep -q "ELF 64-bit.*x86-64"; then
  echo "selfhost backend gate: generated select image is not ELF64 x86-64" >&2
  file "$elf_select_path" >&2 || true
  exit 1
fi

elf_compare_result="$("$stage1" "$repo_dir/compiler/testdata/elf_compare_exit.mko")"
if [[ "$elf_compare_result" != $'elf_bytes 158\ntokens 25 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 5 symbols 1 resolved 0 locals 1 local_refs 1 typed 5 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 3 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost backend gate: ELF compare image changed: $elf_compare_result" >&2
  exit 1
fi
elf_compare_path="$work_dir/elf-compare"
"$stage1" "$repo_dir/compiler/testdata/elf_compare_exit.mko" "$elf_compare_path" >/dev/null
if [[ "$(sha256_of "$elf_compare_path")" != "9c0ecdd5a4b18edf93653d5570640f4c69bb04a437672fbb1de547c42202e4ee" ]]; then
  echo "selfhost backend gate: ELF compare image bytes changed" >&2
  exit 1
fi

elf_call_path="$work_dir/elf-call"
"$stage1" "$repo_dir/compiler/testdata/typed_ir_call.mko" "$elf_call_path" >/dev/null
if [[ "$(sha256_of "$elf_call_path")" != "e2c1a382f885f4acb5df1a15c5a55e15a829bd3ff5f13c0dd2b01314b659e0aa" ]]; then
  echo "selfhost backend gate: ELF call image bytes changed" >&2
  exit 1
fi

elf_cfg_call_path="$work_dir/elf-cfg-call"
"$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_call.mko" "$elf_cfg_call_path" >/dev/null
if [[ "$(sha256_of "$elf_cfg_call_path")" != "e1985233da286db95e3e01d151dd6736d6d24b60858c429f328f197eccdd97b7" ]]; then
  echo "selfhost backend gate: ELF CFG-call image bytes changed" >&2
  exit 1
fi

elf_call_add_result="$("$stage1" "$repo_dir/compiler/testdata/elf_call_add_exit.mko")"
if [[ "$elf_call_add_result" != $'elf_bytes 164\ntokens 31 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 8 symbols 2 resolved 1 locals 1 local_refs 1 typed 7 ir_functions 2 ir_blocks 2 ir_instructions 9 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7' ]]; then
  echo "selfhost backend gate: ELF call-add image changed: $elf_call_add_result" >&2
  exit 1
fi
elf_call_add_path="$work_dir/elf-call-add"
"$stage1" "$repo_dir/compiler/testdata/elf_call_add_exit.mko" "$elf_call_add_path" >/dev/null
if [[ "$(sha256_of "$elf_call_add_path")" != "bacfed7ba54bb57f57579046d9ad4c7c60ec185fc367ed05af5a109b6454bb7b" ]]; then
  echo "selfhost backend gate: ELF call-add image bytes changed" >&2
  exit 1
fi

elf_call2_result="$("$stage1" "$repo_dir/compiler/testdata/elf_call2_add_exit.mko")"
if [[ "$elf_call2_result" != $'elf_bytes 160\ntokens 35 items 2 types 4 signatures 2 parameters 2 statements 2 bodies 2 expressions 8 symbols 2 resolved 1 locals 2 local_refs 2 typed 7 ir_functions 2 ir_blocks 2 ir_instructions 8 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost backend gate: ELF two-arg call image changed: $elf_call2_result" >&2
  exit 1
fi
elf_call2_path="$work_dir/elf-call2"
"$stage1" "$repo_dir/compiler/testdata/elf_call2_add_exit.mko" "$elf_call2_path" >/dev/null
if [[ "$(sha256_of "$elf_call2_path")" != "87f634047f7828b4fd67e8a3c8df642361de9587207597d49a9116c7afad2732" ]]; then
  echo "selfhost backend gate: ELF two-arg call image bytes changed" >&2
  exit 1
fi

elf_hello_result="$("$stage1" "$repo_dir/compiler/testdata/elf_hello_exit.mko")"
if [[ "$elf_hello_result" != $'elf_bytes 401\ntokens 31 items 1 types 0 signatures 1 parameters 0 statements 6 bodies 1 expressions 14 symbols 1 resolved 0 locals 2 local_refs 2 typed 10 ir_functions 1 ir_blocks 1 ir_instructions 12 ir_constants 4 ir_values 4 ir_skipped 0 ir_verified 4' ]]; then
  echo "selfhost backend gate: ELF hello image changed: $elf_hello_result" >&2
  exit 1
fi
elf_hello_path="$work_dir/elf-hello"
"$stage1" "$repo_dir/compiler/testdata/elf_hello_exit.mko" "$elf_hello_path" >/dev/null
if [[ "$(sha256_of "$elf_hello_path")" != "eab9a7200e3c12cd4644f1d81428ea556585df004373217f1425ae5f645c1af1" ]]; then
  echo "selfhost backend gate: ELF hello image bytes changed" >&2
  exit 1
fi
# The stage-0 oracle proves the expected stdout and exit status.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_hello_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/hello-driver.mko"
"$stage0" build "$work_dir/hello-driver.mko" --no-incremental -o "$work_dir/hello-driver" >/dev/null
set +e
hello_driver_out="$("$work_dir/hello-driver")"
hello_driver_status=$?
set -e
if [[ "$hello_driver_out" != $'hello\n \nworld' || "$hello_driver_status" -ne 42 ]]; then
  echo "selfhost backend gate: hello stage-0 driver printed [$hello_driver_out] exit $hello_driver_status" >&2
  exit 1
fi

elf_print_int_result="$("$stage1" "$repo_dir/compiler/testdata/elf_print_int_exit.mko")"
if [[ "$elf_print_int_result" != $'elf_bytes 1013\ntokens 73 items 1 types 0 signatures 1 parameters 0 statements 12 bodies 1 expressions 40 symbols 1 resolved 0 locals 5 local_refs 6 typed 33 ir_functions 1 ir_blocks 1 ir_instructions 28 ir_constants 13 ir_values 20 ir_skipped 0 ir_verified 20' ]]; then
  echo "selfhost backend gate: ELF print-int image changed: $elf_print_int_result" >&2
  exit 1
fi
elf_print_int_path="$work_dir/elf-print-int"
"$stage1" "$repo_dir/compiler/testdata/elf_print_int_exit.mko" "$elf_print_int_path" >/dev/null
if [[ "$(sha256_of "$elf_print_int_path")" != "45f786cf675a65628f55785e8ddb433c40ef957a433363e44cf5507ecc611073" ]]; then
  echo "selfhost backend gate: ELF print-int image bytes changed" >&2
  exit 1
fi
# The stage-0 oracle proves the expected stdout and exit status.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_print_int_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/print-int-driver.mko"
"$stage0" build "$work_dir/print-int-driver.mko" --no-incremental -o "$work_dir/print-int-driver" >/dev/null
set +e
print_int_driver_out="$("$work_dir/print-int-driver")"
print_int_driver_status=$?
set -e
if [[ "$print_int_driver_out" != $'42\n-7\n0\n1000000000000\n-9223372036854775808\n9223372036854775807' || "$print_int_driver_status" -ne 7 ]]; then
  echo "selfhost backend gate: print-int stage-0 driver printed [$print_int_driver_out] exit $print_int_driver_status" >&2
  exit 1
fi

elf_loop_print_result="$("$stage1" "$repo_dir/compiler/testdata/elf_loop_print_exit.mko")"
if [[ "$elf_loop_print_result" != $'elf_bytes 600\ntokens 51 items 1 types 1 signatures 1 parameters 0 statements 9 bodies 1 expressions 23 symbols 1 resolved 0 locals 2 local_refs 7 typed 20 ir_functions 1 ir_blocks 4 ir_instructions 20 ir_constants 6 ir_values 12 ir_skipped 0 ir_verified 12' ]]; then
  echo "selfhost backend gate: ELF loop-print image changed: $elf_loop_print_result" >&2
  exit 1
fi
elf_loop_print_path="$work_dir/elf-loop-print"
"$stage1" "$repo_dir/compiler/testdata/elf_loop_print_exit.mko" "$elf_loop_print_path" >/dev/null
if [[ "$(sha256_of "$elf_loop_print_path")" != "4b4d265c49bd926bc4a897501887902725a612f5c82992dac8d3cd945822b37e" ]]; then
  echo "selfhost backend gate: ELF loop-print image bytes changed" >&2
  exit 1
fi
# The stage-0 oracle proves the expected stdout and exit status.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_loop_print_exit.mko"
  printf '\nfn main() {\n    let loop_print_result = prog_main()\n    exit(loop_print_result)\n}\n'
} > "$work_dir/loop-print-driver.mko"
"$stage0" build "$work_dir/loop-print-driver.mko" --no-incremental -o "$work_dir/loop-print-driver" >/dev/null
set +e
loop_print_driver_out="$("$work_dir/loop-print-driver")"
loop_print_driver_status=$?
set -e
if [[ "$loop_print_driver_out" != $'0\n0\ntick\n1\n0\ntick\n2\n10\ntick\n3\n30\ntick' || "$loop_print_driver_status" -ne 60 ]]; then
  echo "selfhost backend gate: loop-print stage-0 driver printed [$loop_print_driver_out] exit $loop_print_driver_status" >&2
  exit 1
fi

# General CFG lowering: a sequential guard chain (`if cond { print; exit }`
# repeated, then a tail) is not one of the matched shapes, so it exercises the
# shape-independent if-tree lowerer, string constants outside a matched path,
# bool-returning calls, and the general returning-CFG encoder.
elf_guard_chain_result="$("$stage1" "$repo_dir/compiler/testdata/elf_guard_chain_exit.mko")"
if [[ "$elf_guard_chain_result" != $'elf_bytes 790\ntokens 81 items 2 types 2 signatures 2 parameters 1 statements 12 bodies 2 expressions 42 symbols 2 resolved 3 locals 1 local_refs 1 typed 31 ir_functions 2 ir_blocks 8 ir_instructions 43 ir_constants 15 ir_values 23 ir_skipped 0 ir_verified 23' ]]; then
  echo "selfhost backend gate: ELF guard-chain image changed: $elf_guard_chain_result" >&2
  exit 1
fi
elf_guard_chain_path="$work_dir/elf-guard-chain"
"$stage1" "$repo_dir/compiler/testdata/elf_guard_chain_exit.mko" "$elf_guard_chain_path" >/dev/null
if [[ "$(sha256_of "$elf_guard_chain_path")" != "d67c70819434e7fc7cf62803d88b119dc5e9e0f999f13ac18840a7d28346f689" ]]; then
  echo "selfhost backend gate: ELF guard-chain image bytes changed" >&2
  exit 1
fi
if ! file "$elf_guard_chain_path" | grep -q "ELF 64-bit.*x86-64"; then
  echo "selfhost backend gate: guard-chain image is not ELF64 x86-64" >&2
  exit 1
fi
# The stage-0 oracle proves the expected stdout and exit status.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_guard_chain_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/guard-chain-driver.mko"
"$stage0" build "$work_dir/guard-chain-driver.mko" --no-incremental -o "$work_dir/guard-chain-driver" >/dev/null
set +e
guard_chain_driver_out="$("$work_dir/guard-chain-driver")"
guard_chain_driver_status=$?
set -e
if [[ "$guard_chain_driver_out" != "all probes ok" || "$guard_chain_driver_status" -ne 11 ]]; then
  echo "selfhost backend gate: guard-chain stage-0 driver printed [$guard_chain_driver_out] exit $guard_chain_driver_status" >&2
  exit 1
fi

elf_bool_guard_result="$("$stage1" "$repo_dir/compiler/testdata/elf_bool_guard_exit.mko")"
if [[ "$elf_bool_guard_result" != $'elf_bytes 405\ntokens 40 items 2 types 1 signatures 2 parameters 0 statements 6 bodies 2 expressions 16 symbols 2 resolved 1 locals 0 local_refs 0 typed 11 ir_functions 2 ir_blocks 4 ir_instructions 17 ir_constants 5 ir_values 7 ir_skipped 0 ir_verified 7' ]]; then
  echo "selfhost backend gate: ELF bool-guard image changed: $elf_bool_guard_result" >&2
  exit 1
fi
elf_bool_guard_path="$work_dir/elf-bool-guard"
"$stage1" "$repo_dir/compiler/testdata/elf_bool_guard_exit.mko" "$elf_bool_guard_path" >/dev/null
if [[ "$(sha256_of "$elf_bool_guard_path")" != "a763ef707d5063a6fbb94a68d53f536c42cb5fc58097b159f4745968c8d81128" ]]; then
  echo "selfhost backend gate: ELF bool-guard image bytes changed" >&2
  exit 1
fi
# The stage-0 oracle proves both arms: `not ready()` is a logical negation, so
# a true operand must take the fall-through and never the guard.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_bool_guard_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/bool-guard-driver.mko"
"$stage0" build "$work_dir/bool-guard-driver.mko" --no-incremental -o "$work_dir/bool-guard-driver" >/dev/null
set +e
bool_guard_driver_out="$("$work_dir/bool-guard-driver")"
bool_guard_driver_status=$?
set -e
if [[ "$bool_guard_driver_out" != "ready" || "$bool_guard_driver_status" -ne 0 ]]; then
  echo "selfhost backend gate: bool-guard stage-0 driver printed [$bool_guard_driver_out] exit $bool_guard_driver_status" >&2
  exit 1
fi
sed 's/return true/return false/' "$repo_dir/compiler/testdata/elf_bool_guard_exit.mko" > "$work_dir/bool-guard-false.mko"
elf_bool_guard_false_path="$work_dir/elf-bool-guard-false"
"$stage1" "$work_dir/bool-guard-false.mko" "$elf_bool_guard_false_path" >/dev/null

# Intrinsic calls in the general path: `argc()` has no signature, so it
# resolves through the shared intrinsic-sentinel table rather than being
# rejected as a call to an undeclared function.
elf_argc_guard_result="$("$stage1" "$repo_dir/compiler/testdata/elf_argc_guard_exit.mko")"
if [[ "$elf_argc_guard_result" != $'elf_bytes 561\ntokens 56 items 2 types 1 signatures 2 parameters 0 statements 9 bodies 2 expressions 26 symbols 2 resolved 1 locals 0 local_refs 0 typed 18 ir_functions 2 ir_blocks 6 ir_instructions 27 ir_constants 8 ir_values 12 ir_skipped 0 ir_verified 12' ]]; then
  echo "selfhost backend gate: ELF argc-guard image changed: $elf_argc_guard_result" >&2
  exit 1
fi
elf_argc_guard_path="$work_dir/elf-argc-guard"
"$stage1" "$repo_dir/compiler/testdata/elf_argc_guard_exit.mko" "$elf_argc_guard_path" >/dev/null
if [[ "$(sha256_of "$elf_argc_guard_path")" != "f5063a4da26c24a68a193dd00d835b52d1e8d9cb31f6c826143c5f76552cac3b" ]]; then
  echo "selfhost backend gate: ELF argc-guard image bytes changed" >&2
  exit 1
fi
# The stage-0 oracle proves both arms, which differ only by argv length.
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_argc_guard_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/argc-guard-driver.mko"
"$stage0" build "$work_dir/argc-guard-driver.mko" --no-incremental -o "$work_dir/argc-guard-driver" >/dev/null
set +e
argc_guard_noarg_out="$("$work_dir/argc-guard-driver")"
argc_guard_noarg_status=$?
argc_guard_onearg_out="$("$work_dir/argc-guard-driver" one)"
argc_guard_onearg_status=$?
set -e
if [[ "$argc_guard_noarg_out" != "usage: prog <arg>" || "$argc_guard_noarg_status" -ne 2 ]]; then
  echo "selfhost backend gate: argc-guard stage-0 driver no-arg printed [$argc_guard_noarg_out] exit $argc_guard_noarg_status" >&2
  exit 1
fi
if [[ "$argc_guard_onearg_out" != "have argument" || "$argc_guard_onearg_status" -ne 3 ]]; then
  echo "selfhost backend gate: argc-guard stage-0 driver one-arg printed [$argc_guard_onearg_out] exit $argc_guard_onearg_status" >&2
  exit 1
fi

# An owned call result in the general path: arg_get returns a string, bound to
# a local, printed, and dropped when the path exits. The ownership verifier's
# consumed-exactly-once rule is what proves the drop is there on every path.
elf_arg_get_guard_result="$("$stage1" "$repo_dir/compiler/testdata/elf_arg_get_guard_exit.mko")"
if [[ "$elf_arg_get_guard_result" != $'elf_bytes 529\ntokens 38 items 1 types 0 signatures 1 parameters 0 statements 6 bodies 1 expressions 19 symbols 1 resolved 0 locals 1 local_refs 1 typed 13 ir_functions 1 ir_blocks 3 ir_instructions 17 ir_constants 5 ir_values 8 ir_skipped 0 ir_verified 8' ]]; then
  echo "selfhost backend gate: ELF arg_get-guard image changed: $elf_arg_get_guard_result" >&2
  exit 1
fi
elf_arg_get_guard_path="$work_dir/elf-arg-get-guard"
"$stage1" "$repo_dir/compiler/testdata/elf_arg_get_guard_exit.mko" "$elf_arg_get_guard_path" >/dev/null
if [[ "$(sha256_of "$elf_arg_get_guard_path")" != "1701171ea3c17a3292c0ad0fbb2c80cf42729aa78b14cf0d8812515d06a3a0e9" ]]; then
  echo "selfhost backend gate: ELF arg_get-guard image bytes changed" >&2
  exit 1
fi
{
  sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/elf_arg_get_guard_exit.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/arg-get-guard-driver.mko"
"$stage0" build "$work_dir/arg-get-guard-driver.mko" --no-incremental -o "$work_dir/arg-get-guard-driver" >/dev/null
set +e
arg_get_guard_noarg_out="$("$work_dir/arg-get-guard-driver")"
arg_get_guard_noarg_status=$?
arg_get_guard_onearg_out="$("$work_dir/arg-get-guard-driver" world)"
arg_get_guard_onearg_status=$?
set -e
if [[ "$arg_get_guard_noarg_out" != "usage: prog <name>" || "$arg_get_guard_noarg_status" -ne 2 ]]; then
  echo "selfhost backend gate: arg_get-guard stage-0 driver no-arg printed [$arg_get_guard_noarg_out] exit $arg_get_guard_noarg_status" >&2
  exit 1
fi
if [[ "$arg_get_guard_onearg_out" != "world" || "$arg_get_guard_onearg_status" -ne 4 ]]; then
  echo "selfhost backend gate: arg_get-guard stage-0 driver one-arg printed [$arg_get_guard_onearg_out] exit $arg_get_guard_onearg_status" >&2
  exit 1
fi

# Twenty sequential guards give main forty-one blocks, past the single-word
# dominator set the verifier used to be limited to, and more than 127 SSA
# values, past the fixed shapes' slot ceiling.
elf_long_chain_result="$("$stage1" "$repo_dir/compiler/testdata/elf_long_guard_chain_exit.mko")"
if [[ "$elf_long_chain_result" != $'elf_bytes 3808\ntokens 535 items 21 types 20 signatures 21 parameters 0 statements 82 bodies 21 expressions 226 symbols 21 resolved 20 locals 0 local_refs 0 typed 164 ir_functions 21 ir_blocks 61 ir_instructions 246 ir_constants 82 ir_values 122 ir_skipped 0 ir_verified 122' ]]; then
  echo "selfhost backend gate: ELF long-chain image changed: $elf_long_chain_result" >&2
  exit 1
fi
elf_long_chain_path="$work_dir/elf-long-chain"
"$stage1" "$repo_dir/compiler/testdata/elf_long_guard_chain_exit.mko" "$elf_long_chain_path" >/dev/null
if [[ "$(sha256_of "$elf_long_chain_path")" != "67d0f27585aa8ba6f271e85db5a90ac655307103588e02d1f2e6a710996efa65" ]]; then
  echo "selfhost backend gate: ELF long-chain image bytes changed" >&2
  exit 1
fi
# A mid-chain guard proves the branch reaches a block far from the entry, not
# just the first and last arms.
sed 's/^    return 11$/    return 0/' "$repo_dir/compiler/testdata/elf_long_guard_chain_exit.mko" > "$work_dir/long-chain-mid.mko"
elf_long_chain_mid_path="$work_dir/elf-long-chain-mid"
"$stage1" "$work_dir/long-chain-mid.mko" "$elf_long_chain_mid_path" >/dev/null
{
  sed 's/^fn main/fn prog_main/' "$work_dir/long-chain-mid.mko"
  printf '\nfn main() {\n    prog_main()\n}\n'
} > "$work_dir/long-chain-mid-driver.mko"
"$stage0" build "$work_dir/long-chain-mid-driver.mko" --no-incremental -o "$work_dir/long-chain-mid-driver" >/dev/null
set +e
long_chain_mid_out="$("$work_dir/long-chain-mid-driver")"
long_chain_mid_status=$?
set -e
if [[ "$long_chain_mid_out" != "step11 failed" || "$long_chain_mid_status" -ne 51 ]]; then
  echo "selfhost backend gate: long-chain mid stage-0 driver printed [$long_chain_mid_out] exit $long_chain_mid_status" >&2
  exit 1
fi

# Package mode: a comma-separated path list is one translation unit. Without it
# a type declared in one member is invisible in the next, which is what made 92
# of the compiler's own parameters unbindable. Every member is required — one
# that cannot be read must fail the package rather than quietly vanish from it.
selfhost_package="$repo_dir/compiler/lexer.mko,$repo_dir/compiler/parser.mko"
for package_member in z_expression z_statement z_type zz_expression_attach zz_symbols \
  zzz_resolution zzzz_locals zzzzz_types zzzzzz_ir argv_native x86_64 elf64; do
  selfhost_package="$selfhost_package,$repo_dir/compiler/$package_member.mko"
done
package_result="$("$stage1" "$selfhost_package")"
if [[ "$package_result" != *"signatures 317"* ]]; then
  echo "selfhost frontend gate: package signature count changed: $package_result" >&2
  exit 1
fi
# A floor rather than an exact count: this rises as the backend grows, and a
# gate re-pinned on every gain gets re-pinned without being read. It must never
# silently fall.
package_lowered="$(printf '%s' "$package_result" | sed -n 's/.*ir_functions \([0-9]*\).*/\1/p')"
if [[ "${package_lowered:-0}" -lt 63 ]]; then
  echo "selfhost frontend gate: package lowered $package_lowered functions, expected at least 63" >&2
  exit 1
fi
for bad_package in "$repo_dir/compiler/lexer.mko,$repo_dir/compiler/nope.mko" \
  "$repo_dir/compiler/nope.mko" ""; do
  if "$stage1" "$bad_package" >/dev/null 2>&1; then
    echo "selfhost frontend gate: unreadable package member [$bad_package] accepted" >&2
    exit 1
  fi
done

# --------------------------------------------------------------------------
# Capabilities added after the shape-matched era. Each entry is
#   fixture:image-name:sha256:elf_bytes:expected exit
# and every one is checked three ways: the arena shape and image bytes are
# pinned, the stage-0 oracle proves the exit status, and on Linux the generated
# image is executed and must agree with it.
for cap_case in \
  "elf_uncalled_helper_exit:elf-uncalled-helper:aa1a8ba34298ae66ca21ff2ddc44af8c0d392cc04deefe46dd3e8ffe1834b913:204:4" \
  "elf_format_int_exit:elf-format-int:3631f60e08db92c85188e37aa632faf6831d239cf782eff8b893b279cfda9e2a:1861:7" \
  "elf_concat_exit:elf-concat:5bc2ea28998ef1e08c4273066fb257061f2c2d64536f6832ff7ebf151bf8a2c7:1807:5" \
  "elf_string_clone_exit:elf-string-clone:b0a1514e3660687db2b9aaa7952696343ebe93a3ab3fe66a136eeaba8dc822a1:1087:6" \
  "elf_while_sum_exit:elf-while-sum:054c2c39e75fd855907155b9369b25548e23357f5e937f12d8092c66e030bd51:383:18" \
  "elf_while_guard_exit:elf-while-guard:fef9e762b4f673470e4b675e8f5ad7f6594ddd540fb8aa2790df267e95bf1a6f:330:10" \
  "elf_while_alloc_soak_exit:elf-while-soak:9ded95273bf151d3b3daced0e109d3dda5008396e5c70eb3a5223b8fbcd13b50:444:3" \
  "elf_loop_body_print_exit:elf-loop-body-print:70993ce51c9a80cff943c832e772795f23a066fbf3b9f31519d9c3cb2424ce01:321:0" \
  "elf_loop_print_update_exit:elf-loop-print-update:07f70ecb46ed5ed7e1da9b6f80d36be22e7ff4582f20e1d70cfc529abeb3a1bc:411:60" \
  "elf_str_byte_at_exit:elf-str-byte-at:81d93cf057d45ae066c0edcacddb8041af5dba83f98bdb16e95ca9aca050ed88:688:79" \
  "elf_general_len_exit:elf-general-len:723827287d5745c68a57940b633734589b4b5e51f930782b9dfeef53d9492438:850:34" \
  "elf_general_index_exit:elf-general-index:e3992985305e2943de4498924993b389a6f5e2cab9b81cc5e3efbceb29627bf3:1000:190" \
  "elf_straight_line_assign_exit:elf-sl-assign:fa96ae649b72f2cd5c793ecb7050a21efcb2a6ffcc69b49951b3d1d2c7d1ed2b:184:14" \
  "elf_owned_rebind_exit:elf-owned-rebind:ed6d2ee7ba4fb8db9310d0ffc3078a78ce0647e8b3c857d1bd6c09816b2e16ce:1660:43" \
  "elf_struct_literal_exit:elf-struct-literal:7a8547d71a71839c093d26cac073b1c2f5d65d5d5a5cff7db1c954addefbd82b:402:34" \
  "elf_struct_string_field_exit:elf-struct-string:7d5cfaf2f3061c2f4c59907136693162576ec439473b68add884092077f0e389:694:14"; do
  cap_fixture="${cap_case%%:*}"; cap_rest="${cap_case#*:}"
  cap_name="${cap_rest%%:*}"; cap_rest="${cap_rest#*:}"
  cap_sha="${cap_rest%%:*}"; cap_rest="${cap_rest#*:}"
  cap_bytes="${cap_rest%%:*}"; cap_status="${cap_rest##*:}"
  cap_summary="$("$stage1" "$repo_dir/compiler/testdata/$cap_fixture.mko")"
  if [[ "$cap_summary" != "elf_bytes $cap_bytes"* || "$cap_summary" != *"ir_skipped 0"* ]]; then
    echo "selfhost backend gate: $cap_fixture image changed: $cap_summary" >&2
    exit 1
  fi
  cap_path="$work_dir/$cap_name"
  "$stage1" "$repo_dir/compiler/testdata/$cap_fixture.mko" "$cap_path" >/dev/null
  if [[ "$(sha256_of "$cap_path")" != "$cap_sha" ]]; then
    echo "selfhost backend gate: $cap_fixture image bytes changed" >&2
    exit 1
  fi
  # A fixture whose `main` returns a value exits with it under the entry
  # bridge, so the stage-0 driver has to propagate it rather than discard it.
  {
    sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/$cap_fixture.mko"
    if grep -q '^fn main() ->' "$repo_dir/compiler/testdata/$cap_fixture.mko"; then
      printf '\nfn main() {\n    let cap_result = prog_main()\n    exit(cap_result)\n}\n'
    else
      printf '\nfn main() {\n    prog_main()\n}\n'
    fi
  } > "$work_dir/$cap_fixture-driver.mko"
  "$stage0" build "$work_dir/$cap_fixture-driver.mko" --no-incremental -o "$work_dir/$cap_fixture-driver" >/dev/null
  set +e
  cap_oracle_out="$("$work_dir/$cap_fixture-driver")"
  cap_oracle_status=$?
  set -e
  if [[ "$cap_oracle_status" -ne "$cap_status" ]]; then
    echo "selfhost backend gate: $cap_fixture stage-0 driver exited $cap_oracle_status, expected $cap_status" >&2
    exit 1
  fi
  if [[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]]; then
    chmod +x "$cap_path"
    set +e
    cap_elf_out="$("$cap_path")"
    cap_elf_status=$?
    set -e
    if [[ "$cap_elf_status" -ne "$cap_status" || "$cap_elf_out" != "$cap_oracle_out" ]]; then
      echo "selfhost backend gate: $cap_fixture ELF exited $cap_elf_status printing [$cap_elf_out], oracle gave $cap_status [$cap_oracle_out]" >&2
      exit 1
    fi
  fi
done

# Allocation balance, counted at run time rather than read off the image.
# Static site counts cannot express this: one callee's parameter drop serves
# every call, and a literal argument carries an IR_DROP that must emit no munmap
# at all, since unmapping a static view would unmap the code image. An
# unbalanced count is a leak per call or a free of something still read.
#
# The soak is the per-call leak check the memory bar asks for and that no
# earlier increment could run: one owned allocation per iteration, dropped at
# the backedge, 20000 times.
if [[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]] && command -v strace >/dev/null 2>&1; then
  for balance_case in "elf-format-int:6" "elf-concat:7" "elf-string-clone:5" \
    "elf-owned-rebind:6" "elf-struct-literal:1" "elf-struct-string:3" "elf-while-soak:20000"; do
    balance_path="$work_dir/${balance_case%%:*}"
    balance_expect="${balance_case##*:}"
    chmod +x "$balance_path"
    balance_counts="$(strace -c -e trace=mmap,munmap "$balance_path" 2>&1 >/dev/null \
      | awk '$NF == "mmap" { m = $(NF-1) } $NF == "munmap" { u = $(NF-1) } END { print m+0, u+0 }')"
    if [[ "$balance_counts" != "$balance_expect $balance_expect" ]]; then
      echo "selfhost backend gate: $balance_path made [$balance_counts] mmap/munmap calls, expected $balance_expect $balance_expect" >&2
      exit 1
    fi
  done
fi

# An indexed store is modelled nowhere and used to be dropped in silence, so the
# program exited 0 instead of 40. It must be refused and leave no output.
if "$stage1" "$repo_dir/compiler/testdata/bad_index_store.mko" "$work_dir/bad-index-store.elf" >/dev/null 2>&1; then
  echo "selfhost backend gate: indexed store unexpectedly lowered" >&2
  exit 1
fi
if [[ -e "$work_dir/bad-index-store.elf" ]]; then
  echo "selfhost backend gate: indexed-store failure left an output file" >&2
  exit 1
fi

# The index offset has to reach the load. A `make`d slice is all zeros, so the
# returned value cannot tell elements apart; two reads of different indices
# compiling to identical images is the check that caught `pts[0].x` and
# `pts[0].y` doing exactly that.
sed 's/return xs\[i\] + 7/return xs[0] + 7/' "$repo_dir/compiler/testdata/elf_general_index_exit.mko" > "$work_dir/index-at-0.mko"
sed 's/return xs\[i\] + 7/return xs[1] + 7/' "$repo_dir/compiler/testdata/elf_general_index_exit.mko" > "$work_dir/index-at-1.mko"
"$stage1" "$work_dir/index-at-0.mko" "$work_dir/index-at-0.elf" >/dev/null
"$stage1" "$work_dir/index-at-1.mko" "$work_dir/index-at-1.elf" >/dev/null
if cmp -s "$work_dir/index-at-0.elf" "$work_dir/index-at-1.elf"; then
  echo "selfhost backend gate: xs[0] and xs[1] compiled to identical images — the index is ignored" >&2
  exit 1
fi

# Two fixtures that used to be here no longer belong. A print in a plain loop
# body, and one reading a carried local an earlier body statement already
# updated, both lower correctly now that the general path carries assigned
# locals as block parameters — the second is exactly the "stale read" the
# matched shapes had to refuse, and SSA rebinding gets it right. They are
# renamed to `elf_loop_body_print_exit` and `elf_loop_print_update_exit` and are
# pinned and executed with the other loop fixtures below.
if guard_print_result="$("$stage1" "$repo_dir/compiler/testdata/bad_loop_print_guard.mko" "$work_dir/bad-loop-print-guard.elf" 2>&1)"; then
  echo "selfhost backend gate: guarded-loop print unexpectedly lowered" >&2
  exit 1
fi
if [[ "$guard_print_result" != *"ELF lowering failed: source contains functions the typed IR cannot lower yet" ]]; then
  echo "selfhost backend gate: unexpected guard-print diagnostic: $guard_print_result" >&2
  exit 1
fi
if [[ -e "$work_dir/bad-loop-print-guard.elf" ]]; then
  echo "selfhost backend gate: guard-print failure left an output file" >&2
  exit 1
fi
if print_if_result="$("$stage1" "$repo_dir/compiler/testdata/bad_print_if.mko" "$work_dir/bad-print-if.elf" 2>&1)"; then
  echo "selfhost backend gate: print inside an if body unexpectedly lowered" >&2
  exit 1
fi
if [[ "$print_if_result" != *"ELF lowering failed: source contains functions the typed IR cannot lower yet" ]]; then
  echo "selfhost backend gate: unexpected print-if diagnostic: $print_if_result" >&2
  exit 1
fi
if [[ -e "$work_dir/bad-print-if.elf" ]]; then
  echo "selfhost backend gate: print-if failure left an output file" >&2
  exit 1
fi

for spec in \
  "elf_sub2_exit%2512310c750dec416f869e35551200c3e33e8c103867218fbe879df8cecd7f2f%elf_bytes 160%tokens 35 items 2 types 4 signatures 2 parameters 2 statements 2 bodies 2 expressions 8 symbols 2 resolved 1 locals 2 local_refs 2 typed 7 ir_functions 2 ir_blocks 2 ir_instructions 8 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" \
  "elf_mul_param_exit%7e1728192ebdcd48f14f4e77ff4b72005af012c2a05de63b3b4c5c4065f8f133%elf_bytes 159%tokens 29 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 6 symbols 2 resolved 1 locals 1 local_refs 1 typed 5 ir_functions 2 ir_blocks 2 ir_instructions 7 ir_constants 2 ir_values 5 ir_skipped 0 ir_verified 5" \
  "elf_call_sub_exit%f1672ebb929c5366bf69ea9276512f765d05214e8dba9d258c892f7ee8fe28ee%elf_bytes 157%tokens 29 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 6 symbols 2 resolved 1 locals 1 local_refs 1 typed 5 ir_functions 2 ir_blocks 2 ir_instructions 7 ir_constants 2 ir_values 5 ir_skipped 0 ir_verified 5" \
  "elf_two_call_exit%fb62de136bcd6a444bc22959e51f9342ab5527d4450ec9221b47644dc7baf83d%elf_bytes 178%tokens 40 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 10 symbols 2 resolved 2 locals 3 local_refs 3 typed 8 ir_functions 2 ir_blocks 2 ir_instructions 8 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" \
  "elf_two_call_sub_exit%de92ff287506b2560ee8def6be63fc61c3a0cac9132441e4b18d2fb3125f7a41%elf_bytes 184%tokens 40 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 10 symbols 2 resolved 2 locals 3 local_refs 3 typed 8 ir_functions 2 ir_blocks 2 ir_instructions 8 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" \
  "elf_two_call_mul_exit%248cc1d9a935475c5a3d98e63a32c0fa5f2150a0902faec873f4cf258933cc67%elf_bytes 179%tokens 40 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 10 symbols 2 resolved 2 locals 3 local_refs 3 typed 8 ir_functions 2 ir_blocks 2 ir_instructions 8 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" \
  "elf_two_call2_add_exit%27ebcef6110244c019eadf771f13117e6193db22ce0af2222026cd0897bccef1%elf_bytes 192%tokens 50 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 16 symbols 2 resolved 2 locals 4 local_refs 4 typed 14 ir_functions 2 ir_blocks 2 ir_instructions 12 ir_constants 4 ir_values 10 ir_skipped 0 ir_verified 10" \
  "elf_two_call2_sub_exit%74e763ee5805d59cc371e25337ac498ca51128b2be9debd0f118a494a3d1970d%elf_bytes 198%tokens 50 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 16 symbols 2 resolved 2 locals 4 local_refs 4 typed 14 ir_functions 2 ir_blocks 2 ir_instructions 12 ir_constants 4 ir_values 10 ir_skipped 0 ir_verified 10" \
  "elf_call_branch_exit%a76b5cda1d3a224c0729043bdddac64b114771b91eaafca56614f1d3f40fda84%elf_bytes 169%tokens 38 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 8 symbols 2 resolved 1 locals 1 local_refs 1 typed 7 ir_functions 2 ir_blocks 5 ir_instructions 13 ir_constants 4 ir_values 8 ir_skipped 0 ir_verified 8" \
  "elf_cfg_call_exit%82f38d9550abf16084e3673b74d6a8ed3c7565d658fdab361f38f810544790b2%elf_bytes 175%tokens 41 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 10 symbols 2 resolved 2 locals 2 local_refs 2 typed 8 ir_functions 2 ir_blocks 4 ir_instructions 12 ir_constants 3 ir_values 8 ir_skipped 0 ir_verified 8" \
  "elf_cfg_call2_exit%28f07f45c0e9bf2900fc8a25010163970a0f19624923a47c817fedd2431873f8%elf_bytes 189%tokens 51 items 2 types 5 signatures 2 parameters 3 statements 4 bodies 2 expressions 16 symbols 2 resolved 2 locals 3 local_refs 3 typed 14 ir_functions 2 ir_blocks 4 ir_instructions 16 ir_constants 5 ir_values 12 ir_skipped 0 ir_verified 12" \
  "elf_cfg_merge_call_exit%b641544199dce1f435ab508ebe6ec61bbab9fbb2b742ef531c0b881ed41ce5d8%elf_bytes 179%tokens 44 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 10 symbols 2 resolved 2 locals 2 local_refs 2 typed 8 ir_functions 2 ir_blocks 5 ir_instructions 14 ir_constants 3 ir_values 9 ir_skipped 0 ir_verified 9" \
  "elf_scalar_locals_exit%c2273f9137dcab1cc094ebea660c1373ded2e8c4a07e5113dfce0cdfe65f89ff%elf_bytes 194%tokens 28 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 9 symbols 1 resolved 0 locals 3 local_refs 3 typed 9 ir_functions 1 ir_blocks 1 ir_instructions 8 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7" \
  "elf_scalar_callee_exit%b9cbce164081ffb33b0c02d6fba27cfdb6833f7e425c9d0255d50896c2d2e5a9%elf_bytes 212%tokens 47 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 14 symbols 2 resolved 1 locals 4 local_refs 4 typed 13 ir_functions 2 ir_blocks 2 ir_instructions 12 ir_constants 4 ir_values 10 ir_skipped 0 ir_verified 10" \
  "elf_scalar_call_exit%dd4110e1cd0d4bbf30e9c4eab539456fe84e89f0b40755f1bc1747a1ecf499ab%elf_bytes 195%tokens 42 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 10 symbols 2 resolved 1 locals 4 local_refs 4 typed 9 ir_functions 2 ir_blocks 2 ir_instructions 9 ir_constants 2 ir_values 7 ir_skipped 0 ir_verified 7" \
  "elf_scalar_call2_exit%bfa528561910b7a2a12ec4da69f73b586ec0ee1484a277b3ae15faf5be912930%elf_bytes 198%tokens 48 items 2 types 5 signatures 2 parameters 3 statements 4 bodies 2 expressions 12 symbols 2 resolved 1 locals 5 local_refs 5 typed 11 ir_functions 2 ir_blocks 2 ir_instructions 10 ir_constants 2 ir_values 8 ir_skipped 0 ir_verified 8" \
  "elf_scalar_call3_exit%f4787d4eb2801b8aaf75a61f35c3ad5918628d7cef9a4ba9a1e8d0a0b7cdbf29%elf_bytes 238%tokens 56 items 2 types 6 signatures 2 parameters 4 statements 4 bodies 2 expressions 16 symbols 2 resolved 1 locals 6 local_refs 6 typed 15 ir_functions 2 ir_blocks 2 ir_instructions 13 ir_constants 3 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_scalar_call6_exit%11a4a043992f3b101a2c6fb186e174e05d72da34337865e2eb0d24f744d0f7f1%elf_bytes 316%tokens 83 items 2 types 8 signatures 2 parameters 6 statements 6 bodies 2 expressions 28 symbols 2 resolved 1 locals 10 local_refs 10 typed 27 ir_functions 2 ir_blocks 2 ir_instructions 20 ir_constants 6 ir_values 18 ir_skipped 0 ir_verified 18" \
  "elf_scalar_div_exit%f0ca56be38adfafb97954ac3331717937c2fca58e69688970094bf198b3d4616%elf_bytes 199%tokens 32 items 1 types 2 signatures 1 parameters 1 statements 4 bodies 1 expressions 10 symbols 1 resolved 0 locals 4 local_refs 4 typed 10 ir_functions 1 ir_blocks 1 ir_instructions 8 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7" \
  "elf_scalar_mod_exit%34509b5239361d5b9701004f9e77adc7ef755c95253ef620fa4602ae1456dd05%elf_bytes 207%tokens 32 items 1 types 2 signatures 1 parameters 1 statements 4 bodies 1 expressions 10 symbols 1 resolved 0 locals 4 local_refs 4 typed 10 ir_functions 1 ir_blocks 1 ir_instructions 8 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7" \
  "elf_unary_exit%94f8432af88a462d35b22e106769e703b780af6123e495454c554fccd0094664%elf_bytes 169%tokens 20 items 1 types 2 signatures 1 parameters 1 statements 2 bodies 1 expressions 4 symbols 1 resolved 0 locals 2 local_refs 2 typed 4 ir_functions 1 ir_blocks 1 ir_instructions 4 ir_constants 0 ir_values 3 ir_skipped 0 ir_verified 3" \
  "elf_mixed_call_exit%414eaed774f18d835b56adec2d11f446ddce449b7e34ec72de2a535efc3ebf1a%elf_bytes 224%tokens 66 items 3 types 7 signatures 3 parameters 4 statements 5 bodies 3 expressions 17 symbols 3 resolved 2 locals 6 local_refs 6 typed 15 ir_functions 3 ir_blocks 3 ir_instructions 15 ir_constants 3 ir_values 12 ir_skipped 0 ir_verified 12" \
  "elf_mixed_call2_exit%c96d15a68386a7dc71db4de20da776c302a4410283dbe0bb6ebeacf1927234dd%elf_bytes 225%tokens 66 items 3 types 7 signatures 3 parameters 4 statements 5 bodies 3 expressions 17 symbols 3 resolved 2 locals 6 local_refs 6 typed 15 ir_functions 3 ir_blocks 3 ir_instructions 15 ir_constants 3 ir_values 12 ir_skipped 0 ir_verified 12" \
  "elf_three_call_exit%4002ca659745a34ceca776c3f002bbbe29b89ab509089e39ef9d66b69eee563b%elf_bytes 237%tokens 54 items 2 types 4 signatures 2 parameters 2 statements 5 bodies 2 expressions 17 symbols 2 resolved 3 locals 5 local_refs 5 typed 14 ir_functions 2 ir_blocks 2 ir_instructions 13 ir_constants 3 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_scalar_call7_exit%86d5bf4b6f569b0dc29f700af4d7caab009e87eede73a02dd1c4df952e0d1a2f%elf_bytes 340%tokens 80 items 2 types 10 signatures 2 parameters 8 statements 2 bodies 2 expressions 30 symbols 2 resolved 1 locals 8 local_refs 8 typed 29 ir_functions 2 ir_blocks 2 ir_instructions 25 ir_constants 7 ir_values 23 ir_skipped 0 ir_verified 23" \
  "elf_scalar_call9_exit%1bf042d451fab6170c4575bc07318ea9f50d489c87395a7a3e28caaa6d1081f9%elf_bytes 380%tokens 86 items 2 types 12 signatures 2 parameters 10 statements 2 bodies 2 expressions 28 symbols 2 resolved 1 locals 10 local_refs 5 typed 27 ir_functions 2 ir_blocks 2 ir_instructions 26 ir_constants 9 ir_values 24 ir_skipped 0 ir_verified 24" \
  "elf_loop_bound_exit%a1c9fff3b55a2484b4395726175177804dbd21bae3e71c17052dad903c43236c%elf_bytes 218%tokens 30 items 1 types 2 signatures 1 parameters 1 statements 4 bodies 1 expressions 8 symbols 1 resolved 0 locals 2 local_refs 4 typed 8 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6" \
  "elf_loop_call_exit%7338902074da1a7e711ef4f8877b61397a1bda265a64bb35a2145b355415cfdd%elf_bytes 258%tokens 51 items 2 types 4 signatures 2 parameters 2 statements 5 bodies 2 expressions 15 symbols 2 resolved 2 locals 3 local_refs 5 typed 13 ir_functions 2 ir_blocks 5 ir_instructions 16 ir_constants 3 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_loop_acc_exit%a5b99d33b006a43fd73cca67dc77a651a7d0bd2446d78776af06e9462602c5a5%elf_bytes 319%tokens 44 items 1 types 2 signatures 1 parameters 1 statements 6 bodies 1 expressions 16 symbols 1 resolved 0 locals 3 local_refs 6 typed 16 ir_functions 1 ir_blocks 4 ir_instructions 17 ir_constants 5 ir_values 13 ir_skipped 0 ir_verified 13" \
  "elf_loop_acc_call_exit%a04e5d0dec77eee47887ca1f3e51d78e1e03ddf27087087f85c399753f4303c5%elf_bytes 317%tokens 58 items 2 types 4 signatures 2 parameters 2 statements 7 bodies 2 expressions 17 symbols 2 resolved 1 locals 4 local_refs 7 typed 16 ir_functions 2 ir_blocks 5 ir_instructions 18 ir_constants 4 ir_values 13 ir_skipped 0 ir_verified 13" \
  "elf_loop_acc3_exit%2e5bc556670faba8aad900b66a87ee3a54930839b29eae14c93301f4a4aa939f%elf_bytes 435%tokens 56 items 1 types 2 signatures 1 parameters 1 statements 8 bodies 1 expressions 22 symbols 1 resolved 0 locals 4 local_refs 8 typed 22 ir_functions 1 ir_blocks 4 ir_instructions 22 ir_constants 7 ir_values 18 ir_skipped 0 ir_verified 18" \
  "elf_big_locals_exit%db8ec6a5318850e270c34ee8345dc5da65716b5095d5143e68da7b442a1140e0%elf_bytes 413%tokens 84 items 1 types 2 signatures 1 parameters 1 statements 13 bodies 1 expressions 35 symbols 1 resolved 0 locals 13 local_refs 13 typed 35 ir_functions 1 ir_blocks 1 ir_instructions 24 ir_constants 11 ir_values 23 ir_skipped 0 ir_verified 23" \
  "elf_loop_break_exit%4628e4656d6ac100414fc9183f02b17bd20e98dfbb6e7b0804cdf039c01a0c25%elf_bytes 287%tokens 39 items 1 types 2 signatures 1 parameters 1 statements 6 bodies 1 expressions 13 symbols 1 resolved 0 locals 2 local_refs 5 typed 13 ir_functions 1 ir_blocks 8 ir_instructions 19 ir_constants 4 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_loop_break_acc_exit%b68a4440e4cd3e23031d7f0012f39a14d3f773b42c399063d91123e79bda5cc9%elf_bytes 401%tokens 49 items 1 types 2 signatures 1 parameters 1 statements 8 bodies 1 expressions 17 symbols 1 resolved 0 locals 3 local_refs 6 typed 17 ir_functions 1 ir_blocks 8 ir_instructions 24 ir_constants 6 ir_values 16 ir_skipped 0 ir_verified 16" \
  "elf_loop_continue_exit%87b7ce18c0919b063cb6684ad160a7e4b41946546eb76723c77bb5f4e3b739ca%elf_bytes 360%tokens 49 items 1 types 2 signatures 1 parameters 1 statements 8 bodies 1 expressions 17 symbols 1 resolved 0 locals 3 local_refs 6 typed 17 ir_functions 1 ir_blocks 7 ir_instructions 21 ir_constants 6 ir_values 14 ir_skipped 0 ir_verified 14" \
  "elf_loop_call_body_exit%6c55c1892ddff569877b4f9ca2c9e2c62cc77758d1159245c48e949abe09c345%elf_bytes 335%tokens 76 items 3 types 6 signatures 3 parameters 3 statements 8 bodies 3 expressions 22 symbols 3 resolved 2 locals 5 local_refs 8 typed 20 ir_functions 3 ir_blocks 6 ir_instructions 23 ir_constants 5 ir_values 17 ir_skipped 0 ir_verified 17" \
  "elf_loop_call_break_exit%d0a8b33d6302bc8f1cd247f4b8ff9222fd5fd43fb99b98eed852f01b3db674a1%elf_bytes 410%tokens 65 items 2 types 4 signatures 2 parameters 2 statements 9 bodies 2 expressions 20 symbols 2 resolved 1 locals 4 local_refs 7 typed 19 ir_functions 2 ir_blocks 9 ir_instructions 27 ir_constants 6 ir_values 18 ir_skipped 0 ir_verified 18" \
  "elf_loop_call_continue_exit%7abc51b9fcf8590f8ec5f06ee746fa4e73839357fc667d3924fe7e581110672f%elf_bytes 373%tokens 67 items 2 types 4 signatures 2 parameters 2 statements 9 bodies 2 expressions 22 symbols 2 resolved 1 locals 4 local_refs 8 typed 21 ir_functions 2 ir_blocks 8 ir_instructions 25 ir_constants 6 ir_values 17 ir_skipped 0 ir_verified 17" \
  "elf_loop_merge_exit%e890f8d669368812d43fe1af3c88d0911acafb6c88ea70794b39c8fe31279d6d%elf_bytes 397%tokens 56 items 1 types 2 signatures 1 parameters 1 statements 8 bodies 1 expressions 20 symbols 1 resolved 0 locals 3 local_refs 8 typed 20 ir_functions 1 ir_blocks 8 ir_instructions 24 ir_constants 6 ir_values 16 ir_skipped 0 ir_verified 16" \
  "elf_loop_merge2_exit%9b0085ff0a9805b0ab90a60bdaccc81bf3f9d76748e890d3502c54f8b8b896dc%elf_bytes 592%tokens 73 items 1 types 2 signatures 1 parameters 1 statements 11 bodies 1 expressions 29 symbols 1 resolved 0 locals 4 local_refs 11 typed 29 ir_functions 1 ir_blocks 8 ir_instructions 32 ir_constants 9 ir_values 24 ir_skipped 0 ir_verified 24" \
  "elf_loop_merge_post_exit%d04262cfe1008893c2c042e4093d58863d3bedce971683241366e251f5b999ba%elf_bytes 397%tokens 56 items 1 types 2 signatures 1 parameters 1 statements 8 bodies 1 expressions 20 symbols 1 resolved 0 locals 3 local_refs 8 typed 20 ir_functions 1 ir_blocks 8 ir_instructions 24 ir_constants 6 ir_values 16 ir_skipped 0 ir_verified 16" \
  "elf_loop_let_exit%1c9239458228738943eef1a8b22760b4225b9e646f1c04a5b912142d5cc5088b%elf_bytes 326%tokens 64 items 2 types 4 signatures 2 parameters 2 statements 8 bodies 2 expressions 20 symbols 2 resolved 1 locals 5 local_refs 8 typed 19 ir_functions 2 ir_blocks 5 ir_instructions 20 ir_constants 5 ir_values 15 ir_skipped 0 ir_verified 15" \
  "elf_loop_let_break_exit%8eddda888594d82bb9b34e4d174ffcda5b08fb3114e5609e3467b1834fe28a08%elf_bytes 420%tokens 55 items 1 types 2 signatures 1 parameters 1 statements 9 bodies 1 expressions 20 symbols 1 resolved 0 locals 4 local_refs 8 typed 20 ir_functions 1 ir_blocks 8 ir_instructions 25 ir_constants 6 ir_values 17 ir_skipped 0 ir_verified 17" \
  "elf_loop_seq2_exit%6b051d8a7bafbdc067996fec3e7c6623b013eb3c37d96e65dabd2db2b524e1f4%elf_bytes 302%tokens 45 items 1 types 2 signatures 1 parameters 1 statements 6 bodies 1 expressions 18 symbols 1 resolved 0 locals 2 local_refs 7 typed 18 ir_functions 1 ir_blocks 7 ir_instructions 21 ir_constants 5 ir_values 14 ir_skipped 0 ir_verified 14" \
  "elf_loop_seq3_exit%460371b37db8111bc29f15dd7415272152b812b7a10fa98e4c9067dc2e717578%elf_bytes 496%tokens 60 items 1 types 2 signatures 1 parameters 1 statements 9 bodies 1 expressions 25 symbols 1 resolved 0 locals 3 local_refs 10 typed 25 ir_functions 1 ir_blocks 7 ir_instructions 27 ir_constants 7 ir_values 20 ir_skipped 0 ir_verified 20" \
  "elf_loop_seq4_exit%a8741f9a9d3719e3d1854a7fd7e76b0933e82aa542e5aa2a795a779aeb58c4eb%elf_bytes 703%tokens 72 items 1 types 2 signatures 1 parameters 1 statements 10 bodies 1 expressions 34 symbols 1 resolved 0 locals 3 local_refs 12 typed 34 ir_functions 1 ir_blocks 10 ir_instructions 39 ir_constants 10 ir_values 29 ir_skipped 0 ir_verified 29" \
  "elf_loop_seq_break_exit%7bf1811bed7526c242418489f9457f7bcf1a2d2ed5488922debfd53ae247f8da%elf_bytes 648%tokens 69 items 1 types 2 signatures 1 parameters 1 statements 11 bodies 1 expressions 30 symbols 1 resolved 0 locals 3 local_refs 12 typed 30 ir_functions 1 ir_blocks 11 ir_instructions 36 ir_constants 8 ir_values 25 ir_skipped 0 ir_verified 25" \
  "elf_loop_seq_continue_exit%6846b5e03201c8ebf8b24d186248354e4e14d804161a51ce2b668a2bf4f80f1b%elf_bytes 621%tokens 69 items 1 types 2 signatures 1 parameters 1 statements 11 bodies 1 expressions 30 symbols 1 resolved 0 locals 3 local_refs 11 typed 30 ir_functions 1 ir_blocks 10 ir_instructions 34 ir_constants 9 ir_values 24 ir_skipped 0 ir_verified 24" \
  "elf_pre_if_exit%b1b3a21428f50982657983f11147c7d07555373a9d67e85ac4d5f6d61ae46a01%elf_bytes 396%tokens 57 items 1 types 2 signatures 1 parameters 1 statements 9 bodies 1 expressions 19 symbols 1 resolved 0 locals 3 local_refs 7 typed 19 ir_functions 1 ir_blocks 7 ir_instructions 23 ir_constants 7 ir_values 16 ir_skipped 0 ir_verified 16" \
  "elf_pre_if_chain_exit%6d6b97ae7aad97a1155ffdf2b9f9535d4f2381bb418f3f5b79b8cf1e7c19df97%elf_bytes 702%tokens 86 items 1 types 2 signatures 1 parameters 1 statements 15 bodies 1 expressions 33 symbols 1 resolved 0 locals 4 local_refs 12 typed 33 ir_functions 1 ir_blocks 10 ir_instructions 38 ir_constants 12 ir_values 28 ir_skipped 0 ir_verified 28" \
  "elf_pre_if_guard_exit%b745edd35d232c9856ea3c7d61b4c10c87b1487a3de8b8681d719517f82abf9a%elf_bytes 480%tokens 64 items 1 types 2 signatures 1 parameters 1 statements 11 bodies 1 expressions 22 symbols 1 resolved 0 locals 3 local_refs 7 typed 22 ir_functions 1 ir_blocks 11 ir_instructions 32 ir_constants 9 ir_values 21 ir_skipped 0 ir_verified 21" \
  "elf_loop_sum_exit%a1b21466a559aaa775cc2fbf5a14ae5d19dba8013c6fc231665b03401d3c3f49%elf_bytes 243%tokens 34 items 1 types 2 signatures 1 parameters 1 statements 4 bodies 1 expressions 12 symbols 1 resolved 0 locals 2 local_refs 4 typed 12 ir_functions 1 ir_blocks 4 ir_instructions 14 ir_constants 4 ir_values 10 ir_skipped 0 ir_verified 10" \
  "elf_nested_if_exit%3b2c92de0fc1b9fc4980a8028ed9b34afbb009283ddc465a203593d905a3d7c5%elf_bytes 226%tokens 30 items 1 types 2 signatures 1 parameters 1 statements 5 bodies 1 expressions 9 symbols 1 resolved 0 locals 1 local_refs 2 typed 9 ir_functions 1 ir_blocks 5 ir_instructions 13 ir_constants 5 ir_values 8 ir_skipped 0 ir_verified 8" \
  "elf_nested_if_deep_exit%7c02a70e595d86e925c74d79ecd1c039b6b0d94bf366e31f846e5dca61acd7dc%elf_bytes 296%tokens 46 items 1 types 2 signatures 1 parameters 1 statements 9 bodies 1 expressions 17 symbols 1 resolved 0 locals 1 local_refs 4 typed 17 ir_functions 1 ir_blocks 9 ir_instructions 23 ir_constants 9 ir_values 14 ir_skipped 0 ir_verified 14" \
  "elf_nested_else_exit%cb9ca21c495081805de650dc57890e3e7ddbf69f9c7c2e72e983f1d3130df719%elf_bytes 261%tokens 47 items 1 types 2 signatures 1 parameters 1 statements 7 bodies 1 expressions 13 symbols 1 resolved 0 locals 1 local_refs 3 typed 13 ir_functions 1 ir_blocks 7 ir_instructions 18 ir_constants 7 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_nested_let_exit%631fe6931882640fc3000a0810a0bf52cce7fb576bd5a17f048164eeba1aba0c%elf_bytes 273%tokens 54 items 1 types 2 signatures 1 parameters 1 statements 9 bodies 1 expressions 21 symbols 1 resolved 0 locals 5 local_refs 10 typed 21 ir_functions 1 ir_blocks 5 ir_instructions 17 ir_constants 5 ir_values 12 ir_skipped 0 ir_verified 12" \
  "elf_nested_const_exit%82af5754ceb3c348d888a83c9bd22bfbed5a0d6c145f9f14a924270a62a66dae%elf_bytes 235%tokens 45 items 4 types 2 signatures 1 parameters 1 statements 5 bodies 1 expressions 11 symbols 4 resolved 5 locals 1 local_refs 3 typed 11 ir_functions 1 ir_blocks 5 ir_instructions 14 ir_constants 5 ir_values 9 ir_skipped 0 ir_verified 9" \
  "elf_nested_call_exit%ba010a85d7f77738c010abb4ac83eb21188b8950420bdac68d24a7e3e9a2e41b%elf_bytes 307%tokens 84 items 3 types 7 signatures 3 parameters 4 statements 8 bodies 3 expressions 28 symbols 3 resolved 4 locals 5 local_refs 9 typed 24 ir_functions 3 ir_blocks 7 ir_instructions 24 ir_constants 5 ir_values 17 ir_skipped 0 ir_verified 17" \
  "elf_int_slice_exit%e0f6e8750470252e2054c7814738144e89d3e78b4b2d3e6d02a1dcde62cad375%elf_bytes 381%tokens 43 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 14 symbols 1 resolved 0 locals 3 local_refs 4 typed 12 ir_functions 1 ir_blocks 1 ir_instructions 10 ir_constants 2 ir_values 8 ir_skipped 0 ir_verified 8" \
  "elf_int_append_exit%b5eb3f61f7f46636153c68c9d646260a76ab8f116670c05cdbfa5f81de614b26%elf_bytes 873%tokens 64 items 1 types 2 signatures 1 parameters 1 statements 5 bodies 1 expressions 26 symbols 1 resolved 0 locals 5 local_refs 7 typed 22 ir_functions 1 ir_blocks 1 ir_instructions 15 ir_constants 4 ir_values 13 ir_skipped 0 ir_verified 13" \
  "elf_owned_param_exit%d703cf1189e0da0b0222a9acc22363734577155b2405a98aadf5e22f65fd6647%elf_bytes 529%tokens 64 items 2 types 6 signatures 2 parameters 3 statements 5 bodies 2 expressions 16 symbols 2 resolved 1 locals 4 local_refs 4 typed 13 ir_functions 2 ir_blocks 4 ir_instructions 18 ir_constants 3 ir_values 11 ir_skipped 0 ir_verified 11" \
  "elf_owned_pass_exit%a93d10fee5af1ae4a21e67c9a7d82a413b3a2048a87116b7776a4a7ad024a84b%elf_bytes 807%tokens 87 items 3 types 9 signatures 3 parameters 4 statements 6 bodies 3 expressions 23 symbols 3 resolved 3 locals 5 local_refs 6 typed 18 ir_functions 3 ir_blocks 5 ir_instructions 26 ir_constants 3 ir_values 17 ir_skipped 0 ir_verified 17"
do
  op_fixture="${spec%%\%*}"
  spec="${spec#*\%}"
  op_hash="${spec%%\%*}"
  spec="${spec#*\%}"
  op_expected="${spec//\%/$'\n'}"
  op_result="$("$stage1" "$repo_dir/compiler/testdata/$op_fixture.mko")"
  if [[ "$op_result" != "$op_expected" ]]; then
    echo "selfhost backend gate: ELF $op_fixture image changed: $op_result" >&2
    exit 1
  fi
  "$stage1" "$repo_dir/compiler/testdata/$op_fixture.mko" "$work_dir/$op_fixture" >/dev/null
  if [[ "$(sha256_of "$work_dir/$op_fixture")" != "$op_hash" ]]; then
    echo "selfhost backend gate: ELF $op_fixture image bytes changed" >&2
    exit 1
  fi
done
# Stage-0/stage-1 differential execution: the current compiler builds every
# executable fixture with a driver that feeds argc exactly like the stage-1
# entry bridge; the stage-0 and stage-1 binaries must agree on exit status.
for spec in \
  "elf_exit%prog_main()%42%42" \
  "elf_add_exit%prog_main()%42%42" \
  "elf_nested_if_exit%prog_main(int(argc()))%1%2" \
  "elf_nested_if_deep_exit%prog_main(int(argc()))%10%20" \
  "elf_nested_else_exit%prog_main(int(argc()))%4%5" \
  "elf_nested_let_exit%prog_main(int(argc()))%253%255" \
  "elf_nested_const_exit%prog_main(int(argc()))%2%2" \
  "elf_nested_call_exit%prog_main(int(argc()))%102%104" \
  "elf_int_slice_exit%prog_main(int(argc()))%4%5" \
  "elf_int_append_exit%prog_main(int(argc()))%10%11" \
  "elf_owned_param_exit%prog_main(int(argc()))%3%3" \
  "elf_owned_pass_exit%prog_main(int(argc()))%4%104" \
  "typed_ir_call%use_identity()%1%1" \
  "typed_ir_cfg_call%choose_value(argc() > 0)%1%1" \
  "elf_select_exit%choose(argc() > 0)%42%42" \
  "elf_compare_exit%classify(int(argc()))%7%42" \
  "elf_call_add_exit%compute()%42%42" \
  "elf_call2_add_exit%compute()%42%42" \
  "elf_call_branch_exit%compute()%42%42" \
  "elf_sub2_exit%compute()%42%42" \
  "elf_mul_param_exit%compute()%42%42" \
  "elf_call_sub_exit%compute()%42%42" \
  "elf_two_call_exit%compute()%42%42" \
  "elf_two_call_sub_exit%compute()%42%42" \
  "elf_two_call_mul_exit%compute()%42%42" \
  "elf_two_call2_add_exit%compute()%42%42" \
  "elf_two_call2_sub_exit%compute()%42%42" \
  "elf_cfg_call_exit%choose_value(int(argc()))%7%42" \
  "elf_cfg_call2_exit%choose(int(argc()))%21%42" \
  "elf_cfg_merge_call_exit%choose(int(argc()))%7%42" \
  "elf_scalar_locals_exit%compute(int(argc()))%8%10" \
  "elf_scalar_callee_exit%compute()%82%82" \
  "elf_scalar_call_exit%compute(int(argc()))%42%43" \
  "elf_scalar_call2_exit%compute(int(argc()))%42%43" \
  "elf_scalar_call3_exit%compute(int(argc()))%43%44" \
  "elf_scalar_call6_exit%compute()%42%42" \
  "elf_scalar_div_exit%compute(int(argc()))%42%49" \
  "elf_scalar_mod_exit%compute(int(argc()))%28%35" \
  "elf_unary_exit%compute(int(argc()))%0%1" \
  "elf_byte_cast_exit%prog_main(int(argc()))%1%2" \
  "elf_mixed_call_exit%compute(int(argc()))%42%43" \
  "elf_mixed_call2_exit%compute(int(argc()))%42%63" \
  "elf_three_call_exit%compute(int(argc()))%42%43" \
  "elf_scalar_call7_exit%compute(int(argc()))%42%48" \
  "elf_scalar_call9_exit%compute(int(argc()))%42%52" \
  "elf_loop_bound_exit%compute(int(argc()))%10%10" \
  "elf_loop_call_exit%compute(int(argc()))%6%9" \
  "elf_loop_acc_exit%compute(int(argc()))%12%20" \
  "elf_loop_acc_call_exit%compute(int(argc()))%3%6" \
  "elf_loop_acc3_exit%compute(int(argc()))%32%32" \
  "elf_big_locals_exit%compute(int(argc()))%67%68" \
  "elf_loop_break_exit%compute(int(argc()))%12%12" \
  "elf_loop_break_acc_exit%compute(int(argc()))%4%6" \
  "elf_loop_continue_exit%compute(int(argc()))%6%6" \
  "elf_loop_call_body_exit%compute(int(argc()))%20%90" \
  "elf_loop_call_break_exit%compute(int(argc()))%6%12" \
  "elf_loop_call_continue_exit%compute(int(argc()))%15%26" \
  "elf_loop_merge_exit%compute(int(argc()))%25%47" \
  "elf_loop_merge2_exit%compute(int(argc()))%64%82" \
  "elf_loop_merge_post_exit%compute(int(argc()))%6%21" \
  "elf_loop_let_exit%compute(int(argc()))%12%56" \
  "elf_loop_let_break_exit%compute(int(argc()))%12%12" \
  "elf_loop_seq2_exit%compute(int(argc()))%5%10" \
  "elf_loop_seq3_exit%compute(int(argc()))%5%14" \
  "elf_loop_seq4_exit%compute(int(argc()))%250%243" \
  "elf_loop_seq_break_exit%compute(int(argc()))%18%22" \
  "elf_loop_seq_continue_exit%compute(int(argc()))%12%18" \
  "elf_pre_if_exit%compute(int(argc()))%13%20" \
  "elf_pre_if_chain_exit%compute(int(argc()))%17%19" \
  "elf_pre_if_guard_exit%compute(int(argc()))%11%13" \
  "elf_loop_sum_exit%compute(int(argc()))%8%15"
do
  diff_fixture="${spec%%\%*}"
  spec="${spec#*\%}"
  diff_call="${spec%%\%*}"
  spec="${spec#*\%}"
  diff_noarg="${spec%%\%*}"
  diff_onearg="${spec#*\%}"
  {
    sed 's/^fn main/fn prog_main/' "$repo_dir/compiler/testdata/$diff_fixture.mko"
    printf '\nfn main() {\n    let diff_result = %s\n    exit(diff_result)\n}\n' "$diff_call"
  } > "$work_dir/diff-driver.mko"
  "$stage0" build "$work_dir/diff-driver.mko" --no-incremental -o "$work_dir/diff-driver" >/dev/null
  set +e
  "$work_dir/diff-driver" >/dev/null 2>&1
  diff_noarg_status=$?
  "$work_dir/diff-driver" arg >/dev/null 2>&1
  diff_onearg_status=$?
  set -e
  if [[ "$diff_noarg_status" -ne "$diff_noarg" || "$diff_onearg_status" -ne "$diff_onearg" ]]; then
    echo "selfhost differential gate: $diff_fixture stage-0 exited $diff_noarg_status/$diff_onearg_status, expected $diff_noarg/$diff_onearg" >&2
    exit 1
  fi
done

if [[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]]; then
  chmod +x "$elf_hello_path"
  set +e
  elf_hello_out="$("$elf_hello_path")"
  elf_hello_status=$?
  set -e
  if [[ "$elf_hello_out" != $'hello\n \nworld' || "$elf_hello_status" -ne 42 ]]; then
    echo "selfhost backend gate: hello ELF printed [$elf_hello_out] exit $elf_hello_status, expected [hello world] exit 42" >&2
    exit 1
  fi
  chmod +x "$elf_print_int_path"
  set +e
  elf_print_int_out="$("$elf_print_int_path")"
  elf_print_int_status=$?
  set -e
  if [[ "$elf_print_int_out" != $'42\n-7\n0\n1000000000000\n-9223372036854775808\n9223372036854775807' || "$elf_print_int_status" -ne 7 ]]; then
    echo "selfhost backend gate: print-int ELF printed [$elf_print_int_out] exit $elf_print_int_status" >&2
    exit 1
  fi
  chmod +x "$elf_loop_print_path"
  set +e
  elf_loop_print_out="$("$elf_loop_print_path")"
  elf_loop_print_status=$?
  set -e
  if [[ "$elf_loop_print_out" != $'0\n0\ntick\n1\n0\ntick\n2\n10\ntick\n3\n30\ntick' || "$elf_loop_print_status" -ne 60 ]]; then
    echo "selfhost backend gate: loop-print ELF printed [$elf_loop_print_out] exit $elf_loop_print_status" >&2
    exit 1
  fi
  chmod +x "$elf_exit_path"
  set +e
  "$elf_exit_path" >/dev/null 2>&1
  elf_exit_status=$?
  set -e
  if [[ "$elf_exit_status" -ne 42 ]]; then
    echo "selfhost backend gate: generated ELF exited with $elf_exit_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$elf_select_path"
  set +e
  "$elf_select_path" >/dev/null 2>&1
  elf_select_status=$?
  set -e
  if [[ "$elf_select_status" -ne 42 ]]; then
    echo "selfhost backend gate: generated select ELF exited with $elf_select_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$elf_compare_path"
  set +e
  "$elf_compare_path" >/dev/null 2>&1
  elf_compare_false_status=$?
  "$elf_compare_path" arg >/dev/null 2>&1
  elf_compare_true_status=$?
  set -e
  if [[ "$elf_compare_false_status" -ne 7 ]]; then
    echo "selfhost backend gate: compare ELF false arm exited with $elf_compare_false_status, expected 7" >&2
    exit 1
  fi
  if [[ "$elf_compare_true_status" -ne 42 ]]; then
    echo "selfhost backend gate: compare ELF true arm exited with $elf_compare_true_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$elf_call_path"
  set +e
  "$elf_call_path" >/dev/null 2>&1
  elf_call_status=$?
  set -e
  if [[ "$elf_call_status" -ne 1 ]]; then
    echo "selfhost backend gate: call ELF exited with $elf_call_status, expected 1" >&2
    exit 1
  fi
  chmod +x "$elf_cfg_call_path"
  set +e
  "$elf_cfg_call_path" >/dev/null 2>&1
  elf_cfg_call_status=$?
  set -e
  if [[ "$elf_cfg_call_status" -ne 1 ]]; then
    echo "selfhost backend gate: CFG-call ELF exited with $elf_cfg_call_status, expected 1" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_cfg_call_exit"
  set +e
  "$work_dir/elf_cfg_call_exit" >/dev/null 2>&1
  cfg_arm_false=$?
  "$work_dir/elf_cfg_call_exit" arg >/dev/null 2>&1
  cfg_arm_true=$?
  set -e
  if [[ "$cfg_arm_false" -ne 7 || "$cfg_arm_true" -ne 42 ]]; then
    echo "selfhost backend gate: CFG-call arms exited $cfg_arm_false/$cfg_arm_true, expected 7/42" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_cfg_call2_exit"
  set +e
  "$work_dir/elf_cfg_call2_exit" >/dev/null 2>&1
  cfg2_arm_false=$?
  "$work_dir/elf_cfg_call2_exit" arg >/dev/null 2>&1
  cfg2_arm_true=$?
  set -e
  if [[ "$cfg2_arm_false" -ne 21 || "$cfg2_arm_true" -ne 42 ]]; then
    echo "selfhost backend gate: CFG-call2 arms exited $cfg2_arm_false/$cfg2_arm_true, expected 21/42" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_cfg_merge_call_exit"
  set +e
  "$work_dir/elf_cfg_merge_call_exit" >/dev/null 2>&1
  cfgm_arm_false=$?
  "$work_dir/elf_cfg_merge_call_exit" arg >/dev/null 2>&1
  cfgm_arm_true=$?
  set -e
  if [[ "$cfgm_arm_false" -ne 7 || "$cfgm_arm_true" -ne 42 ]]; then
    echo "selfhost backend gate: CFG-merge arms exited $cfgm_arm_false/$cfgm_arm_true, expected 7/42" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_locals_exit"
  set +e
  "$work_dir/elf_scalar_locals_exit" >/dev/null 2>&1
  scalar_noarg=$?
  "$work_dir/elf_scalar_locals_exit" arg >/dev/null 2>&1
  scalar_onearg=$?
  set -e
  if [[ "$scalar_noarg" -ne 8 || "$scalar_onearg" -ne 10 ]]; then
    echo "selfhost backend gate: scalar-locals ELF exited $scalar_noarg/$scalar_onearg, expected 8/10" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_callee_exit"
  set +e
  "$work_dir/elf_scalar_callee_exit" >/dev/null 2>&1
  scalar_callee=$?
  set -e
  if [[ "$scalar_callee" -ne 82 ]]; then
    echo "selfhost backend gate: scalar-callee ELF exited $scalar_callee, expected 82" >&2
    exit 1
  fi
  for scalar_fixture in elf_scalar_call_exit elf_scalar_call2_exit; do
    chmod +x "$work_dir/$scalar_fixture"
    set +e
    "$work_dir/$scalar_fixture" >/dev/null 2>&1
    scalar_noarg=$?
    "$work_dir/$scalar_fixture" arg >/dev/null 2>&1
    scalar_onearg=$?
    set -e
    if [[ "$scalar_noarg" -ne 42 || "$scalar_onearg" -ne 43 ]]; then
      echo "selfhost backend gate: $scalar_fixture exited $scalar_noarg/$scalar_onearg, expected 42/43" >&2
      exit 1
    fi
  done
  chmod +x "$work_dir/elf_scalar_call3_exit"
  set +e
  "$work_dir/elf_scalar_call3_exit" >/dev/null 2>&1
  call3_noarg=$?
  "$work_dir/elf_scalar_call3_exit" arg >/dev/null 2>&1
  call3_onearg=$?
  set -e
  if [[ "$call3_noarg" -ne 43 || "$call3_onearg" -ne 44 ]]; then
    echo "selfhost backend gate: scalar-call3 exited $call3_noarg/$call3_onearg, expected 43/44" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_call6_exit"
  set +e
  "$work_dir/elf_scalar_call6_exit" >/dev/null 2>&1
  call6_status=$?
  set -e
  if [[ "$call6_status" -ne 42 ]]; then
    echo "selfhost backend gate: scalar-call6 exited $call6_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_call7_exit"
  set +e
  "$work_dir/elf_scalar_call7_exit" >/dev/null 2>&1
  call7_noarg=$?
  "$work_dir/elf_scalar_call7_exit" arg >/dev/null 2>&1
  call7_onearg=$?
  set -e
  if [[ "$call7_noarg" -ne 42 || "$call7_onearg" -ne 48 ]]; then
    echo "selfhost backend gate: scalar-call7 exited $call7_noarg/$call7_onearg, expected 42/48" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_call9_exit"
  set +e
  "$work_dir/elf_scalar_call9_exit" >/dev/null 2>&1
  call9_noarg=$?
  "$work_dir/elf_scalar_call9_exit" arg >/dev/null 2>&1
  call9_onearg=$?
  set -e
  if [[ "$call9_noarg" -ne 42 || "$call9_onearg" -ne 52 ]]; then
    echo "selfhost backend gate: scalar-call9 exited $call9_noarg/$call9_onearg, expected 42/52" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_div_exit"
  set +e
  "$work_dir/elf_scalar_div_exit" >/dev/null 2>&1
  div_noarg=$?
  "$work_dir/elf_scalar_div_exit" arg >/dev/null 2>&1
  div_onearg=$?
  set -e
  if [[ "$div_noarg" -ne 42 || "$div_onearg" -ne 49 ]]; then
    echo "selfhost backend gate: scalar-div exited $div_noarg/$div_onearg, expected 42/49" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_scalar_mod_exit"
  set +e
  "$work_dir/elf_scalar_mod_exit" >/dev/null 2>&1
  mod_noarg=$?
  "$work_dir/elf_scalar_mod_exit" arg >/dev/null 2>&1
  mod_onearg=$?
  set -e
  if [[ "$mod_noarg" -ne 28 || "$mod_onearg" -ne 35 ]]; then
    echo "selfhost backend gate: scalar-mod exited $mod_noarg/$mod_onearg, expected 28/35" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_sum_exit"
  set +e
  "$work_dir/elf_loop_sum_exit" >/dev/null 2>&1
  loop_noarg=$?
  "$work_dir/elf_loop_sum_exit" arg >/dev/null 2>&1
  loop_onearg=$?
  set -e
  if [[ "$loop_noarg" -ne 8 || "$loop_onearg" -ne 15 ]]; then
    echo "selfhost backend gate: loop-sum ELF exited $loop_noarg/$loop_onearg, expected 8/15" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_bound_exit"
  set +e
  "$work_dir/elf_loop_bound_exit" >/dev/null 2>&1
  loopb_noarg=$?
  "$work_dir/elf_loop_bound_exit" arg >/dev/null 2>&1
  loopb_onearg=$?
  set -e
  if [[ "$loopb_noarg" -ne 10 || "$loopb_onearg" -ne 10 ]]; then
    echo "selfhost backend gate: loop-bound ELF exited $loopb_noarg/$loopb_onearg, expected 10/10" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_call_exit"
  set +e
  "$work_dir/elf_loop_call_exit" >/dev/null 2>&1
  loopc_noarg=$?
  "$work_dir/elf_loop_call_exit" arg >/dev/null 2>&1
  loopc_onearg=$?
  set -e
  if [[ "$loopc_noarg" -ne 6 || "$loopc_onearg" -ne 9 ]]; then
    echo "selfhost backend gate: loop-call ELF exited $loopc_noarg/$loopc_onearg, expected 6/9" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_acc_exit"
  set +e
  "$work_dir/elf_loop_acc_exit" >/dev/null 2>&1
  loopa_noarg=$?
  "$work_dir/elf_loop_acc_exit" arg >/dev/null 2>&1
  loopa_onearg=$?
  set -e
  if [[ "$loopa_noarg" -ne 12 || "$loopa_onearg" -ne 20 ]]; then
    echo "selfhost backend gate: loop-acc ELF exited $loopa_noarg/$loopa_onearg, expected 12/20" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_acc_call_exit"
  set +e
  "$work_dir/elf_loop_acc_call_exit" >/dev/null 2>&1
  loopac_noarg=$?
  "$work_dir/elf_loop_acc_call_exit" arg >/dev/null 2>&1
  loopac_onearg=$?
  set -e
  if [[ "$loopac_noarg" -ne 3 || "$loopac_onearg" -ne 6 ]]; then
    echo "selfhost backend gate: loop-acc-call ELF exited $loopac_noarg/$loopac_onearg, expected 3/6" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_acc3_exit"
  set +e
  "$work_dir/elf_loop_acc3_exit" >/dev/null 2>&1
  loopa3_noarg=$?
  "$work_dir/elf_loop_acc3_exit" arg >/dev/null 2>&1
  loopa3_onearg=$?
  set -e
  if [[ "$loopa3_noarg" -ne 32 || "$loopa3_onearg" -ne 32 ]]; then
    echo "selfhost backend gate: loop-acc3 ELF exited $loopa3_noarg/$loopa3_onearg, expected 32/32" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_big_locals_exit"
  set +e
  "$work_dir/elf_big_locals_exit" >/dev/null 2>&1
  big_noarg=$?
  "$work_dir/elf_big_locals_exit" arg >/dev/null 2>&1
  big_onearg=$?
  set -e
  if [[ "$big_noarg" -ne 67 || "$big_onearg" -ne 68 ]]; then
    echo "selfhost backend gate: big-locals ELF exited $big_noarg/$big_onearg, expected 67/68" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_break_exit"
  set +e
  "$work_dir/elf_loop_break_exit" >/dev/null 2>&1
  loopb_noarg=$?
  "$work_dir/elf_loop_break_exit" arg >/dev/null 2>&1
  loopb_onearg=$?
  set -e
  if [[ "$loopb_noarg" -ne 12 || "$loopb_onearg" -ne 12 ]]; then
    echo "selfhost backend gate: loop-break ELF exited $loopb_noarg/$loopb_onearg, expected 12/12" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_break_acc_exit"
  set +e
  "$work_dir/elf_loop_break_acc_exit" >/dev/null 2>&1
  loopba_noarg=$?
  "$work_dir/elf_loop_break_acc_exit" arg >/dev/null 2>&1
  loopba_onearg=$?
  set -e
  if [[ "$loopba_noarg" -ne 4 || "$loopba_onearg" -ne 6 ]]; then
    echo "selfhost backend gate: loop-break-acc ELF exited $loopba_noarg/$loopba_onearg, expected 4/6" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_continue_exit"
  set +e
  "$work_dir/elf_loop_continue_exit" >/dev/null 2>&1
  loopct_noarg=$?
  "$work_dir/elf_loop_continue_exit" arg >/dev/null 2>&1
  loopct_onearg=$?
  set -e
  if [[ "$loopct_noarg" -ne 6 || "$loopct_onearg" -ne 6 ]]; then
    echo "selfhost backend gate: loop-continue ELF exited $loopct_noarg/$loopct_onearg, expected 6/6" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_call_body_exit"
  set +e
  "$work_dir/elf_loop_call_body_exit" >/dev/null 2>&1
  loopcb_noarg=$?
  "$work_dir/elf_loop_call_body_exit" arg >/dev/null 2>&1
  loopcb_onearg=$?
  set -e
  if [[ "$loopcb_noarg" -ne 20 || "$loopcb_onearg" -ne 90 ]]; then
    echo "selfhost backend gate: loop-call-body ELF exited $loopcb_noarg/$loopcb_onearg, expected 20/90" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_call_break_exit"
  set +e
  "$work_dir/elf_loop_call_break_exit" >/dev/null 2>&1
  loopck_noarg=$?
  "$work_dir/elf_loop_call_break_exit" arg >/dev/null 2>&1
  loopck_onearg=$?
  set -e
  if [[ "$loopck_noarg" -ne 6 || "$loopck_onearg" -ne 12 ]]; then
    echo "selfhost backend gate: loop-call-break ELF exited $loopck_noarg/$loopck_onearg, expected 6/12" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_call_continue_exit"
  set +e
  "$work_dir/elf_loop_call_continue_exit" >/dev/null 2>&1
  loopcc_noarg=$?
  "$work_dir/elf_loop_call_continue_exit" arg >/dev/null 2>&1
  loopcc_onearg=$?
  set -e
  if [[ "$loopcc_noarg" -ne 15 || "$loopcc_onearg" -ne 26 ]]; then
    echo "selfhost backend gate: loop-call-continue ELF exited $loopcc_noarg/$loopcc_onearg, expected 15/26" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_merge_exit"
  set +e
  "$work_dir/elf_loop_merge_exit" >/dev/null 2>&1
  loopm_noarg=$?
  "$work_dir/elf_loop_merge_exit" arg >/dev/null 2>&1
  loopm_onearg=$?
  set -e
  if [[ "$loopm_noarg" -ne 25 || "$loopm_onearg" -ne 47 ]]; then
    echo "selfhost backend gate: loop-merge ELF exited $loopm_noarg/$loopm_onearg, expected 25/47" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_merge2_exit"
  set +e
  "$work_dir/elf_loop_merge2_exit" >/dev/null 2>&1
  loopm2_noarg=$?
  "$work_dir/elf_loop_merge2_exit" arg >/dev/null 2>&1
  loopm2_onearg=$?
  set -e
  if [[ "$loopm2_noarg" -ne 64 || "$loopm2_onearg" -ne 82 ]]; then
    echo "selfhost backend gate: loop-merge2 ELF exited $loopm2_noarg/$loopm2_onearg, expected 64/82" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_merge_post_exit"
  set +e
  "$work_dir/elf_loop_merge_post_exit" >/dev/null 2>&1
  loopmp_noarg=$?
  "$work_dir/elf_loop_merge_post_exit" arg >/dev/null 2>&1
  loopmp_onearg=$?
  set -e
  if [[ "$loopmp_noarg" -ne 6 || "$loopmp_onearg" -ne 21 ]]; then
    echo "selfhost backend gate: loop-merge-post ELF exited $loopmp_noarg/$loopmp_onearg, expected 6/21" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_let_exit"
  set +e
  "$work_dir/elf_loop_let_exit" >/dev/null 2>&1
  loopl_noarg=$?
  "$work_dir/elf_loop_let_exit" arg >/dev/null 2>&1
  loopl_onearg=$?
  set -e
  if [[ "$loopl_noarg" -ne 12 || "$loopl_onearg" -ne 56 ]]; then
    echo "selfhost backend gate: loop-let ELF exited $loopl_noarg/$loopl_onearg, expected 12/56" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_let_break_exit"
  set +e
  "$work_dir/elf_loop_let_break_exit" >/dev/null 2>&1
  looplb_noarg=$?
  "$work_dir/elf_loop_let_break_exit" arg >/dev/null 2>&1
  looplb_onearg=$?
  set -e
  if [[ "$looplb_noarg" -ne 12 || "$looplb_onearg" -ne 12 ]]; then
    echo "selfhost backend gate: loop-let-break ELF exited $looplb_noarg/$looplb_onearg, expected 12/12" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_seq2_exit"
  set +e
  "$work_dir/elf_loop_seq2_exit" >/dev/null 2>&1
  loops2_noarg=$?
  "$work_dir/elf_loop_seq2_exit" arg >/dev/null 2>&1
  loops2_onearg=$?
  set -e
  if [[ "$loops2_noarg" -ne 5 || "$loops2_onearg" -ne 10 ]]; then
    echo "selfhost backend gate: loop-seq2 ELF exited $loops2_noarg/$loops2_onearg, expected 5/10" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_seq3_exit"
  set +e
  "$work_dir/elf_loop_seq3_exit" >/dev/null 2>&1
  loops3_noarg=$?
  "$work_dir/elf_loop_seq3_exit" arg >/dev/null 2>&1
  loops3_onearg=$?
  set -e
  if [[ "$loops3_noarg" -ne 5 || "$loops3_onearg" -ne 14 ]]; then
    echo "selfhost backend gate: loop-seq3 ELF exited $loops3_noarg/$loops3_onearg, expected 5/14" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_seq4_exit"
  set +e
  "$work_dir/elf_loop_seq4_exit" >/dev/null 2>&1
  loops4_noarg=$?
  "$work_dir/elf_loop_seq4_exit" arg >/dev/null 2>&1
  loops4_onearg=$?
  set -e
  if [[ "$loops4_noarg" -ne 250 || "$loops4_onearg" -ne 243 ]]; then
    echo "selfhost backend gate: loop-seq4 ELF exited $loops4_noarg/$loops4_onearg, expected 250/243" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_seq_break_exit"
  set +e
  "$work_dir/elf_loop_seq_break_exit" >/dev/null 2>&1
  loopsb_noarg=$?
  "$work_dir/elf_loop_seq_break_exit" arg >/dev/null 2>&1
  loopsb_onearg=$?
  set -e
  if [[ "$loopsb_noarg" -ne 18 || "$loopsb_onearg" -ne 22 ]]; then
    echo "selfhost backend gate: loop-seq-break ELF exited $loopsb_noarg/$loopsb_onearg, expected 18/22" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_loop_seq_continue_exit"
  set +e
  "$work_dir/elf_loop_seq_continue_exit" >/dev/null 2>&1
  loopsc_noarg=$?
  "$work_dir/elf_loop_seq_continue_exit" arg >/dev/null 2>&1
  loopsc_onearg=$?
  set -e
  if [[ "$loopsc_noarg" -ne 12 || "$loopsc_onearg" -ne 18 ]]; then
    echo "selfhost backend gate: loop-seq-continue ELF exited $loopsc_noarg/$loopsc_onearg, expected 12/18" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_pre_if_exit"
  set +e
  "$work_dir/elf_pre_if_exit" >/dev/null 2>&1
  preif_noarg=$?
  "$work_dir/elf_pre_if_exit" arg >/dev/null 2>&1
  preif_onearg=$?
  set -e
  if [[ "$preif_noarg" -ne 13 || "$preif_onearg" -ne 20 ]]; then
    echo "selfhost backend gate: pre-if ELF exited $preif_noarg/$preif_onearg, expected 13/20" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_pre_if_chain_exit"
  set +e
  "$work_dir/elf_pre_if_chain_exit" >/dev/null 2>&1
  preifc_noarg=$?
  "$work_dir/elf_pre_if_chain_exit" arg >/dev/null 2>&1
  preifc_onearg=$?
  set -e
  if [[ "$preifc_noarg" -ne 17 || "$preifc_onearg" -ne 19 ]]; then
    echo "selfhost backend gate: pre-if-chain ELF exited $preifc_noarg/$preifc_onearg, expected 17/19" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_pre_if_guard_exit"
  set +e
  "$work_dir/elf_pre_if_guard_exit" >/dev/null 2>&1
  preifg_noarg=$?
  "$work_dir/elf_pre_if_guard_exit" arg >/dev/null 2>&1
  preifg_onearg=$?
  set -e
  if [[ "$preifg_noarg" -ne 11 || "$preifg_onearg" -ne 13 ]]; then
    echo "selfhost backend gate: pre-if-guard ELF exited $preifg_noarg/$preifg_onearg, expected 11/13" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_mixed_call_exit"
  set +e
  "$work_dir/elf_mixed_call_exit" >/dev/null 2>&1
  mixed_noarg=$?
  "$work_dir/elf_mixed_call_exit" arg >/dev/null 2>&1
  mixed_onearg=$?
  set -e
  if [[ "$mixed_noarg" -ne 42 || "$mixed_onearg" -ne 43 ]]; then
    echo "selfhost backend gate: mixed-call exited $mixed_noarg/$mixed_onearg, expected 42/43" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_mixed_call2_exit"
  set +e
  "$work_dir/elf_mixed_call2_exit" >/dev/null 2>&1
  mixed2_noarg=$?
  "$work_dir/elf_mixed_call2_exit" arg >/dev/null 2>&1
  mixed2_onearg=$?
  set -e
  if [[ "$mixed2_noarg" -ne 42 || "$mixed2_onearg" -ne 63 ]]; then
    echo "selfhost backend gate: mixed-call2 exited $mixed2_noarg/$mixed2_onearg, expected 42/63" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf_three_call_exit"
  set +e
  "$work_dir/elf_three_call_exit" >/dev/null 2>&1
  three_noarg=$?
  "$work_dir/elf_three_call_exit" arg >/dev/null 2>&1
  three_onearg=$?
  set -e
  if [[ "$three_noarg" -ne 42 || "$three_onearg" -ne 43 ]]; then
    echo "selfhost backend gate: three-call exited $three_noarg/$three_onearg, expected 42/43" >&2
    exit 1
  fi
  chmod +x "$elf_call_add_path"
  set +e
  "$elf_call_add_path" >/dev/null 2>&1
  elf_call_add_status=$?
  set -e
  if [[ "$elf_call_add_status" -ne 42 ]]; then
    echo "selfhost backend gate: call-add ELF exited with $elf_call_add_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$elf_call2_path"
  set +e
  "$elf_call2_path" >/dev/null 2>&1
  elf_call2_status=$?
  set -e
  if [[ "$elf_call2_status" -ne 42 ]]; then
    echo "selfhost backend gate: two-arg call ELF exited with $elf_call2_status, expected 42" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf-arg-get-guard"
  set +e
  arg_get_elf_noarg_out="$("$work_dir/elf-arg-get-guard")"
  arg_get_elf_noarg_status=$?
  arg_get_elf_onearg_out="$("$work_dir/elf-arg-get-guard" world)"
  arg_get_elf_onearg_status=$?
  set -e
  if [[ "$arg_get_elf_noarg_out" != "usage: prog <name>" || "$arg_get_elf_noarg_status" -ne 2 ]]; then
    echo "selfhost backend gate: arg_get-guard ELF no-arg printed [$arg_get_elf_noarg_out] exit $arg_get_elf_noarg_status" >&2
    exit 1
  fi
  if [[ "$arg_get_elf_onearg_out" != "world" || "$arg_get_elf_onearg_status" -ne 4 ]]; then
    echo "selfhost backend gate: arg_get-guard ELF one-arg printed [$arg_get_elf_onearg_out] exit $arg_get_elf_onearg_status" >&2
    exit 1
  fi
  chmod +x "$work_dir/elf-argc-guard"
  set +e
  argc_guard_elf_noarg_out="$("$work_dir/elf-argc-guard")"
  argc_guard_elf_noarg_status=$?
  argc_guard_elf_onearg_out="$("$work_dir/elf-argc-guard" one)"
  argc_guard_elf_onearg_status=$?
  set -e
  if [[ "$argc_guard_elf_noarg_out" != "usage: prog <arg>" || "$argc_guard_elf_noarg_status" -ne 2 ]]; then
    echo "selfhost backend gate: argc-guard ELF no-arg printed [$argc_guard_elf_noarg_out] exit $argc_guard_elf_noarg_status" >&2
    exit 1
  fi
  if [[ "$argc_guard_elf_onearg_out" != "have argument" || "$argc_guard_elf_onearg_status" -ne 3 ]]; then
    echo "selfhost backend gate: argc-guard ELF one-arg printed [$argc_guard_elf_onearg_out] exit $argc_guard_elf_onearg_status" >&2
    exit 1
  fi
  for chain_case in "elf-guard-chain:all probes ok:11" "elf-bool-guard:ready:0" \
    "elf-bool-guard-false:not ready:41" "elf-long-chain:chain ok:9" \
    "elf-long-chain-mid:step11 failed:51"; do
    chain_path="$work_dir/${chain_case%%:*}"
    chain_rest="${chain_case#*:}"
    chain_expect_out="${chain_rest%%:*}"
    chain_expect_status="${chain_rest##*:}"
    chmod +x "$chain_path"
    set +e
    chain_out="$("$chain_path")"
    chain_status=$?
    set -e
    if [[ "$chain_out" != "$chain_expect_out" || "$chain_status" -ne "$chain_expect_status" ]]; then
      echo "selfhost backend gate: $chain_path printed [$chain_out] exit $chain_status, expected [$chain_expect_out] exit $chain_expect_status" >&2
      exit 1
    fi
  done
  for op_fixture in elf_sub2_exit elf_mul_param_exit elf_call_sub_exit elf_two_call_exit elf_two_call_sub_exit elf_two_call_mul_exit elf_two_call2_add_exit elf_two_call2_sub_exit elf_call_branch_exit; do
    chmod +x "$work_dir/$op_fixture"
    set +e
    "$work_dir/$op_fixture" >/dev/null 2>&1
    op_status=$?
    set -e
    if [[ "$op_status" -ne 42 ]]; then
      echo "selfhost backend gate: $op_fixture ELF exited with $op_status, expected 42" >&2
      exit 1
    fi
  done
else
  echo "selfhost backend gate: ELF execution deferred (requires Linux x86-64 host)"
fi

# A three-block guard-clause function now reaches the general encoder instead
# of being rejected by the narrow branch shape, so it emits an image.
typed_ir_cfg_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg.mko")"
if [[ "$typed_ir_cfg_result" != $'elf_bytes 190\ntokens 22 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 5 symbols 1 resolved 0 locals 1 local_refs 2 typed 5 ir_functions 1 ir_blocks 3 ir_instructions 7 ir_constants 2 ir_values 4 ir_skipped 0 ir_verified 4' ]]; then
  echo "selfhost frontend gate: typed IR CFG shape changed: $typed_ir_cfg_result" >&2
  exit 1
fi
elf_typed_cfg_path="$work_dir/elf-typed-ir-cfg"
"$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg.mko" "$elf_typed_cfg_path" >/dev/null
if [[ "$(sha256_of "$elf_typed_cfg_path")" != "dcd5d88fd35324af0071ff85ee4af852f0d2698a9bf7efe748cfc104410c04a2" ]]; then
  echo "selfhost backend gate: ELF typed-IR-CFG image bytes changed" >&2
  exit 1
fi
if [[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]]; then
  # The entry bridge passes argc, so choose(argc) returns argc for any argc > 0.
  chmod +x "$elf_typed_cfg_path"
  set +e
  "$elf_typed_cfg_path"
  typed_cfg_noarg_status=$?
  "$elf_typed_cfg_path" a b c d e
  typed_cfg_args_status=$?
  set -e
  if [[ "$typed_cfg_noarg_status" -ne 1 || "$typed_cfg_args_status" -ne 6 ]]; then
    echo "selfhost backend gate: typed-IR-CFG ELF exited $typed_cfg_noarg_status/$typed_cfg_args_status, expected 1/6" >&2
    exit 1
  fi
fi

typed_ir_if_else_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_if_else.mko")"
if [[ "$typed_ir_if_else_result" != $'elf_bytes 218\ntokens 25 items 1 types 2 signatures 1 parameters 1 statements 3 bodies 1 expressions 5 symbols 1 resolved 0 locals 1 local_refs 2 typed 5 ir_functions 1 ir_blocks 4 ir_instructions 9 ir_constants 2 ir_values 5 ir_skipped 0 ir_verified 5' ]]; then
  echo "selfhost frontend gate: typed IR if/else shape changed: $typed_ir_if_else_result" >&2
  exit 1
fi

typed_ir_merge_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_merge.mko")"
if [[ "$typed_ir_merge_result" != $'elf_bytes 220\ntokens 35 items 1 types 2 signatures 1 parameters 1 statements 5 bodies 1 expressions 7 symbols 1 resolved 0 locals 4 local_refs 2 typed 7 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 4 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR merge shape changed: $typed_ir_merge_result" >&2
  exit 1
fi

typed_ir_statement_merge_owned_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_statement_merge_owned.mko")"
if [[ "$typed_ir_statement_merge_owned_result" != $'tokens 43 items 2 types 3 signatures 2 parameters 1 statements 5 bodies 2 expressions 7 symbols 2 resolved 2 locals 3 local_refs 1 typed 5 ir_functions 2 ir_blocks 5 ir_instructions 13 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR owned statement-merge shape changed: $typed_ir_statement_merge_owned_result" >&2
  exit 1
fi

typed_ir_cfg_owned_alias_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_owned_alias.mko")"
if [[ "$typed_ir_cfg_owned_alias_result" != $'elf_bytes 422\ntokens 37 items 1 types 2 signatures 1 parameters 1 statements 6 bodies 1 expressions 6 symbols 1 resolved 0 locals 5 local_refs 2 typed 6 ir_functions 1 ir_blocks 4 ir_instructions 14 ir_constants 4 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR owned alias shape changed: $typed_ir_cfg_owned_alias_result" >&2
  exit 1
fi

typed_ir_owned_drop_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_drop.mko")"
if [[ "$typed_ir_owned_drop_result" != $'tokens 21 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 8 symbols 1 resolved 0 locals 1 local_refs 0 typed 7 ir_functions 1 ir_blocks 1 ir_instructions 9 ir_constants 4 ir_values 7 ir_skipped 0 ir_verified 7' ]]; then
  echo "selfhost frontend gate: typed IR owned-drop shape changed: $typed_ir_owned_drop_result" >&2
  exit 1
fi

typed_ir_owned_move_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_move.mko")"
if [[ "$typed_ir_owned_move_result" != $'tokens 23 items 1 types 2 signatures 1 parameters 0 statements 2 bodies 1 expressions 8 symbols 1 resolved 0 locals 1 local_refs 1 typed 7 ir_functions 1 ir_blocks 1 ir_instructions 8 ir_constants 3 ir_values 7 ir_skipped 0 ir_verified 7' ]]; then
  echo "selfhost frontend gate: typed IR owned-move shape changed: $typed_ir_owned_move_result" >&2
  exit 1
fi

typed_ir_owned_cfg_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_cfg.mko")"
if [[ "$typed_ir_owned_cfg_result" != $'tokens 37 items 1 types 7 signatures 1 parameters 3 statements 3 bodies 1 expressions 3 symbols 1 resolved 0 locals 3 local_refs 3 typed 3 ir_functions 1 ir_blocks 4 ir_instructions 10 ir_constants 0 ir_values 4 ir_skipped 0 ir_verified 4' ]]; then
  echo "selfhost frontend gate: typed IR owned-CFG shape changed: $typed_ir_owned_cfg_result" >&2
  exit 1
fi

typed_ir_owned_string_drop_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_string_drop.mko")"
if [[ "$typed_ir_owned_string_drop_result" != $'elf_bytes 190\ntokens 15 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 2 symbols 1 resolved 0 locals 1 local_refs 0 typed 2 ir_functions 1 ir_blocks 1 ir_instructions 4 ir_constants 2 ir_values 2 ir_skipped 0 ir_verified 2' ]]; then
  echo "selfhost frontend gate: typed IR string-drop shape changed: $typed_ir_owned_string_drop_result" >&2
  exit 1
fi

typed_ir_owned_string_move_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_string_move.mko")"
# This fixture returns an owned string, which now lowers to an image as well as
# an IR summary, so the expected output carries the elf_bytes line.
if [[ "$typed_ir_owned_string_move_result" != $'elf_bytes 181\ntokens 15 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 2 symbols 1 resolved 0 locals 1 local_refs 1 typed 2 ir_functions 1 ir_blocks 1 ir_instructions 3 ir_constants 1 ir_values 2 ir_skipped 0 ir_verified 2' ]]; then
  echo "selfhost frontend gate: typed IR string-move shape changed: $typed_ir_owned_string_move_result" >&2
  exit 1
fi

typed_ir_owned_string_array_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_string_array.mko")"
if [[ "$typed_ir_owned_string_array_result" != $'tokens 19 items 1 types 1 signatures 1 parameters 0 statements 2 bodies 1 expressions 6 symbols 1 resolved 0 locals 1 local_refs 0 typed 5 ir_functions 1 ir_blocks 1 ir_instructions 7 ir_constants 3 ir_values 5 ir_skipped 0 ir_verified 5' ]]; then
  echo "selfhost frontend gate: typed IR string-array ownership shape changed: $typed_ir_owned_string_array_result" >&2
  exit 1
fi

typed_ir_call_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_call.mko")"
if [[ "$typed_ir_call_result" != $'elf_bytes 151\ntokens 27 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 4 symbols 2 resolved 1 locals 1 local_refs 1 typed 3 ir_functions 2 ir_blocks 2 ir_instructions 5 ir_constants 1 ir_values 3 ir_skipped 0 ir_verified 3' ]]; then
  echo "selfhost frontend gate: typed IR call shape changed: $typed_ir_call_result" >&2
  exit 1
fi

typed_ir_owned_call_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_call_result.mko")"
if [[ "$typed_ir_owned_call_result" != $'tokens 23 items 2 types 2 signatures 2 parameters 0 statements 2 bodies 2 expressions 3 symbols 2 resolved 1 locals 0 local_refs 0 typed 2 ir_functions 2 ir_blocks 2 ir_instructions 6 ir_constants 1 ir_values 4 ir_skipped 0 ir_verified 4' ]]; then
  echo "selfhost frontend gate: typed IR owned call-result shape changed: $typed_ir_owned_call_result" >&2
  exit 1
fi

typed_ir_owned_call_argument_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_owned_call_argument.mko")"
if [[ "$typed_ir_owned_call_argument_result" != $'tokens 27 items 2 types 3 signatures 2 parameters 1 statements 2 bodies 2 expressions 4 symbols 2 resolved 1 locals 1 local_refs 1 typed 3 ir_functions 2 ir_blocks 2 ir_instructions 7 ir_constants 1 ir_values 5 ir_skipped 0 ir_verified 5' ]]; then
  echo "selfhost frontend gate: typed IR owned call-argument policy changed: $typed_ir_owned_call_argument_result" >&2
  exit 1
fi

typed_ir_multi_call_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_multi_call.mko")"
if [[ "$typed_ir_multi_call_result" != $'tokens 35 items 2 types 4 signatures 2 parameters 2 statements 2 bodies 2 expressions 8 symbols 2 resolved 1 locals 2 local_refs 2 typed 7 ir_functions 2 ir_blocks 2 ir_instructions 12 ir_constants 2 ir_values 8 ir_skipped 0 ir_verified 8' ]]; then
  echo "selfhost frontend gate: typed IR multi-call shape changed: $typed_ir_multi_call_result" >&2
  exit 1
fi

typed_ir_call_clone_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_call_clone.mko")"
if [[ "$typed_ir_call_clone_result" != $'tokens 35 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 6 symbols 2 resolved 1 locals 3 local_refs 3 typed 5 ir_functions 2 ir_blocks 2 ir_instructions 9 ir_constants 1 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR call-clone policy changed: $typed_ir_call_clone_result" >&2
  exit 1
fi

typed_ir_cfg_call_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_call.mko")"
if [[ "$typed_ir_cfg_call_result" != $'elf_bytes 170\ntokens 39 items 2 types 4 signatures 2 parameters 2 statements 4 bodies 2 expressions 8 symbols 2 resolved 2 locals 2 local_refs 2 typed 6 ir_functions 2 ir_blocks 4 ir_instructions 10 ir_constants 2 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR CFG-call shape changed: $typed_ir_cfg_call_result" >&2
  exit 1
fi

typed_ir_cfg_owned_call_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_owned_call_result.mko")"
if [[ "$typed_ir_cfg_owned_call_result" != $'tokens 34 items 2 types 3 signatures 2 parameters 1 statements 4 bodies 2 expressions 6 symbols 2 resolved 2 locals 1 local_refs 1 typed 4 ir_functions 2 ir_blocks 4 ir_instructions 9 ir_constants 1 ir_values 5 ir_skipped 0 ir_verified 5' ]]; then
  echo "selfhost frontend gate: typed IR owned CFG-call result shape changed: $typed_ir_cfg_owned_call_result" >&2
  exit 1
fi

typed_ir_cfg_owned_call_argument="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_owned_call_argument.mko")"
if [[ "$typed_ir_cfg_owned_call_argument" != $'tokens 40 items 2 types 5 signatures 2 parameters 3 statements 4 bodies 2 expressions 6 symbols 2 resolved 1 locals 3 local_refs 2 typed 5 ir_functions 2 ir_blocks 4 ir_instructions 14 ir_constants 2 ir_values 7 ir_skipped 0 ir_verified 7' ]]; then
  echo "selfhost frontend gate: typed IR owned CFG-call argument shape changed: $typed_ir_cfg_owned_call_argument" >&2
  exit 1
fi

typed_ir_cfg_hold_call_argument="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_cfg_hold_call_argument.mko")"
if [[ "$typed_ir_cfg_hold_call_argument" != $'elf_bytes 254\ntokens 41 items 2 types 4 signatures 2 parameters 2 statements 5 bodies 2 expressions 7 symbols 2 resolved 1 locals 3 local_refs 2 typed 6 ir_functions 2 ir_blocks 4 ir_instructions 12 ir_constants 3 ir_values 6 ir_skipped 0 ir_verified 6' ]]; then
  echo "selfhost frontend gate: typed IR hold CFG-call argument shape changed: $typed_ir_cfg_hold_call_argument" >&2
  exit 1
fi

if typed_ir_hold_after_move_result="$("$stage1" "$repo_dir/compiler/testdata/typed_ir_call_hold_after_move.mko" 2>&1)"; then
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

if print_argument_result="$("$stage1" "$repo_dir/compiler/testdata/bad_print_type.mko" 2>&1)"; then
  echo "selfhost frontend gate: float print argument unexpectedly accepted" >&2
  exit 1
fi
if [[ "$print_argument_result" != *":2:11: print needs string or int, got float" ]]; then
  echo "selfhost frontend gate: unexpected print argument diagnostic: $print_argument_result" >&2
  exit 1
fi

# A declared result with no return must be a diagnostic, never a lowered body.
# Stage 1 previously emitted an executable that returned zero, which is how an
# unparsed single-line body became a silently wrong program.
if missing_return_result="$("$stage1" "$repo_dir/compiler/testdata/bad_missing_return.mko" 2>&1)"; then
  echo "selfhost frontend gate: missing return unexpectedly accepted" >&2
  exit 1
fi
if [[ "$missing_return_result" != *":1:4: function must return a value declared by its result type" ]]; then
  echo "selfhost frontend gate: unexpected missing-return diagnostic: $missing_return_result" >&2
  exit 1
fi
if "$stage1" "$repo_dir/compiler/testdata/bad_missing_return.mko" "$work_dir/bad-missing-return.elf" >/dev/null 2>&1; then
  echo "selfhost frontend gate: missing return produced an image" >&2
  exit 1
fi
if [[ -e "$work_dir/bad-missing-return.elf" ]]; then
  echo "selfhost frontend gate: missing return left an output file" >&2
  exit 1
fi

if syscall_arity_result="$("$stage1" "$repo_dir/compiler/testdata/bad_syscall_arity.mko" 2>&1)"; then
  echo "selfhost frontend gate: invalid linux_syscall6 arity unexpectedly accepted" >&2
  exit 1
fi
if [[ "$syscall_arity_result" != *":2:26: linux_syscall6 needs exactly seven arguments"* ]]; then
  echo "selfhost frontend gate: unexpected syscall arity diagnostic: $syscall_arity_result" >&2
  exit 1
fi
if byte_type_result="$("$stage1" "$repo_dir/compiler/testdata/bad_byte_cast.mko" 2>&1)"; then
  echo "selfhost frontend gate: string-to-byte conversion unexpectedly accepted" >&2
  exit 1
fi
if [[ "$byte_type_result" != *":2:17: byte needs int, got string"* ]]; then
  echo "selfhost frontend gate: unexpected byte conversion diagnostic: $byte_type_result" >&2
  exit 1
fi

if unsafe_memory_arity_result="$($stage1 "$repo_dir/compiler/testdata/bad_unsafe_memory_arity.mko" 2>&1)"; then
 echo "selfhost frontend gate: unsafe pointer arity unexpectedly accepted" >&2
 exit 1
fi
if [[ "$unsafe_memory_arity_result" != *":2:28: expected 1 args, got 0"* ]]; then
 echo "selfhost frontend gate: unexpected unsafe pointer diagnostic: $unsafe_memory_arity_result" >&2
 exit 1
fi
if unsafe_memory_type_result="$($stage1 "$repo_dir/compiler/testdata/bad_unsafe_memory_type.mko" 2>&1)"; then
 echo "selfhost frontend gate: unsafe pointer type unexpectedly accepted" >&2
 exit 1
fi
if [[ "$unsafe_memory_type_result" != *"unsafe_ptr_store8 needs int pointer and int byte, got string"* ]]; then
 echo "selfhost frontend gate: unexpected unsafe pointer type diagnostic: $unsafe_memory_type_result" >&2
 exit 1
fi
if unsafe_write_type_result="$($stage1 "$repo_dir/compiler/testdata/bad_unsafe_write_type.mko" 2>&1)"; then
 echo "selfhost frontend gate: unsafe write type unexpectedly accepted" >&2
 exit 1
fi
if [[ "$unsafe_write_type_result" != *"unsafe_write needs int fd pointer and length, got string"* ]]; then
 echo "selfhost frontend gate: unexpected unsafe write diagnostic: $unsafe_write_type_result" >&2
 exit 1
fi
echo "selfhost frontend gate: ok"
