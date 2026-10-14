#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "value.h"
#include "object.h"
#include "vm.h"
#include "table.h"
#include "tensor.h"
#include "tensor_native.h"

#define TENSOR_DIM_MAX 4096

static ObjClass* tensorClass = NULL;

void tensorSetClass(ObjClass* klass) {
	tensorClass = klass;
}

static void setOk(ObjInstance* inst, bool ok) {
	tableSet(&inst->fields, copyString("ok", 2), BOOL_VAL(ok));
}

static void setNum(ObjInstance* inst, const char* name, int len, double n) {
	tableSet(&inst->fields, copyString(name, len), NUMBER_VAL(n));
}

static void setPtr(ObjInstance* inst, Tensor* t) {
	if (t == NULL) {
		tableSet(&inst->fields, copyString("_ptr", 4), NIL_VAL);
		return;
	}
	tableSet(&inst->fields, copyString("_ptr", 4),
		NUMBER_VAL((double)(uintptr_t)t));
}

static Tensor* tensorFromFields(ObjInstance* inst) {
	int i;

	if (inst == NULL || inst->fields.entries == NULL) return NULL;
	for (i = 0; i < inst->fields.capacity; i++) {
		Entry* e = &inst->fields.entries[i];
		if (e->key != NULL && e->key->length == 4 &&
		    memcmp(e->key->chars, "_ptr", 4) == 0) {
			if (!IS_NUMBER(e->value)) return NULL;
			return (Tensor*)(uintptr_t)AS_NUMBER(e->value);
		}
	}
	return NULL;
}

static void dropTensor(ObjInstance* inst) {
	Tensor* t;
	int i;

	t = tensorFromFields(inst);
	if (t == NULL) return;
	if (inst->fields.entries != NULL) {
		for (i = 0; i < inst->fields.capacity; i++) {
			Entry* e = &inst->fields.entries[i];
			if (e->key != NULL && e->key->length == 4 &&
			    memcmp(e->key->chars, "_ptr", 4) == 0) {
				e->value = NIL_VAL;
				break;
			}
		}
	}
	tensor_free(t);
}

void tensorFreeFromInstance(ObjInstance* instance) {
	if (instance == NULL) return;
	if (tensorClass == NULL || instance->klass != tensorClass) return;
	dropTensor(instance);
}

static Tensor* getTensor(ObjInstance* inst) {
	Value v;

	if (inst == NULL) return NULL;
	if (tensorClass == NULL || inst->klass != tensorClass) return NULL;
	if (!tableGet(&inst->fields, copyString("_ptr", 4), &v)) return NULL;
	if (!IS_NUMBER(v)) return NULL;
	return (Tensor*)(uintptr_t)AS_NUMBER(v);
}

static int asDim(Value v, int* out) {
	double n;

	if (!IS_NUMBER(v)) return 0;
	n = AS_NUMBER(v);
	if (n < 1.0 || n > (double)TENSOR_DIM_MAX) return 0;
	if (n != (double)(int)n) return 0;
	*out = (int)n;
	return 1;
}

static int asIndex(Value v, int limit, int* out) {
	double n;

	if (!IS_NUMBER(v)) return 0;
	n = AS_NUMBER(v);
	if (n < 0.0 || n != (double)(int)n) return 0;
	if ((int)n >= limit) return 0;
	*out = (int)n;
	return 1;
}

static void bindTensor(ObjInstance* inst, Tensor* t) {
	setPtr(inst, t);
	setNum(inst, "rows", 4, t->rows);
	setNum(inst, "cols", 4, t->cols);
	setOk(inst, true);
}

/* The instance must already be on the VM stack. */
static Value adoptTensor(Tensor* t) {
	ObjInstance* inst;

	if (t == NULL || tensorClass == NULL) {
		tensor_free(t);
		return NIL_VAL;
	}
	inst = newInstance(tensorClass);
	push(OBJ_VAL(inst));
	bindTensor(inst, t);
	pop();
	return OBJ_VAL(inst);
}

Value tensorInitNative(int argCount, Value* args) {
	ObjInstance* inst;
	Tensor* t;
	int rows;
	int cols;

	if (argCount < 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	inst = AS_INSTANCE(args[0]);
	dropTensor(inst);
	setOk(inst, false);

	if (argCount < 3 || !asDim(args[1], &rows) || !asDim(args[2], &cols))
		return args[0];
	t = tensor_alloc(rows, cols);
	if (t == NULL)
		return args[0];
	tensor_fill(t, 0.0f);
	bindTensor(inst, t);
	return args[0];
}

Value tensorFillNative(int argCount, Value* args) {
	Tensor* t;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_NUMBER(args[1]))
		return NIL_VAL;
	t = getTensor(AS_INSTANCE(args[0]));
	if (t == NULL)
		return NIL_VAL;
	tensor_fill(t, (float)AS_NUMBER(args[1]));
	return args[0];
}

Value tensorSetNative(int argCount, Value* args) {
	Tensor* t;
	int row;
	int col;

	if (argCount != 4 || !IS_INSTANCE(args[0]) || !IS_NUMBER(args[3]))
		return NIL_VAL;
	t = getTensor(AS_INSTANCE(args[0]));
	if (t == NULL)
		return NIL_VAL;
	if (!asIndex(args[1], t->rows, &row) || !asIndex(args[2], t->cols, &col))
		return NIL_VAL;
	t->data[row * t->cols + col] = (float)AS_NUMBER(args[3]);
	return args[0];
}

Value tensorGetNative(int argCount, Value* args) {
	Tensor* t;
	int row;
	int col;

	if (argCount != 3 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	t = getTensor(AS_INSTANCE(args[0]));
	if (t == NULL)
		return NIL_VAL;
	if (!asIndex(args[1], t->rows, &row) || !asIndex(args[2], t->cols, &col))
		return NIL_VAL;
	return NUMBER_VAL((double)t->data[row * t->cols + col]);
}

Value tensorMatmulNative(int argCount, Value* args) {
	Tensor* a;
	Tensor* b;
	Tensor* c;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_INSTANCE(args[1]))
		return NIL_VAL;
	a = getTensor(AS_INSTANCE(args[0]));
	b = getTensor(AS_INSTANCE(args[1]));
	if (a == NULL || b == NULL)
		return NIL_VAL;
	if (a->cols != b->rows)
		return NIL_VAL;
	c = tensor_alloc(a->rows, b->cols);
	if (c == NULL)
		return NIL_VAL;
	if (!tensor_matmul(a, b, c)) {
		tensor_free(c);
		return NIL_VAL;
	}
	return adoptTensor(c);
}

Value tensorAddBiasNative(int argCount, Value* args) {
	Tensor* a;
	Tensor* bias;
	Tensor* c;

	if (argCount != 2 || !IS_INSTANCE(args[0]) || !IS_INSTANCE(args[1]))
		return NIL_VAL;
	a = getTensor(AS_INSTANCE(args[0]));
	bias = getTensor(AS_INSTANCE(args[1]));
	if (a == NULL || bias == NULL)
		return NIL_VAL;
	if (bias->rows != 1 || bias->cols != a->cols)
		return NIL_VAL;
	c = tensor_alloc(a->rows, a->cols);
	if (c == NULL)
		return NIL_VAL;
	if (!tensor_add_bias(a, bias, c)) {
		tensor_free(c);
		return NIL_VAL;
	}
	return adoptTensor(c);
}

Value tensorReluNative(int argCount, Value* args) {
	Tensor* a;
	Tensor* c;

	if (argCount != 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	a = getTensor(AS_INSTANCE(args[0]));
	if (a == NULL)
		return NIL_VAL;
	c = tensor_alloc(a->rows, a->cols);
	if (c == NULL)
		return NIL_VAL;
	if (!tensor_relu(a, c)) {
		tensor_free(c);
		return NIL_VAL;
	}
	return adoptTensor(c);
}

Value tensorShowNative(int argCount, Value* args) {
	Tensor* t;
	const char* label;

	if (argCount < 1 || !IS_INSTANCE(args[0]))
		return NIL_VAL;
	t = getTensor(AS_INSTANCE(args[0]));
	if (t == NULL)
		return NIL_VAL;
	label = "Tensor";
	if (argCount >= 2 && IS_STRING(args[1]))
		label = AS_CSTRING(args[1]);
	tensor_print(t, label);
	return args[0];
}
