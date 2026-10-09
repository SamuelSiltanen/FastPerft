// Copyright 2022 Samuel Siltanen
// MakeImpl.hpp
//
// Making moves, compiles for both the CPU and the GPU. Included from Make.hpp.

#pragma once

#include "Make.hpp"
#include "Config.hpp"
#include "MoveTables.hpp"

// The GPU version does not update the hash key yet
#if HASH_TABLE && !defined(__CUDA_ARCH__)
#define MAKE_UPDATES_HASH 1
#include "HashTable.hpp"
#else
#define MAKE_UPDATES_HASH 0
#endif

#ifndef __CUDA_ARCH__
#include <immintrin.h>
#endif

#ifndef __CUDA_ARCH__
// Which of the bitboards p, n, bq, rq a piece type occupies
alignas(32) static const uint64_t PieceLanes[8][4] =
{
    { 0, 0, 0, 0 },                     // None
    { ~0ULL, 0, 0, 0 },                 // Pawn
    { 0, ~0ULL, 0, 0 },                 // Knight
    { 0, 0, ~0ULL, 0 },                 // Bishop
    { 0, 0, 0, ~0ULL },                 // Rook
    { 0, 0, ~0ULL, ~0ULL },             // Queen
    { 0, 0, 0, 0 },                     // King
    { 0, 0, 0, 0 }                      // EP
};
#endif

// Make a move for side C
template<Color C>
FP_INLINE Position make(const Position& pos, const Move& move)
{
    Position next = pos;

    unsigned long srcSq = move.src();
    unsigned long dstSq = move.dst();
    uint64_t src = (1ULL << srcSq);
    uint64_t dst = (1ULL << dstSq);
    uint64_t mov = src | dst;
    Piece piece = move.piece();

#if MAKE_UPDATES_HASH
    // The hash key is needed only with a hash table. Without it, next.hash keeps the old, unused value.
    const bool updateHash = (hashTable != nullptr);

    // The keys of a square are indexed by piece type - 1, and the white key is at WhiteKey
    constexpr int WhiteKey = 6;
    const uint64_t* dstKeys = nullptr;
    uint64_t hash = 0;

    if (updateHash)
    {
        const uint64_t* srcKeys = HashTable::squareKeys(srcSq);
        dstKeys = HashTable::squareKeys(dstSq);

        hash = pos.hash;
        hash ^= srcKeys[piece - 1] ^ dstKeys[piece - 1];
        if (C == White) hash ^= srcKeys[WhiteKey] ^ dstKeys[WhiteKey];

        // Captured piece, if any. Kings are never captured.
        if ((pos.p | pos.n | pos.bq | pos.rq) & dst)
        {
            // Index of the captured piece type: pawn 0, knight 1, bishop 2, rook 3, queen 4
            uint64_t n = (pos.n >> dstSq) & 1;
            uint64_t b = (pos.bq >> dstSq) & 1;
            uint64_t r = (pos.rq >> dstSq) & 1;
            hash ^= dstKeys[n + 2 * b + 3 * r - (b & r)];
            if (C == Black) hash ^= dstKeys[WhiteKey];
        }
    }
#endif

    // Capture if any, then move the piece in the bitboards it occupies
#ifdef __CUDA_ARCH__
    next.p = (next.p & ~dst) ^ ((piece == Pawn) ? mov : 0);
    next.n = (next.n & ~dst) ^ ((piece == Knight) ? mov : 0);
    next.bq = (next.bq & ~dst) ^ ((piece == Bishop || piece == Queen) ? mov : 0);
    next.rq = (next.rq & ~dst) ^ ((piece == Rook || piece == Queen) ? mov : 0);

    // Promotion: replace the pawn on the destination square with the promoted piece
    if (move.packed & 0x8000)
    {
        Piece prom = move.prom();
        next.p ^= dst;
        next.n ^= (prom == Knight) ? dst : 0;
        next.bq ^= (prom == Bishop || prom == Queen) ? dst : 0;
        next.rq ^= (prom == Rook || prom == Queen) ? dst : 0;
    }
#else
    __m256i pieces = _mm256_load_si256((__m256i*)&next);
    pieces = _mm256_andnot_si256(_mm256_set1_epi64x(dst), pieces);
    pieces = _mm256_xor_si256(pieces, _mm256_and_si256(_mm256_load_si256((const __m256i*)PieceLanes[piece]), _mm256_set1_epi64x(mov)));

    // Promotion: replace the pawn on the destination square with the promoted piece
    if (move.packed & 0x8000)
    {
        pieces = _mm256_xor_si256(pieces, _mm256_set_epi64x(0, 0, 0, dst));
        pieces = _mm256_xor_si256(pieces, _mm256_and_si256(_mm256_load_si256((const __m256i*)PieceLanes[move.prom()]), _mm256_set1_epi64x(dst)));
#if MAKE_UPDATES_HASH
        if (updateHash) hash ^= dstKeys[Pawn - 1] ^ dstKeys[move.prom() - 1];
#endif
    }
    _mm256_store_si256((__m256i*)&next, pieces);
#endif

    if (C == White)
    {
        next.w ^= mov;
    }
    else
    {
        next.w &= ~dst; // Handle capture here, because we test for white here anyway
    }

    // Clear EP after any move (may be reset below) and invalidate castling when K or R moves or a rook is captured
    next.state &= 0xfffffffffffff01f & tables().castlingMasks[srcSq] & tables().castlingMasks[dstSq];

    if (piece == Pawn)
    {
        // Capture EP pawn
        if ((pos.state & EPValid) && dstSq == ((pos.state >> 5) & 63))
        {
            if (C == White)
            {
                next.p ^= (dst << 8);
            }
            else
            {
                next.p ^= (dst >> 8);
                next.w ^= (dst >> 8);
            }
#if MAKE_UPDATES_HASH
            if (updateHash)
            {
                const uint64_t* capturedKeys = HashTable::squareKeys((C == White) ? dstSq + 8 : dstSq - 8);
                hash ^= capturedKeys[Pawn - 1];
                if (C == Black) hash ^= capturedKeys[WhiteKey];
            }
#endif
        }

        // Set new EP square to the skipped square if double pawn move (64 sets the EPValid bit)
        if ((srcSq ^ dstSq) == 16)
        {
            next.state |= (static_cast<uint64_t>((srcSq + dstSq) >> 1) + 64) << 5;
        }
    }
    else if (piece == King)
    {
        next.k ^= mov;

        // Castling rook move
        int distance = static_cast<int>(dstSq) - static_cast<int>(srcSq);
        if (distance == 2 || distance == -2)
        {
            uint64_t rookMov = tables().castlingRookMoves[dstSq];
            next.rq ^= rookMov;
            if (C == White) next.w ^= rookMov;
#if MAKE_UPDATES_HASH
            if (updateHash)
            {
                const uint64_t* rookKeys1 = HashTable::squareKeys(static_cast<unsigned long>(_tzcnt_u64(rookMov)));
                const uint64_t* rookKeys2 = HashTable::squareKeys(63 - static_cast<unsigned long>(_lzcnt_u64(rookMov)));
                hash ^= rookKeys1[Rook - 1] ^ rookKeys2[Rook - 1];
                if (C == White) hash ^= rookKeys1[WhiteKey] ^ rookKeys2[WhiteKey];
            }
#endif
        }
    }

    // Update state
    next.state ^= 1;

#if MAKE_UPDATES_HASH
    if (updateHash) next.hash = hash ^ HashTable::stateHash(pos.state) ^ HashTable::stateHash(next.state);
#endif

    return next;
}
