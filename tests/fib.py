import time

def fibonacci(n):
    if n <= 1:
        return n
    else:
        return fibonacci(n - 1) + fibonacci(n - 2)

# 1. Start clock
start_time = time.perf_counter()

# 2. Run algorithm
n = 50
result = fibonacci(n)

# 3. End clock
end_time = time.perf_counter()

# 4. Calculate duration
duration = end_time - start_time

print(f"Fibonacci({n}) = {result}")
print(f"Time Elapsed: {duration:.6f} seconds")

