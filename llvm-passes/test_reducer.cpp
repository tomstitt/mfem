// Test case to demonstrate reducer detection with annotations
#include <iostream>

// Mock RAJA::Reducer for testing
namespace RAJA {
template<typename T>
struct ReduceSum {
  T value;
  void operator+=(T v) { value += v; }
};

template<typename T>
using Reducer = ReduceSum<T>;
}

// Mock mfem types
namespace mfem {
template<typename T>
struct SumReducer {
  using value_type = T;
  T val;
};

// Regular forall - NOT checked by the pass
template<typename BODY>
void forall(int N, BODY &&body) {
  for(int i = 0; i < N; i++) {
    body(i);
  }
}

// Annotated forall - WILL be checked by the pass
template<typename BODY>
[[clang::annotate("check_reducer")]]
void forall_checked(int N, BODY &&body) {
  for(int i = 0; i < N; i++) {
    body(i);
  }
}
}

// Test 1: UNCHECKED forall with RAJA::Reducer (will NOT be detected)
void test_unchecked_raja_reducer() {
  std::cout << "Test 1: Unchecked forall with RAJA Reducer (no annotation)\n";

  RAJA::Reducer<double> sum{0.0};
  double* data = new double[100];

  // Using regular forall - pass won't check this
  mfem::forall(100, [=](int i) {
    sum += data[i];
  });

  delete[] data;
}

// Test 2: CHECKED forall with RAJA::Reducer (WILL be detected!)
void test_checked_raja_reducer() {
  std::cout << "Test 2: Checked forall with RAJA Reducer (annotated)\n";

  RAJA::Reducer<double> sum{0.0};
  double* data = new double[100];

  // Using annotated forall_checked - pass WILL check this!
  mfem::forall_checked(100, [=](int i) {
    sum += data[i];  // *** SHOULD BE DETECTED ***
  });

  delete[] data;
}

// Test 3: CHECKED forall with mfem::SumReducer (WILL be detected!)
void test_checked_mfem_reducer() {
  std::cout << "Test 3: Checked forall with mfem Reducer (annotated)\n";

  mfem::SumReducer<double> reducer;
  reducer.val = 0.0;
  double* data = new double[100];

  // Using annotated forall_checked
  mfem::forall_checked(100, [=](int i) {
    reducer.val += data[i];  // *** SHOULD BE DETECTED ***
  });

  delete[] data;
}

// Test 4: CHECKED forall with NO reducer (should NOT be flagged)
void test_checked_no_reducer() {
  std::cout << "Test 4: Checked forall with no reducer (clean)\n";

  double sum = 0.0;
  double* data = new double[100];

  // Using annotated forall_checked, but no reducer in capture
  mfem::forall_checked(100, [=](int i) {
    volatile double x = data[i];  // No reducer - OK!
  });

  delete[] data;
}

// Test 5: CHECKED forall with nested reducer struct (WILL be detected!)
struct ComputeContext {
  RAJA::Reducer<double> sum;
  double* data;
  int size;
};

void test_checked_nested_reducer() {
  std::cout << "Test 5: Checked forall with nested reducer (annotated)\n";

  ComputeContext ctx;
  ctx.sum.value = 0.0;
  ctx.data = new double[100];
  ctx.size = 100;

  // Using annotated forall_checked with nested reducer
  mfem::forall_checked(100, [=](int i) {
    ctx.sum += ctx.data[i];  // *** SHOULD BE DETECTED ***
  });

  delete[] ctx.data;
}

// Test 6: UNCHECKED forall with nested reducer (will NOT be detected)
void test_unchecked_nested_reducer() {
  std::cout << "Test 6: Unchecked forall with nested reducer (no annotation)\n";

  ComputeContext ctx;
  ctx.sum.value = 0.0;
  ctx.data = new double[100];
  ctx.size = 100;

  // Using regular forall - pass won't check this
  mfem::forall(100, [=](int i) {
    ctx.sum += ctx.data[i];
  });

  delete[] ctx.data;
}

int main() {
  std::cout << "=== Running Reducer Detection Tests ===\n\n";

  // Unchecked tests (won't be analyzed by pass)
  test_unchecked_raja_reducer();
  test_unchecked_nested_reducer();

  std::cout << "\n";

  // Checked tests (WILL be analyzed by pass)
  test_checked_raja_reducer();
  test_checked_mfem_reducer();
  test_checked_no_reducer();
  test_checked_nested_reducer();

  std::cout << "\n=== Tests Complete ===\n";
  return 0;
}
