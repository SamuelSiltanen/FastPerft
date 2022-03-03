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

enum Piece
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

struct alignas(2) Move
{
    uint16_t packed;

    Move() : packed(0) {}

    Move(Piece piece, unsigned long src, unsigned long dst)
        : packed(static_cast<uint16_t>(src | (dst << 6) | (piece << 12)))
    {}

    Move(Piece piece, unsigned long src, unsigned long dst, Piece prom)
        : packed(static_cast<uint16_t>(src | (dst << 6) | (prom << 12) | 0x8000))
    {}

    __forceinline uint16_t src() const { return packed & 0x3f; }
    __forceinline uint16_t dst() const { return (packed >> 6) & 0x3f; }
    __forceinline Piece piece() const { return (packed & 0x8000) ? Pawn : static_cast<Piece>(packed >> 12); }
    __forceinline Piece prom() const { return (packed & 0x8000) ? static_cast<Piece>((packed >> 12) & 7) : None; }
};

struct RookPins
{
    uint64_t pinnedSN;
    uint64_t pinnedWE;
};

struct BishopPins
{
    uint64_t pinnedSWNE;
    uint64_t pinnedSENW;
};
