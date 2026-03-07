import time

N = 50
ROUNDS = 200000


def fib_fast(n: int) -> int:
    if n < 2:
        return n
    a = 0
    b = 1
    i = 2
    while i <= n:
        nxt = a + b
        a = b
        b = nxt
        i += 1
    return b


start = time.perf_counter()
checksum = 0
for _ in range(ROUNDS):
    checksum += fib_fast(N)
elapsed = time.perf_counter() - start
print(f"N={N}")
print(f"ROUNDS={ROUNDS}")
print(f"CHECKSUM={checksum}")
print(f"BENCH_TIME={elapsed:.9f}")
