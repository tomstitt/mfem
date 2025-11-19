// Example demonstrating HYPRE multi-precision support in MFEM
//
// This is a minimal example showing how to use mixed-precision solvers.
// Even when MFEM is compiled with double precision (real_t = double),
// you can use single-precision for the AMG preconditioner to save memory
// and improve performance.
//
// Compile:
//   Requires HYPRE built with --enable-mixed-precision
//   make ex1p (or your preferred example)
//
// Run:
//   mpirun -np 4 ex1p -m ../data/fichera.mesh -amg-sp

#include "mfem.hpp"
#include <iostream>

using namespace std;
using namespace mfem;

int main(int argc, char *argv[])
{
   // Initialize MPI and HYPRE
   Mpi::Init();
   Hypre::Init();

   int myid = Mpi::WorldRank();

   // Example 1: Mixed-precision BoomerAMG preconditioner
   // ===================================================
   // Use single-precision AMG within double-precision CG solver

#ifdef HYPRE_MIXED_PRECISION
   if (myid == 0)
   {
      cout << "\n=== Mixed-Precision Example ===\n" << endl;
      cout << "MFEM is using real_t = "
#ifdef MFEM_USE_DOUBLE
           << "double"
#else
           << "single"
#endif
           << " precision" << endl;
      cout << "HYPRE multi-precision support is AVAILABLE\n" << endl;
   }

   // Assume we have a HypreParMatrix A and vectors b, x
   // (In practice, these come from your FEM assembly)

   // Create BoomerAMG preconditioner with single precision
   // HypreBoomerAMG amg(A);
   // amg.SetPrecision(1);  // 1 = single, 0 = double (default)

   // Create double-precision CG solver
   // HyprePCG pcg(A);
   // pcg.SetPreconditioner(amg);
   // pcg.SetTol(1e-6);
   // pcg.SetMaxIter(200);
   // pcg.Mult(b, x);

   if (myid == 0)
   {
      cout << "Benefits of mixed-precision:" << endl;
      cout << "  - AMG hierarchy uses 50% less memory (single vs double)" << endl;
      cout << "  - AMG operations are faster on modern hardware" << endl;
      cout << "  - Outer CG solver maintains full double precision" << endl;
      cout << "  - Overall accuracy determined by outer solver tolerance" << endl;
      cout << "\nBest for: Large problems where AMG memory is significant\n" << endl;
   }

   // Example 2: Converting existing vectors/matrices
   // ===============================================

   // HypreParVector v(comm, global_size, col);
   // v.ConvertToSingle();   // Convert to single precision
   // ... use in single-precision operations ...
   // v.ConvertToDouble();   // Convert back to double

   // HypreParMatrix M(comm, ...);
   // M.ConvertToSingle();   // Convert to single precision
   // ... use in single-precision operations ...
   // M.ConvertToDouble();   // Convert back to double

   if (myid == 0)
   {
      cout << "\nSee examples/ex1p.cpp for a complete working example." << endl;
      cout << "Run: mpirun -np 4 ex1p -m ../data/fichera.mesh -amg-sp\n" << endl;
   }

#else
   if (myid == 0)
   {
      cout << "\n=== HYPRE Multi-Precision Not Available ===\n" << endl;
      cout << "HYPRE was not built with --enable-mixed-precision" << endl;
      cout << "\nTo enable mixed-precision support:" << endl;
      cout << "  1. Rebuild HYPRE with: ./configure --enable-mixed-precision" << endl;
      cout << "  2. Rebuild MFEM against the new HYPRE" << endl;
      cout << "  3. Recompile this example\n" << endl;
   }
#endif

   return 0;
}
