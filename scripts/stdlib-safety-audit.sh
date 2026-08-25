#!/usr/bin/env bash
# Static audit for stdlib memory-safety claims.
#
# This does not replace sanitizer or runtime tests. It prevents claim drift:
# every checked-in std package must appear in the safety matrix, every matrix
# entry must refer to a real package, high-risk native wrapper categories must
# keep adversarial/lifecycle fixtures checked in, and every checked-native row
# must have package-local evidence so broad family coverage cannot hide gaps.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

python3 - "$ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
matrix_path = root / "docs" / "STDLIB_SAFETY_MATRIX.md"
contracts_path = root / "docs" / "STDLIB_SAFETY_CONTRACTS.md"
testing_dir = root / "examples" / "testing"
package_evidence_path = testing_dir / "stdlib_package_local_safety_test.mko"

if not matrix_path.exists():
    print("stdlib-safety-audit: missing docs/STDLIB_SAFETY_MATRIX.md", file=sys.stderr)
    raise SystemExit(1)
if not contracts_path.exists():
    print("stdlib-safety-audit: missing docs/STDLIB_SAFETY_CONTRACTS.md", file=sys.stderr)
    raise SystemExit(1)
if not package_evidence_path.exists():
    print("stdlib-safety-audit: missing examples/testing/stdlib_package_local_safety_test.mko", file=sys.stderr)
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

contract_text = contracts_path.read_text(encoding="utf-8")
contract_families = {
    "codec-parser": {
        "prefixes": (
            "archive/",
            "compress/",
            "diameter/",
            "encoding/",
            "graphql/",
            "grpc/",
            "html/template/",
            "image/draw/",
            "image/gif/",
            "image/jpeg/",
            "image/png/",
            "log/syslog/",
            "math/big/",
            "mime/multipart/",
            "mime/quotedprintable/",
            "net/mail/",
            "net/rpc/",
            "net/textproto/",
            "openapi/",
            "regexp/",
            "sip/",
            "strconv/",
            "text/scanner/",
            "text/tabwriter/",
            "text/template/",
        ),
        "markers": {
            "stdlib_parity_adversarial_test.mko": [
                "TestCompressAdversarial",
                "TestAsn1Edges",
                "TestCookieJarHostIsolation",
            ],
            "cbor_msgpack_test.mko": ["TestCbor", "TestMsgpack"],
            "avro_graphql_tz_test.mko": ["TestAvro", "TestGraphql"],
            "grpc_frame_test.mko": ["TestGrpc"],
            "sip_test.mko": ["sip_header", "sip_msg_complete"],
            "yaml_toml_test.mko": ["TestYaml", "TestToml"],
            "serialization_formats_test.mko": ["TestYamlTomlSeeds", "TestBinaryFormatSeeds"],
            "stdlib_codec_malformed_test.mko": [
                "TestCodecMalformedPackageLocal",
                "TestArchiveImageMalformedPackageLocal",
                "TestProtocolMalformedPackageLocal",
            ],
        },
    },
    "crypto-secret": {
        "prefixes": ("crypto/", "hash/", "uuid/"),
        "markers": {
            "security_crypto_test.mko": [
                "TestSecretLenAndEq",
                "TestHkdfSha256Rfc5869",
                "TestTlsClientSurface",
            ],
            "stdlib_parity_adversarial_test.mko": [
                "TestHashVectors",
                "constant_eq_ok",
                "rsa.can_sign",
            ],
            "password_hash_test.mko": ["malformed hash"],
            "tls_pool_test.mko": [
                "tls_pool_open_timeout",
                "tls_pool_close(-1)",
                "TestTlsPoolHostInjectionRejected",
            ],
            "uuid_test.mko": ["uuid_parse_ok(\"\")", "uuid_from_bytes", "ulid_parse_ok"],
        },
    },
    "handle-lifecycle": {
        "prefixes": (
            "bufio/",
            "database/sql/",
            "dtls/",
            "expvar/",
            "fmt/",
            "io/",
            "llm/",
            "log/log.mko",
            "log/slog/",
            "messaging/",
            "net/http/",
            "net/net.mko",
            "net/smtp/",
            "net/wss.mko",
            "os/",
            "peer/",
            "plugin/",
            "print/",
            "runtime/",
            "sctp/",
            "sync/",
            "syscall/",
            "testing/httptest/",
            "time/",
            "timer/",
            "unique/",
        ),
        "markers": {
            "adapters_adversarial_test.mko": [
                "TestMqDoubleFreeAndBadId",
                "TestGrpcServiceDoubleFree",
                "TestLeakScopeAdapters",
            ],
            "sql_rows_test.mko": ["sql_rows_close", "sql_rows_next", "sql_rows_ok(0)"],
            "sql_tx_stmt_test.mko": ["sql_stmt_close", "sql_prepare", "TestSqlStatementInvalid"],
            "file_io_adversarial_test.mko": ["TestManySmallCyclesNoLeak", "TestAppendStressNoLeak"],
            "net_lowlevel_test.mko": ["tcp_close", "udp_bind_addr", "io_set_nonblocking"],
            "udp_proxy_test.mko": ["game_udp_close", "game_udp_fd"],
            "runtime_stats_test.mko": ["TestRuntimeStatsChannels", "TestRuntimeStatsSelectTimeout"],
            "syscall_full_test.mko": ["syscall_close", "syscall_pipe", "TestSyscallAccessErrno"],
            "plugin_product_test.mko": ["plugin_close", "plugin_close_all", "plugin_manifest_artifact"],
            "watch_test.mko": ["watch_close", "/no/such/path"],
            "messaging_queue_test.mko": ["mq_free", "mq_purge", "full"],
        },
    },
}

def contract_family_for(name):
    matches = [
        family
        for family, spec in contract_families.items()
        if any(name.startswith(prefix) or name == prefix for prefix in spec["prefixes"])
    ]
    return matches

missing_contract_families = []
ambiguous_contract_families = []
for name, (tier, _) in rows.items():
    if tier not in {"checked-native", "unsafe-boundary"}:
        continue
    matches = contract_family_for(name)
    if not matches:
        missing_contract_families.append(name)
    elif len(matches) > 1:
        ambiguous_contract_families.append(f"{name}: {', '.join(matches)}")

if missing_contract_families:
    failures.append(
        "native/unsafe rows without a safety contract family:\n  "
        + "\n  ".join(sorted(missing_contract_families))
    )
if ambiguous_contract_families:
    failures.append(
        "native/unsafe rows with ambiguous safety contract families:\n  "
        + "\n  ".join(sorted(ambiguous_contract_families))
    )

tests = {
    path.name: path.read_text(encoding="utf-8", errors="replace")
    for path in testing_dir.glob("*.mko")
}
package_evidence_text = package_evidence_path.read_text(encoding="utf-8", errors="replace")

missing_package_evidence = []
missing_unsafe_exclusions = []
for name, (tier, _) in rows.items():
    if tier == "checked-native":
        marker = f"package-local checked-native: std/{name}"
        if marker not in package_evidence_text:
            missing_package_evidence.append(name)
    elif tier == "unsafe-boundary":
        marker = f"unsafe-boundary excluded-from-default-safe: std/{name}"
        if marker not in package_evidence_text:
            missing_unsafe_exclusions.append(name)

if missing_package_evidence:
    failures.append(
        "checked-native rows without package-local evidence:\n  "
        + "\n  ".join(sorted(missing_package_evidence))
    )
if missing_unsafe_exclusions:
    failures.append(
        "unsafe-boundary rows without explicit default-safe exclusion evidence:\n  "
        + "\n  ".join(sorted(missing_unsafe_exclusions))
    )

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

family_failures = []
for family, spec in contract_families.items():
    if f"### `{family}`" not in contract_text:
        family_failures.append(f"{family}: missing contract section")
    for filename, needles in spec["markers"].items():
        body = tests.get(filename)
        if body is None:
            family_failures.append(f"{family}: {filename}: missing fixture")
            continue
        missing_needles = [needle for needle in needles if needle not in body]
        if missing_needles:
            family_failures.append(
                f"{family}: {filename}: missing marker(s): " + ", ".join(missing_needles)
            )

if family_failures:
    failures.append(
        "missing safety-contract family evidence:\n  " + "\n  ".join(sorted(family_failures))
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
    f"contract-families={len(contract_families)}; "
    "package-local evidence present; required adversarial/lifecycle fixtures present"
)
PY
