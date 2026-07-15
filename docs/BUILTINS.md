# Mako Built-in Functions Reference

Complete reference for every built-in function available in Mako.
Signatures use the form `function_name(param: type, ...) -> return_type`.

---

## 1. Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(s: string) -> void` | Print string + newline to stdout |
| `print_raw` | `print_raw(s: string) -> int` | Print string **without** newline |
| `print_int` / `print_int64` / `print_int32` / `print_int8` / `print_uint64` | typed int print + newline | |
| `print_float` / `print_bool` | float / bool + newline | |
| `eprint` / `eprintln` | `(s) -> int` | stderr without / with newline |
| `dbg` / `dbg_str` | debug echo | |
| `format_int` / `format_int_dec` / `int_to_string` | decimal string |
| `format_int_hex` / `format_int_hex_upper` / `format_int_hex_prefix` | hex (`ff` / `FF` / `0xff`) |
| `format_int_hex_pad` | `(n, width)` zero-padded hex |
| `format_int_bin` / `format_int_oct` | binary / octal |
| `format_int_base` | `(n, base)` base 2–36 |
| `format_pad` | `(s, width, zero)` left pad |
| `format_float` / `format_bool` | float / bool |
| `parse_int` | decimal |
| `parse_int_hex` / `parse_int_bin` / `parse_int_oct` | base parse (`0x`/`0b`/`0o` ok) |
| `parse_int_base` | `(s, base)` base 2–36; `base=0` auto prefix |
| `parse_int_auto` | same as base 0 |
| `hex_encode` / `hex_decode` | byte string ↔ hex (encoding/hex) |

### `fmt` package (Go-style)

**String args:** `%%` `%s` `%v` `%t` `%q` `%x`/`%X` (byte hex) `%f` `%g`  
**Int args** (`fmt_sprintf_d` / `fmt_sprintf_dd`): `%d` `%i` `%v` `%b` `%o` `%x` `%X`  
Flags: `#` (`0x`/`0b`/`0`), `0` zero-pad, `+` sign, width (`%08x`).

| Function | Signature | Description |
|----------|-----------|-------------|
| `fmt_sprintf` … `fmt_sprintf4` | `(fmt, a…)` | Format → string (1–4 string args) |
| `fmt_sprintf_d` | `(fmt, n: int)` | int verbs `%d %x %X %b %o` + flags |
| `fmt_sprintf_dd` | `(fmt, a, b: int)` | two int verbs |
| `fmt_sprintf_f` | `(fmt, v: float, prec)` | float into first verb |
| `fmt_sprint` … `fmt_sprint3` | join with spaces | |
| `fmt_sprintln` / `fmt_sprintln2` | join + `"\n"` | |
| `fmt_print` / `fmt_print2` | stdout, no newline | |
| `fmt_println` / `fmt_println2` | stdout + newline | |
| `fmt_printf` … `fmt_printf3` | printf to stdout | |
| `fmt_eprint` / `fmt_eprintln` / `fmt_eprintf` | stderr | |
| `fmt_errorf` / `fmt_errorf2` | format error string | |

Packs: `std/fmt`, `std/print`. Tests: `fmt_print_test.mko`. Demo: `examples/fmt_demo.mko`.

---

## 2. Strings

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_len` | `str_len(s: string) -> int` | Return byte length of a string |
| `str_eq` | `str_eq(a: string, b: string) -> bool` | Test two strings for equality |
| `str_contains` | `str_contains(s: string, substr: string) -> bool` | Check if string contains a substring |
| `str_has_prefix` | `str_has_prefix(s: string, prefix: string) -> bool` | Check if string starts with prefix |
| `str_has_suffix` | `str_has_suffix(s: string, suffix: string) -> bool` | Check if string ends with suffix |
| `str_index` | `str_index(s: string, substr: string) -> int` | Return index of first occurrence of substr, or -1 |
| `str_last_index` | `str_last_index(s: string, substr: string) -> int` | Return index of last occurrence of substr, or -1 |
| `str_slice_eq` | `str_slice_eq(s: string, off: int, len: int, other: string) -> int` | Compare `s[off:off+len]` to `other` without allocating (1/0; OOB → 0) |
| `str_slice_ci_eq` | `str_slice_ci_eq(s, off, len, other) -> int` | Case-insensitive region equality (no alloc) |
| `str_slice_contains` | `str_slice_contains(s, off, len, needle) -> int` | Needle inside `s[off:off+len]` (no alloc) |
| `str_slice_index` | `str_slice_index(s, off, len, needle) -> int` | First absolute index of needle in region, or −1 |
| `str_at_eq` | `str_at_eq(s: string, off: int, other: string) -> int` | `s[off..]` prefix equals `other` (no alloc) |
| `str_byte_at` | `str_byte_at(s: string, i: int) -> int` | Byte at index (0–255), or −1 if OOB |
| `str_trim` | `str_trim(s: string, cutset: string) -> string` | Trim characters in cutset from both ends |
| `str_trim_space` | `str_trim_space(s: string) -> string` | Trim whitespace from both ends |
| `str_trim_left` | `str_trim_left(s: string, cutset: string) -> string` | Trim characters in cutset from the left |
| `str_trim_right` | `str_trim_right(s: string, cutset: string) -> string` | Trim characters in cutset from the right |
| `str_to_lower` | `str_to_lower(s: string) -> string` | Convert string to lowercase |
| `str_to_upper` | `str_to_upper(s: string) -> string` | Convert string to uppercase |
| `str_repeat` | `str_repeat(s: string, count: int) -> string` | Repeat a string count times |
| `str_replace` | `str_replace(s: string, old: string, new: string) -> string` | Replace all occurrences of old with new |
| `str_split` | `str_split(s: string, sep: string) -> []string` | Split string by separator into an array |
| `str_fields` | `str_fields(s: string) -> []string` | Split string on whitespace into an array |
| `str_join` | `str_join(parts: []string, sep: string) -> string` | Join string array with separator |
| `str_count` | `str_count(s: string, substr: string) -> int` | Count non-overlapping occurrences of substr |
| `str_cut` | `str_cut(s: string, sep: string) -> []string` | Cut string at first occurrence of sep, returning [before, after] |
| `rune_count` | `rune_count(s: string) -> int` | Count the number of Unicode code points |
| `str_builder` | `str_builder() -> StrBuilder` | Create a new growable string buffer |
| `builder_write` | `builder_write(sb: StrBuilder, s: string) -> void` | Append a string to the builder |
| `builder_write_byte` | `builder_write_byte(sb: StrBuilder, b: byte) -> void` | Append a single byte to the builder |
| `builder_string` | `builder_string(sb: StrBuilder) -> string` | Finalize and return the built string |
| `builder_len` | `builder_len(sb: StrBuilder) -> int` | Return current length of the builder |

---

## 3. Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(n: int) -> int` | Absolute value of an integer |
| `abs_f` | `abs_f(f: float) -> float` | Absolute value of a float |
| `min` | `min(a: int, b: int) -> int` | Return the smaller of two integers |
| `max` | `max(a: int, b: int) -> int` | Return the larger of two integers |
| `clamp` | `clamp(val: int, lo: int, hi: int) -> int` | Clamp an integer to [lo, hi] |
| `clamp_f` | `clamp_f(val: float, lo: float, hi: float) -> float` | Clamp a float to [lo, hi] |
| `sqrt` | `sqrt(f: float) -> float` | Square root |
| `sin` | `sin(f: float) -> float` | Sine (radians) |
| `cos` | `cos(f: float) -> float` | Cosine (radians) |
| `atan2` | `atan2(y: float, x: float) -> float` | Two-argument arctangent |
| `floor_f` | `floor_f(f: float) -> float` | Floor of a float |
| `ceil_f` | `ceil_f(f: float) -> float` | Ceiling of a float |
| `lerp` | `lerp(a: float, b: float, t: float) -> float` | Linear interpolation between a and b |
| `dist2d` | `dist2d(x1: float, y1: float, x2: float, y2: float) -> float` | Euclidean distance between two 2D points |
| `math_abs` | `math_abs(f: float) -> float` | Absolute value of a float |
| `math_sqrt` | `math_sqrt(f: float) -> float` | Square root |
| `math_pow` | `math_pow(base: float, exp: float) -> float` | Raise base to the power of exp |
| `math_floor` | `math_floor(f: float) -> float` | Floor of a float |
| `math_ceil` | `math_ceil(f: float) -> float` | Ceiling of a float |
| `math_sin` | `math_sin(f: float) -> float` | Sine (radians) |
| `math_cos` | `math_cos(f: float) -> float` | Cosine (radians) |
| `math_log` | `math_log(f: float) -> float` | Natural logarithm |
| `math_exp` | `math_exp(f: float) -> float` | Exponential (e^x) |
| `rand_seed` | `rand_seed(seed: int) -> void` | Seed the random number generator |
| `rand_intn` | `rand_intn(n: int) -> int` | Random integer in [0, n) |
| `rand_float` | `rand_float() -> float` | Random float in [0.0, 1.0) |
| `random_int` | `random_int(lo: int, hi: int) -> int` | Cryptographically random integer in [lo, hi] |
| `safe_add` | `safe_add(a: int, b: int) -> int` | Overflow-checked integer addition |

---

## 4. Parsing & Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `parse_int` | `parse_int(s: string) -> Result[int, string]` | Parse a string as an integer |
| `parse_float` | `parse_float(s: string) -> float` | Parse a string as a float |
| `parse_bool` | `parse_bool(s: string) -> Result[int, string]` | Parse a string as a boolean (1/0) |

---

## 5. Slices & Arrays

| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `len(arr: []int) -> int` | Return the length of an array |
| `cap` | `cap(arr: []int) -> int` | Return the capacity of an array |
| `append` | `append(arr: []int, val: int) -> []int` | Append a value to an array, returning the new array |
| `copy` | `copy(dst: []int, src: []int) -> int` | Copy elements from src to dst, return count copied |
| `ints_contains` | `ints_contains(arr: []int, val: int) -> bool` | Check if an int array contains a value |
| `strings_contains` | `strings_contains(arr: []string, val: string) -> bool` | Check if a string array contains a value |
| `ints_index` | `ints_index(arr: []int, val: int) -> int` | Return index of value in int array, or -1 |
| `ints_copy` | `ints_copy(arr: []int) -> []int` | Return a shallow copy of an int array |
| `sort_ints` | `sort_ints(arr: []int) -> []int` | Sort an integer array in ascending order |
| `sort_strings` | `sort_strings(arr: []string) -> []string` | Sort a string array in ascending order |
| `slices_reverse` | `slices_reverse(arr: []int) -> []int` | Reverse an integer array |
| `slices_unique` | `slices_unique(arr: []int) -> []int` | Remove duplicates from an integer array |
| `slice_ints` | `slice_ints(arr: []int, start: int, end: int) -> Slice` | Create a borrowed slice view of an int array |
| `slice_len` | `slice_len(s: Slice) -> int` | Return the length of a slice |
| `slice_get` | `slice_get(s: Slice, idx: int) -> int` | Get an element from a slice by index |
| `unsafe_index` | `unsafe_index(arr: []int, idx: int) -> int` | Unchecked array index (no bounds check) |

---

## 6. Maps

| Function | Signature | Description |
|----------|-----------|-------------|
| `maps_keys` | `maps_keys(m: map[K]V) -> []K` | Keys as a slice (`[]int` / `[]string` / `[]float` / `[]bool` / `[]Struct` / `[]Enum`) |
| `maps_values` | `maps_values(m: map[K]V) -> []V` | Values as a slice (incl. `[][]T` for slice-valued maps, `[]map[…]` for nested maps) |
| `maps_clear` | `maps_clear(m: map[K]V) -> void` | Remove all entries |
| `maps_clone` | `maps_clone(m: map[K]V) -> map[K]V` | Shallow copy (nested maps / channel maps: outer entries, inner pointers) |
| `maps_equal` | `maps_equal(a: map[K]V, b: map[K]V) -> int` | Same keys/values (structs/enums structural; nested maps & channel maps: pointer identity on inners) |
| `maps_copy` | `maps_copy(dst: map[K]V, src: map[K]V) -> void` | Copy entries into `dst` |

Supported map kinds — **keys:** `int` \| `string` \| `float` \| `bool` \| named
**Struct** \| **Enum** (incl. pack-qualified types). **Values:** the same set,
**slices** `[]T` / `[][]T` (int/string/float/bool/byte/Struct/Enum),
**nested maps** `map[K2]V` / `map[K2]map[K3]V` (depth ≤3), **bags** `Option[T]` /
`Result[T,E]`, **tuples** `(T,U[,…])`, or **channels** `chan[T]`
(int/bool/float/string/struct elements, same as `chan_open`). Any combination, e.g.:

| Example | Role |
|---------|------|
| `map[string]int` / `map[int]int` / `map[string]string` | Core SI / II / SS |
| `map[int]float` / `map[float]int` / `map[float]float` | Float values / keys |
| `map[string]bool` / `map[bool]int` | Set-style / bool keys |
| `map[int]Point` / `map[Point]int` / `map[Point]Label` | Struct values / keys |
| `map[Color]int` / `map[int]Color` / `map[Color]Point` | Enum values / keys |
| `map[string][]int` / `map[Point][]string` | Slice values |
| `map[string][][]int` / `map[Point][][]int` | Nested-slice values |
| `map[string]map[string]int` / `map[Point]map[int]int` | Nested maps (depth 2) |
| `map[string]map[string][]int` / `map[Point]map[string][]int` | Nested maps + slice values |
| `[]map[string]int` / `map[string][]map[string]int` | Slice of maps / map of those |
| `map[string]Option[int]` / `map[Point]Option[int]` | Option bag values |
| `map[int]Result[string,string]` / `map[string]Result[Point,string]` | Result bag values |
| `map[string](int,int)` / `map[Point](string,int)` | Tuple values |
| `map[string]chan[int]` / `map[Point]chan[string]` / `map[string]chan[Point]` | Channel values |
| `map[string][]chan[int]` / `map[Point][]chan[string]` | Slices of channels |
| `map[string]Option[chan[int]]` / `Option[chan[int]]` | Optional channels |
| `map[int]Result[chan[string],string]` | Fallible channel handles |
| `map[string][]Option[chan[int]]` | Slice of optional channels |
| `map[string]Option[[]chan[int]]` | Optional slice of channels |
| `map[string][][]chan[int]` | Nested channel slices |
| `map[string](chan[int], int)` | Channel + scalar tuple values |
| `map[string](chan[int], int, int)` | Channel + two scalars (any slot) |
| `map[string]Option[Option[int]]` | Nested optional map values |
| `map[string]Option[Option[chan[int]]]` | Nested optional channels |
| `map[int]Result[Option[chan[string]],string]` | Result of optional channel |
| `map[string]Option[Result[int,string]]` | Optional fallible map values |
| `map[string]Option[Result[chan[int],string]]` | Optional fallible channels |
| `map[string]Option[Option[Option[int]]]` | Triple nested optional |
| `map[string]Result[Option[Option[int]],string]` | Result of double optional |
| `map[string]Result[Result[int,string],string]` | Nested Result map values |
| `map[string][]Option[Option[int]]` | Slice of nested optional values |
| `map[string][]Option[Result[int,string]]` | Slice of optional Results |
| `map[string]Option[[]Option[int]]` | Optional slice of Options |
| `map[string]Result[[]Result[int,string],string]` | Fallible slice of Results |
| `map[string](Option[int], int)` | Optional + scalar tuple values |
| `map[int](int, Result[string,string])` | Scalar + Result tuple values |
| `map[string](Option[chan[int]], int)` | Optional channel + scalar tuples |

Float keys: `+0`/`-0` unify; all NaNs share one key. Struct keys: field-wise eq
+ stable field hash (strings by content). Enum keys: tag + payload.
Missing key → zero value (`None` / `Err("")` for bags; **nil channel** for
`chan[T]` values); `len` on a nil map is `0`.

**Monomorphs:** C helpers for monomorphized maps are emitted **only for map
shapes used in the unit** (demand-driven). The language surface remains the
full grid above; unused pairs do not inflate object size.

Tests: `map_test`, `map_struct_test`, `map_float_test`, `map_struct_key_test`,
`map_bool_test`, `map_enum_test`, `map_slice_test`, `map_nested_test`,
`map_depth3_test`, `map_nested_slice_test`, `map_map_slice_test`, `slice_map_test`,
`map_option_result_test`, `map_option_result_nested_test`, `map_nested_bag_slice_test`,
`map_tuple_test`, `map_tuple_bag_test`, `map_chan_test`, `nested_slice_test`.  
Also **`[]Option[T]`** / **`[]Result[T,E]`** (make/append/index/range/lits).  
Hands-on: [howto/10-collections.md](howto/10-collections.md).

---

## 7. File I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `read_file(path: string) -> string` | Read entire file contents as a string |
| `write_file` | `write_file(path: string, data: string) -> int` | Write string data to a file (overwrite) |
| `append_file` | `append_file(path: string, data: string) -> int` | Append string data to a file |
| `atomic_write_file` | `atomic_write_file(path: string, data: string) -> int` | Crash-safe write (temp + fsync + rename) |
| `remove_file` | `remove_file(path: string) -> int` | Delete a file |
| `remove_all` | `remove_all(path: string) -> int` | Recursively delete file or directory tree |
| `file_exists` | `file_exists(path: string) -> bool` | Check if a file exists |
| `is_dir` | `is_dir(path: string) -> bool` | Check if a path is a directory |
| `is_file` | `is_file(path: string) -> int` | `1` if path is a regular file |
| `path_size` | `path_size(path: string) -> int` | File size by path (`-1` if missing) |
| `file_mtime` | `file_mtime(path: string) -> int` | mtime as Unix seconds (`-1` if missing) |
| `chmod` | `chmod(path: string, mode: int) -> int` | Set mode bits (e.g. `420` = 0644) |
| `read_dir` | `read_dir(path: string) -> []string` | List entries in a directory |
| `mkdir` | `mkdir(path: string) -> int` | Create a directory |
| `mkdir_all` | `mkdir_all(path: string) -> int` | Create directory and parents (`mkdir -p`) |
| `rmdir` | `rmdir(path: string) -> int` | Remove empty directory |
| `rename` | `rename(old: string, new: string) -> int` | Rename/move within same filesystem |
| `copy_file` | `copy_file(src: string, dst: string) -> int` | Copy file contents (overwrite dst) |
| `temp_dir` | `temp_dir() -> string` | System temp directory (`TMPDIR` / `/tmp`) |
| `temp_file` | `temp_file(prefix: string) -> string` | Create unique empty temp file; return path |
| `symlink` | `symlink(target: string, link: string) -> int` | Create symbolic link |
| `readlink` | `readlink(path: string) -> string` | Read symlink target |
| `realpath` | `realpath(path: string) -> string` | Resolve absolute path |
| `getcwd` | `getcwd() -> string` | Return the current working directory |
| `chdir` | `chdir(path: string) -> int` | Change the current working directory |
| `path_join` | `path_join(a: string, b: string) -> string` | Join two path segments |
| `path_base` | `path_base(path: string) -> string` | Return the last element of a path |
| `path_dir` | `path_dir(path: string) -> string` | Return the directory portion of a path |
| `path_ext` | `path_ext(path: string) -> string` | Return the file extension |
| `path_clean` | `path_clean(path: string) -> string` | Clean and normalize a path |
| `path_is_abs` | `path_is_abs(path: string) -> bool` | Check if a path is absolute |
| `filepath_walk` | `filepath_walk(root: string) -> []string` | Recursively list all files under root |
| `filepath_walk_n` | `filepath_walk_n(root: string, max: int) -> []string` | Recursively list up to max files under root |
| `embed_file` | `embed_file(path: string) -> string` | Embed file contents at compile time |

Paths reject **embedded NUL** bytes. Prefer `atomic_write_file` for durable
config/log updates. Tests: `examples/testing/fs_storage_test.mko`.

---

## 8. Direct I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_open` | `file_open(path: string, mode: int, flags: int) -> int` | Open fd; always `O_CLOEXEC` when available |
| `file_close` | `file_close(fd: int) -> int` | Close a file descriptor |
| `pread` | `pread(fd: int, count: int, offset: int) -> string` | Read count bytes at offset without seeking |
| `pwrite` | `pwrite(fd: int, data: string, offset: int) -> int` | Write data at offset without seeking |
| `file_append` | `file_append(fd: int, data: string) -> int` | Append data to a file descriptor |
| `fsync` | `fsync(fd: int) -> int` | Flush file data and metadata to disk |
| `fdatasync` | `fdatasync(fd: int) -> int` | Flush file data (not metadata) to disk |
| `fallocate` | `fallocate(fd: int, size: int) -> int` | Pre-allocate disk space for a file |
| `file_size` | `file_size(fd: int) -> int` | Return the size of an open file |
| `path_file_size` | `path_file_size(path: string) -> int` | `stat` path size (−1 if missing) |
| `file_truncate` | `file_truncate(fd: int, size: int) -> int` | Truncate or extend a file to given size |
| `file_seek` | `file_seek(fd: int, offset: int, whence: int) -> int` | Seek to a position in a file |
| `file_read_exact` | `file_read_exact(fd: int, count: int) -> string` | Read exactly count bytes from current position |

`file_open` **mode**: `0`=RO, `1`=WO, `2`=RW.  
**flags** bits: `1`=create, `2`=truncate, `4`=append, `8`=dsync, `16`=direct,
`32`=exclusive create (`O_EXCL`).

---

## 9. Memory-Mapped Files

| Function | Signature | Description |
|----------|-----------|-------------|
| `mmap_open` | `mmap_open(path: string, size: int) -> MMap` | Open an existing file as a memory-mapped region |
| `mmap_create` | `mmap_create(path: string, size: int) -> MMap` | Create a new memory-mapped file |
| `mmap_read` | `mmap_read(m: MMap, offset: int, len: int) -> string` | Read bytes from a mapped region |
| `mmap_write` | `mmap_write(m: MMap, offset: int, data: string) -> int` | Write bytes to a mapped region |
| `mmap_sync` | `mmap_sync(m: MMap, flags: int) -> int` | Flush mapped pages to disk |
| `mmap_size` | `mmap_size(m: MMap) -> int` | Return the size of the mapped region |
| `mmap_close` | `mmap_close(m: MMap) -> int` | Unmap and close the memory-mapped file |

### Storage seeds (`mako_dio.h` + `mako_domain.h`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `page_alloc` | `page_alloc(size: int) -> Page` | Zeroed page (default 4096) |
| `page_read` / `page_write` / `page_free` | … | Page I/O |
| `wal_open` | `wal_open(path: string) -> Wal` | File-backed WAL |
| `wal_append` / `wal_sync` / `wal_read_at` / `wal_next_off` / `wal_close` | … | Length-prefixed records |
| `hindex_new` / `hindex_put` / `hindex_get` / `hindex_del` / `hindex_free` | … | Int→int open-addressing index |
| `store_new` / `store_put` / `store_get` / `store_begin` / `store_commit` / `store_rollback` | … | Txn KV; optional `store_attach_wal` |
| `store_recover_wal` | `store_recover_wal(s, w) -> int` | Replay WAL `P`/`D` records into store (crash recovery seed) |
| `btree_new` / `btree_put` / `btree_get` / `btree_save` / `btree_load` / `btree_free` | … | In-memory B-tree + disk snapshot |
| `btree_range` | `btree_range(t, lo, hi) -> int` | Inclusive ordered range → TLS buffer; count |
| `pbtree_new` / `pbtree_put` / `pbtree_get` / `pbtree_len` / `pbtree_pages` / `pbtree_free` | … | Page-backed B-tree (nodes in `MakoPage`) |
| `sst_build4` / `sst_get` / `sst_len` / `sst_free` | … | Sorted run + binary search |
| `sst_range` | `sst_range(s, lo, hi) -> int` | Inclusive SST range → TLS buffer |
| `range_len` / `range_key_at` / `range_val_at` | … | Read last range result (cap 128) |
| `bloom_new` / `bloom_add` / `bloom_maybe` / `bloom_len` / `bloom_free` | … | Bloom filter (int64 keys; no false negatives) |
| `pman_open` / `pman_alloc` / `pman_set` / `pman_get` / `pman_sync` / `pman_close` | … | Disk page manager (4 KiB file-backed) |
| `pman_pages` / `pman_reads` / `pman_writes` | … | Page count + I/O counters |
| `pcache_new` / `pcache_get` / `pcache_hits` / `pcache_misses` / `pcache_free` | … | 16-slot LRU page cache |
| `mvcc_new` / `mvcc_begin` / `mvcc_put` / `mvcc_get` / `mvcc_gc` / `mvcc_live` / `mvcc_free` | … | Multi-version KV + GC |
| `lsm_new` / `lsm_put` / `lsm_get` / `lsm_flush` / `lsm_attach_run` / `lsm_free` | … | Memtable + L0 run WAL |
| `lsm_compact` / `lsm_compact_down` / `lsm_compactions` / `lsm_flushes` | … | L0→L1 compact; promote/merge L1→L2→L3 |
| `lsm_sst_levels` / `lsm_level_len` | `lsm_level_len(l, level) -> int` | Non-empty SST count; key count at level 1–3 |
| `file_mtime_ns` | `file_mtime_ns(path) -> int` | Nanosecond mtime (`-1` if missing) |
| `hot_reload_watch` / `hot_reload_changed` | `hot_reload_watch(path) -> int` | Mtime watch slots; `changed` returns 1 when updated |
| `snap_encode2` / `snap_encode4` / `snap_get` / `snap_count` / `snap_predict` / `snap_reconcile` | … | Multiplayer snapshot seed |
| `rollback_new` / `rollback_push` / `rollback_get` / `rollback_restore_slot0` / `rollback_free` | … | Frame rollback ring |
| `simd_dot_i64_4` / `simd_sum_i64_4` | … | Portable 4-wide seed |

Tests: `storage_wal_test`, `store_index_test`, `storage_depth_test`, `domain_tracks_test`.  
**Not in scope:** SIPREC / WebRTC.

---

## 10. Environment

| Function | Signature | Description |
|----------|-----------|-------------|
| `env_get` | `env_get(key: string) -> string` | Get an environment variable value |
| `env_set` | `env_set(key: string, val: string) -> int` | Set an environment variable |
| `env_get_or` | `env_get_or(key: string, fallback: string) -> string` | Get an environment variable or return fallback |
| `env_has` | `env_has(key: string) -> int` | Check if an environment variable is set |
| `argc` | `argc() -> int` | Return the number of command-line arguments |
| `args` | `args() -> []string` | Return all command-line arguments |
| `arg_get` | `arg_get(idx: int) -> string` | Get a command-line argument by index |
| `flag_string` | `flag_string(name: string, default: string) -> string` | Parse a string flag from command-line arguments |
| `flag_int` | `flag_int(name: string, default: int) -> int` | Parse an integer flag from command-line arguments |
| `flag_bool` | `flag_bool(name: string, default: int) -> int` | Parse a boolean flag from command-line arguments |

---

## 11. Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `now_ns` | `now_ns() -> int` | **Monotonic** nanoseconds (alias of `mono_ns`; for latency) |
| `now_ms` | `now_ms() -> int` | **Wall** milliseconds (alias of `wall_ms`; for logs/calendar) |
| `wall_ns` / `wall_us` / `wall_ms` | `() -> int` | Wall-clock (`CLOCK_REALTIME`); can jump with NTP |
| `mono_ns` / `mono_us` / `mono_ms` | `() -> int` | Monotonic (`CLOCK_MONOTONIC_RAW` when available); never goes backwards |
| `mono_res_ns` | `mono_res_ns() -> int` | Monotonic clock resolution in ns |
| `mono_overhead_ns` | `mono_overhead_ns() -> int` | Cost of two `mono_ns` samples (calibration) |
| `elapsed_ns` / `elapsed_us` | `(start) -> int` | Elapsed on **mono** domain (pass a mono tick) |
| `elapsed_mono_ms` | `elapsed_mono_ms(start) -> int` | Monotonic ms elapsed |
| `elapsed_ms` | `elapsed_ms(start) -> int` | Wall ms elapsed (legacy; prefer `elapsed_mono_ms`) |
| `deadline_ns` / `deadline_ms` | `(timeout) -> int` | Monotonic deadline = now + timeout |
| `deadline_remaining_ns` | `(deadline) -> int` | Ns left until deadline (0 if expired) |
| `deadline_remaining_ms` | `(deadline) -> int` | Ms left until deadline (0 if expired) |
| `deadline_expired` | `(deadline) -> int` | `1` if mono now ≥ deadline |
| `sleep_ns` / `sleep_us` / `sleep_ms` | `(n) -> void` | High-res sleep (`nanosleep`; short sleeps may oversleep) |
| `sleep_until_ns` | `(deadline) -> void` | Hybrid sleep + final spin to mono deadline |
| `spin_until_ns` | `(deadline) -> void` | Busy-wait to mono deadline (lowest latency, burns CPU) |
| `time_unix` | `time_unix() -> int` | Wall Unix timestamp (seconds) |
| `time_format` | `time_format(unix_ms: int) -> string` | RFC3339 UTC from wall ms |
| `time_sleep_ms` | `time_sleep_ms(ms: int) -> void` | Alias of `sleep_ms` |

**Low-latency rule:** measure and budget with **`mono_*` / `elapsed_ns` / `deadline_*`**.  
Use **`wall_*` / `now_ms`** only for logs and absolute calendar time.

---

## 12. HTTP Server

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_bind` | `http_bind(port: int) -> int` | Bind an HTTP listener on a port |
| `http_accept` | `http_accept(listener: int) -> int` | Accept an incoming HTTP connection |
| `http_method` | `http_method(conn: int) -> string` | Get the HTTP method of a request |
| `http_path` | `http_path(conn: int) -> string` | Get the URL path of a request |
| `http_body` | `http_body(conn: int) -> string` | Get the body of a request |
| `http_header` | `http_header(conn: int, name: string) -> string` | Get a request header value by name |
| `http_respond` | `http_respond(conn: int, status: int, body: string) -> int` | Send an HTTP response with status and body |
| `http_respond_json` | `http_respond_json(conn: int, status: int, json: string) -> int` | Send a JSON HTTP response |
| `http_respond_ct` | `http_respond_ct(conn: int, status: int, body: string, content_type: string) -> int` | Send a response with a custom content type |
| `http_close` | `http_close(conn: int) -> int` | Close an HTTP connection |
| `http_close_listener` | `http_close_listener(listener: int) -> int` | Close an HTTP listener |
| `http_next` | `http_next(conn: int) -> int` | Advance to the next request on a keep-alive connection |
| `http_keepalive` | `http_keepalive(conn: int) -> int` | Check if the connection supports keep-alive |
| `http_serve` | `http_serve(port: int, handler: string) -> int` | Start a simple HTTP server |
| `http_echo` | `http_echo(port: int) -> int` | Start an echo HTTP server |
| `http_listen` | `http_listen(port: int, handler: string) -> int` | Listen and serve HTTP |
| `http_header_ok` | `http_header_ok(name: string, value: string) -> int` | Validate an HTTP header name/value pair |
| `http_content_encoding` | `http_content_encoding(accept: string) -> string` | Determine content encoding from Accept-Encoding |
| `http_compress_if_accepted` | `http_compress_if_accepted(accept: string, body: string) -> string` | Compress body if client accepts compression |
| `http_health_json` | `http_health_json(name: string, status: int) -> string` | Generate a JSON health check response |
| `http_respond_health` | `http_respond_health(conn: int, name: string, status: int) -> int` | Respond with a health check status |
| `http_active_connections` | `http_active_connections() -> int` | Return the number of active HTTP connections |

### HTTP Shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_shutdown_begin` | `http_shutdown_begin(timeout: int) -> int` | Begin graceful shutdown with timeout |
| `http_shutdown_reset` | `http_shutdown_reset() -> int` | Reset shutdown state |
| `http_shutdown_requested` | `http_shutdown_requested() -> int` | Check if shutdown has been requested |
| `http_shutdown_ready` | `http_shutdown_ready() -> int` | Check if shutdown is ready (all connections drained) |
| `http_shutdown_deadline` | `http_shutdown_deadline() -> int` | Get the shutdown deadline timestamp |
| `http_shutdown_remaining` | `http_shutdown_remaining() -> int` | Milliseconds remaining until shutdown deadline |
| `http_shutdown_expired` | `http_shutdown_expired() -> int` | Check if shutdown deadline has passed |
| `http_shutdown_drain_conn` | `http_shutdown_drain_conn(conn: int) -> int` | Drain a connection during shutdown |
| `http_shutdown_from_signal` | `http_shutdown_from_signal(listener: int, timeout: int) -> int` | Begin shutdown from a signal |

---

## 13. HTTP Client

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_get` | `http_get(url: string) -> string` | Perform an HTTP GET request |
| `http_post` | `http_post(url: string, body: string) -> string` | Perform an HTTP POST request |
| `http_request` | `http_request(method: string, url: string, body: string, timeout: int) -> string` | Perform a custom HTTP request |
| `http_get_timeout` | `http_get_timeout(url: string, timeout_ms: int) -> string` | HTTP GET with timeout |
| `http_post_timeout` | `http_post_timeout(url: string, body: string, timeout_ms: int) -> string` | HTTP POST with timeout |
| `http_last_status` | `http_last_status() -> int` | Get the status code of the last HTTP response |
| `http_last_header` | `http_last_header(name: string) -> string` | Get a header from the last HTTP response |

### HTTP Request Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_request_parse` | `http_request_parse(raw: string) -> HttpRequest` | Parse a raw HTTP request string |
| `http_request_from_conn` | `http_request_from_conn(conn: int) -> HttpRequest` | Parse an HTTP request from a connection fd |
| `http_request_method` | `http_request_method(req: HttpRequest) -> string` | Get the method from a parsed request |
| `http_request_path` | `http_request_path(req: HttpRequest) -> string` | Get the path from a parsed request |
| `http_request_body` | `http_request_body(req: HttpRequest) -> string` | Get the body from a parsed request |
| `http_route_match` | `http_route_match(req: HttpRequest, method: string, pattern: string) -> bool` | Match a request against a method and route pattern |
| `http_route_param` | `http_route_param(req: HttpRequest, pattern: string, name: string) -> string` | Extract a route parameter from a matched pattern |
| `http_req_method` | `http_req_method(conn: int) -> string` | Get request method from connection |
| `http_req_path` | `http_req_path(conn: int) -> string` | Get request path from connection |
| `http_req_body` | `http_req_body(conn: int) -> string` | Get request body from connection |

---

## 14. JSON

| Function | Signature | Description |
|----------|-----------|-------------|
| `json_object` | `json_object(key: string, value: string) -> string` | Create a JSON object with one key-value pair |
| `json_get_string` | `json_get_string(json: string, key: string) -> string` | Extract a string value by key from JSON |
| `json_get_int` | `json_get_int(json: string, key: string) -> int` | Extract an integer value by key from JSON |
| `json_get_object` | `json_get_object(json: string, key: string) -> string` | Extract a nested JSON object by key |
| `json_has` | `json_has(json: string, key: string, value: string) -> int` | Check if JSON has a key with a given value |
| `json_nest` | `json_nest(key: string, inner: string) -> string` | Nest a JSON value under a key |
| `json_merge` | `json_merge(a: string, b: string) -> string` | Merge two JSON objects |
| `json_path_string` | `json_path_string(json: string, path: string, key: string) -> string` | Extract a string via dot-path navigation |
| `json_path_int` | `json_path_int(json: string, path: string, key: string) -> int` | Extract an integer via dot-path navigation |
| `json_ss` | `json_ss(k1: string, v1: string, k2: string, v2: string) -> string` | Create a JSON object with two string key-value pairs |
| `json_si` | `json_si(k1: string, v1: string, k2: string, v2: int) -> string` | Create a JSON object with a string and an int key-value pair |
| `json_i` | `json_i(key: string, value: int) -> string` | Create a JSON object with one int key-value pair |
| `json_object_from_map_ss` | `json_object_from_map_ss(m: map[string]string) -> string` | Convert a map[string]string to a JSON object |
| `json_array_len` | `json_array_len(json: string) -> int` | Return the length of a JSON array |
| `json_array_get_int` | `json_array_get_int(json: string, idx: int) -> int` | Get an integer element from a JSON array |
| `json_array_get_string` | `json_array_get_string(json: string, idx: int) -> string` | Get a string element from a JSON array |
| `json_array_push_string` | `json_array_push_string(json: string, val: string) -> string` | Push a string onto a JSON array |
| `json_array_push_int` | `json_array_push_int(json: string, val: int) -> string` | Push an integer onto a JSON array |
| `json_array_ints3` | `json_array_ints3(a: int, b: int, c: int) -> string` | Create a JSON array from three integers |
| `json_array_strings2` | `json_array_strings2(a: string, b: string) -> string` | Create a JSON array from two strings |

---

## 15. Database

### SQLite (Legacy)

| Function | Signature | Description |
|----------|-----------|-------------|
| `sqlite_query_int` | `sqlite_query_int(db: string, sql: string) -> int` | Query an integer from SQLite |
| `sqlite_query_text` | `sqlite_query_text(db: string, sql: string) -> string` | Query a text value from SQLite |
| `sqlite_query_int_params` | `sqlite_query_int_params(db: string, sql: string, params: []int) -> int` | Query an integer from SQLite with params |

### PostgreSQL (Legacy)

| Function | Signature | Description |
|----------|-----------|-------------|
| `pg_connect` | `pg_connect(connstr: string) -> PgConn` | Connect to a PostgreSQL database |
| `pg_ok` | `pg_ok(conn: PgConn) -> int` | Check if a Postgres connection is valid |
| `pg_exec` | `pg_exec(conn: PgConn, sql: string) -> int` | Execute SQL on a Postgres connection |
| `pg_exec_row_count` | `pg_exec_row_count(conn: PgConn, sql: string) -> int` | Execute SQL and return affected row count |
| `pg_close` | `pg_close(conn: PgConn) -> int` | Close a Postgres connection |
| `pg_connect_url` | `pg_connect_url(url: string) -> string` | Parse a Postgres connection URL |

### Unified SQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `sql_open_sqlite` | `sql_open_sqlite(path: string) -> SqlDB` | Open a SQLite database |
| `sql_open_postgres` | `sql_open_postgres(connstr: string) -> SqlDB` | Open a PostgreSQL database |
| `sql_ok` | `sql_ok(db: SqlDB) -> int` | Check if a database connection is valid |
| `sql_close` | `sql_close(db: SqlDB) -> int` | Close a database connection |
| `sql_exec` | `sql_exec(db: SqlDB, sql: string, params: []int) -> int` | Execute SQL with integer params |
| `sql_exec_plain` | `sql_exec_plain(db: SqlDB, sql: string) -> int` | Execute SQL without params |
| `sql_exec_str4` | `sql_exec_str4(db, sql, a, b, c, d) -> int` | Up to 4 string binds; **arity from SQL `$N`/`?`**; `""` is a real value (not “skip”) |
| `sql_query_int` | `sql_query_int(db: SqlDB, sql: string, params: []int) -> int` | Query a single integer result |
| `sql_query_str` | `sql_query_str(db, sql, p1) -> string` | First column of first row; 0–1 string bind |
| `sql_query_str2` / `str3` / `str4` | multi-arg variants | Same bind-arity rules as `sql_exec_str4` |
| `sql_last_insert_id` | `sql_last_insert_id(db: SqlDB) -> int` | Last INSERT row id (SQLite `last_insert_rowid`; Postgres `lastval`) |
| `sql_rows_affected` | `sql_rows_affected(db: SqlDB) -> int` | Rows changed by last INSERT/UPDATE/DELETE on this connection |
| `sql_query_rows` | `sql_query_rows(db: SqlDB, sql: string, params: []int) -> int` | Open multi-row result set (handle; 0 = fail) |
| `sql_query_rows_str` | `sql_query_rows_str(db: SqlDB, sql: string, p1: string) -> int` | Multi-row result with 0–1 string param |
| `sql_rows_ok` | `sql_rows_ok(rows: int) -> int` | 1 if handle is live and not in error |
| `sql_rows_next` | `sql_rows_next(rows: int) -> int` | Advance cursor: 1 = row, 0 = done, -1 = error |
| `sql_rows_int` | `sql_rows_int(rows: int, col: int) -> int` | Read column as int (current row) |
| `sql_rows_str` | `sql_rows_str(rows: int, col: int) -> string` | Read column as string (current row) |
| `sql_rows_cols` | `sql_rows_cols(rows: int) -> int` | Column count |
| `sql_rows_close` | `sql_rows_close(rows: int) -> int` | Free result set (1 if closed) |
| `sql_query_col_int` | `sql_query_col_int(db: SqlDB, sql: string, max: int) -> []int` | Bulk first column as ints (cap `max`, max 10000) |
| `sql_query_col_str` | `sql_query_col_str(db: SqlDB, sql: string, max: int) -> []string` | Bulk first column as strings (cap `max`, max 10000) |
| `sql_begin` | `sql_begin(db: SqlDB) -> int` | Begin a transaction |
| `sql_commit` | `sql_commit(db: SqlDB) -> int` | Commit a transaction |
| `sql_rollback` | `sql_rollback(db: SqlDB) -> int` | Roll back a transaction |
| `sql_prepare` | `sql_prepare(db: SqlDB, sql: string) -> int` | Prepare a SQL statement |
| `sql_stmt_query_int` | `sql_stmt_query_int(stmt: int, params: []int) -> int` | Query integer from a prepared statement |
| `sql_stmt_exec` | `sql_stmt_exec(stmt: int, params: []int) -> int` | Execute a prepared statement |
| `sql_stmt_close` | `sql_stmt_close(stmt: int) -> int` | Close a prepared statement |
| `sql_migration_applied` | `sql_migration_applied(db: SqlDB, version: int) -> int` | Check if a migration has been applied |
| `sql_migrate` | `sql_migrate(db: SqlDB, version: int, sql: string) -> int` | Apply a migration |
| `sql_check_typed` | `sql_check_typed(table: string, col: string, type_name: string, constraint: string) -> int` | Validate column type constraints |
| `sql_query` | `sql_query(sql: string) -> string` | Execute a raw SQL query string |

### SQL Connection Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `sql_pool_open_sqlite` | `sql_pool_open_sqlite(path: string, size: int) -> int` | Open a SQLite connection pool |
| `sql_pool_open_postgres` | `sql_pool_open_postgres(connstr: string, size: int) -> int` | Open a PostgreSQL connection pool |
| `sql_pool_ok` | `sql_pool_ok(pool: int) -> int` | Check if a pool is valid |
| `sql_pool_size` | `sql_pool_size(pool: int) -> int` | Return the pool size |
| `sql_pool_opened` | `sql_pool_opened(pool: int) -> int` | Return number of opened connections |
| `sql_pool_next_slot` | `sql_pool_next_slot(pool: int) -> int` | Get the next available pool slot |
| `sql_pool_query_int` | `sql_pool_query_int(pool: int, sql: string, params: []int) -> int` | Query integer via pool |
| `sql_pool_exec` | `sql_pool_exec(pool: int, sql: string, params: []int) -> int` | Execute SQL via pool |
| `sql_pool_close` | `sql_pool_close(pool: int) -> int` | Close the connection pool |

### MySQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `mysql_connect` | `mysql_connect(connstr: string) -> MysqlConn` | Connect to a MySQL database |
| `mysql_connect_url` | `mysql_connect_url(url: string) -> string` | Parse a MySQL connection URL |
| `mysql_ok` | `mysql_ok(conn: MysqlConn) -> int` | Check if a MySQL connection is valid |
| `mysql_close` | `mysql_close(conn: MysqlConn) -> int` | Close a MySQL connection |
| `mysql_is_mariadb` | `mysql_is_mariadb(version: string) -> int` | Check if the server is MariaDB |
| `mysql_driver_name` | `mysql_driver_name(version: string) -> string` | Return the driver name for a version string |

---

## 16. Redis

| Function | Signature | Description |
|----------|-----------|-------------|
| `redis_connect` | `redis_connect(addr: string) -> RedisConn` | Connect to a Redis server |
| `redis_connect_url` | `redis_connect_url(url: string) -> string` | Parse a Redis connection URL |
| `redis_ok` | `redis_ok(conn: RedisConn) -> int` | Check if a Redis connection is valid |
| `redis_close` | `redis_close(conn: RedisConn) -> int` | Close a Redis connection |
| `redis_conn_ping` | `redis_conn_ping(conn: RedisConn) -> string` | Ping the Redis server |
| `redis_conn_set` | `redis_conn_set(conn: RedisConn, key: string, val: string) -> string` | Set a key-value pair |
| `redis_conn_get` | `redis_conn_get(conn: RedisConn, key: string) -> string` | Get a value by key |
| `redis_conn_del` | `redis_conn_del(conn: RedisConn, key: string) -> string` | Delete a key |
| `redis_conn_exists` | `redis_conn_exists(conn: RedisConn, key: string) -> string` | Check if a key exists |
| `redis_ping` | `redis_ping(addr: string, port: int) -> string` | Ping a Redis server by address and port |
| `redis_set` | `redis_set(addr: string, port: int, key: string, val: string) -> string` | Set a value on a Redis server |
| `redis_get` | `redis_get(addr: string, port: int, key: string) -> string` | Get a value from a Redis server |
| `redis_del` | `redis_del(addr: string, port: int, key: string) -> string` | Delete a key from a Redis server |
| `redis_exists` | `redis_exists(addr: string, port: int, key: string) -> string` | Check if a key exists on a Redis server |
| `redis_mock_once` | `redis_mock_once(port: int) -> int` | Start a single-request Redis mock |
| `redis_mock_kv` | `redis_mock_kv(port: int, count: int) -> int` | Start a Redis mock with key-value support |

### SIP proxy library (built-in)

**Platform SIP library for proxies** (UAs/registrars too). Runtime
`runtime/mako_sip.h`; pack **`std/sip`**. Not a softswitch — the first-class
proxy data-path API. RFCs 3261, 3581, Digest MD5. You own timers,
dialogs, routing, media (rtpengine). **Out of scope:** SIPREC, WebRTC, full B2BUA.

| Function | Signature | Description |
|----------|-----------|-------------|
| `sip_is_request` / `sip_is_response` / `sip_ok` | `(msg) -> int` | Classify message |
| `sip_method` / `sip_request_uri` / `sip_version` | `(msg) -> string` | Request start-line |
| `sip_status_code` / `sip_reason` | `(msg) -> int/string` | Response start-line |
| `sip_header` / `sip_header_n` / `sip_header_count` | `(msg, name[, n])` | Case-insensitive + compact forms; **owned** (malloc) |
| `sip_header_view` / `sip_body_view` / `sip_method_view` | → int | Zero-copy: set TLS view (1=found); use `sip_view_*` |
| `sip_view_len` / `sip_view_offset` / `sip_view_eq` / `sip_view_ci_eq` / `sip_view_contains` / `sip_view_copy` | | Inspect last view; `copy` allocates only when needed |
| `sip_header_eq` / `sip_header_ci_eq` / `sip_header_contains` / `sip_method_eq` | one-shot | Zero-copy compares (no TLS, no malloc) — prefer on hot path |
| `sip_body` / `sip_content_length` | `(msg)` | Body and CL |
| `sip_first_message_len` / `sip_msg_complete` / `sip_msg_needed` | `(buf) -> int` | TCP/TLS framing (first complete msg length / complete? / bytes needed) |
| `sip_request` / `sip_response` | build full message (auto Content-Length) | |
| `sip_headers_append` / `sip_header_line` / `sip_prepend_header` | header blob builders | |
| `sip_via_value` / `sip_via_value_rport` / `sip_via_value_nat` | Build Via; UAC bare `;rport`; full NAT params | |
| `sip_via_fix_source` / `sip_via_add_received` | Ingress fix (RFC 3261 received + RFC 3581 rport=src) | |
| `sip_via_has_rport` / `sip_via_rport` / `sip_via_received` / `sip_via_maddr` | Inspect NAT params (`rport`: -1 absent, 0 bare, >0 valued) | |
| `sip_via_response_host` / `sip_via_response_port` / `sip_via_response_addr` | Where to send response (maddr > received > sent-by; rport > sent-by port > 5060/5061) | |
| `sip_msg_fix_top_via` / `sip_msg_response_host` / `sip_msg_response_port` | Full-message top-Via fix + response next-hop | |
| `sip_via_host` / `sip_via_port` / `sip_via_transport` / `sip_via_branch` | sent-by / transport / branch | |
| `sip_insert_via` / `sip_strip_via` / `sip_record_route` | topmost Via insert/strip (§16.6/§16.7); RR with `;lr` (§20.30) | |
| `sip_from_value` / `sip_to_value` / `sip_contact_value` / `sip_cseq_value` | common values | |
| `sip_addr_tag` | extract tag= | |
| `sip_branch` / `sip_tag` / `sip_call_id_new` / `sip_cseq_new` | ID generation (`z9hG4bK…`) | |
| `sip_dialog_id` / `sip_txn_key` | opaque map keys for dialogs/txns | |
| `sip_uri_*` / `sip_uri_build` | SIP URI parse/build | |
| `sip_udp_bind` / `sip_udp_send` / `sip_udp_recv` / `sip_tcp_send` | transport wrappers | |
| `sip_md5_hex` / `sip_digest_response` / `sip_digest_response_ha1` | Digest MD5 (password or stored HA1) | |
| `sip_www_authenticate` / `sip_proxy_authenticate` / `sip_authorization_digest` | challenge / Authorization values | |
| `sip_reply` / `sip_reply_with_to_tag` / `sip_ensure_to_tag` | response build + To-tag | |
| `sdp_ok` / `sdp_version` / `sdp_origin` / `sdp_origin_addr` / `sdp_timing` | session lines | |
| `sdp_connection` / `sdp_connection_addr` / `sdp_connection_is_ip6` | session `c=` (IP4/IP6) | |
| `sdp_media_*` / `sdp_media_formats` / `sdp_media_connection_addr` | media sections; `c=` inheritance | |
| `sdp_attr` / `sdp_media_attr` / `sdp_direction` / `sdp_media_direction` | attributes + sendrecv/… | |
| `sdp_replace_connection_addr` / `sdp_replace_media_port` / `sdp_set_media_direction` | proxy NAT rewrite | |
| `sdp_build_audio` / `sdp_build_av` / `sdp_attr_rtpmap` / `fmtp` / `candidate` | build helpers | |
| `rtp_pack` / `rtp_parse_ok` / `rtp_seq` / `rtp_timestamp` / `rtp_ssrc` / `rtp_payload` / … | RTP V2 | |

**Prefer `std/sip`** (`sip.insert_via`, `sip.header`, …) so app code does not shadow
free `sip_*` builtins. Hot path may call builtins directly.

Tests: `sip_test.mko`, `sip_digest_ha1_test.mko` · pack: `std/sip`.

---

## 17. Crypto & Security

| Function | Signature | Description |
|----------|-----------|-------------|
| `sha1` | `sha1(data: string) -> string` | Compute SHA-1 hash (raw bytes) |
| `sha256` | `sha256(data: string) -> string` | Compute SHA-256 hash (raw bytes) |
| `sha512` | `sha512(data: string) -> string` | Compute SHA-512 hash (raw bytes) |
| `hmac_sha256` | `hmac_sha256(key: string, data: string) -> string` | Compute HMAC-SHA256 (hex) |
| `sha256_raw` | `sha256_raw(data: string) -> string` | Compute SHA-256 (raw 32 bytes) |
| `hmac_sha256_raw` | `hmac_sha256_raw(key: string, data: string) -> string` | Compute HMAC-SHA256 (raw 32 bytes) |
| `xor_bytes` | `xor_bytes(a: string, b: string) -> string` | Pairwise XOR of two equal-length byte strings |
| `random_bytes` | `random_bytes(n: int) -> string` | Generate n cryptographically random bytes |

### Password Hashing

| Function | Signature | Description |
|----------|-----------|-------------|
| `argon2id_hash` | `argon2id_hash(password: string) -> string` | Hash a password with Argon2id (PHC string). Preferred for new systems |
| `argon2id_verify` | `argon2id_verify(phc: string, password: string) -> int` | Verify a password against an Argon2id PHC string (1/0) |
| `bcrypt_hash` | `bcrypt_hash(password: string, cost: int) -> string` | Hash a password with bcrypt (`$2b$`). Cost 4–31; needs libxcrypt (Linux) |
| `bcrypt_verify` | `bcrypt_verify(hash: string, password: string) -> int` | Verify a password against a bcrypt hash (1/0) |
| `bcrypt_available` | `bcrypt_available() -> int` | Whether bcrypt is available in this build (1/0) |
| `pbkdf2_sha256` | `pbkdf2_sha256(password: string, salt: string, iterations: int, dklen: int) -> string` | PBKDF2-HMAC-SHA256 derived key (raw bytes) |

### SCRAM-SHA-256

Crypto core for SCRAM-SHA-256 (RFC 5802 / RFC 7677) challenge-response auth,
exposed via the `crypto` package (`crypto.scram_*`). Compose the `AuthMessage`
from the protocol strings yourself:

```
auth = client_first_bare + "," + server_first + "," + client_final_without_proof
```

Salt arguments are **raw bytes** (base64-decode wire values first). There is no
`scram_client_first` / SASL framing helper — nonces and message assembly are
application code. See `examples/testing/scram_test.mko` (RFC 7677 §3 vector),
[STDLIB.md](STDLIB.md#crypto), and [SECURITY.md](SECURITY.md).

| Function | Signature | Description |
|----------|-----------|-------------|
| `crypto.scram_salted_password` | `(password, salt, iterations) -> string` | PBKDF2 salted password (salt is raw bytes) |
| `crypto.scram_client_key` | `(salted) -> string` | `HMAC(salted, "Client Key")` |
| `crypto.scram_server_key` | `(salted) -> string` | `HMAC(salted, "Server Key")` |
| `crypto.scram_stored_key` | `(client_key) -> string` | `SHA256(client_key)` |
| `crypto.scram_client_signature` | `(stored_key, auth) -> string` | `HMAC(stored_key, auth)` |
| `crypto.scram_server_signature` | `(server_key, auth) -> string` | `HMAC(server_key, auth)` |
| `crypto.scram_client_proof` | `(client_key, client_sig) -> string` | `client_key XOR client_sig` |
| `crypto.scram_verify_proof` | `(stored_key, auth, proof) -> int` | Server-side proof check (1/0); uses `const_eq` on recovered StoredKey |
| `scram_gs2_header` | `(cbind_name: string) -> string` | `n,,` or `p=<name>,,` for SCRAM-SHA-256-PLUS |
| `scram_cbind_b64` | `(gs2_header, cbind_data) -> string` | base64(gs2-header \|\| cbind-data) for `c=` |
| `scram_client_final_without_proof` | `(cbind_b64, nonce) -> string` | `c=…,r=…` bare final message |
| `seal_at_rest` | `(key, plaintext, aad) -> string` | AES-128-GCM at-rest seal (`nonce\|\|ct\|\|tag`) |
| `open_at_rest` | `(key, sealed, aad) -> string` | Open at-rest blob (empty on fail) |
| `seal_file_at_rest` | `(path, key, plaintext, aad) -> int` | Seal + write file |
| `open_file_at_rest` | `(path, key, aad) -> string` | Read + open file |
| `limits_new` | `(mem, time_ms, max_conns) -> Limits` | Resource budget (`0` = unlimited) |
| `limits_try_mem` / `limits_release_mem` | `(Limits, n) -> int` | Charge / release memory |
| `limits_check_time` | `(Limits) -> int` | Within time budget? |
| `limits_try_conn` / `limits_release_conn` | `(Limits) -> int` | Connection slots |
| `session_cancel_token` | `() -> string` | Mint cancel token |
| `session_cancel` / `session_cancelled` / `session_cancel_clear` | `(token) -> int` | Remote session cancel registry |
| `tls_server_new_mtls` | `(cert, key, client_ca) -> TlsServer` | mTLS server (require client cert) |
| `tls_client_new_mtls` | `(ca, client_cert, client_key) -> TlsClient` | mTLS client (present cert) |
| `tls_unique` | `(conn: TlsConn) -> string` | Finished bytes for tls-unique binding |
| `scram_tls_unique_cbind` | `(conn: TlsConn) -> string` | SCRAM-PLUS `c=` from `tls_unique` |
| `scram_plus_client_final_bare` | `(conn, nonce) -> string` | `c=…,r=…` with tls-unique |
| `tls_server_reload` | `(server, cert, key) -> int` | Hot-reload server cert/key (0=ok) |
| `tls_make_self_signed` | `(cert_path, key_path, cn, days) -> int` | Write self-signed PEMs (OpenSSL) |
| `tls_make_csr` | `(csr_path, key_path, cn, bits) -> int` | Write CSR + key PEMs (OpenSSL) |
| `pem_count_blocks` | `(pem: string) -> int` | Count `-----BEGIN` blocks |
| `pem_has_block` | `(pem, label) -> int` | 1 if `BEGIN label` present |
| `pem_extract_block` | `(pem, label) -> string` | First full PEM block or empty |
| `pem_load_file` | `(path: string) -> string` | Read file; empty if not PEM |
| `const_eq` | `const_eq(a: string, b: string) -> int` | Constant-time string comparison |
| `crypto_eq` | `crypto_eq(a: string, b: string) -> int` | Constant-time byte comparison |
| `secret_from_str` | `secret_from_str(s: string) -> Secret` | Wrap a string as a secret (zeroized on drop) |
| `secret_drop` | `secret_drop(s: Secret) -> void` | Securely erase and drop a secret |
| `secret_len` | `secret_len(s: Secret) -> int` | Length of secret buffer |
| `secret_eq_str` | `secret_eq_str(s: Secret, other: string) -> int` | Constant-time secret vs string compare |
| `hkdf_sha256` | `hkdf_sha256(ikm: string, salt: string, info: string, out_len: int) -> string` | HKDF-SHA256 extract+expand (RFC 5869) |
| `aead_available` | `aead_available() -> int` | Check if AEAD ciphers are available |
| `aes_gcm_seal` | `aes_gcm_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | Encrypt with AES-GCM |
| `aes_gcm_open` | `aes_gcm_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | Decrypt with AES-GCM |
| `aes_ctr` | `aes_ctr(key: string, iv: string, data: string) -> string` | AES-128/256-CTR (key 16\|32, iv 16); SRTP AES-CM building block |
| `hmac_sha1` | `hmac_sha1(key: string, data: string) -> string` | HMAC-SHA1 hex (40 chars) |
| `hmac_sha1_raw` | `hmac_sha1_raw(key: string, data: string) -> string` | HMAC-SHA1 raw 20 bytes (SRTP auth tag source) |
| `chacha20_poly1305_seal` | `chacha20_poly1305_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | Encrypt with ChaCha20-Poly1305 |
| `chacha20_poly1305_open` | `chacha20_poly1305_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | Decrypt with ChaCha20-Poly1305 |

---

## 18. Encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `base64_encode` | `base64_encode(data: string) -> string` | Encode data as base64 |
| `base64_decode` | `base64_decode(data: string) -> string` | Decode base64 data |
| `base32_encode` | `base32_encode(data: string) -> string` | Encode data as base32 |
| `hex_encode` | `hex_encode(data: string) -> string` | Encode data as hexadecimal |
| `hex_decode` | `hex_decode(data: string) -> string` | Decode hexadecimal data |
| `url_query_escape` | `url_query_escape(s: string) -> string` | Escape a string for use in URL query parameters |
| `gzip_compress` | `gzip_compress(data: string) -> string` | Compress data with gzip |
| `gzip_decompress` | `gzip_decompress(data: string) -> string` | Decompress gzip data |
| `gzip_available` | `gzip_available() -> int` | Check if gzip is available |
| `bin_encode_int` | `bin_encode_int(n: int) -> string` | Encode an integer in binary format |

### Binary Encoding (Little-Endian)

| Function | Signature | Description |
|----------|-----------|-------------|
| `binary_put_u16le` | `binary_put_u16le(n: int) -> string` | Encode uint16 as little-endian bytes |
| `binary_put_u32le` | `binary_put_u32le(n: int) -> string` | Encode uint32 as little-endian bytes |
| `binary_put_u64le` | `binary_put_u64le(n: int) -> string` | Encode uint64 as little-endian bytes |
| `binary_u16le` | `binary_u16le(data: string) -> int` | Decode little-endian bytes as uint16 |
| `binary_u32le` | `binary_u32le(data: string) -> int` | Decode little-endian bytes as uint32 |
| `binary_u64le` | `binary_u64le(data: string) -> int` | Decode little-endian bytes as uint64 |

### Binary Encoding (Big-Endian)

| Function | Signature | Description |
|----------|-----------|-------------|
| `binary_put_u16be` | `binary_put_u16be(n: int) -> string` | Encode uint16 as big-endian bytes |
| `binary_put_u32be` | `binary_put_u32be(n: int) -> string` | Encode uint32 as big-endian bytes |
| `binary_put_u64be` | `binary_put_u64be(n: int) -> string` | Encode uint64 as big-endian bytes |
| `binary_u16be` | `binary_u16be(data: string) -> int` | Decode big-endian bytes as uint16 |
| `binary_u32be` | `binary_u32be(data: string) -> int` | Decode big-endian bytes as uint32 |
| `binary_u64be` | `binary_u64be(data: string) -> int` | Decode big-endian bytes as uint64 |

### Gob Encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `gob_encode_string` | `gob_encode_string(s: string) -> string` | Encode a string in gob format |
| `gob_decode_string` | `gob_decode_string(data: string) -> string` | Decode a gob-encoded string |
| `gob_encode_int` | `gob_encode_int(n: int) -> string` | Encode an integer in gob format |
| `gob_decode_int` | `gob_decode_int(data: string) -> int` | Decode a gob-encoded integer |
| `gob_encode_map_ss` | `gob_encode_map_ss(m: map[string]string) -> string` | Encode a map[string]string in gob format |
| `gob_decode_map_ss` | `gob_decode_map_ss(data: string) -> map[string]string` | Decode a gob-encoded map[string]string |
| `gob_encode_strs` | `gob_encode_strs(arr: []string) -> string` | Encode a string array in gob format |
| `gob_decode_strs` | `gob_decode_strs(data: string) -> []string` | Decode a gob-encoded string array |
| `gob_encode_struct` | `gob_encode_struct(val: ReflectValue) -> string` | Encode a reflected struct in gob format |
| `gob_decode_struct` | `gob_decode_struct(data: string) -> ReflectValue` | Decode a gob-encoded struct |

### Serialization Formats

| Function | Signature | Description |
|----------|-----------|-------------|
| `yaml_get_string` | `yaml_get_string(yaml: string, key: string) -> string` | Extract a string value from YAML by key |
| `toml_get_string` | `toml_get_string(toml: string, key: string) -> string` | Extract a string value from TOML by key |
| `toml_get_int` | `toml_get_int(toml: string, key: string) -> int` | Extract an integer value from TOML by key |
| `msgpack_int_hex` | `msgpack_int_hex(n: int) -> string` | Encode an integer as MessagePack hex |
| `cbor_int_hex` | `cbor_int_hex(n: int) -> string` | Encode an integer as CBOR hex |
| `avro_long_hex` | `avro_long_hex(n: int) -> string` | Encode a long as Avro hex |
| `csv_split_line` | `csv_split_line(line: string) -> []string` | Split a CSV line into fields |
| `csv_join_row` | `csv_join_row(fields: []string) -> string` | Join fields into a CSV row |
| `xml_escape` | `xml_escape(s: string) -> string` | Escape special characters for XML |
| `html_escape` | `html_escape(s: string) -> string` | Escape special characters for HTML |
| `xml_tag_text` | `xml_tag_text(tag: string, text: string) -> string` | Create an XML element with text content |

---

## 19. Regular Expressions

| Function | Signature | Description |
|----------|-----------|-------------|
| `regex_match` | `regex_match(pattern: string, s: string) -> bool` | Check if string matches a regex pattern |
| `regex_find` | `regex_find(pattern: string, s: string) -> string` | Find the first match of a pattern |
| `regex_find_all` | `regex_find_all(pattern: string, s: string, max: int) -> []string` | Find all matches up to max count |
| `regex_replace` | `regex_replace(pattern: string, s: string, replacement: string) -> string` | Replace the first match |
| `regex_replace_all` | `regex_replace_all(pattern: string, s: string, replacement: string) -> string` | Replace all matches |
| `regex_capture` | `regex_capture(pattern: string, s: string, group: int) -> string` | Extract a capture group from a match |
| `regex_valid` | `regex_valid(pattern: string) -> int` | Check if a regex pattern is valid |
| `regex_quote_meta` | `regex_quote_meta(s: string) -> string` | Escape regex metacharacters in a string |

---

## 20. UUID

| Function | Signature | Description |
|----------|-----------|-------------|
| `uuid_v4` | `uuid_v4() -> Uuid` | Random UUID v4 (CSPRNG; 16-byte **Copy** POD) |
| `uuid_v7` | `uuid_v7() -> Uuid` | Time-ordered UUID v7 (unix-ms + random; index-friendly) |
| `uuid_v5` | `uuid_v5(ns: Uuid, name: string) -> Uuid` | Name-based UUID v5 (SHA-1 of namespace‖name) |
| `uuid_nil` | `uuid_nil() -> Uuid` | Nil UUID (all zeros) |
| `uuid_ns_dns` / `uuid_ns_url` / `uuid_ns_oid` / `uuid_ns_x500` | `() -> Uuid` | RFC 4122 standard namespaces |
| `uuid_string` | `uuid_string(u: Uuid) -> string` | Canonical lowercase `8-4-4-4-12` |
| `uuid_string_upper` | `uuid_string_upper(u: Uuid) -> string` | Canonical uppercase |
| `uuid_urn` | `uuid_urn(u: Uuid) -> string` | `urn:uuid:…` form |
| `uuid_bytes` | `uuid_bytes(u: Uuid) -> string` | Raw 16 bytes (binary string) |
| `uuid_from_bytes` | `uuid_from_bytes(s: string) -> Uuid` | From 16 bytes; **aborts** if length ≠ 16 |
| `uuid_parse` | `uuid_parse(s: string) -> Uuid` | Parse canonical / 32-hex / braces / `urn:uuid:` (nil on fail) |
| `uuid_parse_ok` | `uuid_parse_ok(s: string) -> bool` | Valid parse? |
| `uuid_eq` | `uuid_eq(a: Uuid, b: Uuid) -> bool` | Equality |
| `uuid_cmp` | `uuid_cmp(a: Uuid, b: Uuid) -> int` | Ordered compare (−1/0/1) |
| `uuid_is_nil` | `uuid_is_nil(u: Uuid) -> bool` | All zeros? |
| `uuid_version` | `uuid_version(u: Uuid) -> int` | RFC version nibble (4, 5, 7, …) |
| `uuid_variant` | `uuid_variant(u: Uuid) -> int` | Variant (2 = RFC 4122) |
| `uuid_check` | `uuid_check(s: string) -> Result[int, string]` | Parse validate → `Ok(1)` or `Err` |
| `ulid_new` | `ulid_new() -> Uuid` | New ULID (same 16-byte POD as Uuid; time-sortable) |
| `ulid_string` | `ulid_string(u: Uuid) -> string` | Crockford Base32 (26 chars) |
| `ulid_parse` / `ulid_parse_ok` | parse / check | ULID string ↔ POD |
| `ulid_timestamp_ms` | `ulid_timestamp_ms(u: Uuid) -> int` | 48-bit unix-ms prefix |

---

## 21. Cookies & Sessions

| Function | Signature | Description |
|----------|-----------|-------------|
| `cookie_get` | `cookie_get(header: string, name: string) -> string` | Extract a cookie value from a Cookie header |
| `cookie_make` | `cookie_make(name: string, value: string, max_age: int) -> string` | Create a Set-Cookie header string |
| `session_id_new` | `session_id_new() -> string` | Generate a new cryptographically random session ID |
| `csrf_token` | `csrf_token() -> string` | Generate a new CSRF token |
| `csrf_check` | `csrf_check(token: string, expected: string) -> int` | Validate a CSRF token |

---

## 22. Authentication

| Function | Signature | Description |
|----------|-----------|-------------|
| `auth_bearer` | `auth_bearer(header: string) -> string` | Extract bearer token from Authorization header |
| `auth_check_bearer` | `auth_check_bearer(header: string, expected: string) -> int` | Validate a bearer token |
| `auth_basic_header` | `auth_basic_header(user: string, pass: string) -> string` | Create a Basic auth header |
| `auth_check_basic` | `auth_check_basic(header: string, user: string, pass: string) -> int` | Validate Basic auth credentials |
| `auth_token_sign` | `auth_token_sign(subject: string, secret: string) -> string` | Sign an auth token with a subject |
| `auth_token_check` | `auth_token_check(token: string, secret: string) -> int` | Verify an auth token |
| `auth_token_subject` | `auth_token_subject(token: string) -> string` | Extract the subject from an auth token |
| `auth_session_cookie` | `auth_session_cookie(user: string, secret: string, name: string) -> int` | Create a session cookie for a user |
| `auth_role_has` | `auth_role_has(roles: string, role: string) -> int` | Check if a role list contains a specific role |
| `authz_allow_role` | `authz_allow_role(roles: string, required: string) -> int` | Authorize based on required role |

---

## 23. Channels

| Function | Signature | Description |
|----------|-----------|-------------|
| `chan_new` | `chan_new(capacity: int) -> chan[int]` | Create a new buffered int channel |
| `chan_open[T]` | `chan_open[T](capacity: int) -> chan[T]` | Typed channel (see element types below) |
| `chan_try_send` | `chan_try_send(ch: chan[int], val: int) -> int` | Non-blocking int send; **1** queued, **0** full/closed |
| `chan_send_timeout` | `chan_send_timeout(ch, val, ms) -> int` | Timed send; **1** ok, **0** timeout, **-1** closed |
| `chan_recv_timeout` | `chan_recv_timeout(ch, ms) -> Result[int, string]` | Timed recv; `Ok(v)` / `Err("timeout"\|"closed")` |
| `chan_str_send_take` | `chan_str_send_take(ch: chan[string], v: string) -> int` | Blocking move-send (no clone); **1** ok, **0** closed |
| `chan_str_try_send_take` | `chan_str_try_send_take(ch: chan[string], v: string) -> int` | Non-blocking move-send; **1** queued, **0** full/closed (consumes `v`) |
| `chan_len` | `chan_len(ch: chan[int]) -> int` | Return the number of items in the channel |
| `chan_cap` | `chan_cap(ch: chan[int]) -> int` | Return the capacity of the channel |
| `chan_select2` | `chan_select2(a: chan[int], b: chan[int], timeout_ms: int) -> int` | Select from two **int** channels with timeout |
| `chan_select3` | `chan_select3(a: chan[int], b: chan[int], c: chan[int], timeout_ms: int) -> int` | Select from three int channels with timeout |
| `chan_select4` | `chan_select4(a: chan[int], b: chan[int], c: chan[int], d: chan[int], timeout_ms: int) -> int` | Select from four int channels with timeout |
| `chan_select_value` | `chan_select_value() -> int` | Value from last **int** select |
| `chan_str_select2` | `chan_str_select2(a, b, timeout_ms) -> int` | Select between two string channels |
| `chan_select_value_str` | `chan_select_value_str() -> string` | Value from last **string** select |

**Methods on `ch`:** `ch.send(v)`, `ch.recv()`, `ch.close()`, `job.join_timeout(ms)` (returns 0 on timeout).

**`chan_open[T]` element types**

| `T` | Runtime | Notes |
|-----|---------|--------|
| int family / bool | `MakoChan` | Default int ring |
| float | `MakoChan` | Bitcast via `mako_f64_to_bits` / `mako_bits_to_f64` |
| string | `MakoChanStr` | Owned strings; `chan_str_select2` / select syntax |
| named struct (incl. pack types) | `MakoChanPtr` | Heap-box on send; free on recv; **select takes the message** (do not `recv` again in the arm) |

`make(chan[T], n)` accepts the same `T` set as `chan_open[T](n)`.

`select timeout … { }` uses int, string, or **struct/ptr** select when all arms match.
Helpers: `chan_select_value` / `chan_select_value_str` / `mako_chan_select_value_ptr`.
Tests: `chan_struct_test`, `chan_make_struct_test`, `chan_float_test`,
`wave8_queue_test`, `wave9_queue_test`.

---

## 24. Concurrency

### Crew / kick / join / fan (language)

| Construct | Meaning |
|-----------|---------|
| `crew t { … }` | Structured scope; cancel+join on exit (no orphan tasks) |
| `t.kick(f(args…))` | Spawn on crew; returns `Job[R]` |
| `job.join()` / `join(job)` | Wait for result of type `R` |
| `job.join_timeout(ms)` | Timed join → **`Result[R, string]`** (`Ok`/`Err("timeout")`). If `R` is already `Result[T, string]`, **flattens** (no nest). |
| `job.join_deadline(dl)` | Same as `join_timeout`, but `dl` is absolute mono deadline from `deadline_ms`/`deadline_ns` |
| `ch.send_timeout(v, ms)` / `ch.try_send(v)` | Timed / non-blocking send (`int` channels); **1**/**0**/**-1** |
| `ch.recv_timeout(ms)` / `ch.try_recv()` | Timed / non-blocking recv → **`Result[int, string]`** (`timeout` / `closed` / `empty`) |
| `t.drain(ms)` / `crew_drain` | Cancel + join with timeout budget |
| `t.cancel()` / `t.cancelled()` | Cooperative cancel flag |
| `t.err_count()` / `t.first_err()` | Child `Result` Errs recorded on join (count / first message) |
| `t.wait()` | Join pending tasks → `Result[int, string]` (`Ok(0)` or first Err) |
| `detach f()` | Spawn on process-scoped nursery (not joined by enclosing crew) |
| `detached_join_all()` | Join all detached tasks (tests / shutdown) |
| `actor` / `receive` | Mailbox loop desugar; optional state fields via `self.field` |
| `fan(xs, \|x\| …)` | Parallel map over array |

**Job return types (`join`)**

| `R` | Behavior |
|-----|----------|
| int / bool | Packed in `intptr_t` |
| string | Heap-boxed across pthread; join unboxes |
| `Result[T, E]` | Heap-boxed `MakoResultInt`; join unboxes |
| float | Bitcast through `intptr_t` |

Kick **args** that are sendable: Copy scalars, **POD structs** (int/float/bool/**string** fields, heap-boxed; strings cloned), string (cloned), chan handles, ShareInt/sync handles. Arrays/maps/non-POD structs remain rejected (`examples/bad/kick_non_pod.mko`).

`reflect_value_of(s)` snapshots reflectable struct fields (POD leaves, nested POD,
Option/Result/array/map of reflectable; not chan/Arena) into a reflect bag,
flattening nested POD structs leaf-first. Structs with maps/slices remain rejected
(`examples/bad/reflect_non_pod.mko`).
`Result[[]int|[]string|[]float|[]Struct, E]` and
`Result[map[string]int|map[int]int|map[string]string, E]` Ok are supported
(heap-boxed arrays / map pointers).

**`fan` element types:** `[]int`, `[]float`, `[]string`, `[]Struct` (POD named structs via `mako_par_map_bytes`).

Tests: `crew_fan_test.mko`, `job_join_typed_test.mko`, `fan_struct_test.mko`, `fan_float_test.mko`, `fan_string_test.mko`, `crew_drain_test.mko`.

### Mutexes

| Function | Signature | Description |
|----------|-----------|-------------|
| `mutex_new` | `mutex_new() -> Mutex` | Create a new mutex |
| `mutex_lock` | `mutex_lock(m: Mutex) -> void` | Acquire the mutex lock |
| `mutex_unlock` | `mutex_unlock(m: Mutex) -> void` | Release the mutex lock |
| `rwmutex_new` | `rwmutex_new() -> RWMutex` | Create a new readers-writer mutex |
| `rwmutex_rlock` | `rwmutex_rlock(m: RWMutex) -> void` | Acquire a read lock |
| `rwmutex_runlock` | `rwmutex_runlock(m: RWMutex) -> void` | Release a read lock |
| `rwmutex_lock` | `rwmutex_lock(m: RWMutex) -> void` | Acquire a write lock |
| `rwmutex_unlock` | `rwmutex_unlock(m: RWMutex) -> void` | Release a write lock |

### Atomics

| Function | Signature | Description |
|----------|-----------|-------------|
| `atomic_new` | `atomic_new(val: int) -> AtomicInt` | Create a new atomic integer |
| `atomic_load` | `atomic_load(a: AtomicInt) -> int` | Atomically load the value |
| `atomic_store` | `atomic_store(a: AtomicInt, val: int) -> void` | Atomically store a value |
| `atomic_add` | `atomic_add(a: AtomicInt, delta: int) -> int` | Atomically add and return the new value |
| `atomic_cas` | `atomic_cas(a: AtomicInt, expected: int, desired: int) -> int` | Compare-and-swap; returns 1 on success |

### WaitGroup

| Function | Signature | Description |
|----------|-----------|-------------|
| `wait_group_new` | `wait_group_new() -> WaitGroup` | Create a new wait group |
| `wait_group_add` | `wait_group_add(wg: WaitGroup, delta: int) -> void` | Add to the wait group counter |
| `wait_group_done` | `wait_group_done(wg: WaitGroup) -> void` | Decrement the wait group counter |
| `wait_group_wait` | `wait_group_wait(wg: WaitGroup) -> void` | Block until the counter reaches zero |

### Shared References

| Function | Signature | Description |
|----------|-----------|-------------|
| `share_int` | `share_int(val: int) -> ShareInt` | Create a shared reference-counted integer |
| `share_clone` | `share_clone(s: ShareInt) -> ShareInt` | Clone a shared integer (increment ref count) |
| `share_get` | `share_get(s: ShareInt) -> int` | Read the value of a shared integer |
| `share_drop` | `share_drop(s: ShareInt) -> void` | Drop a shared integer reference |

---

## 25. Concurrent Map (CMap)

| Function | Signature | Description |
|----------|-----------|-------------|
| `cmap_new` | `cmap_new() -> CMap` | Create a new concurrent hash map |
| `cmap_set` | `cmap_set(m: CMap, key: string, val: string) -> void` | Set a key-value pair |
| `cmap_get` | `cmap_get(m: CMap, key: string) -> string` | Get a value by key |
| `cmap_has` | `cmap_has(m: CMap, key: string) -> int` | Check if a key exists |
| `cmap_del` | `cmap_del(m: CMap, key: string) -> int` | Delete a key |
| `cmap_len` | `cmap_len(m: CMap) -> int` | Return the number of entries |
| `cmap_incr` | `cmap_incr(m: CMap, key: string, delta: int) -> int` | Atomically increment a numeric value |

---

## 26. Event Loop

| Function | Signature | Description |
|----------|-----------|-------------|
| `evloop_new` | `evloop_new() -> EvLoop` | Create a new event loop |
| `evloop_add` | `evloop_add(el: EvLoop, fd: int, flags: int) -> int` | Add a file descriptor to the event loop |
| `evloop_mod` | `evloop_mod(el: EvLoop, fd: int, flags: int) -> int` | Modify events for a file descriptor |
| `evloop_del` | `evloop_del(el: EvLoop, fd: int) -> int` | Remove a file descriptor from the event loop |
| `evloop_wait` | `evloop_wait(el: EvLoop, timeout_ms: int) -> int` | Wait for events with timeout |
| `evloop_event_fd` | `evloop_event_fd(el: EvLoop, idx: int) -> int` | Get the fd of the i-th ready event |
| `evloop_event_flags` | `evloop_event_flags(el: EvLoop, idx: int) -> int` | Get the flags of the i-th ready event |
| `evloop_close` | `evloop_close(el: EvLoop) -> int` | Close the event loop |

### Non-Blocking I/O Helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `nb_listen` | `nb_listen(port: int) -> int` | Create a non-blocking listening socket |
| `nb_accept` | `nb_accept(fd: int) -> int` | Non-blocking accept |
| `nb_read` | `nb_read(fd: int) -> string` | Non-blocking read |
| `nb_write` | `nb_write(fd: int, data: string) -> int` | Non-blocking write |
| `nb_udp_bind` | `nb_udp_bind(port: int) -> int` | Bind a non-blocking UDP socket |
| `nb_udp_recv` | `nb_udp_recv(fd: int) -> string` | Non-blocking UDP receive |
| `nb_close` | `nb_close(fd: int) -> int` | Close a non-blocking fd |

---

## 27. Binary Buffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_pack_new` | `buf_pack_new(capacity: int) -> Buf` | Create a new binary buffer with given capacity |
| `buf_from_string` | `buf_from_string(s: string) -> Buf` | Create a buffer from a string |
| `buf_to_string` | `buf_to_string(b: Buf) -> string` | Convert buffer contents to a string |
| `buf_len` | `buf_len(b: Buf) -> int` | Return the length of the buffer |
| `buf_pos` | `buf_pos(b: Buf) -> int` | Return the current read/write position |
| `buf_reset` | `buf_reset(b: Buf) -> void` | Reset buffer position to zero |
| `buf_seek` | `buf_seek(b: Buf, pos: int) -> void` | Seek to a specific position |
| `buf_free` | `buf_free(b: Buf) -> void` | Free the buffer memory |

### Write Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_write_u8` | `buf_write_u8(b: Buf, val: int) -> void` | Write a uint8 |
| `buf_write_u16` | `buf_write_u16(b: Buf, val: int) -> void` | Write a uint16 (little-endian) |
| `buf_write_u32` | `buf_write_u32(b: Buf, val: int) -> void` | Write a uint32 (little-endian) |
| `buf_write_u64` | `buf_write_u64(b: Buf, val: int) -> void` | Write a uint64 (little-endian) |
| `buf_write_i32` | `buf_write_i32(b: Buf, val: int) -> void` | Write a signed int32 |
| `buf_write_f32` | `buf_write_f32(b: Buf, val: float) -> void` | Write a 32-bit float |
| `buf_write_f64` | `buf_write_f64(b: Buf, val: float) -> void` | Write a 64-bit float |
| `buf_write_bytes` | `buf_write_bytes(b: Buf, data: string) -> void` | Write raw bytes |
| `buf_write_str` | `buf_write_str(b: Buf, s: string) -> void` | Write a length-prefixed string |
| `buf_write_u16be` | `buf_write_u16be(b: Buf, val: int) -> void` | Write a uint16 (big-endian) |
| `buf_write_u32be` | `buf_write_u32be(b: Buf, val: int) -> void` | Write a uint32 (big-endian) |

### Read Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_read_u8` | `buf_read_u8(b: Buf) -> int` | Read a uint8 |
| `buf_read_u16` | `buf_read_u16(b: Buf) -> int` | Read a uint16 (little-endian) |
| `buf_read_u32` | `buf_read_u32(b: Buf) -> int` | Read a uint32 (little-endian) |
| `buf_read_u64` | `buf_read_u64(b: Buf) -> int` | Read a uint64 (little-endian) |
| `buf_read_i32` | `buf_read_i32(b: Buf) -> int` | Read a signed int32 |
| `buf_read_f32` | `buf_read_f32(b: Buf) -> float` | Read a 32-bit float |
| `buf_read_f64` | `buf_read_f64(b: Buf) -> float` | Read a 64-bit float |
| `buf_read_bytes` | `buf_read_bytes(b: Buf, n: int) -> string` | Read n raw bytes |
| `buf_read_str` | `buf_read_str(b: Buf) -> string` | Read a length-prefixed string |
| `buf_read_u16be` | `buf_read_u16be(b: Buf) -> int` | Read a uint16 (big-endian) |
| `buf_read_u32be` | `buf_read_u32be(b: Buf) -> int` | Read a uint32 (big-endian) |

### Byte Buffer Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_get` | `buf_get(size: int) -> []byte` | Get a byte buffer from the pool |
| `buf_put` | `buf_put(b: []byte) -> void` | Return a byte buffer to the pool |

---

## 28. Game Networking

| Function | Signature | Description |
|----------|-----------|-------------|
| `game_udp_bind` | `game_udp_bind(port: int) -> GameUDP` | Bind a game UDP socket |
| `game_udp_recv` | `game_udp_recv(sock: GameUDP) -> string` | Receive a packet |
| `game_udp_sender` | `game_udp_sender(sock: GameUDP) -> int` | Get the sender peer ID of the last received packet |
| `game_udp_send` | `game_udp_send(sock: GameUDP, peer: int, data: string) -> int` | Send a packet to a specific peer |
| `game_udp_broadcast` | `game_udp_broadcast(sock: GameUDP, data: string) -> int` | Broadcast a packet to all peers |
| `game_udp_kick` | `game_udp_kick(sock: GameUDP, peer: int) -> void` | Disconnect a peer |
| `game_udp_peers` | `game_udp_peers(sock: GameUDP) -> int` | Return the number of connected peers |
| `game_udp_fd` | `game_udp_fd(sock: GameUDP) -> int` | Get the underlying file descriptor |
| `game_udp_close` | `game_udp_close(sock: GameUDP) -> void` | Close the game UDP socket |

**Multi-worker pattern:** `GameUDP` is **not Send** across crew tasks. Bind once on the
receiver task; fan-out `host:port\n` + body to workers that hold their own SQL/state.
Replies from workers use `udp_send_to` / `sip_udp_send` on a listen fd (`game_udp_fd`)
or a dedicated reply socket — do not share one `GameUDP` handle across workers.
| `tick_now_us` | `tick_now_us() -> int` | Current time in microseconds (monotonic) |
| `tick_sleep_us` | `tick_sleep_us(target: int, period: int) -> int` | Sleep until next tick period |

### Game Loop

| Function | Signature | Description |
|----------|-----------|-------------|
| `game_fixed_steps` | `game_fixed_steps(dt: int, step: int, accum: int) -> int` | Calculate fixed-step update count |
| `game_fixed_remainder` | `game_fixed_remainder(dt: int, step: int, accum: int) -> int` | Remaining accumulator after fixed steps |
| `game_alpha` | `game_alpha(dt: int, step: int, accum: int) -> int` | Interpolation alpha for rendering |
| `game_frame_budget_ok` | `game_frame_budget_ok(elapsed: int, budget: int) -> int` | Check if frame completed within budget |

### Fixed-Point Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `fx_from_int` | `fx_from_int(val: int, shift: int) -> int` | Convert integer to fixed-point |
| `fx_to_int` | `fx_to_int(val: int, shift: int) -> int` | Convert fixed-point to integer |
| `fx_mul` | `fx_mul(a: int, b: int, shift: int) -> int` | Multiply two fixed-point values |
| `fx_div` | `fx_div(a: int, b: int, shift: int) -> int` | Divide two fixed-point values |

### Deterministic RNG

| Function | Signature | Description |
|----------|-----------|-------------|
| `det_rng_next` | `det_rng_next(state: int) -> int` | Advance deterministic RNG state |
| `det_rng_range` | `det_rng_range(state: int, max: int) -> int` | Get a deterministic random value in range |

### Replay

| Function | Signature | Description |
|----------|-----------|-------------|
| `replay_append` | `replay_append(buf: string, frame: int, input: int) -> string` | Append an input to a replay buffer |
| `replay_input` | `replay_input(buf: string, frame: int) -> int` | Get the input at a specific frame |

---

## 29. Cloud / Distributed

### Consistent Hashing

| Function | Signature | Description |
|----------|-----------|-------------|
| `chash_new` | `chash_new(replicas: int, nodes: int) -> CHash` | Create a consistent hash ring |
| `chash_get` | `chash_get(ring: CHash, key: string) -> int` | Get the node for a key |
| `chash_add_node` | `chash_add_node(ring: CHash) -> int` | Add a node to the ring |
| `chash_remove_node` | `chash_remove_node(ring: CHash, node: int) -> void` | Remove a node from the ring |
| `chash_node_count` | `chash_node_count(ring: CHash) -> int` | Return the number of nodes |
| `chash_free` | `chash_free(ring: CHash) -> void` | Free the hash ring |

### Rate Limiting

| Function | Signature | Description |
|----------|-----------|-------------|
| `ratelimit_new` | `ratelimit_new(max: int, window_ms: int) -> RateLimiter` | Create a new rate limiter |
| `ratelimit_allow` | `ratelimit_allow(rl: RateLimiter) -> int` | Check if a request is allowed |
| `ratelimit_remaining` | `ratelimit_remaining(rl: RateLimiter) -> int` | Return remaining allowed requests |
| `ratelimit_free` | `ratelimit_free(rl: RateLimiter) -> void` | Free the rate limiter |
| `rate_allow` | `rate_allow(key: string, max: int, window_ms: int) -> int` | Key-based rate limiting check |
| `rate_remaining` | `rate_remaining(key: string, max: int, window_ms: int) -> int` | Key-based remaining requests |

### Circuit Breaker

| Function | Signature | Description |
|----------|-----------|-------------|
| `breaker_new` | `breaker_new(threshold: int, timeout_ms: int, half_max: int) -> CircuitBreaker` | Create a new circuit breaker |
| `breaker_allow` | `breaker_allow(cb: CircuitBreaker) -> int` | Check if a request is allowed |
| `breaker_success` | `breaker_success(cb: CircuitBreaker) -> void` | Record a successful operation |
| `breaker_failure` | `breaker_failure(cb: CircuitBreaker) -> void` | Record a failed operation |
| `breaker_state` | `breaker_state(cb: CircuitBreaker) -> int` | Get the current state (closed/open/half-open) |
| `breaker_reset` | `breaker_reset(cb: CircuitBreaker) -> void` | Reset to closed state |
| `breaker_free` | `breaker_free(cb: CircuitBreaker) -> void` | Free the circuit breaker |

### JWT

| Function | Signature | Description |
|----------|-----------|-------------|
| `jwt_sign` | `jwt_sign(payload: string, secret: string) -> string` | Sign a JWT payload |
| `jwt_verify` | `jwt_verify(token: string, secret: string) -> int` | Verify a JWT signature |
| `jwt_payload` | `jwt_payload(token: string) -> string` | Extract the payload from a JWT |

### Backoff

| Function | Signature | Description |
|----------|-----------|-------------|
| `backoff_ms` | `backoff_ms(attempt: int, base_ms: int, max_ms: int) -> int` | Calculate exponential backoff delay |

---

## 30. HTTP Engine

| Function | Signature | Description |
|----------|-----------|-------------|
| `httpengine_new` | `httpengine_new(port: int, max_conns: int) -> HttpEngine` | Create a new HTTP engine |
| `httpengine_route` | `httpengine_route(engine: HttpEngine, pattern: string, method: int, handler: string, name: string) -> void` | Register a route |
| `httpengine_serve` | `httpengine_serve(engine: HttpEngine) -> int` | Start serving |

---

## 31. Router

| Function | Signature | Description |
|----------|-----------|-------------|
| `router_new` | `router_new() -> int` | Create a new router |
| `router_group` | `router_group(router: int, prefix: string) -> int` | Create a route group with a prefix |
| `router_add` | `router_add(router: int, method: string, pattern: string, handler: string) -> int` | Add a route |
| `router_match` | `router_match(router: int, req: HttpRequest) -> string` | Match a request to a route handler |
| `router_match_path` | `router_match_path(router: int, method: string, path: string) -> string` | Match a method and path to a handler |
| `router_param` | `router_param(router: int, req: HttpRequest, name: string) -> string` | Extract a route parameter |
| `router_count` | `router_count(router: int) -> int` | Return the number of registered routes |

---

## 32. Request Context & Middleware

| Function | Signature | Description |
|----------|-----------|-------------|
| `reqctx_new` | `reqctx_new() -> int` | Create a new request context |
| `reqctx_set` | `reqctx_set(ctx: int, key: string, val: string) -> int` | Set a context value |
| `reqctx_get` | `reqctx_get(ctx: int, key: string) -> string` | Get a context value |
| `reqctx_has` | `reqctx_has(ctx: int, key: string) -> int` | Check if a context key exists |
| `reqctx_count` | `reqctx_count(ctx: int) -> int` | Return the number of context entries |
| `middleware_allow_methods` | `middleware_allow_methods(req: HttpRequest, methods: string) -> int` | Check if request method is allowed |
| `middleware_next` | `middleware_next(ctx: int, name: string) -> int` | Advance to the next middleware |
| `middleware_ran` | `middleware_ran(ctx: int, name: string) -> int` | Check if a middleware has run |
| `middleware_trace` | `middleware_trace(ctx: int) -> string` | Get the middleware execution trace |
| `middleware_require_context` | `middleware_require_context(ctx: int, key: string) -> int` | Require a context key to be set |

---

## 33. Testing

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `assert(cond: bool) -> void` | Assert a condition is true; panic if false |
| `assert_eq` | `assert_eq(a: int, b: int) -> void` | Assert two integers are equal |
| `assert_eq_str` | `assert_eq_str(a: string, b: string) -> void` | Assert two strings are equal |
| `fail` | `fail(msg: string) -> void` | Unconditionally fail with a message |
| `t_run` | `t_run(name: string) -> void` | Run a named test case |
| `t_run_nested` | `t_run_nested(name: string) -> void` | Run a nested test case |
| `black_box` | `black_box(n: int) -> int` | Prevent compiler optimization (benchmarking) |
| `httptest_serve_once` | `httptest_serve_once(port: int, response: string) -> int` | Start a test HTTP server for one request |
| `httptest_get` | `httptest_get(url: string) -> string` | Perform a test HTTP GET |
| `httptest_status` | `httptest_status() -> int` | Get the status of the last test request |
| `httptest_header` | `httptest_header(name: string) -> string` | Get a header from the last test response |

---

## 34. Runtime

| Function | Signature | Description |
|----------|-----------|-------------|
| `runtime_stats_json` | `runtime_stats_json() -> string` | Return runtime statistics as JSON |
| `runtime_stats_reset` | `runtime_stats_reset() -> void` | Reset runtime statistics counters |
| `exit` | `exit(code: int) -> void` | Exit the process with a status code |
| `leak_mark` | `leak_mark() -> int` | Mark current allocation state for leak detection |
| `leak_bytes_since` | `leak_bytes_since(mark: int) -> int` | Bytes allocated since a leak mark |
| `leak_detected` | `leak_detected(mark: int) -> int` | Check if any leak detected since mark |
| `leak_assert_clear` | `leak_assert_clear(mark: int) -> int` | Assert no leaks since mark |
| `leak_report_json` | `leak_report_json(mark: int) -> string` | Leak report as JSON |
| `gc_arena_new` | `gc_arena_new() -> Arena` | Create a new GC arena (always available; alias of arena) |
| `gc_alloc` | `gc_alloc(nbytes: int) -> int` | Optional app GC alloc (handle bits); requires `[package] gc = true` |
| `gc_collect` | `gc_collect() -> int` | Tracing collect: mark roots+edges, free unmarked; returns freed count |
| `gc_live` | `gc_live() -> int` | Live GC objects count |
| `gc_enabled` | `gc_enabled() -> int` | 1 if optional GC runtime is on |
| `gc_root` | `gc_root(handle: int) -> int` | Register root (kept across collect) |
| `gc_unroot` | `gc_unroot(handle: int) -> int` | Drop root registration |
| `gc_link` | `gc_link(parent: int, child: int) -> int` | Trace edge parent→child |
| `gc_mark` | `gc_mark(handle: int) -> int` | Manual mark (cleared at collect start unless rooted) |
| `gc_root_count` | `gc_root_count() -> int` | Number of registered roots |

### Map take (no string-key clone)

| Builtin | Signature | Notes |
|---------|-----------|--------|
| `map_si_set_take` | `map_si_set_take(m: map[string]int, key: string, val: int)` | Moves `key` into the map |
| `map_ss_set_take` | `map_ss_set_take(m: map[string]string, key: string, val: string)` | Moves `key` and `val` |

Prefer over `m[k] = v` on bulk-insert hot paths when the key is an owned temporary.

### Channel string take-send (no clone)

| Builtin | Signature | Notes |
|---------|-----------|--------|
| `chan_str_send_take` | `chan_str_send_take(ch: chan[string], v: string) -> int` | Move `v` into the channel (blocking if full); 1 ok, 0 if closed |
| `chan_str_try_send_take` | `chan_str_try_send_take(ch: chan[string], v: string) -> int` | Non-blocking move; 1 queued, 0 full/closed (**consumes** `v` either way) |

Default `ch.send(s)` still clones so the caller may reuse `s`. Prefer take on producer hot paths with owned temporaries.

| Function | Signature | Description |
|----------|-----------|-------------|
| `arena_text` | `arena_text(a: Arena, s: string) -> string` | Allocate a string in the arena |
| `arena_ints` | `arena_ints(a: Arena, n: int) -> []int` | Allocate an int array in the arena |
| `arena_stamp` | `arena_stamp(a: Arena, n: int) -> int` | Stamp an arena allocation |
| `dlopen_probe` | `dlopen_probe(lib: string) -> int` | Check if a dynamic library can be loaded |
| `await_timeout` | `await_timeout(job: Job[int], timeout_ms: int) -> int` | Await a job with timeout |

---

## 35. Allocation Tracking

| Function | Signature | Description |
|----------|-----------|-------------|
| `alloc_track_reset` | `alloc_track_reset() -> int` | Reset the allocation tracker |
| `alloc_track_alloc` | `alloc_track_alloc(bytes: int) -> int` | Record an allocation |
| `alloc_track_free` | `alloc_track_free(bytes: int) -> int` | Record a free |
| `alloc_live_bytes` | `alloc_live_bytes() -> int` | Return currently live allocated bytes |
| `alloc_high_bytes` | `alloc_high_bytes() -> int` | Return peak allocated bytes |
| `alloc_report_json` | `alloc_report_json() -> string` | Allocation report as JSON |

---

## 36. TCP & UDP Networking

### TCP

| Function | Signature | Description |
|----------|-----------|-------------|
| `tcp_listen` | `tcp_listen(port: int) -> int` | Listen on a TCP port (all interfaces) |
| `tcp_listen_addr` | `tcp_listen_addr(host: string, port: int) -> int` | Listen bound to a specific address (`"127.0.0.1"`, `"*"` for all) |
| `tcp_listen_backlog` | `tcp_listen_backlog(host: string, port: int, backlog: int) -> int` | Listen with an explicit accept backlog (bounds inbound queue) |
| `tcp_accept` | `tcp_accept(listener: int) -> int` | Accept a TCP connection (records peer) |
| `tcp_accept_nb` | `tcp_accept_nb(listener: int) -> int` | Non-blocking TCP accept |
| `tcp_connect` | `tcp_connect(host: string, port: int) -> int` | Dual-stack connect (IPv4/IPv6/hostname) with **Happy Eyeballs** |
| `tcp_connect_timeout` | `tcp_connect_timeout(host, port, timeout_ms) -> int` | Same with total timeout (default path uses 30s) |
| `tcp_set_he_delay_ms` / `tcp_get_he_delay_ms` | stagger between HE attempts (default **250**) | RFC 8305 lite |
| `tcp_connect_nb` | `tcp_connect_nb(host: string, port: int) -> int` | Nonblocking connect to first resolved addr (v4 or v6) |
| `tcp_connect_check` | `tcp_connect_check(fd: int) -> int` | `1` connected, `0` pending, `-1` failed |
| `tcp_connect_wait` | `tcp_connect_wait(fd: int, timeout_ms: int) -> int` | Poll until connect completes (`1`/`0`/`-1`) |
| `tcp_pool_open` | `tcp_pool_open(host: string, port: int, max: int, timeout_ms: int) -> int` | Upstream connection pool handle |
| `tcp_pool_acquire` | `tcp_pool_acquire(pool: int) -> int` | Borrow a live fd (validates reuse) |
| `tcp_pool_release` | `tcp_pool_release(pool: int, fd: int, reusable: int) -> int` | Return fd; close if not reusable |
| `tcp_pool_close` | `tcp_pool_close(pool: int) -> int` | Close pool and all idle fds |
| `tcp_pool_idle` / `tcp_pool_open_count` | `…(pool) -> int` | Idle / total open connection counts |
| `tcp_fd_copy` / `tcp_splice` | `tcp_fd_copy(src, dst, max) -> int` | Efficient fd-to-fd copy (`splice` on Linux) |
| `tcp_proxy_pump` | `tcp_proxy_pump(a, b, timeout_ms, max) -> int` | Bidirectional stream pump |
| `tcp_write` | `tcp_write(conn: int, data: string) -> int` | Write data (may be short) |
| `tcp_write_all` | `tcp_write_all(conn: int, data: string) -> int` | Write all bytes (retries short sends) |
| `tcp_read` | `tcp_read(conn: int) -> string` | Read up to 64 KiB |
| `tcp_read_n` | `tcp_read_n(conn: int, n: int) -> string` | Read exactly `n` bytes (or until EOF) |
| `tcp_read_print` | `tcp_read_print(conn: int) -> int` | Read and print TCP data |
| `tcp_peer_addr` | `tcp_peer_addr(fd: int) -> string` | Peer `"ip:port"` (`getpeername`; last accept if `fd<0`) |
| `tcp_local_addr` | `tcp_local_addr(fd: int) -> string` | Local `"ip:port"` (`getsockname`) |
| `tcp_shutdown` | `tcp_shutdown(fd: int, how: int) -> int` | Half-close: `0`=RD, `1`=WR, `2`=RDWR |
| `tcp_linger` | `tcp_linger(fd: int, onoff: int, sec: int) -> int` | `SO_LINGER` |
| `sock_error` | `sock_error(fd: int) -> int` | `SO_ERROR` (0 = ok) after async connect |
| `tcp_nodelay` | `tcp_nodelay(conn: int) -> int` | Set TCP_NODELAY on a connection |
| `tcp_set_timeout` | `tcp_set_timeout(conn: int, ms: int) -> int` | Set recv+send timeout in ms (0 = block forever) |
| `tcp_keepalive` | `tcp_keepalive(conn: int, idle: int, interval: int, count: int) -> int` | Enable TCP keepalive; tune idle/interval (s) and probe count |
| `tcp_set_recv_buf` / `tcp_set_send_buf` | `…(fd, size) -> int` | Socket buffer sizing |
| `tcp_reuseport` | `tcp_reuseport(fd: int) -> int` | Enable `SO_REUSEPORT` (before bind) |
| `tcp_listen_reuseport` | `tcp_listen_reuseport(host, port, backlog) -> int` | Listen with reuseport |
| `tcp_accept4` | `tcp_accept4(listener: int) -> int` | Accept with `NONBLOCK\|CLOEXEC` |
| `tcp_close` | `tcp_close(conn: int) -> int` | Close a TCP connection |

Sockets created by listen/connect/udp_bind use **CLOEXEC** when available.

**IPv6 / dual-stack:** `tcp_listen` / `tcp_listen_addr("")` or `"*"` prefer `::` with
`IPV6_V6ONLY=0` when the OS allows (IPv4+IPv6); explicit `0.0.0.0` stays IPv4;
`::1` / IPv6 literals bind/connect as v6. Peer/local addrs use `[v6]:port` form.
`udp_bind("*")` stays IPv4 for sendto compatibility; `udp_bind_addr("::1", …)` is v6.
`tcp_connect` resolves `AF_UNSPEC`, interleaves AAAA/A, and races attempts (Happy Eyeballs).
| `http_forward` | `http_forward(host, port, method, path, body) -> string` | Forward to HTTP/1.1 backend; returns body only |
| `http_forward_full` | `http_forward_full(host, port, method, path, headers, body, timeout_ms) -> HttpForwardResult` | Status + body + byte counts (chunked OK) |
| `http_forward_fd` | `http_forward_fd(fd, method, path, host, headers, body, timeout_ms) -> HttpForwardResult` | Forward on pooled fd (`Connection: keep-alive`) |
| `http_forward_ok` | `http_forward_ok(r: HttpForwardResult) -> int` | `1` if response fully read |
| `http_forward_status` | `http_forward_status(r) -> int` | HTTP status (0 if fail) |
| `http_forward_body` | `http_forward_body(r) -> string` | Decoded body (chunked already decoded) |
| `http_forward_body_len` | `http_forward_body_len(r) -> int` | Body length |
| `http_forward_total_bytes` | `http_forward_total_bytes(r) -> int` | Raw response size (headers+body wire) |
| `http_forward_headers` | `http_forward_headers(r) -> string` | Raw header block after status line |
| `http_proxy_raw` | `http_proxy_raw(client_fd, backend_fd, raw_request, timeout_ms) -> ProxyIoResult` | Raw request/response byte pump (no rebuild) |
| `proxy_io_ok` / `proxy_io_bytes_written` / `proxy_io_bytes_read` | accessors on `ProxyIoResult` | |
| `http_parse` | `http_parse(raw: string) -> HttpParsed` | C hot-path request parse (method/path/host/headers/body) |
| `http_parsed_ok` | `http_parsed_ok(r) -> int` | `1` if method+path parsed |
| `http_parsed_method` / `path` / `host` / `headers` / `body` | accessors | |
| `http_parsed_content_length` | `…(r) -> int` | `-1` if absent / invalid |
| `http_parsed_chunked` | `…(r) -> int` | `1` if `Transfer-Encoding: chunked` |
| `http_parsed_header` | `http_parsed_header(r, name) -> string` | Case-insensitive single header |
| `http_decode_chunked` | `http_decode_chunked(chunked_body: string) -> string` | Decode a chunked body; incomplete/malformed → `""` |

### Reverse-proxy notes (edge cases)

**Pool (`tcp_pool_*`)**

- Global pool table is **mutex-protected** (`pthread_mutex`) for multi-crew / multi-kick use.
- `release(..., reusable=1)` validates the fd with a **nonblocking** probe (never waits on `SO_RCVTIMEO`); probe runs outside the pool lock.
- Closed peer / unexpected buffered data → fd is closed, not returned to idle.
- Bad host/port, empty host, or CR/LF in host → `open` returns `-1`.
- `max` connections: further `acquire` returns `-1` until a fd is released.
- Double `close` is safe (`0` on already-closed).

**`http_forward_full` / `http_forward_fd`**

- Builds the request with Host + Content-Length unless the caller already supplied them.
- Normalizes caller header blocks that omit a trailing `\r\n`.
- Rejects method/path/host containing CR/LF (request-smuggling seed).
- Reads body by **Content-Length**, **chunked** (extensions + trailers), or connection close.
- Statuses **1xx / 204 / 304** → empty body.
- Chunked incomplete on EOF → failure (`ok=0`).
- Max response size 16 MiB.

**`http_parse` / `http_decode_chunked`**

- Accepts CRLF or bare LF headers.
- Truncates body to Content-Length when the buffer is longer.
- Case-insensitive header names; trims trailing SP/HTAB on values.
- Incomplete headers still yield method/path with `ok=1` and empty body.
- Incomplete/malformed chunked → empty body string.

**`http_proxy_raw`**

- Same-fd client/backend is refused.
- Empty request → `ok=0`.

Tests: `examples/testing/proxy_pool_test.mko`, `examples/testing/proxy_edge_test.mko`.

### UDP

| Function | Signature | Description |
|----------|-----------|-------------|
| `udp_bind` | `udp_bind(port: int) -> int` | Bind UDP on all interfaces (`port` 0 = ephemeral) |
| `udp_bind_addr` | `udp_bind_addr(host: string, port: int) -> int` | Bind UDP to a specific host |
| `udp_send_to` | `udp_send_to(fd: int, host: string, port: int, data: string) -> int` | Send UDP datagram |
| `udp_recv` / `udp_recv_from` | `udp_recv(fd: int, max_bytes: int) -> string` | Receive; records last sender |
| `udp_last_sender_host` | `udp_last_sender_host() -> string` | Host of last UDP peer |
| `udp_last_sender_port` | `udp_last_sender_port() -> int` | Port of last UDP peer |
| `udp_last_sender` | `udp_last_sender() -> string` | `"host:port"` of last UDP peer |
| `udp_local_port` | `udp_local_port(fd: int) -> int` | Get the local port of a UDP socket |
| `udp_close` | `udp_close(fd: int) -> int` | Close a UDP socket |

### Unix Sockets

| Function | Signature | Description |
|----------|-----------|-------------|
| `unix_socket_pair` | `unix_socket_pair() -> int` | Create a Unix socket pair (returns first fd) |
| `unix_socket_pair_peer` | `unix_socket_pair_peer() -> int` | Get the peer fd of the last socket pair |
| `unix_write` | `unix_write(fd: int, data: string) -> int` | Write to a Unix socket |
| `unix_read` | `unix_read(fd: int, max: int) -> string` | Read from a Unix socket |
| `unix_close` | `unix_close(fd: int) -> int` | Close a Unix socket |

---

## 37. TLS

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_hs_reset` | `tls_hs_reset() -> int` | Reset the TLS handshake state machine |
| `tls_hs_state` | `tls_hs_state() -> int` | Get the current TLS handshake state |
| `tls_hs_advance` | `tls_hs_advance(msg: string) -> int` | Advance the handshake with a message |
| `tls_hs_is_app` | `tls_hs_is_app() -> int` | Check if handshake is complete (app data ready) |
| `tls_serve` | `tls_serve(port: int, cert: string, key: string, handler: string) -> int` | Start a TLS server |
| `tls_serve_once` | `tls_serve_once(port: int, cert: string, key: string, response: string) -> int` | Serve one TLS request |
| `tls_serve_n` | `tls_serve_n(port: int, cert: string, key: string, response: string, n: int) -> int` | Serve n TLS requests |
| `tls_listen_stub` | `tls_listen_stub(port: int) -> int` | Stub TLS listener |
| `tls_get_insecure` | `tls_get_insecure(host: string, port: int, path: string) -> string` | TLS GET without certificate verification |
| `tls_get` | `tls_get(host: string, port: int, path: string, ca: string) -> string` | TLS GET with CA certificate |
| `tls_post` | `tls_post(host: string, port: int, path: string, ca: string, body: string) -> string` | TLS POST with CA certificate |
| `tls_handshake_ok` | `tls_handshake_ok(host: string, port: int, ca: string) -> string` | Test TLS handshake |
| `tls_handshake_version` | `tls_handshake_version(host: string, port: int, ca: string) -> string` | Get negotiated TLS version |

### Socket-style TLS server

A blocking, socket-style API for terminating TLS on an accepted TCP fd (also
supports STARTTLS-style upgrades). ALPN prefers `http/1.1` (see runtime notes).
Requires OpenSSL; `tls_server_available()` reports 1 when present.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_server_available` | `tls_server_available() -> int` | Whether the TLS server backend is available (1/0) |
| `tls_server_new` | `tls_server_new(cert: string, key: string) -> TlsServer` | Create a TLS server context (min TLS 1.2) |
| `tls_server_new_tls13` | `tls_server_new_tls13(cert: string, key: string) -> TlsServer` | Create a TLS server that requires TLS 1.3 (rejects older clients) |
| `tls_accept` | `tls_accept(srv: TlsServer, fd: int) -> TlsConn` | Blocking TLS handshake on an accepted TCP fd |
| `tls_accept_start` | `tls_accept_start(srv: TlsServer, fd: int) -> TlsConn` | Nonblocking TLS accept start (handshake may be incomplete) |
| `tls_handshake_step` | `tls_handshake_step(conn: TlsConn) -> int` | Drive handshake: `1` done, `0` want-read, `2` want-write, `-1` error |
| `tls_is_init_finished` | `tls_is_init_finished(conn: TlsConn) -> int` | Handshake complete? |
| `tls_want_read` / `tls_want_write` | `…(conn) -> int` | Event-loop interest flags |
| `tls_conn_fd` | `tls_conn_fd(conn: TlsConn) -> int` | Underlying TCP fd for poll/epoll |
| `tls_read_nb` / `tls_write_nb` | nonblocking TLS I/O | Empty / `0` on want-read/write |

Use `tls_accept_start` + `tls_handshake_step` (or poll on `tls_conn_fd` with want-read/write) so the accept loop is not blocked by slow handshakes. Requires OpenSSL (`tls_server_available()`).
| `tls_read` | `tls_read(conn: TlsConn, max: int) -> string` | Read decrypted bytes (empty on close) |
| `tls_write` | `tls_write(conn: TlsConn, data: string) -> int` | Write plaintext (encrypted on the wire); bytes written or -1 |
| `tls_conn_alpn` | `tls_conn_alpn(conn: TlsConn) -> string` | Negotiated ALPN protocol (e.g. `"h2"`) |
| `tls_conn_version` | `tls_conn_version(conn: TlsConn) -> string` | Negotiated version (`TLSv1.3`, …) |
| `tls_peer_cn` | `tls_peer_cn(conn: TlsConn) -> string` | Peer certificate CN (or `""`) |
| `tls_conn_close` | `tls_conn_close(conn: TlsConn) -> int` | Close a TLS connection |
| `tls_server_free` | `tls_server_free(srv: TlsServer) -> int` | Free a TLS server context |

### Socket-style TLS client

Mirror of the server API for **outbound** TLS (custom protocols, SIPS, mTLS apps).
`tcp_connect` first, then `tls_connect(cli, fd, sni_host)`. Same `TlsConn` for
read/write/close. Prefer `tls_client_new(ca_pem)` (VERIFY_PEER) over
`tls_client_new_insecure` (demos only).

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_client_available` | `tls_client_available() -> int` | OpenSSL client backend present (1/0) |
| `tls_client_new` | `tls_client_new(ca_pem: string) -> TlsClient` | Client ctx; verify peer against CA PEM |
| `tls_client_new_insecure` | `tls_client_new_insecure() -> TlsClient` | Client ctx; **no** cert verify (dev only) |
| `tls_client_free` | `tls_client_free(cli: TlsClient) -> int` | Free client context |
| `tls_connect` | `tls_connect(cli: TlsClient, fd: int, host: string) -> TlsConn` | Blocking handshake + SNI |
| `tls_connect_start` | `tls_connect_start(cli: TlsClient, fd: int, host: string) -> TlsConn` | Nonblocking handshake start |

Drive both sides with `tls_handshake_step` when using `*_start` (blocking
server accept + client connect on the same thread deadlocks).

### TLS Crypto Primitives

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_aead_seal` | `tls_aead_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | AEAD encrypt |
| `tls_aead_open` | `tls_aead_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | AEAD decrypt |
| `tls_record_type` | `tls_record_type(record: string) -> int` | Get TLS record type |
| `tls_record_version` | `tls_record_version(record: string) -> int` | Get TLS record version |
| `tls_record_len` | `tls_record_len(record: string) -> int` | Get TLS record length |
| `tls_record_appdata_seal` | `tls_record_appdata_seal(key: string, iv: string, plaintext: string) -> string` | Seal app data record |
| `tls_record_appdata_open` | `tls_record_appdata_open(key: string, iv: string, ciphertext: string) -> string` | Open app data record |
| `tls_record_seq_reset` | `tls_record_seq_reset() -> int` | Reset sequence numbers |
| `tls_record_write_seq` | `tls_record_write_seq() -> int` | Get write sequence number |
| `tls_record_read_seq` | `tls_record_read_seq() -> int` | Get read sequence number |
| `tls_record_appdata_seal_seq` | `tls_record_appdata_seal_seq(key: string, iv: string, plaintext: string) -> string` | Seal with auto-incrementing sequence |
| `tls_record_appdata_open_seq` | `tls_record_appdata_open_seq(key: string, iv: string, ciphertext: string) -> string` | Open with auto-incrementing sequence |

### TLS 1.3 Handshake Messages

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_client_hello` | `tls_client_hello(random: string) -> string` | Build a ClientHello message |
| `tls_client_hello_legacy_version` | `tls_client_hello_legacy_version(ch: string) -> int` | Parse legacy version from ClientHello |
| `tls_client_hello_random` | `tls_client_hello_random(ch: string) -> string` | Extract random from ClientHello |
| `tls_client_hello_has_aes128_gcm` | `tls_client_hello_has_aes128_gcm(ch: string) -> int` | Check for AES-128-GCM cipher suite |
| `tls_server_hello` | `tls_server_hello(random: string) -> string` | Build a ServerHello message |
| `tls_server_hello_random` | `tls_server_hello_random(sh: string) -> string` | Extract random from ServerHello |
| `tls_certificate` | `tls_certificate(cert: string) -> string` | Build a Certificate message |
| `tls_certificate_der` | `tls_certificate_der(msg: string) -> string` | Extract DER from Certificate message |
| `tls_certificate_verify` | `tls_certificate_verify(scheme: int, sig: string) -> string` | Build a CertificateVerify message |
| `tls_certificate_verify_scheme` | `tls_certificate_verify_scheme(msg: string) -> int` | Get signature scheme |
| `tls_certificate_verify_sig` | `tls_certificate_verify_sig(msg: string) -> string` | Get signature bytes |
| `tls_encrypted_extensions` | `tls_encrypted_extensions() -> string` | Build an EncryptedExtensions message |
| `tls_finished` | `tls_finished(verify_data: string) -> string` | Build a Finished message |
| `tls_hs_msg_type` | `tls_hs_msg_type(msg: string) -> int` | Get handshake message type |
| `tls_finished_verify_data` | `tls_finished_verify_data(secret: string, transcript: string) -> string` | Compute verify data |
| `tls_finished_verify_data_hex` | `tls_finished_verify_data_hex(secret: string, transcript: string) -> string` | Compute verify data as hex |

### TLS Key Derivation

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_derive_secret` | `tls_derive_secret(secret: string, label: string, hash: string) -> string` | Derive a TLS 1.3 secret |
| `tls_derive_secret_hex` | `tls_derive_secret_hex(secret: string, label: string, hash: string) -> string` | Derive secret as hex |
| `tls_client_handshake_traffic_secret` | `tls_client_handshake_traffic_secret(shared: string, hash: string) -> string` | Derive client handshake traffic secret |
| `tls_server_handshake_traffic_secret` | `tls_server_handshake_traffic_secret(shared: string, hash: string) -> string` | Derive server handshake traffic secret |
| `tls_client_handshake_traffic_secret_hex` | `tls_client_handshake_traffic_secret_hex(shared: string, hash: string) -> string` | Derive client handshake secret as hex |
| `tls_server_handshake_traffic_secret_hex` | `tls_server_handshake_traffic_secret_hex(shared: string, hash: string) -> string` | Derive server handshake secret as hex |
| `tls_client_application_traffic_secret` | `tls_client_application_traffic_secret(shared: string, hash: string) -> string` | Derive client app traffic secret |
| `tls_server_application_traffic_secret` | `tls_server_application_traffic_secret(shared: string, hash: string) -> string` | Derive server app traffic secret |
| `tls_client_application_traffic_secret_hex` | `tls_client_application_traffic_secret_hex(shared: string, hash: string) -> string` | Derive client app secret as hex |
| `tls_server_application_traffic_secret_hex` | `tls_server_application_traffic_secret_hex(shared: string, hash: string) -> string` | Derive server app secret as hex |

### TLS Transcript

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_transcript_reset` | `tls_transcript_reset() -> int` | Reset transcript hash |
| `tls_transcript_append` | `tls_transcript_append(msg: string) -> int` | Append a message to transcript |
| `tls_transcript_len` | `tls_transcript_len() -> int` | Get transcript length |
| `tls_transcript_finished_hex` | `tls_transcript_finished_hex(secret: string) -> string` | Compute finished verify data from transcript |

### TLS Handshake Session

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_hs_session_reset` | `tls_hs_session_reset() -> int` | Reset handshake session |
| `tls_hs_session_feed` | `tls_hs_session_feed(data: string) -> int` | Feed data to handshake session |
| `tls_hs_session_client_hello` | `tls_hs_session_client_hello(random: string) -> int` | Process ClientHello |
| `tls_hs_session_server_hello` | `tls_hs_session_server_hello(random: string) -> int` | Process ServerHello |
| `tls_hs_session_finished_hex` | `tls_hs_session_finished_hex(secret: string) -> string` | Compute finished data for session |
| `tls_hs_session_encrypted_extensions` | `tls_hs_session_encrypted_extensions() -> int` | Process EncryptedExtensions |
| `tls_hs_session_certificate` | `tls_hs_session_certificate(cert: string) -> int` | Process Certificate |
| `tls_hs_session_certificate_verify` | `tls_hs_session_certificate_verify(scheme: int, sig: string) -> int` | Process CertificateVerify |
| `tls_hs_session_finished` | `tls_hs_session_finished(verify: string, secret: string) -> int` | Process Finished |

---

## 38. HTTP/2

### Frame Construction

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_detect` | `http2_detect(data: string) -> bool` | Detect HTTP/2 preface |
| `http2_headers_frame` | `http2_headers_frame(stream: int, block: string, flags: int) -> string` | Build a HEADERS frame |
| `http2_data_frame` | `http2_data_frame(stream: int, payload: string, flags: int) -> string` | Build DATA frame(s); auto-splits if payload > max frame (default 16384); `END_STREAM` only on last |
| `http2_continuation_frame` | `http2_continuation_frame(stream: int, block: string, flags: int) -> string` | Build a CONTINUATION frame |
| `http2_goaway_frame` | `http2_goaway_frame(last_stream: int, error_code: int) -> string` | Build a GOAWAY frame |
| `http2_ping_frame` | `http2_ping_frame(data: string, ack: int) -> string` | Build a PING frame |
| `http2_window_update_frame` | `http2_window_update_frame(stream: int, increment: int) -> string` | Build a WINDOW_UPDATE frame |
| `http2_rst_stream_frame` | `http2_rst_stream_frame(stream: int, error_code: int) -> string` | Build a RST_STREAM frame |
| `http2_priority_frame` | `http2_priority_frame(stream: int, dep: int, weight: int, exclusive: int) -> string` | Build a PRIORITY frame |
| `http2_push_promise_frame` | `http2_push_promise_frame(stream: int, promised: int, block: string, flags: int) -> string` | Build a PUSH_PROMISE frame |

### Frame Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_frame_type` | `http2_frame_type(frame: string) -> int` | Get frame type |
| `http2_frame_stream` | `http2_frame_stream(frame: string) -> int` | Get frame stream ID |
| `http2_frame_len` | `http2_frame_len(frame: string) -> int` | Get frame payload length |
| `http2_frame_flags` | `http2_frame_flags(frame: string) -> int` | Get frame flags |
| `http2_frame_payload` | `http2_frame_payload(frame: string) -> string` | Extract frame payload |
| `http2_frame_at` | `http2_frame_at(data: string, idx: int) -> string` | Get the i-th frame from a buffer |
| `http2_concat_frames` | `http2_concat_frames(a: string, b: string) -> string` | Concatenate two frames |
| `http2_header_block` | `http2_header_block(frame: string, flags: int) -> string` | Extract header block from frame |
| `http2_is_goaway` | `http2_is_goaway(frame: string) -> bool` | Check if frame is GOAWAY |
| `http2_is_ping` | `http2_is_ping(frame: string) -> bool` | Check if frame is PING |
| `http2_is_window_update` | `http2_is_window_update(frame: string) -> bool` | Check if frame is WINDOW_UPDATE |
| `http2_is_rst_stream` | `http2_is_rst_stream(frame: string) -> bool` | Check if frame is RST_STREAM |
| `http2_is_priority` | `http2_is_priority(frame: string) -> bool` | Check if frame is PRIORITY |
| `http2_is_push_promise` | `http2_is_push_promise(frame: string) -> bool` | Check if frame is PUSH_PROMISE |
| `http2_is_settings_ack` | `http2_is_settings_ack(frame: string) -> bool` | Check if frame is a SETTINGS ACK |
| `http2_goaway_last_stream` | `http2_goaway_last_stream(frame: string) -> int` | Get last stream ID from GOAWAY |
| `http2_goaway_error` | `http2_goaway_error(frame: string) -> int` | Get error code from GOAWAY |
| `http2_window_update_increment` | `http2_window_update_increment(frame: string) -> int` | Get increment from WINDOW_UPDATE |
| `http2_rst_stream_error` | `http2_rst_stream_error(frame: string) -> int` | Get error code from RST_STREAM |
| `http2_push_promise_stream` | `http2_push_promise_stream(frame: string) -> int` | Get promised stream from PUSH_PROMISE |

### Settings & Connection

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_settings_len` | `http2_settings_len(frame: string) -> int` | Get length of settings frame |
| `http2_empty_settings` | `http2_empty_settings() -> string` | Build an empty SETTINGS frame |
| `http2_settings_max_concurrent` | `http2_settings_max_concurrent(max: int) -> string` | Build SETTINGS with max concurrent streams |
| `http2_settings_ack` | `http2_settings_ack() -> string` | Build a SETTINGS ACK frame |
| `http2_client_preface` | `http2_client_preface() -> string` | Get the HTTP/2 client connection preface |
| `http2_server_preface` | `http2_server_preface() -> string` | Get the HTTP/2 server connection preface |

### Connection State Machine

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_conn_reset` | `http2_conn_reset() -> int` | Reset connection state |
| `http2_conn_recv` | `http2_conn_recv(data: string) -> int` | Process received data |
| `http2_conn_send_settings` | `http2_conn_send_settings() -> int` | Send SETTINGS frame |
| `http2_conn_send_settings_ack` | `http2_conn_send_settings_ack() -> int` | Send SETTINGS ACK |
| `http2_conn_settings_ack_needed` | `http2_conn_settings_ack_needed() -> int` | Check if SETTINGS ACK is needed |
| `http2_conn_auto_settings_ack` | `http2_conn_auto_settings_ack() -> string` | Auto-generate SETTINGS ACK if needed |
| `http2_conn_pump` | `http2_conn_pump(data: string) -> string` | Process data and generate response |
| `http2_conn_goaway_last` | `http2_conn_goaway_last() -> int` | Get last stream from received GOAWAY |
| `http2_conn_max_concurrent` | `http2_conn_max_concurrent() -> int` | Get max concurrent streams |
| `http2_conn_active_streams` | `http2_conn_active_streams() -> int` | Get count of active streams |
| `http2_conn_set_server` | `http2_conn_set_server(is_server: int) -> int` | Set connection role |
| `http2_conn_is_server` | `http2_conn_is_server() -> int` | Check if connection is server |
| `http2_conn_preface_received` | `http2_conn_preface_received() -> int` | Check if preface was received |
| `http2_conn_settings_exchanged` | `http2_conn_settings_exchanged() -> int` | Check if settings exchange is complete |
| `http2_conn_closing` | `http2_conn_closing() -> int` | Check if connection is closing |
| `http2_conn_header_block` | `http2_conn_header_block(stream: int) -> string` | Get header block for a stream |
| `http2_conn_header_stream` | `http2_conn_header_stream() -> int` | Get the stream with pending headers |
| `http2_conn_header_assembling` | `http2_conn_header_assembling() -> int` | Check if headers are being assembled |
| `http2_conn_send_goaway` | `http2_conn_send_goaway() -> int` | Mark connection closing (GOAWAY sent by caller) |
| `http2_conn_goaway` | `http2_conn_goaway(error_code: int) -> string` | Build GOAWAY with last stream id; marks closing |
| `http2_conn_initial_window` | `http2_conn_initial_window() -> int` | Peer `SETTINGS_INITIAL_WINDOW_SIZE` |
| `http2_conn_max_frame_size` | `http2_conn_max_frame_size() -> int` | Peer `SETTINGS_MAX_FRAME_SIZE` |
| `http2_conn_header_table_size` | `http2_conn_header_table_size() -> int` | Peer `SETTINGS_HEADER_TABLE_SIZE` |
| `http2_conn_enable_push` | `http2_conn_enable_push() -> int` | Peer `SETTINGS_ENABLE_PUSH` |
| `http2_conn_max_header_list` | `http2_conn_max_header_list() -> int` | Peer `SETTINGS_MAX_HEADER_LIST_SIZE` |
| `http2_conn_unacked` | `http2_conn_unacked() -> int` | Inbound bytes not yet WINDOW_UPDATE'd |

### Flow Control (dual windows)

RFC 7540-style **send** vs **recv** accounting:

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_window_of` | `http2_window_of(stream: int) -> int` | **Send** window (how much DATA we may still send) |
| `http2_window_conn` | `http2_window_conn() -> int` | Connection **send** window |
| `http2_window_blocked` | `http2_window_blocked(stream: int) -> int` | `1` if send window is 0 |
| `http2_window_consume` | `http2_window_consume(stream: int, amount: int) -> int` | Spend send window (outbound DATA) |
| `http2_window_increment` | `http2_window_increment(stream: int, amount: int) -> int` | Peer WINDOW_UPDATE → raise **send** |
| `http2_recv_window_of` | `http2_recv_window_of(stream: int) -> int` | **Recv** window (how much peer may still send) |
| `http2_recv_window_conn` | `http2_recv_window_conn() -> int` | Connection **recv** window |

Inbound DATA spends **recv** windows. Peer `WINDOW_UPDATE` raises **send**.  
`http2_conn_pump` auto-emits SETTINGS ACK, PING ACK, and WINDOW_UPDATE (restoring
**recv** only) when unacked inbound DATA reaches 16 KiB.  
`http2_response*` spends send windows for the body when the stream is open.

### Stream State

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_stream_reset` | `http2_stream_reset() -> int` | Reset stream state |
| `http2_stream_id` | `http2_stream_id() -> int` | Get current stream ID |
| `http2_stream_state` | `http2_stream_state() -> int` | Get current stream state |
| `http2_stream_state_of` | `http2_stream_state_of(stream: int) -> int` | Get state of a specific stream |
| `http2_stream_apply` | `http2_stream_apply(frame: string) -> int` | Apply a frame to stream state |
| `http2_stream_apply_local` | `http2_stream_apply_local(frame: string) -> int` | Apply a locally-sent frame |
| `http2_stream_half_closed_remote` | `http2_stream_half_closed_remote(stream: int) -> int` | Check if remote half is closed |
| `http2_stream_half_closed_local` | `http2_stream_half_closed_local(stream: int) -> int` | Check if local half is closed |
| `http2_ready_streams` | `http2_ready_streams() -> int` | Count streams with complete HEADERS not yet taken |
| `http2_next_ready_stream` | `http2_next_ready_stream() -> int` | Next untaken ready stream id, or `-1` |
| `http2_stream_take` | `http2_stream_take(stream: int) -> int` | Mark stream taken by a worker (`1` if found) |
| `http2_stream_body` | `http2_stream_body(stream: int) -> string` | Accumulated DATA body for stream |
| `http2_stream_body_len` | `http2_stream_body_len(stream: int) -> int` | Body byte count (`-1` if unknown stream) |
| `http2_stream_body_done` | `http2_stream_body_done(stream: int) -> int` | `1` if END_STREAM seen on DATA |
| `http2_response` | `http2_response(stream, status, body) -> string` | HEADERS `:status` + `content-length` + DATA frame(s) (auto-split) |
| `http2_response_ct` | `http2_response_ct(stream, status, content_type, body) -> string` | Same + `content-type` |

Up to **64 concurrent stream slots** per connection (64 KiB body buffer each,
16 KiB header-block assembly). PADDED and PRIORITY flags on HEADERS/DATA are
stripped before HPACK/body accumulation. Closed streams reclaim slots for
long-lived connections. Full SETTINGS (header table, push, max concurrent,
initial window, max frame, max header list) are parsed from the peer.

### Priority

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_priority_dep` | `http2_priority_dep(frame: string) -> int` | Get dependency stream from PRIORITY |
| `http2_priority_weight` | `http2_priority_weight(frame: string) -> int` | Get weight from PRIORITY |
| `http2_priority_exclusive` | `http2_priority_exclusive(frame: string) -> int` | Get exclusive flag from PRIORITY |
| `http2_priority_apply` | `http2_priority_apply(frame: string) -> int` | Apply priority to the tree |
| `http2_stream_priority_dep` | `http2_stream_priority_dep(stream: int) -> int` | Get dependency of a stream |
| `http2_stream_priority_weight` | `http2_stream_priority_weight(stream: int) -> int` | Get weight of a stream |
| `http2_stream_priority_exclusive` | `http2_stream_priority_exclusive(stream: int) -> int` | Get exclusive flag of a stream |
| `http2_stream_priority_child_count` | `http2_stream_priority_child_count(stream: int) -> int` | Get number of child streams |
| `http2_schedule_next` | `http2_schedule_next() -> int` | Schedule next stream for sending |

### TLS + HTTP/2

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_serve_once_h2` | `tls_serve_once_h2(port: int, cert: string, key: string, response: string) -> int` | Serve one TLS+HTTP/2 request |
| `tls_h2_settings_exchange` | `tls_h2_settings_exchange(host: string, port: int, ca: string) -> string` | Perform H2 settings exchange over TLS |
| `tls_h2_get` | `tls_h2_get(host: string, port: int, ca: string, path: string) -> string` | HTTP/2 GET over TLS |
| `tls_h2_post` | `tls_h2_post(host: string, port: int, ca: string, path: string, body: string) -> string` | HTTP/2 POST over TLS |
| `tls_h2_get_twice` | `tls_h2_get_twice(host: string, port: int, ca: string, path: string) -> string` | Two multiplexed HTTP/2 GETs over TLS |
| `tls_h2_mux` | `tls_h2_mux(host: string, port: int, ca: string) -> string` | HTTP/2 multiplexing test over TLS |
| `tls_serve_h2_wu` | `tls_serve_h2_wu(port: int, cert: string, key: string, response: string, window: int) -> int` | Serve H2 with window update |
| `tls_h2_window_get` | `tls_h2_window_get(host: string, port: int, ca: string, path: string) -> string` | H2 GET with flow control |
| `tls_serve_h2_n` | `tls_serve_h2_n(port: int, cert: string, key: string, response: string, n: int) -> int` | Serve n HTTP/2 requests over TLS |
| `tls_serve_h2_routes` | `tls_serve_h2_routes(port: int, cert: string, key: string, routes: string, responses: string, n: int) -> int` | Serve HTTP/2 with multiple routes |

---

## 39. HPACK (HTTP/2 Header Compression)

| Function | Signature | Description |
|----------|-----------|-------------|
| `hpack_encode_indexed` | `hpack_encode_indexed(idx: int) -> string` | Encode an indexed header |
| `hpack_decode_indexed` | `hpack_decode_indexed(data: string) -> int` | Decode an indexed header |
| `hpack_static_name` | `hpack_static_name(idx: int) -> string` | Get name from static table |
| `hpack_static_value` | `hpack_static_value(idx: int) -> string` | Get value from static table |
| `hpack_encode_literal` | `hpack_encode_literal(name: string, value: string) -> string` | Encode a literal header |
| `hpack_literal_name` | `hpack_literal_name(data: string) -> string` | Decode literal header name |
| `hpack_literal_value` | `hpack_literal_value(data: string) -> string` | Decode literal header value |
| `hpack_decode_stub` | `hpack_decode_stub(data: string) -> int` | Stub HPACK decoder |
| `hpack_decode_block` | `hpack_decode_block(block: string) -> int` | Decode a complete header block |
| `hpack_decoded_count` | `hpack_decoded_count() -> int` | Number of decoded headers |
| `hpack_decoded_name` | `hpack_decoded_name(idx: int) -> string` | Get decoded header name |
| `hpack_decoded_value` | `hpack_decoded_value(idx: int) -> string` | Get decoded header value |
| `hpack_decode_clear` | `hpack_decode_clear() -> int` | Clear decoded headers |
| `hpack_dyn_insert` | `hpack_dyn_insert(name: string, value: string) -> int` | Insert into dynamic table |
| `hpack_dyn_name` | `hpack_dyn_name() -> string` | Get last inserted dynamic name |
| `hpack_dyn_value` | `hpack_dyn_value() -> string` | Get last inserted dynamic value |
| `hpack_dyn_clear` | `hpack_dyn_clear() -> int` | Clear dynamic table |
| `hpack_dyn_len` | `hpack_dyn_len() -> int` | Get dynamic table size |
| `hpack_dyn_name_at` | `hpack_dyn_name_at(idx: int) -> string` | Get name at dynamic table index |
| `hpack_dyn_value_at` | `hpack_dyn_value_at(idx: int) -> string` | Get value at dynamic table index |
| `hpack_huffman_encode` | `hpack_huffman_encode(data: string) -> string` | Huffman-encode a string |
| `hpack_huffman_decode` | `hpack_huffman_decode(data: string) -> string` | Huffman-decode a string |

---

## 40. QUIC

### Packet Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_detect` | `quic_detect(data: string) -> bool` | Detect a QUIC packet |
| `quic_long_header` | `quic_long_header(data: string) -> bool` | Check for long header |
| `quic_short_header` | `quic_short_header(data: string) -> bool` | Check for short header |
| `quic_version` | `quic_version(pkt: string) -> int` | Get QUIC version |
| `quic_dcid_len` | `quic_dcid_len(pkt: string) -> int` | Get destination connection ID length |
| `quic_dcid` | `quic_dcid(pkt: string) -> string` | Get destination connection ID |
| `quic_scid_len` | `quic_scid_len(pkt: string) -> int` | Get source connection ID length |
| `quic_scid` | `quic_scid(pkt: string) -> string` | Get source connection ID |
| `quic_payload_offset` | `quic_payload_offset(pkt: string) -> int` | Get offset of payload data |
| `quic_spin_bit` | `quic_spin_bit(pkt: string) -> int` | Get the spin bit |
| `quic_key_phase` | `quic_key_phase(pkt: string) -> int` | Get the key phase bit |
| `quic_long_type` | `quic_long_type(pkt: string) -> int` | Get long header packet type |
| `quic_is_retry` | `quic_is_retry(pkt: string) -> bool` | Check if packet is a Retry |
| `quic_is_version_negotiation` | `quic_is_version_negotiation(pkt: string) -> bool` | Check if packet is Version Negotiation |
| `quic_vn_version_count` | `quic_vn_version_count(pkt: string) -> int` | Count offered versions |
| `quic_vn_version_at` | `quic_vn_version_at(pkt: string, idx: int) -> int` | Get a specific offered version |
| `quic_vn_select` | `quic_vn_select(pkt: string, preferred: int) -> int` | Select a preferred version |
| `quic_stub` | `quic_stub(port: int) -> int` | Stub QUIC endpoint |

### QUIC Crypto Frames

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_has_crypto` | `quic_has_crypto(pkt: string) -> bool` | Check if packet has CRYPTO frame |
| `quic_crypto_offset` | `quic_crypto_offset(pkt: string) -> int` | Get CRYPTO frame offset |
| `quic_crypto_data_offset` | `quic_crypto_data_offset(pkt: string) -> int` | Get CRYPTO data offset |
| `quic_crypto_data_len` | `quic_crypto_data_len(pkt: string) -> int` | Get CRYPTO data length |
| `quic_crypto_data` | `quic_crypto_data(pkt: string) -> string` | Extract CRYPTO data |
| `quic_crypto_frame` | `quic_crypto_frame(data: string, offset: int) -> string` | Build a CRYPTO frame |
| `quic_crypto_payload` | `quic_crypto_payload(data: string, offset: int, length: int) -> string` | Extract CRYPTO payload |
| `quic_payload_crypto_data` | `quic_payload_crypto_data(payload: string) -> string` | Extract CRYPTO data from payload |
| `quic_payload_crypto_data_len` | `quic_payload_crypto_data_len(payload: string) -> int` | Get CRYPTO data length from payload |

### QUIC ACK & Stream Frames

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_ack_frame` | `quic_ack_frame(largest: int, delay: int, first_range: int) -> string` | Build an ACK frame |
| `quic_ack_largest` | `quic_ack_largest(frame: string) -> int` | Get largest acknowledged packet |
| `quic_ack_delay` | `quic_ack_delay(frame: string) -> int` | Get ACK delay |
| `quic_ack_range_count` | `quic_ack_range_count(frame: string) -> int` | Get ACK range count |
| `quic_ack_first_range` | `quic_ack_first_range(frame: string) -> int` | Get first ACK range |
| `quic_ack_smallest` | `quic_ack_smallest(frame: string) -> int` | Get smallest acknowledged packet |
| `quic_is_ack` | `quic_is_ack(frame: string) -> int` | Check if frame is ACK |
| `quic_stream_frame` | `quic_stream_frame(id: int, offset: int, data: string, fin: int) -> string` | Build a STREAM frame |
| `quic_is_stream` | `quic_is_stream(frame: string) -> int` | Check if frame is STREAM |
| `quic_stream_fin` | `quic_stream_fin(frame: string) -> int` | Check FIN flag |
| `quic_stream_id_of` | `quic_stream_id_of(frame: string) -> int` | Get stream ID |
| `quic_stream_offset` | `quic_stream_offset(frame: string) -> int` | Get stream offset |
| `quic_stream_data_len` | `quic_stream_data_len(frame: string) -> int` | Get stream data length |
| `quic_stream_data` | `quic_stream_data(frame: string) -> string` | Get stream data |

### QUIC Initial Protection

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_hkdf_expand_label` | `quic_hkdf_expand_label(secret: string, label: string, length: int) -> string` | HKDF-Expand-Label |
| `quic_hkdf_expand_label_hex` | `quic_hkdf_expand_label_hex(secret: string, label: string, length: int) -> string` | HKDF-Expand-Label as hex |
| `quic_initial_client_secret` | `quic_initial_client_secret(dcid: string) -> string` | Derive client initial secret |
| `quic_initial_client_secret_hex` | `quic_initial_client_secret_hex(dcid: string) -> string` | Derive client initial secret as hex |
| `quic_initial_client_key` | `quic_initial_client_key(dcid: string) -> string` | Derive client initial key |
| `quic_initial_client_iv` | `quic_initial_client_iv(dcid: string) -> string` | Derive client initial IV |
| `quic_initial_client_key_hex` | `quic_initial_client_key_hex(dcid: string) -> string` | Derive client initial key as hex |
| `quic_initial_client_iv_hex` | `quic_initial_client_iv_hex(dcid: string) -> string` | Derive client initial IV as hex |
| `quic_initial_client_hp` | `quic_initial_client_hp(dcid: string) -> string` | Derive client header protection key |
| `quic_initial_client_hp_hex` | `quic_initial_client_hp_hex(dcid: string) -> string` | Derive client HP key as hex |
| `quic_initial_protect` | `quic_initial_protect(pkt: string, pn_len: int, key: string, iv: string) -> string` | Apply initial protection |
| `quic_initial_unprotect` | `quic_initial_unprotect(pkt: string, pn_len: int, key: string, iv: string) -> string` | Remove initial protection |
| `quic_header_protection_mask` | `quic_header_protection_mask(hp_key: string, sample: string) -> string` | Compute HP mask |
| `quic_header_protection_mask_hex` | `quic_header_protection_mask_hex(hp_key: string, sample: string) -> string` | Compute HP mask as hex |
| `quic_initial_hp_mask` | `quic_initial_hp_mask(hp_key: string, sample: string) -> string` | Compute initial HP mask |
| `quic_initial_hp_mask_hex` | `quic_initial_hp_mask_hex(hp_key: string, sample: string) -> string` | Compute initial HP mask as hex |
| `quic_header_protect_apply` | `quic_header_protect_apply(header: string, pn_offset: int, mask: string) -> string` | Apply header protection |
| `quic_header_protect_remove` | `quic_header_protect_remove(header: string, pn_offset: int, mask: string) -> string` | Remove header protection |
| `quic_initial_packet_protect` | `quic_initial_packet_protect(pkt: string, pn_offset: int, key: string, pn: int, hp_key: string) -> string` | Full packet protection |
| `quic_initial_packet_unprotect` | `quic_initial_packet_unprotect(pkt: string, key: string, pn_offset: int, pn_len: int) -> string` | Full packet unprotection |

### QUIC Libraries

| Function | Signature | Description |
|----------|-----------|-------------|
| `quiche_available` | `quiche_available() -> int` | Check if quiche is available |
| `quiche_version` | `quiche_version() -> string` | Get quiche version |
| `quiche_handshake` | `quiche_handshake(host: string, port: int, ca: string, timeout: int) -> string` | Perform QUIC handshake via quiche |
| `quiche_h3_get` | `quiche_h3_get(host: string, port: int, ca: string, path: string, timeout: int) -> string` | HTTP/3 GET via quiche |
| `quiche_h3_post` | `quiche_h3_post(host: string, port: int, ca: string, path: string, body: string, timeout: int) -> string` | HTTP/3 POST via quiche |
| `quiche_h3_get_two` | `quiche_h3_get_two(host: string, port: int, ca: string, path1: string, path2: string, timeout: int) -> string` | Two multiplexed HTTP/3 GETs |
| `quiche_start_server` | `quiche_start_server(port: int, cert: string, key: string, ca: string, response: string) -> int` | Start a QUIC server |
| `quiche_stop_server` | `quiche_stop_server(handle: int) -> int` | Stop a QUIC server |
| `h3_server_available` | `h3_server_available() -> int` | H3/UDP surface available (`1` when quiche linked) |
| `h3_server_new` | `h3_server_new(cert: string, key: string) -> int` | Create H3 server handle (cert/key paths) |
| `h3_server_bind` | `h3_server_bind(handle, host, port) -> int` | Bind UDP for QUIC |
| `h3_server_fd` | `h3_server_fd(handle) -> int` | UDP fd for event-loop registration |
| `h3_server_poll` | `h3_server_poll(handle, timeout_ms) -> int` | `1` readable, `0` timeout, `-1` error |
| `h3_accept_stream` | `h3_accept_stream(handle) -> int` | Next ready stream id (or `-1`); POST/PUT/PATCH wait for FIN |
| `h3_stream_read` | `h3_stream_read(handle, stream) -> string` | Pseudo-HTTP/1.1 request line + headers + body |
| `h3_stream_write` | `h3_stream_write(handle, stream, data) -> int` | Response; optional `"STATUS\n"` prefix; default `text/plain` |
| `h3_stream_method` / `path` / `body` / `authority` | accessors | Request fields for the accepted stream |
| `h3_response` | `h3_response(handle, stream, status, content_type, body) -> int` | Structured response with content-type |
| `h3_server_close` | `h3_server_close(handle) -> int` | Close server and UDP fd |

Production H3 server: up to **32** concurrent QUIC connections and **64** ready
requests; **64 KiB** body buffer per request. Requires `MAKO_HAS_QUICHE`.
Example: `examples/h3_server.mko` · smoke: `./scripts/h3-server-smoke.sh`.
| `nghttp2_available` | `nghttp2_available() -> int` | Check if nghttp2 is available |
| `nghttp2_get` | `nghttp2_get(host: string, port: int, ca: string, path: string) -> string` | HTTP/2 GET via nghttp2 |
| `nghttp2_post` | `nghttp2_post(host: string, port: int, ca: string, path: string, body: string) -> string` | HTTP/2 POST via nghttp2 |
| `nghttp2_get_two` | `nghttp2_get_two(host: string, port: int, ca: string, path1: string, path2: string) -> string` | Two multiplexed HTTP/2 GETs via nghttp2 |

---

## 41. Protobuf

| Function | Signature | Description |
|----------|-----------|-------------|
| `pb_encode_varint` | `pb_encode_varint(val: int) -> string` | Encode a varint |
| `pb_decode_varint` | `pb_decode_varint(data: string) -> int` | Decode a varint |
| `pb_varint_len` | `pb_varint_len(data: string) -> int` | Get encoded varint length |
| `pb_encode_key` | `pb_encode_key(field: int, wire_type: int) -> string` | Encode a field key |
| `pb_key_field` | `pb_key_field(data: string) -> int` | Get field number from key |
| `pb_key_wire` | `pb_key_wire(data: string) -> int` | Get wire type from key |
| `pb_zigzag_encode` | `pb_zigzag_encode(val: int) -> int` | ZigZag encode a signed integer |
| `pb_zigzag_decode` | `pb_zigzag_decode(val: int) -> int` | ZigZag decode to signed integer |
| `pb_encode_sint` | `pb_encode_sint(val: int) -> string` | Encode a signed integer |
| `pb_decode_sint` | `pb_decode_sint(data: string) -> int` | Decode a signed integer |
| `pb_encode_bytes` | `pb_encode_bytes(data: string) -> string` | Encode length-delimited bytes |
| `pb_bytes_len` | `pb_bytes_len(data: string) -> int` | Get length of encoded bytes |
| `pb_encode_field_varint` | `pb_encode_field_varint(field: int, val: int) -> string` | Encode a complete varint field |
| `pb_encode_simple` | `pb_encode_simple(name: string, id: int) -> string` | Encode a simple message |
| `pb_simple_name` | `pb_simple_name(data: string) -> string` | Get name from simple message |
| `pb_simple_id` | `pb_simple_id(data: string) -> int` | Get ID from simple message |
| `pb_encode_nested` | `pb_encode_nested(name: string, id: int, inner: string, inner_id: int) -> string` | Encode a nested message |
| `pb_nested_inner` | `pb_nested_inner(data: string) -> string` | Extract inner message |
| `pb_encode_repeated_varint` | `pb_encode_repeated_varint(field: int, val1: int, val2: int) -> string` | Encode repeated varint field |
| `pb_repeated_count` | `pb_repeated_count(data: string, field: int) -> int` | Count repeated field occurrences |
| `pb_repeated_at` | `pb_repeated_at(data: string, field: int, idx: int) -> int` | Get repeated field value at index |

---

## 42. WebSocket (RFC 6455)

Production frame I/O: masking (client→server), 7/16/64-bit lengths (cap 16 MiB),
fragment reassembly, auto-pong on ping, close codes. Server APIs send **unmasked**;
client APIs send **masked**. WSS = compose `tls_*` + these primitives.

| Function | Signature | Description |
|----------|-----------|-------------|
| `ws_accept_key` | `ws_accept_key(client_key: string) -> string` | Compute `Sec-WebSocket-Accept` (SHA-1 + base64) |
| `ws_upgrade_request_ok` | `ws_upgrade_request_ok(request: string) -> int` | Validate upgrade (Upgrade/Connection/Key/Version 13) |
| `ws_client_request` | `ws_client_request(host: string, path: string, key: string) -> string` | Build client HTTP upgrade request |
| `ws_client_accept_ok` | `ws_client_accept_ok(key: string, response: string) -> int` | Validate 101 Accept header for key |
| `ws_accept` | `ws_accept(listen_fd: int) -> int` | Accept TCP + perform server upgrade handshake |
| `ws_recv` | `ws_recv(conn: int, max_bytes: int) -> string` | Server: recv full message (masked); `""` on close/err |
| `ws_last_opcode` | `ws_last_opcode() -> int` | Opcode of last data message (1 text, 2 binary, 8 close) |
| `ws_last_fin` | `ws_last_fin() -> int` | 1 if last frame had FIN set |
| `ws_last_close_code` | `ws_last_close_code() -> int` | Close status code from last close frame (0 if none) |
| `ws_last_status` | `ws_last_status() -> int` | 0 ok, -1 err, -2 close, -3 ping handled, -4 pong |
| `ws_send_text` | `ws_send_text(conn: int, data: string) -> int` | Server: unmasked text frame (0 ok, -1 err) |
| `ws_send_binary` | `ws_send_binary(conn: int, data: string) -> int` | Server: unmasked binary frame |
| `ws_send_ping` | `ws_send_ping(conn: int, data: string) -> int` | Server: unmasked ping (payload ≤ 125) |
| `ws_send_pong` | `ws_send_pong(conn: int, data: string) -> int` | Server: unmasked pong (payload ≤ 125) |
| `ws_send_close` | `ws_send_close(conn: int, code: int, reason: string) -> int` | Server: unmasked close |
| `ws_close` | `ws_close(conn: int) -> int` | Close underlying socket (1 ok) |
| `ws_echo_stub` | `ws_echo_stub(port: int) -> int` | Alias of `ws_echo_once` |
| `ws_echo_once` | `ws_echo_once(port: int) -> int` | Bind port, accept one, echo one message, close |
| `ws_echo` | `ws_echo(port: int) -> int` | Bind port, forever echo loop (blocks) |
| `ws_client_connect` | `ws_client_connect(host: string, port: int, path: string, key: string) -> int` | TCP + upgrade; Happy Eyeballs connect |
| `ws_client_recv` | `ws_client_recv(conn: int, max_bytes: int) -> string` | Client: recv unmasked server message |
| `ws_client_send_text` | `ws_client_send_text(conn: int, data: string) -> int` | Client: masked text |
| `ws_client_send_binary` | `ws_client_send_binary(conn: int, data: string) -> int` | Client: masked binary |
| `ws_client_send_ping` | `ws_client_send_ping(conn: int, data: string) -> int` | Client: masked ping |
| `ws_client_send_close` | `ws_client_send_close(conn: int, code: int, reason: string) -> int` | Client: masked close |

Tests: `examples/testing/ws_api_test.mko` (handshake + loopback e2e).

---

## 43. gRPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `grpc_encode_message` | `grpc_encode_message(payload: string) -> string` | Encode a gRPC message with length prefix |
| `grpc_message_len` | `grpc_message_len(msg: string) -> int` | Get gRPC message length |
| `grpc_message_within_limit` | `grpc_message_within_limit(msg: string, limit: int) -> int` | Check if message is within size limit |
| `grpc_default_max_message` | `grpc_default_max_message() -> int` | Get default max message size |
| `grpc_message_payload` | `grpc_message_payload(msg: string) -> string` | Extract gRPC message payload |
| `grpc_unary_request` | `grpc_unary_request(method: string, id: int) -> string` | Build a unary gRPC request |
| `grpc_unary_name` | `grpc_unary_name(req: string) -> string` | Get method name from unary request |
| `grpc_unary_id` | `grpc_unary_id(req: string) -> int` | Get ID from unary request |
| `grpc_content_type` | `grpc_content_type() -> string` | Get the gRPC content type string |
| `grpc_status_trailer` | `grpc_status_trailer(code: int) -> string` | Build a gRPC status trailer |
| `grpc_status_code` | `grpc_status_code(trailer: string) -> int` | Parse gRPC status code |
| `grpc_http2_unary` | `grpc_http2_unary(stream: int, method: string, id: int) -> string` | Build gRPC unary over HTTP/2 |
| `grpc_http2_unary_payload` | `grpc_http2_unary_payload(frames: string) -> string` | Extract payload from gRPC/H2 frames |
| `grpc_http2_unary_response` | `grpc_http2_unary_response(stream: int, payload: string, status: int) -> string` | Build gRPC unary response over HTTP/2 |
| `grpc_http2_unary_response_status` | `grpc_http2_unary_response_status(stream: int, payload: string, status: int, grpc_status: int) -> string` | Build gRPC response with status |
| `grpc_http2_response_payload` | `grpc_http2_response_payload(frames: string) -> string` | Extract payload from gRPC/H2 response |
| `grpc_http2_response_status` | `grpc_http2_response_status(frames: string) -> int` | Get gRPC status from response |
| `grpc_http2_stream_data` | `grpc_http2_stream_data(stream: int, payload: string, id: int, fin: int) -> string` | Build gRPC stream data |
| `grpc_http2_stream_two` | `grpc_http2_stream_two(stream: int, payload1: string, id1: int, payload2: string, id2: int) -> string` | Build two gRPC stream messages |
| `grpc_http2_stream_data_count` | `grpc_http2_stream_data_count(frames: string, stream: int) -> int` | Count gRPC stream data frames |
| `grpc_http2_client_stream_flow` | `grpc_http2_client_stream_flow(stream: int, p1: string, id1: int, p2: string, id2: int, p3: string, id3: int, window: int) -> string` | Build gRPC client stream with flow control |
| `grpc_stub_ping` | `grpc_stub_ping() -> int` | gRPC stub ping |

### TLS + gRPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_serve_grpc_once` | `tls_serve_grpc_once(port: int, cert: string, key: string) -> int` | Serve one gRPC request over TLS |
| `tls_grpc_unary` | `tls_grpc_unary(host: string, port: int, ca: string, method: string, id: int, payload: string) -> string` | gRPC unary call over TLS |
| `tls_serve_grpc_stream` | `tls_serve_grpc_stream(port: int, cert: string, key: string) -> int` | Serve gRPC stream over TLS |
| `tls_grpc_stream` | `tls_grpc_stream(host: string, port: int, ca: string, method: string, id: int, p1: string, count: int, p2: string) -> string` | gRPC streaming call over TLS |

---

## 44. Email & SMTP

Build MIME messages and send them over SMTP (plain / AUTH PLAIN / STARTTLS).
Packs: `std/net/mail`, `std/net/smtp`. Demo: `examples/send_mail.mko`.

### Message builder (`mail_msg_*`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `mail_msg_new` / `mail_msg_free` | `() -> int` / `(m) -> int` | Message handle |
| `mail_msg_set_from` | `(m, from) -> int` | From (display name allowed) |
| `mail_msg_add_to` / `add_cc` / `add_bcc` | `(m, addr) -> int` | Recipients (Bcc envelope-only) |
| `mail_msg_set_subject` | `(m, subject) -> int` | Subject |
| `mail_msg_set_text` / `set_html` | `(m, body) -> int` | Plain and/or HTML body |
| `mail_msg_add_header` | `(m, name, value) -> int` | Extra header |
| `mail_msg_attach` | `(m, filename, content_type, data) -> int` | Base64 attachment |
| `mail_msg_build` | `(m) -> string` | Full RFC822/MIME bytes |
| `mail_msg_envelope_from` / `rcpt_count` / `rcpt_at` | envelope helpers | |
| `mail_simple` | `(from, to, subject, body) -> string` | One-shot plain message |
| `mail_parse_address` | `(addr) -> string` | `Name <a@b>` → `a@b` |
| `mail_header_get` | `(msg, name) -> string` | Header value |
| `mail_address_ok` | `(addr) -> int` | Basic validation |

### SMTP session (`smtp_*`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `smtp_new` | `(host, port) -> int` | Client handle |
| `smtp_set_timeout_ms` | `(c, ms) -> int` | I/O timeout |
| `smtp_connect` / `smtp_ehlo` | session setup | |
| `smtp_starttls` | `(c) -> int` | STARTTLS (OpenSSL when linked) |
| `smtp_auth` | `(c, user, pass) -> int` | AUTH PLAIN |
| `smtp_mail_from` / `smtp_rcpt_to` / `smtp_data` | envelope + body | Dot-stuffing in DATA |
| `smtp_quit` / `smtp_close` | teardown | |
| `smtp_last_reply` / `smtp_last_code` | last server reply | |
| `smtp_send_built` | `(c, msg_handle) -> int` | MAIL/RCPT/DATA for built msg |
| `smtp_send_msg` | `(host, port, user, pass, msg, use_tls) -> int` | One-shot; tls 0/1/2 |
| `smtp_format_message` / `smtp_send_*` | legacy soft helpers | Still available |
| `smtp_auth_plain` | `(user, pass) -> string` | `AUTH PLAIN …` command line |
| `smtp_starttls_available` | `() -> int` | 1 if OpenSSL linked |
| `smtp_mock_start` | `(port) -> int` | Bind 127.0.0.1 mock SMTP; port `0` = ephemeral; returns port |
| `smtp_mock_serve_once` | `() -> int` | Accept one client; capture DATA (block) |
| `smtp_mock_last_message` / `last_from` / `last_rcpt` | captured mail | |
| `smtp_mock_stop` | `() -> int` | Close mock listener |

TLS: set `MAKO_SMTP_TLS_VERIFY=1` for peer cert verify.  
Program end-to-end: `examples/mail_program.mko` · tests: `mail_smtp_test.mko`.

---

## 45. Logging

| Function | Signature | Description |
|----------|-----------|-------------|
| `log_debug` / `log_info` / `log_warn` / `log_error` | `log_*(msg)` | Stderr logs via strong slog backend (level filter + format) |
| `log_kv` | `log_kv(level, key, value)` | Level + single field (empty msg) |
| `slog_set_level` | `slog_set_level(level: string)` | Min level: `debug`/`info`/`warn`/`error` (default **info**) |
| `slog_get_level` | `slog_get_level() -> int` | 0=debug … 3=error |
| `slog_set_json` / `slog_is_json` | `slog_set_json(1)` | JSON lines vs logfmt |
| `slog_set_service` | `slog_set_service(name)` | Global `service=` / `"service"` field |
| `slog_set_output` | `slog_set_output(path) -> int` | Append file path; `""` → stderr |
| `slog_flush` | `slog_flush()` | Flush current output |
| `slog_debug` / `info` / `warn` / `error` | `slog_*(msg)` | Structured message |
| `slog_with` / `slog_with2` / `slog_with3` | multi string fields | 1–3 key/value pairs |
| `slog_with_int` | `slog_with_int(level, msg, key, n)` | Numeric field (JSON number) |
| `slog_redact` | `slog_redact(value) -> string` | Always `"[REDACTED]"` |
| `slog_with_redacted` | `slog_with_redacted(level, msg, key)` | Field value forced redacted |

Runtime: `runtime/mako_log.h`. ISO-8601 `ts=`, optional `trace=` when active.
Tests: `examples/testing/strong_log_test.mko`.

---

## 46. Metrics

| Function | Signature | Description |
|----------|-----------|-------------|
| `metric_inc` | `metric_inc(id: int) -> void` | Increment a counter metric |
| `metric_add` | `metric_add(id: int, val: int) -> void` | Add to a counter metric |
| `metric_get` | `metric_get(id: int) -> int` | Get a counter metric value |
| `gauge_set` | `gauge_set(id: int, val: int) -> void` | Set a gauge value |
| `gauge_add` | `gauge_add(id: int, val: int) -> void` | Add to a gauge |
| `gauge_get` | `gauge_get(id: int) -> int` | Get a gauge value |
| `hist_observe` | `hist_observe(id: int, val: int) -> void` | Record a histogram observation |
| `hist_count` | `hist_count(id: int) -> int` | Get histogram observation count |
| `hist_sum` | `hist_sum(id: int) -> int` | Get histogram sum |
| `hist_avg` | `hist_avg(id: int) -> int` | Get histogram average |
| `metrics_export` | `metrics_export() -> string` | Export all metrics as text |
| `metrics_export_prom` | `metrics_export_prom() -> string` | Prometheus text exposition |
| `metrics_export_otlp_json` | `metrics_export_otlp_json() -> string` | OTLP/HTTP JSON metrics (seed) |
| `trace_export_json` | `trace_export_json() -> string` | Current trace as OTel-ish JSON |
| `trace_export_otlp_json` | `trace_export_otlp_json() -> string` | OTLP/HTTP JSON spans (seed) |
| `trace_span_id` | `trace_span_id() -> string` | Current span id (16 hex chars) |
| `stack_trace` | `stack_trace() -> string` | Symbolized backtrace |
| `fn_drop` | `fn_drop(f: fn(…)) -> void` | Free capture env (if any) |
| `fn_has_env` | `fn_has_env(f: fn(…)) -> int` | 1 if closure has env |
| `task_done` | `task_done(j: Job) -> int` | 1 if task finished |
| `task_joined` | `task_joined(j: Job) -> int` | 1 if joined |
| `task_id` | `task_id(j: Job) -> int` | Registry id for inspect |
| `tasks_inspect_json` | `tasks_inspect_json() -> string` | Active task snapshot JSON |
| `debug_break` | `debug_break(label: string) -> int` | Soft breakpoint (log + count) |
| `debug_break_hits` | `debug_break_hits() -> int` | Soft breakpoint hit count |
| `debug_break_reset` | `debug_break_reset() -> int` | Reset hit counter |
| `debug_set_int` / `debug_get_int` | named int slots | Debug locals registry |
| `debug_locals_json` | `() -> string` | Locals snapshot |
| `debug_bp_enable` / `debug_bp` / `debug_bp_disable` | soft BP ids 0..15 | |
| `debug_set_loc` / `debug_file` / `debug_line` / `debug_frame_json` | source frame seed | |
| `crash_report_install` | `crash_report_install(path: string) -> int` | Install crash signal handlers |
| `crash_report_installed` | `crash_report_installed() -> int` | 1 if crash report installed |
| `process_rss_bytes` | `process_rss_bytes() -> int` | Process RSS in bytes (−1 if N/A) |
| `process_cpu_user_us` | `process_cpu_user_us() -> int` | User CPU µs |
| `process_cpu_sys_us` | `process_cpu_sys_us() -> int` | System CPU µs |
| `profile_snapshot_json` | `profile_snapshot_json() -> string` | Combined process snapshot JSON |
| `profile_sample_clear` | `() -> int` | Clear sampling ring |
| `profile_sample_once` | `(label: string) -> int` | Cooperative stack sample |
| `profile_sample_start` / `stop` | `(interval_ms) / ()` | SIGPROF sampling when available |
| `profile_sample_count` / `len` | `() -> int` | Total recorded · slots filled |
| `profile_sample_cpu_us` / `wall_ns` | `() -> int` | CPU / wall while active |
| `profile_samples_json` | `() -> string` | Export `mako.profile_samples.v1` |

---

## 47. Validation

| Function | Signature | Description |
|----------|-----------|-------------|
| `validate_required` | `validate_required(val: string) -> int` | Check that a value is non-empty |
| `validate_min_len` | `validate_min_len(val: string, min: int) -> int` | Check minimum string length |
| `validate_max_len` | `validate_max_len(val: string, max: int) -> int` | Check maximum string length |
| `validate_int_range` | `validate_int_range(val: int, min: int, max: int) -> int` | Check integer is in range |
| `validate_email` | `validate_email(val: string) -> int` | Validate email format |

---

## 48. Caching & Scheduling

| Function | Signature | Description |
|----------|-----------|-------------|
| `cache_put` | `cache_put(key: string, val: string, ttl_ms: int) -> int` | Store a value in cache with TTL |
| `cache_get` | `cache_get(key: string) -> string` | Get a cached value |
| `cache_has` | `cache_has(key: string) -> int` | Check if a cache key exists |
| `job_schedule` | `job_schedule(name: string, delay_ms: int) -> int` | Schedule a job |
| `job_due` | `job_due(name: string) -> int` | Check if a job is due |
| `job_delay_ms` | `job_delay_ms(name: string) -> int` | Get remaining delay for a job |
| `job_cancel` | `job_cancel(name: string) -> int` | Cancel a scheduled job |
| `conn_pool_slot` | `conn_pool_slot(key: string, max: int) -> int` | Get a connection pool slot |
| `conn_pool_next` | `conn_pool_next(pool_size: int) -> int` | Get next available pool slot |
| `lb_pick2` | `lb_pick2(key: string, a: string, b: string) -> string` | Pick from two load-balance targets |
| `lb_pick3` | `lb_pick3(key: string, a: string, b: string, c: string) -> string` | Pick from three load-balance targets |

---

## 49. Data Structures

### Ring Buffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `ring_new` | `ring_new(capacity: int) -> int` | Create a ring buffer |
| `ring_push` | `ring_push(ring: int, val: int) -> int` | Push a value |
| `ring_pop` | `ring_pop(ring: int) -> int` | Pop a value |
| `ring_peek` | `ring_peek(ring: int) -> int` | Peek at the front value |
| `ring_len` | `ring_len(ring: int) -> int` | Get current length |
| `ring_cap` | `ring_cap(ring: int) -> int` | Get capacity |

### Lock-Free Queue

| Function | Signature | Description |
|----------|-----------|-------------|
| `lfq_new` | `lfq_new(capacity: int) -> int` | Create a lock-free queue |
| `lfq_try_push` | `lfq_try_push(q: int, val: int) -> int` | Try to push a value |
| `lfq_try_pop` | `lfq_try_pop(q: int) -> int` | Try to pop a value |
| `lfq_len` | `lfq_len(q: int) -> int` | Get current length |

### Scatter-Gather

| Function | Signature | Description |
|----------|-----------|-------------|
| `sg_gather2` | `sg_gather2(a: string, b: string) -> string` | Gather two buffers |
| `sg_gather3` | `sg_gather3(a: string, b: string, c: string) -> string` | Gather three buffers |
| `sg_slice` | `sg_slice(buf: string, start: int, end: int) -> string` | Slice a gathered buffer |

### FSM (Finite State Machine)

| Function | Signature | Description |
|----------|-----------|-------------|
| `fsm_rule` | `fsm_rule(state: string, event: string, next: string) -> string` | Define a state transition rule |
| `fsm_is` | `fsm_is(rules: string, state: string) -> int` | Check current state |
| `fsm_can` | `fsm_can(rules: string, state: string, event: string) -> int` | Check if transition is possible |
| `fsm_transition` | `fsm_transition(rules: string, state: string, event: string) -> string` | Perform a state transition |

### Frame Allocator & Object Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `frame_alloc_new` | `frame_alloc_new(size: int) -> int` | Create a frame allocator |
| `frame_alloc` | `frame_alloc(fa: int, bytes: int) -> int` | Allocate from frame allocator |
| `frame_reset` | `frame_reset(fa: int) -> int` | Reset frame allocator |
| `frame_used` | `frame_used(fa: int) -> int` | Get used bytes |
| `frame_cap` | `frame_cap(fa: int) -> int` | Get capacity |
| `obj_pool_new` | `obj_pool_new(count: int) -> int` | Create an object pool |
| `obj_acquire` | `obj_acquire(pool: int) -> int` | Acquire an object |
| `obj_release` | `obj_release(pool: int, id: int) -> int` | Release an object |
| `obj_available` | `obj_available(pool: int) -> int` | Get available objects |
| `obj_pool_cap` | `obj_pool_cap(pool: int) -> int` | Get pool capacity |

---

## 50. ECS (Entity Component System)

| Function | Signature | Description |
|----------|-----------|-------------|
| `ecs_world_new` | `ecs_world_new(max_entities: int) -> int` | Create a new ECS world |
| `ecs_spawn` | `ecs_spawn(world: int) -> int` | Spawn a new entity |
| `ecs_alive` | `ecs_alive(world: int, entity: int) -> int` | Check if an entity is alive |
| `ecs_despawn` | `ecs_despawn(world: int, entity: int) -> int` | Despawn an entity |
| `ecs_add` | `ecs_add(world: int, entity: int, component: int, value: int) -> int` | Add a component to an entity |
| `ecs_set` | `ecs_set(world: int, entity: int, component: int, value: int) -> int` | Set a component value |
| `ecs_has` | `ecs_has(world: int, entity: int, component: int) -> int` | Check if entity has component |
| `ecs_get` | `ecs_get(world: int, entity: int, component: int) -> int` | Get a component value |
| `ecs_remove` | `ecs_remove(world: int, entity: int, component: int) -> int` | Remove a component |
| `ecs_query_count` | `ecs_query_count(world: int, mask: int) -> int` | Count entities matching component mask |
| `ecs_query_first` | `ecs_query_first(world: int, mask: int) -> int` | Get first entity matching mask |
| `ecs_archetype` | `ecs_archetype(world: int, entity: int) -> int` | Get archetype of an entity |
| `ecs_system_add` | `ecs_system_add(world: int, mask: int, callback: int) -> int` | Register a system |

---

## 51. Actors

| Function | Signature | Description |
|----------|-----------|-------------|
| `actor_spawn` | `actor_spawn(mailbox_size: int) -> chan[int]` | Spawn an actor with a mailbox |
| `actor_send` | `actor_send(mailbox: chan[int], msg: int) -> bool` | Send a message to an actor |
| `actor_recv` | `actor_recv(mailbox: chan[int]) -> int` | Receive a message from a mailbox |
| `actor_stop` | `actor_stop(mailbox: chan[int]) -> void` | Stop an actor |

---

## 52. Bytes & UTF-8

| Function | Signature | Description |
|----------|-----------|-------------|
| `as_bytes` | `as_bytes(s: string) -> []byte` | Convert a string to a byte array |
| `bytes_as_str` | `bytes_as_str(b: []byte) -> string` | Convert a byte array to a string |
| `bytes_is_view` | `bytes_is_view(b: []byte) -> int` | Check if byte array is a view |
| `bytes_view` | `bytes_view(s: string, start: int, end: int) -> []byte` | Create a byte view into a string |
| `simd_xor_bytes` | `simd_xor_bytes(b: []byte) -> int` | XOR all bytes (SIMD-accelerated) |
| `utf8_valid` | `utf8_valid(s: string) -> int` | Check if string is valid UTF-8 |
| `utf8_rune_len` | `utf8_rune_len(codepoint: int) -> int` | Get byte length of a rune |

---

## 52b. GPU compute seed — AI building blocks (OpenCL + host)

**North star: AI work** (inference / training primitives you compose in Mako),
not graphics. f32 buffers + kernels: matmul, activations, bias, residual, softmax.

**Backends (prefer first that works):**

| Backend | Where | Vendors |
|---------|--------|---------|
| **OpenCL** | Linked when available (`-DMAKO_HAS_OPENCL`) | **NVIDIA, AMD, Intel** ICDs; **Apple** GPU on macOS |
| **host** | Always | CPU reference (CI / no driver) |

Same `gpu_*` surface on every backend. Prefer GPU devices, then any OpenCL
device, then host. Opt out of OpenCL with env `MAKO_NO_OPENCL=1`. Force host with
`gpu_set_prefer_host(1)`. Cap **64 MiB** / buffer; max 4 devices / 64 buffers.
Layouts are **row-major**. Not a full ML framework (no autograd, no GGUF loader).

| Function | Signature | Description |
|----------|-----------|-------------|
| `gpu_available` | `gpu_available() -> int` | 1 if any compute path present (always) |
| `gpu_backend` | `gpu_backend() -> string` | `"opencl"` or `"host"` (what open prefers) |
| `gpu_opencl_ok` | `gpu_opencl_ok() -> int` | 1 if OpenCL linked and a device is pickable |
| `gpu_set_prefer_host` | `gpu_set_prefer_host(on: int) -> int` | Force host path; returns previous |
| `gpu_device_open` | `gpu_device_open() -> int` | Open default device (`-1` if full) |
| `gpu_device_close` | `gpu_device_close(dev: int) -> int` | Close device + free its buffers |
| `gpu_device_backend` | `gpu_device_backend(dev: int) -> string` | `"opencl"` / `"host"` for this handle |
| `gpu_device_name` | `gpu_device_name(dev: int) -> string` | Device name (e.g. `Apple M4`, `NVIDIA …`) |
| `gpu_device_vendor` | `gpu_device_vendor(dev: int) -> string` | Vendor string |
| `gpu_device_is_gpu` | `gpu_device_is_gpu(dev: int) -> int` | 1 if OpenCL GPU device |
| `gpu_buf_new` | `gpu_buf_new(dev: int, nbytes: int) -> int` | Allocate buffer (`-1` on error) |
| `gpu_buf_len` / `gpu_buf_cap` | `(buf) -> int` | Logical length / capacity |
| `gpu_buf_free` | `gpu_buf_free(buf: int) -> int` | Free one buffer |
| `gpu_buf_write` | `gpu_buf_write(buf: int, data: string) -> int` | Raw bytes write |
| `gpu_buf_read` | `gpu_buf_read(buf: int, max: int) -> string` | Raw bytes read |
| `gpu_upload_f32` | `gpu_upload_f32(buf: int, vals: []float) -> int` | Pack LE f32; returns count |
| `gpu_download_f32` | `gpu_download_f32(buf: int) -> []float` | Unpack LE f32 |
| `gpu_f32_count` | `gpu_f32_count(buf: int) -> int` | `len/4` |
| `gpu_fill_f32` | `gpu_fill_f32(buf: int, n: int, v: float) -> int` | Fill `n` floats |
| `gpu_add_f32` | `gpu_add_f32(out, a, b: int) -> int` | `out[i] = a[i] + b[i]` |
| `gpu_mul_f32` | `gpu_mul_f32(out, a, b: int) -> int` | `out[i] = a[i] * b[i]` |
| `gpu_scale_f32` | `gpu_scale_f32(out, a: int, s: float) -> int` | `out[i] = a[i] * s` |
| `gpu_relu_f32` | `gpu_relu_f32(out, a: int) -> int` | ReLU activation |
| `gpu_saxpy_f32` | `gpu_saxpy_f32(out, a, b: int, alpha: float) -> int` | `out = α·a + b` (residual) |
| `gpu_bias_add_f32` | `gpu_bias_add_f32(out, a, bias, rows, cols) -> int` | Broadcast bias over rows |
| `gpu_matmul_f32` | `gpu_matmul_f32(out, a, b, m, n, k) -> int` | `C[m,n] = A[m,k] @ B[k,n]` |
| `gpu_softmax_rows_f32` | `gpu_softmax_rows_f32(out, a, rows, cols) -> int` | Softmax over last dim |
| `gpu_sum_f32` | `gpu_sum_f32(buf: int) -> float` | Reduce-sum (host readback) |
| `gpu_gelu_f32` / `gpu_silu_f32` | `(out, a) -> int` | GELU (tanh approx) / SiLU |
| `gpu_transpose_f32` | `(out, a, rows, cols) -> int` | Transpose rows×cols |
| `gpu_layernorm_f32` | `(out, x, gamma, beta, rows, cols, eps)` | Per-row LN; gamma/beta `-1` = none |
| `gpu_attention_f32` | `(out, q, k, v, seq, dim) -> int` | Scaled dot-product attention (1 head) |
| `gpu_mha_f32` | `(out, q, k, v, seq, n_heads, head_dim) -> int` | Multi-head attention; Q/K/V `[seq, H·D]` |

Dense / transformer sketches: `matmul`→`bias`→`gelu`; `mha` + `layernorm`.
Tests: `gpu_seed_test.mko`, `ai_depth_test.mko`. Runtime: `runtime/mako_gpu.h`.

---

## 52c. Local models (weights + your own nets)

Two ways to “use AI models” in Mako:

| Path | When | Surface |
|------|------|---------|
| **Remote / hosted** | Chat, tools, embeddings via API | `llm_*` (OpenAI-compatible HTTPS) |
| **Local weights** | Run / author nets on GPU | `model_*` + `gpu_*` |

**Existing open weights:**

- `model_load_safetensors` — Hugging Face safetensors (F32/F16→f32)
- `model_load_gguf` — **GGUF** F32/F16 + **Q4_0/Q8_0 dequant→f32**
- `model_linear_f32(..., hf=1)` — PyTorch `[out, in]` weight layout

**Your own models:** `model_set_f32` + compose layers; `model_save` / `model_load`
(`.makomodel`). **Text:** `tok_*` vocab + **BPE** (`tok_load_bpe` / `tok_encode_bpe`).

Not yet: Q4_K/Q5/Q6, native quant matmul, SentencePiece/tiktoken, full LLaMA loop.

| Function | Signature | Description |
|----------|-----------|-------------|
| `model_new` | `model_new(dev: int) -> int` | Weight store on a GPU device |
| `model_free` | `model_free(m: int) -> int` | Free store + tensors |
| `model_set_f32` | `model_set_f32(m, name, vals, d0,d1,d2,d3) -> int` | Insert/replace named tensor |
| `model_tensor_*` | count / name / buf / elems / ndim / dim | Introspect |
| `model_load_safetensors` | `(m, path) -> int` | HF safetensors F32/F16 |
| `model_load_gguf` | `(m, path) -> int` | GGUF F32/F16/Q4_0/Q8_0→f32 |
| `model_save` / `model_load` | `(m, path) -> int` | Native `.makomodel` |
| `model_linear_f32` | `(m, out, x, w, b, batch, in, out, hf) -> int` | Dense + bias; `hf=1` for HF layout |
| `tok_new` / `tok_free` | tokenizer handle | |
| `tok_set` / `tok_id` / `tok_token` / `tok_size` | vocab CRUD | |
| `tok_load_json` / `tok_load_lines` | load vocab file | |
| `tok_encode` / `tok_decode` | longest-match encode; id→string decode | |
| `tok_load_merges` / `tok_load_bpe` | BPE merges / vocab+merges | |
| `tok_encode_bpe` / `tok_merge_count` | BPE encode; merge table size | |

Tests: `model_weights_test.mko`, `ai_depth_test.mko` · demo: `model_mlp.mko` ·
fixtures: `tiny_linear.safetensors`, `tiny.gguf`, `tiny_quant.gguf`, `tiny_vocab.json`,
`bpe_vocab.json`, `bpe_merges.txt`.

---

## 53. Buffered I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_reader_new` | `buf_reader_new(path: string) -> BufReader` | Create a buffered reader from a file |
| `buf_reader_from_string` | `buf_reader_from_string(data: string) -> BufReader` | Create a buffered reader from a string |
| `buf_read_line` | `buf_read_line(r: BufReader) -> string` | Read the next line |
| `buf_read` | `buf_read(r: BufReader, n: int) -> string` | Read n bytes |
| `buf_reader_close` | `buf_reader_close(r: BufReader) -> int` | Close the reader |
| `buf_writer_new` | `buf_writer_new(path: string) -> BufWriter` | Create a buffered writer to a file |
| `buf_write` | `buf_write(w: BufWriter, data: string) -> int` | Write data to buffer |
| `buf_write_byte` | `buf_write_byte(w: BufWriter, b: int) -> int` | Write a single byte |
| `buf_flush` | `buf_flush(w: BufWriter) -> int` | Flush the buffer to disk |
| `buf_writer_close` | `buf_writer_close(w: BufWriter) -> int` | Close the writer |

---

## 54. Async I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `io_wait` | `io_wait(fd: int, timeout_ms: int) -> int` | Wait for I/O readiness |
| `io_poll2` | `io_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Poll two fds |
| `io_poll3` | `io_poll3(fd1: int, fd2: int, fd3: int, timeout_ms: int) -> int` | Poll three fds |
| `io_poll4` | `io_poll4(fd1: int, fd2: int, fd3: int, fd4: int, timeout_ms: int) -> int` | Poll four fds |
| `io_kq_poll2` | `io_kq_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Kqueue-based poll of two fds |
| `io_epoll_poll2` | `io_epoll_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Epoll-based poll of two fds |
| `io_native_poll2` | `io_native_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Native poll of two fds |
| `io_read_ready` | `io_read_ready(fd: int, timeout_ms: int) -> int` | Check if fd is read-ready |
| `io_write_ready` | `io_write_ready(fd: int, timeout_ms: int) -> int` | Check if fd is write-ready |
| `io_set_nonblocking` | `io_set_nonblocking(fd: int, nonblocking: int) -> int` | Set fd blocking mode |
| `io_try_write` | `io_try_write(fd: int, data: string) -> int` | Non-blocking write attempt |
| `io_backoff_ms` | `io_backoff_ms(attempt: int, base: int, max: int) -> int` | Calculate I/O backoff delay |
| `io_should_pause` | `io_should_pause(pending: int, threshold: int, timeout: int) -> int` | Check if I/O should be paused |

---

## 55. Signals

| Function | Signature | Description |
|----------|-----------|-------------|
| `signal_notify` | `signal_notify(signum: int) -> int` | Register for signal notification |
| `signal_received` | `signal_received() -> int` | Check if a signal was received |

---

## 56. DNS & Networking

| Function | Signature | Description |
|----------|-----------|-------------|
| `lookup_host` | `lookup_host(host: string) -> string` | Look up the first IP for a hostname |
| `dns_lookup_count` | `dns_lookup_count(host: string) -> int` | Count DNS results |
| `dns_lookup_all` | `dns_lookup_all(host: string) -> string` | Get all DNS results |
| `dns_lookup_ipv4` | `dns_lookup_ipv4(host: string) -> string` | Look up IPv4 address |
| `dns_lookup_ipv6` | `dns_lookup_ipv6(host: string) -> string` | Look up IPv6 address |
| `parse_ip_ok` | `parse_ip_ok(addr: string) -> int` | Validate an IP address |
| `dns_ip_family` | `dns_ip_family(addr: string) -> int` | Get IP address family (4 or 6) |
| `dns_is_loopback` | `dns_is_loopback(addr: string) -> int` | Check if address is loopback |
| `dns_is_private` | `dns_is_private(addr: string) -> int` | Check if address is private |
| `dns_normalize_host` | `dns_normalize_host(host: string) -> string` | Normalize a hostname |
| `dns_join_host_port` | `dns_join_host_port(host: string, port: int) -> string` | Join host and port |
| `dns_split_host` | `dns_split_host(hostport: string) -> string` | Extract host from host:port |
| `dns_split_port` | `dns_split_port(hostport: string) -> int` | Extract port from host:port |

---

## 57. URL Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `url_scheme` | `url_scheme(url: string) -> string` | Extract the scheme from a URL |
| `url_host` | `url_host(url: string) -> string` | Extract the host from a URL |
| `url_path` | `url_path(url: string) -> string` | Extract the path from a URL |
| `url_query` | `url_query(url: string) -> string` | Extract the query string from a URL |

---

## 58. Templates

| Function | Signature | Description |
|----------|-----------|-------------|
### Templates (Go-style `text/template` / `html/template`)

Syntax: `{{.key}}`, `{{if}}/{{else}}/{{end}}`, `{{range}}`, `{{with}}`,
`{{define "n"}}` / `{{template "n"}}`, `{{/* comment */}}`, funcs
`len` / `upper` / `lower` / `html` / `printf "%s"`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tmpl_data_new` / `tmpl_data_free` | data bag handle | |
| `tmpl_data_set` | `(d, key, val) -> int` | String field (key may be `.Name`) |
| `tmpl_data_set_list` | `(d, key, "a,b,c") -> int` | List for `{{range}}` |
| `tmpl_data_set_int` | `(d, key, n) -> int` | Int as string |
| `tmpl_new` / `tmpl_free` | parse template source | extracts `{{define}}` |
| `tmpl_execute` | `(t, data) -> string` | Text (no escape) |
| `tmpl_html_execute` | `(t, data) -> string` | HTML auto-escape interpolations |
| `tmpl_text` / `tmpl_html` | `(source, data) -> string` | One-shot parse+exec |
| `template_execute` | legacy one-key replace | still supported |
| `html_template_*` | legacy multi-key / if / range seeds | still supported |

Packs: `std/text/template`, `std/html/template`. Tests: `template_test.mko`.
Demo: `examples/template_demo.mko`.

---

## 59. Image Processing

### PNG

| Function | Signature | Description |
|----------|-----------|-------------|
| `png_available` | `png_available() -> int` | Check if PNG support is available |
| `png_encode_gray` | `png_encode_gray(width: int, height: int, pixels: string) -> string` | Encode grayscale PNG |
| `png_encode_rgb` | `png_encode_rgb(width: int, height: int, pixels: string) -> string` | Encode RGB PNG |
| `png_decode_gray` | `png_decode_gray(data: string) -> string` | Decode grayscale PNG |
| `png_width` | `png_width(data: string) -> int` | Get PNG width |
| `png_height` | `png_height(data: string) -> int` | Get PNG height |

### GIF

| Function | Signature | Description |
|----------|-----------|-------------|
| `gif_encode_rgb` | `gif_encode_rgb(width: int, height: int, pixels: string) -> string` | Encode RGB GIF |
| `gif_decode_rgb` | `gif_decode_rgb(data: string) -> string` | Decode RGB GIF |
| `gif_width` | `gif_width(data: string) -> int` | Get GIF width |
| `gif_height` | `gif_height(data: string) -> int` | Get GIF height |
| `gif_encode_rgb_lzw` | `gif_encode_rgb_lzw(width: int, height: int, pixels: string) -> string` | Encode RGB GIF with LZW |
| `gif_decode_rgb_lzw` | `gif_decode_rgb_lzw(data: string) -> string` | Decode LZW GIF |

### JPEG

| Function | Signature | Description |
|----------|-----------|-------------|
| `jpeg_encode_gray` | `jpeg_encode_gray(width: int, height: int, pixels: string) -> string` | Encode grayscale JPEG |
| `jpeg_decode_gray` | `jpeg_decode_gray(data: string) -> string` | Decode grayscale JPEG |
| `jpeg_width` | `jpeg_width(data: string) -> int` | Get JPEG width |
| `jpeg_height` | `jpeg_height(data: string) -> int` | Get JPEG height |
| `jpeg_encode_gray_dct` | `jpeg_encode_gray_dct(width: int, height: int, pixels: string) -> string` | Encode JPEG with DCT |
| `jpeg_dct_dc` | `jpeg_dct_dc(data: string) -> int` | Get DCT DC coefficient |
| `jpeg_encode_gray_huff` | `jpeg_encode_gray_huff(width: int, height: int, pixels: string) -> string` | Encode JPEG with APP9 Huffman-ish evidence (mako probes) |
| `jpeg_encode_gray_baseline` | `jpeg_encode_gray_baseline(width: int, height: int, pixels: string) -> string` | Viewer-readable baseline grayscale JPEG (DQT/DHT/SOS entropy) |
| `jpeg_is_baseline_huff` | `jpeg_is_baseline_huff(data: string) -> int` | 1 if SOI+DQT+SOF0+DHT+SOS present |
| `jpeg_huff_block` | `jpeg_huff_block(data: string) -> string` | Get Huffman-encoded block |
| `jpeg_encode_gray_jfif` | `jpeg_encode_gray_jfif(width: int, height: int, pixels: string) -> string` | Encode grayscale with SOI+APP0(JFIF)+SOF0 headers; pixels in APP7 (`MAKOJPG`) for `jpeg_decode_gray` roundtrip. External viewers see a JFIF shell, not a full Huffman bitstream. |
| `jpeg_is_jfif` | `jpeg_is_jfif(data: string) -> int` | Check if data has JFIF APP0 marker |
| `jpeg_has_sof0` | `jpeg_has_sof0(data: string) -> int` | Scan markers for SOF0 (baseline DCT header) |
| `jpeg_sof0_width` | `jpeg_sof0_width(data: string) -> int` | Width from SOF0 (0 if missing) |
| `jpeg_sof0_height` | `jpeg_sof0_height(data: string) -> int` | Height from SOF0 (0 if missing) |
| `jpeg_sof0_precision` | `jpeg_sof0_precision(data: string) -> int` | Sample precision from SOF0 (0 if missing; Mako JFIF uses 8) |
| `jpeg_sof0_components` | `jpeg_sof0_components(data: string) -> int` | Component count Nf from SOF0 (grayscale JFIF uses 1) |
| `jpeg_is_baseline_gray` | `jpeg_is_baseline_gray(data: string) -> int` | 1 if JFIF + SOF0 + 8-bit + Nf==1 (Mako grayscale shell) |
| `jpeg_jfif_major` | `jpeg_jfif_major(data: string) -> int` | JFIF major version from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_jfif_minor` | `jpeg_jfif_minor(data: string) -> int` | JFIF minor version from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_sof0_sampling` | `jpeg_sof0_sampling(data: string) -> int` | First component Hi/Vi packed byte from SOF0 (grayscale uses `0x11`) |
| `jpeg_sof0_component_id` | `jpeg_sof0_component_id(data: string) -> int` | First component id Ci from SOF0 (grayscale JFIF uses 1) |
| `jpeg_jfif_density_units` | `jpeg_jfif_density_units(data: string) -> int` | JFIF density units from APP0 (`-1` if missing; Mako shell uses 0) |
| `jpeg_jfif_x_density` | `jpeg_jfif_x_density(data: string) -> int` | JFIF X density from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_jfif_y_density` | `jpeg_jfif_y_density(data: string) -> int` | JFIF Y density from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_has_app7` | `jpeg_has_app7(data: string) -> int` | 1 if APP7 carries `MAKOJPG` roundtrip payload |
| `jpeg_sof0_quant_table` | `jpeg_sof0_quant_table(data: string) -> int` | First component quant table selector Tqi (`-1` if missing; Mako gray uses 0) |
| `jpeg_jfif_thumb_width` | `jpeg_jfif_thumb_width(data: string) -> int` | JFIF embedded thumbnail width (`-1` if no APP0; Mako shell uses 0) |
| `jpeg_jfif_thumb_height` | `jpeg_jfif_thumb_height(data: string) -> int` | JFIF embedded thumbnail height (`-1` if no APP0; Mako shell uses 0) |
| `jpeg_is_mako_jfif` | `jpeg_is_mako_jfif(data: string) -> int` | 1 if baseline-gray JFIF shell **and** MAKOJPG APP7 payload |
| `jpeg_has_eoi` | `jpeg_has_eoi(data: string) -> int` | 1 if JPEG has EOI (`FF D9`) after SOI |
| `jpeg_sof0_matches_app7` | `jpeg_sof0_matches_app7(data: string) -> int` | 1 if SOF0 width/height match MAKOJPG APP7 payload dims |
| `jpeg_is_mako_complete` | `jpeg_is_mako_complete(data: string) -> int` | 1 if `jpeg_is_mako_jfif` + dim match + EOI |
| `jpeg_is_mako_raw` | `jpeg_is_mako_raw(data: string) -> int` | 1 if APP7 MAKOJPG + EOI + positive dims (JFIF optional; covers `jpeg_encode_gray`) |
| `jpeg_jfif_app0_length` | `jpeg_jfif_app0_length(data: string) -> int` | APP0 segment length field (0 if missing; Mako JFIF uses 16) |
| `jpeg_app7_payload_len` | `jpeg_app7_payload_len(data: string) -> int` | APP7 pixel payload byte count (`width * height` from MAKOJPG dims) |
| `jpeg_has_app8` | `jpeg_has_app8(data: string) -> int` | 1 if APP8 DCT-DC evidence marker present (`jpeg_encode_gray_dct`) |
| `jpeg_has_app9` | `jpeg_has_app9(data: string) -> int` | 1 if APP9 Huffman-ish block present (`jpeg_encode_gray_huff`) |
| `jpeg_is_mako_dct` | `jpeg_is_mako_dct(data: string) -> int` | 1 if `jpeg_is_mako_raw` + APP8 |
| `jpeg_is_mako_huff` | `jpeg_is_mako_huff(data: string) -> int` | 1 if `jpeg_is_mako_dct` + APP9 |
| `jpeg_app8_length` | `jpeg_app8_length(data: string) -> int` | APP8 segment length field (0 if missing; Mako DCT uses 6) |
| `jpeg_app9_length` | `jpeg_app9_length(data: string) -> int` | APP9 segment length field (0 if missing; Mako huff uses 66) |
| `jpeg_roundtrip_ok` | `jpeg_roundtrip_ok(data: string) -> int` | 1 if APP7 decode length equals `width*height` payload |
| `jpeg_app7_length` | `jpeg_app7_length(data: string) -> int` | APP7 MAKOJPG segment length field (0 if missing; 8×8 gray uses 77) |
| `jpeg_has_soi` | `jpeg_has_soi(data: string) -> int` | 1 if buffer starts with SOI (`FF D8`) |
| `jpeg_app7_len_matches_payload` | `jpeg_app7_len_matches_payload(data: string) -> int` | 1 if APP7 length field equals `2+7+4+width*height` |

---

## 60. Archive Formats

### Tar

| Function | Signature | Description |
|----------|-----------|-------------|
| `tar_write_file` | `tar_write_file(name: string, data: string, archive: string) -> int` | Write a file to a tar archive |
| `tar_first_name` | `tar_first_name(archive: string) -> string` | Get the name of the first file in a tar |

### Zip

| Function | Signature | Description |
|----------|-----------|-------------|
| `zip_write_file` | `zip_write_file(name: string, data: string, archive: string) -> int` | Write a file to a zip archive |
| `zip_first_name` | `zip_first_name(archive: string) -> string` | Get the name of the first file |
| `zip_read_file` | `zip_read_file(archive: string, name: string) -> string` | Read a file from a zip archive |
| `zip_list` | `zip_list(archive: string) -> []string` | List files in a zip archive |
| `zip_create` | `zip_create() -> ZipWriter` | Create a new zip writer |
| `zip_add` | `zip_add(zw: ZipWriter, name: string, data: string) -> int` | Add a file to a zip writer |
| `zip_write_to` | `zip_write_to(zw: ZipWriter, path: string) -> int` | Write zip to a file |
| `zip_close` | `zip_close(zw: ZipWriter) -> void` | Close the zip writer |
| `zip_deflate_available` | `zip_deflate_available() -> int` | Check if zip deflate is available |

---

## 61. Multipart Forms

| Function | Signature | Description |
|----------|-----------|-------------|
| `multipart_boundary` | `multipart_boundary(content_type: string) -> string` | Extract multipart boundary from Content-Type |
| `multipart_form_value` | `multipart_form_value(body: string, boundary: string, field: string) -> string` | Get a form field value |
| `multipart_file_name` | `multipart_file_name(body: string, boundary: string, field: string) -> string` | Get uploaded file name |
| `multipart_file_content_type` | `multipart_file_content_type(body: string, boundary: string, field: string) -> string` | Get uploaded file content type |
| `multipart_file_value` | `multipart_file_value(body: string, boundary: string, field: string) -> string` | Get uploaded file contents |
| `multipart_file_size` | `multipart_file_size(body: string, boundary: string, field: string) -> int` | Get uploaded file size |
| `multipart_file_allowed` | `multipart_file_allowed(body: string, boundary: string, field: string, max_size: int, allowed_types: string) -> int` | Validate uploaded file |

---

## 62. Reflect

| Function | Signature | Description |
|----------|-----------|-------------|
| `reflect_type_of_int` | `reflect_type_of_int(val: int) -> string` | Get type name of an int value |
| `reflect_type_of_string` | `reflect_type_of_string(val: string) -> string` | Get type name of a string value |
| `reflect_kind_of_int` | `reflect_kind_of_int(val: int) -> string` | Get kind of an int value |
| `reflect_kind_of_string` | `reflect_kind_of_string(val: string) -> string` | Get kind of a string value |
| `reflect_value_string_int` | `reflect_value_string_int(val: int) -> string` | Convert int to reflect string |
| `reflect_value_string_str` | `reflect_value_string_str(val: string) -> string` | Convert string to reflect string |
| `reflect_len_string` | `reflect_len_string(val: string) -> int` | Get length via reflect |
| `reflect_struct_num_fields` | `reflect_struct_num_fields(type_name: string) -> int` | Get number of struct fields |
| `reflect_struct_field_name` | `reflect_struct_field_name(type_name: string, idx: int) -> string` | Get struct field name |
| `reflect_struct_field_type` | `reflect_struct_field_type(type_name: string, idx: int) -> string` | Get struct field type |
| `reflect_struct_has_field` | `reflect_struct_has_field(type_name: string, field: string) -> int` | Check if struct has a field |
| `reflect_value_new` | `reflect_value_new(type_name: string) -> ReflectValue` | Create a new reflect value |
| `reflect_value_set` | `reflect_value_set(rv: ReflectValue, field: string, val: string) -> int` | Set a field on a reflect value |
| `reflect_value_get` | `reflect_value_get(rv: ReflectValue, field: string) -> string` | Get a field from a reflect value |
| `reflect_value_num_fields` | `reflect_value_num_fields(rv: ReflectValue) -> int` | Get number of fields |
| `reflect_value_field_at` | `reflect_value_field_at(rv: ReflectValue, idx: int) -> string` | Get field value at index |
| `reflect_value_set_at` | `reflect_value_set_at(rv: ReflectValue, idx: int, val: string) -> int` | Set field value at index |
| `reflect_value_schema` | `reflect_value_schema(rv: ReflectValue) -> string` | Get value schema as string |
| `reflect_value_clone` | `reflect_value_clone(rv: ReflectValue) -> ReflectValue` | Clone a reflect value |
| `reflect_value_equal` | `reflect_value_equal(a: ReflectValue, b: ReflectValue) -> int` | Compare two reflect values |
| `reflect_value_of_type` | `reflect_value_of_type(type_name: string) -> ReflectValue` | Create a value of a named type |
| `reflect_type_schema` | `reflect_type_schema(type_name: string) -> string` | Get type schema |
| `reflect_type_count` | `reflect_type_count() -> int` | Count registered types |
| `reflect_type_name_at` | `reflect_type_name_at(idx: int) -> string` | Get type name at index |

---

## 63. Context & Timeout

| Function | Signature | Description |
|----------|-----------|-------------|
| `context_with_timeout` | `context_with_timeout(timeout_ms: int) -> int` | Create a context with timeout |
| `context_expired` | `context_expired(ctx: int) -> int` | Check if context has expired |
| `context_remaining` | `context_remaining(ctx: int) -> int` | Milliseconds remaining |

---

## 64. BytesBuffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `bytes_buffer` | `bytes_buffer() -> BytesBuffer` | Create a new bytes buffer |
| `bytes_buffer_write` | `bytes_buffer_write(bb: BytesBuffer, data: string) -> void` | Write to the buffer |
| `bytes_buffer_string` | `bytes_buffer_string(bb: BytesBuffer) -> string` | Get buffer contents as string |
| `bytes_buffer_len` | `bytes_buffer_len(bb: BytesBuffer) -> int` | Get buffer length |
| `bytes_buffer_reset` | `bytes_buffer_reset(bb: BytesBuffer) -> void` | Reset the buffer |

---

## 65. Error Handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `result_unwrap_or` | `result_unwrap_or(r: Result[int, string], default: int) -> int` | Unwrap Result or return default |
| `wrap_err` | `wrap_err(r: Result[int, string], msg: string) -> Result[int, string]` | Wrap an error with additional context |
| `error_context` | alias of `wrap_err` | Same RT |
| `errorf` | `errorf(fmt: string, arg: string) -> Result[int, string]` | Create a formatted error Result |
| `error_tag` | `error_tag(tag: string, msg: string) -> Result[int, string]` | Tagged Err `"tag: msg"` |
| `error_join` | `error_join(a, b: Result[int, string]) -> Result[int, string]` | Prefer first Err; join messages |
| `error_is` | `error_is(r: Result[int, string], target: string) -> bool` | Substring match on wrap chain (Go `errors.Is` style) |
| `error_string` | `error_string(r: Result[int, string]) -> string` | Extract error message from a Result |
| `error_unwrap` | `error_unwrap(r) -> Result[int, string]` | Peel one `"prefix: "` wrap layer |
| `error_root` | `error_root(r) -> Result[int, string]` | Peel all wrap layers to innermost |
| `error_as_tag` | `error_as_tag(r) -> string` | Tag half of `error_tag` form (else `""`) |
| `error_has_tag` | `error_has_tag(r, tag: string) -> bool` | Exact prefix `"tag: "` match |

---

## 66. OpenAPI & GraphQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `openapi_route` | `openapi_route(method: string, path: string, summary: string) -> string` | Define an OpenAPI route |
| `openapi_doc` | `openapi_doc(title: string, version: string, routes: string) -> string` | Generate an OpenAPI document |
| `graphql_field` | `graphql_field(name: string) -> string` | Create a GraphQL field |
| `graphql_arg` | `graphql_arg(name: string, value: string) -> string` | Create a GraphQL argument |
| `graphql_data` | `graphql_data(key: string, value: string) -> string` | Create a GraphQL data response |
| `graphql_error` | `graphql_error(msg: string) -> string` | Create a GraphQL error response |
| `graphql_request` | `graphql_request(query: string) -> string` | Build a GraphQL request |
| `graphql_is_mutation` | `graphql_is_mutation(query: string) -> int` | Check if query is a mutation |

---

## 67. SSE & RPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `sse_event` | `sse_event(event: string, data: string) -> string` | Format a Server-Sent Event |
| `sse_retry` | `sse_retry(ms: int) -> string` | Format an SSE retry directive |

---

## LLM programming (OpenAI-compatible)

First-class **LLM client/runtime** for chat, tools, streaming parse, and structured
output — without Python SDKs or async coloring. Default provider is **xAI**
(`https://api.x.ai/v1`, env `XAI_API_KEY`); also works with OpenAI-compatible
endpoints via `MAKO_LLM_BASE_URL` / `OPENAI_API_KEY`.

### Market gaps closed

| Gap in typical stacks | Mako |
|----------------------|------|
| Async-colored clients | Sync `llm_chat` / `llm_ask` + mono timeouts |
| Stream/tool parsing only in Python/JS | Runtime SSE + tool_call extract |
| JSON buried in markdown | `llm_json_extract` (fences + balanced `{`/`[`) |
| API keys in logs | `llm_redact_key`; keys from env only |
| Rate-limit budgeting | `llm_estimate_tokens`, `llm_retry_delay_ms` |
| Parallel tools need frameworks | Parse tools then `crew`/`fan` your handlers |

### Build messages & bodies

| Function | Signature | Description |
|----------|-----------|-------------|
| `llm_message` | `(role, content) -> string` | One chat message object |
| `llm_messages_append` | `(arr, msg) -> string` | Append into JSON array |
| `llm_chat_body` | `(model, messages, stream) -> string` | Full chat/completions body |
| `llm_system_user` | `(model, system, user) -> string` | Quick two-turn body |
| `llm_body_with_tools` | `(body, tools_json) -> string` | Inject `"tools":[...]` |

### Parse responses & streams

| Function | Signature | Description |
|----------|-----------|-------------|
| `llm_content` | `(response) -> string` | `choices[0].message.content` (+ unescape) |
| `llm_finish_reason` | `(response) -> string` | `finish_reason` |
| `llm_usage_*_tokens` | `(response) -> int` | prompt / completion / total |
| `llm_tool_call_count` | `(response) -> int` | Number of tool calls |
| `llm_tool_call_name` / `args` | `(response, i) -> string` | i-th function name / arguments |
| `llm_sse_data` | `(line) -> string` | Payload after `data:` (`[DONE]` → empty) |
| `llm_sse_delta` | `(chunk) -> string` | `choices[0].delta.content` |
| `llm_stream_append` | `(acc, delta) -> string` | Concatenate stream text |
| `llm_json_extract` | `(text) -> string` | JSON from fences or first object/array |

### Config & transport

| Function | Signature | Description |
|----------|-----------|-------------|
| `llm_api_key` | `() -> string` | `XAI_API_KEY` / `OPENAI_API_KEY` / `MAKO_LLM_API_KEY` |
| `llm_base_url` | `() -> string` | Default `https://api.x.ai/v1` |
| `llm_default_model` | `() -> string` | Default `grok-4.5` (or `MAKO_LLM_MODEL`) |
| `llm_https_post` | `(url, key, body, timeout_ms, verify) -> string` | HTTPS JSON POST + Bearer |
| `llm_chat` | `(base, key, body, timeout_ms) -> string` | POST `{base}/chat/completions` |
| `llm_chat_stream` | `(base, key, body, timeout_ms) -> string` | SSE stream; returns synthetic chat JSON for `llm_content` |
| `llm_chat_retry` | `(base, key, body, timeout_ms, max_attempts) -> string` | Chat with backoff on 429/5xx/connect |
| `llm_body_force_stream` | `(body) -> string` | Ensure `"stream":true` |
| `llm_ask` | `(system, user, timeout_ms) -> string` | One-shot from env config |
| `llm_embed_body` / `llm_embeddings` / `llm_embed` | embeddings request / POST / one-shot | OpenAI-compatible `/embeddings` |
| `llm_embedding_dim` / `llm_embedding_json` | parse first vector length / JSON array | |
| `llm_is_error` / `llm_error_message` / `llm_should_retry` | error detect / message / retryable? | |
| `llm_last_status` | `() -> int` | Last HTTP status from chat/post/stream |
| `llm_https_available` | `() -> int` | `1` when OpenSSL linked |
| `llm_redact_key` | `(key) -> string` | Safe log fragment |
| `llm_estimate_tokens` | `(text) -> int` | ~chars/4 heuristic |
| `llm_retry_delay_ms` | `(attempt, base, max) -> int` | Exponential backoff |

Example: `examples/llm_chat.mko` · tests: `examples/testing/llm_test.mko` · pack: `std/llm`.
| `rpc_frame` | `rpc_frame(method: string, payload: string) -> string` | Build an RPC frame |
| `rpc_method` | `rpc_method(frame: string) -> string` | Extract method from RPC frame |
| `rpc_payload` | `rpc_payload(frame: string) -> string` | Extract payload from RPC frame |

---

## 68. NoSQL Databases

| Function | Signature | Description |
|----------|-----------|-------------|
| `mongo_connect_url` | `mongo_connect_url(url: string) -> string` | Parse a MongoDB connection URL |
| `mongo_find_one_request` | `mongo_find_one_request(collection: string, filter: string) -> string` | Build a findOne request |
| `cassandra_connect_url` | `cassandra_connect_url(url: string) -> string` | Parse a Cassandra connection URL |
| `cassandra_select` | `cassandra_select(keyspace: string, table: string, where_clause: string) -> string` | Build a Cassandra SELECT |
| `clickhouse_connect_url` | `clickhouse_connect_url(url: string) -> string` | Parse a ClickHouse connection URL |
| `clickhouse_select` | `clickhouse_select(db: string, table: string, where_clause: string) -> string` | Build a ClickHouse SELECT |
| `elastic_connect_url` | `elastic_connect_url(url: string) -> string` | Parse an Elasticsearch connection URL |
| `elastic_search_request` | `elastic_search_request(index: string, query: string) -> string` | Build an Elasticsearch search request |
| `queue_stub_ping` | `queue_stub_ping() -> int` | Queue stub ping |

---

## 69. Process Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec_output` | `exec_output(cmd: string) -> string` | Run a command and return its output |
| `exec_run` | `exec_run(cmd: string) -> int` | Run a command and return exit code |

---

## 70. MIME

| Function | Signature | Description |
|----------|-----------|-------------|
| `mime_type` | `mime_type(ext: string) -> string` | Get MIME type for a file extension |


---

## 71. Checked arithmetic & overflow

CLI: `mako build --overflow trap|wrap|ignore` (also on `mako run`).  
Trap mode rewrites integer `+ - *` to `mako_add_i64` / `sub` / `mul` (abort on overflow).

| Function | Signature | Description |
|----------|-----------|-------------|
| `checked_add` | `checked_add(a: int, b: int) -> int` | Add; **abort** on overflow |
| `checked_sub` | `checked_sub(a: int, b: int) -> int` | Subtract; abort on overflow |
| `checked_mul` | `checked_mul(a: int, b: int) -> int` | Multiply; abort on overflow |
| `would_overflow_add` | `would_overflow_add(a: int, b: int) -> int` | `1` if `a+b` would overflow |
| `would_overflow_sub` | `would_overflow_sub(a: int, b: int) -> int` | `1` if `a-b` would overflow |
| `would_overflow_mul` | `would_overflow_mul(a: int, b: int) -> int` | `1` if `a*b` would overflow |

Runtime: `runtime/mako_overflow.h`.

---

## 72. Graceful shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `install_graceful_shutdown` | `install_graceful_shutdown(grace_ms: int) -> int` | `signal_on_term` + store preferred grace |
| `shutdown_requested` | `shutdown_requested() -> int` | `1` if shutdown was signaled |
| `signal_on_term` | `signal_on_term() -> int` | Install SIGTERM/SIGINT → set shutdown flag |
| `register_listener` | `register_listener(fd: int) -> int` | Track listen fd for close on drain |
| `close_listeners` | `close_listeners() -> int` | Close registered listeners |
| `server_shutdown_begin` | `server_shutdown_begin(grace_ms: int) -> int` | Flag drain + close listeners |
| `server_drain` | `server_drain(timeout_ms: int) -> int` | Wait up to timeout (10ms slices) |
| `should_stop_accepting` | `should_stop_accepting() -> int` | Accept-loop stop flag |
| `http_shutdown_begin` | `http_shutdown_begin() -> int` | Begin HTTP server shutdown |
| `http_shutdown_requested` | `http_shutdown_requested() -> int` | Check if HTTP shutdown in progress |
| `http_shutdown_drain_conn` | `http_shutdown_drain_conn(fd: int) -> int` | Drain a single HTTP connection |
| `http_shutdown_expired` | `http_shutdown_expired() -> int` | Check if grace period expired |
| `http_shutdown_remaining` | `http_shutdown_remaining() -> int` | Remaining ms in grace window |
| `http_shutdown_ready` | `http_shutdown_ready() -> int` | `1` if all connections drained |
| `http_shutdown_reset` | `http_shutdown_reset() -> int` | Reset shutdown state |
| `http_shutdown_deadline` | `http_shutdown_deadline(ms: int) -> int` | Set deadline for drain |
| `http_shutdown_from_signal` | `http_shutdown_from_signal() -> int` | Trigger shutdown from signal handler |

Runtime: `runtime/mako_shutdown.h`. Pairs with `signal_watch`.

---

## 73. Leak detector (scopes)

Builds on `leak_mark` / `alloc_track_*`. Nestable scopes:

| Function | Signature | Description |
|----------|-----------|-------------|
| `leak_mark` | `leak_mark() -> int` | Snapshot live alloc bytes |
| `leak_bytes_since` | `leak_bytes_since(mark: int) -> int` | Bytes still live since mark |
| `leak_detected` | `leak_detected(mark: int) -> int` | `1` if bytes grew since mark |
| `leak_assert_clear` | `leak_assert_clear(mark: int) -> int` | Assert no growth since mark |
| `leak_report_json` | `leak_report_json(mark: int) -> string` | JSON report since mark |
| `leak_scope_enter` | `leak_scope_enter() -> int` | Push nestable scope mark |
| `leak_scope_exit` | `leak_scope_exit() -> int` | Pop; returns leaked bytes; warns on stderr if >0 |
| `leak_check` | `leak_check() -> int` | Bytes over current scope mark (or live if none) |
| `leak_assert_scope` | `leak_assert_scope() -> int` | `1` if no leak in current scope |

Runtime: `runtime/mako_leak.h` (builds on `alloc_track_*` in `mako_std.h`).

---

## 74. Distributed tracing + logs

| Function | Signature | Description |
|----------|-----------|-------------|
| `trace_begin` | `trace_begin(name: string) -> int` | Start a named span under current (or new) trace |
| `trace_end` | `trace_end() -> int` | End span; JSON line to stderr; returns duration ms |
| `trace_id` | `trace_id() -> string` | Install new 128-bit id; return hex (32 chars) |
| `trace_set` | `trace_set(id: string) -> int` | Install existing 32-hex id |
| `trace_current` | `trace_current() -> string` | Current **trace id hex**, or empty |
| `trace_log` | `trace_log(msg: string) -> int` | JSON log line with trace context |
| `trace_clear` | `trace_clear() -> int` | Clear TLS trace state |
| `middleware_trace` | `middleware_trace(c: int) -> string` | Extract/generate trace ID from HTTP request |
| `log_info` / `log_warn` / `log_error` / `log_debug` | `log_*(msg: string)` | Stderr logs; when a trace is active, include `trace=<hex>` |
| `slog_with` | `slog_with(level, msg, key, val)` | Structured log; also emits `trace=` when active |

Runtime: `runtime/mako_trace.h` · log helpers in `mako_std.h` / `mako_goext.h` (include order: trace before std).
Test: `examples/testing/trace_log_test.mko`.

---

## CLI notes

| Flag / command | Meaning |
|----------------|---------|
| `--overflow trap\|wrap\|ignore` | Integer overflow codegen (build/run) |
| `--bounds always` | Keep bounds checks in release |
| `mako test --race` | ThreadSanitizer (also `mako build --sanitize thread`) |
| `mako dev [file]` | Watch mtime → rebuild + rerun (hot reload seed) |
| `./scripts/bench-gate.sh [2.0\|1.5]` | Microbench vs Rust (CI job on ubuntu) |

Tests: `examples/testing/overflow_shutdown_test.mko`. Multi-error recovery:
`examples/bad/multi_error.mko` (`mako check` reports all top-level parse errors).

**CI** (`.github/workflows/ci.yml`): native matrix · cross-smoke · **bench-gate** · **TSan** concurrency smoke (`crew_fan`, `kick_send`, `chan_struct`, `crew_drain`).

---

## 75. Result[T, E] typed errors

| Ok type `T` | Encoding |
|-------------|----------|
| int family / bool | `MakoResultInt.value` via `mako_ok_int` |
| string | `MakoResultInt.ok_s` via `mako_ok_str` |
| float | `MakoResultInt.ok_f` via `mako_ok_float_res` |
| named struct | heap box via `mako_ok_ptr`; match Ok unboxes and frees |
| `[]int` | heap-boxed `MakoIntArray` via `mako_ok_ptr` (`slice`) |
| `[]string` | heap-boxed `MakoStrArray` via `mako_ok_ptr` (`slice_str`) |
| `[]float` | heap-boxed `MakoFloatArray` via `mako_ok_ptr` (`slice_float`) |
| `[]Struct` | heap-boxed `MakoArr_{Name}` via `mako_ok_ptr` (`slice_struct`) |
| `map[string]int` | map pointer via `mako_ok_ptr` (`map_si`) |
| `map[int]int` | map pointer via `mako_ok_ptr` (`map_ii`) |
| `map[string]string` | map pointer via `mako_ok_ptr` (`map_ss`) |
| `Option[U]` | heap-boxed `MakoOptionInt` via `mako_ok_ptr` (`option`) |
| `Result[U, E]` | heap-boxed nested `MakoResultInt` via `mako_ok_ptr` (`result`) |

`Option[T]` uses the same payload slots (`value` / `ok_s` / `ok_f` / ptr). Generic
`Some(x)` / `None` and match `Some(v)` work for int/string/float, boxed containers,
and multi-layer Option nests via kind chains. Nested `Ok(Ok(x))` /
`Result[Result[T, E], E2]` is supported (typecheck pushes expected Result for
inner Ok/Err; codegen boxes the inner Result). Mixed nests work via expected-type context plus **general successive nest-kind
chains** for alternating Result/Option layers (including 5-layer
`Result[Option[Result[Option[Result[T]]]]]`).

| Err type `E` | Encoding |
|--------------|----------|
| string | `err_kind=0`, `err` string (`mako_err_int`) |
| user `enum` | `err_kind=1`, tag + i0..i2 + s0..s1 (`mako_err_enum_ex`); `match Err(e)` reconstructs |

```mko
enum IoError { NotFound, Permission(int), Other(string) }

fn open(code: int) -> Result[int, IoError] {
    if code == 0 { return Ok(1) }
    return Err(NotFound)
}

fn name() -> Result[string, string] {
    return Ok("mako")
}

fn scores() -> Result[map[string]int, string] {
    let mut m = make(map[string]int)
    m["a"] = 1
    return Ok(m)
}

match open(1) {
    Ok(v) => print_int(v),
    Err(e) => match e {
        NotFound => print("missing"),
        Permission(c) => print_int(c),
        Other(msg) => print(msg),
    },
}
```

Tests: `result_enum_test.mko`, `job_join_typed_test.mko` (Result across kick/join),
`wave11_queue_test.mko` (`[]int` Ok), `wave12_queue_test.mko` (`map[string]int` Ok),
`wave13_queue_test.mko` (`map[int]int` / `map[string]string` Ok),
`wave14_queue_test.mko` (`[]string` / `[]float` Ok),
`wave15_queue_test.mko` (`[]Struct` Ok),
`wave16_queue_test.mko` (generic `Result[T, string]` Ok mono scalars),
`wave17_queue_test.mko` (generic mono for `[]int`/`[]string`/`[]Struct`/maps),
`wave18_queue_test.mko` (generic `Option[T]`, nested `Result[Option[T]]`),
`wave19_queue_test.mko` (Option containers, `Option[Option[T]]`, `jpeg_has_sof0`),
`wave20_queue_test.mko` (triple Option nest string/int, Option struct),
`wave21_queue_test.mko` (`Result[Result[T]]`, `wrap_ok(Ok(...))`),
`wave22_queue_test.mko` (`Option[Result[T]]`, `Result[Option[Result[T]]]`),
`wave23_queue_test.mko` (`Option[Result[Option[T]]]` deep mixed nests),
`wave24_queue_test.mko` (5-layer alternating Result/Option),
`wave25_queue_test.mko` (bare None, nested Err, mono either, SOF0 dims),
`wave26_queue_test.mko` (None/Err nest edges, nest3 deep Err, SOF0 precision),
`wave27_queue_test.mko` (nested None edges, SOF0 components),
`wave28_queue_test.mko` (deep None/Err, baseline gray, Tai scripts),
`wave29_queue_test.mko` (4-layer Option/Result, JFIF version, SOF0 sampling),
`wave30_queue_test.mko` (5-layer Result nests, JFIF density, APP7, SOF0 Ci),
`wave31_queue_test.mko` (Option 5-layer, SOF0 Tqi, JFIF thumb, `jpeg_is_mako_jfif`),
`wave32_queue_test.mko` (string nests, SOF0↔APP7 match, EOI, `jpeg_is_mako_complete`),
`wave33_queue_test.mko` (bool deep nests, `jpeg_is_mako_raw`, APP0/APP7 lengths),
`wave34_queue_test.mko` (`Result[Result[string]]`, APP8/APP9, `jpeg_is_mako_dct`/`huff`),
`wave35_queue_test.mko` (float nests, `jpeg_roundtrip_ok`, APP8/9 lengths, Common/Mn/Mc),
`wave36_queue_test.mko` (bool/string nests, APP7 length/SOI layout, Sm/Sk/Pc),
`wave37_queue_test.mko` (Option/Result `?` int/string/float unwrap + early return),
`wave38_queue_test.mko` (`?` struct / nested Option·Result / bool / chained),
`wave39_queue_test.mko` (`?` []int/[]string/[]float and map payloads).

---

## 76. const fn

```mko
const fn double(x: int) -> int { return x * 2 }
const SIZE = double(512)   // folded at compile time to 1024
```

Rules:
- Body may use `let` of const expressions and a final `return` / trailing expr.
- Only integer ops (`+ - * / % & | ^ << >>`).
- Calls with const args are folded to integer literals in generated C.

---

## 77. crew_drain / evloop_shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `crew_drain` | `crew_drain(timeout_ms: int) -> int` | Drain all crew tasks with timeout |
| `evloop_shutdown` | `evloop_shutdown(el: EvLoop) -> int` | Close event-loop backend and free |

