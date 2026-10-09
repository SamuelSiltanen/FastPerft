// Copyright 2022 Samuel Siltanen
// Make.cpp

#pragma once

#include "ChessTypes.hpp"

Position make(const Position& pos, const Move& move);
template<Color> FP_HOST_DEVICE Position make(const Position& pos, const Move& move);

#include "MakeImpl.hpp"
