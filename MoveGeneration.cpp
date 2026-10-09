// Copyright 2022 Samuel Siltanen
// MoveGeneration.cpp

#include "MoveGeneration.hpp"

#include <cstdio>
#include <intrin.h>
#include <thread>
#include <cassert>

#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)

#if PEXT_INTRINSIC
#pragma intrinsic(_pext_u64)
#pragma intrinsic(_pdep_u64)
#endif

MoveTables moveTables;

#if MAGIC_BITBOARDS
#include <random>
#include <bitset>

const int BBits[64] =
{
  58, 59, 59, 59, 59, 59, 59, 58,
  59, 59, 59, 59, 59, 59, 59, 59,
  59, 59, 57, 57, 57, 57, 59, 59,
  59, 59, 57, 55, 55, 57, 59, 59,
  59, 59, 57, 55, 55, 57, 59, 59,
  59, 59, 57, 57, 57, 57, 59, 59,
  59, 59, 59, 59, 59, 59, 59, 59,
  58, 59, 59, 59, 59, 59, 59, 58
};

const int RBits[64] =
{
  52, 53, 53, 53, 53, 53, 53, 52,
  53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53,
  53, 54, 54, 54, 54, 54, 54, 53,
  52, 53, 53, 53, 53, 53, 53, 52
};
// 36 x 2^10 + 24 x 2^11 + 4 x 2^12 = 100k

/*
uint64_t BMasks[64];
uint64_t RMasks[64];

uint64_t BMagic[64];
uint64_t RMagic[64];
*/

Magic BMagic[64];
Magic RMagic[64];

// Full tables, can be compressed if necessary
uint64_t BAttacks[64][512]; // 512 kB
uint64_t RAttacks[64][4096]; // 4 MB
uint64_t RCompressedAttacks[100 * 1024]; // 800 kB
#endif

#if MAGIC_BITBOARDS
void calculateMagicNumber(int x, int y)
{
    int src = y * 8 + x;

    uint64_t blockers[4096];
    uint64_t attacks[4096];

    std::mt19937_64 mt(0xacdcabbadeadbeef);

    // Construct blockers and attacks for each index
    int numBits = 64 - BBits[src];
    int numEntries = (1ULL << numBits);
    uint64_t mask = BMagic[src].mask;
    for (int maskIndex = 0; maskIndex < numEntries; maskIndex++)
    {
        // Construct partial mask from index
        blockers[maskIndex] = 0;
        uint64_t workMask = mask;
        unsigned long pos;
        for (int bit = 0; bit < numBits; bit++)
        {
            pos = static_cast<unsigned long>(_tzcnt_u64(workMask));
            if (maskIndex & (1 << bit))
                blockers[maskIndex] |= (1ULL << pos);
            workMask ^= (1ULL << pos);
        }

        uint64_t raySE = 0;
        for (int k = 1; k < 8 && (raySE & blockers[maskIndex]) == 0; k++)
        {
            int xx = x + k;
            int yy = y + k;
            if (xx > 7 || yy > 7) break;

            int dst = yy * 8 + xx;
            raySE |= (1ULL << dst);
        }

        uint64_t raySW = 0;
        for (int k = 1; k < 8 && (raySW & blockers[maskIndex]) == 0; k++)
        {
            int xx = x - k;
            int yy = y + k;
            if (xx < 0 || yy > 7) break;

            int dst = yy * 8 + xx;
            raySW |= (1ULL << dst);
        }

        uint64_t rayNW = 0;
        for (int k = 1; k < 8 && (rayNW & blockers[maskIndex]) == 0; k++)
        {
            int xx = x - k;
            int yy = y - k;
            if (xx < 0 || yy < 0) break;

            int dst = yy * 8 + xx;
            rayNW |= (1ULL << dst);
        }

        uint64_t rayNE = 0;
        for (int k = 1; k < 8 && (rayNE & blockers[maskIndex]) == 0; k++)
        {
            int xx = x + k;
            int yy = y - k;
            if (xx > 7 || yy < 0) break;

            int dst = yy * 8 + xx;
            rayNE |= (1ULL << dst);
        }

        attacks[maskIndex] = raySE | raySW | rayNW | rayNE;
    }

    // Try to find a hash mapping
    bool foundMagic = false;
    std::bitset<4096> used;
    for (int attempt = 0; attempt < 100000000; attempt++)
    {
        uint64_t magic = mt() & mt() & mt();
        if (__popcnt64((mask * magic) & 0xff00000000000000ULL) < 6) continue;

        used.reset();

        bool fail = false;
        for (int i = 0; i < numEntries; i++)
        {
            uint64_t index = (blockers[i] * magic) >> (64 - numBits);
            if (!used.test(index))
            {
                BAttacks[src][index] = attacks[i];
                used.set(index);
            }
            else if (used.test(index) && BAttacks[src][index] != attacks[i])
            {
                fail = true;
                break;
            }
        }

        if (!fail)
        {
            BMagic[src].magic = magic;
            foundMagic = true;
            break;
        }
    }

    if (!foundMagic)
    {
        printf("Failed to find magic number for square %c%d!\n", 'a' + (char)x, 8 - y);
    }

    numBits = 64 - RBits[src];
    numEntries = (1ULL << numBits);
    mask = RMagic[src].mask;
    for (int maskIndex = 0; maskIndex < numEntries; maskIndex++)
    {
        // Construct partial mask from index
        blockers[maskIndex] = 0;
        uint64_t workMask = mask;
        unsigned long pos;
        for (int bit = 0; bit < numBits; bit++)
        {
            pos = static_cast<unsigned long>(_tzcnt_u64(workMask));
            if (maskIndex & (1 << bit))
                blockers[maskIndex] |= (1ULL << pos);
            workMask ^= (1ULL << pos);
        }

        uint64_t rayS = 0;
        for (int k = 1; k < 8 && (rayS & blockers[maskIndex]) == 0; k++)
        {
            int yy = y + k;
            if (yy > 7) break;

            int dst = yy * 8 + x;
            rayS |= (1ULL << dst);
        }

        uint64_t rayW = 0;
        for (int k = 1; k < 8 && (rayW & blockers[maskIndex]) == 0; k++)
        {
            int xx = x - k;
            if (xx < 0) break;

            int dst = y * 8 + xx;
            rayW |= (1ULL << dst);
        }

        uint64_t rayN = 0;
        for (int k = 1; k < 8 && (rayN & blockers[maskIndex]) == 0; k++)
        {
            int yy = y - k;
            if (yy < 0) break;

            int dst = yy * 8 + x;
            rayN |= (1ULL << dst);
        }

        uint64_t rayE = 0;
        for (int k = 1; k < 8 && (rayE & blockers[maskIndex]) == 0; k++)
        {
            int xx = x + k;
            if (xx > 7) break;

            int dst = y * 8 + xx;
            rayE |= (1ULL << dst);
        }

        attacks[maskIndex] = rayS | rayW | rayN | rayE;
    }

    // Try to find a hash mapping
    foundMagic = false;
    for (int attempt = 0; attempt < 100000000; attempt++)
    {
        uint64_t magic = mt() & mt() & mt();
        if (__popcnt64((mask * magic) & 0xff00000000000000ULL) < 6) continue;

        used.reset();

        bool fail = false;
        for (int i = 0; i < numEntries; i++)
        {
            uint64_t index = (blockers[i] * magic) >> (64 - numBits);
            if (!used.test(index))
            {
                RAttacks[src][index] = attacks[i];
                used.set(index);
            }
            else if (used.test(index) && RAttacks[src][index] != attacks[i])
            {
                fail = true;
                break;
            }
        }

        if (!fail)
        {
            RMagic[src].magic = magic;
            foundMagic = true;
            break;
        }
    }

    if (!foundMagic)
    {
        printf("Failed to find magic number for square %c%d!\n", 'a' + (char)x, 8 - y);
    }
}
#endif

#if PEXT_INTRINSIC
uint64_t BMasks[64];
uint64_t RMasks[64];

uint64_t BAttacks[64][512]; // 512 kB
uint64_t RAttacks[64][4096]; // 4 MB
#endif

void fillMoveTables()
{
    int nys[8] = { -2, -2, -1, -1,  1, 1,  2, 2 };
    int nxs[8] = { -1,  1, -2,  2, -2, 2, -1, 1 };
    int kys[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
    int kxs[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            int src = y * 8 + x;

            moveTables.nmoves[src] = 0;
            for (int k = 0; k < 8; k++)
            {
                int xx = x + nxs[k];
                if (xx < 0) continue;
                if (xx > 7) continue;
                int yy = y + nys[k];
                if (yy < 0) continue;
                if (yy > 7) continue;
                int dst = yy * 8 + xx;
                moveTables.nmoves[src] |= (1ULL << dst);
            }

            moveTables.kmoves[src] = 0;
            for (int k = 0; k < 8; k++)
            {
                int xx = x + kxs[k];
                if (xx < 0) continue;
                if (xx > 7) continue;
                int yy = y + kys[k];
                if (yy < 0) continue;
                if (yy > 7) continue;
                int dst = yy * 8 + xx;
                moveTables.kmoves[src] |= (1ULL << dst);
            }

            moveTables.rays[src].SE = 0;
            moveTables.rays[src].SW = 0;
            moveTables.rays[src].NE = 0;
            moveTables.rays[src].NW = 0;
            moveTables.rays[src].S = 0;
            moveTables.rays[src].W = 0;
            moveTables.rays[src].N = 0;
            moveTables.rays[src].E = 0;
            for (int k = 1; k < 8; k++)
            {
                int xx, yy, dst;

                xx = x - k;
                if (xx >= 0)
                {
                    yy = y - k;
                    if (yy >= 0)
                    {
                        dst = yy * 8 + xx;
                        moveTables.rays[src].NW |= (1ULL << dst);
                    }
                    dst = y * 8 + xx;
                    moveTables.rays[src].W |= (1ULL << dst);
                    yy = y + k;
                    if (yy <= 7)
                    {
                        dst = yy * 8 + xx;
                        moveTables.rays[src].SW |= (1ULL << dst);
                    }
                }
                yy = y - k;
                if (yy >= 0)
                {
                    dst = yy * 8 + x;
                    moveTables.rays[src].N |= (1ULL << dst);
                }
                yy = y + k;
                if (yy <= 7)
                {
                    dst = yy * 8 + x;
                    moveTables.rays[src].S |= (1ULL << dst);
                }
                xx = x + k;
                if (xx <= 7)
                {
                    yy = y - k;
                    if (yy >= 0)
                    {
                        dst = yy * 8 + xx;
                        moveTables.rays[src].NE |= (1ULL << dst);
                    }
                    dst = y * 8 + xx;
                    moveTables.rays[src].E |= (1ULL << dst);
                    yy = y + k;
                    if (yy <= 7)
                    {
                        dst = yy * 8 + xx;
                        moveTables.rays[src].SE |= (1ULL << dst);
                    }
                }
            }
        }
    }

    for (int sq = 0; sq < 64; sq++)
    {
        moveTables.bishopRays[sq] = moveTables.rays[sq].SE | moveTables.rays[sq].SW | moveTables.rays[sq].NE | moveTables.rays[sq].NW;
    }

    for (int sq = 0; sq < 64; sq++)
    {
        moveTables.castlingMasks[sq] = ~0ULL;
        moveTables.castlingRookMoves[sq] = 0;
    }
    moveTables.castlingMasks[A8] = ~CastlingBlackLong;
    moveTables.castlingMasks[E8] = ~(CastlingBlackShort | CastlingBlackLong);
    moveTables.castlingMasks[H8] = ~CastlingBlackShort;
    moveTables.castlingMasks[A1] = ~CastlingWhiteLong;
    moveTables.castlingMasks[E1] = ~(CastlingWhiteShort | CastlingWhiteLong);
    moveTables.castlingMasks[H1] = ~CastlingWhiteShort;
    moveTables.castlingRookMoves[G1] = 0xa000000000000000ULL;
    moveTables.castlingRookMoves[C1] = 0x0900000000000000ULL;
    moveTables.castlingRookMoves[G8] = 0x00000000000000a0ULL;
    moveTables.castlingRookMoves[C8] = 0x0000000000000009ULL;

#if MAGIC_BITBOARDS
    const uint64_t BordersOff = 0x007e7e7e7e7e7e00ULL;
    for (int sq = 0; sq < 64; sq++)
    {
        BMagic[sq].mask = moveTables.rays[sq].SE | moveTables.rays[sq].SW | moveTables.rays[sq].NW | moveTables.rays[sq].NE;
        BMagic[sq].mask &= BordersOff;
        RMagic[sq].mask = moveTables.rays[sq].S & 0x00ffffffffffffffULL;
        RMagic[sq].mask |= moveTables.rays[sq].W & 0xfefefefefefefefeULL;
        RMagic[sq].mask |= moveTables.rays[sq].N & 0xffffffffffffff00ULL;
        RMagic[sq].mask |= moveTables.rays[sq].E & 0x7f7f7f7f7f7f7f7fULL;
    }

    std::thread* magicThreads[64];

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            magicThreads[y * 8 + x] = new std::thread(calculateMagicNumber, x, y);
        }
    }

    for (int t = 0; t < 64; t++)
    {
        magicThreads[t]->join();
        delete magicThreads[t];
    }
   
    int compressedIndex = 0;
    for (int sq = 0; sq < 64; sq++)
    {
        int numBits = 64 - RBits[sq];
        int numEntries = (1ULL << numBits);

        uint64_t* squareStart = &RCompressedAttacks[compressedIndex];

        memcpy(squareStart, &RAttacks[sq][0], sizeof(uint64_t) * numEntries);

        RMagic[sq].shift = RBits[sq];
        RMagic[sq].ptr = squareStart;

        compressedIndex += numEntries;
    }

#endif

#if KINDERGARTEN_BITBOARDS
    for (int x = 0; x < 8; x++)
    {
        for (uint64_t mask = 0; mask < 64; mask++)
        {
            uint64_t rankAttack = 0;
            for (int xx = x - 1; xx >= 0; xx--)
            {
                rankAttack |= (1ULL << xx);
                if ((mask << 1) & (1ULL << xx)) break;
            }
            for (int xx = x + 1; xx < 8; xx++)
            {
                rankAttack |= (1ULL << xx);
                if ((mask << 1) & (1ULL << xx)) break;
            }

            rankAttack |= (rankAttack << 8);
            rankAttack |= (rankAttack << 16);
            rankAttack |= (rankAttack << 32);

            moveTables.KinderGartenAttacks[x][mask] = rankAttack;

            uint64_t fileAttack = 0;
            for (int yy = x - 1; yy >= 0; yy--)
            {
                fileAttack |= (1ULL << (yy * 8));
                if ((mask << 1) & (0x80 >> yy)) break;

            }
            for (int yy = x + 1; yy < 8; yy++)
            {
                fileAttack |= (1ULL << (yy * 8));
                if ((mask << 1) & (0x80 >> yy)) break;

            }
            moveTables.AFileAttacks[x][mask] = fileAttack;
        }
    }

    for (int y = 0; y < 8; y++)
    {
        uint64_t wemask = (0x00000000000000ffULL << (y * 8));
        for (int x = 0; x < 8; x++)
        {
            moveTables.weExMask[y * 8 + x] = wemask;

            moveTables.swneExMask[y * 8 + x] = (1ULL << (y * 8 + x));
            moveTables.senwExMask[y * 8 + x] = (1ULL << (y * 8 + x));
            for (int k = 0; k < 8; k++)
            {
                if (y + k < 8 && x - k >= 0)
                    moveTables.swneExMask[y * 8 + x] |= (1ULL << ((y + k) * 8 + (x - k)));
                if (y - k >= 0 && x + k < 8)
                    moveTables.swneExMask[y * 8 + x] |= (1ULL << ((y - k) * 8 + (x + k)));
                if (y + k < 8 && x + k < 8)
                    moveTables.senwExMask[y * 8 + x] |= (1ULL << ((y + k) * 8 + (x + k)));
                if (y - k >= 0 && x - k >= 0)
                    moveTables.senwExMask[y * 8 + x] |= (1ULL << ((y - k) * 8 + (x - k)));
            }
        }
    }
#endif

#if PEXT_INTRINSIC
    const uint64_t BordersOff = 0x007e7e7e7e7e7e00ULL;
    for (int sq = 0; sq < 64; sq++)
    {
        BMasks[sq] = moveTables.rays[sq].SE | moveTables.rays[sq].SW | moveTables.rays[sq].NW | moveTables.rays[sq].NE;
        BMasks[sq] &= BordersOff;
        RMasks[sq] = moveTables.rays[sq].S & 0x00ffffffffffffffULL;
        RMasks[sq] |= moveTables.rays[sq].W & 0xfefefefefefefefeULL;
        RMasks[sq] |= moveTables.rays[sq].N & 0xffffffffffffff00ULL;
        RMasks[sq] |= moveTables.rays[sq].E & 0x7f7f7f7f7f7f7f7fULL;

        int x = sq & 7;
        int y = sq >> 3;

        uint64_t mask = BMasks[sq];
        uint64_t n = (1ULL << __popcnt64(mask));
        for (uint64_t index = 0; index < n; index++)
        {
            uint64_t occ = _pdep_u64(index, mask);

            assert(__popcnt64(occ) <= 9);

            uint64_t raySE = 0;
            for (int k = 1; k < 8 && (raySE & occ) == 0; k++)
            {
                int xx = x + k;
                int yy = y + k;
                if (xx > 7 || yy > 7) break;

                int dst = yy * 8 + xx;
                raySE |= (1ULL << dst);
            }

            uint64_t raySW = 0;
            for (int k = 1; k < 8 && (raySW & occ) == 0; k++)
            {
                int xx = x - k;
                int yy = y + k;
                if (xx < 0 || yy > 7) break;

                int dst = yy * 8 + xx;
                raySW |= (1ULL << dst);
            }

            uint64_t rayNW = 0;
            for (int k = 1; k < 8 && (rayNW & occ) == 0; k++)
            {
                int xx = x - k;
                int yy = y - k;
                if (xx < 0 || yy < 0) break;

                int dst = yy * 8 + xx;
                rayNW |= (1ULL << dst);
            }

            uint64_t rayNE = 0;
            for (int k = 1; k < 8 && (rayNE & occ) == 0; k++)
            {
                int xx = x + k;
                int yy = y - k;
                if (xx > 7 || yy < 0) break;

                int dst = yy * 8 + xx;
                rayNE |= (1ULL << dst);
            }

            BAttacks[sq][index] = raySE | raySW | rayNW | rayNE;            
        }

        mask = RMasks[sq];
        n = (1ULL << __popcnt64(mask));
        for (uint64_t index = 0; index < n; index++)
        {
            uint64_t occ = _pdep_u64(index, mask);

            assert(__popcnt64(occ) <= 12);

            uint64_t rayS = 0;
            for (int k = 1; k < 8 && (rayS & occ) == 0; k++)
            {
                int yy = y + k;
                if (yy > 7) break;

                int dst = yy * 8 + x;
                rayS |= (1ULL << dst);
            }

            uint64_t rayW = 0;
            for (int k = 1; k < 8 && (rayW & occ) == 0; k++)
            {
                int xx = x - k;
                if (xx < 0) break;

                int dst = y * 8 + xx;
                rayW |= (1ULL << dst);
            }

            uint64_t rayN = 0;
            for (int k = 1; k < 8 && (rayN & occ) == 0; k++)
            {
                int yy = y - k;
                if (yy < 0) break;

                int dst = yy * 8 + x;
                rayN |= (1ULL << dst);
            }

            uint64_t rayE = 0;
            for (int k = 1; k < 8 && (rayE & occ) == 0; k++)
            {
                int xx = x + k;
                if (xx > 7) break;

                int dst = y * 8 + xx;
                rayE |= (1ULL << dst);
            }

            RAttacks[sq][index] = rayS | rayW | rayN | rayE;
        }
    }
#endif
}


template Move* generateP<White>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template Move* generateP<Black>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);

Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    return (pos.state & TurnWhite) ? generateP<White>(pos, stack, occ, pins) : generateP<Black>(pos, stack, occ, pins);
}

template Move* generateN<White>(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);
template Move* generateN<Black>(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins);

Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins)
{
    return (pos.state & TurnWhite) ? generateN<White>(pos, stack, occ, anyPins) : generateN<Black>(pos, stack, occ, anyPins);
}

template Move* generateB<White>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template Move* generateB<Black>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);

Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    return (pos.state & TurnWhite) ? generateB<White>(pos, stack, occ, pins) : generateB<Black>(pos, stack, occ, pins);
}

template Move* generateR<White>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template Move* generateR<Black>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);

Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    return (pos.state & TurnWhite) ? generateR<White>(pos, stack, occ, pins) : generateR<Black>(pos, stack, occ, pins);
}

template Move* generateQ<White>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template Move* generateQ<Black>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);

Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    return (pos.state & TurnWhite) ? generateQ<White>(pos, stack, occ, pins) : generateQ<Black>(pos, stack, occ, pins);
}

template Move* generateSliders<White>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);
template Move* generateSliders<Black>(const Position& pos, Move* stack, uint64_t occ, const Pins& pins);

template Move* generateK<White>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
template Move* generateK<Black>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);

Move* generateK(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea)
{
    return (pos.state & TurnWhite) ? generateK<White>(pos, stack, occ, pArea) : generateK<Black>(pos, stack, occ, pArea);
}

template Move* generateCastling<White>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);
template Move* generateCastling<Black>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea);

Move* generateCastling(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea)
{
    return (pos.state & TurnWhite) ? generateCastling<White>(pos, stack, occ, pArea) : generateCastling<Black>(pos, stack, occ, pArea);
}

Move* generateMovesTo(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins)
{
    uint64_t our = (pos.state & TurnWhite) ? pos.w : ~pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    bool isCapture = occ & (1ULL << dst);

    if (pos.state & TurnWhite)
    {
        if (isCapture)
        {
            if (pos.p & our & 0x00fefefefefe0000ULL & (1ULL << (dst + 9)) & (~anyPins | pins.pinnedSENW))
            {
                *stack = Move(Pawn, dst + 9, dst);
                ++stack;
            }
            if (pos.p & our & 0x007f7f7f7f7f0000ULL & (1ULL << (dst + 7)) & (~anyPins | pins.pinnedSWNE))
            {
                *stack = Move(Pawn, dst + 7, dst);
                ++stack;
            }

            // Special case: The target square has a pawn that can be captured with en passant
            if ((pos.state & EPValid) && ((1ULL << dst) & 0x00000000ff000000 & pos.p))
            {
                uint64_t EPSquare = (pos.state >> 5) & 63;
                if (EPSquare + 8 == dst)
                {
                    if (pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW))
                    {
                        *stack = Move(Pawn, EPSquare + 9, EPSquare);
                        ++stack;
                    }
                    if (pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE))
                    {
                        *stack = Move(Pawn, EPSquare + 7, EPSquare);
                        ++stack;
                    }
                }
            }

            if (pos.p & our & 0x000000000000fe00ULL & (1ULL << (dst + 9)) & (~anyPins | pins.pinnedSENW))
            {
                *stack = Move(Pawn, dst + 9, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst + 9, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst + 9, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst + 9, dst, Queen);
                ++stack;
            }
            if (pos.p & our & 0x0000000000007f00ULL & (1ULL << (dst + 7)) & (~anyPins | pins.pinnedSWNE))
            {
                *stack = Move(Pawn, dst + 7, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst + 7, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst + 7, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst + 7, dst, Queen);
                ++stack;
            }
        }
        else
        {
            if (pos.p & our & 0x00ffffffffff0000 & (1ULL << (dst + 8)) & (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst + 8, dst);
                ++stack;
            }
            if (pos.p & our & 0x00ff000000000000 & (1ULL << (dst + 16)) & (~occ << 8) & (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst + 16, dst);
                ++stack;
            }

            if (pos.p & our & 0x000000000000ff00 & (1ULL << (dst + 8)) & (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst + 8, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst + 8, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst + 8, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst + 8, dst, Queen);
                ++stack;
            }
        }
    }
    else
    {
        if (isCapture)
        {
            if (pos.p & our & 0x0000fefefefefe00ULL & (1ULL << (dst - 7)) & (~anyPins | pins.pinnedSWNE))
            {
                *stack = Move(Pawn, dst - 7, dst);
                ++stack;
            }
            if (pos.p & our & 0x00007f7f7f7f7f00ULL & (1ULL << (dst - 9)) & (~anyPins | pins.pinnedSENW))
            {
                *stack = Move(Pawn, dst - 9, dst);
                ++stack;
            }

            // Special case: The target square has a pawn that can be captured with en passant
            if ((pos.state & EPValid) && ((1ULL << dst) & 0x000000ff00000000 & pos.p))
            {
                uint64_t EPSquare = (pos.state >> 5) & 63;
                if (EPSquare - 8 == dst)
                {
                    if (pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE))
                    {
                        *stack = Move(Pawn, EPSquare - 7, EPSquare);
                        ++stack;
                    }
                    if (pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW))
                    {
                        *stack = Move(Pawn, EPSquare - 9, EPSquare);
                        ++stack;
                    }
                }
            }

            if (pos.p & our & 0x00fe000000000000ULL & (1ULL << (dst - 7)) & (~anyPins | pins.pinnedSWNE))
            {
                *stack = Move(Pawn, dst - 7, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst - 7, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst - 7, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst - 7, dst, Queen);
                ++stack;
            }
            if (pos.p & our & 0x007f000000000000ULL & (1ULL << (dst - 9)) & (~anyPins | pins.pinnedSENW))
            {
                *stack = Move(Pawn, dst - 9, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst - 9, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst - 9, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst - 9, dst, Queen);
                ++stack;
            }
        }
        else
        {
            if (pos.p & our & 0x0000ffffffffff00 & (1ULL << (dst - 8)) & (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst - 8, dst);
                ++stack;
            }
            if (pos.p & our & 0x000000000000ff00 & (1ULL << (dst - 16)) & (~occ >> 8)& (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst - 16, dst);
                ++stack;
            }

            if (pos.p & our & 0x00ff000000000000 & (1ULL << (dst - 8)) & (~anyPins | pins.pinnedSN))
            {
                *stack = Move(Pawn, dst - 8, dst, Knight);
                ++stack;
                *stack = Move(Pawn, dst - 8, dst, Bishop);
                ++stack;
                *stack = Move(Pawn, dst - 8, dst, Rook);
                ++stack;
                *stack = Move(Pawn, dst - 8, dst, Queen);
                ++stack;
            }
        }
    }

    unsigned long src;
    uint64_t pcs = pos.n & our & moveTables.nmoves[dst] & ~anyPins;
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Knight, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    uint64_t swne = swneMoves(dst, occ);
    uint64_t senw = senwMoves(dst, occ);
    uint64_t we = weMoves(dst, occ);
    uint64_t sn = snMoves(dst, occ);

    pcs = pos.bq & ~pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Bishop, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & ~pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Bishop, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.rq & ~pos.bq & our & we & (~anyPins | pins.pinnedWE);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Rook, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.rq & ~pos.bq & our & sn & (~anyPins | pins.pinnedSN);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Rook, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & we & (~anyPins | pins.pinnedWE);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & sn & (~anyPins | pins.pinnedSN);
    while (pcs)
    {
        src = static_cast<unsigned long>(_tzcnt_u64(pcs));
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    // Ignore king captures, because they are generate as part of king moves

    return stack;
}

Move* generateMovesInBetween(const Position& pos, unsigned long dst, Move* stack, uint64_t occ, const Pins& pins)
{
    uint64_t our = (pos.state & TurnWhite) ? pos.w : ~pos.w;
    uint64_t king = pos.k & our;
    unsigned long kingSq;
    kingSq = static_cast<unsigned long>(_tzcnt_u64(king));

    if (moveTables.rays[kingSq].N & (1ULL << dst))
    {
        for (unsigned long i = dst + 8; i < kingSq; i += 8)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].S & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 8; i < dst; i += 8)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].W & (1ULL << dst))
    {
        for (unsigned long i = dst + 1; i < kingSq; i++)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].E & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 1; i < dst; i++)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].SW & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 7; i < dst; i += 7)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].NW & (1ULL << dst))
    {
        for (unsigned long i = dst + 9; i < kingSq; i += 9)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].NE & (1ULL << dst))
    {
        for (unsigned long i = dst + 7; i < kingSq; i += 7)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (moveTables.rays[kingSq].SE & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 9; i < dst; i += 9)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }

    return stack;
}

template uint64_t countEvasions<White>(const Position& pos, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);
template uint64_t countEvasions<Black>(const Position& pos, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);

template Move* generateEvasions<White>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);
template Move* generateEvasions<Black>(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins);

Move* generateCheckEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins)
{
    stack = generateK(pos, stack, occ, pArea);

    unsigned long dst;
    dst = static_cast<unsigned long>(_tzcnt_u64(checkers));
    checkers ^= (1ULL << dst);

    if (!checkers)
    {
        stack = generateMovesTo(pos, dst, stack, occ, pins);

        if ((1ULL << dst) & (pos.bq | pos.rq))
        {
            stack = generateMovesInBetween(pos, dst, stack, occ, pins);
        }
    }

    return stack;
}

template uint64_t countP<White>(const Position& pos, uint64_t occ, const Pins& pins);
template uint64_t countP<Black>(const Position& pos, uint64_t occ, const Pins& pins);

template uint64_t countN<White>(const Position& pos, uint64_t occ, uint64_t anyPins);
template uint64_t countN<Black>(const Position& pos, uint64_t occ, uint64_t anyPins);

template uint64_t countSliders<White>(const Position& pos, uint64_t occ, const Pins& pins);
template uint64_t countSliders<Black>(const Position& pos, uint64_t occ, const Pins& pins);

template uint64_t countK<White>(const Position& pos, uint64_t occ, uint64_t pArea);
template uint64_t countK<Black>(const Position& pos, uint64_t occ, uint64_t pArea);

template uint64_t countCastling<White>(const Position& pos, uint64_t occ, uint64_t pArea);
template uint64_t countCastling<Black>(const Position& pos, uint64_t occ, uint64_t pArea);

template uint64_t findPinsAndCheckers<White>(const Position& pos, uint64_t occ, Pins& pins);
template uint64_t findPinsAndCheckers<Black>(const Position& pos, uint64_t occ, Pins& pins);

uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins)
{
    return (pos.state & TurnWhite) ? findPinsAndCheckers<White>(pos, occ, pins) : findPinsAndCheckers<Black>(pos, occ, pins);
}

template uint64_t findProtectionArea<White>(const Position& pos, uint64_t occ);
template uint64_t findProtectionArea<Black>(const Position& pos, uint64_t occ);

uint64_t findProtectionArea(const Position& pos, uint64_t occ)
{
    return (pos.state & TurnWhite) ? findProtectionArea<White>(pos, occ) : findProtectionArea<Black>(pos, occ);
}

