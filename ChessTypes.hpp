// Copyright 2022 Samuel Siltanen
// ChessTypes.hpp

#pragma once

#include <cstdint>

constexpr uint64_t TurnWhite = (1ULL << 0);
constexpr uint64_t CastlingWhiteShort = (1ULL << 1);
constexpr uint64_t CastlingWhiteLong = (1ULL << 2);
constexpr uint64_t CastlingBlackShort = (1ULL << 3);
constexpr uint64_t CastlingBlackLong = (1ULL << 4);
// Bits 5 - 10 denote the EP square
constexpr uint64_t EPValid = (1ULL << 11);

struct alignas(64) Position
{
    uint64_t p = 0x00ff00000000ff00ULL;
    uint64_t n = 0x4200000000000042ULL;
    uint64_t bq = 0x2400000000000024ULL | 0x0800000000000008ULL;
    uint64_t rq = 0x8100000000000081ULL | 0x0800000000000008ULL;
    uint64_t k = 0x1000000000000010ULL;
    uint64_t w = 0xffff000000000000ULL;
    uint64_t state = TurnWhite | CastlingWhiteShort | CastlingWhiteLong | CastlingBlackShort | CastlingBlackLong;
    uint64_t hash = 0;
};

enum Piece : uint16_t
{
    None,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
    EP
};

enum Color
{
    Black,
    White
};

enum Square : uint16_t
{
    A8, B8, C8, D8, E8, F8, G8, H8,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A1, B1, C1, D1, E1, F1, G1, H1
};

struct alignas(2) Move
{
    uint16_t packed;

    Move() : packed(0) {}

    Move(Piece piece, Square src, Square dst)
        : packed(src | (dst << 6) | (piece << 12))
    {}

    Move(Piece piece, unsigned long src, unsigned long dst)
        : packed(static_cast<uint16_t>(src | (dst << 6) | (piece << 12)))
    {}

    Move(Piece piece, uint64_t src, uint64_t dst)
        : packed(static_cast<uint16_t>(src | (dst << 6)) | (piece << 12))
    {}

    Move(Piece piece, unsigned long src, unsigned long dst, Piece prom)
        : packed(static_cast<uint16_t>(src | (dst << 6) | 0x8000) | (prom << 12))
    {}

    bool operator==(const Move& other) const { return other.packed == packed; }

    __forceinline uint16_t src() const { return packed & 0x3f; }
    __forceinline uint16_t dst() const { return (packed >> 6) & 0x3f; }
    __forceinline Piece piece() const { return (packed & 0x8000) ? Pawn : static_cast<Piece>(packed >> 12); }
    __forceinline Piece prom() const { return (packed & 0x8000) ? static_cast<Piece>((packed >> 12) & 7) : None; }
};

struct MoveHash
{
    size_t operator()(const Move& move) const
    {
        return static_cast<size_t>(move.packed);
    }
};

struct Pins
{
    uint64_t pinnedSN;
    uint64_t pinnedWE;
    uint64_t pinnedSWNE;
    uint64_t pinnedSENW;
};
