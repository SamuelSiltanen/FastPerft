# FastPerft

**Fastperft** is a fast chess move generator. As a command line tool, it counts the number of leaf nodes in a full search tree at a given depth. This number is often used for comparing performance of chess move generation algorithms.

## Usage

The command line tool can be used as follows:

  `fastperft <options>`
  
Where supported options include:
  
  `-d <depth>` Depth at which the leaf nodes are counted. The default is 1 which counts the next moves in a position.
  
  `-f "<FEN>"` Position in Forsyth-Edwards notation (FEN, see. https://en.wikipedia.org/wiki/Forsyth%E2%80%93Edwards_Notation). This is supported by many chess GUIs and websites. Remember to use the quotes. The default is the initial position.

  `-h <size>` Hash table size as an exponent of 2, from 2 to 30. Each entry takes 16 bytes, so e.g. -h 20 gives 2<sup>20</sup> = 1 048 576 entries (16 MB). The default is 26 (1 GB). A negative value disables the hash table.
  
  `-w <workers>` Number of worker threads, from 1 to 64. The default is 8. Use `-w 1` to measure single-threaded performance.
  
  `-s` Print stats of the leaf nodes: captures, en passants, castles, promotions, checks, discovered checks, double checks, and checkmates, as in https://www.chessprogramming.org/Perft_Results. This uses a slower search without bulk counting and the hash table (see Stats).

For example, `fastperft -d 6 -f "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -"` counts the nodes of the Kiwipete position at depth 6.

## Requirements

The code is written for Visual Studio (MSVC) on x64 Windows, and it uses MSVC intrinsics. The CPU must support AVX2 and BMI2 (PEXT and TZCNT), i.e. Intel Haswell or newer, or AMD Zen or newer. On AMD processors before Zen 3, PEXT is very slow, so the sliding piece lookups are slow too.

## Configuration

The features are selected at compile time in `Config.hpp`:

  `MULTITHREADED` Use multiple worker threads. Enabled by default. The number of threads is set with the `-w` option. When disabled, the search runs in the main thread and `-w` is ignored.

  `LEAF_NODE_BULK_COUNT` Count the moves at the second to last level instead of making them (see Design). Enabled by default.

  `HASH_TABLE` Store node counts of subtrees in a hash table. Enabled by default. When enabled, `make` also updates the hash key, but it skips the updates if the hash table is disabled with `-h -1`.


The sliding piece attack lookup method is selected at the top of `MoveGeneration.cpp` (`PEXT_INTRINSIC`, `KINDERGARTEN_BITBOARDS`, `MAGIC_BITBOARDS`).

## Performance

On an Intel Core i7-9700K (8 cores, no hyper-threading). The times don't include allocating and clearing the hash table, which takes about 0.2 s for the default size of 1 GB.

| Position | Depth | Nodes | 1 thread, no hash table | 8 threads, no hash table | 1 thread, hash table | 8 threads, hash table (default) |
|---|---|---|---|---|---|---|
| Initial position | 7 | 3 195 901 860 | 4.3 s | 0.61 s | 1.5 s | 0.21 s |
| Initial position | 8 | 84 998 978 956 | | 16.2 s | | 2.6 s |
| Kiwipete | 6 | 8 031 647 685 | 7.3 s | 0.96 s | 2.9 s | 0.39 s |

The multithreading scales almost linearly with the number of cores. Kiwipete at depth 6, compiled without the hash table:

| Threads | Time | Speedup |
|---|---|---|
| 1 | 6.89 s | 1.0x |
| 2 | 3.64 s | 1.9x |
| 4 | 1.83 s | 3.8x |
| 8 | 0.94 s | 7.4x |

The speed depends on the position: positions with many moves per node are faster per node, because the leaf nodes are counted in bulk. The hash table helps more at deeper depths, where more positions are reached through different move orders.

## Tests

The tests are in a separate Google Test project, `Test_FastPerft`, which the solution expects in a sibling folder (`..\Test_FastPerft`). Besides unit tests for the FEN parser and parts of the move generation, it contains perft tests for the six positions in https://www.chessprogramming.org/Perft_Results, both without and with a hash table, and tests for the leaf node stats. Their depths are chosen so that each test takes up to roughly 10 seconds in a Release build, so they catch most regressions when optimizing the move generator. Run the tests in a Release build, because a Debug build is far slower.

## Design

The basic idea is to generate only legal moves. This requires detecting checks, pinned pieces, and protected squares during move generation to avoid kings left in check after the moves. This adds some complexity to the code. An alternative is to generate pseudo-legal moves, which don't take into account king left in check, and then retro-actively removing the moves after a possible king capture is detected. While this leads to simpler code, it also requires making the moves deeper in the tree. Because the search tree grows exponentially with depth, the deepest level takes most of the time, so extending the tree even one level is too much. Our approach makes it possible to cut the tree already at the second to last level of the tree, because the generated moves can be counted there already (= bulk counting), instead of making the moves.

Because of bulk counting, most of the time (roughly 80 %) is spent in the nodes at the second to last level. There the moves are only counted, not stored, so there are separate counting functions (`countP`, `countN`, `countSliders`, `countK`, `countCastling`, `countEvasions`) next to the generating functions (`generateP`, `generateN`, `generateSliders`, `generateK`, `generateCastling`, `generateEvasions`). The counting functions use population counts of the target square bitboards instead of iterating over the moves.

`perft` and the functions it calls are templated on the side to move, so that the color checks are resolved at compile time, and they are force-inlined into `perft`. The rare cases, such as en passant captures, are in separate functions that are not inlined, which keeps the inlined code small.

### Data Types and Move Generation Approach

The board presentation is bitboards. It is commonly used in chess engines and allows many clever tricks with bit operations. Each piece has its own bitboard, except queens, which are combined with bishops and rooks: `bq` contains bishops and queens, and `rq` rooks and queens, so a queen is a piece on both. In addition, there is a bitboard for white pieces. There is also some state information, such as turn, castling rights, and en passant square. Finally, the position has a 64-bit hash key, which is updated with every move when the hash table is enabled. All these fit within 64 bytes. A tighter packing is possible, but doesn't seem to bring additional benefits.

Move information is packed in 16 bits. This almost doubled the performance when compared to 32-bit move structs. It seems writing the moves is one of the bottlenecks. 16 bits is enough for storing the source and destination squares (6 + 6 bits), type of the piece to move (3 bits) and an extra bit to tell if this is a promotion. In promotion, the piece type is interpreted as the promoted piece (we know the moving piece must be a pawn). Castlings are just king moves, and en passants pawn moves.

Pawn moves are generated and counted with bit shifts for all pawns at once. A promotion is four moves, so in counting, the promotions add three more moves to the pawn move count. King and knight moves use lookup tables.

The sliding piece moves use PEXT lookup tables (https://www.chessprogramming.org/BMI2#PEXT_Bitboards): the PEXT instruction extracts the occupancy bits that matter for a square into a table index, and the table gives all bishop or rook attacks at once. The tables are large (512 kB for bishops and 4 MB for rooks), so they don't fit in the L1 or L2 cache, but they were still clearly faster than the alternatives in measurements. Kindergarten bitboards (https://www.chessprogramming.org/Kindergarten_Bitboards) are used for moves along a single line, which are needed for pinned pieces. Classical ray attacks (https://www.chessprogramming.org/Classical_Approach) are used for detecting pins and checks, because they only need work on the rays that actually have an opponent's sliding piece. Magic bitboards are also implemented, but disabled.

Bishops and queens are handled in one pass for the diagonal moves, and rooks and queens in another for the orthogonal moves. Pinned pieces are rare, so the common case has no pin checks, and the pinned pieces are handled separately along their pin lines.

Pins are stored as four bitboards, one per direction (south-north, west-east, and the two diagonals), so that a pinned piece can still move along its pin line.

The protection area (all squares attacked by the opponent) is computed for each node, and the king moves and castlings are checked against it. The king is removed from the occupancy when computing it, so that the king cannot move away from a sliding piece along the attack line.

When the king is in check by a single piece, the other pieces can only capture the checker or block the check. These moves are generated with a target mask, which contains the checker and the squares between it and the king. The squares in between are where the attacks of the king and the checker intersect. Pinned pieces cannot resolve a check, so they are skipped. When in double check, only king moves are possible.

Making a move copies the position and updates it. The piece bitboards are updated with AVX2 instructions using a table that tells which bitboards each piece type occupies, and castling rights are updated with per-square masks, so that only the special moves (en passant, double pawn moves, promotions, and castling) need branches. With the hash table, `make` also updates the hash key incrementally: the keys of the moving and captured pieces are looked up by the piece type from the keys of the source and destination squares, and the keys of the turn, castling rights, and en passant square come from a precomputed table.

### Performance Notes

Some findings from optimizing the move generator:

- Use `_tzcnt_u64` instead of `_BitScanForward64` to find the next square in a loop. On Intel, the BSF instruction depends on the previous value of its destination register, which can serialize the attack table lookups of consecutive loop iterations. Whether this happens depends on the compiler's register allocation, so it can appear or disappear with unrelated code changes.
- Small code changes can change the speed by 10-20 % through the compiler's register allocation and inlining decisions, so it's worth checking the disassembly when a change behaves unexpectedly. Force-inlining the color templated functions into `perft` gave several percent speedup, because the pins and other intermediate results can stay in registers.
- Several seemingly cheaper alternatives were measured to be slower: checking only the king's target squares instead of computing the whole protection area, finding pins with x-ray PEXT attacks, and computing knight attacks with shifts.

### Stats

The `-s` option runs a separate search (`Stats.cpp`), which makes all the moves at the last level instead of counting them in bulk, and classifies them. It doesn't use the hash table. The moves at the root are divided between the worker threads, and each thread collects its own stats, which are summed at the end. It is several times slower than the normal search, e.g. Kiwipete at depth 6 takes about 22 s with 8 threads.

A move is counted as a discovered check if a piece other than the moved one gives check, but not if it is a double check or a check by the rook in castling. With these definitions, the stats match the tables in https://www.chessprogramming.org/Perft_Results, except the double checks in Kiwipete at depths 5 and 6 (2 645 vs. 2 637, and 55 014 vs. 54 948). In all of these double checks, the moved piece gives check, and another piece gives a discovered check. The normal perft counts, which depend on detecting the double checks correctly, match at all depths, so the difference is probably in how the double checks are defined in the tables.

### Multithreading

The multithreading uses a simple work stealing approach. Each worker pushes the branches it needs to go through to a lock-protected work queue. Then it picks them from the queue, one by one, and works on them. However, any other worker can pick branches from the same work queue, so that they work on the items in adjacent branches in the same sub-tree. Once all the branches in a sub-tree have been processed, the worker that originally pushed the branches in the work queue, collects the results and returns it. Once a worker runs out of work, it picks up a branch from the work queue and helps the others.

There could be a potential dead lock, where workers pick up each others' work, and then wait for each other to finish. To avoid this, the worker that pushes the branches in the work queue, must keep on working on those branches, and if it finishes so that there is no work left in the queue, but other workers are still processing the branches that were previously in the work queue, it must wait. This can cause some idling, but typically, this is a short time.

The multithreading is enabled by default (see Configuration). Every level deeper than 4 (`MinWorkItemDepth`) pushes all its moves to the work queue on top of the levels above it, so deep searches need more queue space. Each work queue starts with room for 256 items and doubles its capacity when it is full. The queue uses logical front and back indices that are never wrapped around, so the markers that tell the workers which items they pushed stay valid when the queue grows.

### Hash Table

The hash table uses Zobrist hashing (https://www.chessprogramming.org/Zobrist_Hashing) for generating and keeping up to date 64-bit hash keys. Those are then mapped into a hash table, where each entry stores the hash key, depth, and node count. The hash table utilizes the fact that the entries are updated cache line at a time. If a collision occurs, it may use any of the other entry slots on the same cache line. If all of the slots are taken, it replaces the one with the lowest node count. The hash table is not used at the last levels (`MinHashDepth`), because there, the memory access is slower than counting the moves.

The hash table is shared by all threads without locks, using lockless hashing (https://www.chessprogramming.org/Shared_Hash_Table#Lockless). Each entry is two 64-bit words, the data (depth and node count) and the key XORed with the data. They are written and read separately, so a thread can read an entry while another thread is writing it, and get the key from one entry and the data from another. Then the key XORed with the data doesn't match the probed hash key, so the torn entry is ignored instead of giving a wrong node count. A lost or ignored entry only affects the performance, not the results. (A `std::atomic` of the whole 16-byte entry is not lock-free with MSVC, and it uses 32 bytes per entry.)
