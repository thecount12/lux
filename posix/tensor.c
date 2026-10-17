#include <stdio.h>
#include <stdlib.h>
#include "tensor.h"

Tensor* tensor_alloc(int rows, int cols) {
    Tensor *t = (Tensor *)malloc(sizeof(Tensor));
    if (!t) return NULL;
    
    t->rows = rows;
    t->cols = cols;
    /* Allocate all floats in one continuous block */
    t->data = (float *)malloc(rows * cols * sizeof(float));
    
    if (!t->data) {
        free(t);
        return NULL;
    }
    return t;
}

void tensor_free(Tensor *t) {
    if (t) {
        if (t->data) free(t->data);
        free(t);
    }
}

void tensor_fill(Tensor *t, float value) {
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        t->data[i] = value;
    }
}

/* Classic C Matrix Multiplication: C = A * B */
int tensor_matmul(const Tensor *A, const Tensor *B, Tensor *C) {
    /* Validate matrix dimensions match */
    if (A->cols != B->rows || C->rows != A->rows || C->cols != B->cols) {
        return 0; /* Error */
    }

    /* Zero out result matrix first */
    tensor_fill(C, 0.0f);

    /* Row-major order optimization loops */
    for (int i = 0; i < A->rows; i++) {
        for (int k = 0; k < A->cols; k++) {
            float a_val = A->data[i * A->cols + k];
            for (int j = 0; j < B->cols; j++) {
                C->data[i * C->cols + j] += a_val * B->data[k * B->cols + j];
            }
        }
    }
    return 1; /* Success */
}

/* C = A + B. A, B, and C must share one shape. */
int tensor_add(const Tensor *A, const Tensor *B, Tensor *C) {
    int size;
    int i;

    if (A->rows != B->rows || A->cols != B->cols ||
        C->rows != A->rows || C->cols != A->cols) {
        return 0;
    }

    size = A->rows * A->cols;
    for (i = 0; i < size; i++) {
        C->data[i] = A->data[i] + B->data[i];
    }
    return 1;
}

/* C = A + bias, with bias a 1 x cols vector added to every row. */
int tensor_add_bias(const Tensor *A, const Tensor *bias, Tensor *C) {
    int i;
    int j;

    if (bias->rows != 1 || bias->cols != A->cols ||
        C->rows != A->rows || C->cols != A->cols) {
        return 0;
    }

    for (i = 0; i < A->rows; i++) {
        for (j = 0; j < A->cols; j++) {
            C->data[i * A->cols + j] =
                A->data[i * A->cols + j] + bias->data[j];
        }
    }
    return 1;
}

/* C = max(0, A). C must match A's shape. */
int tensor_relu(const Tensor *A, Tensor *C) {
    int size;
    int i;

    if (C->rows != A->rows || C->cols != A->cols) {
        return 0;
    }

    size = A->rows * A->cols;
    for (i = 0; i < size; i++) {
        float v = A->data[i];
        C->data[i] = v > 0.0f ? v : 0.0f;
    }
    return 1;
}

/* dZ[i] = dH[i] where Z[i] > 0, otherwise 0. Shapes must match. */
int tensor_relu_grad(const Tensor *dH, const Tensor *Z, Tensor *dZ) {
    int size;
    int i;

    if (dH->rows != Z->rows || dH->cols != Z->cols ||
        dZ->rows != Z->rows || dZ->cols != Z->cols) {
        return 0;
    }

    size = Z->rows * Z->cols;
    for (i = 0; i < size; i++) {
        dZ->data[i] = Z->data[i] > 0.0f ? dH->data[i] : 0.0f;
    }
    return 1;
}

/* C[i,j] = rowA[i] * rowB[j]. rowA is 1 x rows, rowB is 1 x cols. */
int tensor_outer(const Tensor *rowA, const Tensor *rowB, Tensor *C) {
    int i;
    int j;

    if (rowA->rows != 1 || rowB->rows != 1 ||
        rowA->cols != C->rows || rowB->cols != C->cols) {
        return 0;
    }

    for (i = 0; i < C->rows; i++) {
        for (j = 0; j < C->cols; j++) {
            C->data[i * C->cols + j] = rowA->data[i] * rowB->data[j];
        }
    }
    return 1;
}

/* A[i] += scale * B[i]. A and B must share one shape. */
int tensor_scale_add(Tensor *A, const Tensor *B, float scale) {
    int size;
    int i;

    if (A->rows != B->rows || A->cols != B->cols) {
        return 0;
    }

    size = A->rows * A->cols;
    for (i = 0; i < size; i++) {
        A->data[i] += scale * B->data[i];
    }
    return 1;
}

void tensor_print(const Tensor *t, const char *label) {
    printf("--- Tensor: %s (%dx%d) ---\n", label, t->rows, t->cols);
    for (int i = 0; i < t->rows; i++) {
        for (int j = 0; j < t->cols; j++) {
            printf("%6.2f ", t->data[i * t->cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}


