// Copyright 2022 Samuel Siltanen
// GpuPerft.cu

#include "GpuPerft.hpp"
#include "Config.hpp"

#if GPU_PERFT

#include "MoveGeneration.hpp"
#include "Make.hpp"

#include <cub/device/device_scan.cuh>
#include <cstdio>
#include <cstdlib>

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
namespace
{

constexpr int MaxGpuDepth = 5; // Plies searched on the GPU, the CPU expands the tree to depth - MaxGpuDepth
constexpr int LeafDepth = 2; // Plies that each GPU thread searches depth first at the end, 1 to 3
constexpr int NumLevels = MaxGpuDepth - LeafDepth + 1; // Levels of positions stored on the GPU
constexpr int BatchSize = 64 * 1024; // Positions per batch from the CPU
constexpr uint32_t LevelCapacity = 4 * 1024 * 1024; // Positions per level below the batch
constexpr int BlockSize = 256;
constexpr int LeafMinBlocks = 3; // Minimum blocks per SM for the leaf kernel, limits its registers
constexpr int MaxMoves = 256; // At most 218 legal moves in any position

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

// Sums the counts of a warp and adds the sum to the result. A warp reduction instead of a block reduction,
// so that the warps that finish early don't wait for the others in the block.
__device__ __forceinline__ void addToResult(unsigned long long count, unsigned long long* result)
{
    for (int offset = 16; offset > 0; offset /= 2)
    {
        count += __shfl_down_sync(0xffffffff, count, offset);
    }
    if ((threadIdx.x & 31) == 0)
    {
        atomicAdd(result, count);
    }
}

template<Color C, int Depth>
__global__ void __launch_bounds__(BlockSize, LeafMinBlocks) leafKernel(const Position* positions, uint32_t numPositions, unsigned long long* result)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;

    unsigned long long count = 0;
    if (index < numPositions)
    {
        count = perftThread<C, Depth>(positions[index]);
    }

    addToResult(count, result);
}

template<Color C>
__global__ void __launch_bounds__(BlockSize) countKernel(const Position* positions, uint32_t numPositions, uint32_t* counts)
{
    loadMoveTables();

    uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < numPositions)
    {
        counts[index] = static_cast<uint32_t>(countMoves<C>(positions[index]));
    }
}

// Generates the moves of the positions [begin, end) to the offsets given by the prefix sum of the move counts,
// relative to the first one. Also stores the index of the position of each move.
template<Color C>
__global__ void __launch_bounds__(BlockSize) generateKernel(const Position* positions, uint32_t begin, uint32_t end, const uint32_t* offsets,
    Move* moves, uint32_t* parents)
{
    loadMoveTables();

    uint32_t index = begin + blockIdx.x * blockDim.x + threadIdx.x;
    if (index < end)
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
    Position* positions; // On the GPU
    uint32_t* counts; // Number of moves of each position, plus one zero at the end for the total
    uint32_t* offsets; // Exclusive prefix sum of counts
    Move* moves; // Moves of the positions of the current chunk
    uint32_t* parents; // Index of the position of each move
};

Level levels[NumLevels];
Position* hostBatch; // Pinned, so that the copy to the GPU is fast
uint32_t* deviceChunk; // End and number of moves of a chunk
uint32_t* hostChunk;
unsigned long long* deviceResult;
void* scanStorage;
size_t scanStorageBytes;
int gpuDepth = 0;
int batchSize = 0;
Color batchColor = White;

template<Color C>
void launchLeafKernel(int depth, const Position* positions, uint32_t numPositions)
{
    switch (depth)
    {
    case 1: leafKernel<C, 1><<<numBlocks(numPositions), BlockSize>>>(positions, numPositions, deviceResult); break;
    case 2: leafKernel<C, 2><<<numBlocks(numPositions), BlockSize>>>(positions, numPositions, deviceResult); break;
    case 3: leafKernel<C, 3><<<numBlocks(numPositions), BlockSize>>>(positions, numPositions, deviceResult); break;
    default: fprintf(stderr, "Unsupported leaf depth %d\n", depth); exit(EXIT_FAILURE);
    }
    CUDA_CHECK(cudaGetLastError());
}

// Searches the positions of a level to the given depth
template<Color C>
void searchLevel(int levelIndex, uint32_t numPositions, int depth)
{
    if (numPositions == 0) return;

    Level& level = levels[levelIndex];
    if (depth <= LeafDepth)
    {
        launchLeafKernel<C>(depth, level.positions, numPositions);
        return;
    }

    countKernel<C><<<numBlocks(numPositions), BlockSize>>>(level.positions, numPositions, level.counts);
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
            generateKernel<C><<<numBlocks(end - begin), BlockSize>>>(level.positions, begin, end, level.offsets, level.moves, level.parents);
            CUDA_CHECK(cudaGetLastError());
            makeKernel<C><<<numBlocks(numMoves), BlockSize>>>(level.positions, level.moves, level.parents, numMoves, next.positions);
            CUDA_CHECK(cudaGetLastError());

            searchLevel<opponent(C)>(levelIndex + 1, numMoves, depth - 1);
        }

        begin = end;
    }
}

void searchBatch()
{
    if (batchSize == 0) return;

    CUDA_CHECK(cudaMemcpy(levels[0].positions, hostBatch, batchSize * sizeof(Position), cudaMemcpyHostToDevice));
    if (batchColor == White)
    {
        searchLevel<White>(0, batchSize, gpuDepth);
    }
    else
    {
        searchLevel<Black>(0, batchSize, gpuDepth);
    }
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

    for (int i = 0; i < NumLevels; ++i)
    {
        Level& level = levels[i];
        uint32_t capacity = (i == 0) ? BatchSize : LevelCapacity;
        CUDA_CHECK(cudaMalloc(&level.positions, capacity * sizeof(Position)));
        if (i < NumLevels - 1)
        {
            CUDA_CHECK(cudaMalloc(&level.counts, (capacity + 1) * sizeof(uint32_t)));
            CUDA_CHECK(cudaMalloc(&level.offsets, (capacity + 1) * sizeof(uint32_t)));
            CUDA_CHECK(cudaMalloc(&level.moves, LevelCapacity * sizeof(Move)));
            CUDA_CHECK(cudaMalloc(&level.parents, LevelCapacity * sizeof(uint32_t)));
        }
        else
        {
            level.counts = level.offsets = level.parents = nullptr;
            level.moves = nullptr;
        }
    }

    CUDA_CHECK(cub::DeviceScan::ExclusiveSum(nullptr, scanStorageBytes, static_cast<uint32_t*>(nullptr), static_cast<uint32_t*>(nullptr), LevelCapacity + 1));
    CUDA_CHECK(cudaMalloc(&scanStorage, scanStorageBytes));

    CUDA_CHECK(cudaMallocHost(&hostBatch, BatchSize * sizeof(Position)));
    CUDA_CHECK(cudaMalloc(&deviceChunk, 2 * sizeof(uint32_t)));
    CUDA_CHECK(cudaMallocHost(&hostChunk, 2 * sizeof(uint32_t)));
    CUDA_CHECK(cudaMalloc(&deviceResult, sizeof(unsigned long long)));

    // The first launches allocate the local memory of the kernels, which isn't included in the search time
    runGpuPerft(Position(), MaxGpuDepth);

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
        CUDA_CHECK(cudaFree(level.counts));
        CUDA_CHECK(cudaFree(level.offsets));
        CUDA_CHECK(cudaFree(level.moves));
        CUDA_CHECK(cudaFree(level.parents));
    }
    CUDA_CHECK(cudaFree(scanStorage));
    CUDA_CHECK(cudaFreeHost(hostBatch));
    CUDA_CHECK(cudaFree(deviceChunk));
    CUDA_CHECK(cudaFreeHost(hostChunk));
    CUDA_CHECK(cudaFree(deviceResult));
}

#endif
