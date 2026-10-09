// Copyright 2022 Samuel Siltanen
// GpuPerft.cu

#include "GpuPerft.hpp"
#include "Config.hpp"

#if GPU_PERFT

#include "MoveGeneration.hpp"
#include "Make.hpp"

#include <cub/block/block_reduce.cuh>
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

namespace
{

constexpr int GpuDepth = 3; // Plies that each GPU thread searches
constexpr int BlockSize = 128;
constexpr int BatchSize = 64 * 1024; // Positions per kernel launch. Keep the launches short because of the Windows GPU timeout.
constexpr int MaxMoves = 256; // At most 218 legal moves in any position
constexpr int NumBatches = 2; // The CPU fills one batch while the GPU searches the other

FP_HOST_DEVICE constexpr Color opponent(Color c) { return (c == White) ? Black : White; }

// Depth-first perft of one position in one GPU thread. The depth is a template parameter, so that
// the recursion is unrolled at compile time and each level has its own move list in registers or local memory.
template<Color C, int Depth>
__device__ uint64_t perftThread(const Position& pos)
{
    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);
    uint64_t anyPins = pins.pinnedSENW | pins.pinnedSWNE | pins.pinnedSN | pins.pinnedWE;

    if constexpr (Depth == 1)
    {
        if (checkers) return countEvasions<C>(pos, occ, pArea, checkers, pins);

        return countP<C>(pos, occ, pins) + countN<C>(pos, occ, anyPins) + countSliders<C>(pos, occ, pins) +
            countK<C>(pos, occ, pArea) + countCastling<C>(pos, occ, pArea);
    }
    else
    {
        Move moves[MaxMoves];
        Move* end = moves;
        if (checkers)
        {
            end = generateEvasions<C>(pos, end, occ, pArea, checkers, pins);
        }
        else
        {
            end = generateP<C>(pos, end, occ, pins);
            end = generateN<C>(pos, end, occ, anyPins);
            end = generateSliders<C>(pos, end, occ, pins);
            end = generateK<C>(pos, end, occ, pArea);
            end = generateCastling<C>(pos, end, occ, pArea);
        }

        uint64_t count = 0;
        for (const Move* move = moves; move < end; ++move)
        {
            count += perftThread<opponent(C), Depth - 1>(make<C>(pos, *move));
        }
        return count;
    }
}

// One thread per position. The node counts are summed within each block and then added to the result.
template<int Depth>
__global__ void __launch_bounds__(BlockSize) perftKernel(const Position* positions, int numPositions, unsigned long long* result)
{
    int index = blockIdx.x * blockDim.x + threadIdx.x;

    unsigned long long count = 0;
    if (index < numPositions)
    {
        const Position pos = positions[index];
        count = (pos.state & TurnWhite) ? perftThread<White, Depth>(pos) : perftThread<Black, Depth>(pos);
    }

    using BlockReduce = cub::BlockReduce<unsigned long long, BlockSize>;
    __shared__ typename BlockReduce::TempStorage reduceStorage;
    unsigned long long blockCount = BlockReduce(reduceStorage).Sum(count);
    if (threadIdx.x == 0)
    {
        atomicAdd(result, blockCount);
    }
}

void launchPerftKernel(int depth, const Position* positions, int numPositions, unsigned long long* result, cudaStream_t stream)
{
    int numBlocks = (numPositions + BlockSize - 1) / BlockSize;
    switch (depth)
    {
    case 1: perftKernel<1><<<numBlocks, BlockSize, 0, stream>>>(positions, numPositions, result); break;
    case 2: perftKernel<2><<<numBlocks, BlockSize, 0, stream>>>(positions, numPositions, result); break;
    case 3: perftKernel<3><<<numBlocks, BlockSize, 0, stream>>>(positions, numPositions, result); break;
    default: fprintf(stderr, "Unsupported GPU depth %d\n", depth); exit(EXIT_FAILURE);
    }
    CUDA_CHECK(cudaGetLastError());
}

struct Batch
{
    Position* hostPositions; // Pinned, so that the copy to the GPU is asynchronous
    Position* devicePositions;
    unsigned long long* hostResult;
    unsigned long long* deviceResult;
    cudaStream_t stream;
    int numPositions;
    bool inFlight;
};

Batch batches[NumBatches];
int currentBatch = 0;
int gpuDepth = 0;
uint64_t totalCount = 0;

// Waits until the GPU has searched the batch and adds its node count to the total
void collect(Batch& batch)
{
    if (!batch.inFlight) return;
    CUDA_CHECK(cudaStreamSynchronize(batch.stream));
    totalCount += *batch.hostResult;
    batch.inFlight = false;
}

void submit(Batch& batch)
{
    if (batch.numPositions == 0) return;
    CUDA_CHECK(cudaMemcpyAsync(batch.devicePositions, batch.hostPositions, batch.numPositions * sizeof(Position), cudaMemcpyHostToDevice, batch.stream));
    CUDA_CHECK(cudaMemsetAsync(batch.deviceResult, 0, sizeof(unsigned long long), batch.stream));
    launchPerftKernel(gpuDepth, batch.devicePositions, batch.numPositions, batch.deviceResult, batch.stream);
    CUDA_CHECK(cudaMemcpyAsync(batch.hostResult, batch.deviceResult, sizeof(unsigned long long), cudaMemcpyDeviceToHost, batch.stream));
    batch.inFlight = true;
    batch.numPositions = 0;
}

void addPosition(const Position& pos)
{
    Batch* batch = &batches[currentBatch];
    if (batch->numPositions == BatchSize)
    {
        submit(*batch);
        currentBatch = (currentBatch + 1) % NumBatches;
        batch = &batches[currentBatch];
        collect(*batch);
    }
    batch->hostPositions[batch->numPositions++] = pos;
}

// Collects the positions at the given depth into the batches
template<Color C>
void expand(const Position& pos, int depth)
{
    if (depth == 0)
    {
        addPosition(pos);
        return;
    }

    uint64_t occ = pos.p | pos.n | pos.bq | pos.rq | pos.k;

    Pins pins;
    uint64_t checkers = findPinsAndCheckers<C>(pos, occ, pins);
    uint64_t pArea = findProtectionArea<C>(pos, occ);

    Move moves[MaxMoves];
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
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount == 0)
    {
        fprintf(stderr, "No CUDA device found\n");
        return false;
    }

    CUDA_CHECK(cudaMemcpyToSymbol(d_moveTables, &moveTables, sizeof(MoveTables)));

    for (Batch& batch : batches)
    {
        CUDA_CHECK(cudaMallocHost(&batch.hostPositions, BatchSize * sizeof(Position)));
        CUDA_CHECK(cudaMalloc(&batch.devicePositions, BatchSize * sizeof(Position)));
        CUDA_CHECK(cudaMallocHost(&batch.hostResult, sizeof(unsigned long long)));
        CUDA_CHECK(cudaMalloc(&batch.deviceResult, sizeof(unsigned long long)));
        CUDA_CHECK(cudaStreamCreate(&batch.stream));
        batch.numPositions = 0;
        batch.inFlight = false;
    }

    return true;
}

uint64_t runGpuPerft(const Position& pos, int depth)
{
    if (depth <= 0) return 1;

    gpuDepth = (depth < GpuDepth) ? depth : GpuDepth;
    totalCount = 0;
    currentBatch = 0;

    if (pos.state & TurnWhite)
    {
        expand<White>(pos, depth - gpuDepth);
    }
    else
    {
        expand<Black>(pos, depth - gpuDepth);
    }

    submit(batches[currentBatch]);
    for (Batch& batch : batches)
    {
        collect(batch);
    }

    return totalCount;
}

void releaseGpuPerft()
{
    for (Batch& batch : batches)
    {
        CUDA_CHECK(cudaStreamDestroy(batch.stream));
        CUDA_CHECK(cudaFree(batch.deviceResult));
        CUDA_CHECK(cudaFreeHost(batch.hostResult));
        CUDA_CHECK(cudaFree(batch.devicePositions));
        CUDA_CHECK(cudaFreeHost(batch.hostPositions));
    }
}

#endif
