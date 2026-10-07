// Copyright 2022 Samuel Siltanen
// Perft.hpp

#pragma once

#include "ChessTypes.hpp"
#include "Config.hpp"
#include "Make.hpp"
#include "MoveGeneration.hpp"
#if HASH_TABLE
#include "HashTable.hpp"
#endif

template<Color C>
uint64_t perft(const Position& pos, int depth, Move* stack)
{
    const Move* stack0 = stack;

#if !LEAF_NODE_BULK_COUNT
    if (depth == 0) return 1;
#endif

#if HASH_TABLE
    if (hashTable && depth >= MinHashDepth) // Don't probe at last levels, because memory access is slower than calculation
    {
        uint64_t entry = hashTable->find(pos, depth);
        if (entry != InvalidHashTableEntry)
        {
            return entry;
}
    }
#endif

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;

    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

#if LEAF_NODE_BULK_COUNT
    if (depth == 1)
    {
        uint64_t count = 0;

        if (checkers)
        {
            count = countEvasions<C>(pos, occ, pArea, checkers, pins);
        }
        else
        {
            count += countP<C>(pos, occ, pins);
            count += countN<C>(pos, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
            count += countSliders<C>(pos, occ, pins);
            count += countK<C>(pos, occ, pArea);
            count += countCastling<C>(pos, occ, pArea);
        }

#if HASH_TABLE
        if (hashTable && 1 >= MinHashDepth)
        {
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
            }
        }
#endif
        return count;
    }
    else
#endif
    {
        if (checkers)
        {
            stack = generateEvasions<C>(pos, stack, occ, pArea, checkers, pins);
        }
        else
        {
            stack = generateP<C>(pos, stack, occ, pins);
            stack = generateN<C>(pos, stack, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
            stack = generateSliders<C>(pos, stack, occ, pins);
            stack = generateK<C>(pos, stack, occ, pArea);
            stack = generateCastling<C>(pos, stack, occ, pArea);
        }

        uint64_t count = 0;

        for (--stack; stack >= stack0; --stack)
        {
            const Move& move = *stack;
            Position tmpPos = make<C>(pos, move);
            count += perft<1 - C>(tmpPos, depth - 1, stack);
        }

#if HASH_TABLE
        if (hashTable && depth >= MinHashDepth)
        {
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
            }
        }
#endif

        return count;
    }
}

#if MULTITHREADED
enum class RunState
{
    Initializing,
    Running,
    Exiting
};

extern RunState runState;

constexpr int MaxWorkerThreads = 64;

void initMultiPerft(int numWorkers);
uint64_t runMultiPerft(const Position& pos, int depth);
void releaseMultiPerft();

uint64_t perftMultithreaded(const Position& pos, int depth, Move* stack, int threadIndex);
#endif
