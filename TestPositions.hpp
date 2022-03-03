#pragma once

#include "ChessTypes.hpp"

// See https://www.chessprogramming.org/Perft_Results for expected results for positions

// Position 1 - Initial position
const Position Position1 =
{
    // 1 2 3 4 5 6 7 8 Pos 1
    0x00ff00000000ff00,
    0x4200000000000042,
    0x2400000000000024 | 0x0800000000000008,
    0x8100000000000081 | 0x0800000000000008,
    0x1000000000000010,
    0xffff000000000000,
    TurnWhite | CastlingWhiteShort | CastlingWhiteLong | CastlingBlackShort | CastlingBlackLong,
    0
};

// Position 2 - Middlegame position with lots of special moves
const Position Position2 =
{
    // 1 2 3 4 5 6 7 8 Pos 2
    0x00e7801208502d00,
    0x0000040010220000,
    0x0018000000014000,
    0x8100000000000081,
    0x0000200000001000,
    0x1000000000000010,
    0x91ff241018000000,
    TurnWhite | CastlingWhiteShort | CastlingWhiteLong | CastlingBlackShort | CastlingBlackLong
};

// Position 3 - Rook endgame
const Position Position3 =
{
    // 1 2 3 4 5 6 7 8 Pos 3
    0x0050002002080400,
    0x0000000000000000,
    0x0000000000000000 | 0x0000000000000000,
    0x0000000280000000 | 0x0000000000000000,
    0x0000008001000000,
    0x0050000203000000,
    TurnWhite,
    0
};

// Position 4 - Another middlegame position with lots of special moves
const Position Position4 =
{
    // 1 2 3 4 5 6 7 8 Pos 4
    0x00cb00140200ef00,
    0x0000200001a00000,
    0x0000000300420000,
    0x2100000000000081,
    0x0800010000000000,
    0x4000000000000010,
    0x69c9201702800100,
    TurnWhite | CastlingBlackShort | CastlingBlackLong
};

// Position 5 - A position that caught bugs in several engines
const Position Position5 =
{
    // 1 2 3 4 5 6 7 8 Pos 5
    0x00c700000004eb00,
    0x0230000000000002,
    0x0400000400001004,
    0x8100000000000081,
    0x0800000000000008,
    0x1000000000000020,
    0x9fd7000400000800,
    TurnWhite | CastlingWhiteShort | CastlingWhiteLong
};

// Position 6 - Simple opening/middlegame position
const Position Position6 =
{
    // 1 2 3 4 5 6 7 8 Pos 6
    0x00e609101009e600,
    0x0000240000240000,
    0x0000004444000000 | 0x0010000000001000,
    0x2100000000000021 | 0x0010000000001000,
    0x4000000000000040,
    0x61f62d1440000000,
    TurnWhite,
    0
};

// Some debugging positions below

Position CheckmateTest1 =
{
    // 1 2 3 4 5 6 7 8 Mate test 1
    0x00e000000000e000,
    0x0000000000000000,
    0x0000000000000000 | 0x0000000000000000,
    0x0001000000000200 | 0x0000000000000000,
    0x4000000000000040,
    0x40e1000000000000,
    TurnWhite,
    0
};

Position CheckmateTest2 =
{
    // 1 2 3 4 5 6 7 8 Mate test 2
    0x0000000000000000,
    0x0000000000000000,
    0x0000000000000000 | 0x1000000000000000,
    0x0000000000000000 | 0x1000000000000000,
    0x0000400000000040,
    0x1000400000000000,
    TurnWhite,
    0
};

Position EPCheck =
{
    // 1 2 3 4 5 6 7 8 EP-Check test
    0x0000000010000800,
    0x0000000000000000,
    0x0000000000000000,
    0x0000000000000000,
    0x0000000000000000,
    0x0000000400000040,
    0x0000000410000000,
    0
};
