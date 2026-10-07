// Copyright 2022 Samuel Siltanen
// Make.cpp

#include "Make.hpp"
#include "Config.hpp"
#if COLLECT_STATS
#include "Stats.hpp"
#endif
#if HASH_TABLE
#include "HashTable.hpp"
#endif

#include <immintrin.h>

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

// Castling rights that remain when a move starts from or ends at a square
struct CastlingMasks
{
    uint64_t mask[64];

    CastlingMasks()
    {
        for (int i = 0; i < 64; ++i) mask[i] = ~0ULL;
        mask[A8] = ~CastlingBlackLong;
        mask[E8] = ~(CastlingBlackShort | CastlingBlackLong);
        mask[H8] = ~CastlingBlackShort;
        mask[A1] = ~CastlingWhiteLong;
        mask[E1] = ~(CastlingWhiteShort | CastlingWhiteLong);
        mask[H1] = ~CastlingWhiteShort;
    }
};
static const CastlingMasks castlingMasks;

// Rook move for castling, indexed by the king's destination square
struct CastlingRookMoves
{
    uint64_t mov[64];

    CastlingRookMoves()
    {
        for (int i = 0; i < 64; ++i) mov[i] = 0;
        mov[G1] = 0xa000000000000000ULL;
        mov[C1] = 0x0900000000000000ULL;
        mov[G8] = 0x00000000000000a0ULL;
        mov[C8] = 0x0000000000000009ULL;
    }
};
static const CastlingRookMoves castlingRookMoves;

// Make a move for side C
template<Color C>
__forceinline Position make(const Position& pos, const Move& move)
{
    Position next = pos;

    unsigned long srcSq = move.src();
    unsigned long dstSq = move.dst();
    uint64_t src = (1ULL << srcSq);
    uint64_t dst = (1ULL << dstSq);
    uint64_t mov = src | dst;
    Piece piece = move.piece();

#if HASH_TABLE || COLLECT_STATS
    // Captured piece, if any. Kings are never captured.
    uint64_t captured = (pos.p | pos.n | pos.bq | pos.rq) & dst;
#endif
#if COLLECT_STATS
    if (captured) statsCaptures++;
#endif

#if HASH_TABLE
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

        if (captured)
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
    __m256i pieces = _mm256_load_si256((__m256i*)&next);
    pieces = _mm256_andnot_si256(_mm256_set1_epi64x(dst), pieces);
    pieces = _mm256_xor_si256(pieces, _mm256_and_si256(_mm256_load_si256((const __m256i*)PieceLanes[piece]), _mm256_set1_epi64x(mov)));

    // Promotion: replace the pawn on the destination square with the promoted piece
    if (move.packed & 0x8000)
    {
        pieces = _mm256_xor_si256(pieces, _mm256_set_epi64x(0, 0, 0, dst));
        pieces = _mm256_xor_si256(pieces, _mm256_and_si256(_mm256_load_si256((const __m256i*)PieceLanes[move.prom()]), _mm256_set1_epi64x(dst)));
#if HASH_TABLE
        if (updateHash) hash ^= dstKeys[Pawn - 1] ^ dstKeys[move.prom() - 1];
#endif
    }
    _mm256_store_si256((__m256i*)&next, pieces);

    if (C == White)
    {
        next.w ^= mov;
    }
    else
    {
        next.w &= ~dst; // Handle capture here, because we test for white here anyway
    }

    // Clear EP after any move (may be reset below) and invalidate castling when K or R moves or a rook is captured
    next.state &= 0xfffffffffffff01f & castlingMasks.mask[srcSq] & castlingMasks.mask[dstSq];

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
#if HASH_TABLE
            if (updateHash)
            {
                const uint64_t* capturedKeys = HashTable::squareKeys((C == White) ? dstSq + 8 : dstSq - 8);
                hash ^= capturedKeys[Pawn - 1];
                if (C == Black) hash ^= capturedKeys[WhiteKey];
            }
#endif
#if COLLECT_STATS
            statsCaptures++;
            statsEPs++;
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
            uint64_t rookMov = castlingRookMoves.mov[dstSq];
            next.rq ^= rookMov;
            if (C == White) next.w ^= rookMov;
#if HASH_TABLE
            if (updateHash)
            {
                const uint64_t* rookKeys1 = HashTable::squareKeys(static_cast<unsigned long>(_tzcnt_u64(rookMov)));
                const uint64_t* rookKeys2 = HashTable::squareKeys(63 - static_cast<unsigned long>(_lzcnt_u64(rookMov)));
                hash ^= rookKeys1[Rook - 1] ^ rookKeys2[Rook - 1];
                if (C == White) hash ^= rookKeys1[WhiteKey] ^ rookKeys2[WhiteKey];
            }
#endif
#if COLLECT_STATS
            statsCastles++;
#endif
        }
    }

    // Update state
    next.state ^= 1;

#if HASH_TABLE
    if (updateHash) next.hash = hash ^ HashTable::stateHash(pos.state) ^ HashTable::stateHash(next.state);
#endif

    return next;
}

template Position make<White>(const Position& pos, const Move& move);
template Position make<Black>(const Position& pos, const Move& move);

Position make(const Position& pos, const Move& move)
{
    return (pos.state & TurnWhite) ? make<White>(pos, move) : make<Black>(pos, move);
}
