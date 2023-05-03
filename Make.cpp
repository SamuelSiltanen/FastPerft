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

#if !HASH_TABLE && !COLLECT_HASH

Position make(const Position& pos, const Move& move)
{
    Position next = pos;

    uint64_t src = (1ULL << (move.src()));
    uint64_t dst = (1ULL << (move.dst()));
    Piece piece = move.piece();
    
    uint64_t mov = src | dst;
    
    // Capture if any
    __m256i pieces = _mm256_load_si256((__m256i*)&next);
    __m256i capture = _mm256_set1_epi64x(dst);
    pieces = _mm256_andnot_si256(capture, pieces);
    _mm256_storeu_si256((__m256i*)&next, pieces);    

    if (next.state & TurnWhite)
    {
        next.w ^= mov;
    }
    else
    {
        next.w &= ~dst; // Handle capture here, because we test for white here anyway
    }

    // Clear EP after any move (may be reset below)
    next.state &= 0xfffffffffffff01f;

    if (piece == Pawn)
    {
        next.p ^= mov;

        // Promotion
        if (move.prom())
        {
            next.p ^= dst;
            switch (move.prom())
            {
            case Queen:
                next.bq ^= dst;
                next.rq ^= dst;
                break;
            case Rook:
                next.rq ^= dst;
                break;
            case Bishop:
                next.bq ^= dst;
                break;
            case Knight:
                next.n ^= dst;
                break;
            default:
                break;
            }
        }

        // Capture EP pawn
        if (pos.state & EPValid)
        {
            uint64_t EPSquare = (pos.state >> 5) & 63;
            if (move.dst() == EPSquare)
            {
                if (pos.state & TurnWhite)
                {
                    mov = (1ULL << (EPSquare + 8));
                    next.p ^= mov;
                }
                else
                {
                    mov = (1ULL << (EPSquare - 8));
                    next.p ^= mov;
                    next.w ^= mov;
                }
            }
        }

        // Set new EP square if double pawn move
        uint64_t EPSquare = 0;
        if (next.state & TurnWhite)
        {
            if ((move.src() >> 3) == 6 && (move.dst() >> 3) == 4)
            {
                EPSquare = static_cast<uint64_t>(move.src()) - 8 + 64;
            }
        }
        else
        {
            if ((move.src() >> 3) == 1 && (move.dst() >> 3) == 3)
            {
                EPSquare = static_cast<uint64_t>(move.src()) + 8 + 64;
            }
        }
        next.state |= (EPSquare << 5);
    }

    else if (piece == Knight)
    {
        next.n ^= mov;
    }
    else if (piece == Bishop)
    {
        next.bq ^= mov;
    }
    else if (piece == Queen)
    {
        next.bq ^= mov;
        next.rq ^= mov;
    }

    // Invalidate castling when R or K moves or when there is a capture to rook square
    // TODO: The conditions can be combined to test just if the squares are involved in the move
    else if (piece == Rook)
    {
        next.rq ^= mov;

        if (next.state & TurnWhite)
        {
            if (move.src() == 63)
            {
                next.state &= ~CastlingWhiteShort;
            }
            else if (move.src() == 56)
            {
                next.state &= ~CastlingWhiteLong;
            }
        }
        else
        {
            if (move.src() == 7)
            {
                next.state &= ~CastlingBlackShort;
            }
            else if (move.src() == 0)
            {
                next.state &= ~CastlingBlackLong;
            }
        }
    }

    else if (piece == King)
    {
        next.k ^= mov;

        next.state &= (next.state & TurnWhite) ?
            ~(CastlingWhiteShort | CastlingWhiteLong) :
            ~(CastlingBlackShort | CastlingBlackLong);

        // Castling rook move
        if (next.state & TurnWhite)
        {
            if (move.packed == 0x6fbc)
            {
                mov = 0xa000000000000000ULL;
                next.rq ^= mov;
                next.w ^= mov;
            }
            else if (move.packed == 0x6ebc)
            {
                mov = 0x0900000000000000ULL;
                next.rq ^= mov;
                next.w ^= mov;
            }
        }
        else
        {
            if (move.packed == 0x6184)
            {
                mov = 0x00000000000000a0ULL;
                next.rq ^= mov;
            }
            else if (move.packed == 0x6084)
            {
                mov = 0x0000000000000009ULL;
                next.rq ^= mov;
            }
        }
    }

    // It actually doesn't matter if there is a rook or not, or even whose move it is
    if (dst & 0x8100000000000081ULL)
    {
        if (move.dst() == 0) next.state &= ~CastlingBlackLong;
        if (move.dst() == 7) next.state &= ~CastlingBlackShort;
        if (move.dst() == 56) next.state &= ~CastlingWhiteLong;
        if (move.dst() == 63) next.state &= ~CastlingWhiteShort;
    }

    // Update state
    next.state ^= 1;

    return next;
}

#else

Position make(const Position& pos, const Move& move)
{
    Position next = pos;

    uint64_t src = (1ULL << (move.src()));
    uint64_t dst = (1ULL << (move.dst()));
    Piece piece = move.piece();

#if COLLECT_STATS
    if (dst & (next.p | next.n | next.bq | next.rq)) statsCaptures++;
#endif

#if HASH_TABLE    
    const HashTable::Hashes& srcHash = HashTable::hashSquare(move.src());
    const HashTable::Hashes& dstHash = HashTable::hashSquare(move.dst());

    if (next.p & dst) next.hash ^= dstHash.p;
    if (next.n & dst) next.hash ^= dstHash.n;
    if (next.bq & ~next.rq & dst) next.hash ^= dstHash.b;
    if (next.rq & ~next.bq & dst) next.hash ^= dstHash.r;
    if (next.bq & next.rq & dst) next.hash ^= dstHash.q;
#endif

    // Capture if any
    next.p &= ~dst;
    next.n &= ~dst;
    next.bq &= ~dst;
    next.rq &= ~dst;

    // Move piece
    uint64_t mov = src | dst;

    if (next.state & TurnWhite)
    {
        next.w ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.w;
        next.hash ^= dstHash.w;
#endif
    }
    else
    {
#if HASH_TABLE
        if (next.w & dst) next.hash ^= dstHash.w;
#endif
        next.w &= ~dst; // Handle capture here, because we test for white here anyway
    }

    // Clear EP after any move (may be reset below)
    next.state &= 0xfffffffffffff01f;

    if (piece == Pawn)
    {
        next.p ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.p;
        next.hash ^= dstHash.p;
#endif

        // Promotion
        if (move.prom())
        {
            next.p ^= dst;
#if HASH_TABLE
            next.hash ^= dstHash.p;
#endif
            switch (move.prom())
            {
            case Queen:
                next.bq ^= dst;
                next.rq ^= dst;
#if HASH_TABLE
                next.hash ^= dstHash.q;
#endif
                break;
            case Rook:
                next.rq ^= dst;
#if HASH_TABLE
                next.hash ^= dstHash.r;
#endif
                break;
            case Bishop:
                next.bq ^= dst;
#if HASH_TABLE
                next.hash ^= dstHash.b;
#endif
                break;
            case Knight:
                next.n ^= dst;
#if HASH_TABLE
                next.hash ^= dstHash.n;
#endif
                break;
            default:
                break;
            }
        }

        // Capture EP pawn
        if (pos.state & EPValid)
        {
            uint64_t EPSquare = (pos.state >> 5) & 63;
            if (move.dst() == EPSquare)
            {
                if (pos.state & TurnWhite)
                {
                    mov = (1ULL << (EPSquare + 8));
                    next.p ^= mov;
#if HASH_TABLE
                    next.hash ^= HashTable::hashSquare(EPSquare + 8).p;
#endif
#if COLLECT_STATS
                    statsCaptures++;
                    statsEPs++;
#endif
                }
                else
                {
                    mov = (1ULL << (EPSquare - 8));
                    next.p ^= mov;
                    next.w ^= mov;
#if HASH_TABLE
                    next.hash ^= HashTable::hashSquare(EPSquare - 8).p;
                    next.hash ^= HashTable::hashSquare(EPSquare - 8).w;
#endif
#if COLLECT_STATS
                    statsCaptures++;
                    statsEPs++;
#endif
                }
            }
        }

        // Set new EP square if double pawn move
        uint64_t EPSquare = 0;
        if (next.state & TurnWhite)
        {
            if ((move.src() >> 3) == 6 && (move.dst() >> 3) == 4)
            {
                EPSquare = static_cast<uint64_t>(move.src()) - 8 + 64;
            }
        }
        else
        {
            if ((move.src() >> 3) == 1 && (move.dst() >> 3) == 3)
            {
                EPSquare = static_cast<uint64_t>(move.src()) + 8 + 64;
            }
        }
        next.state |= (EPSquare << 5);
    }

    else if (piece == Knight)
    {
        next.n ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.n;
        next.hash ^= dstHash.n;
#endif
    }
    else if (piece == Bishop)
    {
        next.bq ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.b;
        next.hash ^= dstHash.b;
#endif
    }
    else if (piece == Queen)
    {
        next.bq ^= mov;
        next.rq ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.q;
        next.hash ^= dstHash.q;
#endif
    }

    // Invalidate castling when R or K moves or when there is a capture to rook square
    // TODO: The conditions can be combined to test just if the squares are involved in the move
    else if (piece == Rook)
    {
        next.rq ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.r;
        next.hash ^= dstHash.r;
#endif

        if (next.state & TurnWhite)
        {
            if (move.src() == 63)
            {
                next.state &= ~CastlingWhiteShort;
            }
            else if (move.src() == 56)
            {
                next.state &= ~CastlingWhiteLong;
            }
        }
        else
        {
            if (move.src() == 7)
            {
                next.state &= ~CastlingBlackShort;
            }
            else if (move.src() == 0)
            {
                next.state &= ~CastlingBlackLong;
            }
        }
    }

    else if (piece == King)
    {
        next.k ^= mov;
#if HASH_TABLE
        next.hash ^= srcHash.k;
        next.hash ^= dstHash.k;
#endif

        next.state &= (next.state & TurnWhite) ?
            ~(CastlingWhiteShort | CastlingWhiteLong) :
            ~(CastlingBlackShort | CastlingBlackLong);

        // Castling rook move
        if (next.state & TurnWhite)
        {
            if (move.packed == 0x6fbc)
            {
                mov = 0xa000000000000000ULL;
                next.rq ^= mov;
                next.w ^= mov;
#if HASH_TABLE
                next.hash ^= (HashTable::hashSquare(63).r | HashTable::hashSquare(61).r);
                next.hash ^= (HashTable::hashSquare(63).w | HashTable::hashSquare(61).w);
#endif
#if COLLECT_STATS
                statsCastles++;
#endif
            }
            else if (move.packed == 0x6ebc)
            {
                mov = 0x0900000000000000ULL;
                next.rq ^= mov;
                next.w ^= mov;
#if HASH_TABLE
                next.hash ^= (HashTable::hashSquare(56).r | HashTable::hashSquare(59).r);
                next.hash ^= (HashTable::hashSquare(56).w | HashTable::hashSquare(59).w);
#endif
#if COLLECT_STATS
                statsCastles++;
#endif
            }
        }
        else
        {
            if (move.packed == 0x6184)
            {
                mov = 0x00000000000000a0ULL;
                next.rq ^= mov;
#if HASH_TABLE
                next.hash ^= (HashTable::hashSquare(7).r | HashTable::hashSquare(5).r);
#endif
#if COLLECT_STATS
                statsCastles++;
#endif
            }
            else if (move.packed == 0x6084)
            {
                mov = 0x0000000000000009ULL;
                next.rq ^= mov;
#if HASH_TABLE
                next.hash ^= (HashTable::hashSquare(0).r | HashTable::hashSquare(3).r);
#endif
#if COLLECT_STATS
                statsCastles++;
#endif
            }
        }
    }

    // It actually doesn't matter if there is a rook or not, or even whose move it is
    if (dst & 0x8100000000000081ULL)
    {
        if (move.dst() == 0) next.state &= ~CastlingBlackLong;
        if (move.dst() == 7) next.state &= ~CastlingBlackShort;
        if (move.dst() == 56) next.state &= ~CastlingWhiteLong;
        if (move.dst() == 63) next.state &= ~CastlingWhiteShort;
    }

#if HASH_TABLE
    next.hash ^= HashTable::hashEP(pos.state, next.state);
    next.hash ^= HashTable::hashCastling(pos.state, next.state);
#endif    

    // Update state
    next.state ^= 1;
#if HASH_TABLE
    next.hash ^= HashTable::hashTurn();
#endif

    return next;
}

#endif
