// Copyright 2022 Samuel Siltanen
// MoveTables.hpp

#pragma once

#include "ChessTypes.hpp"
#include "Platform.hpp"

// Sliding piece attack lookup method on the CPU. The GPU uses kindergarten bitboards, or ray scans if they are disabled.
#define MAGIC_BITBOARDS 0
#define KINDERGARTEN_BITBOARDS 1
#define PEXT_INTRINSIC 1

struct alignas(64) Rays
{
    uint64_t SE;
    uint64_t SW;
    uint64_t NE;
    uint64_t NW;
    uint64_t S;
    uint64_t W;
    uint64_t N;
    uint64_t E;
};

// Lookup tables used by both the CPU and the GPU. They are small (about 17 kB), so the GPU can keep them in fast memory.
struct alignas(64) MoveTables
{
    Rays rays[64];
    uint64_t nmoves[64];
    uint64_t kmoves[64];
    uint64_t bishopRays[64]; // All diagonal squares from a square
    uint64_t castlingMasks[64]; // Castling rights that remain when a move starts from or ends at a square
    uint64_t castlingRookMoves[64]; // Rook move for castling, indexed by the king's destination square
#if KINDERGARTEN_BITBOARDS
    uint64_t swneExMask[64];
    uint64_t senwExMask[64];
    uint64_t weExMask[64];
    uint64_t AFileAttacks[8][64]; // 4 kB
    uint64_t KinderGartenAttacks[8][64]; // 4 kB
#endif
};

extern MoveTables moveTables;

#ifdef __CUDACC__
// Without relocatable device code, each CUDA translation unit has its own copy, which it must upload from moveTables
static __device__ MoveTables d_moveTables;
#endif

FP_INLINE const MoveTables& tables()
{
#ifdef __CUDA_ARCH__
    return d_moveTables;
#else
    return moveTables;
#endif
}

// Large lookup tables used only by the CPU
#if PEXT_INTRINSIC
extern uint64_t BMasks[64];
extern uint64_t RMasks[64];
extern uint64_t BAttacks[64][512]; // 512 kB
extern uint64_t RAttacks[64][4096]; // 4 MB
#endif

#if MAGIC_BITBOARDS
struct alignas(32) Magic
{
    uint64_t mask;
    uint64_t magic;
    uint64_t shift;
    uint64_t* ptr;
};

extern const int BBits[64];
extern const int RBits[64];
extern Magic BMagic[64];
extern Magic RMagic[64];
extern uint64_t BAttacks[64][512]; // 512 kB
extern uint64_t RAttacks[64][4096]; // 4 MB
extern uint64_t RCompressedAttacks[100 * 1024]; // 800 kB
#endif
