// Copyright 2022 Samuel Siltanen
// Stats.cpp

#include "Stats.hpp"
#include "Make.hpp"
#include "MoveGeneration.hpp"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <thread>
#include <vector>

constexpr int StatsMoveStackSize = 1024 * 8;

PerftStats& PerftStats::operator+=(const PerftStats& other)
{
    nodes += other.nodes;
    captures += other.captures;
    enPassants += other.enPassants;
    castles += other.castles;
    promotions += other.promotions;
    checks += other.checks;
    discoveryChecks += other.discoveryChecks;
    doubleChecks += other.doubleChecks;
    checkmates += other.checkmates;
    return *this;
}

template<Color C>
static Move* generateMoves(const Position& pos, Move* stack)
{
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

    if (checkers)
    {
        return generateEvasions<C>(pos, stack, occ, pArea, checkers, pins);
    }

    stack = generateP<C>(pos, stack, occ, pins);
    stack = generateN<C>(pos, stack, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
    stack = generateSliders<C>(pos, stack, occ, pins);
    stack = generateK<C>(pos, stack, occ, pArea);
    stack = generateCastling<C>(pos, stack, occ, pArea);
    return stack;
}

// Classifies a leaf move made by C
template<Color C>
static void addLeafStats(const Position& pos, const Move& move, PerftStats& stats)
{
    constexpr Color Opponent = (C == White) ? Black : White;

    unsigned long src = move.src();
    unsigned long dst = move.dst();
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    bool enPassant = move.piece() == Pawn && (pos.state & EPValid) && dst == ((pos.state >> 5) & 63);
    int distance = static_cast<int>(dst) - static_cast<int>(src);
    bool castling = move.piece() == King && (distance == 2 || distance == -2);

    stats.nodes++;
    if ((occ & (1ULL << dst)) || enPassant) stats.captures++;
    if (enPassant) stats.enPassants++;
    if (castling) stats.castles++;
    if (move.prom() != None) stats.promotions++;

    Position next = make<C>(pos, move);
    uint64_t nextOcc = next.p | next.n | next.bq | next.rq | next.k;
    Pins pins;
    uint64_t checkers = findPinsAndCheckers<Opponent>(next, nextOcc, pins);
    if (checkers)
    {
        stats.checks++;

        // Discovered check: a piece other than the moved one gives check. Double checks are counted
        // separately, and a check by the rook in castling is not counted as discovered.
        if (checkers & (checkers - 1)) stats.doubleChecks++;
        else if ((checkers & ~(1ULL << dst)) && !castling) stats.discoveryChecks++;

        uint64_t pArea = findProtectionArea<Opponent>(next, nextOcc);
        if (countEvasions<Opponent>(next, nextOcc, pArea, checkers, pins) == 0) stats.checkmates++;
    }
}

template<Color C>
static void searchStats(const Position& pos, int depth, Move* stack, PerftStats& stats)
{
    constexpr Color Opponent = (C == White) ? Black : White;

    Move* end = generateMoves<C>(pos, stack);

    for (Move* move = stack; move < end; ++move)
    {
        if (depth == 1)
        {
            addLeafStats<C>(pos, *move, stats);
        }
        else
        {
            searchStats<Opponent>(make<C>(pos, *move), depth - 1, end, stats);
        }
    }
}

template<Color C>
static PerftStats perftStatsRoot(const Position& pos, int depth, int numWorkers)
{
    PerftStats total;
    if (depth <= 0)
    {
        total.nodes = 1;
        return total;
    }

    std::vector<Move> rootMoves(StatsMoveStackSize);
    Move* end = generateMoves<C>(pos, rootMoves.data());
    size_t numMoves = end - rootMoves.data();

    if (depth == 1)
    {
        for (size_t i = 0; i < numMoves; ++i) addLeafStats<C>(pos, rootMoves[i], total);
        return total;
    }

    // Each worker takes the next root move until all have been searched
    size_t numThreads = (numWorkers < 1) ? 1 : static_cast<size_t>(numWorkers);
    if (numThreads > numMoves) numThreads = (numMoves > 0) ? numMoves : 1;

    std::atomic<size_t> nextMove(0);
    std::vector<PerftStats> threadStats(numThreads);
    std::vector<std::thread> threads;
    for (size_t t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&, t]()
        {
            std::vector<Move> stack(StatsMoveStackSize);
            for (size_t i = nextMove++; i < numMoves; i = nextMove++)
            {
                searchStats<(C == White) ? Black : White>(make<C>(pos, rootMoves[i]), depth - 1, stack.data(), threadStats[t]);
            }
        });
    }

    for (size_t t = 0; t < numThreads; ++t)
    {
        threads[t].join();
        total += threadStats[t];
    }

    return total;
}

PerftStats perftStats(const Position& pos, int depth, int numWorkers)
{
    return (pos.state & TurnWhite) ? perftStatsRoot<White>(pos, depth, numWorkers) : perftStatsRoot<Black>(pos, depth, numWorkers);
}

void printStats(const PerftStats& stats)
{
    printf("Captures = %" PRIu64 "\n", stats.captures);
    printf("E.p. = %" PRIu64 "\n", stats.enPassants);
    printf("Castles = %" PRIu64 "\n", stats.castles);
    printf("Promotions = %" PRIu64 "\n", stats.promotions);
    printf("Checks = %" PRIu64 "\n", stats.checks);
    printf("Discovery checks = %" PRIu64 "\n", stats.discoveryChecks);
    printf("Double checks = %" PRIu64 "\n", stats.doubleChecks);
    printf("Checkmates = %" PRIu64 "\n", stats.checkmates);
}
