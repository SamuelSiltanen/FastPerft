// Copyright 2022 Samuel Siltanen
// Stats.hpp

#pragma once

#include "ChessTypes.hpp"

// Statistics of the moves at the last level of the search tree (the leaf nodes),
// as in https://www.chessprogramming.org/Perft_Results
struct PerftStats
{
    uint64_t nodes = 0;
    uint64_t captures = 0;
    uint64_t enPassants = 0;
    uint64_t castles = 0;
    uint64_t promotions = 0;
    uint64_t checks = 0;
    uint64_t discoveryChecks = 0;
    uint64_t doubleChecks = 0;
    uint64_t checkmates = 0;

    PerftStats& operator+=(const PerftStats& other);
};

// Counts the leaf nodes and their stats. Unlike perft, this makes all the moves at the last level,
// so it is slower. The moves at the root are divided between the worker threads.
PerftStats perftStats(const Position& pos, int depth, int numWorkers);

void printStats(const PerftStats& stats);
