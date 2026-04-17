#pragma once
// ══════════════════════════════════════════════════════════
//  SimdMath.h  —  SSE4.1 accelerated math helpers
//  Included by Enemy.h for fast bone matrix operations
// ══════════════════════════════════════════════════════════
#ifdef _WIN32
#include <smmintrin.h>   // SSE4.1
#include <glm/glm.hpp>

// Fast mat4 * mat4 using SSE4.1
// ~2x faster than scalar on bones
inline glm::mat4 mat4MulSSE(const glm::mat4& A, const glm::mat4& B)
{
    glm::mat4 R;
    // Each column of B dot rows of A using _mm_dp_ps (SSE4.1)
    for (int col = 0; col < 4; col++)
    {
        __m128 b = _mm_loadu_ps(&B[col][0]);   // column col of B

        __m128 a0 = _mm_loadu_ps(&A[0][0]);
        __m128 a1 = _mm_loadu_ps(&A[1][0]);
        __m128 a2 = _mm_loadu_ps(&A[2][0]);
        __m128 a3 = _mm_loadu_ps(&A[3][0]);

        // Transpose: get row i of A = (A[0][i], A[1][i], A[2][i], A[3][i])
        // GLM stores column-major so A[c][r]
        // R[col][r] = dot(row r of A, col of B)
        __m128 row0 = _mm_set_ps(A[3][0], A[2][0], A[1][0], A[0][0]);
        __m128 row1 = _mm_set_ps(A[3][1], A[2][1], A[1][1], A[0][1]);
        __m128 row2 = _mm_set_ps(A[3][2], A[2][2], A[1][2], A[0][2]);
        __m128 row3 = _mm_set_ps(A[3][3], A[2][3], A[1][3], A[0][3]);

        // _mm_dp_ps(a, b, 0xF1) = dot of all 4 floats → stored in lane 0
        R[col][0] = _mm_cvtss_f32(_mm_dp_ps(row0, b, 0xF1));
        R[col][1] = _mm_cvtss_f32(_mm_dp_ps(row1, b, 0xF1));
        R[col][2] = _mm_cvtss_f32(_mm_dp_ps(row2, b, 0xF1));
        R[col][3] = _mm_cvtss_f32(_mm_dp_ps(row3, b, 0xF1));
    }
    return R;
}

// Fast vec4 transform by mat4 using SSE4.1 dot product
inline glm::vec4 transformSSE(const glm::mat4& M, const glm::vec4& v)
{
    __m128 vv = _mm_loadu_ps(&v[0]);
    return glm::vec4(
        _mm_cvtss_f32(_mm_dp_ps(_mm_loadu_ps(&M[0][0]), vv, 0xF1)),
        _mm_cvtss_f32(_mm_dp_ps(_mm_loadu_ps(&M[1][0]), vv, 0xF1)),
        _mm_cvtss_f32(_mm_dp_ps(_mm_loadu_ps(&M[2][0]), vv, 0xF1)),
        _mm_cvtss_f32(_mm_dp_ps(_mm_loadu_ps(&M[3][0]), vv, 0xF1))
    );
}

#else
// Fallback: just use GLM (non-Windows / no SSE)
inline glm::mat4 mat4MulSSE(const glm::mat4& A, const glm::mat4& B) { return A * B; }
inline glm::vec4 transformSSE(const glm::mat4& M, const glm::vec4& v) { return M * v; }
#endif