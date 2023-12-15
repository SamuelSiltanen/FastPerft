// Copyright 2022 Samuel Siltanen
// MoveGeneration.cpp

#include "MoveGeneration.hpp"

#include <cstdio>
#include <intrin.h>
#include <thread>
#include <cassert>

#define MAGIC_BITBOARDS 0
#define KINDERGARTEN_BITBOARDS 1
#define PEXT_INTRINSIC 1

#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)

#if PEXT_INTRINSIC
#pragma intrinsic(_pext_u64)
#pragma intrinsic(_pdep_u64)
#endif

uint64_t nmoves[64];
uint64_t kmoves[64];
Rays rays[64];

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

struct alignas(32) Magic
{
    uint64_t mask;
    uint64_t magic;
    uint64_t shift;
    uint64_t* ptr;
};

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

#if KINDERGARTEN_BITBOARDS
uint64_t swneExMask[64];
uint64_t senwExMask[64];
uint64_t weExMask[64];
uint64_t AFileAttacks[8][64]; // 4 kB
uint64_t KinderGartenAttacks[8][64]; // 4 kB
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
            _BitScanForward64(&pos, workMask);
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
            _BitScanForward64(&pos, workMask);
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

            nmoves[src] = 0;
            for (int k = 0; k < 8; k++)
            {
                int xx = x + nxs[k];
                if (xx < 0) continue;
                if (xx > 7) continue;
                int yy = y + nys[k];
                if (yy < 0) continue;
                if (yy > 7) continue;
                int dst = yy * 8 + xx;
                nmoves[src] |= (1ULL << dst);
            }

            kmoves[src] = 0;
            for (int k = 0; k < 8; k++)
            {
                int xx = x + kxs[k];
                if (xx < 0) continue;
                if (xx > 7) continue;
                int yy = y + kys[k];
                if (yy < 0) continue;
                if (yy > 7) continue;
                int dst = yy * 8 + xx;
                kmoves[src] |= (1ULL << dst);
            }

            rays[src].SE = 0;
            rays[src].SW = 0;
            rays[src].NE = 0;
            rays[src].NW = 0;
            rays[src].S = 0;
            rays[src].W = 0;
            rays[src].N = 0;
            rays[src].E = 0;
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
                        rays[src].NW |= (1ULL << dst);
                    }
                    dst = y * 8 + xx;
                    rays[src].W |= (1ULL << dst);
                    yy = y + k;
                    if (yy <= 7)
                    {
                        dst = yy * 8 + xx;
                        rays[src].SW |= (1ULL << dst);
                    }
                }
                yy = y - k;
                if (yy >= 0)
                {
                    dst = yy * 8 + x;
                    rays[src].N |= (1ULL << dst);
                }
                yy = y + k;
                if (yy <= 7)
                {
                    dst = yy * 8 + x;
                    rays[src].S |= (1ULL << dst);
                }
                xx = x + k;
                if (xx <= 7)
                {
                    yy = y - k;
                    if (yy >= 0)
                    {
                        dst = yy * 8 + xx;
                        rays[src].NE |= (1ULL << dst);
                    }
                    dst = y * 8 + xx;
                    rays[src].E |= (1ULL << dst);
                    yy = y + k;
                    if (yy <= 7)
                    {
                        dst = yy * 8 + xx;
                        rays[src].SE |= (1ULL << dst);
                    }
                }
            }
        }
    }

#if MAGIC_BITBOARDS
    const uint64_t BordersOff = 0x007e7e7e7e7e7e00ULL;
    for (int sq = 0; sq < 64; sq++)
    {
        BMagic[sq].mask = rays[sq].SE | rays[sq].SW | rays[sq].NW | rays[sq].NE;
        BMagic[sq].mask &= BordersOff;
        RMagic[sq].mask = rays[sq].S & 0x00ffffffffffffffULL;
        RMagic[sq].mask |= rays[sq].W & 0xfefefefefefefefeULL;
        RMagic[sq].mask |= rays[sq].N & 0xffffffffffffff00ULL;
        RMagic[sq].mask |= rays[sq].E & 0x7f7f7f7f7f7f7f7fULL;
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

            KinderGartenAttacks[x][mask] = rankAttack;

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
            AFileAttacks[x][mask] = fileAttack;
        }
    }

    for (int y = 0; y < 8; y++)
    {
        uint64_t wemask = (0x00000000000000ffULL << (y * 8));
        for (int x = 0; x < 8; x++)
        {
            weExMask[y * 8 + x] = wemask;

            swneExMask[y * 8 + x] = (1ULL << (y * 8 + x));
            senwExMask[y * 8 + x] = (1ULL << (y * 8 + x));
            for (int k = 0; k < 8; k++)
            {
                if (y + k < 8 && x - k >= 0)
                    swneExMask[y * 8 + x] |= (1ULL << ((y + k) * 8 + (x - k)));
                if (y - k >= 0 && x + k < 8)
                    swneExMask[y * 8 + x] |= (1ULL << ((y - k) * 8 + (x + k)));
                if (y + k < 8 && x + k < 8)
                    senwExMask[y * 8 + x] |= (1ULL << ((y + k) * 8 + (x + k)));
                if (y - k >= 0 && x - k >= 0)
                    senwExMask[y * 8 + x] |= (1ULL << ((y - k) * 8 + (x - k)));
            }
        }
    }
#endif

#if PEXT_INTRINSIC
    const uint64_t BordersOff = 0x007e7e7e7e7e7e00ULL;
    for (int sq = 0; sq < 64; sq++)
    {
        BMasks[sq] = rays[sq].SE | rays[sq].SW | rays[sq].NW | rays[sq].NE;
        BMasks[sq] &= BordersOff;
        RMasks[sq] = rays[sq].S & 0x00ffffffffffffffULL;
        RMasks[sq] |= rays[sq].W & 0xfefefefefefefefeULL;
        RMasks[sq] |= rays[sq].N & 0xffffffffffffff00ULL;
        RMasks[sq] |= rays[sq].E & 0x7f7f7f7f7f7f7f7fULL;

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

Move* generateP(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    if (pos.state & TurnWhite)
    {
        uint64_t our = pos.w;
        uint64_t pcs = pos.p & our & 0x00ffffffffff0000 & (~occ << 8) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 8;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00ff000000000000 & (~occ << 8) & (~occ << 16) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 16;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        uint64_t their = occ & ~pos.w;
        pcs = pos.p & our & 0x00fefefefefe0000 & (their << 9) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 9;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x007f7f7f7f7f0000 & (their << 7) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 7;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        // Promotions (also capturing)
        pcs = pos.p & our & 0x000000000000ff00 & (~occ << 8) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 8;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
        
        pcs = pos.p & our & 0x000000000000fe00 & (their << 9) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 9;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x0000000000007f00 & (their << 7) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src - 7;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
    }
    else
    {
        uint64_t our = occ & ~pos.w;
        uint64_t pcs = pos.p & our & 0x0000ffffffffff00 & (~occ >> 8) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 8;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x000000000000ff00 & (~occ >> 8) & (~occ >> 16) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 16;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        uint64_t their = pos.w;
        pcs = pos.p & our & 0x0000fefefefefe00 & (their >> 7) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 7;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00007f7f7f7f7f00 & (their >> 9) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 9;
            *stack = Move(Pawn, src, dst);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        // Promotions (also capturing)
        pcs = pos.p & our & 0x00ff000000000000 & (~occ >> 8) & (~anyPins | pins.pinnedSN);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 8;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x00fe000000000000 & (their >> 7) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 7;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }

        pcs = pos.p & our & 0x007f000000000000 & (their >> 9) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs))
        {
            dst = src + 9;
            *stack = Move(Pawn, src, dst, Knight);
            ++stack;
            *stack = Move(Pawn, src, dst, Bishop);
            ++stack;
            *stack = Move(Pawn, src, dst, Rook);
            ++stack;
            *stack = Move(Pawn, src, dst, Queen);
            ++stack;
            pcs ^= (1ULL << src);
            //pcs &= (pcs - 1);
        }
    }

    // En passant
    if (pos.state & EPValid)
    {
        uint64_t EPSquare = (pos.state >> 5) & 63;
        if (pos.state & TurnWhite)
        {
            uint64_t our = pos.w;

            // Because EP removes two pieces from the same row, horizontal pins need an extra check
            uint64_t king = pos.k & our;
            unsigned long kingSq;
            _BitScanForward64(&kingSq, king);
            bool kingOnEPRow = (kingSq >> 3) == 3;

            uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW);
            while (_BitScanForward64(&src, pcs)) // Use while instead if to avoid goto-statement (see breaks below)
            {
                dst = src - 9;
                if (kingOnEPRow)
                {
                    uint64_t left = rays[src - 1].W & occ;
                    uint64_t right = rays[src].E & occ;

                    unsigned long hit;
                    if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }

            pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE);
            while (_BitScanForward64(&src, pcs))
            {
                dst = src - 7;

                if (kingOnEPRow)
                {
                    uint64_t left = rays[src].W & occ;
                    uint64_t right = rays[src + 1].E & occ;

                    unsigned long hit;
                    if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }
        }
        else
        {
            uint64_t our = ~pos.w;

            // Because EP removes two pieces from the same row, horizontal pins need an extra check
            uint64_t king = pos.k & our;
            unsigned long kingSq;
            _BitScanForward64(&kingSq, king);
            bool kingOnEPRow = (kingSq >> 3) == 4;

            uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE);
            while (_BitScanForward64(&src, pcs))
            {
                dst = src + 7;

                if (kingOnEPRow)
                {
                    uint64_t left = rays[src - 1].W & occ;
                    uint64_t right = rays[src].E & occ;

                    unsigned long hit;
                    if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }

            pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW);
            while (_BitScanForward64(&src, pcs))
            {
                dst = src + 9;

                if (kingOnEPRow)
                {
                    uint64_t left = rays[src].W & occ;
                    uint64_t right = rays[src + 1].E & occ;

                    unsigned long hit;
                    if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                    {
                        if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                    else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                    {
                        if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                    }
                }

                *stack = Move(Pawn, src, dst);
                ++stack;
                break;
            }
        }
    }

    return stack;
}

Move* generateN(const Position& pos, Move* stack, uint64_t occ, uint64_t anyPins)
{
    unsigned long src, dst;
    
    uint64_t our = (pos.state & TurnWhite) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.n & our & ~anyPins;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = nmoves[src] & ~our;
        while (_BitScanForward64(&dst, sqrs))
        {
            *stack = Move(Knight, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs &= (pcs - 1);
    }

    return stack;
}

Move* generateB(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (pos.state & TurnWhite) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.bq & ~pos.rq & our & ~(pins.pinnedSN | pins.pinnedWE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!(pins.pinnedSENW & (1ULL << src))) sqrs |= swneMoves(src, occ);
        if (!(pins.pinnedSWNE & (1ULL << src))) sqrs |= senwMoves(src, occ);        
        sqrs &= ~our;
        while (_BitScanForward64(&dst, sqrs))
        {
            *stack = Move(Bishop, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

Move* generateR(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (pos.state & TurnWhite) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.rq & ~pos.bq & our & ~(pins.pinnedSENW | pins.pinnedSWNE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!(pins.pinnedWE & (1ULL << src))) sqrs |= snMoves(src, occ);
        if (!(pins.pinnedSN & (1ULL << src))) sqrs |= weMoves(src, occ);
        sqrs &= ~our;
        while (_BitScanForward64(&dst, sqrs))
        {
            *stack = Move(Rook, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

Move* generateQ(const Position& pos, Move* stack, uint64_t occ, const Pins& pins)
{
    unsigned long src, dst;

    uint64_t our = (pos.state & TurnWhite) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.bq & pos.rq & our;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;

        if (!(anyPins & ~pins.pinnedWE & (1ULL << src))) sqrs |= weMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSN & (1ULL << src))) sqrs |= snMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSWNE & (1ULL << src))) sqrs |= swneMoves(src, occ);
        if (!(anyPins & ~pins.pinnedSENW & (1ULL << src))) sqrs |= senwMoves(src, occ);

        sqrs &= ~our;
        while (_BitScanForward64(&dst, sqrs))
        {
            *stack = Move(Queen, src, dst);
            ++stack;
            sqrs &= (sqrs - 1);
        }
        pcs ^= (1ULL << src);
    }

    return stack;
}

Move* generateK(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea)
{
    unsigned long src, dst;

    uint64_t our = (pos.state & TurnWhite) ? pos.w : occ & ~pos.w;
    uint64_t pcs = pos.k & our;
    _BitScanForward64(&src, pcs);
    uint64_t sqrs = kmoves[src] & ~our & ~pArea;
    while (_BitScanForward64(&dst, sqrs))
    {
        *stack = Move(King, src, dst);
        ++stack;
        sqrs &= (sqrs - 1);
    }

    return stack;
}

Move* generateCastling(const Position& pos, Move* stack,  uint64_t occ, uint64_t pArea)
{
    if (pos.state & TurnWhite)
    {
        if (pos.state & CastlingWhiteShort)
        {
            if ((pArea & 0x7000000000000000ULL) == 0 && (occ & 0x6000000000000000ULL) == 0)
            {
                *stack = Move(King, E1, G1);
                ++stack;
            }
        }
        if (pos.state & CastlingWhiteLong)
        {
            if ((pArea & 0x1c00000000000000ULL) == 0 && (occ & 0x0e00000000000000ULL) == 0)
            {
                *stack = Move(King, E1, C1);
                ++stack;
            }
        }
    }
    else
    {
        if (pos.state & CastlingBlackShort)
        {
            if ((pArea & 0x0000000000000070ULL) == 0 && (occ & 0x0000000000000060ULL) == 0)
            {
                *stack = Move(King, E8, G8);
                ++stack;
            }
        }
        if (pos.state & CastlingBlackLong)
        {
            if ((pArea & 0x000000000000001cULL) == 0 && (occ & 0x000000000000000eULL) == 0)
            {
                *stack = Move(King, E8, C8);
                ++stack;
            }
        }
    }

    return stack;
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
    uint64_t pcs = pos.n & our & nmoves[dst] & ~anyPins;
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Knight, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    uint64_t swne = swneMoves(dst, occ);
    uint64_t senw = senwMoves(dst, occ);
    uint64_t we = weMoves(dst, occ);
    uint64_t sn = snMoves(dst, occ);

    pcs = pos.bq & ~pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Bishop, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & ~pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Bishop, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.rq & ~pos.bq & our & we & (~anyPins | pins.pinnedWE);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Rook, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.rq & ~pos.bq & our & sn & (~anyPins | pins.pinnedSN);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Rook, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & we & (~anyPins | pins.pinnedWE);
    while (_BitScanForward64(&src, pcs))
    {
        *stack = Move(Queen, src, dst);
        ++stack;
        pcs ^= (1ULL << src);
    }

    pcs = pos.bq & pos.rq & our & sn & (~anyPins | pins.pinnedSN);
    while (_BitScanForward64(&src, pcs))
    {
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
    _BitScanForward64(&kingSq, king);

    if (rays[kingSq].N & (1ULL << dst))
    {
        for (unsigned long i = dst + 8; i < kingSq; i += 8)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].S & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 8; i < dst; i += 8)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].W & (1ULL << dst))
    {
        for (unsigned long i = dst + 1; i < kingSq; i++)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].E & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 1; i < dst; i++)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].SW & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 7; i < dst; i += 7)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].NW & (1ULL << dst))
    {
        for (unsigned long i = dst + 9; i < kingSq; i += 9)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].NE & (1ULL << dst))
    {
        for (unsigned long i = dst + 7; i < kingSq; i += 7)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }
    else if (rays[kingSq].SE & (1ULL << dst))
    {
        for (unsigned long i = kingSq + 9; i < dst; i += 9)
        {
            stack = generateMovesTo(pos, i, stack, occ, pins);
        }
    }

    return stack;
}

Move* generateCheckEvasions(const Position& pos, Move* stack, uint64_t occ, uint64_t pArea, uint64_t checkers, const Pins& pins)
{
    stack = generateK(pos, stack, occ, pArea);

    unsigned long dst;
    _BitScanForward64(&dst, checkers);
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

template<>
uint64_t countP<Black>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    
    uint64_t our = occ & ~pos.w;
    uint64_t their = pos.w;
    uint64_t empty = ~occ;
    uint64_t ourPawns = pos.p & our;
    uint64_t unblockedPawns = ourPawns & (empty >> 8) & (~anyPins | pins.pinnedSN);

    uint64_t pcs = unblockedPawns & 0x0000ffffffffff00;
    count += __popcnt64(pcs);        

    pcs = unblockedPawns & 0x000000000000ff00 & (empty >> 16);
    count += __popcnt64(pcs);
        
    uint64_t rightCapturingPawns = ourPawns & (their >> 7) & (~anyPins | pins.pinnedSWNE);
        
    pcs = rightCapturingPawns & 0x0000fefefefefe00;
    count += __popcnt64(pcs);
        
    uint64_t leftCapturingPawns = ourPawns & (their >> 9) & (~anyPins | pins.pinnedSENW);

    pcs = leftCapturingPawns & 0x00007f7f7f7f7f00;
    count += __popcnt64(pcs);

    // Promotions (also capturing)
    pcs = unblockedPawns & 0x00ff000000000000;
    count += __popcnt64(pcs) * 4;
        
    pcs = rightCapturingPawns & 0x00fe000000000000;
    count += __popcnt64(pcs) * 4;

    pcs = leftCapturingPawns & 0x007f000000000000;
    count += __popcnt64(pcs) * 4;

    // En passant
    if (pos.state & EPValid)
    {
        uint64_t EPSquare = (pos.state >> 5) & 63;
        
        uint64_t our = ~pos.w;

        // Because EP removes two pieces from the same row, horizontal pins need an extra check
        uint64_t king = pos.k & our;
        unsigned long kingSq;
        _BitScanForward64(&kingSq, king);
        bool kingOnEPRow = (kingSq >> 3) == 4;

        uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            if (kingOnEPRow)
            {
                uint64_t left = rays[src - 1].W & occ;
                uint64_t right = rays[src].E & occ;

                unsigned long hit;
                if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                {
                    if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
                else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                {
                    if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
            }

            ++count;
            break;
        }

        pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs))
        {
            if (kingOnEPRow)
            {
                uint64_t left = rays[src].W & occ;
                uint64_t right = rays[src + 1].E & occ;

                unsigned long hit;
                if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                {
                    if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
                else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                {
                    if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
            }

            ++count;
            break;
        }
    }

    return count;
}

template<>
uint64_t countP<White>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
   
    uint64_t our = pos.w;
    uint64_t their = occ & ~pos.w;
    uint64_t empty = ~occ;
    uint64_t ourPawns = pos.p & our;
    uint64_t unblockedPawns = ourPawns & (empty << 8) & (~anyPins | pins.pinnedSN);

    uint64_t pcs = unblockedPawns & 0x00ffffffffff0000;
    count += __popcnt64(pcs);

    pcs = unblockedPawns & 0x00ff000000000000 & (empty << 16);
    count += __popcnt64(pcs);

    uint64_t leftCapturingPawns = ourPawns & (their << 9) & (~anyPins | pins.pinnedSENW);

    pcs = leftCapturingPawns & 0x00fefefefefe0000;
    count += __popcnt64(pcs);

    uint64_t rightCapturingPawns = ourPawns & (their << 7) & (~anyPins | pins.pinnedSWNE);

    pcs = rightCapturingPawns & 0x007f7f7f7f7f0000;
    count += __popcnt64(pcs);

    // Promotions (also capturing)
    pcs = unblockedPawns & 0x000000000000ff00;
    count += __popcnt64(pcs) * 4;

    pcs = leftCapturingPawns & 0x000000000000fe00;
    count += __popcnt64(pcs) * 4;

    pcs = rightCapturingPawns & 0x0000000000007f00;
    count += __popcnt64(pcs) * 4;

    // En passant
    if (pos.state & EPValid)
    {
        uint64_t EPSquare = (pos.state >> 5) & 63;
        
        uint64_t our = pos.w;

        // Because EP removes two pieces from the same row, horizontal pins need an extra check
        uint64_t king = pos.k & our;
        unsigned long kingSq;
        _BitScanForward64(&kingSq, king);
        bool kingOnEPRow = (kingSq >> 3) == 3;

        uint64_t pcs = pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW);
        while (_BitScanForward64(&src, pcs)) // Use while instead if to avoid goto-statement (see breaks below)
        {
            if (kingOnEPRow)
            {
                uint64_t left = rays[src - 1].W & occ;
                uint64_t right = rays[src].E & occ;

                unsigned long hit;
                if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                {
                    if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
                else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                {
                    if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
            }

            ++count;
            break;
        }

        pcs = pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE);
        while (_BitScanForward64(&src, pcs))
        {
            if (kingOnEPRow)
            {
                uint64_t left = rays[src].W & occ;
                uint64_t right = rays[src + 1].E & occ;

                unsigned long hit;
                if (_BitScanReverse64(&hit, left) && (hit == kingSq))
                {
                    if (_BitScanForward64(&hit, right) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
                else if (_BitScanForward64(&hit, right) && (hit == kingSq))
                {
                    if (_BitScanReverse64(&hit, left) && ((1ULL << hit) & pos.rq & ~our)) break;
                }
            }

            ++count;
            break;
        }
    }

    return count;
}

template<>
uint64_t countN<Black>(const Position& pos, uint64_t occ, uint64_t anyPins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = occ & ~pos.w;
    uint64_t pcs = pos.n & our & ~anyPins;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = nmoves[src] & ~our;
        count += __popcnt64(sqrs);
        pcs &= (pcs - 1);
    }

    return count;
}

template<>
uint64_t countN<White>(const Position& pos, uint64_t occ, uint64_t anyPins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = pos.w;
    uint64_t pcs = pos.n & our & ~anyPins;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = nmoves[src] & ~our;
        count += __popcnt64(sqrs);
        pcs &= (pcs - 1);
    }

    return count;
}

template<>
uint64_t countB<Black>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = occ & ~pos.w;
    uint64_t pcs = pos.bq & ~pos.rq & our & ~(pins.pinnedSN | pins.pinnedWE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!((pins.pinnedSENW | pins.pinnedSWNE) & (1ULL << src)))
        {
            sqrs = bmoves(src, occ);
        }
        else
        {
            if (!(pins.pinnedSENW & (1ULL << src))) sqrs |= swneMoves(src, occ);
            if (!(pins.pinnedSWNE & (1ULL << src))) sqrs |= senwMoves(src, occ);
        }
        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countB<White>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = pos.w;
    uint64_t pcs = pos.bq & ~pos.rq & our & ~(pins.pinnedSN | pins.pinnedWE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!((pins.pinnedSENW | pins.pinnedSWNE) & (1ULL << src)))
        {
            sqrs = bmoves(src, occ);
        }
        else
        {
            if (!(pins.pinnedSENW & (1ULL << src))) sqrs |= swneMoves(src, occ);
            if (!(pins.pinnedSWNE & (1ULL << src))) sqrs |= senwMoves(src, occ);
        }
        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countR<Black>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = occ & ~pos.w;
    uint64_t pcs = pos.rq & ~pos.bq & our & ~(pins.pinnedSENW | pins.pinnedSWNE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!((pins.pinnedSN | pins.pinnedWE) & (1ULL << src)))
        {
            sqrs = rmoves(src, occ);
        }
        else
        {
            if (!(pins.pinnedWE & (1ULL << src))) sqrs |= snMoves(src, occ);
            if (!(pins.pinnedSN & (1ULL << src))) sqrs |= weMoves(src, occ);
        }
        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countR<White>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = pos.w;
    uint64_t pcs = pos.rq & ~pos.bq & our & ~(pins.pinnedSENW | pins.pinnedSWNE);
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;
        if (!((pins.pinnedSN | pins.pinnedWE) & (1ULL << src)))
        {
            sqrs = rmoves(src, occ);
        }
        else
        {
            if (!(pins.pinnedWE & (1ULL << src))) sqrs |= snMoves(src, occ);
            if (!(pins.pinnedSN & (1ULL << src))) sqrs |= weMoves(src, occ);
        }
        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countQ<Black>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = occ & ~pos.w;
    uint64_t pcs = pos.bq & pos.rq & our;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;

        if (!(anyPins & (1ULL << src)))
        {
            sqrs |= bmoves(src, occ);
            sqrs |= rmoves(src, occ);
        }
        else
        {
            if (!(anyPins & ~pins.pinnedWE & (1ULL << src))) sqrs |= weMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSN & (1ULL << src))) sqrs |= snMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSWNE & (1ULL << src))) sqrs |= swneMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSENW & (1ULL << src))) sqrs |= senwMoves(src, occ);
        }

        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countQ<White>(const Position& pos, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = pos.w;
    uint64_t pcs = pos.bq & pos.rq & our;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    while (_BitScanForward64(&src, pcs))
    {
        uint64_t sqrs = 0;

        if (!(anyPins & (1ULL << src)))
        {
            sqrs |= bmoves(src, occ);
            sqrs |= rmoves(src, occ);
        }
        else
        {
            if (!(anyPins & ~pins.pinnedWE & (1ULL << src))) sqrs |= weMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSN & (1ULL << src))) sqrs |= snMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSWNE & (1ULL << src))) sqrs |= swneMoves(src, occ);
            if (!(anyPins & ~pins.pinnedSENW & (1ULL << src))) sqrs |= senwMoves(src, occ);
        }

        sqrs &= ~our;
        count += __popcnt64(sqrs);
        pcs ^= (1ULL << src);
    }

    return count;
}

template<>
uint64_t countK<Black>(const Position& pos, uint64_t occ, const uint64_t pArea)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = occ & ~pos.w;
    uint64_t pcs = pos.k & our;
    _BitScanForward64(&src, pcs);
    uint64_t sqrs = kmoves[src] & ~our & ~pArea;
    count += __popcnt64(sqrs);

    return count;
}

template<>
uint64_t countK<White>(const Position& pos, uint64_t occ, const uint64_t pArea)
{
    uint64_t count = 0;

    unsigned long src;

    uint64_t our = pos.w;
    uint64_t pcs = pos.k & our;
    _BitScanForward64(&src, pcs);
    uint64_t sqrs = kmoves[src] & ~our & ~pArea;
    count += __popcnt64(sqrs);

    return count;
}

template<>
uint64_t countCastling<Black>(const Position& pos, uint64_t occ, uint64_t pArea)
{
    uint64_t count = 0;

    if (pos.state & CastlingBlackShort)
    {
        if ((pArea & 0x0000000000000070ULL) == 0 && (occ & 0x0000000000000060ULL) == 0)
        {
            ++count;
        }
    }
    if (pos.state & CastlingBlackLong)
    {
        if ((pArea & 0x000000000000001cULL) == 0 && (occ & 0x000000000000000eULL) == 0)
        {
            ++count;
        }
    }

    return count;
}

template<>
uint64_t countCastling<White>(const Position& pos, uint64_t occ, uint64_t pArea)
{
    uint64_t count = 0;

    if (pos.state & CastlingWhiteShort)
    {
        if ((pArea & 0x7000000000000000ULL) == 0 && (occ & 0x6000000000000000ULL) == 0)
        {
            ++count;
        }
    }
    if (pos.state & CastlingWhiteLong)
    {
        if ((pArea & 0x1c00000000000000ULL) == 0 && (occ & 0x0e00000000000000ULL) == 0)
        {
            ++count;
        }
    }    

    return count;
}

template<>
uint64_t countMovesTo<Black>(const Position& pos, unsigned long dst, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    uint64_t our = ~pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    bool isCapture = occ & (1ULL << dst);
    
    if (isCapture)
    {
        if (pos.p & our & 0x0000fefefefefe00ULL & (1ULL << (dst - 7)) & (~anyPins | pins.pinnedSWNE))
        {
            ++count;
        }
        if (pos.p & our & 0x00007f7f7f7f7f00ULL & (1ULL << (dst - 9)) & (~anyPins | pins.pinnedSENW))
        {
            ++count;
        }

        // Special case: The target square has a pawn that can be captured with en passant
        if ((pos.state & EPValid) && ((1ULL << dst) & 0x000000ff00000000 & pos.p))
        {
            uint64_t EPSquare = (pos.state >> 5) & 63;
            if (EPSquare - 8 == dst)
            {
                if (pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare - 7)) & (~anyPins | pins.pinnedSWNE))
                {
                    ++count;
                }
                if (pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare - 9)) & (~anyPins | pins.pinnedSENW))
                {
                    ++count;
                }
            }
        }

        if (pos.p & our & 0x00fe000000000000ULL & (1ULL << (dst - 7)) & (~anyPins | pins.pinnedSWNE))
        {
            count += 4;
        }
        if (pos.p & our & 0x007f000000000000ULL & (1ULL << (dst - 9)) & (~anyPins | pins.pinnedSENW))
        {
            count += 4;
        }
    }
    else
    {
        if (pos.p & our & 0x0000ffffffffff00 & (1ULL << (dst - 8)) & (~anyPins | pins.pinnedSN))
        {
            ++count;
        }
        if (pos.p & our & 0x000000000000ff00 & (1ULL << (dst - 16)) & (~occ >> 8) & (~anyPins | pins.pinnedSN))
        {
            ++count;
        }

        if (pos.p & our & 0x00ff000000000000 & (1ULL << (dst - 8)) & (~anyPins | pins.pinnedSN))
        {
            count += 4;
        }
    }

    uint64_t pcs = pos.n & our & nmoves[dst] & ~anyPins;
    count += __popcnt64(pcs);

    uint64_t swne = swneMoves(dst, occ);
    uint64_t senw = senwMoves(dst, occ);
    uint64_t we = weMoves(dst, occ);
    uint64_t sn = snMoves(dst, occ);

    pcs = pos.bq & ~pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    count += __popcnt64(pcs);
    
    pcs = pos.bq & ~pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    count += __popcnt64(pcs);
    
    pcs = pos.rq & ~pos.bq & our & we & (~anyPins | pins.pinnedWE);
    count += __popcnt64(pcs);
    
    pcs = pos.rq & ~pos.bq & our & sn & (~anyPins | pins.pinnedSN);
    count += __popcnt64(pcs);
    
    pcs = pos.bq & pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    count += __popcnt64(pcs);
    
    pcs = pos.bq & pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    count += __popcnt64(pcs);
    
    pcs = pos.bq & pos.rq & our & we & (~anyPins | pins.pinnedWE);
    count += __popcnt64(pcs);
    
    pcs = pos.bq & pos.rq & our & sn & (~anyPins | pins.pinnedSN);
    count += __popcnt64(pcs);
    
    // Ignore king captures, because they are generate as part of king moves

    return count;
}

template<>
uint64_t countMovesTo<White>(const Position& pos, unsigned long dst, uint64_t occ, const Pins& pins)
{
    uint64_t count = 0;

    uint64_t our = pos.w;
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    bool isCapture = occ & (1ULL << dst);
    
    if (isCapture)
    {
        if (pos.p & our & 0x00fefefefefe0000ULL & (1ULL << (dst + 9)) & (~anyPins | pins.pinnedSENW))
        {
            ++count;
        }
        if (pos.p & our & 0x007f7f7f7f7f0000ULL & (1ULL << (dst + 7)) & (~anyPins | pins.pinnedSWNE))
        {
            ++count;
        }

        // Special case: The target square has a pawn that can be captured with en passant
        if ((pos.state & EPValid) && ((1ULL << dst) & 0x00000000ff000000 & pos.p))
        {
            uint64_t EPSquare = (pos.state >> 5) & 63;
            if (EPSquare + 8 == dst)
            {
                if (pos.p & our & 0xfefefefefefefefeULL & (1ULL << (EPSquare + 9)) & (~anyPins | pins.pinnedSENW))
                {
                    ++count;
                }
                if (pos.p & our & 0x7f7f7f7f7f7f7f7fULL & (1ULL << (EPSquare + 7)) & (~anyPins | pins.pinnedSWNE))
                {
                    ++count;
                }
            }
        }

        if (pos.p & our & 0x000000000000fe00ULL & (1ULL << (dst + 9)) & (~anyPins | pins.pinnedSENW))
        {
            count += 4;
        }
        if (pos.p & our & 0x0000000000007f00ULL & (1ULL << (dst + 7)) & (~anyPins | pins.pinnedSWNE))
        {
            count += 4;
        }
    }
    else
    {
        if (pos.p & our & 0x00ffffffffff0000 & (1ULL << (dst + 8)) & (~anyPins | pins.pinnedSN))
        {
            ++count;
        }
        if (pos.p & our & 0x00ff000000000000 & (1ULL << (dst + 16)) & (~occ << 8) & (~anyPins | pins.pinnedSN))
        {
            ++count;
        }

        if (pos.p & our & 0x000000000000ff00 & (1ULL << (dst + 8)) & (~anyPins | pins.pinnedSN))
        {
            count += 4;
        }
    }

    uint64_t pcs = pos.n & our & nmoves[dst] & ~anyPins;
    count += __popcnt64(pcs);

    uint64_t swne = swneMoves(dst, occ);
    uint64_t senw = senwMoves(dst, occ);
    uint64_t we = weMoves(dst, occ);
    uint64_t sn = snMoves(dst, occ);

    pcs = pos.bq & ~pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    count += __popcnt64(pcs);

    pcs = pos.bq & ~pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    count += __popcnt64(pcs);

    pcs = pos.rq & ~pos.bq & our & we & (~anyPins | pins.pinnedWE);
    count += __popcnt64(pcs);

    pcs = pos.rq & ~pos.bq & our & sn & (~anyPins | pins.pinnedSN);
    count += __popcnt64(pcs);

    pcs = pos.bq & pos.rq & our & swne & (~anyPins | pins.pinnedSWNE);
    count += __popcnt64(pcs);

    pcs = pos.bq & pos.rq & our & senw & (~anyPins | pins.pinnedSENW);
    count += __popcnt64(pcs);

    pcs = pos.bq & pos.rq & our & we & (~anyPins | pins.pinnedWE);
    count += __popcnt64(pcs);

    pcs = pos.bq & pos.rq & our & sn & (~anyPins | pins.pinnedSN);
    count += __popcnt64(pcs);

    // Ignore king captures, because they are generate as part of king moves

    return count;
}


uint64_t swneMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (swneExMask[src] & occ) * bFile >> 58;
    return swneExMask[src] & KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t bRays = 0;

    _BitScanForward64(&hit, (rays[src].SW & occ) | 0x8000000000000000);
    bRays |= rays[src].SW;
    bRays ^= rays[hit].SW;

    _BitScanReverse64(&hit, (rays[src].NE & occ) | 0x0000000000000001);
    bRays |= rays[src].NE;
    bRays ^= rays[hit].NE;

    return bRays;
#endif
}

uint64_t senwMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (senwExMask[src] & occ) * bFile >> 58;
    return senwExMask[src] & KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t bRays = 0;

    _BitScanForward64(&hit, (rays[src].SE & occ) | 0x8000000000000000);
    bRays |= rays[src].SE;
    bRays ^= rays[hit].SE;

    _BitScanReverse64(&hit, (rays[src].NW & occ) | 0x0000000000000001);
    bRays |= rays[src].NW;
    bRays ^= rays[hit].NW;

    return bRays;
#endif
}

uint64_t bmoves(unsigned long src, uint64_t occ)
{
#if PEXT_INTRINSIC
    uint64_t mask = BMasks[src];
    uint64_t index = _pext_u64(occ, mask);
    uint64_t moves = BAttacks[src][index];
    return moves;
#elif MAGIC_BITBOARDS
    uint64_t index = occ & BMagic[src].mask;
    index *= BMagic[src].magic;
    index >>= BBits[src];
    uint64_t moves = BAttacks[src][index];
    return moves;
#else
    return swneMoves(src, occ) | senwMoves(src, occ);
#endif
}

uint64_t weMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t bFile = 0x0202020202020202ULL;
    uint64_t index = (weExMask[src] & occ) * bFile >> 58;
    return weExMask[src] & KinderGartenAttacks[src & 7][index];
#else
    unsigned long hit;
    uint64_t rRays = 0;

    _BitScanForward64(&hit, (rays[src].E & occ) | 0x8000000000000000);
    rRays |= rays[src].E;
    rRays ^= rays[hit].E;

    _BitScanReverse64(&hit, (rays[src].W & occ) | 0x0000000000000001);
    rRays |= rays[src].W;
    rRays ^= rays[hit].W;

    return rRays;
#endif
}

uint64_t snMoves(unsigned long src, uint64_t occ)
{
#if KINDERGARTEN_BITBOARDS
    const uint64_t AFile = 0x0101010101010101ULL;
    const uint64_t c7h2 = 0x0080402010080400ULL;
    uint64_t index = AFile & (occ >> (src & 7));
    index = (c7h2 * index) >> 58;
    return AFileAttacks[src >> 3][index] << (src & 7);
#else
    unsigned long hit;
    uint64_t rRays = 0;

    _BitScanForward64(&hit, (rays[src].S & occ) | 0x8000000000000000);
    rRays |= rays[src].S;
    rRays ^= rays[hit].S;

    _BitScanReverse64(&hit, (rays[src].N & occ) | 0x0000000000000001);
    rRays |= rays[src].N;
    rRays ^= rays[hit].N;

    return rRays;
#endif
}

uint64_t rmoves(unsigned long src, uint64_t occ)
{
#if PEXT_INTRINSIC
    uint64_t mask = RMasks[src];
    uint64_t index = _pext_u64(occ, mask);
    uint64_t moves = RAttacks[src][index];
    return moves;
#elif MAGIC_BITBOARDS
    const Magic& m = RMagic[src];
    uint64_t index = occ & m.mask;
    index *= m.magic;
    index >>= m.shift;//RBits[src];
    uint64_t moves = m.ptr[index];//RAttacks[src][index];
    return moves;
#else
    return weMoves(src, occ) | snMoves(src, occ);
#endif
}

uint64_t findPinsAndCheckers(const Position& pos, uint64_t occ, Pins& pins)
{
    memset(&pins, 0, sizeof(Pins));

    uint64_t our, king;
    uint64_t pawnsLeft, pawnsRight;
    if (pos.state & TurnWhite)
    {
        our = pos.w;
        king = pos.k & pos.w;
        pawnsLeft = king >> 9;
        pawnsRight = king >> 7;
    }
    else
    {
        our = ~pos.w;
        king = pos.k & ~pos.w;
        pawnsLeft = king << 7;
        pawnsRight = king << 9;
    }
    
    uint64_t their = ~our;    

    unsigned long src;
    _BitScanForward64(&src, king);

    uint64_t checkers = 0;
        
    checkers |= pos.p & their & 0x7f7f7f7f7f7f7f7fULL & pawnsLeft;
    checkers |= pos.p & their & 0xfefefefefefefefeULL & pawnsRight;    

    checkers |= pos.n & their & nmoves[src];

    // Sliding pieces - handle pins and checks at once
    uint64_t bPcs = pos.bq & their;
    uint64_t rPcs = pos.rq & their;

    unsigned long hit1, hit2;
    uint64_t ray = rays[src].SE;
    uint64_t attackers = ray & bPcs;
    if (attackers)
    {        
        ray &= occ;
        _BitScanForward64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanForward64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSENW |= potentialPinned;
    }

    ray = rays[src].NW;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSENW |= potentialPinned;
    }

    ray = rays[src].SW;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanForward64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanForward64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSWNE |= potentialPinned;
    }

    ray = rays[src].NE;
    attackers = ray & bPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & bPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & bPcs)
            pins.pinnedSWNE |= potentialPinned;
    }

    ray = rays[src].S;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanForward64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanForward64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedSN |= potentialPinned;
    }

    ray = rays[src].N;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedSN |= potentialPinned;
    }

    ray = rays[src].W;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanReverse64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanReverse64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedWE |= potentialPinned;
    }

    ray = rays[src].E;
    attackers = ray & rPcs;
    if (attackers)
    {
        ray &= occ;
        _BitScanForward64(&hit1, ray);
        uint64_t firstHit = (1ULL << hit1);
        checkers |= firstHit & rPcs;
        uint64_t potentialPinned = firstHit & occ & our;
        ray &= ~potentialPinned;
        _BitScanForward64(&hit2, ray);
        if ((1ULL << hit2) & rPcs)
            pins.pinnedWE |= potentialPinned;
    }

    // King cannot pin or check

    return checkers;
}

uint64_t findProtectionArea(const Position& pos, uint64_t occ)
{
    unsigned long src;
    uint64_t pArea = 0;

    uint64_t their = pos.state & TurnWhite ? ~pos.w : pos.w;
    if (pos.state & TurnWhite)
    {
        pArea |= ((pos.p & their) & 0xfefefefefefefefe) << 7;
        pArea |= ((pos.p & their) & 0x7f7f7f7f7f7f7f7f) << 9;
    }
    else
    {
        pArea |= ((pos.p & their) & 0xfefefefefefefefe) >> 9;
        pArea |= ((pos.p & their) & 0x7f7f7f7f7f7f7f7f) >> 7;
    }     
    
    uint64_t pcs = pos.n & their;
    while (_BitScanForward64(&src, pcs))
    {
        pArea |= nmoves[src];
        pcs &= (pcs - 1);
    }    

    occ ^= (pos.k & ~their); // King doesn't block the sliding pieces' protection area

    pcs = pos.bq & their;
    while (_BitScanForward64(&src, pcs))
    {
        pcs &= (pcs - 1);
        pArea |= bmoves(src, occ);
    }

    pcs = pos.rq & their;
    while (_BitScanForward64(&src, pcs))
    {
        pcs &= (pcs - 1);
        pArea |= rmoves(src, occ);
    }

    pcs = pos.k & their;
    _BitScanForward64(&src, pcs);
    pArea |= kmoves[src];

    return pArea;
}
