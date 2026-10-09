// Copyright 2022 Samuel Siltanen
// Platform.hpp

#pragma once

#include <cstdint>

// Functions marked with these compile for both the CPU and the GPU when the file is compiled with nvcc
#ifdef __CUDACC__
#define FP_HOST_DEVICE __host__ __device__
#define FP_INLINE __host__ __device__ __forceinline__
#else
#define FP_HOST_DEVICE
#define FP_INLINE __forceinline
#endif

#ifndef __CUDA_ARCH__
#include <intrin.h>
#endif

// Bit operations with the semantics of the x64 intrinsics: tzcnt64 and lzcnt64 return 64 for zero, and
// bitScanForward64 and bitScanReverse64 return false for zero and leave the index unchanged
FP_INLINE uint64_t tzcnt64(uint64_t x)
{
#ifdef __CUDA_ARCH__
    return static_cast<uint64_t>(__popcll((x & (0 - x)) - 1)); // Fewer slow instructions than __clzll(__brevll(x))
#else
    return _tzcnt_u64(x);
#endif
}

FP_INLINE uint64_t lzcnt64(uint64_t x)
{
#ifdef __CUDA_ARCH__
    return static_cast<uint64_t>(__clzll(static_cast<long long>(x)));
#else
    return _lzcnt_u64(x);
#endif
}

FP_INLINE uint64_t popcnt64(uint64_t x)
{
#ifdef __CUDA_ARCH__
    return static_cast<uint64_t>(__popcll(x));
#else
    return __popcnt64(x);
#endif
}

FP_INLINE bool bitScanForward64(unsigned long* index, uint64_t x)
{
#ifdef __CUDA_ARCH__
    if (!x) return false;
    *index = static_cast<unsigned long>(tzcnt64(x));
    return true;
#else
    return _BitScanForward64(index, x) != 0;
#endif
}

FP_INLINE bool bitScanReverse64(unsigned long* index, uint64_t x)
{
#ifdef __CUDA_ARCH__
    if (!x) return false;
    *index = static_cast<unsigned long>(63 - lzcnt64(x));
    return true;
#else
    return _BitScanReverse64(index, x) != 0;
#endif
}
