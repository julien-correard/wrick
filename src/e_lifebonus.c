/*
 * xrick/src/e_lifebonus.c
 *
 * RUxF-only bonus (ent 0x4d, sprite 213): gives Rick an extra life when he
 * has fewer than 6 (no pickup animation), otherwise credits him 500 points
 * with the usual score popup (mirrors e_bonus).
 */

#include "system.h"
#include "game.h"
#include "ents.h"
#include "e_lifebonus.h"

#include "e_rick.h"
#include "maps.h"


/*
 * Entity action
 */
void
e_lifebonus_action(U8 e)
{
#define seq c1

  if (ent_ents[e].seq == 0) {
    if (e_rick_boxtest(e)) {
      map_marks[ent_ents[e].mark].ent |= MAP_MARK_NACT;
#ifdef ENABLE_SOUND
      syssnd_play(WAV_BONUS, 1);
#endif
      if (game_lives < 6) {
	/* extra life: no popup, vanish on the spot */
	game_lives++;
	ent_ents[e].n = 0;
	return;
      }
      /* at max lives: 500 points popup like the other bonuses */
      game_score += 500;
      ent_ents[e].seq = 1;
      ent_ents[e].sprite = 0xad;
      ent_ents[e].front = TRUE;
      ent_ents[e].y -= 0x08;
    }
  }

  else if (ent_ents[e].seq > 0 && ent_ents[e].seq < 10) {
    ent_ents[e].seq++;
    ent_ents[e].y -= 2;
  }

  else {
    ent_ents[e].n = 0;
  }
}


/* eof */