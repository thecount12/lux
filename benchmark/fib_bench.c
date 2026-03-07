#include <stdio.h>
#include <time.h>

#define N 50
#define ROUNDS 200000

static unsigned long long
fib_fast(int n)
{
    if (n < 2)
        return (unsigned long long)n;

    unsigned long long a = 0;
    unsigned long long b = 1;

    for (int i = 2; i <= n; i++) {
        unsigned long long next = a + b;
        a = b;
        b = next;
    }

    return b;
}

int
main(void)
{
    clock_t start = clock();
    unsigned long long checksum = 0;

    for (int i = 0; i < ROUNDS; i++) {
        checksum += fib_fast(N);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / (double)CLOCKS_PER_SEC;

    printf("N=%d\n", N);
    printf("ROUNDS=%d\n", ROUNDS);
    printf("CHECKSUM=%llu\n", checksum);
    printf("BENCH_TIME=%.9f\n", elapsed);

    return 0;
}
