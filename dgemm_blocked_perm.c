#include "immintrin.h"
#include <stdio.h>

const char* dgemm_desc = "Simple blocked dgemm.";

/* In theory, we will need to fit all of A and all of B into the L2 cache,
 * as well as a 1-D vector of BLOCK_SIZE for C.
 *
 * Given the 256KB L2 cache size for the cluster, we must compute
 *
 *     (256 * 1024)bytes = 2b^2 + b, where b is BLOCK_SIZE
 *                       ~ 361
 *
 * However, having byte-aligned memory is desireable in order to compute
 * using various vector operations.  The result being that we want to
 * take into consideration the byte-alignment we seek to use here.
 *
 * So if we choose a byte alignment of 64, then we should choose BLOCK_SIZE
 * to be floor(361 / 64) * 64 = 320.  However, if we seek to use a 16byte
 * alignment, then we should choose BLOCK_SIZE = floor(361 / 16) * 16 = 352.
 */

#define BYTE_ALIGN 64
#define BLOCK_SIZE 64// ((361 / BYTE_ALIGN) * BYTE_ALIGN) // important to leave as a define for compiler optimizations

#define KERNEL_SIZE 8

#ifndef NULL
#define NULL ((void *)0)
#endif

double * restrict A_KERNEL = NULL;
double * restrict B_KERNEL = NULL;
double * restrict C_KERNEL = NULL;

// more convenient access; column major
#define A(i, j) A[(j) + (i)]
#define B(i, j) B[(j) + (i)]
#define C(i, j) C[(j) + (i)]

// more convenient access; row major
#define A_KERNEL(i, j) A_KERNEL[(i)*KERNEL_SIZE + (j)]
#define B_KERNEL(i, j) B_KERNEL[(i)*KERNEL_SIZE + (j)]
#define C_KERNEL(i, j) C_KERNEL[(i)*KERNEL_SIZE + (j)]

// assumes ymm16-31 already have the rows of B, and that ymm00-015 can be clobbered
inline void row8x8(unsigned int row, double * restrict A, double * restrict C,
                   __m256d ymm00, __m256d ymm01, __m256d ymm02, __m256d ymm03, __m256d ymm04, __m256d ymm05, __m256d ymm06, __m256d ymm07,// piecewise store A
                   __m256d ymm08, __m256d ymm09, __m256d ymm10, __m256d ymm11, __m256d ymm12, __m256d ymm13, __m256d ymm14, __m256d ymm15,
                   __m256d ymm16, __m256d ymm17, __m256d ymm18, __m256d ymm19, __m256d ymm20, __m256d ymm21, __m256d ymm22, __m256d ymm23,// piecewise store B
                   __m256d ymm24, __m256d ymm25, __m256d ymm26, __m256d ymm27, __m256d ymm28, __m256d ymm29, __m256d ymm30, __m256d ymm31) {

    __assume_aligned(A, BYTE_ALIGN);// unsure if this is necessary with an inline being called
    __assume_aligned(C, BYTE_ALIGN);// by an inline, but shouldn't cause too much of a ruckus

    // Broadcast each element of matrix A Row [row] into a ymm register
    // If row = [ a b c d e f g h ], then we need two registers for each
    ymm00 = _mm256_broadcast_sd((double *)(A + row*8 + 0)); ymm01 = _mm256_broadcast_sd((double *)(A + row*8 + 0));// a
    ymm02 = _mm256_broadcast_sd((double *)(A + row*8 + 1)); ymm03 = _mm256_broadcast_sd((double *)(A + row*8 + 1));// b
    ymm04 = _mm256_broadcast_sd((double *)(A + row*8 + 2)); ymm05 = _mm256_broadcast_sd((double *)(A + row*8 + 2));// c
    ymm06 = _mm256_broadcast_sd((double *)(A + row*8 + 3)); ymm07 = _mm256_broadcast_sd((double *)(A + row*8 + 3));// d
    ymm08 = _mm256_broadcast_sd((double *)(A + row*8 + 4)); ymm09 = _mm256_broadcast_sd((double *)(A + row*8 + 4));// e
    ymm10 = _mm256_broadcast_sd((double *)(A + row*8 + 5)); ymm11 = _mm256_broadcast_sd((double *)(A + row*8 + 5));// f
    ymm12 = _mm256_broadcast_sd((double *)(A + row*8 + 6)); ymm13 = _mm256_broadcast_sd((double *)(A + row*8 + 6));// g
    ymm14 = _mm256_broadcast_sd((double *)(A + row*8 + 7)); ymm15 = _mm256_broadcast_sd((double *)(A + row*8 + 7));// h

    // multiply
    // left half
    ymm00 = _mm256_mul_pd(ymm00, ymm16); ymm01 = _mm256_mul_pd(ymm01, ymm17);// row 1
    ymm02 = _mm256_mul_pd(ymm02, ymm18); ymm03 = _mm256_mul_pd(ymm03, ymm19);// row 2
    ymm04 = _mm256_mul_pd(ymm04, ymm20); ymm05 = _mm256_mul_pd(ymm05, ymm21);// row 3
    ymm06 = _mm256_mul_pd(ymm06, ymm22); ymm07 = _mm256_mul_pd(ymm07, ymm23);// row 4
    ymm08 = _mm256_mul_pd(ymm08, ymm24); ymm09 = _mm256_mul_pd(ymm09, ymm25);// row 5
    ymm10 = _mm256_mul_pd(ymm10, ymm26); ymm11 = _mm256_mul_pd(ymm11, ymm27);// row 6
    ymm12 = _mm256_mul_pd(ymm12, ymm28); ymm13 = _mm256_mul_pd(ymm13, ymm29);// row 7
    ymm14 = _mm256_mul_pd(ymm14, ymm30); ymm15 = _mm256_mul_pd(ymm15, ymm31);// row 8
    // add up
    ymm00 = _mm256_add_pd(ymm00, ymm02); ymm01 = _mm256_add_pd(ymm01, ymm03);
    ymm04 = _mm256_add_pd(ymm04, ymm06); ymm05 = _mm256_add_pd(ymm05, ymm07);
    ymm08 = _mm256_add_pd(ymm08, ymm10); ymm09 = _mm256_add_pd(ymm09, ymm11);
    ymm12 = _mm256_add_pd(ymm12, ymm14); ymm13 = _mm256_add_pd(ymm13, ymm15);
    //++
    ymm00 = _mm256_add_pd(ymm00, ymm04); ymm01 = _mm256_add_pd(ymm01, ymm05);
    ymm08 = _mm256_add_pd(ymm08, ymm12); ymm09 = _mm256_add_pd(ymm09, ymm13);
    //++
    ymm00 = _mm256_add_pd(ymm00, ymm08); ymm01 = _mm256_add_pd(ymm01, ymm09);

    // ym00 and ym08 now hold the left and right halves, store back in C
    _mm256_store_pd((double *) (C + row*8), ymm00); _mm256_store_pd((double *) (C + row*8 + 4), ymm01);
}

inline void vectorized8x8(double * restrict A, double * restrict B, double * restrict C) {
    __assume_aligned(A, BYTE_ALIGN);
    __assume_aligned(B, BYTE_ALIGN);
    __assume_aligned(C, BYTE_ALIGN);
    
    // adapted from:
    //     https://software.intel.com/en-us/articles/benefits-of-intel-avx-for-small-matrices

    __m256d ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,// piecewise store A
            ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
            ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,// piecewise store B
            ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31;

    // Read in the 8 rows of Matrix B into zmm registers
    ymm16 = _mm256_load_pd((double *) (B + 0*8)); ymm17 = _mm256_load_pd((double *) (B + 0*8 + 4));// row 1
    ymm18 = _mm256_load_pd((double *) (B + 1*8)); ymm19 = _mm256_load_pd((double *) (B + 1*8 + 4));// row 2
    ymm20 = _mm256_load_pd((double *) (B + 2*8)); ymm21 = _mm256_load_pd((double *) (B + 2*8 + 4));// row 3
    ymm22 = _mm256_load_pd((double *) (B + 3*8)); ymm23 = _mm256_load_pd((double *) (B + 3*8 + 4));// row 4
    ymm24 = _mm256_load_pd((double *) (B + 4*8)); ymm25 = _mm256_load_pd((double *) (B + 4*8 + 4));// row 5
    ymm26 = _mm256_load_pd((double *) (B + 5*8)); ymm27 = _mm256_load_pd((double *) (B + 5*8 + 4));// row 6
    ymm28 = _mm256_load_pd((double *) (B + 6*8)); ymm29 = _mm256_load_pd((double *) (B + 6*8 + 4));// row 7
    ymm30 = _mm256_load_pd((double *) (B + 7*8)); ymm31 = _mm256_load_pd((double *) (B + 7*8 + 4));// row 8

    // row by row computations
    row8x8(0, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(1, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(2, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(3, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(4, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(5, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(6, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
    row8x8(7, A, C,
           ymm00, ymm01, ymm02, ymm03, ymm04, ymm05, ymm06, ymm07,
           ymm08, ymm09, ymm10, ymm11, ymm12, ymm13, ymm14, ymm15,
           ymm16, ymm17, ymm18, ymm19, ymm20, ymm21, ymm22, ymm23,
           ymm24, ymm25, ymm26, ymm27, ymm28, ymm29, ymm30, ymm31);
}

#include <stdio.h>

/*
  A is M-by-K
  B is K-by-N
  C is M-by-N
  lda is the leading dimension of the matrix (the M of square_dgemm).
*/
void basic_dgemm(const int lda, const int M, const int N, const int K,
                 const double * restrict A, const double * restrict B, double * restrict C,
                 int shortcut)
{
    if(1) {
        int i, j, k;
        for (j = 0; j < N; ++j) {
            for (k = 0; k < K; ++k){
                double bkj = B[j*lda+k];
                for (i = 0; i < M; ++i) {
                    C[j*lda+i] += A[k*lda+i] * bkj;
                }
            }
        }
    }
    else {
        // sub-blocking based on KERNEL_SIZE
        int row_kernels = M / KERNEL_SIZE + (M % KERNEL_SIZE ? 1 : 0);
        int col_kernels = N / KERNEL_SIZE + (N % KERNEL_SIZE ? 1 : 0);
        int sli_kernels = K / KERNEL_SIZE + (K % KERNEL_SIZE ? 1 : 0);

        int row, col, sli, // i, j, k offsets for outer loop
            ki_row, ki_sli,// kernel row i and k offsets for copying to aligned memory
            kj_col, kj_sli;// kernel col j and k offsets for copying to aligned memory
        
        for(int bj = 0; bj < col_kernels; ++bj) {
            col = bj * KERNEL_SIZE;

            for(int bk = 0; bk < sli_kernels; ++bk) {
                sli = bk * KERNEL_SIZE;

                for(int bi = 0; bi < row_kernels; ++bi) {
                    row = bi * KERNEL_SIZE;

                    // copy from A and B to byte aligned memory, zero out aligned C
                    #pragma unroll
                    for(int kj = 0; kj < KERNEL_SIZE; ++kj) {
                        kj_col = kj + col;
                        kj_sli = kj + sli;
                        for(int ki = 0; ki < KERNEL_SIZE; ++ki) {
                            ki_row = ki + row;
                            ki_sli = ki + sli;

                            // if this is a valid location, copy the data.  Otherwise, pad with 0s
                            // A is MxK
                            if(ki_row < M && kj_sli < K)
                                A_KERNEL(ki, kj) = A[kj_sli*lda + ki_row];
                            else
                                A_KERNEL(ki, kj) = 0.0;
                            // B is KxN
                            if(ki_sli < K && kj_col < N)
                                B_KERNEL(ki, kj) = B[kj_col*lda + ki_sli];
                            else
                                B_KERNEL(ki, kj) = 0.0;
                            // need to zero out C for each round
                            C_KERNEL(ki, kj) = 0.0;
                        }
                    }

                    // multiplies A_KERNEL and B_KERNEL, stores results in C_KERNEL
                    vectorized8x8(A_KERNEL, B_KERNEL, C_KERNEL);

                    // copy everything back to C
                    #pragma unroll
                    for(int kj = 0; kj < KERNEL_SIZE; ++kj) {
                        kj_col = kj + col;
                        for(int ki = 0; ki < KERNEL_SIZE; ++ki) {
                            ki_row = ki + row;
                            if(ki_row < M && kj_col < N)
                                C[kj_col*lda + ki_row] += C_KERNEL(ki, kj);
                        }
                    }
                }
            }
        }
    }
}

void do_block(const int lda,
              const double * restrict A, const double * restrict B, double * restrict C,
              const int i, const int j, const int k)
{
    const int M = (i+BLOCK_SIZE > lda? lda-i : BLOCK_SIZE);
    const int N = (j+BLOCK_SIZE > lda? lda-j : BLOCK_SIZE);
    const int K = (k+BLOCK_SIZE > lda? lda-k : BLOCK_SIZE);
    basic_dgemm(lda, M, N, K,
                A + i + k*lda, B + k + j*lda, C + i + j*lda,
                0);
}

void square_dgemm(const int M, const double * restrict A, const double * restrict B, double * restrict C)
  {
    if (M <= BLOCK_SIZE) {
       basic_dgemm(M, M, M, M, A, B, C, 1);
       return;
    }

     A_KERNEL = (double *) _mm_malloc(KERNEL_SIZE * KERNEL_SIZE * sizeof(double), BYTE_ALIGN);
     B_KERNEL = (double *) _mm_malloc(KERNEL_SIZE * KERNEL_SIZE * sizeof(double), BYTE_ALIGN);
     C_KERNEL = (double *) _mm_malloc(KERNEL_SIZE * KERNEL_SIZE * sizeof(double), BYTE_ALIGN);

    const int n_blocks = M / BLOCK_SIZE + (M%BLOCK_SIZE? 1 : 0);
    int bi, bj, bk;

    for (bi = 0; bi < n_blocks; ++bi) {
        const int i = bi * BLOCK_SIZE;
        for (bj = 0; bj < n_blocks; ++bj) {
            const int j = bj * BLOCK_SIZE;
            for (bk = 0; bk < n_blocks; ++bk) {
                const int k = bk * BLOCK_SIZE;
                do_block(M, A, B, C, i, j, k);
            }
        }
    }

    _mm_free(A_KERNEL); A_KERNEL = NULL;
    _mm_free(B_KERNEL); B_KERNEL = NULL;
    _mm_free(C_KERNEL); C_KERNEL = NULL;
}
