// Copyright 2022 Samuel Siltanen
// Perft.cpp

#include "Perft.hpp"

#if MULTITHREADED
#include "WorkQueue.hpp"
#endif
#include <cassert>
#include <random>

#if MULTITHREADED

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

#if !LEAF_NODE_BULK_COUNT
    if (depth == 0) return 1;
#endif

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;

    uint64_t checkers = findPinsAndCheckers(pos, occ, pins);
    uint64_t pArea = findProtectionArea(pos, occ);

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

#if LEAF_NODE_BULK_COUNT
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
