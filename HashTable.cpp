// Copyright 2022 Samuel Siltanen
// HashTable.cpp

#include "HashTable.hpp"

#include <intrin.h>
#include <random>
#include <cstdio>

#pragma intrinsic(_BitScanForward64)

//#define DEBUG_DUMP

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
uint64_t HashTable::stateKeys[32];
bool HashTable::hashesReady = false;

static_assert(sizeof(std::atomic<uint64_t>) == sizeof(uint64_t), "64-bit atomics must have no overhead");

HashTable::HashTable(uint32_t sizeExp)
    : m_size(1 << sizeExp)
    , m_sizeExp(sizeExp)
{
    m_hashTable = (HashSlot*)_aligned_malloc(m_size * sizeof(HashSlot), 64);
    clear();
    initHashKeys();
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
                fprintf(f, "%d,%d\n", i, read(static_cast<uint32_t>(i)).empty() ? 0 : 1);
            }

            fclose(f);
        }
#endif
        _aligned_free(m_hashTable);
        m_hashTable = nullptr;
    }
}

HashEntry HashTable::read(uint32_t index) const
{
    const HashSlot& slot = m_hashTable[index];

    HashEntry entry;
    entry.depth_and_count = slot.data.load(std::memory_order_relaxed);
    entry.hash = slot.key.load(std::memory_order_relaxed) ^ entry.depth_and_count;
#ifdef HASH_DEBUG
    entry.bqr = slot.bqr;
    entry.rkn = slot.rkn;
    entry.npb = slot.npb;
    entry.w = slot.w;
    entry.state = slot.state;
#endif
    return entry;
}

void HashTable::write(uint32_t index, const HashEntry& entry)
{
    HashSlot& slot = m_hashTable[index];

    slot.key.store(entry.hash ^ entry.depth_and_count, std::memory_order_relaxed);
    slot.data.store(entry.depth_and_count, std::memory_order_relaxed);
#ifdef HASH_DEBUG
    slot.bqr = entry.bqr;
    slot.rkn = entry.rkn;
    slot.npb = entry.npb;
    slot.w = entry.w;
    slot.state = entry.state;
#endif
}

// Insert an entry to the hash table. Another thread may overwrite the entry at the same time,
// but that only affects the performance, not the validity of the results, because torn entries
// don't match in find. The collisions are so rare that it doesn't make sense to optimize for them,
// but to make the common case as fast as possible.
bool HashTable::insert(const HashEntry& entry)
{
    uint32_t index = mapToIndex(entry.hash);

    HashEntry tableEntry = read(index);

    if (tableEntry.empty() ||
        (tableEntry.hash == entry.hash && tableEntry.depth() == entry.depth()))
    {
        write(index, entry);
        return true;
    }
    else
    {
        uint32_t cacheLineStartIndex = index & 0xfffffffc;

        int bestReplacement = -1;
        int64_t bestScore = 0;
        for (int i = 0; i < 4; ++i)
        {
            tableEntry = read(cacheLineStartIndex + i);

            if (tableEntry.empty()) // First try empty slots
            {
                bestReplacement = i;
                break;
            }
            else // Then calculate replacement score
            {
                int64_t score = replacementPolicy(tableEntry, entry);
                if (score > bestScore)
                {
                    bestReplacement = i;
                    bestScore = score;
                }
            }
        }

        if (bestReplacement >= 0)
        {
            write(cacheLineStartIndex + bestReplacement, entry);
            return true;
        }
    }

    return false;
}

uint64_t HashTable::find(const Position& pos, uint16_t depth)
{
    uint32_t index = mapToIndex(pos.hash);
    uint32_t cacheLineStartIndex = index & 0xfffffffc;

    for (int i = 0; i < 4; ++i)
    {
        HashEntry entry = read(cacheLineStartIndex + i);

        if (entry.hash == pos.hash && entry.depth() == depth)
        {
            uint64_t count = entry.count();

#ifdef HASH_DEBUG
            if (!entry.posEqual(pos))
            {
                const HashEntry& e = entry;
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
            return count;
        }
    }

    return InvalidHashTableEntry;
}

void HashTable::clear()
{
    memset(m_hashTable, 0, m_size * sizeof(HashSlot));
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
    assert(hashesReady);

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

    pcs = pos.w & (pos.p | pos.n | pos.bq | pos.rq | pos.k);
    while (_BitScanForward64(&sq, pcs))
    {
        hash ^= hashKeys[sq].w;
        pcs ^= (1ULL << sq);
    }

    hash ^= stateHash(pos.state);

    return hash;
}

// Initializes the Zobrist keys. Safe to call more than once.
void HashTable::initHashKeys()
{
    if (hashesReady) return;

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

    // Turn (bit 0) and castling rights (bits 1-4) use the state keys of squares 0-4
    for (int bits = 0; bits < 32; ++bits)
    {
        stateKeys[bits] = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (bits & (1 << i)) stateKeys[bits] ^= hashKeys[i].state;
        }
    }

    hashesReady = true;
}
