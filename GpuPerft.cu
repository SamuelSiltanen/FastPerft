// Copyright 2022 Samuel Siltanen
// GpuPerft.cu

#include "GpuPerft.hpp"
#include "Config.hpp"

#if GPU_PERFT

#include "MoveGeneration.hpp"
#include "Make.hpp"

#include <cub/device/device_radix_sort.cuh>
#include <cub/device/device_scan.cuh>
#include <cub/device/device_select.cuh>
#include <cub/iterator/counting_input_iterator.cuh>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#define CUDA_CHECK(call)                                                                        \
    do                                                                                          \
    {                                                                                           \
        cudaError_t error = (call);                                                             \
        if (error != cudaSuccess)                                                               \
        {                                                                                       \
            fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(error), __FILE__, __LINE__); \
            exit(EXIT_FAILURE);                                                                 \
        }                                                                                       \
    } while (0)

// The GPU search is breadth first: for each level, one kernel counts the moves of each position, a prefix sum
// gives the offsets of their moves, one kernel generates the moves, and one kernel makes them, one thread per move.
// The children are searched the same way, until the last LeafDepth plies, which each thread searches depth first.
// If the children don't fit in the buffer of the next level, the positions are searched in chunks, depth first.
//
// Before a level is searched, its duplicate positions are merged, and the positions found in the hash table are
// not searched again. Each position gets the node count of its subtree, which is stored in the hash table. The
// node count of a position is the sum of the node counts of its children, through the mapping from the children
// to the merged positions.
namespace
{

constexpr int MaxGpuDepth = 5; // Plies searched on the GPU, the CPU expands the tree to depth - MaxGpuDepth
constexpr int LeafDepth = 2; // Plies that each GPU thread searches depth first at the end, 1 to 3
constexpr int NumLevels = MaxGpuDepth - LeafDepth + 1; // Levels of positions stored on the GPU
constexpr int BatchSize = 64 * 1024; // Positions per batch from the CPU
constexpr uint32_t LevelCapacity = 4 * 1024 * 1024; // Positions per level
constexpr int BlockSize = 256;
constexpr int LeafMinBlocks = 3; // Minimum blocks per SM for the leaf kernel, limits its registers
constexpr int MaxMoves = 256; // At most 218 legal moves in any position

constexpr bool UseHashTable = true;
constexpr int HashTableSizeExp = 25; // Number of buckets as an exponent of 2. A bucket has 4 entries of 16 bytes, so 2^25 buckets take 2 GB.
constexpr int HashBucketSize = 4;
constexpr int MinHashDepth = 2; // Depth-1 counts are faster to calculate than to look up
constexpr uint64_t HashCountMask = (1ULL << 56) - 1; // The count is in the low 56 bits of the data, and the depth in the high 8 bits
constexpr uint64_t UnknownCount = ~0ULL;
constexpr int MinMergeDepth = LeafDepth + 1; // Merging the leaf level costs more than it saves, and the hash table finds most of its duplicates
constexpr bool SortLeavesByMoves = true; // Sort the leaf positions by their number of moves, so that the threads of a warp have similar work

static_assert(LeafDepth >= 1 && LeafDepth <= 3 && LeafDepth <= MaxGpuDepth, "Unsupported leaf depth");
static_assert(BatchSize <= LevelCapacity, "The batch must fit in a level");

FP_HOST_DEVICE constexpr Color opponent(Color c) { return (c == White) ? Black : White; }

template<Color C>
__device__ __forceinline__ uint64_t countMoves(const Position& pos)
{
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

    if (checkers) return countEvasions<C>(pos, occ, pArea, checkers, pins);

    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;
    return countP<C>(pos, occ, pins) + countN<C>(pos, occ, anyPins) + countSliders<C>(pos, occ, pins) +
        countK<C>(pos, occ, pArea) + countCastling<C>(pos, occ, pArea);
}

template<Color C>
__device__ __forceinline__ Move* generateMoves(const Position& pos, Move* moves)
{
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

    if (checkers) return generateEvasions<C>(pos, moves, occ, pArea, checkers, pins);

    moves = generateP<C>(pos, moves, occ, pins);
    moves = generateN<C>(pos, moves, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
    moves = generateSliders<C>(pos, moves, occ, pins);
    moves = generateK<C>(pos, moves, occ, pArea);
    return generateCastling<C>(pos, moves, occ, pArea);
}

// Depth-first perft of one position in one thread. The depth is a template parameter, so that the recursion
// is unrolled at compile time. The last ply is counted in bulk.
template<Color C, int Depth>
__device__ uint64_t perftThread(const Position& pos)
{
    if constexpr (Depth == 1)
    {
        return countMoves<C>(pos);
    }
    else
    {
        Move moves[MaxMoves];
        Move* end = generateMoves<C>(pos, moves);

        uint64_t count = 0;
        for (const Move* move = moves; move < end; ++move)
        {
            count += perftThread<opponent(C), Depth - 1>(make<C>(pos, *move));
        }
        return count;
    }
}

// The finalizer of MurmurHash3, a bijective mix of all bits
__device__ __forceinline__ uint64_t fmix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// Hashes the position by mixing in its fields one by one. Each step is bijective, so positions that differ in only
// one field never collide, and otherwise the mixing makes collisions as unlikely as for random keys. A weaker mix
// of the fields caused a wrong count for perft 10 from the initial position.
__device__ __forceinline__ uint64_t hashPosition(const Position& pos, uint64_t seed)
{
    uint64_t h = seed;
    h = fmix64(h ^ pos.p);
    h = fmix64(h ^ pos.n);
    h = fmix64(h ^ pos.bq);
    h = fmix64(h ^ pos.rq);
    h = fmix64(h ^ pos.k);
    h = fmix64(h ^ pos.w);
    h = fmix64(h ^ pos.state);
    return h;
}

// Two independent hashes of a position, for the hash table key and the bucket index
__device__ __forceinline__ uint64_t hashKey(const Position& pos) { return hashPosition(pos, 0x243f6a8885a308d3ULL); }
__device__ __forceinline__ uint64_t hashIndex(const Position& pos) { return hashPosition(pos, 0x13198a2e03707344ULL); }

__device__ __forceinline__ bool samePosition(const Position& a, const Position& b)
{
    return a.p == b.p && a.n == b.n && a.bq == b.bq && a.rq == b.rq && a.k == b.k && a.w == b.w && a.state == b.state;
}

// Searches the positions given by the indices, or all positions if there are no indices. The number of indices
// is read on the GPU, so that the CPU doesn't need to wait for it.
template<Color C, int Depth>
__global__ void __launch_bounds__(BlockSize, LeafMinBlocks) leafKernel(const Position* positions, const uint32_t* indices,
    const uint32_t* numIndices, uint32_t numPositions, uint64_t* nodeCounts)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    uint32_t count = indices ? *numIndices : numPositions;
    if (index < count)
    {
        uint32_t position = indices ? indices[index] : index;
        nodeCounts[position] = perftThread<C, Depth>(positions[position]);
    }
}

// Selects the positions that weren't found in the hash table
struct IsUnknown
{
    const uint64_t* nodeCounts;
    __device__ __forceinline__ bool operator()(uint32_t index) const { return nodeCounts[index] == UnknownCount; }
};

// Counts the moves of the positions that need to be searched
template<Color C>
__global__ void __launch_bounds__(BlockSize) countKernel(const Position* positions, const uint64_t* nodeCounts, uint32_t numPositions, uint32_t* counts)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        counts[index] = (nodeCounts[index] == UnknownCount) ? static_cast<uint32_t>(countMoves<C>(positions[index])) : 0;
    }
}

// Generates the moves of the positions [begin, end) that need to be searched to the offsets given by the prefix sum
// of the move counts, relative to the first one. Also stores the index of the position of each move.
template<Color C>
__global__ void __launch_bounds__(BlockSize) generateKernel(const Position* positions, const uint64_t* nodeCounts, uint32_t begin, uint32_t end,
    const uint32_t* offsets, Move* moves, uint32_t* parents)
{
    loadMoveTables();

    uint32_t index = begin + blockIdx.x * blockDim.x + threadIdx.x;
    if (index < end && nodeCounts[index] == UnknownCount)
    {
        uint32_t first = offsets[index] - offsets[begin];
        Move* last = generateMoves<C>(positions[index], moves + first);
        uint32_t numMoves = static_cast<uint32_t>(last - (moves + first));
        for (uint32_t i = 0; i < numMoves; ++i)
        {
            parents[first + i] = index;
        }
    }
}

template<Color C>
__global__ void __launch_bounds__(BlockSize) makeKernel(const Position* positions, const Move* moves, const uint32_t* parents,
    uint32_t numMoves, Position* children)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numMoves)
    {
        children[index] = make<C>(positions[parents[index]], moves[index]);
    }
}

// Sums the node counts of the children of the positions [begin, end) that were searched. The children of a
// position are consecutive, and childRuns maps them to the merged children.
__global__ void aggregateKernel(uint32_t begin, uint32_t end, const uint32_t* offsets, const uint64_t* childCounts, const uint32_t* childRuns,
    uint64_t* nodeCounts)
{
    uint32_t index = begin + blockIdx.x * blockDim.x + threadIdx.x;
    if (index < end && nodeCounts[index] == UnknownCount)
    {
        uint32_t first = offsets[index] - offsets[begin];
        uint32_t last = offsets[index + 1] - offsets[begin];
        uint64_t count = 0;
        for (uint32_t i = first; i < last; ++i)
        {
            count += childCounts[childRuns[i]];
        }
        nodeCounts[index] = count;
    }
}

__global__ void keyKernel(const Position* positions, uint32_t numPositions, uint32_t* keys, uint32_t* indices)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        keys[index] = static_cast<uint32_t>(hashKey(positions[index]) >> 32);
        indices[index] = index;
    }
}

// Marks the first position of each run of equal positions in the order sorted by the keys. Different positions
// may have the same key, so equal positions may end up in different runs, which only means that they are searched
// more than once.
__global__ void headKernel(const Position* positions, const uint32_t* sortedKeys, const uint32_t* sortedIndices, uint32_t numPositions,
    uint32_t* heads)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        heads[index] = (index == 0 || sortedKeys[index] != sortedKeys[index - 1] ||
            !samePosition(positions[sortedIndices[index]], positions[sortedIndices[index - 1]])) ? 1 : 0;
    }
}

// Stores the first position of each run, and maps all positions to their run. runs is the inclusive prefix sum
// of the heads, so it gives the index of the run, plus one.
__global__ void mergeKernel(const Position* positions, const uint32_t* sortedIndices, const uint32_t* heads, const uint32_t* runs,
    uint32_t numPositions, Position* runPositions, uint32_t* runOf)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        uint32_t source = sortedIndices[index];
        uint32_t run = runs[index] - 1;
        if (heads[index])
        {
            runPositions[run] = positions[source];
        }
        runOf[source] = run;
    }
}

// The key is stored XORed with the data. If two threads write the same slot at the same time, the slot may get the
// key from one and the data from the other, and then the key doesn't match the data, like in the CPU hash table.
struct alignas(16) GpuHashSlot
{
    uint64_t keyXorData;
    uint64_t data; // Depth in the high 8 bits, node count in the low 56 bits. Zero for an empty slot.
};

__device__ __forceinline__ GpuHashSlot loadSlot(const GpuHashSlot* slot)
{
    ulonglong2 value = *reinterpret_cast<const ulonglong2*>(slot);
    return { value.x, value.y };
}

__device__ __forceinline__ void storeSlot(GpuHashSlot* slot, uint64_t key, uint64_t data)
{
    *reinterpret_cast<ulonglong2*>(slot) = make_ulonglong2(key ^ data, data);
}

__global__ void probeKernel(const Position* positions, uint32_t numPositions, uint64_t depth, const GpuHashSlot* table, uint64_t bucketMask,
    uint64_t* nodeCounts)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        const Position& pos = positions[index];
        uint64_t key = hashKey(pos);
        const GpuHashSlot* bucket = table + (hashIndex(pos) & bucketMask) * HashBucketSize;
        uint64_t count = UnknownCount;
        for (int i = 0; i < HashBucketSize; ++i)
        {
            GpuHashSlot slot = loadSlot(bucket + i);
            if ((slot.keyXorData ^ slot.data) == key && (slot.data >> 56) == depth)
            {
                count = slot.data & HashCountMask;
                break;
            }
        }
        nodeCounts[index] = count;
    }
}

// Stores the node counts of the positions given by the indices, or of all positions if there are no indices.
// Replaces the same position, an empty slot, or the slot with the smallest depth.
__global__ void insertKernel(const Position* positions, const uint32_t* indices, const uint32_t* numIndices, uint32_t numPositions,
    uint64_t depth, const uint64_t* nodeCounts, GpuHashSlot* table, uint64_t bucketMask)
{
    uint32_t thread = blockIdx.x * blockDim.x + threadIdx.x;
    uint32_t count = indices ? *numIndices : numPositions;
    if (thread >= count) return;

    uint32_t index = indices ? indices[thread] : thread;
    if (nodeCounts[index] <= HashCountMask)
    {
        const Position& pos = positions[index];
        uint64_t key = hashKey(pos);
        GpuHashSlot* bucket = table + (hashIndex(pos) & bucketMask) * HashBucketSize;
        int victim = 0;
        uint64_t victimDepth = ~0ULL;
        for (int i = 0; i < HashBucketSize; ++i)
        {
            GpuHashSlot slot = loadSlot(bucket + i);
            uint64_t slotDepth = slot.data >> 56;
            if (slot.data == 0 || ((slot.keyXorData ^ slot.data) == key && slotDepth == depth))
            {
                victim = i;
                break;
            }
            if (slotDepth < victimDepth)
            {
                victimDepth = slotDepth;
                victim = i;
            }
        }
        storeSlot(bucket + victim, key, (depth << 56) | nodeCounts[index]);
    }
}

// Counts the moves of the positions given by the indices
template<Color C>
__global__ void __launch_bounds__(BlockSize) countSelectedKernel(const Position* positions, const uint32_t* indices, uint32_t numIndices,
    uint32_t* counts)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numIndices)
    {
        counts[index] = static_cast<uint32_t>(countMoves<C>(positions[indices[index]]));
    }
}

__global__ void identityKernel(uint32_t* values, uint32_t count)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count)
    {
        values[index] = index;
    }
}

__global__ void fillKernel(uint64_t* values, uint32_t count, uint64_t value)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count)
    {
        values[index] = value;
    }
}

// Adds the node counts of the positions of a batch, through the mapping to the merged positions, to the result
__global__ void sumKernel(const uint64_t* nodeCounts, const uint32_t* runOf, uint32_t numPositions, unsigned long long* result)
{
    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;

    unsigned long long count = (index < numPositions) ? nodeCounts[runOf[index]] : 0;
    for (int offset = 16; offset > 0; offset /= 2)
    {
        count += __shfl_down_sync(0xffffffff, count, offset);
    }
    if ((threadIdx.x & 31) == 0)
    {
        atomicAdd(result, count);
    }
}

// Finds the end of the chunk of positions starting at begin, so that their moves fit in the capacity.
// offsets has numPositions + 1 entries, the last one is the total number of moves.
__global__ void findChunkEndKernel(const uint32_t* offsets, uint32_t numPositions, uint32_t begin, uint32_t capacity, uint32_t* chunk)
{
    uint32_t limit = offsets[begin] + capacity;
    uint32_t low = begin + 1, high = numPositions; // The chunk ends somewhere in [begin + 1, numPositions]
    while (low < high)
    {
        uint32_t middle = high - (high - low) / 2;
        if (offsets[middle] <= limit) low = middle;
        else high = middle - 1;
    }
    chunk[0] = low;
    chunk[1] = offsets[low] - offsets[begin];
}

uint32_t numBlocks(uint32_t numThreads)
{
    return (numThreads + BlockSize - 1) / BlockSize;
}

struct Level
{
    Position* positions; // Merged positions of the level
    uint64_t* nodeCounts; // Node count of the subtree of each merged position
    uint32_t* runOf; // The merged position of each position before merging
    uint32_t* counts; // Number of moves of each position, plus one zero at the end for the total
    uint32_t* offsets; // Exclusive prefix sum of counts
    Move* moves; // Moves of the positions of the current chunk
    uint32_t* parents; // Index of the position of each move
};

// Buffers for merging the duplicate positions of a level
struct Merge
{
    Position* positions; // Swapped with the positions of the merged level
    uint32_t* keys[2];
    uint32_t* indices[2];
    uint32_t* heads;
    uint32_t* runs;
    void* sortStorage;
    size_t sortStorageBytes;
};

Level levels[NumLevels];
Merge merge;
GpuHashSlot* gpuHashTable;
uint64_t bucketMask;
Position* hostBatch; // Pinned, so that the copy to the GPU is fast
uint32_t* deviceChunk; // End and number of moves of a chunk
uint32_t* hostChunk;
uint32_t* hostNumUnique;
unsigned long long* deviceResult;
void* scanStorage;
size_t scanStorageBytes;
void* selectStorage;
size_t selectStorageBytes;
uint32_t* deviceNumSelected;
int gpuDepth = 0;
int batchSize = 0;
Color batchColor = White;

template<Color C>
void launchLeafKernel(int depth, const Position* positions, const uint32_t* indices, const uint32_t* numIndices, uint32_t numPositions,
    uint64_t* nodeCounts)
{
    switch (depth)
    {
    case 1: leafKernel<C, 1><<<numBlocks(numPositions), BlockSize>>>(positions, indices, numIndices, numPositions, nodeCounts); break;
    case 2: leafKernel<C, 2><<<numBlocks(numPositions), BlockSize>>>(positions, indices, numIndices, numPositions, nodeCounts); break;
    case 3: leafKernel<C, 3><<<numBlocks(numPositions), BlockSize>>>(positions, indices, numIndices, numPositions, nodeCounts); break;
    default: fprintf(stderr, "Unsupported leaf depth %d\n", depth); exit(EXIT_FAILURE);
    }
    CUDA_CHECK(cudaGetLastError());
}

// Merges the duplicate positions of a level, maps the positions to the merged ones, and returns the number of merged positions
uint32_t deduplicate(Level& level, uint32_t numPositions)
{
    if (numPositions == 1)
    {
        CUDA_CHECK(cudaMemsetAsync(level.runOf, 0, sizeof(uint32_t)));
        return 1;
    }

    keyKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, numPositions, merge.keys[0], merge.indices[0]);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cub::DeviceRadixSort::SortPairs(merge.sortStorage, merge.sortStorageBytes, merge.keys[0], merge.keys[1],
        merge.indices[0], merge.indices[1], numPositions));
    headKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, merge.keys[1], merge.indices[1], numPositions, merge.heads);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cub::DeviceScan::InclusiveSum(scanStorage, scanStorageBytes, merge.heads, merge.runs, numPositions));
    mergeKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, merge.indices[1], merge.heads, merge.runs, numPositions,
        merge.positions, level.runOf);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpy(hostNumUnique, merge.runs + numPositions - 1, sizeof(uint32_t), cudaMemcpyDeviceToHost));

    std::swap(level.positions, merge.positions);
    return *hostNumUnique;
}

// Searches the positions of a level to the given depth. Afterwards, the node counts of the merged positions and the
// mapping from the positions to the merged positions are in the level.
template<Color C>
void searchLevel(int levelIndex, uint32_t numPositions, int depth)
{
    if (numPositions == 0) return;

    Level& level = levels[levelIndex];
    if (depth >= MinMergeDepth)
    {
        numPositions = deduplicate(level, numPositions);
    }
    else
    {
        identityKernel<<<numBlocks(numPositions), BlockSize>>>(level.runOf, numPositions);
        CUDA_CHECK(cudaGetLastError());
    }

    bool useHashTable = UseHashTable && depth >= MinHashDepth;
    if (useHashTable)
    {
        probeKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, numPositions, depth, gpuHashTable, bucketMask, level.nodeCounts);
    }
    else
    {
        fillKernel<<<numBlocks(numPositions), BlockSize>>>(level.nodeCounts, numPositions, UnknownCount);
    }
    CUDA_CHECK(cudaGetLastError());

    if (depth <= LeafDepth)
    {
        if (useHashTable)
        {
            // Search and store only the positions that weren't found, so that the warps don't have idle threads
            CUDA_CHECK(cub::DeviceSelect::If(selectStorage, selectStorageBytes, cub::CountingInputIterator<uint32_t>(0), merge.keys[0],
                deviceNumSelected, numPositions, IsUnknown{ level.nodeCounts }));
            const uint32_t* indices = merge.keys[0];
            if (SortLeavesByMoves && depth >= 2)
            {
                CUDA_CHECK(cudaMemcpy(hostNumUnique, deviceNumSelected, sizeof(uint32_t), cudaMemcpyDeviceToHost));
                uint32_t numSelected = *hostNumUnique;
                if (numSelected > 1)
                {
                    countSelectedKernel<C><<<numBlocks(numSelected), BlockSize>>>(level.positions, merge.keys[0], numSelected, merge.heads);
                    CUDA_CHECK(cudaGetLastError());
                    CUDA_CHECK(cub::DeviceRadixSort::SortPairs(merge.sortStorage, merge.sortStorageBytes, merge.heads, merge.runs,
                        merge.keys[0], merge.indices[0], numSelected, 0, 8)); // At most 218 moves, so 8 bits are enough
                    indices = merge.indices[0];
                }
            }
            launchLeafKernel<C>(depth, level.positions, indices, deviceNumSelected, numPositions, level.nodeCounts);
            insertKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, indices, deviceNumSelected, numPositions, depth,
                level.nodeCounts, gpuHashTable, bucketMask);
            CUDA_CHECK(cudaGetLastError());
        }
        else
        {
            launchLeafKernel<C>(depth, level.positions, nullptr, nullptr, numPositions, level.nodeCounts);
        }
        return;
    }
    else
    {
        countKernel<C><<<numBlocks(numPositions), BlockSize>>>(level.positions, level.nodeCounts, numPositions, level.counts);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaMemsetAsync(level.counts + numPositions, 0, sizeof(uint32_t)));
        CUDA_CHECK(cub::DeviceScan::ExclusiveSum(scanStorage, scanStorageBytes, level.counts, level.offsets, numPositions + 1));

        Level& next = levels[levelIndex + 1];
        uint32_t begin = 0;
        while (begin < numPositions)
        {
            findChunkEndKernel<<<1, 1>>>(level.offsets, numPositions, begin, LevelCapacity, deviceChunk);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaMemcpy(hostChunk, deviceChunk, 2 * sizeof(uint32_t), cudaMemcpyDeviceToHost));
            uint32_t end = hostChunk[0];
            uint32_t numMoves = hostChunk[1];

            if (numMoves > 0)
            {
                generateKernel<C><<<numBlocks(end - begin), BlockSize>>>(level.positions, level.nodeCounts, begin, end, level.offsets,
                    level.moves, level.parents);
                CUDA_CHECK(cudaGetLastError());
                makeKernel<C><<<numBlocks(numMoves), BlockSize>>>(level.positions, level.moves, level.parents, numMoves, next.positions);
                CUDA_CHECK(cudaGetLastError());

                searchLevel<opponent(C)>(levelIndex + 1, numMoves, depth - 1);
            }

            aggregateKernel<<<numBlocks(end - begin), BlockSize>>>(begin, end, level.offsets, next.nodeCounts, next.runOf, level.nodeCounts);
            CUDA_CHECK(cudaGetLastError());

            begin = end;
        }
    }

    if (useHashTable)
    {
        insertKernel<<<numBlocks(numPositions), BlockSize>>>(level.positions, nullptr, nullptr, numPositions, depth, level.nodeCounts,
            gpuHashTable, bucketMask);
        CUDA_CHECK(cudaGetLastError());
    }
}

void searchBatch()
{
    if (batchSize == 0) return;

    Level& level = levels[0];
    CUDA_CHECK(cudaMemcpy(level.positions, hostBatch, batchSize * sizeof(Position), cudaMemcpyHostToDevice));
    if (batchColor == White)
    {
        searchLevel<White>(0, batchSize, gpuDepth);
    }
    else
    {
        searchLevel<Black>(0, batchSize, gpuDepth);
    }

    sumKernel<<<numBlocks(batchSize), BlockSize>>>(level.nodeCounts, level.runOf, batchSize, deviceResult);
    CUDA_CHECK(cudaGetLastError());
    batchSize = 0;
}

void addPosition(const Position& pos)
{
    if (batchSize == BatchSize)
    {
        searchBatch();
    }
    hostBatch[batchSize++] = pos;
}

// Collects the positions at the given depth into batches
template<Color C>
void expand(const Position& pos, int depth)
{
    if (depth == 0)
    {
        addPosition(pos);
        return;
    }

    Move moves[MaxMoves];
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

    Move* end = moves;
    if (checkers)
    {
        end = generateEvasions<C>(pos, end, occ, pArea, checkers, pins);
    }
    else
    {
        end = generateP<C>(pos, end, occ, pins);
        end = generateN<C>(pos, end, occ, pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE);
        end = generateSliders<C>(pos, end, occ, pins);
        end = generateK<C>(pos, end, occ, pArea);
        end = generateCastling<C>(pos, end, occ, pArea);
    }

    for (const Move* move = moves; move < end; ++move)
    {
        expand<opponent(C)>(make<C>(pos, *move), depth - 1);
    }
}

} // namespace

bool initGpuPerft()
{
    // Load all kernels now instead of at their first launch, so that loading them isn't included in the search time
    _putenv_s("CUDA_MODULE_LOADING", "EAGER");

    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
    {
        fprintf(stderr, "No CUDA device found\n");
        return false;
    }

    CUDA_CHECK(cudaMemcpyToSymbol(d_moveTables, &moveTables, sizeof(MoveTables)));

    // All levels have the same capacity, because merging swaps their positions with the merge buffer
    for (int i = 0; i < NumLevels; ++i)
    {
        Level& level = levels[i];
        CUDA_CHECK(cudaMalloc(&level.positions, LevelCapacity * sizeof(Position)));
        CUDA_CHECK(cudaMalloc(&level.nodeCounts, LevelCapacity * sizeof(uint64_t)));
        CUDA_CHECK(cudaMalloc(&level.runOf, LevelCapacity * sizeof(uint32_t)));
        if (i < NumLevels - 1)
        {
            CUDA_CHECK(cudaMalloc(&level.counts, (LevelCapacity + 1) * sizeof(uint32_t)));
            CUDA_CHECK(cudaMalloc(&level.offsets, (LevelCapacity + 1) * sizeof(uint32_t)));
            CUDA_CHECK(cudaMalloc(&level.moves, LevelCapacity * sizeof(Move)));
            CUDA_CHECK(cudaMalloc(&level.parents, LevelCapacity * sizeof(uint32_t)));
        }
        else
        {
            level.counts = level.offsets = level.parents = nullptr;
            level.moves = nullptr;
        }
    }

    CUDA_CHECK(cudaMalloc(&merge.positions, LevelCapacity * sizeof(Position)));
    for (int i = 0; i < 2; ++i)
    {
        CUDA_CHECK(cudaMalloc(&merge.keys[i], LevelCapacity * sizeof(uint32_t)));
        CUDA_CHECK(cudaMalloc(&merge.indices[i], LevelCapacity * sizeof(uint32_t)));
    }
    CUDA_CHECK(cudaMalloc(&merge.heads, LevelCapacity * sizeof(uint32_t)));
    CUDA_CHECK(cudaMalloc(&merge.runs, LevelCapacity * sizeof(uint32_t)));
    CUDA_CHECK(cub::DeviceRadixSort::SortPairs(nullptr, merge.sortStorageBytes, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr),
        static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr), LevelCapacity));
    CUDA_CHECK(cudaMalloc(&merge.sortStorage, merge.sortStorageBytes));

    size_t inclusiveScanBytes = 0;
    CUDA_CHECK(cub::DeviceScan::ExclusiveSum(nullptr, scanStorageBytes, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr), LevelCapacity + 1));
    CUDA_CHECK(cub::DeviceScan::InclusiveSum(nullptr, inclusiveScanBytes, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr), LevelCapacity));
    scanStorageBytes = std::max(scanStorageBytes, inclusiveScanBytes);
    CUDA_CHECK(cudaMalloc(&scanStorage, scanStorageBytes));

    CUDA_CHECK(cub::DeviceSelect::If(nullptr, selectStorageBytes, cub::CountingInputIterator<uint32_t>(0), static_cast<uint32_t*>(nullptr),
        static_cast<uint32_t*>(nullptr), LevelCapacity, IsUnknown{ nullptr }));
    CUDA_CHECK(cudaMalloc(&selectStorage, selectStorageBytes));
    CUDA_CHECK(cudaMalloc(&deviceNumSelected, sizeof(uint32_t)));

    if (UseHashTable)
    {
        size_t numBuckets = 1ULL << HashTableSizeExp;
        bucketMask = numBuckets - 1;
        CUDA_CHECK(cudaMalloc(&gpuHashTable, numBuckets * HashBucketSize * sizeof(GpuHashSlot)));
        CUDA_CHECK(cudaMemset(gpuHashTable, 0, numBuckets * HashBucketSize * sizeof(GpuHashSlot)));
    }

    CUDA_CHECK(cudaMallocHost(&hostBatch, BatchSize * sizeof(Position)));
    CUDA_CHECK(cudaMalloc(&deviceChunk, 2 * sizeof(uint32_t)));
    CUDA_CHECK(cudaMallocHost(&hostChunk, 2 * sizeof(uint32_t)));
    CUDA_CHECK(cudaMallocHost(&hostNumUnique, sizeof(uint32_t)));
    CUDA_CHECK(cudaMalloc(&deviceResult, sizeof(unsigned long long)));

    // The first launches allocate the local memory of the kernels, which isn't included in the search time
    runGpuPerft(Position(), MaxGpuDepth);
    if (UseHashTable)
    {
        CUDA_CHECK(cudaMemset(gpuHashTable, 0, (bucketMask + 1) * HashBucketSize * sizeof(GpuHashSlot)));
    }
    CUDA_CHECK(cudaDeviceSynchronize()); // cudaMemset is asynchronous, so wait for it here instead of in the first search

    return true;
}

uint64_t runGpuPerft(const Position& pos, int depth)
{
    if (depth <= 0) return 1;

    gpuDepth = (depth < MaxGpuDepth) ? depth : MaxGpuDepth;
    batchSize = 0;
    int cpuDepth = depth - gpuDepth;
    bool white = (pos.state & TurnWhite) != 0;
    batchColor = ((cpuDepth % 2 == 0) == white) ? White : Black; // All positions at the same depth have the same side to move

    CUDA_CHECK(cudaMemset(deviceResult, 0, sizeof(unsigned long long)));

    if (white)
    {
        expand<White>(pos, cpuDepth);
    }
    else
    {
        expand<Black>(pos, cpuDepth);
    }
    searchBatch();

    unsigned long long result = 0;
    CUDA_CHECK(cudaMemcpy(&result, deviceResult, sizeof(result), cudaMemcpyDeviceToHost));
    return result;
}

void releaseGpuPerft()
{
    for (Level& level : levels)
    {
        CUDA_CHECK(cudaFree(level.positions));
        CUDA_CHECK(cudaFree(level.nodeCounts));
        CUDA_CHECK(cudaFree(level.runOf));
        CUDA_CHECK(cudaFree(level.counts));
        CUDA_CHECK(cudaFree(level.offsets));
        CUDA_CHECK(cudaFree(level.moves));
        CUDA_CHECK(cudaFree(level.parents));
    }
    CUDA_CHECK(cudaFree(merge.positions));
    for (int i = 0; i < 2; ++i)
    {
        CUDA_CHECK(cudaFree(merge.keys[i]));
        CUDA_CHECK(cudaFree(merge.indices[i]));
    }
    CUDA_CHECK(cudaFree(merge.heads));
    CUDA_CHECK(cudaFree(merge.runs));
    CUDA_CHECK(cudaFree(merge.sortStorage));
    CUDA_CHECK(cudaFree(scanStorage));
    CUDA_CHECK(cudaFree(selectStorage));
    CUDA_CHECK(cudaFree(deviceNumSelected));
    if (UseHashTable)
    {
        CUDA_CHECK(cudaFree(gpuHashTable));
    }
    CUDA_CHECK(cudaFreeHost(hostBatch));
    CUDA_CHECK(cudaFree(deviceChunk));
    CUDA_CHECK(cudaFreeHost(hostChunk));
    CUDA_CHECK(cudaFreeHost(hostNumUnique));
    CUDA_CHECK(cudaFree(deviceResult));
}

#endif
