// Copyright 2022 Samuel Siltanen
// Make.cpp

#include "Make.hpp"

template Position make<White>(const Position& pos, const Move& move);
template Position make<Black>(const Position& pos, const Move& move);

Position make(const Position& pos, const Move& move)
{
    return (pos.state & TurnWhite) ? make<White>(pos, move) : make<Black>(pos, move);
}
