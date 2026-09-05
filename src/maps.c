/*
 * xrick/src/maps.c
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
 * A map is composed of submaps, which in turn are composed of rows of
 * 0x20 tiles. map_map contains the tiles for the current portion of the
 * current submap, i.e. a little bit more than what appear on the screen,
 * but not the whole submap.
 *
 * map_frow is map_map top row within the submap.
 *
 * Submaps are stored as arrays of blocks, each block being a 4x4 tile
 * array. map_submaps[].bnum points to the first block of the array.
 *
 * Before a submap can be played, it needs to be expanded from blocks
 * to map_map.
 */

#include "system.h"
#include "game.h"
#include "maps.h"
#include "debug.h"

#include "ents.h"
#include "e_rick.h"
#include "draw.h"
#include "screens.h"
#include "e_sbonus.h"

/*
 * global vars
 */
U16 map_map[0x2C][0x20];
U16 map_frow;


/*
 * Fill in map_map with ABSOLUTE tile numbers by expanding blocks.
 *
 * add map_submaps[].bnum to map_frow to find out where to start from.
 * We need to /4 map_frow to convert from tile rows to block rows, then
 * we need to *8 to convert from block rows to block numbers (there
 * are 8 blocks per block row). This is achieved by *2 then &0xfff8.
 *
 * RUxF: map_bnums[pbnum] is a U16 BLOCK number (0-1023) into the unified
 * block space; each of its 16 cells is an absolute tile index 0-1023.
 */
void
map_expand(void)
{
  U8 i, j, k, l;
  U8 row, col;
  U16 pbnum;

  pbnum = map_submaps[game_submap].bnum + ((2 * map_frow) & 0xfff8);
  row = col = 0;

  for (i = 0; i < 0x0b; i++) {  /* 0x0b rows of blocks */
    for (j = 0; j < 0x08; j++) {  /* 0x08 blocks per row */
      for (k = 0, l = 0; k < 0x04; k++) {  /* expand one block */
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col++] = map_blocks[map_bnums[pbnum]][l++];
	map_map[row][col]   = map_blocks[map_bnums[pbnum]][l++];
	row += 1; col -= 3;
      }
      row -= 4; col += 4;
      pbnum++;
    }
    row += 4; col = 0;
  }
}


/*
 * Initialize a new submap
 *
 * ASM 0cc3
 */
void
map_init(void)
{
  /*sys_printf("xrick/map_init: map=%#04x submap=%#04x\n", g_map, game_submap);*/
#ifdef GFXPC
  draw_filter = 0xffff;
#endif
  map_expand();
  ent_reset();
  ent_actvis(map_frow + MAP_ROW_SCRTOP, map_frow + MAP_ROW_SCRBOT);
  ent_actvis(map_frow + MAP_ROW_HTTOP, map_frow + MAP_ROW_HTBOT);
  ent_actvis(map_frow + MAP_ROW_HBTOP, map_frow + MAP_ROW_HBBOT);
}


/*
 * Chain (sub)maps
 *
 * ASM 0c08
 * return: TRUE/next submap OK, FALSE/map finished
 */
U8
map_chain(void)
{
  U16 c;

  game_chsm = 0;
  e_sbonus_counting = FALSE;

  /* find connection */
  c = map_submaps[game_submap].connect;

  IFDEBUG_MAPS(
    sys_printf("xrick/maps: chain submap=%#04x frow=%#04x .connect=%#04x %s\n",
	       game_submap, map_frow, c,
	       (game_dir == LEFT ? "-> left" : "-> right"));
  );

  /*
   * look for the connector matching the exit direction, choosing by Rick's
   * current tile row (bracketing): pick the first connector (of the matching
   * direction) whose rowout (ABSOLUTE tile row on this submap) is >= Rick's
   * current absolute tile row. If all of them are above Rick, fall back to
   * the last one. This lets a single submap have several exits on the same
   * edge, one per height. If no connector of that direction exists, panic.
   *
   * map_frow is RELATIVE to this submap (map_expand adds map_submaps[].bnum
   * to it), so Rick's absolute tile row is:
   *     submap.bnum/2 + map_frow + (E_RICK_ENT.y >> 3)
   */
  {
    U16 first = 0xffff, last = 0xffff;
    U16 rickAbs = (map_submaps[game_submap].bnum >> 1) + map_frow + (E_RICK_ENT.y >> 3);
    for (c = map_submaps[game_submap].connect; ; c++) {
      if (map_connect[c].dir == 0xff) break;
      if (map_connect[c].dir != game_dir) continue;
      last = c;
      if (first == 0xffff) first = c;
      if (map_connect[c].rowout >= rickAbs) break;
    }
    if (first == 0xffff)
      sys_panic("(map_chain) can not find connector\n");
    c = (map_connect[c].dir == 0xff) ? last : c;
  }

  /* got it */
  IFDEBUG_MAPS(
    sys_printf("xrick/maps: chain frow=%#04x y=%#06x\n",
	       map_frow, ent_ents[1].y);
    sys_printf("xrick/maps: chain connect=%#04x - ",
	       c);
    );

  if (map_connect[c].submap == 0xff) {
    /* no next submap - request next map */
    IFDEBUG_MAPS(
      sys_printf("chain to next map\n");
      );
    return FALSE;
  }
  else  {
    /* next submap: teleport to the exact arrival (row,col) on the
     * destination submap. Keep E_RICK_ENT.y as close as possible so a
     * mid-jump keeps its height.
     *
     * rowin is an ABSOLUTE tile row on the destination, but map_frow is
     * RELATIVE: map_expand reads from submap.bnum + map_frow, and the
     * destination submap's own data starts at submap.bnum/2 (tile rows).
     * So we subtract the destination's base (bnum/2) to make Rick land at
     * exactly `rowin` absolute:
     *     map_frow = rowin - (E_RICK_ENT.y >> 3) - dest.bnum/2
     *
     * map_expand only honors map_frow values that are a multiple of 4
     * ((2 * map_frow) & 0xfff8 rounds down to a block row), while entities
     * are positioned using the exact value. A non-multiple-of-4 map_frow
     * would therefore draw entities up to 3 tile rows higher than the map.
     * So round map_frow DOWN to a multiple of 4 and push Rick down by the
     * same amount (a << 3 pixels): he still lands at exactly `rowin`.
     */
    {
      U16 destBase = map_submaps[map_connect[c].submap].bnum >> 1;
      U16 raw = map_connect[c].rowin - (E_RICK_ENT.y >> 3) - destBase;
      U16 a = raw & 3;
      IFDEBUG_MAPS(
	sys_printf("chain to submap=%#04x rowin=%#04x base=%#04x\n",
		   map_connect[c].submap, map_connect[c].rowin, destBase);
	);
      game_submap = map_connect[c].submap;
      E_RICK_ENT.x = (U16)map_connect[c].colin * 8;
      E_RICK_ENT.y += a << 3;
      map_frow = raw - a;

      IFDEBUG_MAPS(
	sys_printf("xrick/maps: chain frow=%#04x\n",
		   map_frow);
	);
      return TRUE;
    }
  }
}


/*
 * Reset all marks, i.e. make them all active again.
 *
 * ASM 0025
 *
 */
void
map_resetMarks(void)
{
  U16 i;
  for (i = 0; i < MAP_NBR_MARKS; i++)
    map_marks[i].ent &= ~MAP_MARK_NACT;
}


/* eof */
