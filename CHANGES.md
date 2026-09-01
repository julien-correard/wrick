# XRick — Modifications from the Original

This repository is a modified version of **XRick** (the re-implementation of the
classic platformer *Rick Dangerous*). Compared to the original code base, several
additions and fixes were made. Everything listed here was written and implemented
in a "vibe-coding" style: changes were driven by the goal and iterated until the
game did what was wanted, without starting from a strict plan.

---

## 1. Non-Technical Summary

If you just want to know what's different, here it is in plain words:

- **A CRT shader.** The game can now look like it's running on a real cathode-ray
  tube TV: scanlines, a slightly curved screen, and warm glow. It's on by default,
  and you can turn it on or off with the **F12** key (or disable it at startup with
  `-nocrt`).

- **Smoother, adjustable speed.** The game's timing was reworked so it renders at a
  smooth ~60 frames per second instead of being locked to the original slow tick,
  while the game logic itself keeps its original pace. You can also play faster or
  slower at any moment (**F10** to speed up, **F11** to slow down), or set a speed at
  startup with `-speed`.

- **A proper respawn.** After dying, Rick used to come back on screen still holding
  the last pose he had before he died (often the death pose). Now he respawns
  standing, facing the right way, like nothing happened.

- **Rolling barrels that actually roll.** Barrels now have a proper rolling
  animation, and the animation even flips correctly when a barrel changes direction.

- **A fixed back-and-forth barrel.** There was a barrel that was supposed to roll
  left and then roll back to the right; it now does exactly that, with the rotating
  animation facing the correct way for both halves of the trip.

- **Fair lethal tiles.** Some "death" tiles were drawn with transparent gaps (the
  dangerous pixels were invisible or barely there). Rick used to die just by
  touching the empty part of such a tile. Now, only the actually-drawn pixels of a
  lethal tile can hurt Rick. This is what makes it possible to get through
  **sub-map 19** the honest way, without cheating.

---

## 2. Detailed, Technical Documentation

Everything below goes back over each point in depth with the implementation details.

---

### 2.1 The CRT Shader

The CRT look is implemented as a **single-pass OpenGL fragment shader** written in
GLSL, entirely contained in `src/sysvid_crt.e` (a text file that is `#include`d into
`src/sysvid.c` at line 93, right where the OpenGL video path is set up).

**What it emulates** (inspired by libretro CRT presets):

- **Scanlines** — the horizontal dark lines of a CRT.
- **Aperture grille** — the vertical phosphor-stripe structure.
- **Screen curvature** — the subtle barrel distortion of a tube.
- **Halation / glow** — the soft bloom around bright pixels.
- **Vignette** — the darker corners of a real tube.

**How it is wired without new dependencies:**

- The effect deliberately avoids linking against any OpenGL library. Every GL entry
  point the shader needs is resolved **at runtime** via
  `SDL_GL_GetProcAddress`, so nothing new is linked into the binary.
- It uses **fixed-function immediate mode** (glBegin/glEnd with textured quads) and a
  single fragment shader, keeping the integration minimal on top of plain SDL.
- The game palette is pushed to the GPU as a lookup texture each time the palette
  changes (`crt_palrgb`), so the CRT path renders the indexed frame buffer through the
  palette.

**Default state and toggling:**

- The CRT mode is **on by default** when OpenGL can be initialized
  (`src/sysvid.c:270`: `if (!sysarg_args_nocrt && crt_start()) crt_on = TRUE;`).
- It can be disabled at startup with the **`-nocrt`** command-line option
  (`src/sysarg.c:204`), falling back to the software renderer.
- It can be toggled live with the **F12** key via `sysvid_toggleCrt()`
  (`src/sysevt.c:120`).
- The init is done *before* a single `SDL_SetVideoMode`, which avoids the
  software-to-OpenGL mode switch that used to break the window under Wine/Windows.

---

### 2.2 FPS Improvement and Adjustable Speed

The original XRick tied rendering directly to the game logic tick. This version
decouples the two and adds a runtime-adjustable speed.

**The timing core** (`include/game.h`, `src/game.c`):

- `GAME_PERIOD` is defined as **32 ms** per logic tick (`include/game.h:31`), i.e.
  about **31 logic ticks per second** — the original pace of the game.
- The runtime variable `game_period` is the current number of milliseconds per logic
  tick (`U8 game_period`, `src/game.c:58`).
- `-speed <n>` accepts an integer from **1 (fastest) to 100 (slowest)** and sets the
  logic tick period (`src/sysarg.c:163`; the value is stored in `sysarg_args_period`
  and applied in `game_run` at `src/game.c:257`).

**Run-time speed control** (`src/game.c:148`, `game_speed(S8 delta)`):

- `speed` is simply *the inverse* of the frame period: more milliseconds per tick
  means fewer logic ticks per second, so the game runs **slower**; fewer
  milliseconds means **faster**.
- The value is clamped to `1..100` ms.
- It is bound to **F10** (`game_speed(1)`, faster) and **F11** (`game_speed(-1)`,
  slower) in `src/sysevt.c:114-117`.

**The reworked main loop** (`src/game.c:235-309`, `game_run`):

- Logic is run with an **accumulator** against `game_period`:
  ```c
  while (acc >= game_period && game_state != EXIT) {
      acc -= game_period;
      ent_snap();          /* save pre-tick state for interpolation */
      frame();             /* advance the game logic */
  }
  ```
- A stall clamp prevents the loop from "fast-forwarding" a mountain of ticks after a
  frame hitch: `if (el > 4 * game_period) el = 4 * game_period;` (`src/game.c:275`).
- **Presentation is decoupled at ~60 fps** (`GAME_RENDER_PERIOD = 16` ms,
  `src/game.c:235`). Between logic ticks, entities are drawn at positions
  **interpolated** between the pre-tick snapshot (`ent_snap`) and the post-tick state
  (`ent_draw_interp(a16)`), and while scrolling the playfield is shifted by a
  fractional vertical offset. This yields smooth motion **without changing game
  speed** — an FPS improvement rather than a speed change.

---

### 2.3 Rick's Respawn Sprite

Previously, when Rick lost a life and respawned, the code restored his position but
left him holding whatever sprite he had when he died (typically the death/zombie
pose). The respawn now forces a clean standing pose.

`src/e_rick.c:605` (`e_rick_restore`):

```c
E_RICK_ENT.x = save_x;
E_RICK_ENT.y = save_y;
E_RICK_ENT.front = FALSE;
E_RICK_STRST(E_RICK_STCLIMB);
game_dir = (save_x >= 0xA0) ? LEFT : RIGHT;
E_RICK_ENT.sprite = (game_dir ? 0x17 : 0x0B);   // <-- respawn sprite fix
```

- Sprites `0x17` and `0x0B` are exactly the **standing/still** poses that
  `e_rick_action` selects for the stopped state (`E_RICK_ENT.sprite = (game_dir ?
  0x17 : 0x0B)` at `src/e_rick.c:462`), facing right or left respectively.
- The facing direction is derived from the respawn position (`save_x >= 0xA0` →
  LEFT, else RIGHT), so Rick always comes back standing and correctly oriented
  instead of frozen in a pre-death frame.

---

### 2.4 Barrel Rolling Animation

Barrels gained a dedicated rolling animation with proper frame rotation, including a
reversed sequence so the rotation looks correct whichever way the barrel is moving.

**New data** (`src/dat_ents.c`):

- A dedicated t3 rolling-barrel entity was added (entdata index `0x4a`), with a
  base sprite `sprbase = 0x92`:
  ```c
  /* 0x4a : dedicated t3 rolling barrel, 4 frames (sprseq 146) round-trip 20 cols (mvstep 784) */
  { 0x12, 0x15, 0x0092, 0x0310, 0x18, 0x04, 0x1A },
  ```
- Two new sprite sequences were appended to `ent_sprseq`, at bases `0x92` and `0x9f`,
  each a 4-frame rolling cycle repeated three times, terminated by `0xff`:
  ```c
  106, 106, 106, 150, 150, 150, 107, 107, 107, 151, 151, 151, 0xff,  // forward (left)
  106, 106, 106, 151, 151, 151, 107, 107, 107, 150, 150, 150, 0xff,  // reversed (right)
  ```
  - **forward** (base `0x92`): sprites `106, 150, 107, 151`
  - **reversed** (base `0x9f`): sprites `106, 151, 107, 150`

**The direction-aware rotation** (`src/e_them.c`, in `e_them_t3_action2`):

- Instead of always using `sprbase`, the code picks the **forward or reversed**
  sequence depending on the current horizontal direction of the movement step:
  ```c
  /* forward (left) base 0x92: 106,150,107,151
     reversed (right) base 0x9f: 106,151,107,150 */
  if (ent_ents[e].sprbase == 0x92)
      base = (ent_mvstep[ent_ents[e].step_no].dx < 0) ? 0x92 : 0x9f;
  else
      base = ent_ents[e].sprbase;
  ```
- So when the barrel rolls to the left (`dx < 0`) it uses the `0x92` sequence, and
  when it rolls to the right it uses the `0x9f` sequence, making the rolling
  animation visually turn around with the direction of travel.

---

### 2.5 Ent 71 — The Back-and-Forth Barrel

A specific barrel (entity 71) was meant to roll one way and then come back the other
way; it now really does that round trip, with the animation returning.

**New movement steps** (`src/dat_ents.c`, near the `ent_mvstep` index used by the
barrel, `0x0310`):

```c
/* ent 71 barrel: roll left 20 cols (160px) then back right 20 cols, at ~3px/frame */
{ 0x35, -3, 0 },   // roll left, 53 frames of -3px
{ 0x01, -1, 0 },   // finish with 1 frame of -1px  (total ~160 px left)
{ 0x35,  3, 0 },   // roll right, 53 frames of +3px
{ 0x01,  1, 0 },   // finish with 1 frame of +1px   (total ~160 px right)
{ 0xff,  0, 0 },   // end of sequence
```

- Each step has a `count`, an X delta and a Y delta. The `-3/+3` per-frame steps are
  repeated 0x35 (53) times and rounded off with a single `-1/+1` frame, so the barrel
  covers roughly **160 pixels** (20 tile columns) out and the same 160 pixels back.
- Because it moves left on the way out and right on the way back, the 
  direction-aware rolling sequence from §2.4 picks the **forward** sequence while
  rolling left and the **reversed** sequence while returning — so the barrel rotates
  consistently on both halves of the trip.

---

### 2.6 Pixel-Perfect Lethal Tile Collision

Lethality used to be decided entirely by the per-tile environment flag
`MAP_EFLG_LETHAL`: if Rick's box overlapped a lethal tile, he died. The problem is
that several lethal tiles are drawn with **transparent (color 0) pixels** — for
example spike / edge tiles where only part of the sprite is the actual hazard and
the rest is empty. Rick could be killed by brushing an invisible empty part of the
tile.

**The fix — `e_rick_lethalpix()`** (`src/e_rick.c`): a pixel-perfect lethal check.

- It takes Rick's **prospective** position and his crawl state and recomputes the
  exact hitbox, matching `e_rick_boxtest`:
  - X range: `x + 0x05 .. x + 0x11`
  - Y range: `y + (crawl ? 0x08 : 0x00) .. y + 0x14`
- It iterates the map tiles overlapped by that hitbox (`map_map[ty][tx]`), skips any
  tile that is not flagged lethal (`map_eflg[tile] & MAP_EFLG_LETHAL`), and then for
  each **(non-zero) pixel** of that tile checks whether it falls inside Rick's
  hitbox. Pixel colour is extracted from the ST tile data (4 bits per pixel) via:
  ```c
  row = tiles_data[map_tilesBank][tile][py];
  if ((row >> (4 * (7 - px))) & 0x0F) return TRUE;   /* color > 0 → lethal */
  ```
  This nibble extraction matches exactly how `draw_tile` renders the pixels (rightmost
  pixel = low nibble), so what looks hazardous on screen is what can kill you.

**Where it is applied** — all four lethal-death checks in `e_rick_action2` now use:

```c
if ((env1 & MAP_EFLG_LETHAL) &&
    e_rick_lethalpix(<prospective x>, <prospective y>, crawl)) {
    e_rick_gozombie();
    return;
}
```

covering:
1. vertical (falling) movement,
2. horizontal (walking) movement,
3. climbing vertically,
4. climbing horizontally.

The coarse `env1 & MAP_EFLG_LETHAL` test is kept as a fast gate: the (more
expensive) pixel loop only runs when a lethal tile is actually in play.

**Net effect:** a lethal tile only kills Rick when his hitbox overlaps a **coloured
(color > 0) pixel** of that tile; overlapping only its **transparent color-0
pixels** is safe. Because the previously lethal-but-transparent areas of several
tiles are now harmless, **sub-map 19 can be completed legitimately** (it makes
heavy use of such lethal tiles, where the honest route was previously blocked by
invisible deaths), without resorting to cheats.

---

### 2.7 Vertical Scroll Alignment and Climber Fixes

The smooth-scroll rendering (§2.2) draws the background with a fractional vertical
offset while the playfield scrolls, but the entities drawn *during* a scroll were not
given the same offset. Entities that could not be interpolated — e.g. climbers just
spawned or reset in the middle of a scroll `ent_actvis` pass — were therefore drawn at
their **current** position while the background was shifted, so they appeared a few
pixels out of place (upwards, toward the top of the screen, on an upward scroll).
Sub-maps with several climbers side by side (such as the three at row 48 in sub-map
12) made this obvious.

**The fix — `src/ents.c` (`ent_draw_interp`):**

- The function now takes the scroll offset alongside the interpolation factor
  (`ent_draw_interp(U32 a16, S16 off)`, declared in `include/ents.h`, called from
  `src/game.c:242` with the same `off` passed to `draw_mapCompose`).
- Only the **non-interpolated** branch applies it, so interpolated entities are not
  double-shifted (their snapshot already tracks the background through the scroll):
  ```c
  if (!isnap_n[i] || dx > 0x20 || dx < -0x20 || dy > 0x16 || dy < -0x16) {
      xi = ent_ents[i].x;
      yi = ent_ents[i].y + off;   /* follow the scroll offset like the map */
  }
  ```

**The fix — shared per-entity state (`src/e_them.c`, `e_them_t2_action2`):**

- The "Black Magic" randomizer used file-global `static` scratch variables
  (`bx`, `cx` and their byte aliases `bl/bh/cl/ch`) that were shared by **every**
  climber on the sub-map, so several climbers corrupted each other's walk direction.
- They are now per-invocation **local** variables; only the pointers into the shared
  global rng seed (`e_them_rndseed`) remain, which is the intended shared state.

**The fix — latent enemies standing frozen at the top of a sub-map (`src/ents.c`, `ent_actvis`):**

The real reason climbers (e_them types) stood idle "at the top of the screen" and took
far too long to start moving was their **wake-up latency**. Each entity is created with
`latency = (lt & 7) << 5` (32/64/96 ticks ≈ 1-3 s) and stays frozen while `latency > 0`.
When such an entity is born right at (or just above) the visible top — which is exactly
what happens with the three row-48 climbers of sub-map 12, entered at `rowin = 48` — it
spends that whole delay already visible.

The delay was meant to play out *before* the entity becomes visible: during the
approach (or scroll) it is born in the hidden top/bottom bands. So when an enemy is created
**outside the visible column** (below the top row or below the bottom row), that delay
is pre-consumed at spawn, so by the time it scrolls into view it is already moving:

```c
if (map_marks[m].ent < 0x10) {
    int rel = (int)map_marks[m].row - (int)map_frow;
    if (rel < MAP_ROW_SCRTOP || rel > MAP_ROW_SCRBOT)
        ent_ents[e].latency = 0;
}
```

- Scoped to e_them types (`ent < 0x10`), so bonus/box/other timing is untouched.
- `rel < MAP_ROW_SCRTOP` ⇒ born above the visible top; `rel > MAP_ROW_SCRBOT` ⇒ born
  below the visible bottom; both are off-screen, so their latency can be safely served
  in advance.

---

*All of the above changes were vibe-coded: implemented iteratively against observed
behaviour until each one did what was wanted, then validated with clean Linux and
Windows builds and headless/Wine smoke tests.*
