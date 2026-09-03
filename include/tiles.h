/*
 * xrick/include/tiles.h
 *
 * Copyright (C) 1998-2002 BigOrno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

/*
 * NOTES
 *
 * A tile consists in one column and 8 rows of 8 U16 (cga encoding, two
 * bits per pixel). The tl_tiles array contains all tiles, with the
 * following structure:
 *
 *  0x0000 - 0x00FF  tiles for main intro
 *  0x0100 - 0x01FF  tiles for map intro
 *  0x0200 - 0x0327  unused
 *  0x0328 - 0x0427  game tiles, page 0
 *  0x0428 - 0x0527  game tiles, page 1
 *  0x0527 - 0x05FF  unused
 */

#ifndef _TILES_H
#define _TILES_H

#include "system.h"

#ifdef GFXPC
#define TILES_NBR_BANKS 5
#endif
#ifdef GFXST
#define TILES_NBR_BANKS 5
#endif

/*
 * RUxF unified tile model:
 *   bank 0        -- font / cutscene decor (indices < 256, used by all
 *                    UI/screenshot rendering; a U8 tile-list index).
 *   banks 1..4    -- four game-tile pages (1024 tiles total). A block cell
 *                    holds an ABSOLUTE tile index `u` in 0-1023; the real
 *                    bank and in-bank offset are derived at draw time with
 *                    game_bank(u) = 1 + (u >> 8), game_off(u) = u & 0xFF.
 *                    There is NO active-tile-page indirection anymore.
 */
#define TILES_BANK_FONT 0
#define TILES_BANK_GAME 1   /* base bank for the absolute-index mapping */
#define TILES_GAME_BANKS 4

#define TILES_SIZEOF8 (0x10)
#define TILES_SIZEOF16 (0x08)

/*
 * three special tile numbers
 */
#define TILES_BULLET 0x01
#define TILES_BOMB 0x02
#define TILES_RICK 0x03

/*
 * one single tile
 */
#ifdef GFXPC
typedef U16 tile_t[TILES_SIZEOF16];
#endif
#ifdef GFXST
typedef U32 tile_t[0x08];
#endif

/*
 * tiles banks (each bank is 0x100 tiles)
 */
extern tile_t tiles_data[TILES_NBR_BANKS][0x100];

#endif

/* eof */
