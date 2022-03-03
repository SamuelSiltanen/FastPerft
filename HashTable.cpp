#include "HashTable.hpp"

#include <intrin.h>
#include <random>
#include <cstdio>

#pragma intrinsic(_BitScanForward64)

//#define DEBUG_DUMP

#ifdef MULTITHREADED
#define LOCK(x) x.m_lock.lock();
#define UNLOCK(x) x.m_lock.unlock();
#else
#define LOCK(x) 
#define UNLOCK(x) 
#endif

#ifdef HASH_DEBUG
bool HashEntry::posEqual(const Position& pos)
{
    if ((pos.bq | pos.rq) != bqr) return false;
    if (((pos.rq & ~pos.bq) | pos.k | pos.n) != rkn) return false;
    if (((pos.bq & ~pos.rq) | pos.p | pos.n) != npb) return false;
    if (pos.w != w) return false;
    if (pos.state != state) return false;
    return true;
}
#endif

HashTable::Hashes HashTable::hashKeys[64];
bool HashTable::hashesReady = false;

HashTable::HashTable(uint32_t sizeExp)
    : m_size(1 << sizeExp)
    , m_sizeExp(sizeExp)
{
    m_hashTable = (HashEntry*)_aligned_malloc(m_size * sizeof(HashEntry), 64);
    clear();
    if (!hashesReady)
    {
        initHashes();
        hashesReady = true;
    }
}

HashTable::~HashTable()
{
    if (m_hashTable)
    {
#ifdef DEBUG_DUMP
        FILE* f = nullptr;
        errno_t err = fopen_s(&f, "hash_table_debug.csv", "wb");
        if (err)
        {
            printf("Opening a file failed with error code %d\n", err);
        }
        else
        {
            fprintf(f, "Index,Elements\n");
            for (size_t i = 0; i < m_size; ++i)
            {
                fprintf(f, "%d,%d\n", i, m_hashTable[i].empty() ? 0 : 1);
            }

            fclose(f);
        }
#endif
        _aligned_free(m_hashTable);
        m_hashTable = nullptr;
    }
}

bool HashTable::insert(const HashEntry& entry)
{
    uint32_t index = mapToIndex(entry.hash);

    LOCK(this);

    if (m_hashTable[index].empty() ||
        (m_hashTable[index].hash == entry.hash && m_hashTable[index].depth() == entry.depth()))
    {
        m_hashTable[index] = entry;
        UNLOCK(this);
        return true;
    }
    else
    {
        uint32_t cacheLineStartIndex = index & 0xfffffffc;

        int bestReplacement = -1;
        int64_t bestScore = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (m_hashTable[cacheLineStartIndex + i].empty()) // First try empty slots
            {
                bestReplacement = i;
                break;
            }
            else // Then calculate replacement score
            {
                int64_t score = replacementPolicy(m_hashTable[cacheLineStartIndex + i], entry);
                if (score > bestScore)
                {
                    bestReplacement = i;
                    bestScore = score;
                }
            }
        }

        if (bestReplacement >= 0)
        {
            m_hashTable[cacheLineStartIndex + bestReplacement] = entry;
            UNLOCK(this);
            return true;
        }
    }

    UNLOCK(this);
    return false;
}

uint64_t HashTable::find(const Position& pos, uint16_t depth)
{
    uint32_t index = mapToIndex(pos.hash);    
    
    LOCK(this);

    uint32_t cacheLineStartIndex = index & 0xfffffffc;

    for (int i = 0; i < 4; ++i)
    {
        if (m_hashTable[cacheLineStartIndex + i].hash == pos.hash &&
            m_hashTable[cacheLineStartIndex + i].depth() == depth)
        {
            UNLOCK(this);
#ifdef HASH_DEBUG
            if (!m_hashTable[cacheLineStartIndex + i].posEqual(pos))
            {
                const HashEntry& e = m_hashTable[cacheLineStartIndex + i];
                printf("Hashes match (%016llx), but positions differ!\n", pos.hash);
                printf("Hash table position:\n");
                printf("p: %016llx\n", e.npb & ~e.rkn & ~e.bqr);
                printf("n: %016llx\n", e.npb & e.rkn);
                printf("b: %016llx\n", e.npb & e.bqr);
                printf("r: %016llx\n", e.rkn & e.bqr);
                printf("q: %016llx\n", e.bqr & ~e.npb & ~e.rkn);
                printf("k: %016llx\n", e.rkn & ~e.bqr & ~e.npb);
                printf("w: %016llx\n", e.w);
                printf("state: %016llx\n", e.state);
                printf("Perft probing position:\n");
                printf("p: %016llx\n", pos.p);
                printf("n: %016llx\n", pos.n);
                printf("b: %016llx\n", pos.bq & ~pos.rq);
                printf("r: %016llx\n", pos.rq & ~pos.bq);
                printf("q: %016llx\n", pos.bq & pos.rq);
                printf("k: %016llx\n", pos.k);
                printf("w: %016llx\n", pos.w);
                printf("state: %016llx\n", pos.state);
            }
#endif
            return m_hashTable[cacheLineStartIndex + i].count();
        }
    }

    UNLOCK(this);

    return InvalidHashTableEntry;
}

void HashTable::clear()
{
    memset(m_hashTable, 0, m_size * sizeof(HashEntry));
}

uint32_t HashTable::mapToIndex(uint64_t hash)
{
    // Just take lowest bits, assuming we have a good hash
    return static_cast<uint32_t>(hash & (m_size - 1));
}

int64_t HashTable::replacementPolicy(const HashEntry& currentEntry, const HashEntry& candidateEntry)
{
    // Simplest possible
    return static_cast<int64_t>(candidateEntry.count()) - static_cast<int64_t>(currentEntry.count());
}

uint64_t HashTable::calcHash(const Position& pos)
{
    uint64_t hash = 0;
    unsigned long sq = 0;

    uint64_t pcs = pos.p;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].p;
        pcs ^= (1ULL << sq);
    }

    pcs = pos.n;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].n;
        pcs ^= (1ULL << sq);
    }

    pcs = pos.bq & ~pos.rq;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].b;
        pcs ^= (1ULL << sq);
    }

    pcs = pos.rq & ~pos.bq;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].r;
        pcs ^= (1ULL << sq);
    }

    pcs = pos.bq & pos.rq;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].q;
        pcs ^= (1ULL << sq);
    }

    pcs = pos.k;
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].k;
        pcs ^= (1ULL << sq);
    }

    if (pos.state & TurnWhite) hash ^= hashKeys[0].state;
    if (pos.state & CastlingWhiteShort) hash ^= hashKeys[1].state;
    if (pos.state & CastlingWhiteLong) hash ^= hashKeys[2].state;
    if (pos.state & CastlingBlackShort) hash ^= hashKeys[3].state;
    if (pos.state & CastlingBlackLong) hash ^= hashKeys[4].state;

    if (pos.state & EPValid)
    {
        uint64_t EPSquare = (pos.state >> 5) & 63;
        hash ^= hashKeys[EPSquare].state; // This is OK, because EPSquare is always 16-23 or 40-47
        hash ^= hashKeys[11].state;
    }

    return hash;
}

uint64_t HashTable::hashCastling(uint64_t oldState, uint64_t newState)
{
    assert(hashesReady);

    uint64_t hash = 0;
    if ((oldState ^ newState) & 0x000000000000001e)
    {
        if ((oldState ^ newState) & CastlingWhiteShort) hash ^= hashKeys[1].state;
        if ((oldState ^ newState) & CastlingWhiteLong) hash ^= hashKeys[2].state;
        if ((oldState ^ newState) & CastlingBlackShort) hash ^= hashKeys[3].state;
        if ((oldState ^ newState) & CastlingBlackLong) hash ^= hashKeys[4].state;
    }
    return hash;
}

uint64_t HashTable::hashEP(uint64_t oldState, uint64_t newState)
{
    assert(hashesReady);

    uint64_t hash = 0;
    if ((oldState ^ newState) & EPValid) hash ^= hashKeys[11].state;
    if ((oldState ^ newState) & 0x00000000000007e0)
    {
        if (oldState & EPValid) hash ^= hashKeys[(oldState >> 5) & 63].state;
        if (newState & EPValid) hash ^= hashKeys[(newState >> 5) & 63].state;
    }
    return hash;
}

void HashTable::initHashes()
{
    std::mt19937_64 generator(0xacdcabba);

    for (int i = 0; i < 64; ++i)
    {
        hashKeys[i].p = generator();
        hashKeys[i].n = generator();
        hashKeys[i].b = generator();
        hashKeys[i].r = generator();
        hashKeys[i].q = generator();
        hashKeys[i].k = generator();
        hashKeys[i].w = generator();
        hashKeys[i].state = generator();
    }
}