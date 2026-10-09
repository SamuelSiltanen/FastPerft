// Copyright 2022 Samuel Siltanen
// MoveGeneration.hpp

#pragma once

#include "ChessTypes.hpp"
#include "MoveTables.hpp"

// Initialize lookup tables
void fillMoveTables();

// Move generation, store moves in move stack
Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);
template<Color> FP_HOST_DEVICE Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);
Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateSliders(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
Move* generateK(const Position& pos, Move* stack, uint64_t occ, const uint64_t pArea);
template<Color> FP_HOST_DEVICE Move* generateK(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
Move* generateCastling(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
template<Color> FP_HOST_DEVICE Move* generateCastling(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
Move* generateMovesTo(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins);
Move* generateMovesInBetween(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins);
Move* generateCheckEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);
template<Color> FP_HOST_DEVICE uint64_t countEvasions(const Position& pos, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);
template<Color> FP_HOST_DEVICE Move* generateEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);

// Count moves, but don't store them
template<Color> FP_HOST_DEVICE uint64_t countP(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE uint64_t countN(const Position& pos, uint64_t occ, uint64_t anyPins);
template<Color> FP_HOST_DEVICE uint64_t countSliders(const Position& pos, uint64_t occ, const Pins& pins);
template<Color> FP_HOST_DEVICE uint64_t countK(const Position& pos, uint64_t occ, uint64_t pArea);
template<Color> FP_HOST_DEVICE uint64_t countCastling(const Position& pos, uint64_t occ, uint64_t pArea);

// Helpers
FP_INLINE uint64_t swneMoves(unsigned long src, uint64_t occ);
FP_INLINE uint64_t senwMoves(unsigned long src, uint64_t occ);
FP_INLINE uint64_t bmoves(unsigned long src, uint64_t occ);
FP_INLINE uint64_t weMoves(unsigned long src, uint64_t occ);
FP_INLINE uint64_t snMoves(unsigned long src, uint64_t occ);
FP_INLINE uint64_t rmoves(unsigned long src, uint64_t occ);
uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins);
template<Color> FP_HOST_DEVICE uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins);
uint64_t findProtectionArea(const Position& pos, uint64_t occ);
template<Color> FP_HOST_DEVICE uint64_t findProtectionArea(const Position& pos, uint64_t occ);

#include "MoveGenerationImpl.hpp"
