/***************************************************************************************************
 * Copyright (c) 2017-2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **************************************************************************************************/
/*! \file
    \brief Defies structural properties of mixed-precision integer GEMM. Multiplicands are assumed
      to be packed 8bit integers, accumulators are assumed to be 32b signed integers, and output
      formats vary.
*/
#pragma once

#include <cutlass/convert.h>
#include <cutlass/gemm/gemm.h>
#include <cutlass/gemm/gemm_epilogue.h>
#include <cutlass/gemm/gemm_epilogue_traits.h>
#include <cutlass/gemm/gemm_global_tile.h>
#include <cutlass/gemm/gemm_shared_tile.h>
#include <cutlass/gemm/gemm_traits.h>
#include <cutlass/gemm/igemm_epilogue.h>
#include <cutlass/gemm/igemm_global_tile.h>
#include <cutlass/gemm/igemm_multiply_add.h>
#include <cutlass/gemm/igemm_swizzle.h>
#include <cutlass/reshape_tile.h>

namespace cutlass {
namespace gemm {

////////////////////////////////////////////////////////////////////////////////////////////////////

template <
    /// The tile size for the GEMM KxNxM.
    typename OutputTile_,
    /// The output type.
    typename ScalarD_,
    /// The number of accumulators per thread.
    typename AccumulatorsPerThread_>
struct IgemmConfig
    : public GemmConfig<
          /// The scalar type for A.
          int8_t,
          /// The scalar type for B.
          int8_t,
          /// The scalar type for C.
          ScalarD_,
          /// The scalar type for D.
          ScalarD_,
          /// The tile size for the GEMM KxNxM.
          OutputTile_,
          /// The functor to do the math in the main loop.
          ThreadMultiplyAdd<AccumulatorsPerThread_, Shape<1, 4, 8>, int8_t, int8_t, int>,
          /// The number of scalars per LDG for A.
          4,
          /// The number of scalars per STS for A.
          4,
          /// The number of scalars per LDS for A.
          16,
          /// The number of scalars per LDG for B.
          4,
          /// The number of scalars per STS for B.
          4,
          /// The number of scalars per LDS for B.
          16,
          /// The number of scalars per LDG for C and STG for D.
          1,
          /// The number of scalars per STS for D.
          4,
          /// The number of scalars per LDS for D.
          1,
          /// The number of stages in shared memory.
          2> {};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename OutputTile_, typename AccumulatorsPerThread_>
struct IgemmConfig<OutputTile_, int8_t, AccumulatorsPerThread_>
    : public GemmConfig<
          /// The scalar type for A.
          int8_t,
          /// The scalar type for B.
          int8_t,
          /// The scalar type for C.
          int8_t,
          /// The scalar type for D.
          int8_t,
          /// The tile size for the GEMM KxNxM.
          OutputTile_,
          /// The functor to do the math in the main loop.
          ThreadMultiplyAdd<AccumulatorsPerThread_, Shape<1, 4, 8>, int8_t, int8_t, int>,
          /// The number of scalars per LDG for A.
          4,
          /// The number of scalars per STS for A.
          4,
          /// The number of scalars per LDS for A.
          16,
          /// The number of scalars per LDG for B.
          4,
          /// The number of scalars per STS for B.
          4,
          /// The number of scalars per LDS for B.
          16,
          /// The number of scalars per LDG for C and STG for D.
          4,
          /// The number of scalars per STS for D.
          4,
          /// The number of scalars per LDS for D.
          4,
          /// The number of stages in shared memory.
          2> {};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <enum MatrixLayout::Kind kLayout_, typename GemmConfig_>
struct IgemmTileTraitsHelperA : public GemmTileTraitsHelperA<kLayout_, GemmConfig_> {};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename GemmConfig_>
struct IgemmTileTraitsHelperA<MatrixLayout::kColumnMajor, GemmConfig_>
    : public GemmTileTraitsHelperA<MatrixLayout::kColumnMajor, GemmConfig_> {
  /// The base config.
  typedef GemmTileTraitsHelperA<MatrixLayout::kColumnMajor, GemmConfig_> Base;

  /// The number of scalars per LDG/STS/LDS for A.
  static int const kScalarsPerStsA = 16;

  /// The traits class to build the iterator to load data from global memory for A^N.
  typedef IgemmContiguousGlobalTileTraits<
      GemmOperand::kA,
      // The layout.
      MatrixLayout::kColumnMajor,
      // The pointer is float const.
      int8_t const,
      // The tile has size KxM in GEMM's terminology.
      Shape<1, GemmConfig_::OutputTile::kD, GemmConfig_::OutputTile::kW>,
      // The threads are distributed as warps x 32 (the traits may reorganize).
      Shape<1, ShapeCount<typename GemmConfig_::Warps>::kCount, GemmConfig_::kWarpSize>,
      // The number of scalars per LDG (LDG.32 or LDG.128, etc).
      4>
      GlobalTileTraits;

  /// The traits class to build the iterator to store data to shared memory for A^N.
  typedef GemmSharedStoreTileAbTraits<
      // The pointer is float.
      int8_t,
      // The tile has size KxM in GEMM's terminology.
      Shape<GemmConfig_::kStages, GemmConfig_::OutputTile::kD / 4, GemmConfig_::OutputTile::kW * 4>,
      // The threads are distributed as warps x 32 (the traits may reorganize).
      typename GlobalTileTraits::Threads,
      // The number of scalars per STS (STS.32 or STS.128, etc).
      kScalarsPerStsA>
      SharedStoreTileTraits;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <enum MatrixLayout::Kind kLayout_, typename GemmConfig_>
struct IgemmTileTraitsHelperB : public GemmTileTraitsHelperB<kLayout_, GemmConfig_> {};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename GemmConfig_>
struct IgemmTileTraitsHelperB<MatrixLayout::kRowMajor, GemmConfig_>
    : public GemmTileTraitsHelperB<MatrixLayout::kRowMajor, GemmConfig_> {
  /// The base config.
  typedef GemmTileTraitsHelperB<MatrixLayout::kRowMajor, GemmConfig_> Base;

  /// The number of scalars per LDG/STS/LDS for B.
  static int const kScalarsPerStsB = 16;

  /// The traits class to build the iterator to load data from global memory for B^T.
  typedef IgemmContiguousGlobalTileTraits<
      GemmOperand::kB,
      // The layout.
      MatrixLayout::kRowMajor,
      // The pointer is float const.
      int8_t const,
      // The tile has size KxM in GEMM's terminology.
      Shape<1, GemmConfig_::OutputTile::kD, GemmConfig_::OutputTile::kH>,
      // The threads are distributed as warps x 32 (the traits may reorganize).
      Shape<1, ShapeCount<typename GemmConfig_::Warps>::kCount, GemmConfig_::kWarpSize>,
      // The number of scalars per LDG (LDG.32 or LDG.128, etc).
      4>
      GlobalTileTraits;

  /// The traits class to build the iterator to store data to shared memory for B^N.
  typedef GemmSharedStoreTileAbTraits<
      // The pointer is float.
      int8_t,
      // The tile has size KxM in GEMM's terminology.
      Shape<GemmConfig_::kStages, GemmConfig_::OutputTile::kD / 4, GemmConfig_::OutputTile::kH * 4>,
      // The threads are distributed as warps x 32 (the traits may reorganize).
      typename GlobalTileTraits::Threads,
      // The number of scalars per STS (STS.32 or STS.128, etc).
      kScalarsPerStsB>
      SharedStoreTileTraits;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <enum MatrixLayout::Kind kLayout_, typename Iterator_>
struct IgemmTransformerA {};

template <typename Iterator_>
struct IgemmTransformerA<MatrixLayout::kRowMajor, Iterator_> {
  typedef Copy<typename Iterator_::Fragment> Transformer;
};

template <typename Iterator_>
struct IgemmTransformerA<MatrixLayout::kColumnMajor, Iterator_> {
  typedef IgemmSwizzle<Iterator_> Transformer;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <enum MatrixLayout::Kind kLayout_, typename Iterator_>
struct IgemmTransformerB {};

template <typename Iterator_>
struct IgemmTransformerB<MatrixLayout::kColumnMajor, Iterator_> {
  typedef Copy<typename Iterator_::Fragment> Transformer;
};

template <typename Iterator_>
struct IgemmTransformerB<MatrixLayout::kRowMajor, Iterator_> {
  typedef IgemmSwizzle<Iterator_> Transformer;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <
    /// The layout for A.
    MatrixLayout::Kind kLayoutA_,
    /// The layout for B.
    MatrixLayout::Kind kLayoutB_,
    /// The output tile.
    typename OutputTile_,
    /// The output type.
    typename ScalarD_,
    /// The functor to do the math in the epilogue.
    typename EpilogueFunctor_,
    /// The number of accumulators per thread.
    typename AccumulatorsPerThread_ = Shape<32, 8, 8>,
    /// The index.
    typename Index_ = int>
struct IgemmTraitsHelper {
  /// The IGEMM config.
  typedef IgemmConfig<OutputTile_, ScalarD_, AccumulatorsPerThread_> GemmConfig;
  /// The GEMM config for A.
  typedef IgemmTileTraitsHelperA<kLayoutA_, GemmConfig> GemmTileTraitsHelperA;
  /// The GEMM config for B.
  typedef IgemmTileTraitsHelperB<kLayoutB_, GemmConfig> GemmTileTraitsHelperB;

  /// The iterator to load A from global memory.
  typedef GemmGlobalIteratorAb<typename GemmTileTraitsHelperA::GlobalTileTraits, Index_>
      GlobalLoadIteratorA;
  /// The default transformer for A.
  typedef typename IgemmTransformerA<GemmTileTraitsHelperA::kLayout,
                                     GlobalLoadIteratorA>::Transformer GlobalTransformerA;
  /// The iterator to store A to shared memory.
  typedef TileStoreIterator<typename GemmTileTraitsHelperA::SharedStoreTileTraits,
                            typename GemmTileTraitsHelperA::SharedStoreTileTraits::Scalar,
                            IteratorAdvance::kH,
                            MemorySpace::kShared>
      SharedStoreIteratorA;
  /// The stream to load A from global memory to shared memory.
  typedef GlobalLoadStream<GlobalLoadIteratorA, SharedStoreIteratorA, GlobalTransformerA>
      GlobalLoadStreamA;

  /// The iterator to load B from global memory.
  typedef GemmGlobalIteratorAb<typename GemmTileTraitsHelperB::GlobalTileTraits, Index_>
      GlobalLoadIteratorB;
  // The default transformer for B.
  typedef typename IgemmTransformerB<GemmTileTraitsHelperB::kLayout,
                                     GlobalLoadIteratorB>::Transformer GlobalTransformerB;
  /// The iterator to store B to shared memory.
  typedef TileStoreIterator<typename GemmTileTraitsHelperB::SharedStoreTileTraits,
                            typename GemmTileTraitsHelperB::SharedStoreTileTraits::Scalar,
                            IteratorAdvance::kH,
                            MemorySpace::kShared>
      SharedStoreIteratorB;
  /// The stream to load B from global memory to shared memory.
  typedef GlobalLoadStream<GlobalLoadIteratorB, SharedStoreIteratorB, GlobalTransformerB>
      GlobalLoadStreamB;

  /// The iterator to load A from shared memory.
  typedef TileLoadIterator<typename GemmTileTraitsHelperA::SharedLoadTileTraits,
                           typename GemmTileTraitsHelperA::SharedLoadTileTraits::Scalar,
                           IteratorAdvance::kH,
                           MemorySpace::kShared>
      SharedLoadIteratorA;
  /// The stream to load A from shared memory.
  typedef SharedLoadStream<SharedLoadIteratorA, Copy<typename SharedLoadIteratorA::Fragment> >
      SharedLoadStreamA;
  /// The iterator to load B from shared memory.
  typedef TileLoadIterator<typename GemmTileTraitsHelperB::SharedLoadTileTraits,
                           typename GemmTileTraitsHelperB::SharedLoadTileTraits::Scalar,
                           IteratorAdvance::kH,
                           MemorySpace::kShared>
      SharedLoadIteratorB;
  /// The stream to load B from shared memory.
  typedef SharedLoadStream<SharedLoadIteratorB, Copy<typename SharedLoadIteratorB::Fragment> >
      SharedLoadStreamB;

  /// The multiply-add functor.
  typedef typename GemmConfig::MultiplyAdd MultiplyAdd;
  /// The object to clear accumulators.
  typedef ClearAccumulators<typename MultiplyAdd::ScalarC> ClearAccumulators;

  /// The epilogue.
  typedef IgemmEpilogue<IgemmEpilogueTraits<GemmConfig, EpilogueFunctor_> > Epilogue;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename ScalarD_>
struct IgemmEpilogueScalar {
  typedef float Scalar;
};

template <>
struct IgemmEpilogueScalar<int> {
  typedef int Scalar;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

template <
    /// The layout for A.
    MatrixLayout::Kind kLayoutA_,
    /// The layout for B.
    MatrixLayout::Kind kLayoutB_,
    /// The output tile.
    typename OutputTile_ = Shape<32, 128, 128>,
    /// The output type.
    typename ScalarD_ = int,
    /// The functor to do the math in the epilogue.
    typename EpilogueFunctor_ = LinearScaling<typename IgemmEpilogueScalar<ScalarD_>::Scalar>,
    /// The number of accumulators per thread.
    typename AccumulatorsPerThread_ = Shape<32, 8, 8>,
    /// The index.
    typename Index_ = int,
    /// The helper class.
    typename Helper_ = IgemmTraitsHelper<kLayoutA_,
                                         kLayoutB_,
                                         OutputTile_,
                                         ScalarD_,
                                         EpilogueFunctor_,
                                         AccumulatorsPerThread_,
                                         Index_> >
struct IgemmTraits : public GemmTraits<
                         // The config.
                         typename Helper_::GemmConfig,
                         // The stream to load A from global memory to shared memory.
                         typename Helper_::GlobalLoadStreamA,
                         // The stream to load B from global memory to shared memory.
                         typename Helper_::GlobalLoadStreamB,
                         // The stream to load A from shared memory.
                         typename Helper_::SharedLoadStreamA,
                         // The stream to load B from shared memory.
                         typename Helper_::SharedLoadStreamB,
                         // The epilogue.
                         typename Helper_::Epilogue,
                         // The block swizzle to reorganize the grid.
                         IdentityBlockSwizzle,
                         // The index.
                         Index_,
                         // The tool used to clear accumulators.
                         typename Helper_::ClearAccumulators> {};

////////////////////////////////////////////////////////////////////////////////////////////////////

}  // namespace gemm
}  // namespace cutlass
