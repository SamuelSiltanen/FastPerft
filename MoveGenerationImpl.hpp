// Copyright 2022 Samuel Siltanen
// MoveGenerationImpl.hpp
//
// Move generation functions that compile for both the CPU and the GPU. Included from MoveGeneration.hpp.

#pragma once

#include "MoveGeneration.hpp"

#include <cstring>

template<Color C>
FP_INLINE Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    if (C == White)
    {
        uint64_t our = pos.w;
        uint64_t pcs = pos.p & our & 0x00ffffffffff0000 & (~occ << 8) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 8;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00ff000000000000 & (~occ << 8) & (~occ << 16) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 16;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        uint64_t their = occ & ~pos.w;
        pcs = pos.p & our & 0x00fefefefefe0000 & (their << 9) & (~anyPins | pins.pinnedSENW);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 9;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x007f7f7f7f7f0000 & (their << 7) & (~anyPins | pins.pinnedSWNE);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 7;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        // Promotions (also capturing)
        pcs = pos.p & our & 0x000000000000ff00 & (~occ << 8) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 8;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
        
        pcs = pos.p & our & 0x000000000000fe00 & (their << 9) & (~anyPins | pins.pinnedSENW);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 9;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x0000000000007f00 & (their << 7) & (~anyPins | pins.pinnedSWNE);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src - 7;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
    }
    else
    {
        uint64_t our = occ & ~pos.w;
        uint64_t pcs = pos.p & our & 0x0000ffffffffff00 & (~occ >> 8) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 8;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x000000000000ff00 & (~occ >> 8) & (~occ >> 16) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 16;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        uint64_t their = pos.w;
        pcs = pos.p & our & 0x0000fefefefefe00 & (their >> 7) & (~anyPins | pins.pinnedSWNE);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 7;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00007f7f7f7f7f00 & (their >> 9) & (~anyPins | pins.pinnedSENW);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 9;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        // Promotions (also capturing)
        pcs = pos.p & our & 0x00ff000000000000 & (~occ >> 8) & (~anyPins | pins.pinnedSN);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 8;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00fe000000000000 & (their >> 7) & (~anyPins | pins.pinnedSWNE);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 7;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x007f000000000000 & (their >> 9) & (~anyPins | pins.pinnedSENW);
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            dst = src + 9;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
    }

    // En passant
    if (pos.state & EPValid)
    {
        uint64_t EPSquare = (pos.state >> 5) & 63;
        if (C == White)
        {
            uint64_t our = pos.w;

            // Because EP removes two pieces from the same row, horizontal pins need an extra check
            uint64_t king = pos.k & our;
            unsigned long kingSq;
            kingSq = static_cast<unsigned long>(tzcnt64(king));
            bool kingOnEPRow = (kingSq >> 3) == 3;

            uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW);
            while (pcs) // Use while instead if to avoid goto-statement (see breaks below)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                dst = src - 9;
                if (kingOnEPRow)
                {
                    uint64_t left = tables().rays[src - 1].W & occ;
                    uint64_t right = tables().rays[src].E & occ;

                    unsigned long hit;
                    if (bitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (bitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }

            pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE);
            while (pcs)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                dst = src - 7;

                if (kingOnEPRow)
                {
                    uint64_t left = tables().rays[src].W & occ;
                    uint64_t right = tables().rays[src + 1].E & occ;

                    unsigned long hit;
                    if (bitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (bitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }
        }
        else
        {
            uint64_t our = ~pos.w;

            // Because EP removes two pieces from the same row, horizontal pins need an extra check
            uint64_t king = pos.k & our;
            unsigned long kingSq;
            kingSq = static_cast<unsigned long>(tzcnt64(king));
            bool kingOnEPRow = (kingSq >> 3) == 4;

            uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE);
            while (pcs)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                dst = src + 7;

                if (kingOnEPRow)
                {
                    uint64_t left = tables().rays[src - 1].W & occ;
                    uint64_t right = tables().rays[src].E & occ;

                    unsigned long hit;
                    if (bitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (bitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }

            pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW);
            while (pcs)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                dst = src + 9;

                if (kingOnEPRow)
                {
                    uint64_t left = tables().rays[src].W & occ;
                    uint64_t right = tables().rays[src + 1].E & occ;

                    unsigned long hit;
                    if (bitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (bitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }
        }
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins)
{
    unsigned long src, dst;
    
    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.n & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = tables().nmoves[src] & ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(Knight, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.bq & ~pos.rq & our & ~(pins.pinnedSN | pins.pinnedWE);
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = 0;
        if (!(pins.pinnedSENW & (1ULL << src))) sqrs |= swneMoves(src, occ);
        if (!(pins.pinnedSWNE & (1ULL << src))) sqrs |= senwMoves(src, occ);        
        sqrs &= ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(Bishop, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.rq & ~pos.bq & our & ~(pins.pinnedSENW | pins.pinnedSWNE);
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = 0;
        if (!(pins.pinnedWE & (1ULL << src))) sqrs |= snMoves(src, occ);
        if (!(pins.pinnedSN & (1ULL << src))) sqrs |= weMoves(src, occ);
        sqrs &= ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(Rook, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.bq & pos.rq & our;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = 0;

        if (!(anyPins & ~pins.pinnedWE & (1ULL << src))) sqrs |= weMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSN & (1ULL << src))) sqrs |= snMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSWNE & (1ULL << src))) sqrs |= swneMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSENW & (1ULL << src))) sqrs |= senwMoves(src, occ);

        sqrs &= ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(Queen, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

// Generates bishop, rook, and queen moves. Queens are handled in both the diagonal and the orthogonal pass.
// Pinned pieces are rare, so they are handled separately and the common case has no pin checks.
template<Color C>
FP_INLINE Move* generateSliders(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    // A piece on both bq and rq is a queen
    uint64_t pcs = pos.bq & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        Piece piece = static_cast<Piece>(Bishop + 2 * ((pos.rq >> src) & 1));
        uint64_t sqrs = bmoves(src, occ) & ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(piece, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    pcs = pos.rq & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        Piece piece = static_cast<Piece>(Rook + ((pos.bq >> src) & 1));
        uint64_t sqrs = rmoves(src, occ) & ~our;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(piece, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    if (anyPins & (pos.bq | pos.rq))
    {
        // A pinned piece can only move along the pin line
        struct PinnedLine { uint64_t pieces; bool diagonal; uint64_t (*moves)(unsigned long, uint64_t); };
        const PinnedLine lines[4] =
        {
            { pos.bq & pins.pinnedSWNE, true, swneMoves },
            { pos.bq & pins.pinnedSENW, true, senwMoves },
            { pos.rq & pins.pinnedWE, false, weMoves },
            { pos.rq & pins.pinnedSN, false, snMoves }
        };

        for (const PinnedLine& line : lines)
        {
            pcs = line.pieces;
            while (pcs)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                Piece piece = line.diagonal ?
                    static_cast<Piece>(Bishop + 2 * ((pos.rq >> src) & 1)) :
                    static_cast<Piece>(Rook + ((pos.bq >> src) & 1));
                uint64_t sqrs = line.moves(src, occ) & ~our;
                while (sqrs)
                {
                    dst = static_cast<unsigned long>(tzcnt64(sqrs));
                    *stack = Move(piece, src, dst);
                    ++stack;
                    sqrs &= (sqrs - 1);
                }
                pcs &= (pcs - 1);
            }
        }
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateK(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea)
{
    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.k & our;
    src = static_cast<unsigned long>(tzcnt64(pcs));
    uint64_t sqrs = tables().kmoves[src] & ~our & ~pArea;
    while (sqrs)
    {
        dst = static_cast<unsigned long>(tzcnt64(sqrs));
        *stack = Move(King, src, dst);
        ++stack;
        sqrs &= (sqrs - 1);
    }

    return stack;
}

template<Color C>
FP_INLINE Move* generateCastling(const Position& pos, Move* stack,  uint64_t occ, uint64_t pArea)
{
    if (C == White)
    {
        if (pos.state & CastlingWhiteShort)
        {
            if ((pArea & 0x7000000000000000ULL) == 0 && (occ & 0x6000000000000000ULL) == 0)
            {
                *stack = Move(King, E1, G1);
                ++stack;
            }
        }
        if (pos.state & CastlingWhiteLong)
        {
            if ((pArea & 0x1c00000000000000ULL) == 0 && (occ & 0x0e00000000000000ULL) == 0)
            {
                *stack = Move(King, E1, C1);
                ++stack;
            }
        }
    }
    else
    {
        if (pos.state & CastlingBlackShort)
        {
            if ((pArea & 0x0000000000000070ULL) == 0 && (occ & 0x0000000000000060ULL) == 0)
            {
                *stack = Move(King, E8, G8);
                ++stack;
            }
        }
        if (pos.state & CastlingBlackLong)
        {
            if ((pArea & 0x000000000000001cULL) == 0 && (occ & 0x000000000000000eULL) == 0)
            {
                *stack = Move(King, E8, C8);
                ++stack;
            }
        }
    }

    return stack;
}

// Stores a pawn move, or all four promotions if the pawn moves to the last rank
static FP_INLINE Move* storePawnMove(Move* stack, unsigned long src, unsigned long dst)
{
    if ((1ULL << dst) & 0xff000000000000ffULL)
    {
        stack[0] = Move(Pawn, src, dst, Knight);
        stack[1] = Move(Pawn, src, dst, Bishop);
        stack[2] = Move(Pawn, src, dst, Rook);
        stack[3] = Move(Pawn, src, dst, Queen);
        return stack + 4;
    }
    *stack = Move(Pawn, src, dst);
    return stack + 1;
}

// Generates check evasions: king moves, and with a single checker, moves that capture the checker or
// block the check. Pinned pieces are skipped, because they can never resolve a check.
template<Color C>
FP_INLINE Move* generateEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins)
{
    stack = generateK<C>(pos, stack, occ, pArea);

    if (checkers & (checkers - 1)) return stack; // Double check, only king moves

    unsigned long src, dst;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    uint64_t movable = our & ~anyPins;
    uint64_t empty = ~occ;

    unsigned long kingSq = static_cast<unsigned long>(tzcnt64(pos.k & our));
    unsigned long checkerSq = static_cast<unsigned long>(tzcnt64(checkers));

    // The squares between the king and a sliding checker are where their attacks intersect
    uint64_t between = 0;
    if (checkers & (pos.bq | pos.rq))
    {
        between = (tables().bishopRays[kingSq] & checkers) ?
            bmoves(kingSq, occ) & bmoves(checkerSq, occ) :
            rmoves(kingSq, occ) & rmoves(checkerSq, occ);
    }
    uint64_t targets = checkers | between;

    // Pawns
    uint64_t pawns = pos.p & movable;
    uint64_t pcs = pawns & ((C == White) ? (empty << 8) & (targets << 8) : (empty >> 8) & (targets >> 8));
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        stack = storePawnMove(stack, src, (C == White) ? src - 8 : src + 8);
        pcs &= (pcs - 1);
    }

    pcs = pawns & ((C == White) ?
        0x00ff000000000000 & (empty << 8) & (empty << 16) & (targets << 16) :
        0x000000000000ff00 & (empty >> 8) & (empty >> 16) & (targets >> 16));
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        *stack = Move(Pawn, src, (C == White) ? src - 16 : src + 16);
        ++stack;
        pcs &= (pcs - 1);
    }

    // West captures go towards the a-file and east captures towards the h-file
    pcs = pawns & 0xfefefefefefefefe & ((C == White) ? (checkers << 9) : (checkers >> 7));
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        stack = storePawnMove(stack, src, (C == White) ? src - 9 : src + 7);
        pcs &= (pcs - 1);
    }

    pcs = pawns & 0x7f7f7f7f7f7f7f7f & ((C == White) ? (checkers << 7) : (checkers >> 9));
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        stack = storePawnMove(stack, src, (C == White) ? src - 7 : src + 9);
        pcs &= (pcs - 1);
    }

    // En passant resolves the check only when the captured pawn is the checker
    if (pos.state & EPValid)
    {
        unsigned long epSq = static_cast<unsigned long>((pos.state >> 5) & 63);
        if (checkers & ((C == White) ? (1ULL << (epSq + 8)) : (1ULL << (epSq - 8))))
        {
            pcs = (C == White) ?
                (pawns & 0xfefefefefefefefe & (1ULL << (epSq + 9))) | (pawns & 0x7f7f7f7f7f7f7f7f & (1ULL << (epSq + 7))) :
                (pawns & 0xfefefefefefefefe & (1ULL << (epSq - 7))) | (pawns & 0x7f7f7f7f7f7f7f7f & (1ULL << (epSq - 9)));
            while (pcs)
            {
                src = static_cast<unsigned long>(tzcnt64(pcs));
                *stack = Move(Pawn, src, epSq);
                ++stack;
                pcs &= (pcs - 1);
            }
        }
    }

    // Knights
    pcs = pos.n & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = tables().nmoves[src] & targets;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(Knight, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    // Sliders. A piece on both bq and rq is a queen.
    pcs = pos.bq & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        Piece piece = static_cast<Piece>(Bishop + 2 * ((pos.rq >> src) & 1));
        uint64_t sqrs = bmoves(src, occ) & targets;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(piece, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    pcs = pos.rq & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        Piece piece = static_cast<Piece>(Rook + ((pos.bq >> src) & 1));
        uint64_t sqrs = rmoves(src, occ) & targets;
        while (sqrs)
        {
            dst = static_cast<unsigned long>(tzcnt64(sqrs));
            *stack = Move(piece, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    return stack;
}

// Counts check evasions, see generateEvasions
template<Color C>
FP_INLINE uint64_t countEvasions(const Position& pos, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins)
{
    unsigned long src;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    unsigned long kingSq = static_cast<unsigned long>(tzcnt64(pos.k & our));

    uint64_t count = popcnt64(tables().kmoves[kingSq] & ~our & ~pArea);

    if (checkers & (checkers - 1)) return count; // Double check, only king moves

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    uint64_t movable = our & ~anyPins;
    uint64_t empty = ~occ;

    unsigned long checkerSq = static_cast<unsigned long>(tzcnt64(checkers));

    // The squares between the king and a sliding checker are where their attacks intersect
    uint64_t between = 0;
    if (checkers & (pos.bq | pos.rq))
    {
        between = (tables().bishopRays[kingSq] & checkers) ?
            bmoves(kingSq, occ) & bmoves(checkerSq, occ) :
            rmoves(kingSq, occ) & rmoves(checkerSq, occ);
    }
    uint64_t targets = checkers | between;

    // Pawns. Promotions are four moves each, and one is counted with the other moves.
    uint64_t pawns = pos.p & movable;
    uint64_t pushingPawns = pawns & ((C == White) ? (empty << 8) & (targets << 8) : (empty >> 8) & (targets >> 8));
    uint64_t doublePushingPawns = pawns & ((C == White) ?
        0x00ff000000000000 & (empty << 8) & (empty << 16) & (targets << 16) :
        0x000000000000ff00 & (empty >> 8) & (empty >> 16) & (targets >> 16));
    uint64_t westCapturingPawns = pawns & 0xfefefefefefefefe & ((C == White) ? (checkers << 9) : (checkers >> 7));
    uint64_t eastCapturingPawns = pawns & 0x7f7f7f7f7f7f7f7f & ((C == White) ? (checkers << 7) : (checkers >> 9));

    count += popcnt64(pushingPawns) + popcnt64(doublePushingPawns) +
        popcnt64(westCapturingPawns) + popcnt64(eastCapturingPawns);

    constexpr uint64_t PromotionRank = (C == White) ? 0x000000000000ff00 : 0x00ff000000000000;
    if (pawns & PromotionRank)
    {
        count += 3 * (popcnt64(pushingPawns & PromotionRank) +
            popcnt64(westCapturingPawns & PromotionRank) +
            popcnt64(eastCapturingPawns & PromotionRank));
    }

    // En passant resolves the check only when the captured pawn is the checker
    if (pos.state & EPValid)
    {
        unsigned long epSq = static_cast<unsigned long>((pos.state >> 5) & 63);
        if (checkers & ((C == White) ? (1ULL << (epSq + 8)) : (1ULL << (epSq - 8))))
        {
            count += popcnt64((C == White) ?
                (pawns & 0xfefefefefefefefe & (1ULL << (epSq + 9))) | (pawns & 0x7f7f7f7f7f7f7f7f & (1ULL << (epSq + 7))) :
                (pawns & 0xfefefefefefefefe & (1ULL << (epSq - 7))) | (pawns & 0x7f7f7f7f7f7f7f7f & (1ULL << (epSq - 9))));
        }
    }

    // Knights
    uint64_t pcs = pos.n & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        count += popcnt64(tables().nmoves[src] & targets);
        pcs &= (pcs - 1);
    }

    // Sliders
    pcs = pos.bq & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        count += popcnt64(bmoves(src, occ) & targets);
        pcs &= (pcs - 1);
    }

    pcs = pos.rq & movable;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        count += popcnt64(rmoves(src, occ) & targets);
        pcs &= (pcs - 1);
    }

    return count;
}

// En passant captures, separate from countP because they are rare
template<Color C>
FP_HOST_DEVICE uint64_t countEP(const Position& pos, uint64_t occ, const Pins& pins);

template<>
FP_INLINE uint64_t countEP<Black>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    uint64_t EPSquare = (pos.state >> 5) & 63;
    
    uint64_t our = ~pos.w;

    // Because EP removes two pieces from the same row, horizontal pins need an extra check
    uint64_t king = pos.k & our;
    unsigned long kingSq;
    kingSq = static_cast<unsigned long>(tzcnt64(king));
    bool kingOnEPRow = (kingSq >> 3) == 4;

    uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE);
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        if (kingOnEPRow)
        {
            uint64_t left = tables().rays[src - 1].W & occ;
            uint64_t right = tables().rays[src].E & occ;

            unsigned long hit;
            if (bitScanReverse64(&hit, left) && (hit == kingSq))
            {
                if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
            else if (bitScanForward64(&hit, right) && (hit == kingSq))
            {
                if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
        }

        ++count;
        break;
    }

    pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW);
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        if (kingOnEPRow)
        {
            uint64_t left = tables().rays[src].W & occ;
            uint64_t right = tables().rays[src + 1].E & occ;

            unsigned long hit;
            if (bitScanReverse64(&hit, left) && (hit == kingSq))
            {
                if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
            else if (bitScanForward64(&hit, right) && (hit == kingSq))
            {
                if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
        }

        ++count;
        break;
    }

    return count;
}

template<>
FP_INLINE uint64_t countEP<White>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    uint64_t EPSquare = (pos.state >> 5) & 63;
    
    uint64_t our = pos.w;

    // Because EP removes two pieces from the same row, horizontal pins need an extra check
    uint64_t king = pos.k & our;
    unsigned long kingSq;
    kingSq = static_cast<unsigned long>(tzcnt64(king));
    bool kingOnEPRow = (kingSq >> 3) == 3;

    uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW);
    while (pcs) // Use while instead if to avoid goto-statement (see breaks below)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        if (kingOnEPRow)
        {
            uint64_t left = tables().rays[src - 1].W & occ;
            uint64_t right = tables().rays[src].E & occ;

            unsigned long hit;
            if (bitScanReverse64(&hit, left) && (hit == kingSq))
            {
                if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
            else if (bitScanForward64(&hit, right) && (hit == kingSq))
            {
                if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
        }

        ++count;
        break;
    }

    pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE);
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        if (kingOnEPRow)
        {
            uint64_t left = tables().rays[src].W & occ;
            uint64_t right = tables().rays[src + 1].E & occ;

            unsigned long hit;
            if (bitScanReverse64(&hit, left) && (hit == kingSq))
            {
                if (bitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
            else if (bitScanForward64(&hit, right) && (hit == kingSq))
            {
                if (bitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
            }
        }

        ++count;
        break;
    }

    return count;
}

template<Color C>
FP_INLINE uint64_t countP(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t their = occ & ~our;
    uint64_t empty = ~occ;
    uint64_t ourPawns = pos.p & our;

    // Pawns are never on the first or last rank, so only the file edges need masking.
    // West captures go towards the a-file and east captures towards the h-file.
    uint64_t unblockedPawns = ourPawns & ((C == White) ? (empty << 8) : (empty >> 8)) & (~anyPins | pins.pinnedSN);
    uint64_t doublePushPawns = unblockedPawns &
        ((C == White) ? (0x00ff000000000000 & (empty << 16)) : (0x000000000000ff00 & (empty >> 16)));
    uint64_t westCapturingPawns = ourPawns & 0xfefefefefefefefe &
        ((C == White) ? ((their << 9) & (~anyPins | pins.pinnedSENW)) : ((their >> 7) & (~anyPins | pins.pinnedSWNE)));
    uint64_t eastCapturingPawns = ourPawns & 0x7f7f7f7f7f7f7f7f &
        ((C == White) ? ((their << 7) & (~anyPins | pins.pinnedSWNE)) : ((their >> 9) & (~anyPins | pins.pinnedSENW)));

    uint64_t count = popcnt64(unblockedPawns) + popcnt64(doublePushPawns) +
        popcnt64(westCapturingPawns) + popcnt64(eastCapturingPawns);

    // Promotions (also capturing) are four moves each, and one was already counted above
    constexpr uint64_t PromotionRank = (C == White) ? 0x000000000000ff00 : 0x00ff000000000000;
    if (ourPawns & PromotionRank)
    {
        count += 3 * (popcnt64(unblockedPawns & PromotionRank) +
            popcnt64(westCapturingPawns & PromotionRank) +
            popcnt64(eastCapturingPawns & PromotionRank));
    }

    if (pos.state & EPValid)
    {
        count += countEP<C>(pos, occ, pins);
    }

    return count;
}

template<Color C>
FP_INLINE uint64_t countN(const Position& pos, uint64_t occ, uint64_t anyPins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.n & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        uint64_t sqrs = tables().nmoves[src] & ~our;
        count += popcnt64(sqrs);
        pcs &= (pcs - 1);
    }

    return count;
}

// Counts bishop, rook, and queen moves. Queens are counted in both the diagonal and the orthogonal pass.
// Pinned pieces are rare, so they are handled separately and the common case has no pin checks.
template<Color C>
FP_INLINE uint64_t countSliders(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    uint64_t pcs = pos.bq & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        count += popcnt64(bmoves(src, occ) & ~our);
        pcs &= (pcs - 1);
    }

    pcs = pos.rq & our & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        count += popcnt64(rmoves(src, occ) & ~our);
        pcs &= (pcs - 1);
    }

    if (anyPins & (pos.bq | pos.rq))
    {
        // A pinned piece can only move along the pin line
        pcs = pos.bq & pins.pinnedSWNE;
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            count += popcnt64(swneMoves(src, occ) & ~our);
            pcs &= (pcs - 1);
        }

        pcs = pos.bq & pins.pinnedSENW;
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            count += popcnt64(senwMoves(src, occ) & ~our);
            pcs &= (pcs - 1);
        }

        pcs = pos.rq & pins.pinnedWE;
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            count += popcnt64(weMoves(src, occ) & ~our);
            pcs &= (pcs - 1);
        }

        pcs = pos.rq & pins.pinnedSN;
        while (pcs)
        {
            src = static_cast<unsigned long>(tzcnt64(pcs));
            count += popcnt64(snMoves(src, occ) & ~our);
            pcs &= (pcs - 1);
        }
    }

    return count;
}

template<Color C>
FP_INLINE uint64_t countK(const Position& pos, uint64_t occ, uint64_t pArea)
{
    uint64_t our = (C == White) ? pos.w : occ & ~pos.w;
    unsigned long src = static_cast<unsigned long>(tzcnt64(pos.k & our));
    uint64_t sqrs = tables().kmoves[src] & ~our & ~pArea;

    return popcnt64(sqrs);
}

// Counts castlings. The king's start square is included in the protection area check, so this
// also verifies that the king is not in check.
template<Color C>
FP_INLINE uint64_t countCastling(const Position& pos, uint64_t occ, uint64_t pArea)
{
    uint64_t count = 0;

    constexpr uint64_t ShortCastling = (C == White) ? CastlingWhiteShort : CastlingBlackShort;
    constexpr uint64_t LongCastling = (C == White) ? CastlingWhiteLong : CastlingBlackLong;
    constexpr uint64_t ShortKingPath = (C == White) ? 0x7000000000000000ULL : 0x0000000000000070ULL;
    constexpr uint64_t ShortEmpty = (C == White) ? 0x6000000000000000ULL : 0x0000000000000060ULL;
    constexpr uint64_t LongKingPath = (C == White) ? 0x1c00000000000000ULL : 0x000000000000001cULL;
    constexpr uint64_t LongEmpty = (C == White) ? 0x0e00000000000000ULL : 0x000000000000000eULL;

    if (pos.state & ShortCastling)
    {
        if ((pArea & ShortKingPath) == 0 && (occ & ShortEmpty) == 0)
        {
            ++count;
        }
    }
    if (pos.state & LongCastling)
    {
        if ((pArea & LongKingPath) == 0 && (occ & LongEmpty) == 0)
        {
            ++count;
        }
    }

    return count;
}



FP_INLINE uint64_t swneMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (tables().swneExMask[src] & occ) * bFile >> 58;
    return tables().swneExMask[src] & tables().KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t bRays = 0;

    hit = static_cast<unsigned long>(tzcnt64((tables().rays[src].SW & occ) | 0x8000000000000000));
    bRays |= tables().rays[src].SW;
    bRays ^= tables().rays[hit].SW;

    bitScanReverse64(&hit, (tables().rays[src].NE & occ) | 0x0000000000000001);
    bRays |= tables().rays[src].NE;
    bRays ^= tables().rays[hit].NE;

    return bRays;
#endif
}

FP_INLINE uint64_t senwMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (tables().senwExMask[src] & occ) * bFile >> 58;
    return tables().senwExMask[src] & tables().KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t bRays = 0;

    hit = static_cast<unsigned long>(tzcnt64((tables().rays[src].SE & occ) | 0x8000000000000000));
    bRays |= tables().rays[src].SE;
    bRays ^= tables().rays[hit].SE;

    bitScanReverse64(&hit, (tables().rays[src].NW & occ) | 0x0000000000000001);
    bRays |= tables().rays[src].NW;
    bRays ^= tables().rays[hit].NW;

    return bRays;
#endif
}

FP_INLINE uint64_t bmoves(unsigned long src, uint64_t occ)
{
#if PEXT_INTRINSIC && !defined(__CUDA_ARCH__)
    uint64_t mask = BMasks[src];
    uint64_t index = _pext_u64(occ, mask);
    uint64_t moves = BAttacks[src][index];
    return moves;
#elif MAGIC_BITBOARDS && !defined(__CUDA_ARCH__)
    uint64_t index = occ & BMagic[src].mask;
    index *= BMagic[src].magic;
    index >>= BBits[src];
    uint64_t moves = BAttacks[src][index];
    return moves;
#else
    return swneMoves(src, occ) | senwMoves(src, occ);
#endif
}

FP_INLINE uint64_t weMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (tables().weExMask[src] & occ) * bFile >> 58;
    return tables().weExMask[src] & tables().KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t rRays = 0;

    hit = static_cast<unsigned long>(tzcnt64((tables().rays[src].E & occ) | 0x8000000000000000));
    rRays |= tables().rays[src].E;
    rRays ^= tables().rays[hit].E;

    bitScanReverse64(&hit, (tables().rays[src].W & occ) | 0x0000000000000001);
    rRays |= tables().rays[src].W;
    rRays ^= tables().rays[hit].W;

    return rRays;
#endif
}

FP_INLINE uint64_t snMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t AFile = 0x0101010101010101ULL;
    const uint64_t c7h2 = 0x0080402010080400ULL;
    uint64_t index = AFile & (occ >> (src & 7));
    index = (c7h2 * index) >> 58;
    return tables().AFileAttacks[src >> 3][index] << (src & 7);
#else
    unsigned long hit;
    uint64_t rRays = 0;

    hit = static_cast<unsigned long>(tzcnt64((tables().rays[src].S & occ) | 0x8000000000000000));
    rRays |= tables().rays[src].S;
    rRays ^= tables().rays[hit].S;

    bitScanReverse64(&hit, (tables().rays[src].N & occ) | 0x0000000000000001);
    rRays |= tables().rays[src].N;
    rRays ^= tables().rays[hit].N;

    return rRays;
#endif
}

FP_INLINE uint64_t rmoves(unsigned long src, uint64_t occ)
{
#if PEXT_INTRINSIC && !defined(__CUDA_ARCH__)
    uint64_t mask = RMasks[src];
    uint64_t index = _pext_u64(occ, mask);
    uint64_t moves = RAttacks[src][index];
    return moves;
#elif MAGIC_BITBOARDS && !defined(__CUDA_ARCH__)
    const Magic& m = RMagic[src];
    uint64_t index = occ & m.mask;
    index *= m.magic;
    index >>= m.shift;//RBits[src];
    uint64_t moves = m.ptr[index];//RAttacks[src][index];
    return moves;
#else
    return weMoves(src, occ) | snMoves(src, occ);
#endif
}

// Pins and checks against the king of C
template<Color C>
FP_INLINE uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins)
{
    memset(&pins, 0, sizeof(Pins));

    uint64_t our, king;
    uint64_t pawnsLeft, pawnsRight;
    if (C == White)
    {
        our = pos.w;
        king = pos.k & pos.w;
        pawnsLeft = king >> 9;
        pawnsRight = king >> 7;
    }
    else
    {
        our = ~pos.w;
        king = pos.k & ~pos.w;
        pawnsLeft = king << 7;
        pawnsRight = king << 9;
    }
    
    uint64_t their = ~our;    

    unsigned long src;
    src = static_cast<unsigned long>(tzcnt64(king));

    uint64_t checkers = 0;
        
    checkers |= pos.p & their & 0x7f7f7f7f7f7f7f7fULL & pawnsLeft;
    checkers |= pos.p & their & 0xfefefefefefefefeULL & pawnsRight;    

    checkers |= pos.n & their & tables().nmoves[src];

    // Sliding pieces - handle pins and checks at once
    uint64_t bPcs = pos.bq & their;
    uint64_t rPcs = pos.rq & their;

    unsigned long hit1, hit2;
    uint64_t ray = tables().rays[src].SE;
    uint64_t attackers = ray & bPcs;
    if (attackers)
    {        
        ray &= occ;
        hit1 = static_cast<unsigned long>(tzcnt64(ray));
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        hit2 = static_cast<unsigned long>(tzcnt64(ray));
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSENW |= potentialPinned;
    }

    ray = tables().rays[src].NW;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        bitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        bitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSENW |= potentialPinned;
    }

    ray = tables().rays[src].SW;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        hit1 = static_cast<unsigned long>(tzcnt64(ray));
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        hit2 = static_cast<unsigned long>(tzcnt64(ray));
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSWNE |= potentialPinned;
    }

    ray = tables().rays[src].NE;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        bitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        bitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSWNE |= potentialPinned;
    }

    ray = tables().rays[src].S;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        hit1 = static_cast<unsigned long>(tzcnt64(ray));
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        hit2 = static_cast<unsigned long>(tzcnt64(ray));
        if ((1ULL << hit2) & rPcs)
            pins.pinnedSN |= potentialPinned;
    }

    ray = tables().rays[src].N;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        bitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        bitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedSN |= potentialPinned;
    }

    ray = tables().rays[src].W;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        bitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        bitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedWE |= potentialPinned;
    }

    ray = tables().rays[src].E;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        hit1 = static_cast<unsigned long>(tzcnt64(ray));
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        hit2 = static_cast<unsigned long>(tzcnt64(ray));
        if ((1ULL << hit2) & rPcs)
            pins.pinnedWE |= potentialPinned;
    }

    // King cannot pin or check

    return checkers;
}

// Squares attacked by the opponent of C
template<Color C>
FP_INLINE uint64_t findProtectionArea(const Position& pos, uint64_t occ)
{
    unsigned long src;
    uint64_t pArea = 0;

    uint64_t their = (C == White) ? ~pos.w : pos.w;
    if (C == White)
    {
        pArea |= ((pos.p & their) & 0xfefefefefefefefe) << 7;
        pArea |= ((pos.p & their) & 0x7f7f7f7f7f7f7f7f) << 9;
    }
    else
    {
        pArea |= ((pos.p & their) & 0xfefefefefefefefe) >> 9;
        pArea |= ((pos.p & their) & 0x7f7f7f7f7f7f7f7f) >> 7;
    }     
    
    uint64_t pcs = pos.n & their;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        pArea |= tables().nmoves[src];
        pcs &= (pcs - 1);
    }    

    occ ^= (pos.k & ~their); // King doesn't block the sliding pieces' protection area

    pcs = pos.bq & their;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        pcs &= (pcs - 1);
        pArea |= bmoves(src, occ);
    }

    pcs = pos.rq & their;
    while (pcs)
    {
        src = static_cast<unsigned long>(tzcnt64(pcs));
        pcs &= (pcs - 1);
        pArea |= rmoves(src, occ);
    }

    pcs = pos.k & their;
    src = static_cast<unsigned long>(tzcnt64(pcs));
    pArea |= tables().kmoves[src];

    return pArea;
}
