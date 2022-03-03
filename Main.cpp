#include <cstdio>
#include <cstdint>
#include <cinttypes>
#include <thread>
#include <chrono>
#include <cassert>

#include "Config.hpp"
#include "ChessTypes.hpp"
#include "MoveGeneration.hpp"
#include "Make.hpp"
#include "Perft.hpp"
#include "TestPositions.hpp"

#ifdef COLLECT_STATS
#include "Stats.hpp"
#endif

#ifdef HASH_TABLE
#include "HashTable.hpp"
#endif

#ifdef HASH_TABLE
HashTable* hashTable = nullptr;
#endif

void testPerft(const Position& pos);

int main(int argc, char** argv)
{       
    Position pos = Position1;

#ifdef HASH_TABLE
    hashTable = new HashTable(HashTableSize);

    pos.hash = HashTable::calcHash(pos);
#endif

    fillMoveTables();
    
    testPerft(pos);

#ifdef HASH_TABLE
    delete hashTable;
#endif

    return 0;
}

void testPerft(const Position& pos)
{
#ifdef COLLECT_STATS
    resetStats();
#endif

#ifdef MULTITHREADED
    initMultiPerft();
#endif    

#ifdef MULTITHREADED
    uint64_t count = runMultiPerft(pos, 9/*7*/);
#else
    Move stack[1024];
    uint64_t count = perft(pos, 9/*7*/, stack);
#endif

#ifdef COLLECT_STATS
    printStats(count);
#else
    printf("Node count = %" PRIu64 "\n", count);
#endif

#ifdef MULTITHREADED
    releaseMultiPerft();
#endif
}
