// Test case to demonstrate reducer detection
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

template<typename BODY>
void forall(int N, BODY &&body) {
  for(int i = 0; i < N; i++) {
    body(i);
  }
}
}

// Test 1: Lambda that captures a RAJA::Reducer
void test_raja_reducer() {
  std::cout << "Test 1: RAJA Reducer\n";

  RAJA::Reducer<double> sum{0.0};
  double* data = new double[100];

  mfem::forall(100, [=](int i) {
    sum += data[i];  // sum is captured by value in the lambda
  });

  delete[] data;
}

// Test 2: Lambda that captures mfem::SumReducer
void test_mfem_reducer() {
  std::cout << "Test 2: mfem Reducer\n";

  mfem::SumReducer<double> reducer;
  reducer.val = 0.0;
  double* data = new double[100];

  mfem::forall(100, [=](int i) {
    reducer.val += data[i];  // reducer is captured
  });

  delete[] data;
}

// Test 3: Lambda with no reducer (should not be flagged)
void test_no_reducer() {
  std::cout << "Test 3: No Reducer\n";

  double sum = 0.0;
  double* data = new double[100];

  mfem::forall(100, [=](int i) {
    // Just access data, no reducer
    volatile double x = data[i];
  });

  delete[] data;
}

// Test 4: Nested struct containing reducer
struct ComputeContext {
  RAJA::Reducer<double> sum;
  double* data;
  int size;
};

void test_nested_reducer() {
  std::cout << "Test 4: Nested Reducer in struct\n";

  ComputeContext ctx;
  ctx.sum.value = 0.0;
  ctx.data = new double[100];
  ctx.size = 100;

  mfem::forall(100, [=](int i) {
    ctx.sum += ctx.data[i];  // ctx contains a reducer
  });

  delete[] ctx.data;
}

int main() {
  test_raja_reducer();
  test_mfem_reducer();
  test_no_reducer();
  test_nested_reducer();
  return 0;
}
