// Copyright 2022 Samuel Siltanen
// Perft.cpp

#include "Perft.hpp"
#include "Config.hpp"
#include "Make.hpp"
#include "MoveGeneration.hpp"
#ifdef COLLECT_STATS
#include "Stats.hpp"
#endif
#ifdef HASH_TABLE
#include "HashTable.hpp"
#endif
#ifdef MULTITHREADED
#include "WorkQueue.hpp"
#endif
#include <random>

uint64_t perft(const Position& pos, int depth, Move* stack)
{
    const Move* stack0 = stack;

#ifndef LEAF_NODE_BULK_COUNT
    if (depth == 0) return 1;
#endif

#ifdef HASH_TABLE
    if (depth >= MinHashDepth) // Don't probe at last levels, because memory access is slower than calculation
    {
        uint64_t entry = hashTable->find(pos, depth);
#ifdef COLLECT_STATS
        statsHashProbes++;
#endif
        if (entry != InvalidHashTableEntry)
        {
#ifdef COLLECT_STATS
            statsHashHits++;
#endif
            return entry;
        }
    }
#endif

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    BishopPins bPins;
    RookPins rPins;

    uint64_t checkers = findPinsAndCheckers(pos, occ, bPins, rPins);
    uint64_t pArea = findProtectionArea(pos, occ);    

#ifdef LEAF_NODE_BULK_COUNT
    if (depth == 1)
    {
        uint64_t count = 0;

        if (checkers)
        {
            count = countCheckEvasions(pos, occ, pArea, checkers, bPins, rPins);
            if (stack == stack0)
            {
#ifdef COLLECT_STATS
                statsCheckmates++;
#endif
            }
        }
        else
        {
            count += countP(pos, occ, bPins, rPins);
            count += countN(pos, occ, bPins.pinnedSENW | bPins.pinnedSWNE | rPins.pinnedSN | rPins.pinnedWE);
            count += countB(pos, occ, bPins, rPins);
            count += countR(pos, occ, bPins, rPins);
            count += countQ(pos, occ, bPins, rPins);
            count += countK(pos, occ, pArea);
            count += countCastling(pos, occ, pArea);
        }
        
#ifdef HASH_TABLE
        if (1 >= MinHashDepth)
        {
#ifdef COLLECT_STATS
            statsHashWriteTries++;
#endif
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
#ifdef COLLECT_STATS
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
            stack = generateCheckEvasions(pos, stack, occ, pArea, checkers, bPins, rPins);
            if (stack == stack0)
            {
#ifdef COLLECT_STATS
                statsCheckmates++;
#endif
            }
        }
        else
        {
            stack = generateP(pos, stack, occ, bPins, rPins);
            stack = generateN(pos, stack, occ, bPins.pinnedSENW | bPins.pinnedSWNE | rPins.pinnedSN | rPins.pinnedWE);
            stack = generateB(pos, stack, occ, bPins, rPins);
            stack = generateR(pos, stack, occ, bPins, rPins);
            stack = generateQ(pos, stack, occ, bPins, rPins);
            stack = generateK(pos, stack, occ, pArea);
            stack = generateCastling(pos, stack, occ, pArea);
        }

        uint64_t count = 0;

        for (--stack; stack >= stack0; --stack)
        {
            const Move& move = *stack;
            Position tmpPos = make(pos, move);
            count += perft(tmpPos, depth - 1, stack);
        }

#ifdef HASH_TABLE
        if (depth >= MinHashDepth)
        {
#ifdef COLLECT_STATS
            statsHashWriteTries++;
#endif
            if (hashTable->insert({ pos, static_cast<uint16_t>(depth), count }))
            {
#ifdef COLLECT_STATS
                statsHashWrites++;
#endif
            }
        }
#endif

        return count;
    }
}

#ifdef MULTITHREADED

constexpr size_t MaxWorkQueueSize = 256;
constexpr int MaxMoveStackSize = 1024 * 8;
constexpr int NumWorkerThreads = 8;
constexpr int MinWorkItemDepth = 4;

WorkQueue* workQueue[NumWorkerThreads];
RunState runState;
Move* threadLocalStack[NumWorkerThreads];
std::thread* worker[NumWorkerThreads];

void worker_loop(int threadIndex)
{
    std::mt19937 gen(0x12345678 + threadIndex);
    std::uniform_int_distribution<int> dist(0, NumWorkerThreads - 2);

    while (runState != RunState::Exiting)
    {
        if (runState == RunState::Initializing)
        {
            std::this_thread::yield();
            continue;
        }

        WorkItem item;
        if (workQueue[threadIndex]->try_pop_front(item))
        {
            item.result->count += perftMultithreaded(item.pos, item.depth, threadLocalStack[threadIndex], threadIndex);
            item.result->workLeft--;
        }
        else
        {

            int stealIndex = dist(gen);
            if (stealIndex >= threadIndex) stealIndex++;

            if (workQueue[stealIndex]->try_pop_front(item))
            {
                item.result->count += perftMultithreaded(item.pos, item.depth, threadLocalStack[threadIndex], threadIndex);
                item.result->workLeft--;
            }
            else

            {
                std::this_thread::yield();
            }
        }
    }
}

uint64_t perftMultithreaded(const Position& pos, int depth, Move* stack, int threadIndex)
{
    const Move* stack0 = stack;

#ifndef LEAF_NODE_BULK_COUNT
    if (depth == 0) return 1;
#endif

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    BishopPins bPins;
    RookPins rPins;

    uint64_t checkers = findPinsAndCheckers(pos, occ, bPins, rPins);
    uint64_t pArea = findProtectionArea(pos, occ);

    if (checkers)
    {
        stack = generateCheckEvasions(pos, stack, occ, pArea, checkers, bPins, rPins);
        if (stack == stack0)
        {
#ifdef COLLECT_STATS
            statsCheckmates++;
#endif
        }
    }
    else
    {
        stack = generateP(pos, stack, occ, bPins, rPins);
        stack = generateN(pos, stack, occ, bPins.pinnedSENW | bPins.pinnedSWNE | rPins.pinnedSN | rPins.pinnedWE);
        stack = generateB(pos, stack, occ, bPins, rPins);
        stack = generateR(pos, stack, occ, bPins, rPins);
        stack = generateQ(pos, stack, occ, bPins, rPins);
        stack = generateK(pos, stack, occ, pArea);
        stack = generateCastling(pos, stack, occ, pArea);
    }

#ifdef LEAF_NODE_BULK_COUNT
    if (depth == 1)
    {
        return static_cast<uint64_t>(stack - stack0);
    }
    else
#endif
    {
        if (depth > MinWorkItemDepth)
        {
            WorkResult result = { 0, 0 };
            workQueue[threadIndex]->lock();
            size_t marker = workQueue[threadIndex]->marker();
            for (--stack; stack >= stack0; --stack)
            {
                const Move& move = *stack;
                Position tmpPos = make(pos, move);
                WorkItem perftItem = { tmpPos, depth - 1, &result };
                workQueue[threadIndex]->push_front_unsafe(perftItem);
            }
            workQueue[threadIndex]->unlock();

            WorkItem item;
            while (workQueue[threadIndex]->try_pop_front(item, marker))
            {
                assert(item.result == &result);

                item.result->count += perftMultithreaded(item.pos, item.depth, threadLocalStack[threadIndex], threadIndex);
                item.result->workLeft--;
            }

            // There might be someone else still working on this work list
            bool once = true;
            while (result.workLeft)
            {
                if (once)
                {
                    int workLeft = result.workLeft;
                    once = false;
                }
                std::this_thread::yield();
            }

            return result.count;
        }
        else
        {
            uint64_t count = 0;

            for (--stack; stack >= stack0; --stack)
            {
                const Move& move = *stack;
                Position tmpPos = make(pos, move);
                count += perft(tmpPos, depth - 1, stack);
            }

            return count;
        }
    }
}

void initMultiPerft()
{
    runState = RunState::Initializing;
    for (int i = 0; i < NumWorkerThreads; i++)
    {
        threadLocalStack[i] = new Move[MaxMoveStackSize];
        workQueue[i] = new WorkQueue(MaxWorkQueueSize);
        worker[i] = new std::thread(worker_loop, i);
    }
}

uint64_t runMultiPerft(const Position& pos, int depth)
{
    WorkResult result = { 0, 0 };
    WorkItem item = { pos, depth, &result };
    workQueue[0]->push_back(item);

    runState = RunState::Running;

    while (result.workLeft)
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(5ms);
    }

    return result.count;
}

void releaseMultiPerft()
{
    runState = RunState::Exiting;
    for (int i = 0; i < NumWorkerThreads; i++)
    {
        worker[i]->join();
        delete worker[i];

        if (workQueue[i])
        {
            delete workQueue[i];
        }
        if (threadLocalStack[i])
        {
            delete[] threadLocalStack[i];
        }
    }
}

#endif
