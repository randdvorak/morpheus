#include <math.h>
#include <stdio.h>

#include "math_host.h"

static int near(double left, double right, double tolerance)
{
    return fabs(left - right) <= tolerance;
}

static int test_fixed_and_interop(void)
{
    const float half_pi = 1.57079632679489661923f;
    V3f x = v3f(1.0f, 0.0f, 0.0f);
    V3f y = v3f(0.0f, 1.0f, 0.0f);
    V3f fallback = v3f(9.0f, 8.0f, 7.0f);
    V3f cross = v3f_cross(x, y);
    V3f normalized_zero = v3f_norm(v3ff(0.0f), 1e-6f, fallback);
    V4f rotated = m4f_mul_vec(
        m4f_mul(m4f_id(), m4f_rot_z(half_pi)),
        v4f(1.0f, 0.0f, 0.0f, 1.0f));
    V3d fixed = v3d(2.0, 3.0, 4.0);
    V3d round_trip = {0};
    M3d fixed_matrix = m3d_rot_z(0.25);
    M3d matrix_round_trip = {0};
    morph_numeric_vector dynamic = {0};
    morph_numeric_matrix dynamic_matrix = {0};
    int valid;

    if (morph_numeric_vector_from_v3d(&dynamic, fixed) != MORPH_NUMERIC_OK ||
        morph_numeric_vector_to_v3d(&dynamic, &round_trip) != MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_m3d(&dynamic_matrix, fixed_matrix) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_matrix_to_m3d(&dynamic_matrix, &matrix_round_trip) !=
            MORPH_NUMERIC_OK) return 1;
    valid = near(cross.x, 0.0, 1e-5) && near(cross.y, 0.0, 1e-5) &&
        near(cross.z, 1.0, 1e-5) &&
        near(normalized_zero.x, fallback.x, 1e-5) &&
        near(normalized_zero.y, fallback.y, 1e-5) &&
        near(normalized_zero.z, fallback.z, 1e-5) &&
        near(rotated.x, 0.0, 1e-5) && near(rotated.y, 1.0, 1e-5) &&
        near(rotated.w, 1.0, 1e-5) &&
        near(round_trip.x, fixed.x, 1e-12) &&
        near(round_trip.y, fixed.y, 1e-12) &&
        near(round_trip.z, fixed.z, 1e-12) &&
        near(matrix_round_trip._12, fixed_matrix._12, 1e-12) &&
        near(matrix_round_trip._21, fixed_matrix._21, 1e-12);
    morph_numeric_vector_dispose(&dynamic);
    morph_numeric_matrix_dispose(&dynamic_matrix);
    return valid ? 0 : 2;
}

static int test_qr_and_regression(void)
{
    static const double design_data[] = {
        1.0, 1.0,
        1.0, 2.0,
        1.0, 3.0
    };
    static const double response_data[] = {3.0, 5.0, 7.0};
    static const double prediction_data[] = {
        1.0, 4.0,
        1.0, 5.0
    };
    static const double square_data[] = {2.0, 1.0, 1.0, 3.0};
    static const double rhs_data[] = {5.0, 7.0};
    static const double singular_data[] = {1.0, 2.0, 2.0, 4.0};
    morph_numeric_matrix design = {0};
    morph_numeric_vector response = {0};
    morph_numeric_qr qr = {0};
    morph_numeric_matrix q = {0};
    morph_numeric_matrix r = {0};
    morph_numeric_matrix reconstructed = {0};
    morph_numeric_matrix qtq = {0};
    morph_numeric_regression regression = {0};
    morph_numeric_matrix prediction_design = {0};
    morph_numeric_vector predictions = {0};
    morph_numeric_matrix square = {0};
    morph_numeric_vector rhs = {0};
    morph_numeric_vector solution = {0};
    morph_numeric_matrix singular = {0};
    morph_numeric_vector singular_solution = {0};
    morph_numeric_vector normalized = {0};
    morph_numeric_vector zero = {0};
    morph_numeric_status singular_status;
    size_t index;
    int valid = 0;

    if (morph_numeric_matrix_from_array(&design, design_data, 3, 2) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_vector_from_array(&response, response_data, 3) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_qr_factor(&qr, &design, 0.0) != MORPH_NUMERIC_OK ||
        morph_numeric_qr_unpack(&qr, &q, &r) != MORPH_NUMERIC_OK ||
        morph_numeric_matrix_multiply(&reconstructed, &q, &r) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_matrix_multiply_transpose_left(&qtq, &q, &q) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_regression_fit(&regression, &design, &response, 0.0) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_array(
            &prediction_design, prediction_data, 2, 2) != MORPH_NUMERIC_OK ||
        morph_numeric_regression_predict(
            &predictions, &regression, &prediction_design) != MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_array(&square, square_data, 2, 2) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_vector_from_array(&rhs, rhs_data, 2) != MORPH_NUMERIC_OK ||
        morph_numeric_solve(&solution, &square, &rhs, 0.0) != MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_array(&singular, singular_data, 2, 2) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_vector_init(&zero, 3) != MORPH_NUMERIC_OK) goto cleanup;
    singular_status = morph_numeric_solve(
        &singular_solution, &singular, &rhs, 0.0);
    if (singular_status != MORPH_NUMERIC_SINGULAR ||
        morph_numeric_vector_normalize(&normalized, &zero, 0.0) !=
            MORPH_NUMERIC_SINGULAR ||
        morph_numeric_vector_normalize(&normalized, &response, -1.0) !=
            MORPH_NUMERIC_INVALID) goto cleanup;
    for (index = 0; index < 6; ++index) {
        if (!near(reconstructed.data[index], design.data[index], 1e-10))
            goto cleanup;
    }
    for (index = 0; index < 4; ++index) {
        double expected = index == 0 || index == 3 ? 1.0 : 0.0;
        if (!near(qtq.data[index], expected, 1e-10)) goto cleanup;
    }
    valid = near(regression.coefficients.data[0], 1.0, 1e-10) &&
        near(regression.coefficients.data[1], 2.0, 1e-10) &&
        near(regression.residual_standard_deviation, 0.0, 1e-10) &&
        near(predictions.data[0], 9.0, 1e-10) &&
        near(predictions.data[1], 11.0, 1e-10) &&
        near(solution.data[0], 1.6, 1e-10) &&
        near(solution.data[1], 1.8, 1e-10);

cleanup:
    morph_numeric_matrix_dispose(&design);
    morph_numeric_vector_dispose(&response);
    morph_numeric_qr_dispose(&qr);
    morph_numeric_matrix_dispose(&q);
    morph_numeric_matrix_dispose(&r);
    morph_numeric_matrix_dispose(&reconstructed);
    morph_numeric_matrix_dispose(&qtq);
    morph_numeric_regression_dispose(&regression);
    morph_numeric_matrix_dispose(&prediction_design);
    morph_numeric_vector_dispose(&predictions);
    morph_numeric_matrix_dispose(&square);
    morph_numeric_vector_dispose(&rhs);
    morph_numeric_vector_dispose(&solution);
    morph_numeric_matrix_dispose(&singular);
    morph_numeric_vector_dispose(&singular_solution);
    morph_numeric_vector_dispose(&zero);
    morph_numeric_vector_dispose(&normalized);
    return valid ? 0 : 3;
}

static int test_eigen_and_limits(void)
{
    static const double symmetric_data[] = {
        2.0, 1.0, 0.0,
        1.0, 2.0, 1.0,
        0.0, 1.0, 2.0
    };
    static const double nonsymmetric_data[] = {1.0, 2.0, 0.0, 1.0};
    static const double nonfinite_data[] = {1.0, NAN, NAN, 1.0};
    morph_numeric_matrix symmetric = {0};
    morph_numeric_matrix nonsymmetric = {0};
    morph_numeric_matrix nonfinite = {0};
    morph_numeric_vector eigenvalues = {0};
    morph_numeric_matrix eigenvectors = {0};
    morph_numeric_vector invalid_values = {0};
    morph_numeric_matrix invalid_vectors = {0};
    morph_numeric_vector unconverged_values = {0};
    morph_numeric_matrix unconverged_vectors = {0};
    morph_numeric_vector nonfinite_values = {0};
    morph_numeric_matrix nonfinite_vectors = {0};
    morph_numeric_matrix oversized = {0};
    const double expected_values[] = {
        2.0 - 1.4142135623730950488,
        2.0,
        2.0 + 1.4142135623730950488
    };
    size_t column;
    size_t row;
    int valid = 1;

    if (morph_numeric_matrix_from_array(&symmetric, symmetric_data, 3, 3) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_symmetric_eigen(
            &symmetric, 1e-13, 128, &eigenvalues, &eigenvectors) !=
            MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_array(
            &nonsymmetric, nonsymmetric_data, 2, 2) != MORPH_NUMERIC_OK ||
        morph_numeric_matrix_from_array(
            &nonfinite, nonfinite_data, 2, 2) != MORPH_NUMERIC_OK)
        valid = 0;
    for (column = 0; valid && column < 3; ++column) {
        if (!near(eigenvalues.data[column], expected_values[column], 1e-10))
            valid = 0;
        for (row = 0; valid && row < 3; ++row) {
            double product = 0.0;
            size_t shared;
            for (shared = 0; shared < 3; ++shared) {
                product += symmetric.data[row * 3 + shared] *
                    eigenvectors.data[shared * 3 + column];
            }
            if (!near(product, eigenvalues.data[column] *
                    eigenvectors.data[row * 3 + column], 1e-10)) valid = 0;
        }
    }
    if (valid && morph_numeric_symmetric_eigen(
            &nonsymmetric, 1e-13, 64, &invalid_values, &invalid_vectors) !=
            MORPH_NUMERIC_INVALID) valid = 0;
    if (valid && morph_numeric_symmetric_eigen(
            &nonfinite, 1e-13, 64, &nonfinite_values, &nonfinite_vectors) !=
            MORPH_NUMERIC_INVALID) valid = 0;
    if (valid && morph_numeric_matrix_init(
            &oversized, MORPHEUS_NUMERIC_MAX_ELEMENTS + 1u, 1) !=
            MORPH_NUMERIC_TOO_LARGE) valid = 0;
    if (valid && morph_numeric_symmetric_eigen(
            &symmetric, 1e-13, 1, &unconverged_values,
            &unconverged_vectors) != MORPH_NUMERIC_NOT_CONVERGED) valid = 0;
    morph_numeric_matrix_dispose(&symmetric);
    morph_numeric_matrix_dispose(&nonsymmetric);
    morph_numeric_matrix_dispose(&nonfinite);
    morph_numeric_vector_dispose(&eigenvalues);
    morph_numeric_matrix_dispose(&eigenvectors);
    morph_numeric_vector_dispose(&invalid_values);
    morph_numeric_matrix_dispose(&invalid_vectors);
    morph_numeric_vector_dispose(&unconverged_values);
    morph_numeric_matrix_dispose(&unconverged_vectors);
    morph_numeric_vector_dispose(&nonfinite_values);
    morph_numeric_matrix_dispose(&nonfinite_vectors);
    morph_numeric_matrix_dispose(&oversized);
    return valid ? 0 : 4;
}

int main(void)
{
    int result = test_fixed_and_interop();
    if (!result) result = test_qr_and_regression();
    if (!result) result = test_eigen_and_limits();
    if (result) {
        fprintf(stderr, "math host test failed at stage %d\n", result);
        return result;
    }
    puts("PASS: fixed-size and checked dynamic math host interface");
    return 0;
}
