# Mako benchmark plan

The benchmark product goal is not to claim that Mako beats every language. The
goal is to make backend-service regressions visible and reproducible.

Run the current suite wrapper:

```bash
sh scripts/product-bench.sh
```

## Required Workloads

| Workload | Why It Matters | Current Entry Point |
|----------|----------------|---------------------|
| Compile latency | Fast edit/test loop is part of the product | `python3 scripts/bench-compile.py --output out/compile-bench.json` |
| Native microkernels | Guards scalar, slice, map, string, I/O regressions | `./scripts/native-bench-gate.sh` |
| Rust/C parity gate | Keeps basic hot paths honest | `./scripts/bench-gate.sh` |
| HTTP throughput/latency | Backend-service wedge | `./scripts/bench-http.sh` |
| HTTP long-run RSS | Long-running service trust | `./scripts/http-long-run-soak.sh` |
| Runtime long-run RSS | Ownership/drop stability | `./scripts/long-run-soak.sh` |

## Publishable Result Rules

A benchmark result is publishable only when it includes:

| Field | Required |
|-------|----------|
| Hardware | CPU, core count, RAM, OS |
| Mako build | commit, version, backend, flags |
| Workload | source file, request body/response size, TLS on/off, logging on/off |
| Load generator | tool, version, command, duration, concurrency |
| Result | median, p95/p99 when applicable, errors, CPU, peak RSS |

If any field is missing, call it a local experiment instead of a benchmark.

## Open Gaps

- JSON encode/decode throughput needs a dedicated script.
- Channel send/receive latency and contention need a dedicated script.
- Allocation pressure needs a common report shape across runtime and HTTP soaks.
- LLVM release numbers should be separated from Cranelift debug/native numbers.
