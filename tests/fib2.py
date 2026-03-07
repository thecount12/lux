import time


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
result = fib_fast(35)
elapsed = time.perf_counter() - start
print(f"Fibonacci(35) = {result}")
print(f"Time Elapsed: {elapsed:.6f} seconds")
