# RAJA/MFEM Reducer Detection LLVM Pass

This directory contains an LLVM pass that detects whether lambdas passed to `forall` routines capture `RAJA::Reducer` or MFEM reducer types.

## Why This Works (No Reflection Needed!)

C++ lambdas are compiled to anonymous structs/classes where:
- Each captured variable becomes a **member field** of the struct
- The lambda's `operator()` becomes a member function
- The type information is **fully preserved** in LLVM IR

At the LLVM IR level, we can:
1. Identify calls to `forall` functions
2. Examine the lambda argument's type (a struct)
3. Inspect the struct's member types recursively
4. Check if any members are or contain `Reducer` types

**No C++ reflection required** - LLVM IR already has all the type information!

## How Lambdas Look in LLVM IR

When you write:
```cpp
RAJA::Reducer<double> sum{0.0};
mfem::forall(100, [=](int i) {
  sum += data[i];
});
```

The compiler generates something like:
```llvm
%class.anon = type { %"class.RAJA::Reducer"*, double* }
; Lambda struct with two captures: ^^^^^^^^^^^^^^^^^  ^^^^^^^
;                                   RAJA::Reducer*    data*

call void @_Z6forall(%class.anon %lambda)
```

The pass examines `%class.anon` and sees it contains a `RAJA::Reducer` member!

## Building the Pass

### Prerequisites
- LLVM 13+ (14, 15, 16, 17, 18 all work)
- Clang compiler
- `llvm-config` in your PATH

### Build Steps

```bash
cd llvm-passes
make
```

This creates `ReducerDetectionPass.so`.

## Usage

### Option 1: Run on Test Code

```bash
make test
```

This will:
1. Compile `test_reducer.cpp` to LLVM bitcode
2. Run the pass on it
3. Print detection results

### Option 2: Run on Your Own Code

```bash
# Generate LLVM IR from your code
clang++ -S -emit-llvm -O1 your_code.cpp -o your_code.ll

# Or generate bitcode
clang++ -c -emit-llvm -O1 your_code.cpp -o your_code.bc

# Run the pass
opt -load-pass-plugin=./ReducerDetectionPass.so \
    -passes=reducer-detection \
    -disable-output your_code.bc
```

### Option 3: Integrate into MFEM Build

You can run this pass as part of your compilation pipeline:

```bash
clang++ -O2 -Xclang -load -Xclang ./ReducerDetectionPass.so \
        -mllvm -print-after-all \
        your_code.cpp -o your_binary
```

## What It Detects

The pass detects the following reducer types:

### RAJA Reducers
- `RAJA::Reducer<T>`
- `RAJA::ReduceSum<T>`
- `RAJA::ReduceMin<T>`
- `RAJA::ReduceMax<T>`
- etc.

### MFEM Reducers (from `general/reducers.hpp`)
- `mfem::SumReducer<T>`
- `mfem::MinReducer<T>`
- `mfem::MaxReducer<T>`
- `mfem::MultReducer<T>`
- `mfem::BAndReducer<T>`
- `mfem::BOrReducer<T>`
- `mfem::MinMaxReducer<T>`
- `mfem::ArgMinReducer<T, I>`
- `mfem::ArgMaxReducer<T, I>`
- `mfem::ArgMinMaxReducer<T, I>`

### Nested Detection
The pass **recursively** checks:
- Direct captures: `[reducer]`
- Nested in structs: `[ctx]` where `ctx.reducer` exists
- Pointers to reducers: `[ptr]` where `*ptr` is a reducer
- Arrays of reducers

## Example Output

```
=== RAJA/MFEM Reducer Detection Pass ===

Found forall call in function: test_raja_reducer
  Analyzing argument 1: %class.anon*
    Lambda struct type: class.anon
      Found RAJA::Reducer: class.RAJA::ReduceSum
    *** REDUCER DETECTED in lambda capture! ***

Found forall call in function: test_no_reducer
  Analyzing argument 1: %class.anon.0*
    Lambda struct type: class.anon.0
```

## Advanced: Customizing the Pass

You can modify `ReducerDetectionPass.cpp` to:

1. **Emit warnings/errors** instead of just printing
2. **Collect statistics** about reducer usage
3. **Check for specific patterns** (e.g., multiple reducers in one lambda)
4. **Suggest optimizations** (e.g., use `RAJA::atomic` instead)
5. **Integrate with compiler diagnostics**

### Example: Making it a Compiler Error

Add this to the pass:
```cpp
if (structContainsReducer(StructTy)) {
  LLVMContext &Ctx = Call->getContext();
  DiagnosticInfoUnsupported Diag(
    *Call->getFunction(),
    "Lambda captures RAJA::Reducer - this may cause race conditions!",
    Call->getDebugLoc()
  );
  Ctx.diagnose(Diag);
}
```

## Technical Details

### Why Reducers in Lambdas Can Be Problematic

RAJA reducers often use thread-local storage and synchronization. When captured by value in a lambda:
1. Each GPU thread may get a **copy** of the reducer
2. Updates to the copy don't propagate back
3. Final reduction may not occur correctly

### Proper Usage Pattern

```cpp
// WRONG: Reducer captured by value
RAJA::Reducer<double> sum{0.0};
forall(N, [=](int i) { sum += data[i]; });  // ❌ Copy!

// CORRECT: Use RAJA's reduction pattern
RAJA::ReduceSum<double> sum(0.0);
RAJA::forall<RAJA::cuda_exec<256>>(
  RAJA::RangeSegment(0, N),
  [=](int i) { sum += data[i]; }  // ✓ Special RAJA handling
);
```

The pass helps you catch the problematic pattern!

## Debugging

### View LLVM IR
```bash
make view-ir
```

### Enable Verbose Output
Modify the pass to print more details:
```cpp
errs() << "  Field " << i << " type: ";
ElementType->print(errs());
errs() << "\n";
```

### Check Type Names
Add to `structContainsReducerImpl`:
```cpp
errs() << "    Checking type: " << typeName << "\n";
```

## Integration with CI/CD

You can run this as part of your continuous integration:

```bash
#!/bin/bash
# check_reducers.sh

find . -name "*.cpp" | while read file; do
  clang++ -c -emit-llvm -O1 "$file" -o /tmp/temp.bc 2>/dev/null
  if opt -load-pass-plugin=./ReducerDetectionPass.so \
         -passes=reducer-detection \
         -disable-output /tmp/temp.bc 2>&1 | grep -q "REDUCER DETECTED"; then
    echo "WARNING: $file contains reducer in forall lambda"
  fi
done
```

## Further Reading

- [LLVM Pass Writing Guide](https://llvm.org/docs/WritingAnLLVMPass.html)
- [RAJA Performance Portability](https://raja.readthedocs.io/)
- [LLVM IR Type System](https://llvm.org/docs/LangRef.html#type-system)
- [Lambda Lowering in Clang](https://clang.llvm.org/docs/ItaniumMangleAbiTags.html)

## License

Same as MFEM (BSD-3)
