// Copyright (c) 2010-2025, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#ifndef MFEM_TILE_ABSTRACTION_HPP
#define MFEM_TILE_ABSTRACTION_HPP

#include "../../general/forall.hpp"

namespace mfem
{

/// Tile abstraction for 3D thread blocks
/// Encapsulates thread iteration patterns for 3D data
namespace tile3d
{

/// Represents threading dimensions for a 3D tile
/// Template parameters are compile-time max dimensions
template<int MX, int MY, int MZ>
struct Tile
{
   int nx, ny, nz;  // Runtime dimensions

   MFEM_HOST_DEVICE Tile(int x, int y, int z) : nx(x), ny(y), nz(z) {}

   /// Load a 3D tile from global memory using this tile's threading pattern
   template<typename SrcView, typename DstArray>
   MFEM_HOST_DEVICE void load(const SrcView &src, DstArray &dst, int elem) const
   {
      MFEM_FOREACH_THREAD_DIRECT(iz,z,nz)
      {
         MFEM_FOREACH_THREAD_DIRECT(iy,y,ny)
         {
            MFEM_FOREACH_THREAD_DIRECT(ix,x,nx)
            {
               dst[iz][iy][ix] = src(ix,iy,iz,elem);
            }
         }
      }
   }

   /// Execute a lambda for each thread in this tile's X-Y plane
   template<typename Lambda>
   MFEM_HOST_DEVICE void forXY(Lambda&& func) const
   {
      MFEM_FOREACH_THREAD_DIRECT(iy,y,ny)
      {
         MFEM_FOREACH_THREAD_DIRECT(ix,x,nx)
         {
            func(ix, iy);
         }
      }
   }

   /// Execute a lambda for each thread in this tile
   template<typename Lambda>
   MFEM_HOST_DEVICE void forXYZ(Lambda&& func) const
   {
      MFEM_FOREACH_THREAD_DIRECT(iz,z,nz)
      {
         MFEM_FOREACH_THREAD_DIRECT(iy,y,ny)
         {
            MFEM_FOREACH_THREAD_DIRECT(ix,x,nx)
            {
               func(ix, iy, iz);
            }
         }
      }
   }
};

/// Helper to create a mixed-dimension tile (e.g., D x D x Q)
/// Useful for intermediate tensor contraction stages
template<int MX, int MY, int MZ>
struct MixedTile
{
   int nx, ny, nz;

   MFEM_HOST_DEVICE MixedTile(int x, int y, int z) : nx(x), ny(y), nz(z) {}

   /// Execute a lambda for each thread with mixed dimensions
   template<typename Lambda>
   MFEM_HOST_DEVICE void forEach(Lambda&& func) const
   {
      MFEM_FOREACH_THREAD_DIRECT(iz,z,nz)
      {
         MFEM_FOREACH_THREAD_DIRECT(iy,y,ny)
         {
            MFEM_FOREACH_THREAD_DIRECT(ix,x,nx)
            {
               func(ix, iy, iz);
            }
         }
      }
   }

   /// Execute a lambda using MFEM_FOREACH_THREAD (not DIRECT)
   /// Used when we don't need strict thread-to-index mapping
   template<typename Lambda>
   MFEM_HOST_DEVICE void forEachNonDirect(Lambda&& func) const
   {
      MFEM_FOREACH_THREAD(iz,z,nz)
      {
         MFEM_FOREACH_THREAD(iy,y,ny)
         {
            MFEM_FOREACH_THREAD(ix,x,nx)
            {
               func(ix, iy, iz);
            }
         }
      }
   }
};

} // namespace tile3d

} // namespace mfem

#endif // MFEM_TILE_ABSTRACTION_HPP
