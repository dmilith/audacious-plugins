/*
 * Audacious: Cross platform multimedia player
 * Copyright (c) 2005  Audacious Team
 *
 * Driver for Game_Music_Emu library. See details at:
 * http://www.slack.net/~ant/libs/
 */

#ifndef AUD_CONSOLE_CONFIGURE_H
#define AUD_CONSOLE_CONFIGURE_H 1

#include <libaudcore/core.h>

typedef struct {
	int loop_length;           /* length of tracks that lack timing information */
	bool_t resample;          /* whether or not to resample */
	int resample_rate;         /* rate to resample at */
	int treble;                /* -100 to +100 */
	int bass;                  /* -100 to +100 */
	bool_t ignore_spc_length; /* if true, ignore length from SPC tags */
	int echo;                  /* 0 to +100 */
	bool_t inc_spc_reverb;    /* if true, increases the default reverb */
} AudaciousConsoleConfig;

extern AudaciousConsoleConfig audcfg;

bool_t console_cfg_load(void);
void console_cfg_save(void);
void console_cfg_ui(void);

#endif /* AUD_CONSOLE_CONFIGURE_H */
