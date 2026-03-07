#include <u.h>
#include <libc.h>

static uvlong
fib_fast(int n)
{
	if (n < 2)
		return (uvlong)n;

	uvlong a = 0;
	uvlong b = 1;
	int i = 2;

	while (i <= n) {
		uvlong next = a + b;
		a = b;
		b = next;
		i++;
	}

	return b;
}

void
main(void)
{
	int n = 35;
	vlong start = nsec();
	uvlong result = fib_fast(n);
	vlong end = nsec();
	double elapsed = (double)(end - start) / 1000000000.0;

	print("Fibonacci(%d) = %llud\n", n, result);
	print("Time Elapsed: %.6f seconds\n", elapsed);

	exits(nil);
}
