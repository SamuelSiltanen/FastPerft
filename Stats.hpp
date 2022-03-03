// Copyright 2022 Samuel Siltanen
// Stats.hpp

#pragma once

#include "Config.hpp"

#ifdef COLLECT_STATS

#include <atomic>

extern std::atomic<int> statsCaptures;
extern std::atomic<int> statsEPs;
extern std::atomic<int> statsCastles;
extern std::atomic<int> statsCheckmates;
extern std::atomic<int> statsHashProbes;
extern std::atomic<int> statsHashHits;
extern std::atomic<int> statsHashWriteTries;
extern std::atomic<int> statsHashWrites;

void resetStats();
void printStats(uint64_t count);

#endif
