#!/usr/bin/env python3
"""Generate and measure deterministic Mako compiler-scaling fixtures."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
FIXTURE_CONFIG = REPO_ROOT / "benchmarks" / "compile" / "fixtures.json"
SHAPES = ("single", "multi", "generic", "backend")
COMMAND_TIMEOUT_SECONDS = 300


@dataclass(frozen=True)
class Fixture:
    name: str
    shape: str
    lines: int
    path: Path
    source_files: int


def write_text(path: Path, content: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write(content)


def size_label(lines: int) -> str:
    if lines >= 1_000_000 and lines % 1_000_000 == 0:
        return f"{lines // 1_000_000}m"
    if lines >= 1_000 and lines % 1_000 == 0:
        return f"{lines // 1_000}k"
    return str(lines)


def fit_blocks(
    target: int,
    header: list[str],
    block_size: int,
    make_block,
    footer_size: int,
    make_footer,
) -> list[str]:
    count = (target - len(header) - footer_size) // block_size
    if count < 1:
        raise ValueError(f"target of {target} lines is too small for this fixture")

    lines = list(header)
    for index in range(count):
        block = make_block(index)
        if len(block) != block_size:
            raise AssertionError("fixture block size changed")
        lines.extend(block)

    footer = make_footer(count)
    if len(footer) != footer_size:
        raise AssertionError("fixture footer size changed")
    padding = target - len(lines) - footer_size
    lines.extend("// padding keeps the generated line count exact" for _ in range(padding))
    lines.extend(footer)
    if len(lines) != target:
        raise AssertionError(f"generated {len(lines)} lines, expected {target}")
    return lines


def single_source(target: int) -> dict[str, list[str]]:
    def block(index: int) -> list[str]:
        value = "x + 1" if index == 0 else f"step_{index - 1:06d}(x) + 1"
        return [
            f"fn step_{index:06d}(x: int) -> int {{",
            f"    return {value}",
            "}",
            "",
        ]

    def footer(count: int) -> list[str]:
        return ["fn main() {", f"    print_int(step_{count - 1:06d}(1))", "}"]

    lines = fit_blocks(target, ["pack bench_single", ""], 4, block, 3, footer)
    return {"main.mko": lines}


def multi_source(target: int) -> dict[str, list[str]]:
    file_count = max(4, min(100, target // 1_000))
    support_files = ["lib.mko"] + [
        f"unit_{index:03d}.mko" for index in range(1, file_count - 1)
    ]
    header = ["pack bench_multi", ""]
    footer_size = 3
    available = target - file_count * len(header) - footer_size
    function_count = available // 4
    if function_count < len(support_files):
        raise ValueError(f"target of {target} lines is too small for a multi-file fixture")

    projects = {name: list(header) for name in support_files}
    base, extra = divmod(function_count, len(support_files))
    function_index = 0
    for file_index, name in enumerate(support_files):
        count = base + (1 if file_index < extra else 0)
        for _ in range(count):
            value = (
                "x + 1"
                if function_index == 0
                else f"step_{function_index - 1:06d}(x) + 1"
            )
            projects[name].extend(
                [
                    f"fn step_{function_index:06d}(x: int) -> int {{",
                    f"    return {value}",
                    "}",
                    "",
                ]
            )
            function_index += 1

    main = list(header)
    current = sum(len(lines) for lines in projects.values()) + len(main) + footer_size
    main.extend("// padding keeps the generated line count exact" for _ in range(target - current))
    main.extend(
        ["fn main() {", f"    print_int(step_{function_count - 1:06d}(1))", "}"]
    )
    projects["main.mko"] = main
    if sum(len(lines) for lines in projects.values()) != target:
        raise AssertionError("multi-file fixture line count changed")
    return projects


def generic_source(target: int) -> dict[str, list[str]]:
    def block(index: int) -> list[str]:
        value = "x + 1" if index == 0 else f"generic_step_{index - 1:06d}(x) + 1"
        return [
            f"struct Box_{index:06d}[T] {{",
            "    value: T",
            "}",
            "",
            f"fn generic_step_{index:06d}(x: int) -> int {{",
            f"    let boxed = Box_{index:06d}[int] {{ value: {value} }}",
            "    return boxed.value",
            "}",
            "",
        ]

    def footer(count: int) -> list[str]:
        return [
            "fn main() {",
            f"    print_int(generic_step_{count - 1:06d}(1))",
            "}",
        ]

    lines = fit_blocks(target, ["pack bench_generic", ""], 9, block, 3, footer)
    return {"main.mko": lines}


def backend_source(target: int) -> dict[str, list[str]]:
    # Keep these calls aligned with the stable backend intrinsics used by examples.
    def block(index: int) -> list[str]:
        fallback = (
            '"{\\"error\\":\\"not found\\"}"'
            if index == 0
            else f"route_{index - 1:06d}(path)"
        )
        return [
            f"struct Payload_{index:06d} {{",
            "    id: int",
            "    body: string",
            "}",
            "",
            f"fn decode_{index:06d}(body: string) -> Result[int, string] {{",
            f'    let payload = Payload_{index:06d} {{ id: json_get_int(body, "id"), body: body }}',
            "    if payload.id < 0 {",
            '        return Err("invalid id")',
            "    }",
            "    return Ok(payload.id)",
            "}",
            "",
            f"fn route_{index:06d}(path: string) -> string {{",
            f'    if str_eq(path, "/v1/items/{index:06d}") {{',
            f'        match decode_{index:06d}("{{\\"id\\":1}}") {{',
            "            Ok(_) => {",
            '                return "{\\"ok\\":true}"',
            "            }",
            "            Err(_) => {",
            '                return "{\\"ok\\":false}"',
            "            }",
            "        }",
            "    }",
            f"    return {fallback}",
            "}",
            "",
        ]

    def footer(count: int) -> list[str]:
        return [
            "fn main() {",
            f'    print(route_{count - 1:06d}("/health"))',
            "}",
        ]

    lines = fit_blocks(target, ["pack bench_backend", ""], 27, block, 3, footer)
    return {"main.mko": lines}


GENERATORS = {
    "single": single_source,
    "multi": multi_source,
    "generic": generic_source,
    "backend": backend_source,
}


def source_line_count(path: Path) -> int:
    return sum(
        len(source.read_text(encoding="utf-8").splitlines())
        for source in path.glob("*.mko")
    )


def generate_fixture(root: Path, shape: str, target: int) -> Fixture:
    name = f"{shape}-{size_label(target)}"
    path = root / name
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)

    sources = GENERATORS[shape](target)
    package_name = f"compile_{shape}_{target}"
    write_text(
        path / "mako.toml",
        f'name = "{package_name}"\nversion = "0.0.0"\n',
    )
    for filename, lines in sources.items():
        write_text(path / filename, "\n".join(lines) + "\n")

    actual = source_line_count(path)
    if actual != target:
        raise RuntimeError(f"{name}: generated {actual} source lines, expected {target}")
    metadata = {
        "schema": "mako.compile-fixture.v1",
        "name": name,
        "shape": shape,
        "lines": actual,
        "source_files": sorted(sources),
    }
    write_text(
        path / "fixture.json",
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
    )
    return Fixture(name, shape, actual, path, len(sources))


def load_defaults() -> tuple[list[int], list[str]]:
    config = json.loads(FIXTURE_CONFIG.read_text(encoding="utf-8"))
    if config.get("schema") != "mako.compile-fixtures.v1":
        raise RuntimeError(f"unsupported fixture schema in {FIXTURE_CONFIG}")
    sizes = list(config["sizes"])
    shapes = list(config["shapes"])
    if not sizes or not shapes or any(shape not in SHAPES for shape in shapes):
        raise RuntimeError(f"invalid fixture matrix in {FIXTURE_CONFIG}")
    return sizes, shapes


def find_mako(value: str | None) -> Path:
    candidates: list[Path] = []
    if value:
        supplied = Path(value)
        supplied_paths = (
            [supplied]
            if supplied.is_absolute()
            else [REPO_ROOT / supplied, supplied]
        )
        for path in supplied_paths:
            candidates.append(path)
            if os.name == "nt" and path.suffix.lower() != ".exe":
                candidates.append(path.with_suffix(".exe"))
        found = shutil.which(value)
        if found:
            candidates.append(Path(found))
    else:
        suffix = ".exe" if os.name == "nt" else ""
        candidates.extend(
            [
                REPO_ROOT / "target" / "release" / f"mako{suffix}",
                REPO_ROOT / "target" / "debug" / f"mako{suffix}",
            ]
        )
        found = shutil.which("mako")
        if found:
            candidates.append(Path(found))

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    requested = value or "target/release/mako"
    raise RuntimeError(f"Mako compiler not found ({requested}); build it or pass --mako")


def run_once(command: list[str], env: dict[str, str]) -> float:
    start = time.perf_counter()
    try:
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(
            f"command timed out after {COMMAND_TIMEOUT_SECONDS}s: {' '.join(command)}"
        ) from error
    elapsed = (time.perf_counter() - start) * 1_000
    if result.returncode != 0:
        output = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{output}")
    return elapsed


def command_output(command: list[str]) -> str | None:
    try:
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=COMMAND_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(
            f"command timed out after {COMMAND_TIMEOUT_SECONDS}s: {' '.join(command)}"
        ) from error
    except OSError:
        return None
    output = result.stdout.strip()
    return output if result.returncode == 0 and output else None


def samples(command: list[str], env: dict[str, str], repeat: int) -> list[float]:
    return [run_once(command, env) for _ in range(repeat)]


def summary(values: list[float]) -> dict[str, object]:
    return {
        "median_ms": round(statistics.median(values), 3),
        "samples_ms": [round(value, 3) for value in values],
    }


def clear_directory(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)


def measure_mode(
    mako: Path,
    fixture: Fixture,
    mode: str,
    root: Path,
    repeat: int,
) -> dict[str, object]:
    cache = root / "cache" / fixture.name / mode
    artifacts = root / "artifacts" / fixture.name
    clear_directory(cache)
    if mode == "build":
        clear_directory(artifacts)
    else:
        artifacts.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["MAKO_CACHE"] = str(cache)
    env.setdefault("MAKO_STD", str(REPO_ROOT / "std"))
    env.setdefault("MAKO_RUNTIME", str(REPO_ROOT / "runtime"))

    if mode == "check":
        cold = [str(mako), "check", str(fixture.path), "--no-incremental"]
        warm = [str(mako), "check", str(fixture.path)]
    else:
        output = artifacts / "app"
        cold = [
            str(mako),
            "build",
            str(fixture.path),
            "--no-incremental",
            "-o",
            str(output),
        ]
        warm = [str(mako), "build", str(fixture.path), "-o", str(output)]

    cold_values = samples(cold, env, repeat)
    clear_directory(cache)
    run_once(warm, env)
    warm_values = samples(warm, env, repeat)
    return {"cold": summary(cold_values), "warm": summary(warm_values)}


def parse_args() -> argparse.Namespace:
    default_sizes, default_shapes = load_defaults()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sizes",
        nargs="+",
        type=int,
        default=default_sizes,
        help="source line counts to generate",
    )
    parser.add_argument(
        "--shapes",
        nargs="+",
        choices=SHAPES,
        default=default_shapes,
        help="project shapes to generate",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(".mako/compile-bench"),
        help="generated fixture and cache root, relative to the repository",
    )
    parser.add_argument(
        "--mako",
        help="compiler executable, resolved from the repository or PATH",
    )
    parser.add_argument("--repeat", type=int, default=3, help="recorded runs per metric")
    parser.add_argument("--build", action="store_true", help="also measure full debug builds")
    parser.add_argument(
        "--generate-only",
        action="store_true",
        help="generate and validate fixtures without measuring them",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="JSON report path, relative to the repository",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.repeat < 1:
        raise RuntimeError("--repeat must be at least 1")
    if any(size < 100 for size in args.sizes):
        raise RuntimeError("fixture sizes must be at least 100 lines")

    root = args.root if args.root.is_absolute() else REPO_ROOT / args.root
    fixtures = [
        generate_fixture(root / "fixtures", shape, size)
        for size in dict.fromkeys(args.sizes)
        for shape in dict.fromkeys(args.shapes)
    ]
    for fixture in fixtures:
        print(
            f"generated {fixture.name}: {fixture.lines} lines "
            f"across {fixture.source_files} source file(s)"
        )

    if args.generate_only:
        return 0

    mako = find_mako(args.mako)
    results = []
    for fixture in fixtures:
        metrics = {
            "check": measure_mode(mako, fixture, "check", root, args.repeat)
        }
        if args.build:
            metrics["build"] = measure_mode(mako, fixture, "build", root, args.repeat)
        result = {
            "name": fixture.name,
            "shape": fixture.shape,
            "lines": fixture.lines,
            "source_files": fixture.source_files,
            "metrics": metrics,
        }
        results.append(result)
        check = metrics["check"]
        print(
            f"{fixture.name}: check cold {check['cold']['median_ms']:.1f} ms, "
            f"warm {check['warm']['median_ms']:.1f} ms"
        )
        if args.build:
            build = metrics["build"]
            print(
                f"{fixture.name}: build cold {build['cold']['median_ms']:.1f} ms, "
                f"warm {build['warm']['median_ms']:.1f} ms"
            )

    mako_version = command_output([str(mako), "--version"])
    source_commit = command_output(["git", "rev-parse", "HEAD"])
    if mako_version is None:
        print("bench-compile: warning: compiler version is unavailable", file=sys.stderr)
    if source_commit is None:
        print("bench-compile: warning: source commit is unavailable", file=sys.stderr)

    report = {
        "schema": "mako.compile-bench.v1",
        "host": {
            "os": platform.system(),
            "release": platform.release(),
            "arch": platform.machine(),
            "processor": platform.processor(),
        },
        "mako": {
            "path": str(mako),
            "version": mako_version,
            "source_commit": source_commit,
        },
        "repeat": args.repeat,
        "fixtures": results,
    }
    if args.output:
        output = args.output if args.output.is_absolute() else REPO_ROOT / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        write_text(
            output,
            json.dumps(report, indent=2, sort_keys=True) + "\n",
        )
        print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"bench-compile: {error}", file=sys.stderr)
        raise SystemExit(1)
