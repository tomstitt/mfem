# Quick Reference: Annotation-Based Reducer Detection

## TL;DR

Add `[[clang::annotate("check_reducer")]]` to forall functions you want to check:

```cpp
template<typename BODY>
[[clang::annotate("check_reducer")]]  // ← Add this line
void my_forall(int N, BODY &&body) {
  // implementation
}
```

## Complete Example

```cpp
namespace mfem {

// ============================================================================
// Annotate GPU backends where reducer captures cause race conditions
// ============================================================================

#if defined(MFEM_USE_CUDA)
template <const int BLCK, typename DBODY>
[[clang::annotate("check_reducer")]]  // ← Check CUDA forall
void CuWrap1D(const int N, DBODY &&d_body) {
  if (N==0) { return; }
  const int GRID = (N+BLCK-1)/BLCK;
  CuKernel1D<<<GRID,BLCK>>>(N, d_body);
  MFEM_GPU_CHECK(cudaGetLastError());
}
#endif

#if defined(MFEM_USE_HIP)
template <const int BLCK, typename DBODY>
[[clang::annotate("check_reducer")]]  // ← Check HIP forall
void HipWrap1D(const int N, DBODY &&d_body) {
  if (N==0) { return; }
  const int GRID = (N+BLCK-1)/BLCK;
  hipLaunchKernelGGL(HipKernel1D,GRID,BLCK,0,nullptr,N,d_body);
  MFEM_GPU_CHECK(hipGetLastError());
}
#endif

// ============================================================================
// DON'T annotate CPU backends - reducers might be OK there
// ============================================================================

template <typename HBODY>
void OmpWrap(const int N, HBODY &&h_body) {  // ← No annotation
#ifdef MFEM_USE_OPENMP
  #pragma omp parallel for
  for (int k = 0; k < N; k++) {
    h_body(k);
  }
#endif
}

} // namespace mfem
```

## What Gets Checked

### ✅ Annotated forall - WILL be checked
```cpp
[[clang::annotate("check_reducer")]]
void forall_gpu(int N, auto &&body) { /*...*/ }

void test() {
  RAJA::Reducer<double> sum;
  forall_gpu(100, [=](int i) {
    sum += data[i];  // ← DETECTED! Pass will flag this
  });
}
```

### ❌ Regular forall - ignored
```cpp
void forall_cpu(int N, auto &&body) { /*...*/ }  // No annotation

void test() {
  RAJA::Reducer<double> sum;
  forall_cpu(100, [=](int i) {
    sum += data[i];  // ← Ignored, not checked
  });
}
```

## Using the Pass

### 1. Build the pass
```bash
cd llvm-passes
make
```

### 2. Compile your code to LLVM IR
```bash
clang++ -c -emit-llvm -O1 your_code.cpp -o your_code.bc
```

### 3. Run the pass
```bash
opt -load-pass-plugin=./ReducerDetectionPass.so \
    -passes=reducer-detection \
    -disable-output your_code.bc
```

### Output example:
```
=== RAJA/MFEM Reducer Detection Pass ===
Only checking annotated forall functions with [[clang::annotate("check_reducer")]]

  Found annotated function: _ZN4mfem8CuWrap1D...
Found 1 annotated function(s) to check

Checking annotated forall: _ZN4mfem8CuWrap1D...
  Called from: _Z12compute_kernelv
  Analyzing argument 1: %class.anon*
    Lambda struct type: class.anon
      Found RAJA::Reducer: class.RAJA::ReduceSum
    *** REDUCER DETECTED in lambda capture! ***
```

## Recommended Strategy for MFEM

```cpp
// In general/forall.hpp:

// Define a macro to conditionally enable checking
#ifdef MFEM_CHECK_REDUCER_CAPTURES
  #define MFEM_GPU_CHECK_REDUCER [[clang::annotate("check_reducer")]]
#else
  #define MFEM_GPU_CHECK_REDUCER
#endif

// Apply to GPU wrappers only:
template <typename DBODY>
MFEM_GPU_CHECK_REDUCER  // Expands to annotation when enabled
void CuWrap1D(const int N, DBODY &&d_body) { /* ... */ }

template <typename DBODY>
MFEM_GPU_CHECK_REDUCER
void HipWrap1D(const int N, DBODY &&d_body) { /* ... */ }

// Leave CPU wrappers unchecked:
template <typename HBODY>
void OmpWrap(const int N, HBODY &&h_body) { /* ... */ }
```

Enable in CMakeLists.txt:
```cmake
option(MFEM_CHECK_REDUCER_CAPTURES "Enable LLVM reducer detection pass" OFF)

if(MFEM_CHECK_REDUCER_CAPTURES)
  add_compile_definitions(MFEM_CHECK_REDUCER_CAPTURES)
endif()
```

## Alternative: Multiple Forall Variants

Instead of annotating existing forall, create checked variants:

```cpp
// Keep existing forall as-is
template<typename BODY>
void forall(int N, BODY &&body) {
  ForallWrap<1>(true, N, body);
}

// Add strict variant for critical sections
template<typename BODY>
[[clang::annotate("check_reducer")]]
void forall_strict(int N, BODY &&body) {
  ForallWrap<1>(true, N, body);
}
```

Use `forall_strict` in performance-critical GPU kernels where you want to ensure no reducer captures.

## What Gets Detected

The pass detects these types in lambda captures:
- `RAJA::Reducer<T>`
- `RAJA::ReduceSum<T>`, `RAJA::ReduceMin<T>`, etc.
- `mfem::SumReducer<T>`
- `mfem::MinReducer<T>`, `mfem::MaxReducer<T>`
- Any type with "Reducer" in the name
- Nested structs containing reducers
- Pointers/references to reducers

## Common Patterns

### ❌ Bad: Reducer in capture (will be detected)
```cpp
RAJA::ReduceSum<double> sum;
forall_gpu(N, [=](int i) {
  sum += data[i];  // WRONG: reducer copied into lambda
});
```

### ✅ Good: Use mfem::reduce instead
```cpp
double sum = 0.0;
Array<double> workspace;
reduce(N, sum,
       [=](int i, double& local) {
         local += data[i];
       },
       SumReducer<double>(), true, workspace);
```

### ✅ Good: Manual reduction
```cpp
double* partial_sums = new double[N];
forall_gpu(N, [=](int i) {
  partial_sums[i] = data[i];  // No reducer
});
// Reduce partial_sums on host
```

## Integration with CI/CD

Check for reducers in your CI pipeline:

```bash
#!/bin/bash
# check_reducers.sh

find src/ -name "*.cpp" | while read file; do
  echo "Checking $file..."
  clang++ -c -emit-llvm -O1 -I. "$file" -o /tmp/temp.bc 2>/dev/null || continue

  if opt -load-pass-plugin=./llvm-passes/ReducerDetectionPass.so \
         -passes=reducer-detection \
         -disable-output /tmp/temp.bc 2>&1 | grep -q "REDUCER DETECTED"; then
    echo "ERROR: $file contains reducer in annotated forall lambda!"
    exit 1
  fi
done

echo "All checks passed!"
```

## Need Help?

- See `README.md` for detailed documentation
- See `how_to_annotate_forall.hpp` for annotation strategies
- See `LLVM_IR_EXAMPLE.md` for technical details on how it works
- Run `make test` to see the pass in action on test code
