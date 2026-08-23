/*
 * xrick/src/draw.c
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
 * This is the only file which accesses the video. Anything calling d_*
 * function should be video-independant.
 *
 * draw.c draws into a 320x200 or 0x0140x0xc8 8-bits depth frame buffer,
 * using the CGA 2 bits color codes. It is up to the video to figure out
 * how to display the frame buffer. Whatever draw.c does, does not show
 * until the screen is explicitely refreshed.
 *
 * The "screen" is the whole 0x0140 by 0x00c8 screen, coordinates go from
 * 0x0000,0x0000 to 0x013f,0x00c7.
 *
 * The "map" is a 0x0100 by 0x0140 rectangle that represents the active
 * game area.
 *
 * Relative to the screen, the "map" is located at 0x0020,-0x0040 : the
 * "map" is composed of two hidden 0x0100 by 0x0040 rectangles (one at the
 * top and one at the bottom) and one visible 0x0100 by 0x00c0 rectangle (in
 * the middle).
 *
 * The "map screen" is the visible rectangle ; it is a 0x0100 by 0xc0
 * rectangle located at 0x0020,0x00.
 *
 * Coordinates can be relative to the screen, the map, or the map screen.
 *
 * Coordinates can be expressed in pixels. When relative to the map or the
 * map screen, they can also be expressed in tiles, the map being composed
 * of rows of 0x20 tiles of 0x08 by 0x08 pixels.
 */

#include <string.h>

#include "system.h"
#include "game.h"
#include "draw.h"

#include "sysvid.h"
#include "sprites.h"
#include "tiles.h"
#include "maps.h"
#include "rects.h"
#include "img.h"


/*
 * counters positions (pixels, screen)
 */
#ifdef GFXPC
#define DRAW_STATUS_SCORE_X 0x28
#define DRAW_STATUS_LIVES_X 0xE8
#define DRAW_STATUS_Y 0x08
#endif
#define DRAW_STATUS_BULLETS_X 0x68
#define DRAW_STATUS_BOMBS_X 0xA8
#ifdef GFXST
#define DRAW_STATUS_SCORE_X 0x20
#define DRAW_STATUS_LIVES_X 0xF0
#define DRAW_STATUS_Y 0
#endif


/*
 * public vars
 */
U8 *draw_tllst;    /* pointer to tiles list */
#ifdef GFXPC
U16 draw_filter;   /* CGA colors filter */
#endif
U8 draw_tilesBank; /* tile number offset */
rect_t draw_STATUSRECT = {
  DRAW_STATUS_SCORE_X, DRAW_STATUS_Y,
  DRAW_STATUS_LIVES_X + 6 * 8 - DRAW_STATUS_SCORE_X, 8,
  NULL
};
rect_t draw_SCREENRECT = { 0, 0, SYSVID_WIDTH, SYSVID_HEIGHT, NULL };


/*
 * private vars
 */
static U8 *fb;     /* frame buffer pointer */
static U8 *fb_base = NULL;  /* frame buffer base (redirectable for offscreen rendering) */

/* offscreen buffer holding map rows 7..32 (visible + hidden top & bottom),
 * used to compose smoothly scrolled frames. 26 rows * 8 pixels tall. */
#define DRAW_MAPBUF_ROWS 26
static U8 draw_mapbuf[DRAW_MAPBUF_ROWS * 8][SYSVID_WIDTH];

/* screen y range covered by the map background */
#ifdef GFXST
#define DRAW_MAPSCR_TOP 8
#define DRAW_MAPSCR_BOT SYSVID_HEIGHT
#else
#define DRAW_MAPSCR_TOP 0
#define DRAW_MAPSCR_BOT (SYSVID_HEIGHT - 8)
#endif


/*
 * Set the frame buffer pointer
 *
 * x, y: position (pixels, screen)
 */
void
draw_setfb(U16 x, U16 y)
{
  if (!fb_base)
    fb_base = sysvid_fb;
  fb = fb_base + x + y * SYSVID_WIDTH;
}


/*
 * Clip to map screen
 *
 * x, y: position (pixels, map) CHANGED clipped
 * width, height: dimension CHANGED clipped
 * return: TRUE if fully clipped, FALSE if still (at least partly) visible
 */
U8
draw_clipms(S16 *x, S16 *y, U16 *width, U16 *height)
{
  if (*x < 0) {
    if (*x + *width < 0)
      return TRUE;
    else {
      *width += *x;
      *x = 0;
    }
  }
  else {
    if (*x > 0x0100)
      return TRUE;
    else if (*x + *width > 0x0100) {
      *width = 0x0100 - *x;
    }
  }

  if (*y < DRAW_XYMAP_SCRTOP) {
    if ((*y + *height) < DRAW_XYMAP_SCRTOP)
      return TRUE;
    else {
      *height += *y - DRAW_XYMAP_SCRTOP;
      *y = DRAW_XYMAP_SCRTOP;
    }
  }
  else {
    if (*y >= DRAW_XYMAP_HBTOP)
      return TRUE;
    else if (*y + *height > DRAW_XYMAP_HBTOP)
      *height = DRAW_XYMAP_HBTOP - *y;
  }

  return FALSE;
}


/*
 * Draw a list of tiles onto the frame buffer
 * start at position indicated by fb ; at the end of each (sub)list,
 * perform a "carriage return + line feed" i.e. go back to the initial
 * position then go down one tile row (8 pixels)
 *
 * ASM 1e33
 * fb: CHANGED (see above)
 * draw_tllst: CHANGED points to the element following 0xfe/0xff end code
 */
void
draw_tilesList(void)
{
  U8 *t;

  t = fb;
  while (draw_tilesSubList() != 0xFE) {  /* draw sub-list */
    t += 8 * SYSVID_WIDTH;  /* go down one tile i.e. 8 lines */
    fb = t;
  }
}


/*
 * Draw a list of tiles onto the frame buffer -- same as draw_tilesList,
 * but accept an immediate string as parameter. Note that the string needs
 * to be properly terminated with 0xfe (\376) and 0xff (\377) chars.
 */
void
draw_tilesListImm(U8 *list)
{
  draw_tllst = list;
  draw_tilesList();
}


/*
 * Draw a sub-list of tiles onto the frame buffer
 * start at position indicated by fb ; leave fb pointing to the next
 * tile to the right of the last tile drawn
 *
 * ASM 1e41
 * fpb: CHANGED (see above)
 * draw_tllst: CHANGED points to the element following 0xfe/0xff end code
 * returns: end code (0xfe : end of list ; 0xff : end of sub-list)
 */
U8
draw_tilesSubList()
{
  U8 i;

  i = *(draw_tllst++);
  while (i != 0xFF && i != 0xFE) {  /* while not end */
    draw_tile(i);  /* draw tile */
    i = *(draw_tllst++);
  }
  return i;
}


/*
 * Draw a tile
 * at position indicated by fb ; leave fb pointing to the next tile
 * to the right of the tile drawn
 *
 * ASM 1e6c
 * tlnbr: tile number
 * draw_filter: CGA colors filter
 * fb: CHANGED (see above)
 */
void
draw_tile(U8 tileNumber)
{
  U8 i, k, *f;

#ifdef GFXPC
  U16 x;
#endif

#ifdef GFXST
  U32 x;
#endif

  f = fb;  /* frame buffer */
  for (i = 0; i < 8; i++) {  /* for all 8 pixel lines */

#ifdef GFXPC
    x = tiles_data[draw_tilesBank][tileNumber][i] & draw_filter;
    /*
     * tiles / perform the transformation from CGA 2 bits
     * per pixel to frame buffer 8 bits per pixels
     */
    for (k = 8; k--; x >>= 2)
      f[k] = x & 3;
    f += SYSVID_WIDTH;  /* next line */
#endif

#ifdef GFXST
  x = tiles_data[draw_tilesBank][tileNumber][i];
  /*
   * tiles / perform the transformation from ST 4 bits
   * per pixel to frame buffer 8 bits per pixels
   */
  for (k = 8; k--; x >>= 4)
    f[k] = x & 0x0F;
  f += SYSVID_WIDTH;  /* next line */
#endif

  }

  fb += 8;  /* next tile */
}

/*
 * Draw a sprite
 *
 * ASM 1a09
 * nbr: sprite number
 * x, y: sprite position (pixels, screen)
 * fb: CHANGED
 */
#ifdef GFXPC
void
draw_sprite(U8 nbr, U16 x, U16 y)
{
  U8 i, j, k, *f;
  U16 xm = 0, xp = 0;

  draw_setfb(x, y);

  for (i = 0; i < 4; i++) {  /* for each tile column */
    f = fb;  /* frame buffer */
    for (j = 0; j < 0x15; j++) {  /* for each pixel row */
      xm = sprites_data[nbr][i][j].mask;  /* mask */
      xp = sprites_data[nbr][i][j].pict;  /* picture */
      /*
       * sprites / perform the transformation from CGA 2 bits
       * per pixel to frame buffer 8 bits per pixels
       */
      for (k = 8; k--; xm >>= 2, xp >>= 2)
	f[k] = (f[k] & (xm & 3)) | (xp & 3);
      f += SYSVID_WIDTH;
    }
    fb += 8;
  }
}
#endif


/*
 * Draw a sprite
 *
 * foobar
 */
#ifdef GFXST
void
draw_sprite(U8 number, U16 x, U16 y)
{
  U8 i, j, k, *f;
  U16 g;
  U32 d;

  draw_setfb(x, y);
  g = 0;
  for (i = 0; i < 0x15; i++) { /* rows */
    f = fb;
    for (j = 0; j < 4; j++) { /* cols */
      d = sprites_data[number][g++];
      for (k = 8; k--; d >>= 4)
	if (d & 0x0F) f[k] = (f[k] & 0xF0) | (d & 0x0F);
      f += 8;
    }
    fb += SYSVID_WIDTH;
  }
}
#endif


/*
 * Draw a sprite
 *
 * NOTE re-using original ST graphics format
 */
#ifdef GFXST
static U16
draw_tilecol(S16 v)
{
  if (v < 0)
    return 0;
  if (v > 0xff)
    return 0x1f;
  return ((U16)v) >> 3;
}

void
draw_sprite2(U8 number, U16 x, U16 y, U8 front)
{
  U32 d = 0;   /* sprite data */
  S16 x0, y0;  /* clipped x, y */
  U16 w, h;    /* width, height */
  S16 g,       /* sprite data offset*/
    r, c,      /* row, column */
    i,         /* frame buffer shifter */
    im;        /* tile flag shifter */
  U8 flg;      /* tile flag */

  x0 = x;
  y0 = y;
  w = 0x20;
  h = 0x15;

  if (draw_clipms(&x0, &y0, &w, &h))  /* return if not visible */
    return;

  g = 0;
  draw_setfb(x0 - DRAW_XYMAP_SCRLEFT, y0 - DRAW_XYMAP_SCRTOP + 8);

  for (r = 0; r < 0x15; r++) {
    if (r >= h || y + r < y0) continue;

    i = 0x1f;
    im = x - (x & 0xfff8);
    flg = map_eflg[map_map[(y + r) >> 3][draw_tilecol((S16)x + 0x1f)]];

#ifdef ENABLE_CHEATS
#define LOOP(N, C0, C1) \
    d = sprites_data[number][g + N]; \
    for (c = C0; c >= C1; c--, i--, d >>= 4, im--) { \
      if (im == 0) { \
	flg = map_eflg[map_map[(y + r) >> 3][draw_tilecol((S16)x + c)]]; \
	im = 8; \
      } \
      if (c >= w || x + c < x0) continue; \
      if (!front && !game_cheat3 && (flg & MAP_EFLG_FGND)) continue; \
      if (d & 0x0F) fb[i] = (fb[i] & 0xF0) | (d & 0x0F); \
      if (game_cheat3) fb[i] |= 0x10; \
    }
#else
#define LOOP(N, C0, C1) \
    d = sprites_data[number][g + N]; \
    for (c = C0; c >= C1; c--, i--, d >>= 4, im--) { \
      if (im == 0) { \
	flg = map_eflg[map_map[(y + r) >> 3][draw_tilecol((S16)x + c)]]; \
	im = 8; \
      } \
      if (!front && (flg & MAP_EFLG_FGND)) continue; \
      if (c >= w || x + c < x0) continue; \
      if (d & 0x0F) fb[i] = (fb[i] & 0xF0) | (d & 0x0F); \
    }
#endif
    LOOP(3, 0x1f, 0x18);
    LOOP(2, 0x17, 0x10);
    LOOP(1, 0x0f, 0x08);
    LOOP(0, 0x07, 0x00);

#undef LOOP

    fb += SYSVID_WIDTH;
    g += 4;
  }
}

#endif


/*
 * Draw a sprite
 * align to tile column, determine plane automatically, and clip
 *
 * nbr: sprite number
 * x, y: sprite position (pixels, map).
 * fb: CHANGED
 */
#ifdef GFXPC
void
draw_sprite2(U8 number, U16 x, U16 y, U8 front)
{
  U8 k, *f, c, r, dx;
  U16 cmax, rmax;
  U16 xm = 0, xp = 0;
  S16 xmap, ymap;

  /* align to tile column, prepare map coordinate and clip */
  xmap = x & 0xFFF8;
  ymap = y;
  cmax = 0x20;  /* width, 4 tile columns, 8 pixels each */
  rmax = 0x15;  /* height, 15 pixels */
  dx = (x - xmap) * 2;
  if (draw_clipms(&xmap, &ymap, &cmax, &rmax))  /* return if not visible */
    return;

  /* get back to screen */
  draw_setfb(xmap - DRAW_XYMAP_SCRLEFT, ymap - DRAW_XYMAP_SCRTOP);
  xmap >>= 3;
  cmax >>= 3;

  /* draw */
  for (c = 0; c < cmax; c++) {  /* for each tile column */
    f = fb;
    for (r = 0; r < rmax; r++) {  /* for each pixel row */
      /* check that tile is not hidden behind foreground */
#ifdef ENABLE_CHEATS
      if (front || game_cheat3 ||
	  !(map_eflg[map_map[(ymap + r) >> 3][xmap + c]] & MAP_EFLG_FGND)) {
#else
      if (front ||
	  !(map_eflg[map_map[(ymap + r) >> 3][xmap + c]] & MAP_EFLG_FGND)) {
#endif
	xp = xm = 0;
	if (c > 0) {
	  xm |= sprites_data[number][c - 1][r].mask << (16 - dx);
	  xp |= sprites_data[number][c - 1][r].pict << (16 - dx);
	}
	else
	  xm |= 0xFFFF << (16 - dx);
	if (c < cmax) {
	  xm |= sprites_data[number][c][r].mask >> dx;
	  xp |= sprites_data[number][c][r].pict >> dx;
	}
	else
	  xm |= 0xFFFF >> dx;
	/*
	 * sprites / perform the transformation from CGA 2 bits
	 * per pixel to frame buffer 8 bits per pixels
	 */
	for (k = 8; k--; xm >>= 2, xp >>= 2) {
	  f[k] = ((f[k] & (xm & 3)) | (xp & 3));
#ifdef ENABLE_CHEATS
	  if (game_cheat3) f[k] |= 4;
#endif
	}
      }
      f += SYSVID_WIDTH;
    }
    fb += 8;
  }
}
#endif


/*
 * Redraw the map behind a sprite
 * align to tile column and row, and clip
 *
 * x, y: sprite position (pixels, map).
 */
void
draw_spriteBackground(U16 x, U16 y)
{
  U8 r, c;
  U16 rmax, cmax;
  S16 xmap, ymap;
  U16 xs, ys;

  /* aligne to column and row, prepare map coordinate, and clip */
  xmap = x & 0xFFF8;
  ymap = y & 0xFFF8;
  cmax = (x - xmap == 0 ? 0x20 : 0x28);  /* width, 4 tl cols, 8 pix each */
  rmax = (y & 0x04) ? 0x20 : 0x18;  /* height, 3 or 4 tile rows */
  if (draw_clipms(&xmap, &ymap, &cmax, &rmax))  /* don't draw if fully clipped */
    return;

  /* get back to screen */
  xs = xmap - DRAW_XYMAP_SCRLEFT;
  ys = ymap - DRAW_XYMAP_SCRTOP;
  xmap >>= 3;
  ymap >>= 3;
  cmax >>= 3;
  rmax >>= 3;

  /* draw */
  for (r = 0; r < rmax; r++) {  /* for each row */
#ifdef GFXPC
    draw_setfb(xs, ys + r * 8);
#endif
#ifdef GFXST
    draw_setfb(xs, 8 + ys + r * 8);
#endif
    for (c = 0; c < cmax; c++) {  /* for each column */
      draw_tile(map_map[ymap + r][xmap + c]);
    }
  }
}


/*
 * Draw entire map screen background tiles onto frame buffer.
 *
 * ASM 0af5, 0a54
 */
void
draw_map(void)
{
  U8 i, j;

  draw_tilesBank = map_tilesBank;

  for (i = 0; i < 0x18; i++) {  /* 0x18 rows */
#ifdef GFXPC
    draw_setfb(0x20, (i * 8));
#endif
#ifdef GFXST
    draw_setfb(0x20, 8 + (i * 8));
#endif
    for (j = 0; j < 0x20; j++)  /* 0x20 tiles per row */
      draw_tile(map_map[i + 8][j]);
  }
}


/*
 * Pre-render map rows 7..32 (visible screen rows plus the hidden top
 * and bottom rows) into the offscreen buffer, so that smoothly
 * scrolled frames can be composed by draw_mapCompose().
 */
void
draw_mapBufFill(void)
{
  U8 i, j;
  U8 *save;

  draw_tilesBank = map_tilesBank;

  save = fb_base;
  fb_base = (U8 *)draw_mapbuf;

  for (i = 0; i < DRAW_MAPBUF_ROWS; i++) {
    draw_setfb(0x20, i * 8);
    for (j = 0; j < 0x20; j++)
      draw_tile(map_map[i + MAP_ROW_HTTOP + 7][j]);
  }

  fb_base = save;
}

/*
 * Compose the map background onto the frame buffer, shifted vertically
 * by off pixels (-8..+8). Rows are taken from the offscreen buffer,
 * which holds one extra hidden row on each side of the visible area.
 *
 * This allows sub-tick smooth scrolling: the game logic still shifts
 * the map by whole 8-pixel steps, but intermediate frames can show
 * in-between positions.
 */
void
draw_mapCompose(S16 off)
{
  S16 br, y, y0, y1;

  if (!fb_base)
    fb_base = sysvid_fb;

  if (off > 8) off = 8;
  if (off < -8) off = -8;

  for (br = 0; br < DRAW_MAPBUF_ROWS; br++) {
    /* natural position of buffer row br on screen (br==1 -> map top) */
    y = DRAW_MAPSCR_TOP + (br - 1) * 8 + off;
    if (y + 8 <= DRAW_MAPSCR_TOP || y >= DRAW_MAPSCR_BOT)
      continue;
    y0 = (y < DRAW_MAPSCR_TOP) ? DRAW_MAPSCR_TOP : y;
    y1 = (y + 8 > DRAW_MAPSCR_BOT) ? DRAW_MAPSCR_BOT : y + 8;
    memcpy(fb_base + y0 * SYSVID_WIDTH,
	   (U8 *)draw_mapbuf + br * 8 * SYSVID_WIDTH + (y0 - y) * SYSVID_WIDTH,
	   (U32)(y1 - y0) * SYSVID_WIDTH);
  }
}


/*
 * Draw status indicators
 *
 * ASM 0309
 */
void
draw_drawStatus(void)
{
  S8 i;
  U32 sv;
  static U8 s[7] = {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xfe};

  draw_tilesBank = 0;

  for (i = 5, sv = game_score; i >= 0; i--) {
    s[i] = 0x30 + (U8)(sv % 10);
    sv /= 10;
  }
  draw_tllst = s;

  draw_setfb(DRAW_STATUS_SCORE_X, DRAW_STATUS_Y);
  draw_tilesList();

  draw_setfb(DRAW_STATUS_BULLETS_X, DRAW_STATUS_Y);
  for (i = 0; i < game_bullets; i++)
    draw_tile(TILES_BULLET);

  draw_setfb(DRAW_STATUS_BOMBS_X, DRAW_STATUS_Y);
  for (i = 0; i < game_bombs; i++)
    draw_tile(TILES_BOMB);

  draw_setfb(DRAW_STATUS_LIVES_X, DRAW_STATUS_Y);
  for (i = 0; i < game_lives; i++)
    draw_tile(TILES_RICK);
}


/*
 * Draw info indicators
 */
#ifdef ENABLE_CHEATS
void
draw_infos(void)
{
  draw_tilesBank = 0;

#ifdef GFXPC
  draw_filter = 0xffff;
#endif

  draw_setfb(0x00, DRAW_STATUS_Y);
  draw_tile(game_cheat1 ? 'T' : '@');
  draw_setfb(0x08, DRAW_STATUS_Y);
  draw_tile(game_cheat2 ? 'N' : '@');
  draw_setfb(0x10, DRAW_STATUS_Y);
  draw_tile(game_cheat3 ? 'V' : '@');
}
#endif


/*
 * Clear status indicators
 */
void
draw_clearStatus(void)
{
  U8 i;

#ifdef GFXPC
  draw_tilesBank = map_tilesBank;
#endif
#ifdef GFXST
  draw_tilesBank = 0;
#endif
  draw_setfb(DRAW_STATUS_SCORE_X, DRAW_STATUS_Y);
  for (i = 0; i < DRAW_STATUS_LIVES_X/8 + 6 - DRAW_STATUS_SCORE_X/8; i++) {
#ifdef GFXPC
    draw_tile(map_map[MAP_ROW_SCRTOP + (DRAW_STATUS_Y / 8)][i]);
#endif
#ifdef GFXST
    draw_tile('@');
#endif
  }
}

/*
 * Draw a picture
 */
#ifdef GFXST
void
draw_pic(U16 x, U16 y, U16 w, U16 h, U32 *pic)
{
  U8 *f;
  U16 i, j, k, pp;
  U32 v;

  draw_setfb(x, y);
  pp = 0;

  for (i = 0; i < h; i++) { /* rows */
    f = fb;
    for (j = 0; j < w; j += 8) {  /* cols */
      v = pic[pp++];
      for (k = 8; k--; v >>=4)
	f[k] = v & 0x0F;
      f += 8;
    }
    fb += SYSVID_WIDTH;
  }
}
#endif


/*
 * Draw a bitmap
 */
void
draw_img(img_t *i)
{
  U16 k;

  draw_setfb(0, 0);
  if (i->ncolors > 0)
    sysvid_setPalette(i->colors, i->ncolors);
  for (k = 0; k < SYSVID_WIDTH * SYSVID_HEIGHT; k++)
    fb[k] = i->pixels[k];
}


/* eof */
