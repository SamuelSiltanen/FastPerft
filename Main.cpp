// Copyright 2022 Samuel Siltanen
// Main.cpp

#include <cstdio>
#include <cinttypes>
#include <chrono>

#include "Config.hpp"
#include "ChessTypes.hpp"
#include "MoveGeneration.hpp"
#include "Make.hpp"
#include "Perft.hpp"
#include "TestPositions.hpp"
#include "FENParser.hpp"

#if COLLECT_STATS
#include "Stats.hpp"
#endif

#if HASH_TABLE
#include "HashTable.hpp"
#endif

#if HASH_TABLE
HashTable* hashTable = nullptr;
#endif

struct PerftParams
{
    int depth;
    int hashTableSize;
    int numberOfWorkers;
    bool collectStats;
    Position position;
};

PerftParams parseCommandLine(int argc, char** argv);
void printUsage();
void testPerft(const Position& pos, int depth);

int main(int argc, char** argv)
{       
    PerftParams params = parseCommandLine(argc, argv);

#if HASH_TABLE
    hashTable = new HashTable(params.hashTableSize);

    params.position.hash = HashTable::calcHash(params.position);
#endif

    fillMoveTables();
    
    testPerft(params.position, params.depth);

#if HASH_TABLE
    delete hashTable;
#endif

    return 0;
}

PerftParams parseCommandLine(int argc, char** argv)
{
    PerftParams params;
    params.depth = 1;
#if HASH_TABLE
    params.hashTableSize = DefaultHashTableSize;
#else
    params.hashTableSize = 0;
#endif
    params.numberOfWorkers = 8;
    params.collectStats = false;
    params.position = Position1;

    bool failure = false;

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-')
        {
            failure = true;
        }

        switch (argv[i][1])
        {
        case 'd':
            if (argc <= i + 1)
            {
                failure = true;
                break;
            }
            params.depth = atoi(argv[i + 1]);
            ++i;
            break;
        case 'h':
            if (argc <= i + 1)
            {
                failure = true;
                break;
            }
            params.hashTableSize = atoi(argv[i + 1]);
            ++i;
            break;
        case 'w':
            if (argc <= i + 1)
            {
                failure = true;
                break;
            }
            params.numberOfWorkers = atoi(argv[i + 1]);
            ++i;
            break;
        case 's':
            params.collectStats = true;
            break;
        case 'f':
            if (argc <= i + 1)
            {
                failure = true;
                break;
            }
            if (!parseFEN(argv[i + 1], params.position))
            {
                failure = true;
                break;
            }
            ++i;
            break;
        default:
            failure = true;
            break;
        };
    }

    if (failure)
    {
        printUsage();
        exit(EXIT_FAILURE);
    }

    return params;
}

void printUsage()
{
    printf("Usage:\n");
    printf("\tfastperft.exe <options>\n");
    printf("Supported options:\n");
    printf("\t-d <depth>      Depth at which to calculate leaf nodes. Default is 1.\n");
    printf("\t-h <size>       Hash table size as an exponent of 2.\n");
    printf("\t                E.g. -h 20 gives 2 ^ 20 = 1048576 hash table entries.\n");
    printf("\t                Default is 26. Negative value disables hash table.\n");
    printf("\t-w <workers>    Number of worker threads. Default is 8.\n");
    printf("\t-s              Print extra stats about moves and hash table.\n");
    printf("\t-f \"<FEN>\"    Position in FEN notation. Remember to use the quotes.\n");
}

void testPerft(const Position& pos, int depth)
{
#if COLLECT_STATS
    resetStats();
#endif

#if MULTITHREADED
    initMultiPerft();
#endif    

    auto start = std::chrono::high_resolution_clock::now();

#if MULTITHREADED
    uint64_t count = runMultiPerft(pos, depth);
#else
    Move stack[1024];
    uint64_t count = perft(pos, depth, stack);
#endif

    auto stop = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = stop - start;

    double nps = static_cast<double>(count) / elapsed.count() / 1e6;

#if COLLECT_STATS
    printStats(count);
#else
    printf("Node count = %" PRIu64 " Time %.3f s Speed: %.3f Mnps\n", count, elapsed.count(), nps);
#endif

#if MULTITHREADED
    releaseMultiPerft();
#endif
}
