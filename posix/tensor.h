#ifndef TENSOR_H
#define TENSOR_H

#include <stddef.h>

typedef struct {
    int rows;
    int cols;
    float *data;   /* Flat, contiguous array of memory */
} Tensor;

/* Lifecycle */
Tensor* tensor_alloc(int rows, int cols);
void    tensor_free(Tensor *t);
void    tensor_fill(Tensor *t, float value);

/* Math Operations. Each returns 1 on success, 0 on a shape mismatch. */
int     tensor_matmul(const Tensor *A, const Tensor *B, Tensor *C);
int     tensor_add(const Tensor *A, const Tensor *B, Tensor *C);
int     tensor_add_bias(const Tensor *A, const Tensor *bias, Tensor *C);
int     tensor_relu(const Tensor *A, Tensor *C);
int     tensor_relu_grad(const Tensor *dH, const Tensor *Z, Tensor *dZ);
int     tensor_outer(const Tensor *rowA, const Tensor *rowB, Tensor *C);
int     tensor_scale_add(Tensor *A, const Tensor *B, float scale);
void    tensor_print(const Tensor *t, const char *label);

#endif


