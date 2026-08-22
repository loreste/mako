#!/usr/bin/env bash
# Static audit for stdlib memory-safety claims.
#
# This does not replace sanitizer or runtime tests. It prevents claim drift:
# every checked-in std package must appear in the safety matrix, every matrix
# entry must refer to a real package, and high-risk native wrapper categories
# must keep adversarial/lifecycle fixtures checked in.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

python3 - "$ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
matrix_path = root / "docs" / "STDLIB_SAFETY_MATRIX.md"
testing_dir = root / "examples" / "testing"

if not matrix_path.exists():
    print("stdlib-safety-audit: missing docs/STDLIB_SAFETY_MATRIX.md", file=sys.stderr)
    raise SystemExit(1)

std_files = {
    str(path.relative_to(root / "std"))
    for path in (root / "std").rglob("*.mko")
}

row_re = re.compile(r"^\| `([^`]+)` \| `([^`]+)` \| ([^|]+) \|$")
rows = {}
for line in matrix_path.read_text(encoding="utf-8").splitlines():
    m = row_re.match(line.strip())
    if not m:
        continue
    file_name, tier, required = m.groups()
    rows[file_name] = (tier, required.strip())

matrix_files = set(rows)
missing = sorted(std_files - matrix_files)
extra = sorted(matrix_files - std_files)
bad_tiers = sorted(
    f"{name}: {tier}"
    for name, (tier, _) in rows.items()
    if tier not in {"safe", "checked-native", "unsafe-boundary", "blocked", "won't"}
)
weak_requirements = sorted(
    name
    for name, (tier, required) in rows.items()
    if tier in {"checked-native", "unsafe-boundary", "blocked"}
    and (
        not required
        or required == "-"
        or required.lower() in {"none", "tbd", "todo"}
        or "test" not in required.lower()
    )
)

failures = []
if missing:
    failures.append("missing matrix rows for std files:\n  " + "\n  ".join(missing))
if extra:
    failures.append("matrix rows without std files:\n  " + "\n  ".join(extra))
if bad_tiers:
    failures.append("unknown matrix tiers:\n  " + "\n  ".join(bad_tiers))
if weak_requirements:
    failures.append(
        "native/unsafe rows without explicit test requirement:\n  "
        + "\n  ".join(weak_requirements)
    )

tests = {
    path.name: path.read_text(encoding="utf-8", errors="replace")
    for path in testing_dir.glob("*.mko")
}

coverage_requirements = {
    "stdlib_parity_adversarial_test.mko": [
        "TestCompressAdversarial",
        "TestAsn1Edges",
        "TestCookieJarHostIsolation",
        "constant_eq_ok",
    ],
    "adapters_adversarial_test.mko": [
        "TestMqDoubleFreeAndBadId",
        "TestGrpcServiceDoubleFree",
        "TestLeakScopeAdapters",
        "graphql_schema_free",
    ],
    "tls_pool_test.mko": [
        "tls_pool_close(-1)",
        "tls_pool_open_timeout",
        "tls_pool_open_mtls",
    ],
    "uuid_test.mko": [
        "uuid_parse_ok(\"\")",
        "uuid_from_bytes",
        "ulid_parse_ok",
    ],
    "sql_rows_test.mko": [
        "sql_rows_close",
        "sql_rows_next",
    ],
    "sql_tx_stmt_test.mko": [
        "sql_prepare",
        "sql_stmt_close",
    ],
    "watch_test.mko": [
        "watch_close",
        "/no/such/path",
    ],
    "sip_test.mko": [
        "sip_header",
        "sip_msg_complete",
    ],
    "unicode_full_test.mko": [
        "utf8_valid",
        "unicode_is",
    ],
}

coverage_failures = []
for filename, needles in coverage_requirements.items():
    body = tests.get(filename)
    if body is None:
        coverage_failures.append(f"{filename}: missing fixture")
        continue
    missing_needles = [needle for needle in needles if needle not in body]
    if missing_needles:
        coverage_failures.append(
            f"{filename}: missing coverage marker(s): " + ", ".join(missing_needles)
        )

if coverage_failures:
    failures.append(
        "missing required adversarial/lifecycle coverage markers:\n  "
        + "\n  ".join(coverage_failures)
    )

if failures:
    print("stdlib-safety-audit: FAILED", file=sys.stderr)
    for failure in failures:
        print("\n" + failure, file=sys.stderr)
    raise SystemExit(1)

counts = {}
for tier, _ in rows.values():
    counts[tier] = counts.get(tier, 0) + 1
ordered = ", ".join(f"{tier}={counts[tier]}" for tier in sorted(counts))
print(
    f"stdlib-safety-audit: {len(std_files)} std files covered; {ordered}; "
    "required adversarial/lifecycle fixtures present"
)
PY
