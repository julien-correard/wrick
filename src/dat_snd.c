/*
 * xrick/data/dat_snd.c
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

#include "config.h"

#ifdef ENABLE_SOUND

#include "system.h"

/*
 * NOTE: the sound pointers (WAV_*) are defined in game.c, which loads
 * and owns them. This file used to define them as well, which is no
 * longer valid with modern toolchains (-fno-common).
 */

#endif /* ENABLE_SOUND */

/* eof */
