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

- CUDA 12.3 does not support the VS 2022 compiler (MSVC 14.44), but the project uses the VS 2019 toolset (v142, MSVC 14.29), which works with CUDA 12.3 through MSBuild. Only a command-line nvcc build with the VS 2022 environment needs `-allow-unsupported-compiler -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`.
- nvcc rejects `perft<1 - C>` in `Perft.hpp` (int to `Color` template argument), so the GPU code must not include `Perft.hpp`. It has its own kernels anyway.
- `generateSliders` handles pinned sliders through function pointers, which is an indirect call on the GPU. It's rare, but check it when profiling.

## Phase 1: Naive kernel (correctness baseline)

- The CPU expands the tree to a split depth and uploads the frontier positions. Each GPU thread runs the existing depth-first `perft<C>` with a local-memory stack (3-4 plies left, bulk counting), and a CUB reduction sums the results.
- Sliders: kindergarten (`KINDERGARTEN_BITBOARDS` without PEXT). All tables are about 15 KB and fit in shared memory.
- `-g` option in Main, behind a `GPU_PERFT` switch in `Config.hpp`.
- Check: CPU and GPU counts match on the 6 test positions and on a few thousand random positions, compared per move (divide).
- Expected: maybe 10-20 Gnps, limited by warp divergence and uneven subtree sizes.

Status: done, and much faster than expected.

- `GpuPerft.cu`: the CPU expands the tree to depth - 3 (single-threaded), and collects the positions into batches of 64k. Two batches alternate, so the CPU fills one while the GPU searches the other. Each GPU thread searches 3 plies depth first, with the depth as a template parameter, so that the recursion is unrolled at compile time. The last ply is counted in bulk, and `cub::BlockReduce` sums the counts of each block.
- `-g` option, `GPU_PERFT` switch in `Config.hpp`. The GPU search doesn't use the hash table.
- The CUDA build customization (12.3) is in the project, `sm_75`. `cudart_static.lib` requests `LIBCMT`, so it's ignored to match `/MD`.
- Correctness: the 6 test positions (and the mirrored position 4) match the published values at depths up to 8, and a scratch differential test had 0 mismatches in 12,300 comparisons against the CPU (3,000 random positions from random walks, depths 1-4 and every tenth to depth 5, 158 of them in check and 156 with an en passant square).

| Position | Depth | CPU, 8 threads, no hash table | CPU, 8 threads, hash table | GPU (RTX 2070 Super) |
|---|---|---|---|---|
| Initial position | 7 | 0.61 s (5.3 Gnps) | 0.20 s (15.9 Gnps) | 0.113 s (28.4 Gnps) |
| Initial position | 8 | 16.2 s (5.2 Gnps) | 2.5 s (34.7 Gnps) | 2.31 s (36.8 Gnps) |
| Kiwipete | 6 | 0.93 s (8.6 Gnps) | 0.37 s (21.6 Gnps) | 0.144 s (55.8 Gnps) |
| Kiwipete | 7 | | | 4.85 s (77.2 Gnps) |

Findings for phase 2:

- The depth 3 kernel uses 255 registers, a 1472-byte stack frame, and spills 880 bytes. That allows only 2 blocks of 128 threads (8 warps) per SM, so occupancy is low. The depth 2 kernel uses 166 registers and doesn't spill. The breadth-first kernels of phase 2 have much smaller per-thread state, which should help.
- Small searches are dominated by launch overhead and too few threads (initial position depth 5 is only 400 positions). They're not worth optimizing.
- The CPU frontier expansion is single-threaded. At depth 8 from the initial position it creates 4.9M positions, which isn't a bottleneck yet, but it will be with a faster GPU search or a deeper split.

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

Status: done.

- The GPU searches the last 5 plies (`MaxGpuDepth`). The upper 3 are breadth first as described above, and the last 2 (`LeafDepth`) are depth first in each thread, using the phase 1 code. Storing the last level and counting it in bulk (leaf depth 1) moves too much memory, and leaf depth 3 spills registers.
- All positions of a level have the same side to move, so the kernels are templated on the color.
- If the moves of a level don't fit in the next level (4M positions), the level is split into chunks with a binary search over the prefix sums (one thread), and the chunks are searched one after another. Each level has its own buffers, so the memory use is fixed (about 1 GB).
- The kernels are loaded eagerly (`CUDA_MODULE_LOADING`), and a warm-up search allocates their local memory in `initGpuPerft`, so that small searches don't pay about 35 ms for them.
- The leaf kernel takes 97% of the GPU time (Nsight Systems, Kiwipete depth 7), and the breadth-first levels about 3%.
- Tuning (initial position depth 8 / Kiwipete depth 7 / position 6 depth 7, in seconds): leaf depth 1: 1.86 / 4.63 / 4.10, leaf depth 2: 1.14 / 2.68 / 2.17, leaf depth 3: 2.14 / 4.60 / 3.76. GPU depth 4, 5, or 6 and a level capacity of 2M-16M positions make no difference. 256 threads per block is 11% faster than 128, and limiting the registers of the leaf kernel to 3 blocks per SM (`LeafMinBlocks`) another 12%: 0.87 / 2.10 / 1.69.
- Correctness: the test positions match the published values (also initial position depth 9), and the differential test had 0 mismatches in 12,300 comparisons.

| Position | Depth | Phase 1 | Phase 2 |
|---|---|---|---|
| Initial position | 7 | 0.113 s (28.4 Gnps) | 0.038 s (83.6 Gnps) |
| Initial position | 8 | 2.31 s (36.8 Gnps) | 0.885 s (96.0 Gnps) |
| Initial position | 9 | | 22.3 s (109.4 Gnps) |
| Kiwipete | 6 | 0.144 s (55.8 Gnps) | 0.049 s (165.6 Gnps) |
| Kiwipete | 7 | 4.85 s (77.2 Gnps) | 2.07 s (180.5 Gnps) |

Findings for phase 3:

- Optimizing the leaf kernel is what matters now. It generates the moves of a position into a local array, and makes and counts each of them. Candidates: the slider lookups (phase 3), the kindergarten and other tables in shared memory, fewer registers, and the warp divergence from positions with different numbers of moves and from the check evasions.

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
