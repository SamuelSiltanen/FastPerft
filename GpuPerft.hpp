// Copyright 2022 Samuel Siltanen
// GpuPerft.hpp

#pragma once

#include "ChessTypes.hpp"

#include <cstdint>

// Perft where the CPU expands the tree down to the last plies, and the GPU searches the remaining subtrees.
// Doesn't use the hash table.
bool initGpuPerft();
uint64_t runGpuPerft(const Position& pos, int depth);
void releaseGpuPerft();
