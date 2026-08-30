#!/usr/bin/env python3
"""Generate Mako's immutable Unicode 17 lookup tables from the official UCD.

The generator is intentionally dependency-free. It validates scalar bounds and
range ordering, merges adjacent ranges, and emits read-only C and Rust tables.
It does not run during a normal build; generated outputs are committed so
offline/reproducible builds never depend on unicode.org availability.
"""

from __future__ import annotations

import argparse
import pathlib
import re
from collections import defaultdict
from dataclasses import dataclass

VERSION = "17.0.0"
ARCHIVE_SHA256 = "2066d1909b2ea93916ce092da1c0ee4808ea3ef8407c94b4f14f5b7eb263d28e"
MAX_SCALAR = 0x10FFFF


@dataclass(frozen=True)
class UnicodeMaps:
    lower: list[tuple[int, int]]
    upper: list[tuple[int, int]]
    title: list[tuple[int, int]]
    combining: list[tuple[int, int]]
    decompositions: list[tuple[int, bool, tuple[int, ...]]]
    compositions: list[tuple[int, int, int]]


def parse_range(raw: str) -> tuple[int, int]:
    parts = raw.strip().split("..", 1)
    lo = int(parts[0], 16)
    hi = int(parts[1], 16) if len(parts) == 2 else lo
    if not (0 <= lo <= hi <= MAX_SCALAR):
        raise ValueError(f"invalid Unicode range: {raw}")
    return lo, hi


def merge_ranges(values: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[tuple[int, int]] = []
    for lo, hi in sorted(values):
        if merged and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    return merged


def property_file(path: pathlib.Path) -> dict[str, list[tuple[int, int]]]:
    result: dict[str, list[tuple[int, int]]] = defaultdict(list)
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        fields = [field.strip() for field in body.split(";")]
        if len(fields) < 2:
            raise ValueError(f"{path}:{line_no}: malformed property record")
        result[fields[1]].append(parse_range(fields[0]))
    return {name: merge_ranges(ranges) for name, ranges in result.items()}


def general_categories(path: pathlib.Path) -> dict[str, list[tuple[int, int]]]:
    result: dict[str, list[tuple[int, int]]] = defaultdict(list)
    pending: tuple[int, str] | None = None
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split(";")
        if len(fields) != 15:
            raise ValueError(f"{path}:{line_no}: expected 15 UnicodeData fields")
        cp, name, category = int(fields[0], 16), fields[1], fields[2]
        if name.endswith(", First>"):
            if pending is not None:
                raise ValueError(f"{path}:{line_no}: nested First range")
            pending = (cp, category)
        elif name.endswith(", Last>"):
            if pending is None or pending[1] != category:
                raise ValueError(f"{path}:{line_no}: unmatched Last range")
            result[category].append((pending[0], cp))
            pending = None
        else:
            result[category].append((cp, cp))
    if pending is not None:
        raise ValueError(f"{path}: unterminated First range")
    merged = {name: merge_ranges(ranges) for name, ranges in result.items()}
    merged["Assigned"] = merge_ranges(
        [item for ranges in result.values() for item in ranges]
    )
    return merged


def codepoint_set(path: pathlib.Path) -> set[int]:
    values: set[int] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        lo, hi = parse_range(body.split(";", 1)[0])
        values.update(range(lo, hi + 1))
    return values


def unicode_maps(data_path: pathlib.Path, exclusions_path: pathlib.Path) -> UnicodeMaps:
    lower: list[tuple[int, int]] = []
    upper: list[tuple[int, int]] = []
    title: list[tuple[int, int]] = []
    combining: list[tuple[int, int]] = []
    decompositions: list[tuple[int, bool, tuple[int, ...]]] = []
    canonical_pairs: list[tuple[int, int, int]] = []
    exclusions = codepoint_set(exclusions_path)
    for line_no, line in enumerate(data_path.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split(";")
        if len(fields) != 15:
            raise ValueError(f"{data_path}:{line_no}: expected 15 UnicodeData fields")
        cp = int(fields[0], 16)
        ccc = int(fields[3])
        if not 0 <= ccc <= 255:
            raise ValueError(f"{data_path}:{line_no}: invalid combining class")
        if ccc != 0:
            combining.append((cp, ccc))
        for target, output in ((fields[13], lower), (fields[12], upper), (fields[14], title)):
            if target:
                output.append((cp, int(target, 16)))
        raw_decomp = fields[5].strip()
        if raw_decomp:
            parts = raw_decomp.split()
            compatibility = parts[0].startswith("<")
            if compatibility:
                parts = parts[1:]
            mapping = tuple(int(part, 16) for part in parts)
            if not mapping:
                raise ValueError(f"{data_path}:{line_no}: empty decomposition")
            decompositions.append((cp, compatibility, mapping))
            if not compatibility and len(mapping) == 2 and cp not in exclusions:
                canonical_pairs.append((mapping[0], mapping[1], cp))
    canonical_pairs.sort()
    return UnicodeMaps(lower, upper, title, combining, decompositions, canonical_pairs)


def c_ident(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()


def emit_c(
    groups: dict[str, dict[str, list[tuple[int, int]]]], maps: UnicodeMaps
) -> str:
    for name, entries in (
        ("lower", maps.lower),
        ("upper", maps.upper),
        ("title", maps.title),
        ("combining", maps.combining),
    ):
        if any(entries[i - 1][0] >= entries[i][0] for i in range(1, len(entries))):
            raise ValueError(f"Unicode {name} map keys are not strictly increasing")
    if any(
        maps.compositions[i - 1][:2] >= maps.compositions[i][:2]
        for i in range(1, len(maps.compositions))
    ):
        raise ValueError("Unicode composition keys are not strictly increasing")
    lines = [
        "/* Generated by scripts/generate-unicode17.py; DO NOT EDIT. */",
        "#ifndef MAKO_UNICODE17_DATA_H",
        "#define MAKO_UNICODE17_DATA_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        f'#define MAKO_UNICODE_VERSION "{VERSION}"',
        "typedef struct { uint32_t lo; uint32_t hi; } MakoUnicodeRange;",
        "typedef struct { const char *name; const MakoUnicodeRange *ranges; size_t len; } MakoUnicodeProperty;",
        "static inline int mako_u17_in_ranges(uint32_t cp, const MakoUnicodeRange *ranges, size_t len) {",
        "    size_t lo = 0;",
        "    size_t hi = len;",
        "    while (lo < hi) {",
        "        size_t mid = lo + (hi - lo) / 2;",
        "        if (cp < ranges[mid].lo) hi = mid;",
        "        else if (cp > ranges[mid].hi) lo = mid + 1;",
        "        else return 1;",
        "    }",
        "    return 0;",
        "}",
    ]
    registry: list[tuple[str, str]] = []
    symbols: dict[str, str] = {}
    for namespace, properties in sorted(groups.items()):
        for name, ranges in sorted(properties.items()):
            symbol = f"mako_u17_{c_ident(namespace)}_{c_ident(name)}"
            qualified = f"{namespace}:{name}"
            if symbol in symbols:
                raise ValueError(
                    f"generated C symbol collision: {qualified} and {symbols[symbol]}"
                )
            symbols[symbol] = qualified
            lines.append(f"static const MakoUnicodeRange {symbol}[] = {{")
            lines.extend(f"    {{0x{lo:X}u, 0x{hi:X}u}}," for lo, hi in ranges)
            lines.append("};")
            registry.append((qualified, symbol))
    lines.extend(
        [
            "#define MAKO_U17_HAS(symbol, cp) mako_u17_in_ranges((cp), (symbol), sizeof(symbol) / sizeof((symbol)[0]))",
            "static inline int mako_u17_is_letter(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_lu, cp) || MAKO_U17_HAS(mako_u17_gc_ll, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_lt, cp) || MAKO_U17_HAS(mako_u17_gc_lm, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_lo, cp);",
            "}",
            "static inline int mako_u17_is_number(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_nd, cp) || MAKO_U17_HAS(mako_u17_gc_nl, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_no, cp);",
            "}",
            "static inline int mako_u17_is_punct(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_pc, cp) || MAKO_U17_HAS(mako_u17_gc_pd, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_ps, cp) || MAKO_U17_HAS(mako_u17_gc_pe, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_pi, cp) || MAKO_U17_HAS(mako_u17_gc_pf, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_po, cp);",
            "}",
            "static inline int mako_u17_is_symbol(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_sm, cp) || MAKO_U17_HAS(mako_u17_gc_sc, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_sk, cp) || MAKO_U17_HAS(mako_u17_gc_so, cp);",
            "}",
            "static inline int mako_u17_is_mark(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_mn, cp) || MAKO_U17_HAS(mako_u17_gc_mc, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_me, cp);",
            "}",
            "static inline int mako_u17_is_separator(uint32_t cp) {",
            "    return MAKO_U17_HAS(mako_u17_gc_zs, cp) || MAKO_U17_HAS(mako_u17_gc_zl, cp) ||",
            "           MAKO_U17_HAS(mako_u17_gc_zp, cp);",
            "}",
            "static inline int mako_u17_is_graphic(uint32_t cp) {",
            "    return mako_u17_is_letter(cp) || mako_u17_is_mark(cp) || mako_u17_is_number(cp) ||",
            "           mako_u17_is_punct(cp) || mako_u17_is_symbol(cp) || mako_u17_is_separator(cp);",
            "}",
        ]
    )
    lines.extend(
        [
            "typedef struct { uint32_t key; uint32_t value; } MakoUnicodeMap;",
            "typedef struct { uint32_t cp; uint32_t offset; uint16_t len; uint8_t compatibility; } MakoUnicodeDecomposition;",
            "typedef struct { uint32_t first; uint32_t second; uint32_t composed; } MakoUnicodeComposition;",
        ]
    )
    for name, entries in (("lower", maps.lower), ("upper", maps.upper), ("title", maps.title), ("combining", maps.combining)):
        lines.append(f"static const MakoUnicodeMap mako_u17_{name}_map[] = {{")
        lines.extend(f"    {{0x{key:X}u, 0x{value:X}u}}," for key, value in entries)
        lines.append("};")
    decomposition_data: list[int] = []
    lines.append("static const MakoUnicodeDecomposition mako_u17_decompositions[] = {")
    for cp, compatibility, mapping in maps.decompositions:
        offset = len(decomposition_data)
        if len(mapping) > 0xFFFF or offset > 0xFFFFFFFF:
            raise ValueError(f"Unicode decomposition table overflow at U+{cp:04X}")
        decomposition_data.extend(mapping)
        lines.append(
            f"    {{0x{cp:X}u, {offset}u, {len(mapping)}u, {1 if compatibility else 0}u}},"
        )
    lines.append("};")
    if len(decomposition_data) > 0xFFFFFFFF:
        raise ValueError("Unicode decomposition data exceeds uint32 offsets")
    lines.append("static const uint32_t mako_u17_decomposition_data[] = {")
    for start in range(0, len(decomposition_data), 12):
        chunk = decomposition_data[start : start + 12]
        lines.append("    " + ", ".join(f"0x{cp:X}u" for cp in chunk) + ",")
    lines.append("};")
    lines.append("static const MakoUnicodeComposition mako_u17_compositions[] = {")
    lines.extend(
        f"    {{0x{first:X}u, 0x{second:X}u, 0x{composed:X}u}},"
        for first, second, composed in maps.compositions
    )
    lines.append("};")
    lines.append("static const MakoUnicodeProperty mako_u17_properties[] = {")
    for name, symbol in registry:
        lines.append(f'    {{"{name}", {symbol}, sizeof({symbol}) / sizeof({symbol}[0])}},')
    lines.extend(
        [
            "};",
            "static const size_t mako_u17_properties_len = sizeof(mako_u17_properties) / sizeof(mako_u17_properties[0]);",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def emit_rust(xid_start: list[tuple[int, int]], xid_continue: list[tuple[int, int]]) -> str:
    def table(name: str, ranges: list[tuple[int, int]]) -> list[str]:
        out = [f"pub static {name}: &[(u32, u32)] = &["]
        out.extend(f"    (0x{lo:X}, 0x{hi:X})," for lo, hi in ranges)
        out.append("];")
        return out

    lines = [
        "// Generated by scripts/generate-unicode17.py; DO NOT EDIT.",
        f'pub const UNICODE_VERSION: &str = "{VERSION}";',
        "pub const UCD_ARCHIVE_SHA256: &str =",
        f'    "{ARCHIVE_SHA256}";',
    ]
    lines.extend(table("XID_START", xid_start))
    lines.extend(table("XID_CONTINUE", xid_continue))
    lines.extend(
        [
            "#[inline]",
            "pub fn in_ranges(cp: u32, ranges: &[(u32, u32)]) -> bool {",
            "    ranges",
            "        .binary_search_by(|&(lo, hi)| {",
            "            if cp < lo {",
            "                std::cmp::Ordering::Greater",
            "            } else if cp > hi {",
            "                std::cmp::Ordering::Less",
            "            } else {",
            "                std::cmp::Ordering::Equal",
            "            }",
            "        })",
            "        .is_ok()",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ucd", required=True, type=pathlib.Path)
    parser.add_argument("--repo", default=pathlib.Path(__file__).resolve().parents[1], type=pathlib.Path)
    args = parser.parse_args()
    root, repo = args.ucd, args.repo
    required = [
        "UnicodeData.txt", "DerivedCoreProperties.txt", "PropList.txt", "Scripts.txt",
        "auxiliary/GraphemeBreakProperty.txt", "auxiliary/WordBreakProperty.txt",
        "emoji/emoji-data.txt",
    ]
    missing = [name for name in required if not (root / name).is_file()]
    if missing:
        raise SystemExit("missing UCD inputs: " + ", ".join(missing))

    core = property_file(root / "DerivedCoreProperties.txt")
    groups = {
        "gc": general_categories(root / "UnicodeData.txt"),
        "core": core,
        "prop": property_file(root / "PropList.txt"),
        "script": property_file(root / "Scripts.txt"),
        "gcb": property_file(root / "auxiliary/GraphemeBreakProperty.txt"),
        "wb": property_file(root / "auxiliary/WordBreakProperty.txt"),
        "emoji": property_file(root / "emoji/emoji-data.txt"),
    }
    for namespace, properties in groups.items():
        if not properties:
            raise SystemExit(f"empty Unicode property namespace: {namespace}")
    maps = unicode_maps(root / "UnicodeData.txt", root / "CompositionExclusions.txt")
    c_output = emit_c(groups, maps)
    rust_output = emit_rust(core["XID_Start"], core["XID_Continue"])
    (repo / "runtime/mako_unicode17_data.h").write_text(c_output, encoding="utf-8")
    (repo / "src/unicode17_data.rs").write_text(rust_output, encoding="utf-8")
    print(f"generated Unicode {VERSION}: {sum(len(v) for g in groups.values() for v in g.values())} merged ranges")


if __name__ == "__main__":
    main()
