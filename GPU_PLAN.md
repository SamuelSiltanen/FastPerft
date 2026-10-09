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

Status: done. The leaf kernel is 13-15% faster than in phase 2.

Measurements (initial position depth 8 / Kiwipete depth 7 / position 6 depth 7 / position 3 depth 9, in seconds; the runs vary by about 5%):

| Variant | Time |
|---|---|
| Phase 2: kindergarten, tables in global memory | 0.837 / 2.096 / 1.687 / 0.625 |
| Kindergarten, tables in shared memory (`GPU_SHARED_TABLES`) | 0.753 / 1.897 / 1.520 / 0.601 |
| Hyperbola quintessence, tables in global memory (`GPU_HYPERBOLA_QUINTESSENCE`) | 0.954 / 2.376 / 1.941 / 0.814 |
| Hyperbola quintessence, tables in shared memory | 0.916 / 2.289 / 1.864 / 0.856 |
| + warp reduction instead of block reduction | 0.752 / 1.887 / 1.507 / 0.600 |
| + `tzcnt64` as `__popcll((x & -x) - 1)` instead of `__clzll(__brevll(x))` | 0.714-0.758 / 1.818 / 1.465 / 0.539 |
| Not inlining the evasions and en passant on the GPU (rejected) | 1.432 / 3.047 / 2.747 / 1.473 |

- Each block copies the lookup tables (16 kB) to shared memory at the start of each kernel (`loadMoveTables`). With 3 blocks of 256 threads per SM (`LeafMinBlocks`), they take 48 kB of the 64 kB of shared memory. 2 or 4 blocks per SM, or blocks of 128 threads, are slower.
- Hyperbola quintessence is slower, because 64-bit subtractions and bit reversals take several instructions on Turing. The code stays as an option (`lineAttacks` in `MoveGenerationImpl.hpp`), since newer GPUs may differ.
- Magic bitboards and Kogge-Stone were not implemented. Magic needs a 64-bit multiplication and an L2 lookup per slider (the tables don't fit in shared memory), while kindergarten with shared memory needs one shared memory lookup per line. Kogge-Stone needs even more 64-bit shifts than hyperbola quintessence.
- The bit scan of `tzcnt64` on the GPU used two slow 64-bit instructions (`__brevll` and `__clzll`). The popcount version has exactly the same results, including 64 for zero.
- Not inlining the rarely used functions moves `Position` and `Pins` to local memory, which costs much more than the smaller code saves.

Nsight Compute of the leaf kernel (Kiwipete depth 6, before the `tzcnt64` change): 80 registers, 73% of the warps active, 62% of the issue slots used, 26.3 of 32 threads active per instruction (about 18% lost to divergence), L1 hit rate 47%, memory throughput 34% of peak. The warp stall reasons: wait 18%, no instruction 17% (instruction cache misses from the large inlined code), short scoreboard 15% (shared memory and MIO instructions), not selected 12%, selected 10%, math pipe throttle 9%, barrier 8%, long scoreboard 4%. So the kernel is limited by the instructions, not by the memory.

Nsight Compute 2023.3 fails with "bad conversion" with the Finnish locale. It works with `LC_ALL=C` and `LANG=en_US` in the environment and `--metrics` with a few metrics at a time. The sections and metrics with large values still fail.

| Position | Depth | Phase 2 | Phase 3 |
|---|---|---|---|
| Initial position | 7 | 0.038 s (83.6 Gnps) | 0.035 s (92.1 Gnps) |
| Initial position | 8 | 0.885 s (96.0 Gnps) | 0.758 s (112.1 Gnps) |
| Initial position | 9 | 22.3 s (109.4 Gnps) | 19.4 s (125.5 Gnps) |
| Kiwipete | 6 | 0.049 s (165.6 Gnps) | 0.041 s (194.9 Gnps) |
| Kiwipete | 7 | 2.07 s (180.5 Gnps) | 1.82 s (206.1 Gnps) |

Ideas for later: less divergence in the leaf kernel (e.g., distribute the children of the positions of a block evenly over its threads through shared memory), and less code in the hot loop.

## Phase 4: Hybrid CPU+GPU and transposition tables

- The CPU walks the top of the tree with its hash table. The GPU consumes batches of `WorkQueue` items.
- Deduplicate each frontier level: CUB radix sort by hash, merge duplicates, sum their multiplicities. This gets transpositions within a batch without a shared table, and is the same idea as the unique(k) enumeration needed for perft(16).
- A lockless GPU hash table with 128-bit keys in device memory, probed at frontier levels with depth >= 2. It's designed for the perft(16) requirements from the start.

Status: merging and the hash table are done, the hybrid search is not (see below). The GPU search is 2.8-7x faster than in phase 3.

- Merging: before a level is searched, its positions are sorted by a 32-bit key (`cub::DeviceRadixSort`), and equal neighbors (all fields compared) are merged. Each original position is mapped to its merged position (`runOf`). Different positions with the same key only cause missed merges, never wrong counts.
- Per-position node counts: instead of adding the leaf counts to the result, each merged position gets the node count of its subtree. After the children of a chunk are searched, each parent sums the counts of its children through the mapping (`aggregateKernel`). The batch sums its positions the same way at the end.
- First version (merging with weights, the number of move sequences to each position, without the hash table): initial position depth 8 0.75 -> 0.306 s, Kiwipete depth 7 1.82 -> 0.908 s, position 6 depth 7 1.47 -> 0.628 s, position 3 depth 9 0.54 -> 0.064 s. Merging works only within a chunk, so the leaf level (24M positions at initial position depth 8, of which 9.4M unique) is merged only partly.
- Hash table: 2^25 buckets of 4 entries of 16 bytes (2 GB). An entry has a 64-bit key and the depth (8 bits) and node count (56 bits), and the bucket comes from a second, independent 64-bit hash. The key is stored XORed with the data, as in the CPU table, so that a slot written by two threads at once doesn't match. Each level is probed before it's searched, at depths 2 and up, and its node counts are stored afterwards. The positions found aren't searched again.
- At the leaf level, the positions not found are selected with `cub::DeviceSelect`, and the leaf kernel and the insertion process only them. Without that, the found positions just left idle threads in the warps, and the hash table didn't help at all.
- A collision bug: the first position hash mixed the fields with `(h ^ (h >> 31) ^ field) * multiplier`, and perft 10 from the initial position was off by 165, deterministically. Different hash constants gave the correct count. The hash now mixes each field with the MurmurHash3 finalizer (`fmix64`). Each step is bijective, so positions that differ in one field never collide.
- The 64-bit key and the independent bucket index make a false match about 2^-62 per probe. That's fine for these searches (billions of probes), but a perft(16) run (10^18 probes or more) needs wider entries, e.g. 32 bytes with a 128-bit key.
- Tuning: GPU depth 6 (5 and 7 are about the same), level capacity 4M (8M is about the same), table 2 GB (4 GB is the same), batch 64k (1M is slower for perft 10). Probing only at depths 3 and up is 30-40% slower.
- Nsight Systems, Kiwipete depth 8: leaf kernel 69%, insert 9%, merge 8.5% (gathering the positions in the sorted order), probe 4.7%, aggregate 2.7%.
- Correctness: the test positions match the published values, the differential test (which reuses the hash table across thousands of searches) had 0 mismatches, initial position depth 10 matches the published value, and position 3 depth 9 and Kiwipete depth 8 match the CPU.

| Position | Depth | Phase 3 | Phase 4 |
|---|---|---|---|
| Initial position | 8 | 0.758 s | 0.254 s |
| Initial position | 9 | 19.4 s | 3.33 s |
| Initial position | 10 | | 73.1 s |
| Kiwipete | 7 | 1.82 s | 0.629 s |
| Kiwipete | 8 | | 19.2 s (CPU with hash table: 270 s) |
| Position 6 | 7 | 1.47 s | 0.472 s |

### Leaf search optimization (after phase 4)

The search is 1.4-2.4x faster than at the end of phase 4. Measurements (initial position depth 8 / depth 9 / Kiwipete depth 7 / depth 8 / position 6 depth 7, in seconds):

| Variant | Time |
|---|---|
| Phase 4 | 0.254 / 3.33 / 0.629 / 19.2 / 0.472 |
| Leaf positions sorted by their number of moves (`SortLeavesByMoves`) | 0.227 / 2.98 / 0.575 / 17.9 / 0.419 |
| Separate kernels for generating the leaf moves and for counting the moves of the children (rejected) | 0.259 / 3.38 / 0.660 / 21.5 / 0.519 |
| Leaf depth 1 (rejected) | 1.30 / 16.7 / 3.88 / 140 / 2.90 |
| No merging at the leaf level (`MinMergeDepth`) | 0.157 / 1.96 / 0.415 / 13.3 / 0.298 |
| + GPU depth 5 instead of 6 | 0.159 / 1.87 / 0.415 / 13.0 / 0.298 |

- Sorting: the positions that weren't found are counted (`countSelectedKernel`) and sorted by their number of moves with one 8-bit radix sort pass. A warp then gets positions with about as many moves, so its threads loop about as many times. Without merging at the leaf level, sorting still helps by 1-8%.
- Nsight Compute after sorting (Kiwipete depth 7): 25.1 of 32 threads active per instruction, 80 registers, 71% of the warps active, 58% of the issue slots used. Stalls: no instruction 27%, short scoreboard 18%, wait 16%, math pipe throttle 8%, long scoreboard 3%. The depth 2 leaf kernel is 9,120 instructions (146 kB), while counting the moves of one position (the depth 1 leaf kernel) is 1,680 instructions.
- Split leaf search: the moves of the leaf positions were generated into global memory (6 bytes per move), and a second kernel made each move and counted the moves of the child, one thread per child, with a segmented warp sum per parent. The child kernel alone took 259 ms against 291 ms for the whole fused leaf kernel (Kiwipete depth 7), but generating the moves took 129 ms more, mostly from the scattered writes and from processing the positions twice.
- No merging at the leaf level: merging the largest level took 15% of the GPU time, and it reorders the positions by their hash, which separates the siblings that the leaf kernel searches efficiently together. The hash table still finds most of the duplicates between the chunks. Merging only from depth 4 up is slower.
- The register limit (3 blocks per SM), the level capacity (2M-8M), and GPU depth 7 make no difference or are slower.
- Initial position depth 10: 73.1 s -> 30.2 s.

### 128-bit hash keys

`WideHashKeys` (enabled) stores two 64-bit keys in 32-byte entries, written as two 16-byte halves. The second half has the second key XORed with the data, so an entry with halves from two writes doesn't match. The bucket comes from the second key, so 128 - log2(buckets) = 103 bits (with a 4 GB table) separate the positions in a bucket, and a false match is about 2^-102 per lookup with 2 entries per bucket, against 2^-62 with 64-bit keys and 4 entries per bucket.

Splitting a computation into separate runs with their own hash tables doesn't change the risk: a lookup compares the key with the few entries of one bucket, regardless of the table size, so the expected number of false matches is the total number of lookups of all runs times the probability per lookup. Separate runs only make the errors easier to find, if each unit is run twice with different keys.

Measurements (initial position depth 8 / depth 9 / Kiwipete depth 8 / initial position depth 10, in seconds):

| Keys | Table | Entries | Time |
|---|---|---|---|
| 64-bit, 4 per bucket | 2 GB | 128M | 0.157 / 1.89 / 13.2 / 31.4 |
| 64-bit, 4 per bucket | 1 GB | 64M | 0.160 / 2.37 / 15.5 / 40.0 |
| 64-bit, 4 per bucket | 4 GB | 256M | 0.160 / 1.71 / 10.8 / 26.0 |
| 128-bit, 2 per bucket | 2 GB | 64M | 0.163 / 2.33 / 17.2 / 38.5 |
| 128-bit, 4 per bucket | 2 GB | 64M | 0.171 / 2.64 / 16.4 / 43.2 |
| 128-bit, 2 per bucket | 4 GB | 128M | 0.162 / 1.97 / 14.0 / 30.7 |

- With the same number of entries, 128-bit keys cost about 5% (the bigger entries take more memory bandwidth). With the same memory, they cost 15-30%, because the table has half as many entries, and the large searches still get faster with more entries.
- 2 entries per bucket (one 64-byte cache line) is faster than 4 (two cache lines).
- The default is 128-bit keys in a 4 GB table, which is about as fast as the earlier 64-bit keys in a 2 GB table. The GPU uses about 6 GB at most. For the fastest search, use 64-bit keys in a 4 GB table.

### Smaller positions on the GPU

The GPU buffers store `GpuPosition` (48 bytes) instead of `Position` (64 bytes): no hash key, and the state (12 bits) in the first and last ranks of the pawn bitboard. The kernels unpack them into `Position` for the move generator.

| Position size | Initial position depth 8 / 9 / 10, Kiwipete depth 8, position 6 depth 7 | GPU time, Kiwipete depth 8 |
|---|---|---|
| 64 bytes | 0.162 / 1.97 / 30.7 s, 14.03 s, 0.304 s | 13.77 s |
| 56 bytes (no hash key) | 0.164 / 2.02 / 31.0 s, 14.05 s, 0.311 s | 14.01 s |
| 48 bytes (state in the pawn bitboard) | 0.159 / 1.94 / 30.1 s, 13.90 s, 0.299 s | 13.80 s |

- The merging was meant to get faster, but it takes under 1% of the GPU time since the leaf level isn't merged anymore. The position size matters only in the kernels that read or write positions, and they take about 25% of the GPU time, mostly in the random hash table accesses.
- 56-byte positions are slower, because they aren't aligned to 16 bytes, so they're read and written with 8-byte accesses instead of 16-byte vector accesses. The make kernel was 47% slower.
- 48-byte positions are aligned to 16 bytes. The make kernel is 24% faster and the probe kernel 4% faster, but the total is 1-2% faster at most. The levels and the merge buffer take 25% less memory for the positions (about 400 MB less).

32-byte positions (the current format): three bitboards give each square a 3-bit piece code, `a` = pawns, bishops, and queens, `b` = knights, rooks, and queens, `c` = pawns, knights, and kings, plus the white bitboard. The unused code 111 marks a rook that can still castle, so the castling rights are the corners with that code. The en passant square is always empty, and it's the only empty square in the white bitboard. The side to move isn't stored, because all positions in a level have the same side to move, so it's mixed into the hash table keys instead. Unpacking takes about 15 logical operations.

| Variant | Initial position depth 8 / 9 / 10, Kiwipete depth 8, position 6 depth 7 |
|---|---|
| 64-byte positions, 4 GB table | 0.162 / 1.97 / 30.7 s, 14.03 s, 0.304 s |
| 32-byte positions, 4 GB table | 0.153 / 1.86 / 28.9 s, 13.50 s, 0.292 s |
| 32-byte positions, table from the free memory (about 5.6 GB) | 0.151 / 1.80 / 27.6 s, 12.79 s, 0.291 s |

- 32-byte positions are 4-6% faster, and they free about 0.8 GB of GPU memory, which the hash table now takes.
- The bucket comes from the high 64 bits of `key2 * numBuckets` (`__umul64hi`), so the number of buckets can be anything. The table takes the free GPU memory after the other buffers, minus a reserve of 1 GB, up to `MaxHashTableBytes`.
- The table from the free memory is a problem if other programs use the GPU at the same time: with another program on the GPU, initial position depth 10 took 66-100 s instead of 27.6 s, probably because the GPU was shared and its memory overcommitted.

### Less divergence in the leaf search

The depth 2 leaf search counts the children in check after the others (`DeferChecks`, `perftDepth2DeferringChecks`). The first loop counts the children that aren't in check, and only records the ones in check, and the second loop makes them again and counts them with `countEvasions`. If only a few percent of the children are in check, a warp of 32 threads still has one in most iterations, and then all its threads waited for the evasion counting.

| Variant | Initial position depth 9 / 10, Kiwipete depth 7 / 8, position 6 depth 7 |
|---|---|
| Before | 1.80 / 27.6 s, 0.408 / 12.79 s, 0.291 s |
| Children in check counted after the others | 1.78 / 27.1 s, 0.398 / 12.20 s, 0.278 s |
| + leaf positions in check sorted together (rejected) | 1.78 / 27.2 s, 0.395 / 12.29 s, 0.282 s |

- Nsight Compute (Kiwipete depth 7): 27.3 of 32 threads active per instruction instead of 25.1, instruction cache stalls 24% instead of 27%, short scoreboard stalls 13% instead of 18%.
- Upper bounds, measured with wrong counts: without the en passant counting of the children, the search would be about 1% faster, and without their pins 5-7%. Deferring the pinned children would recalculate a large share of the children, so it was not tried.
- Sorting the leaf positions in check together (a check bit in the sort key) made no difference, since they're rare.

Several threads per position (rejected): a group of G threads searched one depth 2 leaf position. All threads of the group generated the same moves, each counted every G-th child, and a shuffle reduction summed the group. The idea was that the children of one position are similar, so a warp with fewer positions would diverge less.

| G | Initial position depth 8 / 9, Kiwipete depth 7 / 8, position 6 depth 7 |
|---|---|
| 1 (current) | 0.152 / 1.78 s, 0.398 / 12.27 s, 0.278 s |
| 2 | 0.162 / 1.92 s, 0.414 / 12.87 s, 0.297 s |
| 4 | 0.203 / 2.34 s, 0.470 / 14.71 s, 0.342 s |
| 8 | 0.319 / 3.61 s, 0.663 / 19.99 s, 0.515 s |
| 16 | 0.539 / 6.10 s, 1.141 / 35.10 s, 0.879 s |
| 32 | 0.983 / 10.99 s, 2.050 / 63.43 s, 1.575 s |

- The time grows almost linearly with G, about T(1) x (0.84 + 0.16 G). Generating the moves of a position is about 16% of its work, and with G threads per position, G times as many warps each generate the moves once. Generating them once into shared memory wouldn't help, since the other threads of the group would just wait. The similarity of the children saved much less than that.
- It would need a data-parallel move generator, where the threads of a group generate different parts of the moves with the same code (e.g. one piece per thread, with branch-free attack calculations), instead of the shared move generator.

The hybrid search (the CPU walks the top of the tree with its hash table) is not implemented. The GPU already handles the transpositions below the CPU levels, by merging within a batch and with its hash table between batches, and the CPU levels have only a few hundred thousand positions even at depth 10. Searching part of the tree on the CPU at about 8 Gnps would add less than 2% to the GPU's effective 300-900 Gnps.

## Testing and measurement

- Reuse the Test_FastPerft positions, and add a GPU-vs-CPU differential test over random positions that reports the first move whose counts differ.
- Record the Gnps for each phase in a README table, in the existing format.

## Risks

- 64-bit integer operations are emulated with 32-bit ones on Turing. The kindergarten multiplications and the bit scans cost several instructions each.
- Register spills: the large generator functions and `Pins` may push registers into local memory and reduce occupancy.
- Long tail: positions with check evasions or many promotions take separate, slower paths.
