#include "Stats.hpp"

#ifdef COLLECT_STATS

#include <cstdio>
#include <cinttypes>

#ifdef HASH_TABLE
#include "HashTable.hpp"
#endif

std::atomic<int> statsCaptures = 0;
std::atomic<int> statsEPs = 0;
std::atomic<int> statsCastles = 0;
std::atomic<int> statsCheckmates = 0;
std::atomic<int> statsHashProbes = 0;
std::atomic<int> statsHashHits = 0;
std::atomic<int> statsHashWriteTries = 0;
std::atomic<int> statsHashWrites = 0;

void resetStats()
{
    statsCaptures = 0;
    statsEPs = 0;
    statsCastles = 0;
    statsCheckmates = 0;
    statsHashProbes = 0;
    statsHashHits = 0;
    statsHashWriteTries = 0;
    statsHashWrites = 0;
}

void printStats(uint64_t count)
{
    int sCaps = statsCaptures;
    int sEPs = statsEPs;
    int sCsls = statsCastles;
    int sChks = statsCheckmates;
    int sHPrs = statsHashProbes;
    int sHHts = statsHashHits;
    int sHWts = statsHashWriteTries;
    int sHWrs = statsHashWrites;
    printf("Node count = %" PRIu64 " Captures = %d EPs = %d Castles = %d Checkmates = %d"
#ifdef HASH_TABLE
        " Hash probes = %d Hash hits = %d Hash write tries = %d Hash writes = %d"
#endif
        "\n",
        count, sCaps, sEPs, sCsls, sChks
#ifdef HASH_TABLE
        , sHPrs, sHHts, sHWts, sHWrs
#endif
    );
#ifdef HASH_TABLE
    float hashTableHitRate = (float)statsHashHits / (float)statsHashProbes;
    float hashCollisionRate = (float)(statsHashWriteTries - statsHashWrites) / (float)(statsHashWriteTries);
    printf("Hash table size %dk elements, read hit rate %f %%, write collision rate %f %%\n",
        1 << (HashTableSize - 10), hashTableHitRate * 100.0f, hashCollisionRate * 100.0f);
#endif
}

#endif
