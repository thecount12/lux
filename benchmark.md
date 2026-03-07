# Lux Performance Benchmarks

## Scope

Do not mix results across hardware or OS environments.

- `Intel i5 + Plan 9/macOS` data is kept separate.
- `Apple Silicon M2 + macOS POSIX` data is kept separate.
- No cross-table relative claims should be made.

## Common Benchmark Setup

- Algorithm: iterative Fibonacci (`O(n)`)
- Test case: `fib(50)` = `12586269025`

## Dataset A: Intel i5 (Historical, Mixed OS)

Hardware: `2.6 GHz Dual-Core Intel Core i5`

| Implementation | Platform | Time (us) |
|---|---|---:|
| Plan 9 C | Plan 9 | 1 |
| POSIX C | macOS | 3 |
| Python 3 | macOS | 7 |
| Lux (POSIX) | macOS | 13 |
| Lux (Plan 9) | Plan 9 | 17.88 |

Notes:
- This table is only valid within the Intel i5 environment.
- It includes both Plan 9-native and macOS POSIX measurements taken on that same Intel hardware.

## Dataset B: Apple Silicon M2 (Current, POSIX-only)

Environment: current local macOS run via `benchmark/run_bench.py`.

Runner output (9 repeats each, seconds):

| Implementation | Min (s) | Median (s) | Max (s) |
|---|---:|---:|---:|
| Python | 0.429167000 | 0.432679459 | 0.451138209 |
| POSIX C | 0.000001000 | 0.000001000 | 0.000004000 |
| Lux POSIX | 1.283121000 | 1.327808000 | 1.406099000 |

Notes:
- No Plan 9-native result is included in this table.
- This table must not be merged with Intel i5 data.

## Benchmark Files

- `benchmark/fib_bench.py`
- `benchmark/fib_bench.c`
- `benchmark/fib_bench.lux`
- `benchmark/run_bench.py`
- `tests/fib-plan.c`
- `tests/fib-posix.c`
- `tests/fib2.py`
- `tests/fib2.lux`

## Run Commands

### Unified POSIX Harness (macOS/Linux)

```sh
python3 benchmark/run_bench.py
```

### Plan 9 Native Spot Check

```sh
6c -o fib-plan.6 tests/fib-plan.c
6l -o fib-plan fib-plan.6
./fib-plan
8.out tests/fib2.lux
```
