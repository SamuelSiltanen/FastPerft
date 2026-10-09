# GPU Perft Port Plan

All GPU work happens on the `gpu` branch. The CPU code on `master` is not modified. On this branch, the shared code may be refactored where the GPU needs it, but the changes are kept small and mechanical, so that later changes on `master` can be merged into `gpu` with few conflicts.

## Starting point

- RTX 2070 Super, 8 GB, Turing (`sm_75`), 40 SMs. CUDA 12.3 and the VS 2022 CUDA 12.3 build customization are installed.
- `Position` is 6 bitboards + state + hash (64 B), the same compact layout as Ankan Banerjee's perft_gpu.
- The move generator is branchy but allocation free and templated on color. The `count*()` functions already do bulk counting at the last ply, which is the core of a GPU design.

What blocks a straight port:

1. Intrinsics: `_tzcnt_u64`, `__popcnt64`, `_BitScanForward64/Reverse64`, `_lzcnt_u64`, `_pext_u64`. All but PEXT have GPU equivalents.
2. `make()` uses AVX2 (`_mm256_*`) for the p/n/bq/rq lanes.
3. Lookup tables are globals: `nmoves`, `kmoves`, `rays`, kindergarten, PEXT (`BAttacks`/`RAttacks`).
4. The code lives in .cpp files with explicit template instantiation. nvcc needs the bodies in the `.cu` translation unit to inline them (`-rdc` would cost a lot of speed).
5. Windows TDR: if the GPU drives the display, kernels running longer than about 2 s are killed. Keep each launch well under 100 ms, or raise `TdrDelay`.

## Phase 0: Portability refactor (on the `gpu` branch)

Goal: the CPU build still passes all tests at the same speed.

- `Platform.hpp`: `FP_INLINE` (`__forceinline`, or `__host__ __device__ __forceinline__` under nvcc), and wrappers `popcount`, `lsb`, `msb`, `pext` that map to `__popcll`, `__ffsll - 1`, `63 - __clzll` under `__CUDA_ARCH__`. Replace the `_BitScan*` calls.
- `make()`: a scalar path for the four lanes under `__CUDA_ARCH__`.
- Move the templated bodies of `generate*`, `count*`, `findPinsAndCheckers`, `findProtectionArea`, and `make` into `MoveGenerationImpl.hpp` / `MakeImpl.hpp`. The .cpp files keep the explicit instantiations, so the public signatures and the test project are unaffected.
- Tables in one `MoveTables` struct reached through a pointer. On the CPU it points to the existing globals. On the GPU it is copied to device memory, and each block loads the hot parts into shared memory.
- Check: `Test_FastPerft` passes and the Kiwipete d6 timings are unchanged within noise.

Status: done.

- `MoveGeneration.hpp` and `Make.hpp` include the impl headers, so all translation units see the template bodies. The .cpp files keep the explicit instantiations, the non-template wrappers, and the CPU-only code (table setup, PEXT and magic tables, `generateMovesTo`/`generateCheckEvasions`).
- The castling tables of `make()` moved into `MoveTables`, and `bishopRays` replaces `BAttacks[sq][0]` (a PEXT table) in the check evasions.
- `make()` on the GPU does not update the hash key yet (`MAKE_UPDATES_HASH`). That comes with the GPU hash table in phase 4.
- All 47 tests pass. The CPU got slightly faster, probably because the bodies can be inlined without link-time code generation: Kiwipete d6 (8 threads, no hash table) 0.97 s -> 0.93 s, initial position d7 (1 thread, no hash table) 4.32 s -> 4.21 s.
- A scratch nvcc build (one thread per position, depth-first, 3 plies on the GPU) gave the correct perft(5) for all 6 test positions.

Findings for phase 1:

- CUDA 12.3 does not support the installed MSVC (14.44). Install CUDA 12.4 or newer. Until then, nvcc needs `-allow-unsupported-compiler -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`.
- nvcc rejects `perft<1 - C>` in `Perft.hpp` (int to `Color` template argument), so the GPU code must not include `Perft.hpp`. It has its own kernels anyway.
- `generateSliders` handles pinned sliders through function pointers, which is an indirect call on the GPU. It's rare, but check it when profiling.

## Phase 1: Naive kernel (correctness baseline)

- The CPU expands the tree to a split depth and uploads the frontier positions. Each GPU thread runs the existing depth-first `perft<C>` with a local-memory stack (3-4 plies left, bulk counting), and a CUB reduction sums the results.
- Sliders: kindergarten (`KINDERGARTEN_BITBOARDS` without PEXT). All tables are about 15 KB and fit in shared memory.
- `-g` option in Main, behind a `GPU_PERFT` switch in `Config.hpp`.
- Check: CPU and GPU counts match on the 6 test positions and on a few thousand random positions, compared per move (divide).
- Expected: maybe 10-20 Gnps, limited by warp divergence and uneven subtree sizes.

## Phase 2: Breadth-first kernels (Banerjee's design)

For each level of a batch:

1. Count: one thread per position runs the `count*` functions.
2. Scan: exclusive prefix sum (`cub::DeviceScan`) gives the offsets.
3. Generate: one thread per position writes its moves and the parent index at its offset.
4. Make: one thread per move builds the child position.
5. Last level: the bulk count kernel, then a CUB reduction. The last two plies are never stored.

- Divergence mostly disappears, because all threads in a kernel do the same kind of work.
- Memory: 8 GB holds about 10^8 positions per frontier level at 64 B (56 B without `hash` when there's no hash table). The CPU picks the batch size and split depth so each level fits and each launch is short.
- 2-3 CUDA streams to overlap upload, kernels, and readback.
- Expected: 40-60 Gnps or more without a hash table, against 8.4 Gnps on the CPU.

## Phase 3: Slider lookups on the GPU

Measure with the same harness: kindergarten in shared memory, plain magic in L2 (the compressed rook table is about 800 KB, the L2 is 4 MB), Kogge-Stone (no tables), and hyperbola quintessence with `__brevll`. The winner depends on register pressure and occupancy more than on instruction counts. Profile with Nsight Compute.

## Phase 4: Hybrid CPU+GPU and transposition tables

- The CPU walks the top of the tree with its hash table. The GPU consumes batches of `WorkQueue` items.
- Deduplicate each frontier level: CUB radix sort by hash, merge duplicates, sum their multiplicities. This gets transpositions within a batch without a shared table, and is the same idea as the unique(k) enumeration needed for perft(16).
- A lockless GPU hash table with 128-bit keys in device memory, probed at frontier levels with depth >= 2. It's designed for the perft(16) requirements from the start.

## Testing and measurement

- Reuse the Test_FastPerft positions, and add a GPU-vs-CPU differential test over random positions that reports the first move whose counts differ.
- Record the Gnps for each phase in a README table, in the existing format.

## Risks

- 64-bit integer operations are emulated with 32-bit ones on Turing. The kindergarten multiplications and the bit scans cost several instructions each.
- Register spills: the large generator functions and `Pins` may push registers into local memory and reduce occupancy.
- Long tail: positions with check evasions or many promotions take separate, slower paths.
