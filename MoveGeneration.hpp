#pragma once

#include "ChessTypes.hpp"

void fillMoveTables();
Move* generateP(const Position& pos, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);
Move* generateB(const Position& pos, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateR(const Position& pos, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateK(const Position& pos, Move* stack, uint64_t occ, const uint64_t pArea);
Move* generateCastling(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
Move* generateMovesTo(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateMovesInBetween(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const BishopPins& bPins, const RookPins& rPins);
Move* generateCheckEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const BishopPins& bPins, const RookPins& rPins);
uint64_t swneMoves(unsigned long src, uint64_t occ);
uint64_t senwMoves(unsigned long src, uint64_t occ);
uint64_t bmoves(unsigned long src, uint64_t occ);
uint64_t weMoves(unsigned long src, uint64_t occ);
uint64_t snMoves(unsigned long src, uint64_t occ);
uint64_t rmoves(unsigned long src, uint64_t occ);
uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, BishopPins& bPins, RookPins& rPins);
uint64_t findProtectionArea(const Position& pos, uint64_t occ);
