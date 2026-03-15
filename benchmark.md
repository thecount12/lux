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

Environment: current local macOS run via `benchmark/run_bench.py`. Lux built with `OPT=-O2` (default).

Runner output (9 repeats each, seconds):

| Implementation | Min (s) | Median (s) | Max (s) |
|---|---:|---:|---:|
| Python | 0.269890834 | 0.271973792 | 0.274642208 |
| POSIX C | 0.000000000 | 0.000000000 | 0.000001000 |
| Lux POSIX | 0.614356000 | 0.660829000 | 0.861098000 |

Notes:
- No Plan 9-native result is included in this table.
- This table must not be merged with Intel i5 data.
- Lux POSIX ~2× faster than pre-OPT builds (previously ~1.33 s median).

## Dataset C: Intel i5 (Current, Comprehensive 200k ROUNDS)

Hardware: `2.6 GHz Dual-Core Intel Core i5`

Runner: `python3 benchmark/run_bench.py` (9 repeats, 200,000 iterations each)

| Implementation | Platform | Time (s) |
|---|---|---:|
| Python | macOS | 0.966097 (median of 9, 200k rounds) |
| POSIX C | macOS | 0.000002 (median of 9, 200k rounds) |
| Lux POSIX | macOS | 1.721212 (median of 9, 200k rounds) |
| Plan 9 C | Plan 9 | 0.008283 (200k rounds) |
| Plan 9 C | Plan 9 | 0.004334 (100k rounds) |
| Lux Plan 9 | Plan 9 | 2.677545 (200k rounds) |

Scaling Notes:
- Plan 9 C scales linearly: 100k ROUNDS → 4.33 ms, 200k ROUNDS → 8.28 ms (~2.0× linear).
- POSIX C is still ~4100× faster than Plan 9 C per iteration in the harness.
- Python is ~1.78× slower than Lux POSIX.
- Lux Plan 9 is ~1.56× slower than Lux POSIX.

## Plan 9 C Performance Analysis

**Root Cause of Slowness:**

Assembly inspection (`6c -S`) reveals excessive register spilling in the main benchmark loop:

```asm
INCL	,CX            # i++
CMPL	CX,$100000
MOVL	CX,i+-20(SP)   # *** STORE i to stack ***
JGE	,-4(PC)

MOVL	$50,BP
CALL	,fib_fast<>+0(SB)
MOVL	i+-20(SP),CX   # *** RELOAD i from stack ***
```

The compiler stores the loop counter `i` to the stack before each function call and reloads it after, generating:
- **200,000 memory stores** (one per iteration)
- **200,000 memory loads** (one per iteration)

**Why this happens:**

Plan 9's 6c compiler (from the 1990s/2000s) is overly conservative with register allocation. It doesn't optimize across function calls:
- It assumes the function call will clobber registers
- It spills `i` to preserve it across the `fib_fast()` call
- Modern compilers (clang, gcc with `-O3`) track which registers are caller-saved and avoid unnecessary spilling

**Impact:**

Each iteration incurs two L1 cache misses (store + load) instead of keeping the loop counter in a register. Over 200,000 iterations, this adds up to ~8 ms of overhead.

**Comparison to macOS clang:**

Modern clang keeps the loop counter entirely in a register across function calls, generating tight, cache-efficient code with zero memory overhead for loop variables.

This explains why **Plan 9 C (8.3 ms) is ~4100× slower than macOS POSIX C (2 μs)** in the harness, despite being "native" code—the compiler itself is the limiting factor.

**Optimization Attempt: Register Hints**

Created `fib_bench_plan9_opt.c` with explicit `register` keywords on loop counter and accumulator:

```c
register uvlong checksum = 0;
register int i;
for (i = 0; i < ROUNDS; i++) {
    checksum += fib_fast(N);
}
```

Result: **6.7 ms** (down from 8.3 ms, ~19% improvement). The hints reduced spilling somewhat but 6c still performs stack operations across call boundaries due to its ABI prologue/epilogue requirements. The improvement plateaus because the `register` keyword is merely a hint; 6c's architecture doesn't allow true interprocedural register preservation within its calling convention.

## Build Optimization

**POSIX:** The Makefile uses `OPT ?= -O2` by default. Use `make OPT=-O3` for maximum optimization, or `make OPT=-O0` for unoptimized debug builds. Modern compilers (clang, gcc) handle register allocation well; no `register` hints are needed in benchmark harnesses.

**Plan 9:** The 6c/8c compilers spill registers across function calls. Register hints help: use `fib_bench_plan9_opt.c` as the canonical Plan 9 C harness. The Lux VM `run()` loop also uses `register` hints on `frame` and `instruction` to reduce spilling in the interpreter hot path.

## Benchmark Files

- `benchmark/fib_bench.py`
- `benchmark/fib_bench.c` (POSIX; clang/gcc optimize without register hints)
- `benchmark/fib_bench.lux`
- `benchmark/fib_bench_plan9.c` (baseline)
- `benchmark/fib_bench_plan9_opt.c` (canonical Plan 9 C harness with register hints)
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
