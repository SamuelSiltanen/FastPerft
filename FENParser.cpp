#include "FENParser.hpp"

#include <cstring>

bool parseFEN(const char* fen, Position& pos)
{
    memset(&pos, 0, sizeof(Position));

    int i = 0;

    // Scan the board first    
    int square = 0;
    while (fen[i] != ' ')
    {
        switch (fen[i])
        {
        case '/':
            if ((square & 7) != 0)
                return false;
            break;
        case 'p':
            pos.p |= (1ULL << square);
            ++square;
            break;
        case 'n':
            pos.n |= (1ULL << square);
            ++square;
            break;
        case 'b':
            pos.bq |= (1ULL << square);
            ++square;
            break;
        case 'r':
            pos.rq |= (1ULL << square);
            ++square;
            break;
        case 'q':
            pos.bq |= (1ULL << square);
            pos.rq |= (1ULL << square);
            ++square;
            break;
        case 'k':
            pos.k |= (1ULL << square);
            ++square;
            break;
        case 'P':
            pos.p |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case 'N':
            pos.n |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case 'B':
            pos.bq |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case 'R':
            pos.rq |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case 'Q':
            pos.bq |= (1ULL << square);
            pos.rq |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case 'K':
            pos.k |= (1ULL << square);
            pos.w |= (1ULL << square);
            ++square;
            break;
        case '1':
            square += 1;
            break;
        case '2':
            square += 2;
            break;
        case '3':
            square += 3;
            break;
        case '4':
            square += 4;
            break;
        case '5':
            square += 5;
            break;
        case '6':
            square += 6;
            break;
        case '7':
            square += 7;
            break;
        case '8':
            square += 8;
            break;
        default:
            return false;
        }

        ++i;
    }

    if (square != 64)
        return false;

    // Skip whitespace
    ++i;

    // Turn
    if (fen[i] == 'w')
        pos.state |= TurnWhite;
    else if (fen[i] != 'b')
        return false;
    ++i;

    // Skip whitespace
    if (fen[i] != ' ')
        return false;
    ++i;

    // Castling
    while (fen[i] != ' ')
    {
        switch (fen[i])
        {
        case 'K':
            pos.state |= CastlingWhiteShort;
            break;
        case 'Q':
            pos.state |= CastlingWhiteLong;
            break;
        case 'k':
            pos.state |= CastlingBlackShort;
            break;
        case 'q':
            pos.state |= CastlingBlackLong;
            break;
        case '-':
            break;
        default:
            break;
        }

        ++i;
    }

    // Skip whitespace
    ++i;
        
    // En passant
    if (fen[i] != '-')
    {
        if (fen[i] < 'a' || fen[i] > 'h')
            return false;

        int file = static_cast<int>(fen[i] - 'a');
        ++i;

        if (fen[i] < '1' || fen[i] > '8')
            return false;

        int row = static_cast<int>(fen[i] - '1');
        int square = file + (8 - row);

        pos.state |= (row << 5);

        pos.state |= EPValid;
    }

    // Don't care about counters

    return true;
}