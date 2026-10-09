// Copyright 2022 Samuel Siltanen
// GpuPerft.hpp

#pragma once

#include "ChessTypes.hpp"

#include <cstdint>

// Perft where the CPU expands the tree down to the last plies, and the GPU searches the remaining subtrees.
// Doesn't use the hash table.
bool initGpuPerft();
uint64_t runGpuPerft(const Position& pos, int depth);
// Builds the unique positions after splitDepth plies with the number of move sequences to each, and searches each
// unique position once on the GPU. Prints the number of unique positions and the CPU and GPU times.
uint64_t runGpuPerftSplit(const Position& pos, int depth, int splitDepth);
void releaseGpuPerft();
