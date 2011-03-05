#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <SDL.h>

#include "cmd.h"
#include "config.h"
#include "protocol.h"
#include "common.h"
#include "console.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

/* miscellaneous helper routines */

static void handle_key(SDL_KeyboardEvent *e, int *repaint);
static void handle_mouse(SDL_MouseButtonEvent *e, SDL_Surface *screen);

/* start the user interface side */

void start_ui(gboolean map, gint scale, gboolean resizable, int wnd_w, int wnd_h)
{
	console_init();

	SDL_Surface *screen = NULL;

	if (map)
	{
		screen = SDL_SetVideoMode(wnd_w, wnd_h, 32, SDL_SWSURFACE|(resizable ? SDL_RESIZABLE : 0));

		if (!screen)
		{
			dief("Failed to set video mode: %s", SDL_GetError());
			exit(1);
		}

		SDL_WM_SetCaption("mcmap", "mcmap");
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

		map_init(screen);
		map_setscale(scale, 0);
	}

	/* enter SDL main loop */

	while (1)
	{
		int repaint = 0;

		/* process pending events, coalesce repaints */

		SDL_Event e;

		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				SDL_Quit();
				exit(0);
				return;

			case SDL_KEYDOWN:
				handle_key(&e.key, &repaint);
				break;

			case SDL_MOUSEBUTTONDOWN:
				handle_mouse(&e.button, screen);
				break;

			case SDL_VIDEORESIZE:
				screen = SDL_SetVideoMode(e.resize.w, e.resize.h, 32, SDL_SWSURFACE|SDL_RESIZABLE);
				repaint = 1;
				break;

			case SDL_VIDEOEXPOSE:
			case MCMAP_EVENT_REPAINT:
				repaint = 1;
				break;
			}
		}

		/* repaint dirty bits if necessary */

		if (repaint && map)
			map_draw(screen);

		/* wait for something interesting to happen */

		SDL_WaitEvent(0);
	}
}

/* helper routine implementations */

static void handle_key(SDL_KeyboardEvent *e, int *repaint)
{
	switch (e->keysym.sym)
	{
	case SDLK_1:
		map_setmode(MAP_MODE_SURFACE, 0, 0, 0);
		*repaint = 1;
		break;

	case SDLK_2:
		map_setmode(MAP_MODE_CROSS, MAP_FLAG_FOLLOW_Y, 0, 0);
		*repaint = 1;
		break;

	case SDLK_3:
		map_setmode(MAP_MODE_CROSS, 0, MAP_FLAG_FOLLOW_Y, 0);
		*repaint = 1;
		break;

	case SDLK_4:
		map_setmode(MAP_MODE_TOPO, 0, 0, 0);
		*repaint = 1;
		break;

	case SDLK_c:
		map_setmode(MAP_MODE_SURFACE, 0, 0, MAP_FLAG_CHOP);
		map_update_ceiling();
		*repaint = 1;
		break;

#ifdef FEAT_FULLCHUNK
	case SDLK_n:
		/* TODO: handle if map mode != lights */
		map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_NIGHT);
		*repaint = 1;
		break;

	case SDLK_l:
		map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_LIGHTS);
		*repaint = 1;
		break;
#endif

	case SDLK_UP:
		map_update_alt(+1, 1);
		break;

	case SDLK_DOWN:
		map_update_alt(-1, 1);
		break;

	case SDLK_PAGEUP:
		map_setscale(+1, 1);
		break;

	case SDLK_PAGEDOWN:
		map_setscale(-1, 1);
		break;

	default:
		break;
	}
}

static void handle_mouse(SDL_MouseButtonEvent *e, SDL_Surface *screen)
{
	if (e->button == SDL_BUTTON_RIGHT)
	{
		/* teleport */
		int x, z;
		map_s2w(screen, e->x, e->y, &x, &z, 0, 0);
		teleport(x, z);
	}
}

void handle_chat(unsigned char *msg, int msglen)
{
	static char *colormap[16] =
	{
		"30",   "34",   "32",   "36",   "31",   "35",   "33",   "37",
		"30;1", "34;1", "32;1", "36;1", "31;1", "35;1", "33;1", "0"
	};
	unsigned char *p = msg;
	GString *s = g_string_new("");

	while (msglen > 0)
	{
		if (msglen >= 3 && p[0] == 0xc2 && p[1] == 0xa7)
		{
			unsigned char cc = p[2];
			int c = -1;

			if (cc >= '0' && cc <= '9') c = cc - '0';
			else if (cc >= 'a' && cc <= 'f') c = cc - 'a' + 10;

			if (c >= 0 && c <= 15)
			{
				if (!opt.noansi)
					g_string_append_printf(s, "\x1b[%sm", colormap[c]);
				p += 3;
				msglen -= 3;
				continue;
			}
		}

		g_string_append_c(s, *p++);
		msglen--;
	}

	gchar *str = g_string_free(s, FALSE);
	if (opt.noansi)
		log_print("[CHAT] %s", str);
	else
		log_print("[CHAT] %s\x1b[0m", str);
	g_free(str);
}
