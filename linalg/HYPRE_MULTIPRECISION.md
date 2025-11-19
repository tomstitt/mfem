# HYPRE Multi-Precision Support in MFEM

This document describes MFEM's support for HYPRE's multi-precision functionality.

## Overview

HYPRE's multi-precision support (available when HYPRE is built with `--enable-mixed-precision`) allows using different precision levels for various components of a solver. This can significantly improve performance while maintaining accuracy in the overall solve.

## Key Features

### Mixed-Precision Solvers

The primary use case is **mixed-precision preconditioning**: using a lower-precision preconditioner (e.g., single precision) within a higher-precision Krylov solver (e.g., double precision). This can reduce memory usage and improve performance for preconditioner operations while the outer solver maintains full precision.

## Requirements

- HYPRE must be built with `--enable-mixed-precision`
- MFEM's `real_t` type determines the outer solver precision
- The HYPRE version must support multi-precision (typically HYPRE >= 2.20.0)

## API Usage

### HypreBoomerAMG

Set the precision for BoomerAMG preconditioner operations:

```cpp
#ifdef HYPRE_MIXED_PRECISION
   // Create a BoomerAMG preconditioner
   HypreBoomerAMG amg(A);

   // Set to use single precision for AMG operations
   // 0 = double precision (default)
   // 1 = single precision
   amg.SetPrecision(1);
#endif
```

### Why No ConvertTo* Methods for Vectors/Matrices?

**Important:** MFEM does NOT provide `ConvertToSingle()/ConvertToDouble()` methods for
`HypreParVector` and `HypreParMatrix` because:

1. **Memory Safety**: HYPRE's conversion functions reallocate internal data arrays
2. **Ownership Conflicts**: MFEM often manages or aliases the memory, making conversion unsafe
3. **Dangling Pointers**: Conversion would invalidate MFEM's pointers to HYPRE data
4. **Not the Primary Use Case**: Mixed-precision is designed for preconditioners, not data conversion

The safe and recommended approach is to set precision **before** HYPRE allocates memory,
which is exactly what `HypreBoomerAMG::SetPrecision()` does.

## Example: Mixed-Precision PCG Solve

```cpp
// Assume we have a HypreParMatrix A and vectors b, x
// MFEM is built with double precision (real_t = double)

#ifdef HYPRE_MIXED_PRECISION
   // Create BoomerAMG preconditioner in single precision
   HypreBoomerAMG M(A);
   M.SetPrecision(1);  // Use single precision for AMG

   // Create double-precision PCG solver
   HyprePCG pcg(A);
   pcg.SetPreconditioner(M);
   pcg.SetTol(1e-6);
   pcg.SetMaxIter(200);

   // Solve: outer iterations in double, preconditioner in single
   pcg.Mult(b, x);

   // The preconditioner runs in single precision, saving memory
   // and improving performance, while the Krylov solver maintains
   // double precision accuracy
#else
   // Fallback for when HYPRE doesn't have mixed-precision support
   HypreBoomerAMG M(A);
   HyprePCG pcg(A);
   pcg.SetPreconditioner(M);
   pcg.Mult(b, x);
#endif
```

## Performance Considerations

1. **Memory Savings**: Single precision uses half the memory of double precision
2. **Computational Speed**: Single precision operations are often faster on modern hardware
3. **Accuracy Trade-off**: The overall solve accuracy is primarily determined by the outer Krylov solver precision
4. **Best Use Cases**: Large-scale problems where the preconditioner dominates memory/time

## Compatibility

- The multi-precision API is only available when `HYPRE_MIXED_PRECISION` is defined
- Code using these features should be wrapped in `#ifdef HYPRE_MIXED_PRECISION` blocks
- The API gracefully degrades when HYPRE is not built with multi-precision support

## References

- [HYPRE Multi-Precision Documentation](https://hypre.readthedocs.io/en/latest/ch-mprecision.html)
- HYPRE CHANGELOG entry for multi-precision support
- "Mixed-Precision Iterative Refinement" techniques in numerical linear algebra

## Implementation Notes

The MFEM multi-precision support provides a safe wrapper around HYPRE's multi-precision API:
- `HYPRE_BoomerAMGSetPrecision()` - Safe: sets precision before memory allocation

**Not Exposed** (due to memory safety concerns with MFEM's memory management):
- `HYPRE_ParVectorConvertToSingle()` / `HYPRE_ParVectorConvertToDouble()`
- `HYPRE_ParCSRMatrixConvertToSingle()` / `HYPRE_ParCSRMatrixConvertToDouble()`

These conversion functions reallocate HYPRE's internal memory, which would invalidate
MFEM's pointers and violate ownership assumptions. The recommended approach is to set
precision before HYPRE creates its internal data structures, not convert them afterward.
