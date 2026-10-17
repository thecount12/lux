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

static int float_to_tile(float v, int16_t *out) {
	long r;

	if (v < -FLUX9_ELEM_MAX || v > FLUX9_ELEM_MAX)
		return 0;
	r = lroundf(v);
	if (r < -64 || r > 64)
		return 0;
	*out = (int16_t)r;
	return 1;
}

static int tensor_to_utensor(const Tensor *t, utensor_t *u) {
	int i, j;

	if (t == NULL || t->rows != T_SIZE || t->cols != T_SIZE)
		return 0;
	for (i = 0; i < T_SIZE; i++) {
		for (j = 0; j < T_SIZE; j++) {
			if (!float_to_tile(t->data[i * t->cols + j], &u->data[i][j]))
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

/*
 * a.matmulFlux9(b) — both must be 4x4; elements in [-64, 64].
 * Runs flux9 bytecode MATMUL T0,T1,T2 ; HALT.
 */
Value tensorMatmulFlux9Native(int argCount, Value* args) {
	Tensor *a;
	Tensor *b;
	Tensor *c;
	utensor_t ua, ub, uc;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_INSTANCE(args[1]))
		return NIL_VAL;
	a = tensorFromInstance(AS_INSTANCE(args[0]));
	b = tensorFromInstance(AS_INSTANCE(args[1]));
	if (a == NULL || b == NULL)
		return NIL_VAL;
	if (a->rows != T_SIZE || a->cols != T_SIZE ||
	    b->rows != T_SIZE || b->cols != T_SIZE)
		return NIL_VAL;
	if (!tensor_to_utensor(a, &ua) || !tensor_to_utensor(b, &ub))
		return NIL_VAL;

	flux9_matmul(&uc, &ua, &ub);

	c = tensor_alloc(T_SIZE, T_SIZE);
	if (c == NULL)
		return NIL_VAL;
	utensor_to_tensor(&uc, c);
	return tensorAdopt(c);
}
