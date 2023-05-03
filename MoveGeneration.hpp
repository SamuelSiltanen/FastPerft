// Copyright 2022 Samuel Siltanen
// MoveGeneration.hpp

#pragma once

#include "ChessTypes.hpp"

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
extern Rays rays[64];

// Initialize lookup tables
void fillMoveTables();

// Move generation, store moves in move stack
Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);
Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateK(const Position& pos, Move* stack, uint64_t occ, const uint64_t pArea);
Move* generateCastling(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
Move* generateMovesTo(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins);
Move* generateMovesInBetween(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins);
Move* generateCheckEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);

// Count moves, but don't store them
template<Color> uint64_t countP(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> uint64_t countN(const Position& pos, uint64_t occ, uint64_t anyPins);
template<Color> uint64_t countB(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> uint64_t countR(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> uint64_t countQ(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> uint64_t countK(const Position& pos, uint64_t occ, const uint64_t pArea);
template<Color> uint64_t countCastling(const Position& pos, uint64_t occ, uint64_t pArea);
template<Color> uint64_t countMovesTo(const Position& pos, unsigned long dst, uint64_t occ, const Pins& pins);

template<Color C>
uint64_t countMovesInBetween(const Position& pos, unsigned long dst, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    uint64_t our = (pos.state & TurnWhite) ? pos.w : ~pos.w;
    uint64_t king = pos.k & our;
    unsigned long kingSq;
    _BitScanForward64(&kingSq, king);

    if (rays[kingSq].N & (1ULL << dst))
    {
        for (int i = kingSq - 8; i > dst; i -= 8)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].S & (1ULL << dst))
    {
        for (int i = kingSq + 8; i < dst; i += 8)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].W & (1ULL << dst))
    {
        for (int i = kingSq - 1; i > dst; i--)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].E & (1ULL << dst))
    {
        for (int i = kingSq + 1; i < dst; i++)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].SW & (1ULL << dst))
    {
        for (int i = kingSq + 7; i < dst; i += 7)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].NW & (1ULL << dst))
    {
        for (int i = kingSq - 9; i > dst; i -= 9)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].NE & (1ULL << dst))
    {
        for (int i = kingSq - 7; i > dst; i -= 7)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }
    else if (rays[kingSq].SE & (1ULL << dst))
    {
        for (int i = kingSq + 9; i < dst; i += 9)
        {
            count += countMovesTo<C>(pos, i, occ, pins);
        }
    }

    return count;
}

template<Color C>
uint64_t countCheckEvasions(const Position& pos, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins)
{
    uint64_t count = 0;

    count += countK<C>(pos, occ, pArea);

    unsigned long dst;
    _BitScanForward64(&dst, checkers);
    checkers ^= (1ULL << dst);

    if (!checkers)
    {
        count += countMovesTo<C>(pos, dst, occ, pins);

        if ((1ULL << dst) & (pos.bq | pos.rq))
        {
            count += countMovesInBetween<C>(pos, dst, occ, pins);
        }
    }

    return count;
}

// Helpers
uint64_t swneMoves(unsigned long src, uint64_t occ);
uint64_t senwMoves(unsigned long src, uint64_t occ);
uint64_t bmoves(unsigned long src, uint64_t occ);
uint64_t weMoves(unsigned long src, uint64_t occ);
uint64_t snMoves(unsigned long src, uint64_t occ);
uint64_t rmoves(unsigned long src, uint64_t occ);
uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins);
uint64_t findProtectionArea(const Position& pos, uint64_t occ);
