#include "numeric.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int morph_numeric_vector_is_empty(const morph_numeric_vector *vector)
{
    return vector && !vector->data && vector->length == 0;
}

static int morph_numeric_vector_is_valid(const morph_numeric_vector *vector)
{
    return vector && vector->data && vector->length > 0 &&
        vector->length <= MORPHEUS_NUMERIC_MAX_ELEMENTS;
}

static int morph_numeric_matrix_is_empty(const morph_numeric_matrix *matrix)
{
    return matrix && !matrix->data && matrix->rows == 0 && matrix->columns == 0;
}

static int morph_numeric_matrix_is_valid(const morph_numeric_matrix *matrix)
{
    return matrix && matrix->data && matrix->rows > 0 && matrix->columns > 0 &&
        matrix->rows <= MORPHEUS_NUMERIC_MAX_ELEMENTS / matrix->columns;
}

static int morph_numeric_vector_is_finite(const morph_numeric_vector *vector)
{
    size_t index;
    if (!morph_numeric_vector_is_valid(vector)) return 0;
    for (index = 0; index < vector->length; ++index) {
        if (!isfinite(vector->data[index])) return 0;
    }
    return 1;
}

static int morph_numeric_matrix_is_finite(const morph_numeric_matrix *matrix)
{
    size_t index;
    size_t count;
    if (!morph_numeric_matrix_is_valid(matrix)) return 0;
    count = matrix->rows * matrix->columns;
    for (index = 0; index < count; ++index) {
        if (!isfinite(matrix->data[index])) return 0;
    }
    return 1;
}

static size_t morph_numeric_minimum(size_t left, size_t right)
{
    return left < right ? left : right;
}

static size_t morph_numeric_matrix_index(
    const morph_numeric_matrix *matrix, size_t row, size_t column)
{
    return row * matrix->columns + column;
}

static double morph_numeric_matrix_get(
    const morph_numeric_matrix *matrix, size_t row, size_t column)
{
    return matrix->data[morph_numeric_matrix_index(matrix, row, column)];
}

static void morph_numeric_matrix_set(
    morph_numeric_matrix *matrix, size_t row, size_t column, double value)
{
    matrix->data[morph_numeric_matrix_index(matrix, row, column)] = value;
}

const char *morph_numeric_status_string(morph_numeric_status status)
{
    switch (status) {
    case MORPH_NUMERIC_OK: return "ok";
    case MORPH_NUMERIC_INVALID: return "invalid argument or nonempty output";
    case MORPH_NUMERIC_DIMENSION_MISMATCH: return "dimension mismatch";
    case MORPH_NUMERIC_TOO_LARGE: return "numeric object exceeds its limit";
    case MORPH_NUMERIC_OUT_OF_MEMORY: return "out of memory";
    case MORPH_NUMERIC_SINGULAR: return "singular or rank-deficient input";
    case MORPH_NUMERIC_NOT_CONVERGED: return "algorithm did not converge";
    default: return "unknown numeric status";
    }
}

morph_numeric_status morph_numeric_vector_init(
    morph_numeric_vector *vector, size_t length)
{
    if (!morph_numeric_vector_is_empty(vector) || !length)
        return MORPH_NUMERIC_INVALID;
    if (length > MORPHEUS_NUMERIC_MAX_ELEMENTS)
        return MORPH_NUMERIC_TOO_LARGE;
    vector->data = calloc(length, sizeof(*vector->data));
    if (!vector->data) return MORPH_NUMERIC_OUT_OF_MEMORY;
    vector->length = length;
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_from_array(
    morph_numeric_vector *vector, const double *data, size_t length)
{
    morph_numeric_status status;
    if (!data) return MORPH_NUMERIC_INVALID;
    status = morph_numeric_vector_init(vector, length);
    if (status != MORPH_NUMERIC_OK) return status;
    memcpy(vector->data, data, length * sizeof(*data));
    return MORPH_NUMERIC_OK;
}

void morph_numeric_vector_dispose(morph_numeric_vector *vector)
{
    if (!vector) return;
    free(vector->data);
    vector->data = NULL;
    vector->length = 0;
}

morph_numeric_status morph_numeric_vector_copy(
    morph_numeric_vector *output, const morph_numeric_vector *input)
{
    if (!morph_numeric_vector_is_valid(input)) return MORPH_NUMERIC_INVALID;
    return morph_numeric_vector_from_array(output, input->data, input->length);
}

morph_numeric_status morph_numeric_vector_slice(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    size_t begin, size_t end)
{
    if (!morph_numeric_vector_is_valid(input) || begin >= end ||
        end > input->length) return MORPH_NUMERIC_INVALID;
    return morph_numeric_vector_from_array(
        output, input->data + begin, end - begin);
}

static morph_numeric_status morph_numeric_vector_binary(
    morph_numeric_vector *output, const morph_numeric_vector *left,
    const morph_numeric_vector *right, int subtract)
{
    morph_numeric_status status;
    size_t index;
    if (!morph_numeric_vector_is_valid(left) ||
        !morph_numeric_vector_is_valid(right)) return MORPH_NUMERIC_INVALID;
    if (left->length != right->length)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_vector_init(output, left->length);
    if (status != MORPH_NUMERIC_OK) return status;
    for (index = 0; index < left->length; ++index) {
        output->data[index] = subtract
            ? left->data[index] - right->data[index]
            : left->data[index] + right->data[index];
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_add(
    morph_numeric_vector *output, const morph_numeric_vector *left,
    const morph_numeric_vector *right)
{
    return morph_numeric_vector_binary(output, left, right, 0);
}

morph_numeric_status morph_numeric_vector_subtract(
    morph_numeric_vector *output, const morph_numeric_vector *left,
    const morph_numeric_vector *right)
{
    return morph_numeric_vector_binary(output, left, right, 1);
}

morph_numeric_status morph_numeric_vector_scale(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    double scalar)
{
    morph_numeric_status status;
    size_t index;
    if (!morph_numeric_vector_is_valid(input)) return MORPH_NUMERIC_INVALID;
    status = morph_numeric_vector_init(output, input->length);
    if (status != MORPH_NUMERIC_OK) return status;
    for (index = 0; index < input->length; ++index)
        output->data[index] = input->data[index] * scalar;
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_dot(
    const morph_numeric_vector *left, const morph_numeric_vector *right,
    double *result)
{
    double sum = 0.0;
    size_t index;
    if (!result || !morph_numeric_vector_is_valid(left) ||
        !morph_numeric_vector_is_valid(right)) return MORPH_NUMERIC_INVALID;
    if (left->length != right->length)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    for (index = 0; index < left->length; ++index)
        sum += left->data[index] * right->data[index];
    *result = sum;
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_norm(
    const morph_numeric_vector *vector, double *result)
{
    double norm = 0.0;
    size_t index;
    if (!result || !morph_numeric_vector_is_valid(vector))
        return MORPH_NUMERIC_INVALID;
    for (index = 0; index < vector->length; ++index)
        norm = hypot(norm, vector->data[index]);
    *result = norm;
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_normalize(
    morph_numeric_vector *output, const morph_numeric_vector *input,
    double tolerance)
{
    morph_numeric_status status;
    double norm;
    if (!morph_numeric_vector_is_finite(input) || !isfinite(tolerance) ||
        tolerance < 0.0) return MORPH_NUMERIC_INVALID;
    status = morph_numeric_vector_norm(input, &norm);
    if (status != MORPH_NUMERIC_OK) return status;
    if (norm <= (tolerance > 0.0 ? tolerance : DBL_EPSILON))
        return MORPH_NUMERIC_SINGULAR;
    return morph_numeric_vector_scale(output, input, 1.0 / norm);
}

morph_numeric_status morph_numeric_matrix_init(
    morph_numeric_matrix *matrix, size_t rows, size_t columns)
{
    size_t elements;
    if (!morph_numeric_matrix_is_empty(matrix) || !rows || !columns)
        return MORPH_NUMERIC_INVALID;
    if (rows > MORPHEUS_NUMERIC_MAX_ELEMENTS / columns)
        return MORPH_NUMERIC_TOO_LARGE;
    elements = rows * columns;
    if (elements > MORPHEUS_NUMERIC_MAX_ELEMENTS)
        return MORPH_NUMERIC_TOO_LARGE;
    matrix->data = calloc(elements, sizeof(*matrix->data));
    if (!matrix->data) return MORPH_NUMERIC_OUT_OF_MEMORY;
    matrix->rows = rows;
    matrix->columns = columns;
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_from_array(
    morph_numeric_matrix *matrix, const double *data,
    size_t rows, size_t columns)
{
    morph_numeric_status status;
    if (!data) return MORPH_NUMERIC_INVALID;
    status = morph_numeric_matrix_init(matrix, rows, columns);
    if (status != MORPH_NUMERIC_OK) return status;
    memcpy(matrix->data, data, rows * columns * sizeof(*data));
    return MORPH_NUMERIC_OK;
}

void morph_numeric_matrix_dispose(morph_numeric_matrix *matrix)
{
    if (!matrix) return;
    free(matrix->data);
    matrix->data = NULL;
    matrix->rows = 0;
    matrix->columns = 0;
}

morph_numeric_status morph_numeric_matrix_copy(
    morph_numeric_matrix *output, const morph_numeric_matrix *input)
{
    if (!morph_numeric_matrix_is_valid(input)) return MORPH_NUMERIC_INVALID;
    return morph_numeric_matrix_from_array(
        output, input->data, input->rows, input->columns);
}

morph_numeric_status morph_numeric_matrix_identity(
    morph_numeric_matrix *output, size_t dimension)
{
    morph_numeric_status status =
        morph_numeric_matrix_init(output, dimension, dimension);
    size_t index;
    if (status != MORPH_NUMERIC_OK) return status;
    for (index = 0; index < dimension; ++index)
        morph_numeric_matrix_set(output, index, index, 1.0);
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_transpose(
    morph_numeric_matrix *output, const morph_numeric_matrix *input)
{
    morph_numeric_status status;
    size_t row;
    size_t column;
    if (!morph_numeric_matrix_is_valid(input)) return MORPH_NUMERIC_INVALID;
    status = morph_numeric_matrix_init(output, input->columns, input->rows);
    if (status != MORPH_NUMERIC_OK) return status;
    for (row = 0; row < input->rows; ++row) {
        for (column = 0; column < input->columns; ++column) {
            morph_numeric_matrix_set(output, column, row,
                morph_numeric_matrix_get(input, row, column));
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_multiply(
    morph_numeric_matrix *output, const morph_numeric_matrix *left,
    const morph_numeric_matrix *right)
{
    morph_numeric_status status;
    size_t row;
    size_t shared;
    size_t column;
    if (!morph_numeric_matrix_is_valid(left) ||
        !morph_numeric_matrix_is_valid(right)) return MORPH_NUMERIC_INVALID;
    if (left->columns != right->rows)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_matrix_init(output, left->rows, right->columns);
    if (status != MORPH_NUMERIC_OK) return status;
    for (row = 0; row < left->rows; ++row) {
        for (shared = 0; shared < left->columns; ++shared) {
            double value = morph_numeric_matrix_get(left, row, shared);
            for (column = 0; column < right->columns; ++column) {
                output->data[morph_numeric_matrix_index(output, row, column)] +=
                    value * morph_numeric_matrix_get(right, shared, column);
            }
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_multiply_transpose_left(
    morph_numeric_matrix *output, const morph_numeric_matrix *left,
    const morph_numeric_matrix *right)
{
    morph_numeric_status status;
    size_t row;
    size_t left_column;
    size_t right_column;
    if (!morph_numeric_matrix_is_valid(left) ||
        !morph_numeric_matrix_is_valid(right)) return MORPH_NUMERIC_INVALID;
    if (left->rows != right->rows)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_matrix_init(
        output, left->columns, right->columns);
    if (status != MORPH_NUMERIC_OK) return status;
    for (row = 0; row < left->rows; ++row) {
        for (left_column = 0; left_column < left->columns; ++left_column) {
            double value = morph_numeric_matrix_get(left, row, left_column);
            for (right_column = 0; right_column < right->columns; ++right_column) {
                output->data[morph_numeric_matrix_index(
                    output, left_column, right_column)] +=
                    value * morph_numeric_matrix_get(
                        right, row, right_column);
            }
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_vector_multiply(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *vector)
{
    morph_numeric_status status;
    size_t row;
    size_t column;
    if (!morph_numeric_matrix_is_valid(matrix) ||
        !morph_numeric_vector_is_valid(vector)) return MORPH_NUMERIC_INVALID;
    if (matrix->columns != vector->length)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_vector_init(output, matrix->rows);
    if (status != MORPH_NUMERIC_OK) return status;
    for (row = 0; row < matrix->rows; ++row) {
        for (column = 0; column < matrix->columns; ++column) {
            output->data[row] += morph_numeric_matrix_get(matrix, row, column) *
                vector->data[column];
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_transpose_vector_multiply(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *vector)
{
    morph_numeric_status status;
    size_t row;
    size_t column;
    if (!morph_numeric_matrix_is_valid(matrix) ||
        !morph_numeric_vector_is_valid(vector)) return MORPH_NUMERIC_INVALID;
    if (matrix->rows != vector->length)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_vector_init(output, matrix->columns);
    if (status != MORPH_NUMERIC_OK) return status;
    for (row = 0; row < matrix->rows; ++row) {
        for (column = 0; column < matrix->columns; ++column) {
            output->data[column] +=
                morph_numeric_matrix_get(matrix, row, column) * vector->data[row];
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_matrix_diagonal(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix)
{
    morph_numeric_status status;
    size_t length;
    size_t index;
    if (!morph_numeric_matrix_is_valid(matrix)) return MORPH_NUMERIC_INVALID;
    length = morph_numeric_minimum(matrix->rows, matrix->columns);
    status = morph_numeric_vector_init(output, length);
    if (status != MORPH_NUMERIC_OK) return status;
    for (index = 0; index < length; ++index)
        output->data[index] = morph_numeric_matrix_get(matrix, index, index);
    return MORPH_NUMERIC_OK;
}

static double morph_numeric_matrix_scale(const morph_numeric_matrix *matrix)
{
    double scale = 0.0;
    size_t index;
    size_t count = matrix->rows * matrix->columns;
    for (index = 0; index < count; ++index) {
        double value = fabs(matrix->data[index]);
        if (value > scale) scale = value;
    }
    return scale;
}

morph_numeric_status morph_numeric_qr_factor(
    morph_numeric_qr *output, const morph_numeric_matrix *matrix,
    double tolerance)
{
    morph_numeric_status status;
    size_t k;
    size_t row;
    size_t column;
    double scale;
    if (!output || output->factors.data || output->tau.data || output->rank ||
        output->threshold != 0.0 || !morph_numeric_matrix_is_finite(matrix) ||
        !isfinite(tolerance) || tolerance < 0.0)
        return MORPH_NUMERIC_INVALID;
    if (matrix->rows < matrix->columns)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_matrix_copy(&output->factors, matrix);
    if (status != MORPH_NUMERIC_OK) return status;
    status = morph_numeric_vector_init(&output->tau, matrix->columns);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_matrix_dispose(&output->factors);
        return status;
    }
    scale = morph_numeric_matrix_scale(matrix);
    output->threshold = tolerance > 0.0 ? tolerance :
        DBL_EPSILON * (double)(matrix->rows > matrix->columns
            ? matrix->rows : matrix->columns) * (scale > 1.0 ? scale : 1.0);

    for (k = 0; k < matrix->columns; ++k) {
        double norm = 0.0;
        double alpha;
        double beta;
        double first;
        for (row = k; row < matrix->rows; ++row)
            norm = hypot(norm, morph_numeric_matrix_get(&output->factors, row, k));
        alpha = morph_numeric_matrix_get(&output->factors, k, k);
        if (norm <= output->threshold) {
            output->tau.data[k] = 0.0;
            morph_numeric_matrix_set(&output->factors, k, k, 0.0);
            continue;
        }
        beta = -copysign(norm, alpha == 0.0 ? 1.0 : alpha);
        first = alpha - beta;
        morph_numeric_matrix_set(&output->factors, k, k, beta);
        for (row = k + 1; row < matrix->rows; ++row) {
            morph_numeric_matrix_set(&output->factors, row, k,
                morph_numeric_matrix_get(&output->factors, row, k) / first);
        }
        output->tau.data[k] = (beta - alpha) / beta;
        output->rank++;

        for (column = k + 1; column < matrix->columns; ++column) {
            double dot = morph_numeric_matrix_get(&output->factors, k, column);
            for (row = k + 1; row < matrix->rows; ++row) {
                dot += morph_numeric_matrix_get(&output->factors, row, k) *
                    morph_numeric_matrix_get(&output->factors, row, column);
            }
            dot *= output->tau.data[k];
            morph_numeric_matrix_set(&output->factors, k, column,
                morph_numeric_matrix_get(&output->factors, k, column) - dot);
            for (row = k + 1; row < matrix->rows; ++row) {
                morph_numeric_matrix_set(&output->factors, row, column,
                    morph_numeric_matrix_get(&output->factors, row, column) -
                    morph_numeric_matrix_get(&output->factors, row, k) * dot);
            }
        }
    }
    return MORPH_NUMERIC_OK;
}

void morph_numeric_qr_dispose(morph_numeric_qr *qr)
{
    if (!qr) return;
    morph_numeric_matrix_dispose(&qr->factors);
    morph_numeric_vector_dispose(&qr->tau);
    qr->rank = 0;
    qr->threshold = 0.0;
}

morph_numeric_status morph_numeric_qr_solve(
    morph_numeric_vector *output, const morph_numeric_qr *qr,
    const morph_numeric_vector *right_hand_side)
{
    morph_numeric_vector work = {0};
    morph_numeric_status status;
    size_t k;
    size_t row;
    size_t reverse;
    size_t columns;
    if (!qr || !morph_numeric_matrix_is_valid(&qr->factors) ||
        !morph_numeric_vector_is_valid(&qr->tau) ||
        !morph_numeric_vector_is_finite(right_hand_side) ||
        !morph_numeric_matrix_is_finite(&qr->factors) ||
        !morph_numeric_vector_is_finite(&qr->tau) ||
        !isfinite(qr->threshold) || qr->threshold < 0.0 ||
        !morph_numeric_vector_is_empty(output)) return MORPH_NUMERIC_INVALID;
    columns = qr->factors.columns;
    if (qr->factors.rows != right_hand_side->length ||
        qr->tau.length != columns) return MORPH_NUMERIC_DIMENSION_MISMATCH;
    if (qr->rank != columns) return MORPH_NUMERIC_SINGULAR;
    status = morph_numeric_vector_copy(&work, right_hand_side);
    if (status != MORPH_NUMERIC_OK) return status;
    status = morph_numeric_vector_init(output, columns);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_vector_dispose(&work);
        return status;
    }
    for (k = 0; k < columns; ++k) {
        double dot = work.data[k];
        for (row = k + 1; row < qr->factors.rows; ++row) {
            dot += morph_numeric_matrix_get(&qr->factors, row, k) * work.data[row];
        }
        dot *= qr->tau.data[k];
        work.data[k] -= dot;
        for (row = k + 1; row < qr->factors.rows; ++row) {
            work.data[row] -=
                morph_numeric_matrix_get(&qr->factors, row, k) * dot;
        }
    }
    for (reverse = columns; reverse > 0; --reverse) {
        size_t index = reverse - 1;
        double value = work.data[index];
        double diagonal = morph_numeric_matrix_get(&qr->factors, index, index);
        if (fabs(diagonal) <= qr->threshold) {
            morph_numeric_vector_dispose(&work);
            morph_numeric_vector_dispose(output);
            return MORPH_NUMERIC_SINGULAR;
        }
        for (k = index + 1; k < columns; ++k) {
            value -= morph_numeric_matrix_get(&qr->factors, index, k) *
                output->data[k];
        }
        output->data[index] = value / diagonal;
    }
    morph_numeric_vector_dispose(&work);
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_qr_unpack(
    const morph_numeric_qr *qr, morph_numeric_matrix *q,
    morph_numeric_matrix *r)
{
    morph_numeric_status status;
    size_t rows;
    size_t columns;
    size_t index;
    size_t reverse;
    size_t row;
    size_t column;
    if (!qr || !morph_numeric_matrix_is_valid(&qr->factors) ||
        !morph_numeric_vector_is_valid(&qr->tau) ||
        !morph_numeric_matrix_is_empty(q) || !morph_numeric_matrix_is_empty(r))
        return MORPH_NUMERIC_INVALID;
    rows = qr->factors.rows;
    columns = qr->factors.columns;
    status = morph_numeric_matrix_init(q, rows, columns);
    if (status != MORPH_NUMERIC_OK) return status;
    status = morph_numeric_matrix_init(r, columns, columns);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_matrix_dispose(q);
        return status;
    }
    for (index = 0; index < columns; ++index)
        morph_numeric_matrix_set(q, index, index, 1.0);
    for (row = 0; row < columns; ++row) {
        for (column = row; column < columns; ++column) {
            morph_numeric_matrix_set(r, row, column,
                morph_numeric_matrix_get(&qr->factors, row, column));
        }
    }
    for (reverse = columns; reverse > 0; --reverse) {
        size_t k = reverse - 1;
        for (column = 0; column < columns; ++column) {
            double dot = morph_numeric_matrix_get(q, k, column);
            for (row = k + 1; row < rows; ++row) {
                dot += morph_numeric_matrix_get(&qr->factors, row, k) *
                    morph_numeric_matrix_get(q, row, column);
            }
            dot *= qr->tau.data[k];
            morph_numeric_matrix_set(q, k, column,
                morph_numeric_matrix_get(q, k, column) - dot);
            for (row = k + 1; row < rows; ++row) {
                morph_numeric_matrix_set(q, row, column,
                    morph_numeric_matrix_get(q, row, column) -
                    morph_numeric_matrix_get(&qr->factors, row, k) * dot);
            }
        }
    }
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_solve(
    morph_numeric_vector *output, const morph_numeric_matrix *matrix,
    const morph_numeric_vector *right_hand_side, double tolerance)
{
    morph_numeric_qr qr = {0};
    morph_numeric_status status;
    if (!morph_numeric_matrix_is_valid(matrix)) return MORPH_NUMERIC_INVALID;
    if (matrix->rows != matrix->columns)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_qr_factor(&qr, matrix, tolerance);
    if (status == MORPH_NUMERIC_OK)
        status = morph_numeric_qr_solve(output, &qr, right_hand_side);
    morph_numeric_qr_dispose(&qr);
    return status;
}

void morph_numeric_regression_dispose(morph_numeric_regression *regression)
{
    if (!regression) return;
    morph_numeric_vector_dispose(&regression->coefficients);
    morph_numeric_vector_dispose(&regression->fitted);
    regression->observations = 0;
    regression->predictors = 0;
    regression->residual_standard_deviation = 0.0;
}

morph_numeric_status morph_numeric_regression_fit(
    morph_numeric_regression *output, const morph_numeric_matrix *design,
    const morph_numeric_vector *response, double tolerance)
{
    morph_numeric_qr qr = {0};
    morph_numeric_status status;
    double residual_sum = 0.0;
    size_t index;
    if (!output || output->observations || output->predictors ||
        output->coefficients.data || output->fitted.data ||
        output->residual_standard_deviation != 0.0 ||
        !morph_numeric_matrix_is_valid(design) ||
        !morph_numeric_vector_is_finite(response) || !isfinite(tolerance) ||
        tolerance < 0.0) return MORPH_NUMERIC_INVALID;
    if (design->rows != response->length || design->rows <= design->columns)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    status = morph_numeric_qr_factor(&qr, design, tolerance);
    if (status == MORPH_NUMERIC_OK)
        status = morph_numeric_qr_solve(&output->coefficients, &qr, response);
    if (status == MORPH_NUMERIC_OK) {
        status = morph_numeric_matrix_vector_multiply(
            &output->fitted, design, &output->coefficients);
    }
    morph_numeric_qr_dispose(&qr);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_regression_dispose(output);
        return status;
    }
    for (index = 0; index < response->length; ++index) {
        double residual = response->data[index] - output->fitted.data[index];
        residual_sum += residual * residual;
    }
    output->observations = design->rows;
    output->predictors = design->columns;
    output->residual_standard_deviation = sqrt(
        residual_sum / (double)(design->rows - design->columns));
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_regression_predict(
    morph_numeric_vector *output,
    const morph_numeric_regression *regression,
    const morph_numeric_matrix *design)
{
    if (!regression || !regression->observations || !regression->predictors ||
        !morph_numeric_vector_is_valid(&regression->coefficients) ||
        !morph_numeric_matrix_is_valid(design)) return MORPH_NUMERIC_INVALID;
    if (design->columns != regression->predictors)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    return morph_numeric_matrix_vector_multiply(
        output, design, &regression->coefficients);
}

morph_numeric_status morph_numeric_symmetric_eigen(
    const morph_numeric_matrix *matrix, double tolerance,
    size_t max_iterations, morph_numeric_vector *eigenvalues,
    morph_numeric_matrix *eigenvectors)
{
    morph_numeric_matrix work = {0};
    morph_numeric_status status;
    double scale;
    double threshold;
    size_t dimension;
    size_t row;
    size_t column;
    size_t iteration;
    int converged = 0;
    if (!morph_numeric_matrix_is_finite(matrix) || !isfinite(tolerance) ||
        tolerance < 0.0 ||
        !morph_numeric_vector_is_empty(eigenvalues) ||
        !morph_numeric_matrix_is_empty(eigenvectors)) return MORPH_NUMERIC_INVALID;
    if (matrix->rows != matrix->columns)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    dimension = matrix->rows;
    if (dimension > MORPHEUS_NUMERIC_MAX_EIGEN_DIMENSION)
        return MORPH_NUMERIC_TOO_LARGE;
    scale = morph_numeric_matrix_scale(matrix);
    threshold = tolerance > 0.0 ? tolerance :
        DBL_EPSILON * (double)dimension * (scale > 1.0 ? scale : 1.0);
    for (row = 0; row < dimension; ++row) {
        for (column = row + 1; column < dimension; ++column) {
            if (fabs(morph_numeric_matrix_get(matrix, row, column) -
                    morph_numeric_matrix_get(matrix, column, row)) > threshold)
                return MORPH_NUMERIC_INVALID;
        }
    }
    status = morph_numeric_matrix_copy(&work, matrix);
    if (status != MORPH_NUMERIC_OK) return status;
    status = morph_numeric_matrix_identity(eigenvectors, dimension);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_matrix_dispose(&work);
        return status;
    }
    if (!max_iterations) max_iterations = 50u * dimension * dimension;
    for (iteration = 0; iteration < max_iterations; ++iteration) {
        size_t p = 0;
        size_t q = 0;
        double largest = 0.0;
        for (row = 0; row < dimension; ++row) {
            for (column = row + 1; column < dimension; ++column) {
                double value = fabs(morph_numeric_matrix_get(&work, row, column));
                if (value > largest) {
                    largest = value;
                    p = row;
                    q = column;
                }
            }
        }
        if (largest <= threshold) {
            converged = 1;
            break;
        }
        {
            double app = morph_numeric_matrix_get(&work, p, p);
            double aqq = morph_numeric_matrix_get(&work, q, q);
            double apq = morph_numeric_matrix_get(&work, p, q);
            double ratio = (aqq - app) / (2.0 * apq);
            double tangent = copysign(1.0, ratio) /
                (fabs(ratio) + hypot(1.0, ratio));
            double cosine = 1.0 / hypot(1.0, tangent);
            double sine = tangent * cosine;
            if (ratio == 0.0) {
                tangent = 1.0;
                cosine = 0.70710678118654752440;
                sine = cosine;
            }
            for (row = 0; row < dimension; ++row) {
                if (row != p && row != q) {
                    double arp = morph_numeric_matrix_get(&work, row, p);
                    double arq = morph_numeric_matrix_get(&work, row, q);
                    double new_rp = cosine * arp - sine * arq;
                    double new_rq = sine * arp + cosine * arq;
                    morph_numeric_matrix_set(&work, row, p, new_rp);
                    morph_numeric_matrix_set(&work, p, row, new_rp);
                    morph_numeric_matrix_set(&work, row, q, new_rq);
                    morph_numeric_matrix_set(&work, q, row, new_rq);
                }
                {
                    double vrp = morph_numeric_matrix_get(eigenvectors, row, p);
                    double vrq = morph_numeric_matrix_get(eigenvectors, row, q);
                    morph_numeric_matrix_set(eigenvectors, row, p,
                        cosine * vrp - sine * vrq);
                    morph_numeric_matrix_set(eigenvectors, row, q,
                        sine * vrp + cosine * vrq);
                }
            }
            morph_numeric_matrix_set(&work, p, p, app - tangent * apq);
            morph_numeric_matrix_set(&work, q, q, aqq + tangent * apq);
            morph_numeric_matrix_set(&work, p, q, 0.0);
            morph_numeric_matrix_set(&work, q, p, 0.0);
        }
    }
    if (!converged) {
        morph_numeric_matrix_dispose(&work);
        morph_numeric_matrix_dispose(eigenvectors);
        return MORPH_NUMERIC_NOT_CONVERGED;
    }
    status = morph_numeric_vector_init(eigenvalues, dimension);
    if (status != MORPH_NUMERIC_OK) {
        morph_numeric_matrix_dispose(&work);
        morph_numeric_matrix_dispose(eigenvectors);
        return status;
    }
    for (row = 0; row < dimension; ++row)
        eigenvalues->data[row] = morph_numeric_matrix_get(&work, row, row);
    morph_numeric_matrix_dispose(&work);

    for (row = 0; row < dimension; ++row) {
        size_t smallest = row;
        for (column = row + 1; column < dimension; ++column) {
            if (eigenvalues->data[column] < eigenvalues->data[smallest])
                smallest = column;
        }
        if (smallest != row) {
            double value = eigenvalues->data[row];
            eigenvalues->data[row] = eigenvalues->data[smallest];
            eigenvalues->data[smallest] = value;
            for (iteration = 0; iteration < dimension; ++iteration) {
                value = morph_numeric_matrix_get(eigenvectors, iteration, row);
                morph_numeric_matrix_set(eigenvectors, iteration, row,
                    morph_numeric_matrix_get(eigenvectors, iteration, smallest));
                morph_numeric_matrix_set(eigenvectors, iteration, smallest, value);
            }
        }
    }
    return MORPH_NUMERIC_OK;
}

static morph_numeric_status morph_numeric_vector_to_array(
    const morph_numeric_vector *input, double *output, size_t length)
{
    if (!output || !morph_numeric_vector_is_valid(input))
        return MORPH_NUMERIC_INVALID;
    if (input->length != length) return MORPH_NUMERIC_DIMENSION_MISMATCH;
    memcpy(output, input->data, length * sizeof(*output));
    return MORPH_NUMERIC_OK;
}

static morph_numeric_status morph_numeric_matrix_to_array(
    const morph_numeric_matrix *input, double *output, size_t dimension)
{
    if (!output || !morph_numeric_matrix_is_valid(input))
        return MORPH_NUMERIC_INVALID;
    if (input->rows != dimension || input->columns != dimension)
        return MORPH_NUMERIC_DIMENSION_MISMATCH;
    memcpy(output, input->data, dimension * dimension * sizeof(*output));
    return MORPH_NUMERIC_OK;
}

morph_numeric_status morph_numeric_vector_from_v2d(
    morph_numeric_vector *output, V2d value)
{
    return morph_numeric_vector_from_array(output, value.c, 2);
}

morph_numeric_status morph_numeric_vector_from_v3d(
    morph_numeric_vector *output, V3d value)
{
    return morph_numeric_vector_from_array(output, value.c, 3);
}

morph_numeric_status morph_numeric_vector_from_v4d(
    morph_numeric_vector *output, V4d value)
{
    return morph_numeric_vector_from_array(output, value.c, 4);
}

morph_numeric_status morph_numeric_vector_to_v2d(
    const morph_numeric_vector *input, V2d *output)
{
    return output
        ? morph_numeric_vector_to_array(input, output->c, 2)
        : MORPH_NUMERIC_INVALID;
}

morph_numeric_status morph_numeric_vector_to_v3d(
    const morph_numeric_vector *input, V3d *output)
{
    return output
        ? morph_numeric_vector_to_array(input, output->c, 3)
        : MORPH_NUMERIC_INVALID;
}

morph_numeric_status morph_numeric_vector_to_v4d(
    const morph_numeric_vector *input, V4d *output)
{
    return output
        ? morph_numeric_vector_to_array(input, output->c, 4)
        : MORPH_NUMERIC_INVALID;
}

morph_numeric_status morph_numeric_matrix_from_m2d(
    morph_numeric_matrix *output, M2d value)
{
    return morph_numeric_matrix_from_array(output, value.c, 2, 2);
}

morph_numeric_status morph_numeric_matrix_from_m3d(
    morph_numeric_matrix *output, M3d value)
{
    return morph_numeric_matrix_from_array(output, value.c, 3, 3);
}

morph_numeric_status morph_numeric_matrix_from_m4d(
    morph_numeric_matrix *output, M4d value)
{
    return morph_numeric_matrix_from_array(output, value.c, 4, 4);
}

morph_numeric_status morph_numeric_matrix_to_m2d(
    const morph_numeric_matrix *input, M2d *output)
{
    return output
        ? morph_numeric_matrix_to_array(input, output->c, 2)
        : MORPH_NUMERIC_INVALID;
}

morph_numeric_status morph_numeric_matrix_to_m3d(
    const morph_numeric_matrix *input, M3d *output)
{
    return output
        ? morph_numeric_matrix_to_array(input, output->c, 3)
        : MORPH_NUMERIC_INVALID;
}

morph_numeric_status morph_numeric_matrix_to_m4d(
    const morph_numeric_matrix *input, M4d *output)
{
    return output
        ? morph_numeric_matrix_to_array(input, output->c, 4)
        : MORPH_NUMERIC_INVALID;
}
