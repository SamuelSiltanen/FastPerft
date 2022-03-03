#pragma once

#include "ChessTypes.hpp"
#include "Config.hpp"

uint64_t perft(const Position& pos, int depth, Move* stack);
#ifdef MULTITHREADED
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
