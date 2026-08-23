/*
 * xrick/src/sysvid.c
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

#include <stdlib.h> /* malloc */

#include <SDL.h>

#include "system.h"
#include "game.h"
#include "img.h"
#include "debug.h"

#ifdef __MSVC__
#include <memory.h> /* memset */
#endif

U8 *sysvid_fb; /* frame buffer */
rect_t SCREENRECT = {0, 0, SYSVID_WIDTH, SYSVID_HEIGHT, NULL}; /* whole fb */

static SDL_Color palette[256];
static SDL_Surface *screen;
static U32 videoFlags;

static U8 zoom = SYSVID_ZOOM; /* actual zoom level (windowed) */
static U8 szoom = 0;  /* saved zoom level */
static U16 fsw = 0;  /* fullscreen surface width == desktop width */
static U16 fsh = 0;  /* fullscreen surface height == desktop height */
static U16 *xtab = NULL;  /* fullscreen dest column -> fb column */
static U16 *ytab = NULL;  /* fullscreen dest row -> fb row */

#include "img_icon.e"

/*
 * color tables
 */

#ifdef GFXPC
static U8 RED[] = { 0x00, 0x50, 0xf0, 0xf0, 0x00, 0x50, 0xf0, 0xf0 };
static U8 GREEN[] = { 0x00, 0xf8, 0x50, 0xf8, 0x00, 0xf8, 0x50, 0xf8 };
static U8 BLUE[] = { 0x00, 0x50, 0x50, 0x50, 0x00, 0xf8, 0xf8, 0xf8 };
#endif
#ifdef GFXST
static U8 RED[] = { 0x00, 0xd8, 0xb0, 0xf8,
                    0x20, 0x00, 0x00, 0x20,
                    0x48, 0x48, 0x90, 0xd8,
                    0x48, 0x68, 0x90, 0xb0,
                    /* cheat colors */
                    0x50, 0xe0, 0xc8, 0xf8,
                    0x68, 0x50, 0x50, 0x68,
                    0x80, 0x80, 0xb0, 0xe0,
                    0x80, 0x98, 0xb0, 0xc8
};
static U8 GREEN[] = { 0x00, 0x00, 0x6c, 0x90,
                      0x24, 0x48, 0x6c, 0x48,
                      0x6c, 0x24, 0x48, 0x6c,
                      0x48, 0x6c, 0x90, 0xb4,
		      /* cheat colors */
                      0x54, 0x54, 0x9c, 0xb4,
                      0x6c, 0x84, 0x9c, 0x84,
                      0x9c, 0x6c, 0x84, 0x9c,
                      0x84, 0x9c, 0xb4, 0xcc
};
static U8 BLUE[] = { 0x00, 0x00, 0x68, 0x68,
                     0x20, 0xb0, 0xd8, 0x00,
                     0x20, 0x00, 0x00, 0x00,
                     0x48, 0x68, 0x90, 0xb0,
		     /* cheat colors */
                     0x50, 0x50, 0x98, 0x98,
                     0x68, 0xc8, 0xe0, 0x50,
                     0x68, 0x50, 0x50, 0x50,
                     0x80, 0x98, 0xb0, 0xc8};
#endif

/*
 * Initialize screen
 */
static
SDL_Surface *initScreen(U16 w, U16 h, U16 bpp, U32 flags)
{
  return SDL_SetVideoMode(w, h, bpp, flags);
}

#include "sysvid_crt.e"

void
sysvid_setPalette(img_color_t *pal, U16 n)
{
  U16 i;

  for (i = 0; i < n; i++) {
    palette[i].r = pal[i].r;
    palette[i].g = pal[i].g;
    palette[i].b = pal[i].b;
    crt_palrgb[i * 3] = pal[i].r;
    crt_palrgb[i * 3 + 1] = pal[i].g;
    crt_palrgb[i * 3 + 2] = pal[i].b;
  }
  if (!crt_on)
    SDL_SetColors(screen, (SDL_Color *)&palette, 0, n);
}

void
sysvid_restorePalette(void)
{
  U16 i;

  for (i = 0; i < 256; i++) {
    crt_palrgb[i * 3] = palette[i].r;
    crt_palrgb[i * 3 + 1] = palette[i].g;
    crt_palrgb[i * 3 + 2] = palette[i].b;
  }
  if (!crt_on)
    SDL_SetColors(screen, (SDL_Color *)&palette, 0, 256);
}

void
sysvid_setGamePalette()
{
  U8 i;
  img_color_t pal[256];

  for (i = 0; i < 32; ++i) {
    pal[i].r = RED[i];
    pal[i].g = GREEN[i];
    pal[i].b = BLUE[i];
  }
  sysvid_setPalette(pal, 32);
}

/*
 * Detect desktop resolution for fullscreen.
 * We run fullscreen at the native desktop resolution and do our own
 * scaling, so that no GPU/compositor bilinear filtering is involved.
 */
void
sysvid_chkvm(void)
{
  const SDL_VideoInfo *info;
  SDL_Rect **modes;
  U8 i;

  IFDEBUG_VIDEO(sys_printf("xrick/video: checking video modes\n"););

  fsw = fsh = 0;

  info = SDL_GetVideoInfo();
  if (info && info->current_w > 0 && info->current_h > 0) {
    fsw = info->current_w;
    fsh = info->current_h;
  }

  if (!fsw || !fsh) {
    /* fallback: use largest available fullscreen mode */
    modes = SDL_ListModes(NULL, videoFlags|SDL_FULLSCREEN);
    if (modes != (SDL_Rect **)0 && modes != (SDL_Rect **)-1 && modes[0]) {
      IFDEBUG_VIDEO(sys_printf("xrick/video: SDL says, use these modes:\n"););
      for (i = 0; modes[i]; i++) {
        IFDEBUG_VIDEO(sys_printf("  %dx%d\n", modes[i]->w, modes[i]->h););
        if (modes[i]->w >= fsw && modes[i]->h >= fsh) {
          fsw = modes[i]->w;
          fsh = modes[i]->h;
        }
      }
    }
  }

  if (!fsw || !fsh) {
    IFDEBUG_VIDEO(
      sys_printf("xrick/video: can not detect desktop resolution\n");
      );
    sys_panic("xrick/video: can not detect desktop resolution\n");
  }

  IFDEBUG_VIDEO(
    sys_printf("xrick/video: desktop resolution is %dx%d\n", fsw, fsh);
    );
}

/*
 * Build dest->src mapping tables for nearest-neighbor stretch.
 * Each destination pixel simply replicates one source pixel:
 * crisp pixels, no filtering of any kind.
 */
static void
sysvid_buildStretch(void)
{
  U32 i;

  free(xtab);
  free(ytab);
  xtab = malloc(fsw * sizeof(U16));
  ytab = malloc(fsh * sizeof(U16));
  if (!xtab || !ytab)
    sys_panic("xrick/video: stretch tables malloc failed\n");

  for (i = 0; i < fsw; i++)
    xtab[i] = (U16)(i * SYSVID_WIDTH / fsw);
  for (i = 0; i < fsh; i++)
    ytab[i] = (U16)(i * SYSVID_HEIGHT / fsh);
}

/*
 * Initialise video
 */
void
sysvid_init(void)
{
  SDL_Surface *s;
  U8 tpix;

  IFDEBUG_VIDEO(printf("xrick/video: start\n"););

  /* SDL */
  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) < 0)
    sys_panic("xrick/video: could not init SDL\n");

  /* various WM stuff */
  SDL_WM_SetCaption("xrick", "xrick");
  SDL_ShowCursor(SDL_DISABLE);
  s = SDL_CreateRGBSurfaceFrom(IMG_ICON->pixels, IMG_ICON->w, IMG_ICON->h, 8, IMG_ICON->w, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
  SDL_SetColors(s, (SDL_Color *)IMG_ICON->colors, 0, IMG_ICON->ncolors);

  tpix = *(IMG_ICON->pixels);
  IFDEBUG_VIDEO(
    sys_printf("xrick/video: icon is %dx%d\n",
	       IMG_ICON->w, IMG_ICON->h);
    sys_printf("xrick/video: icon transp. color is #%d (%d,%d,%d)\n", tpix,
	       IMG_ICON->colors[tpix].r,
	       IMG_ICON->colors[tpix].g,
	       IMG_ICON->colors[tpix].b);
    );
  /*
   * FIXME
   * Setting a mask produces strange results depending on the
   * Window Manager. On fvwm2 it is shifted to the right ...
   */
  SDL_SetColorKey(s,
                  SDL_SRCCOLORKEY,
                  SDL_MapRGB(s->format,IMG_ICON->colors[tpix].r,IMG_ICON->colors[tpix].g,IMG_ICON->colors[tpix].b));

  SDL_WM_SetIcon(s, NULL);

  /* video modes and screen */
  videoFlags = SDL_SWSURFACE|SDL_HWPALETTE;
  sysvid_chkvm();  /* check video modes */
  if (sysarg_args_zoom)
    zoom = sysarg_args_zoom;
  if (sysarg_args_fullscreen) {
    videoFlags |= SDL_FULLSCREEN;
    szoom = zoom;
  }

  if (videoFlags & SDL_FULLSCREEN) {
    sysvid_buildStretch();
    screen = initScreen(fsw, fsh, 8, videoFlags);
  }
  else {
    screen = initScreen(SYSVID_WIDTH * zoom,
			SYSVID_HEIGHT * zoom,
			8, videoFlags);
  }
  if (!screen)
    sys_panic("xrick/video: could not set video mode\n");

  /*
   * create v_ frame buffer
   */
  sysvid_fb = malloc(SYSVID_WIDTH * SYSVID_HEIGHT);
  if (!sysvid_fb)
    sys_panic("xrick/video: sysvid_fb malloc failed\n");

  /* CRT shader on by default (F12 to toggle) */
  sysvid_toggleCrt();

  IFDEBUG_VIDEO(printf("xrick/video: ready\n"););
}

/*
 * Shutdown video
 */
void
sysvid_shutdown(void)
{
  free(sysvid_fb);
  sysvid_fb = NULL;
  free(xtab);
  xtab = NULL;
  free(ytab);
  ytab = NULL;

  if (crt_on) {
    crt_cleanup();
    crt_on = FALSE;
  }

  SDL_Quit();
}

/*
 * Update a single rectangle in windowed mode: integer zoom replication.
 */
static void
updateWindowed(rect_t *rects)
{
  static SDL_Rect area;
  U16 x, y, xz, yz;
  U8 *p, *q, *p0, *q0;
  U16 pitch = (U16)(screen->pitch);

  p0 = sysvid_fb;
  p0 += rects->x + rects->y * SYSVID_WIDTH;
  q0 = (U8 *)screen->pixels;
  q0 += rects->x * zoom + rects->y * zoom * pitch;

  for (y = rects->y; y < rects->y + rects->height; y++) {
    for (yz = 0; yz < zoom; yz++) {
      p = p0;
      q = q0;
      for (x = rects->x; x < rects->x + rects->width; x++) {
	for (xz = 0; xz < zoom; xz++) {
	  *q = *p;
	  q++;
	}
	p++;
      }
      q0 += pitch;
    }
    p0 += SYSVID_WIDTH;
  }

  area.x = rects->x * zoom;
  area.y = rects->y * zoom;
  area.h = rects->height * zoom;
  area.w = rects->width * zoom;
  SDL_UpdateRects(screen, 1, &area);
}

/*
 * Update a single rectangle in fullscreen mode: stretch to fill the
 * whole screen using nearest-neighbor replication (no filtering).
 */
static void
updateFullscreen(rect_t *rects)
{
  static SDL_Rect area;
  U16 dx, dy, dx1, dy1;
  U16 pitch = (U16)(screen->pitch);
  U8 *p, *q0;

  area.x = rects->x * fsw / SYSVID_WIDTH;
  area.y = rects->y * fsh / SYSVID_HEIGHT;
  dx1 = (rects->x + rects->width) * fsw / SYSVID_WIDTH;
  dy1 = (rects->y + rects->height) * fsh / SYSVID_HEIGHT;
  area.w = dx1 - area.x;
  area.h = dy1 - area.y;

  q0 = (U8 *)screen->pixels;
  q0 += area.y * pitch + area.x;

  for (dy = area.y; dy < dy1; dy++) {
    p = sysvid_fb;
    p += ytab[dy] * SYSVID_WIDTH + xtab[area.x];
    for (dx = area.x; dx < dx1; dx++)
      q0[dx - area.x] = p[xtab[dx] - xtab[area.x]];
    q0 += pitch;
  }

  SDL_UpdateRects(screen, 1, &area);
}

/*
 * Update screen
 * NOTE errors processing ?
 */
void
sysvid_update(rect_t *rects)
{
  if (rects == NULL)
    return;

  if (crt_on) {
    crt_draw();
    return;
  }

  if (SDL_LockSurface(screen) == -1)
    sys_panic("xrick/panic: SDL_LockSurface failed\n");

  while (rects) {
    if (videoFlags & SDL_FULLSCREEN)
      updateFullscreen(rects);
    else
      updateWindowed(rects);
    rects = rects->next;
  }

  SDL_UnlockSurface(screen);
}


/*
 * Clear screen
 * (077C)
 */
void
sysvid_clear(void)
{
  memset(sysvid_fb, 0, SYSVID_WIDTH * SYSVID_HEIGHT);
}


/*
 * Zoom
 */
void
sysvid_zoom(S8 z)
{
  if (!(videoFlags & SDL_FULLSCREEN) &&
      ((z < 0 && zoom > 1) ||
       (z > 0 && zoom < SYSVID_MAXZOOM))) {
    zoom += z;
    if (crt_on)
      crt_setvideo();
    else
      screen = initScreen(SYSVID_WIDTH * zoom,
			  SYSVID_HEIGHT * zoom,
			  screen->format->BitsPerPixel, videoFlags);
    sysvid_restorePalette();
    sysvid_update(&SCREENRECT);
  }
}

/*
 * Toggle fullscreen
 */
void
sysvid_toggleFullscreen(void)
{
  videoFlags ^= SDL_FULLSCREEN;

  if (videoFlags & SDL_FULLSCREEN) {  /* go fullscreen */
    szoom = zoom;
    sysvid_buildStretch();
    if (crt_on)
      crt_setvideo();
    else
      screen = initScreen(fsw, fsh, 8, videoFlags);
  }
  else {  /* go window */
    zoom = szoom;
    if (crt_on)
      crt_setvideo();
    else
      screen = initScreen(SYSVID_WIDTH * zoom,
			  SYSVID_HEIGHT * zoom,
			  8, videoFlags);
  }
  if (!screen)
    sys_panic("xrick/video: could not set video mode\n");
  sysvid_restorePalette();
  sysvid_update(&SCREENRECT);
}

/* eof */
