// Example: How to annotate mfem::forall functions for reducer detection
//
// This shows how to add [[clang::annotate("check_reducer")]] to your
// existing forall implementations in general/forall.hpp

#ifndef EXAMPLE_ANNOTATED_FORALL_HPP
#define EXAMPLE_ANNOTATED_FORALL_HPP

namespace mfem {

// ============================================================================
// OPTION 1: Annotate specific template overloads you want to check
// ============================================================================

// Example: Annotate the 1D forall wrapper
template<typename lambda>
[[clang::annotate("check_reducer")]]  // <-- Add this annotation
inline void forall(int N, lambda &&body) {
  ForallWrap<1>(true, N, body);
}

// Example: Annotate the 2D forall wrapper
template<typename lambda>
[[clang::annotate("check_reducer")]]  // <-- Add this annotation
inline void forall(int Nx, int Ny, lambda &&body) {
  // ... implementation ...
}

// Example: Annotate the 3D forall wrapper
template<typename lambda>
[[clang::annotate("check_reducer")]]  // <-- Add this annotation
inline void forall(int Nx, int Ny, int Nz, lambda &&body) {
  // ... implementation ...
}

// ============================================================================
// OPTION 2: Create separate "checked" versions for specific use cases
// ============================================================================

// Keep the regular forall unchecked
template<typename lambda>
inline void forall(int N, lambda &&body) {
  ForallWrap<1>(true, N, body);
}

// Create an annotated version for critical sections
template<typename lambda>
[[clang::annotate("check_reducer")]]
inline void forall_strict(int N, lambda &&body) {
  ForallWrap<1>(true, N, body);
}

// ============================================================================
// OPTION 3: Annotate specific backend wrappers
// ============================================================================

// Only check RAJA CUDA backend
template <const int BLOCKS = MFEM_CUDA_BLOCKS, typename DBODY>
[[clang::annotate("check_reducer")]]
void RajaCuWrap1D(const int N, DBODY &&d_body) {
  RAJA::forall<RAJA::cuda_exec<BLOCKS,true>>(RAJA::RangeSegment(0,N),d_body);
}

// Only check HIP backend
template <const int BLOCKS = MFEM_HIP_BLOCKS, typename DBODY>
[[clang::annotate("check_reducer")]]
void RajaHipWrap1D(const int N, DBODY &&d_body) {
  RAJA::forall<RAJA::hip_exec<BLOCKS,true>>(RAJA::RangeSegment(0,N),d_body);
}

// ============================================================================
// OPTION 4: Conditional annotation based on device
// ============================================================================

// You can even combine with macros for conditional checking
#if defined(MFEM_USE_CUDA) || defined(MFEM_USE_HIP)
  #define MFEM_CHECK_REDUCER [[clang::annotate("check_reducer")]]
#else
  #define MFEM_CHECK_REDUCER
#endif

template<typename lambda>
MFEM_CHECK_REDUCER  // Only check on GPU builds
inline void forall_device(int N, lambda &&body) {
  ForallWrap<1>(true, N, body);
}

} // namespace mfem

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

namespace example {

void good_usage() {
  double* data = nullptr;
  int N = 100;

  // This is fine - no reducer captured
  mfem::forall(N, [=](int i) {
    data[i] = i * 2.0;
  });
}

void bad_usage_will_be_caught() {
  double* data = nullptr;
  int N = 100;

  // BAD: This will be detected if forall is annotated!
  mfem::SumReducer<double> sum;

  mfem::forall(N, [=](int i) {
    sum.val += data[i];  // *** PASS WILL FLAG THIS ***
  });
  // Error: Lambda captures mfem::SumReducer in annotated forall!
}

void correct_pattern() {
  double* data = nullptr;
  int N = 100;

  // CORRECT: Use mfem::reduce instead
  mfem::SumReducer<double> sum;
  sum.val = 0.0;

  Array<double> workspace;
  reduce(N, sum.val,
         [=](int i, double& local_sum) {
           local_sum += data[i];
         },
         sum, true, workspace);
}

} // namespace example

// ============================================================================
// PRACTICAL RECOMMENDATION FOR MFEM
// ============================================================================

/*
For the actual mfem codebase, I recommend:

1. Add annotations to GPU-specific forall wrappers only:
   - RajaCuWrap1D, RajaCuWrap2D, RajaCuWrap3D
   - RajaHipWrap1D, RajaHipWrap2D, RajaHipWrap3D
   - CuWrap1D, CuWrap2D, CuWrap3D
   - HipWrap1D, HipWrap2D, HipWrap3D

   These are where reducer captures cause the most problems (race conditions).

2. Keep CPU backends (OpenMP, serial) unchecked:
   - OmpWrap, RajaOmpWrap, RajaSeqWrap
   - These might work fine with reducers in some cases

3. Add to your general/forall.hpp:

   #ifdef MFEM_CHECK_REDUCER_CAPTURES
     #define MFEM_GPU_CHECK_REDUCER [[clang::annotate("check_reducer")]]
   #else
     #define MFEM_GPU_CHECK_REDUCER
   #endif

   Then annotate GPU wrappers:

   template <typename DBODY>
   MFEM_GPU_CHECK_REDUCER
   void CuWrap1D(const int N, DBODY &&d_body) {
     // ...
   }

4. Enable checking in your CMakeLists.txt or Makefile:

   add_compile_options(-DMFEM_CHECK_REDUCER_CAPTURES)

   This gives you opt-in checking without changing behavior by default.
*/

#endif // EXAMPLE_ANNOTATED_FORALL_HPP
