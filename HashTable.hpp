#pragma once

#include "ChessTypes.hpp"

#include <cassert>
#ifdef MULTITHREADED
#include <mutex>
#endif

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

constexpr uint64_t InvalidHashTableEntry = 0xffffffffffffffffULL;
constexpr int MinHashDepth = 2;
constexpr uint32_t HashTableSize = 26;// 18;

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

    static uint64_t calcHash(const Position& pos);
    static const Hashes& hashSquare(unsigned long sq) { assert(hashesReady); return hashKeys[sq]; }
    static uint64_t hashTurn() { assert(hashesReady); return hashKeys[0].state; }
    static uint64_t hashCastling(uint64_t oldState, uint64_t newState);
    static uint64_t hashEP(uint64_t oldState, uint64_t newState);
private:
    uint32_t mapToIndex(uint64_t hash);
    int64_t replacementPolicy(const HashEntry& currentEntry, const HashEntry& candidateEntry);

    void initHashes();

    HashEntry* m_hashTable;
    uint32_t m_size;
    uint32_t m_sizeExp;

#ifdef MULTITHREADED
    std::mutex m_lock;
#endif
    
    static Hashes hashKeys[64];
    static bool hashesReady;
};

extern HashTable* hashTable;
