// @HEADER
// ***********************************************************************
//
//          Tpetra: Templated Linear Algebra Services Package
//                 Copyright (2008) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Michael A. Heroux (maherou@sandia.gov)
//
// ************************************************************************
// @HEADER

#include "Tpetra_Experimental_BlockView.hpp"

#ifdef HAVE_TPETRA_INST_FLOAT128
namespace Tpetra {
namespace Details {

void
Lapack128::
SWAP (const int N, __float128* X, const int INCX, __float128* Y, const int INCY) const
{
  if (N <= 0) {
    return;
  }

  int ix = 1;
  int iy = 1;
  if (INCX < 0) {
    ix = (1-N) * INCX + 1;
    iy = (1-N) * INCY + 1;
  }

  for (int i = 1; i <= N; ++i) {
    __float128 temp = X[ix - 1];
    X[ix - 1] = Y[iy - 1];
    Y[iy - 1] = temp;
    ix += INCX;
    iy += INCY;
  }
}

void
Lapack128::
GETRF (const int M, const int N, __float128 A[],
       const int LDA, int IPIV[], int* INFO) const
{
  //std::cerr << "GETRF: N = " << N << std::endl;

  Teuchos::BLAS<int, __float128> blas;

  // NOTE (mfh 05 Sep 2015) This is a direct translation of LAPACK
  // 3.5.0's DGETF2 routine.  LAPACK is under a BSD license.

  *INFO = 0;
  if (M < 0) {
    *INFO = -1;
  } else if (N < 0) {
    *INFO = -2;
  } else if (LDA < std::max (1, M)) {
    *INFO = -4;
  }
  if (*INFO != 0) {
    return;
  }

  // Quick return if possible
  if (M == 0 || N == 0) {
    return;
  }

  // Compute machine safe minimum sfmin (such that 1/sfmin does
  // not overflow).  LAPACK 3.1 just returns for this the smallest
  // normalized number.
  const __float128 sfmin = FLT128_MIN;
  const __float128 zero = 0.0;
  const __float128 one = 1.0;

  const int j_upperBound = std::min (M, N);
  for (int j = 1; j <= j_upperBound; ++j) {
    //std::cerr << "  j = " << j << std::endl;

    // Find pivot and test for singularity.
    const __float128* const A_jj = A + j*LDA + j;

    //std::cerr << "  CALLING IAMAX" << std::endl;
    const int jp = j - 1 + blas.IAMAX (M - j + 1, A_jj, 1);
    IPIV[j] = jp;

    const __float128* A_jp_j = A + jp + LDA*j;
    if (*A_jp_j != zero) {
      // Apply the interchange to columns 1:N.
      __float128* const A_j1 = A + j + 1*LDA;
      __float128* const A_jp_1 = A + jp + 1*LDA;

      if (jp != j) {
        // mfh 05 Sep 2015: Teuchos::BLAS doesn't implement SWAP
        // (e.g., DSWAP), so I'll roll my own here.
        SWAP (N, A_j1, LDA, A_jp_1, LDA);
      }

      // Compute elements J+1:M of J-th column.
      if (j < M) {
        __float128* const A_jj = A + j + j*LDA;
        __float128* const A_j1_j = A + j*LDA + (j-1);

        if (fabsq (*A_jj) >= sfmin) {
          blas.SCAL (M-j, one / *A_jj, A_j1_j, 1);
        } else {
          for (int i = 1; i <= M-j; ++i) {
            A[j + i + j * LDA] /= *A_jj;
          }
        }
      }
    } else if (*INFO == 0) {
      *INFO = j;
    }

    if (j < std::min (M, N)) {
      //std::cerr << "  UPDATE TRAILING SUBMATRIX" << std::endl;

      // Update trailing submatrix.
      const __float128* A_j1_j = A + (j+1) + j*LDA;
      const __float128* A_j_j1 = A + j + (j+1)*LDA;
      __float128* A_j1_j1 = A + (j+1) + (j+1)*LDA;
      blas.GER (M-j, N-j, -one, A_j1_j, 1, A_j_j1, LDA, A_j1_j1, LDA);
    }
  }
}

void
Lapack128::
LASWP (const int N, __float128 A[], const int LDA, const int K1,
       const int K2, const int IPIV[], const int INCX) const
{
  int i, i1, i2, inc, ip, ix, ix0, j, k, n32;
  __float128 temp;

  if (INCX > 0) {
    ix0 = K1;
    i1 = K1;
    i2 = K2;
    inc = 1;
  } else if (INCX < 0) {
    ix0 = 1 + (1 - K2)*INCX;
    i1 = K2;
    i2 = K1;
    inc = -1;
  } else { // INCX == 0
    return;
  }

  // The LAPACK 3.5.0 source code does 32 entries at a time,
  // presumably for better vectorization or cache line usage.
  n32 = (N / 32) * 32;

  if (n32 != 0) {
    for (j = 1; j <= n32; j += 32) {
      ix = ix0;
      // C and C++ lack Fortran's convenient range specifier,
      // which can iterate over a range in either direction
      // without particular fuss about the end condition.
      for (i = i1; (inc > 0) ? (i <= i2) : (i >= i2); i += inc) {
        ip = IPIV[ix];
        if (ip != i) {
          for (k = j; k <= j+31; ++k) {
            temp = A[i + k*LDA]; //  temp = a( i, k )
            A[i + k*LDA] = A[ip + k*LDA]; // a( i, k ) = a( ip, k )
            A[ip + k*LDA] = temp; // a( ip, k ) = temp
          }
        }
        ix = ix + INCX;
      }
    }
  }

  if (n32 != N) {
    n32 = n32 + 1;
    ix = ix0;
    // C and C++ lack Fortran's convenient range specifier,
    // which can iterate over a range in either direction
    // without particular fuss about the end condition.
    for (i = i1; (inc > 0) ? (i <= i2) : (i >= i2); i += inc) {
      ip = IPIV[ix];
      if (ip != i) {
        for (k = n32; k <= N; ++k) {
          temp = A[i + k*LDA]; //  temp = a( i, k )
          A[i + k*LDA] = A[ip + k*LDA]; // a( i, k ) = a( ip, k )
          A[ip + k*LDA] = temp; // a( ip, k ) = temp
        }
      }
      ix = ix + INCX;
    }
  }

#if 0
  for (int j = 0; j < N; ++j) {
    if (INCX > 0) {
      int pivInd = K1 - 1;
      for (int i = K1 - 1; i < K2; ++i, pivInd += INCX) {
        const int ip = IPIV[pivInd];
        if (ip != i+1) {
          for (int k = 0; k < N; ++k) {
            __float128* const A_ik = A + i + k*LDA;
            __float128* const A_ip_k = A + ip + k*LDA;
            std::swap (*A_ik, *A_ip_k);
          }
        }
      }
    }
    else { // apply pivots in reverse order
      int pivInd = (K2-1) * INCX;
      for (int i = K2 - 1; i >= K1 - 1; --i, pivInd -= INCX) {
        const int ip = IPIV[pivInd];
        if (ip != i+1) {
          for (int k = 0; k < N; ++k) {
            __float128* const A_ik = A + i + k*LDA;
            __float128* const A_ip_k = A + ip + k*LDA;
            std::swap (*A_ik, *A_ip_k);
          }
        }
      }
    }
  }
#endif // 0
}

void
Lapack128::
GETRI (const int /* N */, __float128 /* A */ [], const int /* LDA */,
       int /* IPIV */ [], __float128 /* WORK */ [], const int /* LWORK */,
       int* /* INFO */) const
{
  TEUCHOS_TEST_FOR_EXCEPTION
    (true, std::logic_error, "Lapack128::GETRI: Not implemented yet.");
}


void
Lapack128::
GETRS (const char TRANS, const int N, const int NRHS,
       const __float128 A[], const int LDA, const int IPIV[],
       __float128 B[], const int LDB, int* INFO) const
{
  //std::cerr << "GETRS: N = " << N << std::endl;

  Teuchos::BLAS<int, __float128> blas;

  // NOTE (mfh 05 Sep 2015) This is a direct translation of LAPACK
  // 3.5.0's DGETRS routine.  LAPACK is under a BSD license.

  *INFO = 0;
  const bool notran = (TRANS == 'N' || TRANS == 'n');
  if (! notran
      && ! (TRANS == 'T' || TRANS == 't')
      && ! (TRANS == 'C' || TRANS == 'c')) {
    *INFO = -1; // invalid TRANS argument
  }
  else if (N < 0) {
    *INFO = -2; // invalid N (negative)
  }
  else if (NRHS < 0) {
    *INFO = -3; // invalid NRHS (negative)
  }
  else if (LDA < std::max (1, N)) {
    *INFO = -5; // invalid LDA (too small)
  }
  else if (LDB < std::max (1, N)) {
    *INFO = -8; // invalid LDB (too small)
  }
  if (*INFO != 0) {
    return;
  }

  const __float128 one = 1.0;

  using Teuchos::LEFT_SIDE;
  using Teuchos::LOWER_TRI;
  using Teuchos::UPPER_TRI;
  using Teuchos::NO_TRANS;
  using Teuchos::TRANS;
  using Teuchos::CONJ_TRANS;
  using Teuchos::UNIT_DIAG;
  using Teuchos::NON_UNIT_DIAG;

  if (notran) { // No transpose; solve AX=B
    // Apply row interchanges to the right-hand sides.
    //std::cerr << "CALLING LASWP" << std::endl;
    LASWP (NRHS, B, LDB, 1, N, IPIV, 1);
    // Solve L*X = B, overwriting B with X.
    //std::cerr << "CALLING TRSM (1)" << std::endl;
    blas.TRSM (LEFT_SIDE, LOWER_TRI, NO_TRANS, UNIT_DIAG, N, NRHS,
               one, A, LDA, B, LDB);
    // Solve U*X = B, overwriting B with X.
    //std::cerr << "CALLING TRSM (2)" << std::endl;
    blas.TRSM (LEFT_SIDE, UPPER_TRI, NO_TRANS, NON_UNIT_DIAG, N, NRHS,
               one, A, LDA, B, LDB);
  }
  else { // Transpose or conjugate transpose: solve A^{T,H}X = B.
    const Teuchos::ETransp transposeMode = (TRANS == 'T' || TRANS == 't') ?
      TRANS : CONJ_TRANS;

    // Solve U^{T,H}*X = B, overwriting B with X.
    //std::cerr << "CALLING TRSM (1)" << std::endl;
    blas.TRSM (LEFT_SIDE, UPPER_TRI, transposeMode, NON_UNIT_DIAG, N, NRHS,
               one, A, LDA, B, LDB);
    // Solve L^{T,H}*X = B, overwriting B with X.
    //std::cerr << "CALLING TRSM (2)" << std::endl;
    blas.TRSM (LEFT_SIDE, LOWER_TRI, transposeMode, UNIT_DIAG, N, NRHS,
               one, A, LDA, B, LDB);
    //std::cerr << "CALLING LASWP" << std::endl;
    // Apply row interchanges to the solution vectors.
    LASWP (NRHS, B, LDB, 1, N, IPIV, -1);
  }

  //std::cerr << "DONE WITH GETRS" << std::endl;
}

} // namespace Details
} // namespace Tpetra

#endif // HAVE_TPETRA_INST_FLOAT128

