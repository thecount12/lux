#include <stdio.h>
#include <time.h>

static unsigned long long
fib_fast(int n)
{
    if (n < 2) {
        return (unsigned long long)n;
    }

    unsigned long long a = 0;
    unsigned long long b = 1;
    int i = 2;

    while (i <= n) {
        unsigned long long next = a + b;
        a = b;
        b = next;
        i++;
    }

    return b;
}

int
main(void)
{
    const int n = 35;
    clock_t start = clock();
    unsigned long long result = fib_fast(n);
    clock_t end = clock();

    double elapsed = (double)(end - start) / (double)CLOCKS_PER_SEC;

    printf("Fibonacci(%d) = %llu\n", n, result);
    printf("Time Elapsed: %.6f seconds\n", elapsed);

    return 0;
}
