# FastPerft

**Fastperft** is a fast chess move generator. As a command line tool, it counts the number of leaf nodes in a full search tree at a given depth. This number is often used for comparing performance of chess move generation algorithms.

## Usage

The command line tool can be used as follows:

  `fastperft <options>`
  
Where supported options include:
  
  `-d <depth>` Depth at which the leaf nodes are counted. The default is 1 which counts the next moves in a position.
  
  `-h <size>` Hash table size as an exponent of 2. E.g. -h 20 gives 2<sup>20</sup> = 1 048 576 hash table entries. The default is 26. Negative values disable the hash table.
  
  `-w <workers>` Number of worker threads. The default is 8.
  
  `-s` Print extra stats about moves and hash table.
  
  `-f "<FEN>"` Position in Forsyth-Edwards notation (FEN, see. https://en.wikipedia.org/wiki/Forsyth%E2%80%93Edwards_Notation). This is supported by many chess GUIs and websites. Remember to use the quotes.

## Design

The basic idea is to generate only legal moves. This requires detecting checks, pinned pieces, and protected squares during move generation to avoid kings left in check after the moves. This adds some complexity to the code. An alternative is to generate pseudo-legal moves, which don't take into account king left in check, and then retro-actively removing the moves after a possible king capture is detected. While this leads to simpler code, it also requires making the moves deeper in the tree. Because the search tree grows exponentially with depth, the deepest level takes most of the time, so extending the tree even one level is too much. Our approach makes it possible to cut the tree already at the second to last level of the tree, because the generated moves can be counted there already (= bulk counting), instead of making the moves.

Another design consideration is low memory foot print and cache friendliness. Modern processors can often calculate a lot of operation during a single memory write or read to main memory, so it makes sense to find a balance between operation count and memory fetches. By keeping the core of the move generation in a few kilobytes helps to keep everything in L1 cache.

### Data Types and Move Generation Approach

The board presentation is bitboards. It is commonly used in chess engines and allows many clever tricks with bit operations. Each piece has it's own bitboard, except queens, which are combined with bishops and rooks. In addition, there is bitboard for white pieces. There is also some state information, such as turn, castling rights, and en passant square. Finally, the position has a 64-bit hash key, which is updated with every move. All these with in 64 bytes. A tighter packing is possible, but doesn't seem to bring additional benefits.

Move information is packed in 16 bits. This almost doubled the performance when compared to 32-bit move structs. It seems writing the moves is move of the bottlenecks in the current implementation. 16 bits is enough for storing the source and destination squares (6 + 6 bits), type of the piece to move (3 bits) and an extra bit to tell if this is a promotion. In promotion, the piece type is interpretted as the promoted piece (we know the moving piece must be a pawn). Castlings are just king moves, and en passants pawn moves.

Pawn moves are generated with bit shifts. Kings and knight moves uses lookup tables. The sliding piece moves are using the classical ray attacks approach (https://www.chessprogramming.org/Classical_Approach). As alternatives rotated bitboards (https://www.chessprogramming.org/Rotated_Bitboards) and magic bitboards (https://www.chessprogramming.org/Magic_Bitboards) were considered. Having implemented both of those in the past, I decided agains them. Making moves is slower in rotated bitboards approach, while the magic bitboards required larger lookup table, potentially running into problems with the cache size with all the other stuff that must fit in there.

### Multithreading

The multithreading uses a simple work stealing approach. Each worker pushes the branches it needs to go through to a lock-protected work queue. Then it picks them from the queue, one by one, and works on them. However, any other worker can pick branches from the same work queue, so that they work on the items in adjacent branches in the same sub-tree. Once all the branches in a sub-tree have been processed, the worker that originally pushed the branches in the work queue, collects the results and returns it. Once a worker runs out of work, it picks up a branch from the work queue and helps the others.

There could be a potential dead lock, where workers pick up each others' work, and then wait for each other to finish. To avoid this, the worker that pushes the branches in the work queue, must keep on working on those branches, and if it finishes so that there is no work left in the queue, but other workers are still processing the branches in the where previously in the work queue, it must wait. This can cause some idling, but typically, this is a short time.

### Hash Table

The hash table uses Zobrist hashing (https://www.chessprogramming.org/Zobrist_Hashing) for generating and keeping upto date 64-bit hash keys. Those are then mapped into a hash table, where each entry stores the hash key, depth, and node count. The hash table utilizes the fact that the entries are updated cache line at a time. If a collision occurs, it may use any of the other entry slots on the same cache line. If all of the slots are taken, it replaces the one with the lowest node count. The hash table is protected with a mutex against simultaneous accesses from multiple threads.
