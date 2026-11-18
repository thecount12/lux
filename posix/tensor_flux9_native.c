/* tensor_flux9_native.c — Lux Tensor ↔ flux9 tile VM seam.
 * Only file that includes both tensor.h and flux9.h.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "tensor.h"
#include "tensor_native.h"

#include "api.h"
#include "flux9.h"

#define FLUX9_ELEM_MAX 64.0f

static int float_to_i16(float v, int16_t *out) {
	long r;

	if (v < -FLUX9_ELEM_MAX || v > FLUX9_ELEM_MAX)
		return 0;
	r = lroundf(v);
	if (r < -64 || r > 64)
		return 0;
	*out = (int16_t)r;
	return 1;
}

static int multiple_of_tile(int n) {
	return n > 0 && (n % T_SIZE) == 0;
}

static int tensor_to_utensor(const Tensor *t, utensor_t *u) {
	int i, j;

	if (t == NULL || t->rows != T_SIZE || t->cols != T_SIZE)
		return 0;
	for (i = 0; i < T_SIZE; i++) {
		for (j = 0; j < T_SIZE; j++) {
			if (!float_to_i16(t->data[i * t->cols + j], &u->data[i][j]))
				return 0;
		}
	}
	return 1;
}

static void utensor_to_tensor(const utensor_t *u, Tensor *t) {
	int i, j;

	for (i = 0; i < T_SIZE; i++) {
		for (j = 0; j < T_SIZE; j++) {
			t->data[i * t->cols + j] = (float)u->data[i][j];
		}
	}
}

static int tensor_to_i16_buf(const Tensor *t, int16_t *buf) {
	int i, n;

	n = t->rows * t->cols;
	for (i = 0; i < n; i++) {
		if (!float_to_i16(t->data[i], &buf[i]))
			return 0;
	}
	return 1;
}

static void i16_buf_to_tensor(const int16_t *buf, Tensor *t) {
	int i, n;

	n = t->rows * t->cols;
	for (i = 0; i < n; i++) {
		t->data[i] = (float)buf[i];
	}
}

/*
 * a.matmulFlux9(b)
 * - Exact 4×4: bytecode flux9_matmul
 * - Larger multiples of 4: flux9_matmul_tiled
 * Elements must lie in [-64, 64].
 */
Value tensorMatmulFlux9Native(int argCount, Value* args) {
	Tensor *a;
	Tensor *b;
	Tensor *c;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_INSTANCE(args[1]))
		return NIL_VAL;
	a = tensorFromInstance(AS_INSTANCE(args[0]));
	b = tensorFromInstance(AS_INSTANCE(args[1]));
	if (a == NULL || b == NULL)
		return NIL_VAL;
	if (a->cols != b->rows)
		return NIL_VAL;
	if (!multiple_of_tile(a->rows) || !multiple_of_tile(a->cols) ||
	    !multiple_of_tile(b->rows) || !multiple_of_tile(b->cols))
		return NIL_VAL;

	/* Fast path: single tile via bytecode. */
	if (a->rows == T_SIZE && a->cols == T_SIZE &&
	    b->rows == T_SIZE && b->cols == T_SIZE) {
		utensor_t ua, ub, uc;

		if (!tensor_to_utensor(a, &ua) || !tensor_to_utensor(b, &ub))
			return NIL_VAL;
		flux9_matmul(&uc, &ua, &ub);
		c = tensor_alloc(T_SIZE, T_SIZE);
		if (c == NULL)
			return NIL_VAL;
		utensor_to_tensor(&uc, c);
		return tensorAdopt(c);
	}

	/* Tiled path for larger multiples of 4. */
	{
		int M = a->rows;
		int K = a->cols;
		int N = b->cols;
		int16_t *A;
		int16_t *B;
		int16_t *Cbuf;

		A = (int16_t *)malloc((size_t)M * (size_t)K * sizeof(int16_t));
		B = (int16_t *)malloc((size_t)K * (size_t)N * sizeof(int16_t));
		Cbuf = (int16_t *)malloc((size_t)M * (size_t)N * sizeof(int16_t));
		if (A == NULL || B == NULL || Cbuf == NULL) {
			free(A);
			free(B);
			free(Cbuf);
			return NIL_VAL;
		}
		if (!tensor_to_i16_buf(a, A) || !tensor_to_i16_buf(b, B)) {
			free(A);
			free(B);
			free(Cbuf);
			return NIL_VAL;
		}
		if (flux9_matmul_tiled(Cbuf, A, B, M, N, K) != 0) {
			free(A);
			free(B);
			free(Cbuf);
			return NIL_VAL;
		}
		c = tensor_alloc(M, N);
		if (c == NULL) {
			free(A);
			free(B);
			free(Cbuf);
			return NIL_VAL;
		}
		i16_buf_to_tensor(Cbuf, c);
		free(A);
		free(B);
		free(Cbuf);
		return tensorAdopt(c);
	}
}
