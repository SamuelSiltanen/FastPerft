// Copyright 2022 Samuel Siltanen
// HashTable.hpp

#pragma once

#include "ChessTypes.hpp"
#include "Config.hpp"

#include <cassert>
#include <atomic>
#include <xmmintrin.h>

//#define HASH_DEBUG

#ifdef HASH_DEBUG
struct alignas(64) HashEntry
#else
struct alignas(16) HashEntry
#endif
{
    uint64_t hash;

    uint64_t depth_and_count;

#ifdef HASH_DEBUG
    uint64_t bqr;
    uint64_t rkn;
    uint64_t npb;
    uint64_t w;
    uint64_t state;
    uint64_t padding;
#endif

    HashEntry()
        : hash(0)        
        , depth_and_count(0)
#ifdef HASH_DEBUG
        , bqr(0)
        , rkn(0)
        , npb(0)
        , w(0)
        , state(0)
        , padding(0)
#endif
    {}
    HashEntry(const Position& pos, uint16_t depth, uint64_t count)
        : hash(pos.hash)
        , depth_and_count((static_cast<uint64_t>(depth) << 48) | count)
#ifdef HASH_DEBUG
        , bqr(pos.bq | pos.rq)
        , rkn((pos.rq & ~pos.bq) | pos.k | pos.n)
        , npb((pos.bq & ~pos.rq) | pos.p | pos.n)
        , w(pos.w)
        , state(pos.state)
        , padding(0)
#endif
    {}

    __forceinline uint64_t count() const { return depth_and_count & 0x0000ffffffffffffULL; }
    __forceinline uint16_t depth() const { return static_cast<uint16_t>(depth_and_count >> 48); }
    __forceinline bool empty() const { return depth_and_count == 0; }

#ifdef HASH_DEBUG
    bool posEqual(const Position& pos);
#endif
};

// A slot in the hash table. The key is stored XORed with the data, so that a slot that is read
// while another thread writes it (and thus has the key and data from different entries) does not
// match the probed hash. This makes the table thread safe without locks (lockless hashing).
#ifdef HASH_DEBUG
struct alignas(64) HashSlot
#else
struct alignas(16) HashSlot
#endif
{
    std::atomic<uint64_t> key;
    std::atomic<uint64_t> data;

#ifdef HASH_DEBUG
    // Not thread safe, for single-threaded debugging only
    uint64_t bqr;
    uint64_t rkn;
    uint64_t npb;
    uint64_t w;
    uint64_t state;
    uint64_t padding;
#endif
};

constexpr uint64_t InvalidHashTableEntry = 0xffffffffffffffffULL;
constexpr int MinHashDepth = 2;
constexpr uint32_t DefaultHashTableSize = 26;
constexpr int MinHashTableSize = 2;
constexpr int MaxHashTableSize = 30;

class HashTable
{
public:
    HashTable(uint32_t sizeExp);
    ~HashTable();

    HashTable(const HashTable&) = delete;
    HashTable(HashTable&&) = delete;
    HashTable& operator=(const HashTable&) = delete;
    HashTable& operator=(HashTable&&) = delete;

    bool insert(const HashEntry& entry);
    uint64_t find(const Position& pos, uint16_t depth);

    // Starts loading the cache line that find and insert use for the hash key
    void prefetch(uint64_t hash) const
    {
        uint32_t cacheLineStartIndex = static_cast<uint32_t>(hash & (m_size - 1)) & 0xfffffffc;
        _mm_prefetch(reinterpret_cast<const char*>(&m_hashTable[cacheLineStartIndex]), _MM_HINT_T0);
    }
    void clear();

    struct alignas(64) Hashes
    {
        uint64_t p;
        uint64_t n;
        uint64_t b;
        uint64_t r;
        uint64_t q;
        uint64_t k;
        uint64_t w;
        uint64_t state;
    };

    static void initHashKeys();
    static uint64_t calcHash(const Position& pos);

    // The keys of a square as an array indexed by piece type - 1 (pawn to king), followed by the white key
    static const uint64_t* squareKeys(unsigned long sq) { assert(hashesReady); return &hashKeys[sq].p; }

    // The keys for the turn, castling rights, and en passant square in the state
    static uint64_t stateHash(uint64_t state)
    {
        assert(hashesReady);
        uint64_t hash = stateKeys[state & 0x1f]; // Turn and castling rights
        if (state & EPValid)
        {
            hash ^= hashKeys[(state >> 5) & 63].state ^ hashKeys[11].state; // EP squares are 16-23 or 40-47, so they don't overlap 0-4 and 11
        }
        return hash;
    }
private:
    uint32_t mapToIndex(uint64_t hash);
    int64_t replacementPolicy(const HashEntry& currentEntry, const HashEntry& candidateEntry);

    HashEntry read(uint32_t index) const;
    void write(uint32_t index, const HashEntry& entry);

    HashSlot* m_hashTable;
    uint32_t m_size;
    uint32_t m_sizeExp;
    
    static Hashes hashKeys[64];
    static uint64_t stateKeys[32];
    static bool hashesReady;
};

extern HashTable* hashTable;
