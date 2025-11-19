# How Lambda Reducer Detection Works at the LLVM IR Level

## Example C++ Code

```cpp
#include "general/reducers.hpp"
#include "general/forall.hpp"

void compute(double* data, int N) {
  mfem::SumReducer<double> sum;
  sum.val = 0.0;

  mfem::forall(N, [=](int i) {
    sum.val += data[i];  // sum is captured by value
  });
}
```

## Corresponding LLVM IR (Simplified)

```llvm
; The lambda type is generated as an anonymous struct
; with members for each captured variable
%class.anon = type {
  %"struct.mfem::SumReducer"*,  ; Captured: sum (by value, so pointer to it)
  double**                       ; Captured: data
}

; The SumReducer type definition
%"struct.mfem::SumReducer" = type {
  double  ; The 'val' member
}

; Your function
define void @_Z7computePdi(double* %data, i32 %N) {
entry:
  ; Allocate the reducer on the stack
  %sum = alloca %"struct.mfem::SumReducer", align 8

  ; Initialize sum.val = 0.0
  %val_ptr = getelementptr inbounds %"struct.mfem::SumReducer",
                           %"struct.mfem::SumReducer"* %sum, i32 0, i32 0
  store double 0.0, double* %val_ptr

  ; Create the lambda closure object
  %lambda = alloca %class.anon, align 8

  ; Store captured 'sum' into lambda
  %lambda.sum = getelementptr inbounds %class.anon,
                               %class.anon* %lambda, i32 0, i32 0
  store %"struct.mfem::SumReducer"* %sum,
        %"struct.mfem::SumReducer"** %lambda.sum

  ; Store captured 'data' into lambda
  %lambda.data = getelementptr inbounds %class.anon,
                                %class.anon* %lambda, i32 0, i32 1
  store double** %data, double*** %lambda.data

  ; Call forall with the lambda
  call void @_ZN4mfem6forallIZ7computePdiE3$_0EEviT_(
    i32 %N,
    %class.anon* %lambda  ; <-- Pass the lambda struct
  )

  ret void
}

; The forall function template instantiation
define void @_ZN4mfem6forallIZ7computePdiE3$_0EEviT_(
    i32 %N,
    %class.anon* %lambda) {  ; <-- Lambda struct parameter
  ; ... forall implementation ...
}
```

## What the LLVM Pass Does

### Step 1: Find forall Calls
```llvm
call void @_ZN4mfem6forallIZ7computePdiE3$_0EEviT_(...)
             ^^^^^^^^ Contains "forall" - this is a match!
```

### Step 2: Examine Arguments
```llvm
%class.anon* %lambda
^^^^^^^^^^^^^
This is a struct type - likely a lambda!
```

### Step 3: Inspect the Struct Type
```llvm
%class.anon = type {
  %"struct.mfem::SumReducer"*,  ; <-- Field 0
  ^^^^^^^^^^^^^^^^^^^^^^^^^
  Check this type name!

  double**                      ; <-- Field 1
}
```

### Step 4: Check Type Names
```cpp
// In the LLVM pass:
StringRef typeName = StructTy->getName();
// typeName = "struct.mfem::SumReducer"

if (typeName.contains("mfem") && typeName.contains("SumReducer")) {
  // MATCH! Found a reducer!
  return true;
}
```

## More Complex Example: Nested Reducers

```cpp
struct Context {
  RAJA::ReduceSum<double> sum;
  RAJA::ReduceMin<double> min;
  double* data;
};

void compute(double* data, int N) {
  Context ctx;
  ctx.sum.val = 0.0;
  ctx.min.val = HUGE_VAL;
  ctx.data = data;

  mfem::forall(N, [=](int i) {
    ctx.sum += data[i];
    ctx.min.min(data[i]);
  });
}
```

### LLVM IR:
```llvm
; Lambda type
%class.anon.0 = type {
  %struct.Context*  ; <-- Lambda captures the Context
}

; Context type (nested)
%struct.Context = type {
  %"class.RAJA::ReduceSum",  ; <-- Field 0: RAJA Reducer!
  %"class.RAJA::ReduceMin",  ; <-- Field 1: Another RAJA Reducer!
  double*                     ; <-- Field 2: data
}

; RAJA::ReduceSum type
%"class.RAJA::ReduceSum" = type {
  double,  ; val
  ; ... other RAJA internal fields ...
}
```

### Pass Detection Logic:
```
1. Find forall call
2. See lambda type: %class.anon.0
3. Inspect %class.anon.0:
   - Field 0: %struct.Context* (a struct!)
4. Recursively inspect %struct.Context:
   - Field 0: %"class.RAJA::ReduceSum"
     ✓ Name contains "RAJA" and "Reduce" → MATCH!
   - Field 1: %"class.RAJA::ReduceMin"
     ✓ Name contains "RAJA" and "Reduce" → MATCH!
```

## Real LLVM IR Example Output

To see the actual LLVM IR from your code:

```bash
# Compile to LLVM IR
clang++ -S -emit-llvm -O1 test_reducer.cpp -o test_reducer.ll

# View the IR
less test_reducer.ll

# Search for your lambda types
grep "class.anon" test_reducer.ll
grep "Reducer" test_reducer.ll
```

You'll see output like:
```llvm
%class.anon = type { %"class.RAJA::Reducer"*, double** }
%"class.RAJA::Reducer" = type { double, i32, i8* }
```

This is the **actual type information** the pass analyzes!

## Key Insights

1. **Lambdas are just structs** - No magic, just compiler-generated classes
2. **Type names are preserved** - LLVM IR keeps the original C++ type names (mangled but readable)
3. **Recursive inspection works** - We can traverse nested struct types
4. **No reflection needed** - All information is in the IR already!

## Comparison with C++ Reflection (Future C++29)

### With LLVM Pass (Available Now):
```cpp
// Happens at compile time in LLVM IR
✓ Works today
✓ Sees actual compiled types
✓ Can integrate into compilation pipeline
✗ Requires separate LLVM pass
```

### With C++ Reflection (Future):
```cpp
// Would be possible in C++ source:
template<typename Lambda>
void forall(int N, Lambda&& body) {
  constexpr auto members = ^Lambda::members();  // C++29 reflection

  for (auto member : members) {
    if (is_reducer_type(member.type())) {
      // Compile-time error or warning
      static_assert(false, "Lambda captures reducer!");
    }
  }

  // ... rest of forall
}
```

**But** reflection won't be in C++ until 2029+ and won't be widely available for years after!

The LLVM pass gives you this capability **today**!

## Performance Impact

Running the pass has **zero runtime overhead**:
- Runs during compilation only
- No code generation changes
- Just analysis and reporting
- Can be enabled/disabled with a flag

## Try It Yourself

```bash
cd llvm-passes
make test
```

You'll see the pass detect reducers in the test cases!
