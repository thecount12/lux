#include <u.h>
#include <libc.h>

#define N 50
#define ROUNDS 200000

static uvlong
fib_fast(int n)
{
	if (n < 2)
		return (uvlong)n;

	uvlong a = 0;
	uvlong b = 1;

	for (int i = 2; i <= n; i++) {
		uvlong next = a + b;
		a = b;
		b = next;
	}

	return b;
}

void
main(void)
{
	vlong start = nsec();
	register uvlong checksum = 0;  /* Force register allocation */
	register int i;

	for (i = 0; i < ROUNDS; i++) {
		checksum += fib_fast(N);
	}

	vlong end = nsec();
	double elapsed = (double)(end - start) / 1000000000.0;

	print("N=%d\n", N);
	print("ROUNDS=%d\n", ROUNDS);
	print("CHECKSUM=%llud\n", checksum);
	print("BENCH_TIME=%.9f\n", elapsed);

	exits(nil);
}
