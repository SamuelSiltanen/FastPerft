// Copyright 2022 Samuel Siltanen
// Perft.hpp

#pragma once

#include "ChessTypes.hpp"
#include "Config.hpp"
#include "Make.hpp"
#include "MoveGeneration.hpp"
#if COLLECT_STATS
#include "Stats.hpp"
#endif
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
    if (depth >= MinHashDepth) // Don't probe at last levels, because memory access is slower than calculation
    {
        uint64_t entry = hashTable->find(pos, depth);
#if COLLECT_STATS
        statsHashProbes++;
#endif
        if (entry != InvalidHashTableEntry)
        {
#if COLLECT_STATS
            statsHashHits++;
#endif
            return entry;
}
    }
#endif

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;

    uint64_t checkers = findPinsAndCheckers(pos, occ, pins);
    uint64_t pArea = findProtectionArea(pos, occ);

#if LEAF_NODE_BULK_COUNT
    if (depth == 1)
    {
        uint64_t count = 0;

        if (checkers)
        {
            count = countCheckEvasions<C>(pos, occ, pArea, checkers, pins);
            if (stack == stack0)
            {
#if COLLECT_STATS
                statsCheckmates++;
#endif
            }
        }
        else
        {
            count += countP<C>(pos, occ, pins);
            count += countN<C>(pos, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
            count += countB<C>(pos, occ, pins);
            count += countR<C>(pos, occ, pins);
            count += countQ<C>(pos, occ, pins);
            count += countK<C>(pos, occ, pArea);
            count += countCastling<C>(pos, occ, pArea);
        }

#if HASH_TABLE
        if (1 >= MinHashDepth)
        {
#if COLLECT_STATS
            statsHashWriteTries++;
#endif
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
#if COLLECT_STATS
                statsHashWrites++;
#endif
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
            stack = generateCheckEvasions(pos, stack, occ, pArea, checkers, pins);
            if (stack == stack0)
            {
#if COLLECT_STATS
                statsCheckmates++;
#endif
            }
        }
        else
        {
            stack = generateP(pos, stack, occ, pins);
            stack = generateN(pos, stack, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
            stack = generateB(pos, stack, occ, pins);
            stack = generateR(pos, stack, occ, pins);
            stack = generateQ(pos, stack, occ, pins);
            stack = generateK(pos, stack, occ, pArea);
            stack = generateCastling(pos, stack, occ, pArea);
        }

        uint64_t count = 0;

        for (--stack; stack >= stack0; --stack)
        {
            const Move& move = *stack;
            Position tmpPos = make(pos, move);
            count += perft<1 - C>(tmpPos, depth - 1, stack);
        }

#if HASH_TABLE
        if (depth >= MinHashDepth)
        {
#if COLLECT_STATS
            statsHashWriteTries++;
#endif
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
#if COLLECT_STATS
                statsHashWrites++;
#endif
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

void initMultiPerft();
uint64_t runMultiPerft(const Position& pos, int depth);
void releaseMultiPerft();

uint64_t perftMultithreaded(const Position& pos, int depth, Move* stack, int threadIndex);
#endif
