# Collections: maps, slices, and bag values

Everyday data structures in Mako use one monomorphized surface — no special
collection package, no iterator types, no hand-rolled hashes for common keys.

This guide covers:

- Slices `[]T` and nested `[][]T`
- Maps `map[K]V` across the full key/value grid
- Sets, groups, nested maps
- Bag values `map[K]Option[T]` / `map[K]Result[T,E]`
- Wrapping maps in `Option` / `Result`
- Bulk helpers (`maps_*`)

Identity and low-ceremony patterns: [ERGONOMICS.md](../ERGONOMICS.md).  
Full syntax: [GUIDE.md](../GUIDE.md) §4b–4c · book tour: [ch03](../book/src/ch03-language-tour.md).

---

## Slices

```mko
fn main() {
    let mut xs = [1, 2, 3]
    xs = append(xs, 4)
    print(len(xs))           // 4
    print(xs[0])             // 1

    let mid = xs[1:3]        // [2, 3]
    let tail = xs[2:]
    let head = xs[:2]

    // Pre-size when you know capacity
    let mut buf = make([]int, 0, 64)
    buf = append(buf, 10)

    // Nested slices
    let grid: [][]int = [[1, 2], [3, 4]]
    print(grid[0][1])        // 2

    // Bool / string / float / struct / enum elements all work
    let flags: []bool = [true, false]
    let names: []string = ["a", "b"]

    // Option / Result elements (bag slices)
    let mut maybe = make([]Option[int], 0, 4)
    maybe = append(maybe, Some(1))
    maybe = append(maybe, None)
    match maybe[0] {
        Some(v) => print(v),
        None => print("none"),
    }
    let xs: []Option[int] = [Some(10), None]
    let mut tried = make([]Result[string, string], 0, 2)
    tried = append(tried, Ok("yes"))
    tried = append(tried, Err("no"))
}
```

| Op | Notes |
|----|--------|
| `len` / `cap` | length and capacity |
| `s[i]` / `s[i] = v` | bounds-checked (unless `unsafe`) |
| `append(s, v)` | may reallocate; reassign result |
| `s[low:high]` | sub-slice |
| `make([]T, len[, cap])` | allocate (`[]Option[T]` / `[]Result[T,E]` supported) |

---

## Maps — keys and values

**Keys:** `int` · `string` · `float` · `bool` · named **struct** · named **enum**
(including pack-qualified types after `pull`).

**Values:** the same set, plus:

| Value shape | Example |
|-------------|---------|
| Scalar / struct / enum | `map[string]int`, `map[int]Point` |
| Set-style | `map[string]bool` |
| Slice | `map[string][]int`, `map[Point][]string` |
| Nested slice | `map[string][][]int` |
| Nested map (depth 2) | `map[string]map[string]int` |
| Nested map + slices | `map[string]map[string][]int` |
| Slice of maps | `[]map[string]int` |
| Map of slice-of-maps | `map[string][]map[string]int` |
| Option bag | `map[string]Option[int]` |
| Result bag | `map[int]Result[string, string]` |
| Slice of bags | `map[string][]Option[int]`, `map[int][]Result[string,string]` |
| Bag of slices | `map[string]Option[[]int]`, `map[int]Result[[]int,string]` |
| Tuple values | `map[string](int, int)`, `map[string](Point, int)`, `map[K](int,int,int,int)` |
| Bag of maps | `map[string]Option[map[string]int]`, `map[int]Result[map[string]int,string]` |

```mko
struct Point { x: int, y: int }
enum Color { Red, Green }

fn demo_maps() {
    let mut m = make(map[string]int)
    m["a"] = 1
    print(m["a"])            // 1
    print(m["missing"])      // 0 (zero value)
    if has(m, "a") { }
    let v, ok = m["a"]       // comma-ok
    delete(m, "a")

    for k, v in range m {
        print(k)
        print(v)
    }

    // Struct / enum keys (field-wise / tag eq)
    let mut by_pt = make(map[Point]int)
    by_pt[Point { x: 1, y: 2 }] = 10
    let mut by_e = make(map[Color][]string)
    by_e[Red] = ["hot"]
}
```

Float keys: `+0.0` and `-0.0` are the same key; all NaNs share one key.
Missing key → zero value. `len` on a nil map is `0`.

---

## Sets and groups (everyday patterns)

```mko
fn sets_and_groups() {
    // Set
    let mut seen = make(map[string]bool)
    seen["alice"] = true
    if has(seen, "alice") {
        print("known")
    }

    // Group by key
    let mut groups = make(map[string][]int)
    groups["even"] = [2, 4, 6]
    groups["odd"] = [1, 3]
    print(len(groups["even"]))   // 3
    print(groups["even"][0])     // 2
}
```

---

## Nested maps (depth 2)

Build the **inner** map, then store it. Nested-map values are **pointers**;
missing outer key yields a nil map (`len` 0). `maps_clone` / `maps_equal` are
**shallow** (inner pointer identity).

```mko
fn nested_demo() {
    let mut outer = make(map[string]map[string]int)
    let mut row = make(map[string]int)
    row["x"] = 1
    row["y"] = 2
    outer["a"] = row
    print(outer["a"]["x"])       // 1

    // Nested map whose values are slices
    let mut by_user = make(map[string]map[string][]int)
    let mut scores = make(map[string][]int)
    scores["math"] = [90, 95]
    by_user["ada"] = scores
}
```

Depth **3** (`map[K]map[K2]map[K3]V`) is not supported yet.

---

## Bag values: Option and Result on the map

Store nullable or fallible data **per key** without sentinel ints:

```mko
fn bag_demo() {
    let mut maybe = make(map[string]Option[int])
    maybe["a"] = Some(42)
    maybe["b"] = None
    match maybe["a"] {
        Some(v) => print(v),
        None => {},
    }
    // Missing key → None (zero bag)
    match maybe["missing"] {
        Some(_) => {},
        None => print("absent"),
    }

    let mut tried = make(map[int]Result[string, string])
    tried[1] = Ok("yes")
    tried[2] = Err("no")
    match tried[1] {
        Ok(s) => print(s),
        Err(e) => print(e),
    }

    // Named keys work too
    struct Point { x: int, y: int }
    let mut by_pt = make(map[Point]Option[int])
    by_pt[Point { x: 0, y: 0 }] = Some(1)
}
```

Index-assign sets the expected type, so bare `None` / `Err("…")` match the map
value type (not a default `Option[int]`).

Payloads for bag values: int, string, float, bool, named struct, named enum.

---

## Option / Result **of** a map

Wrap a whole map when presence of the map itself is optional or fallible:

```mko
fn opt_map_demo() {
    let mut m = make(map[string]int)
    m["a"] = 2
    let s: Option[map[string]int] = Some(m)
    match s {
        Some(x) => print(x["a"]),
        None => {},
    }

    let r: Result[map[string]int, string] = Ok(m)
    match r {
        Ok(x) => print(x["a"]),
        Err(e) => print(e),
    }
}
```

Also works with float/bool keys and monomorphized map kinds.

---

## Slices of maps

```mko
fn slice_of_maps() {
    let mut rows = make([]map[string]int, 0, 4)
    let mut a = make(map[string]int)
    a["n"] = 1
    rows = append(rows, a)
    print(rows[0]["n"])      // 1
}
```

---

## Bulk helpers

Available for all map kinds (including bag values and nested maps):

| Helper | Role |
|--------|------|
| `maps_keys(m)` | `[]K` |
| `maps_values(m)` | `[]V` (e.g. `[][]int` for slice values, `[]Option[…]` for bags) |
| `maps_clone(m)` | shallow copy |
| `maps_equal(a, b)` | `1` / `0` (structs/enums structural; nested maps: pointer identity) |
| `maps_copy(dst, src)` | copy entries into `dst` |
| `maps_clear(m)` | remove all |

```mko
fn helpers_demo() {
    let mut m = make(map[string]Option[int])
    m["a"] = Some(1)
    m["b"] = Some(2)
    let ks = maps_keys(m)
    let vs = maps_values(m)
    let c = maps_clone(m)
    assert_eq(maps_equal(m, c), 1)
    maps_clear(c)
    print(len(c))            // 0
}
```

Pre-size with a hint: `make(map[string]int, 1024)`.

---

## Quick decision table

| Need | Use |
|------|-----|
| Membership set | `map[K]bool` + `has` |
| Group rows by key | `map[K][]T` |
| Sparse grid / matrix | `map[string][][]int` or nested maps |
| Optional value per key | `map[K]Option[T]` |
| Fallible value per key | `map[K]Result[T,E]` |
| Optional whole table | `Option[map[K]V]` |
| Config tree (2 levels) | `map[K]map[K2]V` |

---

## Tests to learn from

| File | Covers |
|------|--------|
| `examples/testing/map_test.mko` | Core SI/II/SS |
| `examples/testing/map_bool_test.mko` | bool keys/values, sets |
| `examples/testing/map_float_test.mko` | float keys |
| `examples/testing/map_struct_test.mko` | struct values |
| `examples/testing/map_struct_key_test.mko` | struct keys, `map[Struct]Struct` |
| `examples/testing/map_enum_test.mko` | enum keys/values |
| `examples/testing/map_slice_test.mko` | `map[K][]T` |
| `examples/testing/map_nested_test.mko` | `map[K]map[K2]V` |
| `examples/testing/map_nested_slice_test.mko` | `map[K][][]T` |
| `examples/testing/map_map_slice_test.mko` | nested maps + slice values |
| `examples/testing/slice_map_test.mko` | `[]map` / `map[K][]map` |
| `examples/testing/option_map_test.mko` | `Option[map]` / `Result[map]` |
| `examples/testing/map_option_result_test.mko` | bag values |
| `examples/testing/option_result_slice_test.mko` | `[]Option[T]` / `[]Result[T,E]` |
| `examples/testing/map_option_slice_test.mko` | `map[K][]Option[T]` / `map[K][]Result[T,E]` |
| `examples/testing/map_option_of_slice_test.mko` | `map[K]Option[[]T]` / `map[K]Result[[]T,E]` |
| `examples/testing/map_tuple_test.mko` | `map[K](T,U)` scalar tuples |
| `examples/testing/map_tuple_struct_test.mko` | Struct/Enum tuples + 4-tuples |
| `examples/testing/map_option_of_map_test.mko` | `map[K]Option[map]` / `map[K]Result[map]` |
| `examples/testing/nested_slice_test.mko` | `[][]T` |

```bash
mako test examples/testing/map_option_result_test.mko
```

---

## Related

- [ERGONOMICS.md](../ERGONOMICS.md) — short path (sets, groups, bags)
- [GUIDE.md](../GUIDE.md) §4c — full map surface
- [BUILTINS.md](../BUILTINS.md) §6 — `maps_*` signatures
- Book: [Language Tour — Maps](../book/src/ch03-language-tour.md) · [Cookbook](../book/src/ch14-cookbook.md) · [Appendix](../book/src/ch15-appendix.md)
