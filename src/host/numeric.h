#ifndef MORPHEUS_NUMERIC_H
#define MORPHEUS_NUMERIC_H

#include <stddef.h>

#include "la_host.h"

#define MORPHEUS_NUMERIC_MAX_ELEMENTS (4u * 1024u * 1024u)
#define MORPHEUS_NUMERIC_MAX_EIGEN_DIMENSION 256u

typedef enum morph_numeric_status {
    MORPH_NUMERIC_OK = 0,
    MORPH_NUMERIC_INVALID = 1,
    MORPH_NUMERIC_DIMENSION_MISMATCH = 2,
    MORPH_NUMERIC_TOO_LARGE = 3,
    MORPH_NUMERIC_OUT_OF_MEMORY = 4,
    MORPH_NUMERIC_SINGULAR = 5,
    MORPH_NUMERIC_NOT_CONVERGED = 6
} morph_numeric_status;

typedef struct morph_numeric_vector {
    size_t length;
    double *data;
} morph_numeric_vector;

typedef struct morph_numeric_matrix {
    size_t rows;
    size_t columns;
    double *data;
} morph_numeric_matrix;

/* Packed Householder factors. The upper triangle stores R; entries below the
   diagonal and tau store the reflectors. */
typedef struct morph_numeric_qr {
    morph_numeric_matrix factors;
    morph_numeric_vector tau;
    size_t rank;
    double threshold;
} morph_numeric_qr;

typedef struct morph_numeric_regression {
    size_t observations;
    size_t predictors;
    morph_numeric_vector coefficients;
    morph_numeric_vector fitted;
    double residual_standard_deviation;
} morph_numeric_regression;

const char *morph_numeric_status_string(morph_numeric_status status);

/* Outputs passed to allocating functions must be zero-initialized. */
morph_numeric_status morph_numeric_vector_init(
    morph_numeric_vector *vector, size_t length);
morph_numeric_status morph_numeric_vector_from_array(
    morph_numeric_vector *vector, const double *data, size_t length);
void morph_numeric_vector_dispose(morph_numeric_vector *vector);
morph_numeric_status morph_numeric_vector_copy(
    morph_numeric_vector *output, const morph_numeric_vector *input);
morph_numeric_status morph_numeric_vector_slice(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    size_t begin, size_t end);
morph_numeric_status morph_numeric_vector_add(
    morph_numeric_vector *output, const morph_numeric_vector *left,
    const morph_numeric_vector *right);
morph_numeric_status morph_numeric_vector_subtract(
    morph_numeric_vector *output, const morph_numeric_vector *left,
    const morph_numeric_vector *right);
morph_numeric_status morph_numeric_vector_scale(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    double scalar);
morph_numeric_status morph_numeric_vector_dot(
    const morph_numeric_vector *left, const morph_numeric_vector *right,
    double *result);
morph_numeric_status morph_numeric_vector_norm(
    const morph_numeric_vector *vector, double *result);
morph_numeric_status morph_numeric_vector_normalize(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    double tolerance);

morph_numeric_status morph_numeric_matrix_init(
    morph_numeric_matrix *matrix, size_t rows, size_t columns);
morph_numeric_status morph_numeric_matrix_from_array(
    morph_numeric_matrix *matrix, const double *data,
    size_t rows, size_t columns);
void morph_numeric_matrix_dispose(morph_numeric_matrix *matrix);
morph_numeric_status morph_numeric_matrix_copy(
    morph_numeric_matrix *output, const morph_numeric_matrix *input);
morph_numeric_status morph_numeric_matrix_identity(
    morph_numeric_matrix *output, size_t dimension);
morph_numeric_status morph_numeric_matrix_transpose(
    morph_numeric_matrix *output, const morph_numeric_matrix *input);
morph_numeric_status morph_numeric_matrix_multiply(
    morph_numeric_matrix *output, const morph_numeric_matrix *left,
    const morph_numeric_matrix *right);
morph_numeric_status morph_numeric_matrix_multiply_transpose_left(
    morph_numeric_matrix *output, const morph_numeric_matrix *left,
    const morph_numeric_matrix *right);
morph_numeric_status morph_numeric_matrix_vector_multiply(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *vector);
morph_numeric_status morph_numeric_matrix_transpose_vector_multiply(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *vector);
morph_numeric_status morph_numeric_matrix_diagonal(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix);

morph_numeric_status morph_numeric_qr_factor(
    morph_numeric_qr *output, const morph_numeric_matrix *matrix,
    double tolerance);
void morph_numeric_qr_dispose(morph_numeric_qr *qr);
morph_numeric_status morph_numeric_qr_solve(
    morph_numeric_vector *output, const morph_numeric_qr *qr,
    const morph_numeric_vector *right_hand_side);
morph_numeric_status morph_numeric_qr_unpack(
    const morph_numeric_qr *qr, morph_numeric_matrix *q,
    morph_numeric_matrix *r);
morph_numeric_status morph_numeric_solve(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *right_hand_side, double tolerance);

morph_numeric_status morph_numeric_regression_fit(
    morph_numeric_regression *output, const morph_numeric_matrix *design,
    const morph_numeric_vector *response, double tolerance);
void morph_numeric_regression_dispose(morph_numeric_regression *regression);
morph_numeric_status morph_numeric_regression_predict(
    morph_numeric_vector *output,
    const morph_numeric_regression *regression,
    const morph_numeric_matrix *design);

/* Jacobi decomposition for real symmetric matrices. Eigenvalues are sorted in
   ascending order and eigenvectors are returned as matrix columns. */
morph_numeric_status morph_numeric_symmetric_eigen(
    const morph_numeric_matrix *matrix, double tolerance,
    size_t max_iterations, morph_numeric_vector *eigenvalues,
    morph_numeric_matrix *eigenvectors);

/* Copying interoperability with la's fixed-size double types. */
morph_numeric_status morph_numeric_vector_from_v2d(
    morph_numeric_vector *output, V2d value);
morph_numeric_status morph_numeric_vector_from_v3d(
    morph_numeric_vector *output, V3d value);
morph_numeric_status morph_numeric_vector_from_v4d(
    morph_numeric_vector *output, V4d value);
morph_numeric_status morph_numeric_vector_to_v2d(
    const morph_numeric_vector *input, V2d *output);
morph_numeric_status morph_numeric_vector_to_v3d(
    const morph_numeric_vector *input, V3d *output);
morph_numeric_status morph_numeric_vector_to_v4d(
    const morph_numeric_vector *input, V4d *output);
morph_numeric_status morph_numeric_matrix_from_m2d(
    morph_numeric_matrix *output, M2d value);
morph_numeric_status morph_numeric_matrix_from_m3d(
    morph_numeric_matrix *output, M3d value);
morph_numeric_status morph_numeric_matrix_from_m4d(
    morph_numeric_matrix *output, M4d value);
morph_numeric_status morph_numeric_matrix_to_m2d(
    const morph_numeric_matrix *input, M2d *output);
morph_numeric_status morph_numeric_matrix_to_m3d(
    const morph_numeric_matrix *input, M3d *output);
morph_numeric_status morph_numeric_matrix_to_m4d(
    const morph_numeric_matrix *input, M4d *output);

#endif
