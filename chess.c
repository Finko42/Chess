/* Requires: SDL2 and SDL2_image
   Compile with: eval cc chess.c $(pkg-config sdl2 sdl2_image --cflags --libs)

   Each board square is represented with a byte:
   MSB - Piece selected
   7th - Dot is present on square
   6th - Pawn can be "en passanted"
   5th - Rook/King hasn't moved / Pawn is top or bottom pawn
   4th - Color of piece
   3 LSBs - Piece type
*/

#include "SDL.h"
#include "SDL_image.h"

typedef unsigned char u8;

#define WIN_WIDTH  600
#define WIN_HEIGHT 600
#define TILE_LEN (WIN_WIDTH >> 3)
#define TEXTURES_NUM 13

enum Pieces { NONE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum Color  { WHITE, BLACK=8 };

SDL_Texture* loadSVGFromFile(SDL_Renderer* renderer, const char* filename,
                             int width, int height)
{
    SDL_RWops* io = SDL_RWFromFile(filename, "r");
    if (io == NULL) {
        SDL_Log("Error opening %s: %s\n", filename, SDL_GetError());
        return NULL;
    }

    SDL_Surface* sur = IMG_LoadSizedSVG_RW(io, width, height);
    SDL_RWclose(io);
    if (sur == NULL) {
        SDL_Log("Error creating surface: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, sur);
    SDL_FreeSurface(sur);
    if (tex == NULL) {
        SDL_Log("Error creating texture: %s\n", SDL_GetError());
        return NULL;
    }

    return tex;
}

// Pass board byte and get texture offset
u8 getTextureID(u8 tile)
{
    u8 p = tile & 7;
    if ((tile >> 3) & 1)
        return 5 + p; // Is black
    return p - 1; // Is white
}

// Set board to initial state
void setupBoard(u8* board, u8 white_on_top)
{
    u8 i, side1, side2, left_middle, right_middle;

    // | 16 is for king and rooks that haven't moved
    if (white_on_top) {
        side1 = WHITE;
        side2 = BLACK;
        left_middle = KING | 16;
        right_middle = QUEEN;
    } else {
        side1 = BLACK;
        side2 = WHITE;
        left_middle = QUEEN;
        right_middle = KING | 16;
    }

    board[0] = side1 | ROOK | 16;
    board[1] = side1 | KNIGHT;
    board[2] = side1 | BISHOP;
    board[3] = side1 | left_middle;
    board[4] = side1 | right_middle;
    board[5] = side1 | BISHOP;
    board[6] = side1 | KNIGHT;
    board[7] = side1 | ROOK | 16;
    for (i = 8; i < 16; i++)
        board[i] = side1 | PAWN | 16; // | 16 is for top pawns
    for (; i < 48; i++)
        board[i] = NONE;
    for (; i < 56; i++)
        board[i] = side2 | PAWN;
    board[56] = side2 | ROOK | 16;
    board[57] = side2 | KNIGHT;
    board[58] = side2 | BISHOP;
    board[59] = side2 | left_middle;
    board[60] = side2 | right_middle;
    board[61] = side2 | BISHOP;
    board[62] = side2 | KNIGHT;
    board[63] = side2 | ROOK | 16;
}

u8 drawBoard(u8* board, SDL_Renderer* renderer, SDL_Texture** textures,
             SDL_Texture* chessboard, SDL_Rect* tile)
{
    // It is recommended to clear renderer before each frame drawing
    if (SDL_RenderClear(renderer) != 0) {
        SDL_Log("Error clearing renderer: %s\n", SDL_GetError());
        return 1;
    }

    // Draw chessboard
    if (SDL_RenderCopy(renderer, chessboard, NULL, NULL) != 0) {
        SDL_Log("Error drawing chessboard: %s\n", SDL_GetError());
        return 1;
    }

    // Draw pieces
    u8 i;
    for (i = 0; i < 64; i++) {
        tile->x = (i & 7) * TILE_LEN;
        tile->y = (i >> 3) * TILE_LEN;

        // Check if there is piece on tile
        if (board[i] & 7) {

            // Check if selected
            if (board[i] >> 7) {
                if (SDL_RenderFillRect(renderer, tile) != 0) {
                    SDL_Log("Error drawing select square: %s\n", SDL_GetError());
                    return 1;
                }
            }

            // Draw piece
            if (SDL_RenderCopy(renderer, textures[getTextureID(board[i])], NULL, tile) != 0) {
                SDL_Log("Error drawing piece: %s\n", SDL_GetError());
                return 1;
            }
        }

        // Draw dot if exists
        if ((board[i] >> 6) & 1) {
            if (SDL_RenderCopy(renderer, textures[12], NULL, tile) != 0) {
                SDL_Log("Error drawing dot: %s\n", SDL_GetError());
                return 1;
            }
        }
    }

    SDL_RenderPresent(renderer);
    return 0;
}

// Removes all dots from board
void removeDots(u8* board)
{
    u8 i;
    for (i = 0; i < 64; i++)
        board[i] &= 0xbf;
}

/* Returns whether target square was dotted
   0 = Not dotted, piece of same side on square
   1 = Empty square dotted
   2 = Occupied square dotted */
u8 dotSquare(u8* board, u8 pos, u8 n)
{
    if ((board[n] & 7) == NONE) {
        board[n] |= 64;
        return 1;
    }
    if (((board[pos] >> 3) & 1) != ((board[n] >> 3) & 1)) {
        board[n] |= 64;
        return 2;
    }
    return 0;
}

void dotDiagonals(u8* board, u8 i)
{
    u8 j;
    for (j = i - 9; (j < 64) && ((j & 7) != 7); j -= 9)
        if (dotSquare(board, i, j) != 1) break;
    for (j = i - 7; (j < 64) && ((j & 7) != 0); j -= 7)
        if (dotSquare(board, i, j) != 1) break;
    for (j = i + 7; (j < 64) && ((j & 7) != 7); j += 7)
        if (dotSquare(board, i, j) != 1) break;
    for (j = i + 9; (j < 64) && ((j & 7) != 0); j += 9)
        if (dotSquare(board, i, j) != 1) break;
}

/* Moves piece on a board
   i is where piece moved */
void movePiece(u8* board, u8 i)
{
    u8 j;

    // Each turn turn off en passant bit for pawns
    for (j = 0; j < 64; j++)
        board[j] &= 0xdf;

    // Get index of selected piece
    for (j = 0; (board[j] >> 7) == 0; j++);
    removeDots(board);

    switch (board[j] & 7) {
    case PAWN:
        // Check if promoting pawn
        if ((i < 8) || (i >= 56)) {
            board[i] = (board[j] & 8) | QUEEN;
        // If en passant
        } else if (((board[i] & 7) == NONE)
                  && ((j & 7) != (i & 7))) {
            board[i] = board[j] & 31;
            // Delete en passanted pawn
            if ((j & 7) > (i & 7)) {
                board[j-1] = NONE;
            } else {
                board[j+1] = NONE;
            }
        } else {
            // Move selected piece on new square
            board[i] = board[j] & 31;
            // Record that pawn moved 2 squares (for en passant)
            if (((i-j) == 16) || ((j-i) == 16))
                 board[i] |= 32;
        }
        break;
    case KING:
        // Castling
        // If rook is on left
        if ((j-i) == 2) {
        board[i+1] = board[i & 0xf8] & 15;
        board[i & 0xf8] = NONE;
        // If rook is on right
        } else if ((i-j) == 2) {
            board[j+1] = board[(i & 0xf8)+7] & 15;
            board[(i & 0xf8)+7] = NONE;
        }
    case ROOK:
        // If king or rook moves, record it to prevent castling
        board[i] = board[j] & 15;
        break;
    default:
        // Replace dotted square
        board[i] = board[j] & 63;
    }

    // Delete piece from original position
    board[j] = NONE;
}

// Calculates dots for a piece
void calculateMoves(u8* board, u8 i)
{
    u8 j;

    switch (board[i] & 7) {
    case PAWN:
        if ((board[i] >> 4) & 1) {
        // Top pawn
        // Two squares forward
        if ((i < 16) && ((board[i+16] & 7) == NONE))
            board[i+16] |= 64;
        // One square forward
        if ((i < 56) && ((board[i+8] & 7) == NONE))
            board[i+8] |= 64;
        // Capture on right
        if ((i < 55) && ((i & 7) != 7) && (((board[i+9] & 7) &&
            (((board[i+9] >> 3) & 1) != ((board[i] >> 3) & 1)))
            // En passant on right
            || (((board[i+1] & 7) == PAWN) && ((board[i+1] >> 5) & 1)
            && (((board[i+1] >> 3) & 1) != ((board[i] >> 3) & 1)))))
            board[i+9] |= 64;
        // Capture on left
        if ((i < 57) && (i & 7) && (((board[i+7] & 7) &&
            (((board[i+7] >> 3) & 1) != ((board[i] >> 3) & 1)))
            // En passant on left
            || (((board[i-1] & 7) == PAWN) && ((board[i-1] >> 5) & 1)
            && (((board[i-1] >> 3) & 1) != ((board[i] >> 3) & 1)))))
            board[i+7] |= 64;
        } else {
            // Bottom pawn
            // Two squares forward
            if ((i >= 48) && ((board[i-16] & 7) == NONE))
                board[i-16] |= 64;
            // One square forward
            if ((i >= 8) && ((board[i-8] & 7) == NONE))
                board[i-8] |= 64;
            // Capture on left
            if ((i >= 9) && (i & 7) && (((board[i-9] & 7) &&
                (((board[i-9] >> 3) & 1) != ((board[i] >> 3) & 1)))
                // En passant on left
                || (((board[i-1] & 7) == PAWN) && ((board[i-1] >> 5) & 1)
                && (((board[i-1] >> 3) & 1) != ((board[i] >> 3) & 1)))))
                board[i-9] |= 64;
            // Capture on right
            if ((i > 7) && ((i & 7) != 7) && (((board[i-7] & 7) &&
                (((board[i-7] >> 3) & 1) != ((board[i] >> 3) & 1)))
                // En passant on right
                || (((board[i+1] & 7) == PAWN) && ((board[i+1] >> 5) & 1)
                && (((board[i+1] >> 3) & 1) != ((board[i] >> 3) & 1)))))
                board[i-7] |= 64;
        }
        break;
    case KNIGHT:
        if ((i >= 17) && (i & 7))
            dotSquare(board, i, i-17);
        if ((i >= 15) && ((i & 7) != 7))
            dotSquare(board, i, i-15);
        if ((i >= 10) && ((i & 7) > 1))
            dotSquare(board, i, i-10);
        if ((i >= 6) && ((i & 7) < 6))
            dotSquare(board, i, i-6);
        if ((i < 47) && (i & 7) != 7)
            dotSquare(board, i, i+17);
        if ((i < 49) && (i & 7))
            dotSquare(board, i, i+15);
        if ((i < 54) && ((i & 7) < 6))
            dotSquare(board, i, i+10);
        if ((i < 58) && ((i & 7) > 1))
            dotSquare(board, i, i+6);
        break;
    case BISHOP:
        dotDiagonals(board, i);
        break;
    case QUEEN:
        dotDiagonals(board, i);
    case ROOK:
        for (j = i - 8; j < 64; j -= 8)
            if (dotSquare(board, i, j) != 1) break;
        for (j = i - 1; (j & 7) != 7; j--)
            if (dotSquare(board, i, j) != 1) break;
        for (j = i + 1; (j & 7) != 0; j++)
            if (dotSquare(board, i, j) != 1) break;
        for (j = i + 8; j < 64; j += 8)
            if (dotSquare(board, i, j) != 1) break;
        break;
    case KING:
        if (i >= 8)
            dotSquare(board, i, i-8);
        if (i & 7)
            dotSquare(board, i, i-1);
        if ((i & 7) != 7)
            dotSquare(board, i, i+1);
        if (i < 56)
            dotSquare(board, i, i+8);
        if ((i >= 9) && (i & 7))
            dotSquare(board, i, i-9);
        if ((i > 7) && ((i & 7) != 7))
            dotSquare(board, i, i-7);
        if ((i < 57) && (i & 7))
            dotSquare(board, i, i+7);
        if ((i < 55) && ((i & 7) != 7))
            dotSquare(board, i, i+9);

        // Castling
        if ((board[i] >> 4) & 1) {
            if ((i & 7) == 3) {
                // King on left
                if (((board[i-3] & 7) == ROOK) && ((board[i-3] >> 4) & 1)
                    && ((board[i-2] & 7) == NONE) && ((board[i-1] & 7) == NONE))
                    dotSquare(board, i, i-2);
                if (((board[i+4] & 7) == ROOK) && ((board[i+4] >> 4) & 1)
                    && ((board[i+1] & 7) == NONE) && ((board[i+2] & 7) == NONE)
                    && ((board[i+3] & 7) == NONE))
                    dotSquare(board, i, i+2);
            } else {
                // King on right
                if (((board[i+3] & 7) == ROOK) && ((board[i+3] >> 4) & 1)
                    && ((board[i+2] & 7) == NONE) && ((board[i+1] & 7) == NONE))
                    dotSquare(board, i, i+2);
                if (((board[i-4] & 7) == ROOK) && ((board[i-4] >> 4) & 1)
                    && ((board[i-1] & 7) == NONE) && ((board[i-2] & 7) == NONE)
                    && ((board[i-3] & 7) == NONE))
                    dotSquare(board, i, i-2);
            }
        }
        break;
    }
}

// next_sides_turn: 1 = white, 0 = black
u8 verifyMove(u8* board, u8 i, u8 next_sides_turn)
{
    u8 board2[64];
    u8 j;

    // Make copy of board
    for (j = 0; j < 64; j++)
        board2[j] = board[j];

    // Simulate if move is played
    movePiece(board2, i);

    // Search board for opposite player's pieces
    for (j = 0; j < 64; j++) {
        if ((board2[j] & 7) &&
            (((board2[j] >> 3) & 1) == next_sides_turn)) {
            // See if player can take king on next turn
            calculateMoves(board2, j);
            for (i = 0; i < 64; i++) {
                if (((board2[i] >> 6) & 1) && ((board2[i] & 7) == KING))
                    return 0;
            }
            removeDots(board2);
        }
    }

    return 1;
}

int main(int argc, char *argv[])
{
    const char* asset_names[] = { 
        "assets/white_pieces/white_pawn.svg",
        "assets/white_pieces/white_knight.svg",
        "assets/white_pieces/white_bishop.svg",
        "assets/white_pieces/white_rook.svg",
        "assets/white_pieces/white_queen.svg",
        "assets/white_pieces/white_king.svg",
        "assets/black_pieces/black_pawn.svg",
        "assets/black_pieces/black_knight.svg",
        "assets/black_pieces/black_bishop.svg",
        "assets/black_pieces/black_rook.svg",
        "assets/black_pieces/black_queen.svg",
        "assets/black_pieces/black_king.svg",
        "assets/dot.svg"
    };

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("Error initializing SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Chess", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_OPENGL);
    if (window == NULL) {
        SDL_Log("Error creating window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        SDL_Log("Error creating renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load textures
    SDL_Texture* textures[TEXTURES_NUM];
    u8 i;
    for (i = 0; i < TEXTURES_NUM; i++) {
        textures[i] = loadSVGFromFile(renderer, asset_names[i], TILE_LEN, TILE_LEN);
        if (textures[i] == NULL) {
            SDL_Log("Error loading textures: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    // Make dot transparent (25% opacity)
    if (SDL_SetTextureAlphaMod(textures[12], 63) != 0) {
        SDL_Log("Error setting alpha mod: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* chessboard = loadSVGFromFile(renderer, "assets/chessboard.svg",
                                              WIN_WIDTH, WIN_HEIGHT);
    if (chessboard == NULL) {
        SDL_Log("Error loading textures: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Set to tile highlight color
    if (SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255) != 0) {
        SDL_Log("Error setting draw color: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    u8 board[64];
    // If 'b' is passed as first arg, put black on bottom
    setupBoard(board, (argc > 1) && ((argv[1][0] | 32) == 'b'));

    SDL_Rect tile;
    tile.w = TILE_LEN;
    tile.h = TILE_LEN;

    if (drawBoard(board, renderer, textures, chessboard, &tile) != 0) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    u8 is_whites_turn = 1, j;
    SDL_Event e;
    while (1) {
	    if (SDL_WaitEvent(&e)) {
            // Close when exit button is clicked
            if (e.type == SDL_QUIT) {
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                // Calculate index in chessboard from cursor position
                i = ((e.button.y / TILE_LEN) << 3) + e.button.x / TILE_LEN;

                // If tile is empty, skip
                if (board[i] == 0)
                    continue;

                // If moving piece (clicked on dot)
                if ((board[i] >> 6) & 1) {
                    movePiece(board, i);

                    if (drawBoard(board, renderer, textures,
                                  chessboard, &tile) != 0)
                        break;
                        
                    // Flip turn
                    is_whites_turn ^= 1;
                    continue;
                }

                // If empty and not a dot
                if ((board[i] & 7) == NONE)
                    continue;

                // Prevent selecting opponents pieces
                if (((board[i] >> 3) & 1) == is_whites_turn)
                    continue;

                // If tile is already selected, deselect
                if (board[i] >> 7) {
                    board[i] &= 127;
                    removeDots(board);
                    if (drawBoard(board, renderer, textures, chessboard, &tile) != 0)
                        break;
                    continue;
                }

                // Tile must be unselected and not a dot then
                // Unselect any others first
                for (j = 0; j < 64; j++)
                    board[j] &= 0x3f;

                // Select piece
                board[i] |= 128;

                // Calculate where dots should go
                calculateMoves(board, i);

                // Verify dots don't put king in danger
                for (j = 0; j < 64; j++) {
                    if (((board[j] >> 6) & 1) &&
                        (!verifyMove(board, j, is_whites_turn)))
                        board[j] &= 0xbf;
                }

                if (drawBoard(board, renderer, textures, chessboard, &tile) != 0)
                    break;
            }
	    }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}
