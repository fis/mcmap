#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

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
static void handle_mouse(SDL_MouseButtonEvent *e);

/* start the user interface side */

void start_ui(bool map, int scale, bool resizable, int wnd_w, int wnd_h)
{
	console_init();

	SDL_Surface *screen = NULL;

	if (map)
	{
		uint32_t videoflags = SDL_SWSURFACE;
		if (resizable) videoflags |= SDL_RESIZABLE;

		map_w = wnd_w;
		map_h = wnd_h;
		screen = SDL_SetVideoMode(wnd_w, wnd_h + 24, 32, videoflags);

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
				TTF_Quit();
				SDL_Quit();
				exit(0);
				return;

			case SDL_KEYDOWN:
				handle_key(&e.key, &repaint);
				break;

			case SDL_MOUSEBUTTONDOWN:
				handle_mouse(&e.button);
				break;

			case SDL_VIDEORESIZE:
				/* the map doesn't seem to like being zero pixels high */
				if (e.resize.h < 36) e.resize.h = 36;
				map_w = e.resize.w;
				map_h = e.resize.h - 24;
				screen = SDL_SetVideoMode(e.resize.w, e.resize.h, 32, SDL_SWSURFACE|SDL_RESIZABLE);
				repaint = 1;
				break;

			case SDL_ACTIVEEVENT:
				if (e.active.state & SDL_APPMOUSEFOCUS)
					map_focused = e.active.gain;
				break;

			case SDL_VIDEOEXPOSE:
			case SDL_MOUSEMOTION:
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
	switch (e->keysym.unicode)
	{
	case '1':
		map_setmode(MAP_MODE_SURFACE, 0, 0, 0);
		*repaint = 1;
		break;

	case '2':
		map_setmode(MAP_MODE_CROSS, 0, 0, 0);
		*repaint = 1;
		break;

	case '3':
		map_setmode(MAP_MODE_TOPO, 0, 0, 0);
		*repaint = 1;
		break;

	case '4':
		map_setmode(MAP_MODE_ISOMETRIC, 0, 0, 0);
		*repaint = 1;
		break;

	case 'c':
		if (map_mode == MAP_MODE_SURFACE || map_mode == MAP_MODE_ISOMETRIC)
		{
			map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_CHOP);
			map_update_ceiling();
			*repaint = 1;
		}
		break;

	case 'f':
		if (map_mode == MAP_MODE_CROSS)
		{
			map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_FOLLOW_Y);
			*repaint = 1;
		}
		break;

#ifdef FEAT_FULLCHUNK
	case 'l':
		if (map_mode == MAP_MODE_SURFACE || map_mode == MAP_MODE_ISOMETRIC)
		{
			map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_LIGHTS);
			*repaint = 1;
		}
		break;

	case 'n':
		if ((map_mode == MAP_MODE_SURFACE || map_mode == MAP_MODE_ISOMETRIC)
		    && (map_flags & MAP_FLAG_LIGHTS))
		{
			map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_NIGHT);
			*repaint = 1;
		}
		break;
#endif

	case 'm':
		map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_MOBS);
		*repaint = 1;
		break;

	case 'p':
		map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_PICKUPS);
		*repaint = 1;
		break;

	case 0:
		break;
	default:
		return;
	}
	switch (e->keysym.sym)
	{
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

static void handle_mouse(SDL_MouseButtonEvent *e)
{
	switch (e->button)
	{
	case SDL_BUTTON_RIGHT:
		if (e->y >= map_h)
			break;

		/* teleport */
		teleport(map_s2w(e->x, e->y, 0, 0));
		break;

	case SDL_BUTTON_WHEELUP:
		map_setscale(+1, 1);
		break;

	case SDL_BUTTON_WHEELDOWN:
		map_setscale(-1, 1);
		break;
	}
}

void handle_chat(struct buffer msg)
{
	static char *colormap[16] =
	{
		"30",   "34",   "32",   "36",   "31",   "35",   "33",   "37",
		"30;1", "34;1", "32;1", "36;1", "31;1", "35;1", "33;1", "0"
	};
	GString *s = g_string_new("");

	while (msg.len > 0)
	{
		if (msg.len >= 3 && msg.data[0] == 0xc2 && msg.data[1] == 0xa7)
		{
			unsigned char cc = msg.data[2];
			int c = -1;

			if (cc >= '0' && cc <= '9') c = cc - '0';
			else if (cc >= 'a' && cc <= 'f') c = cc - 'a' + 10;

			if (c >= 0 && c <= 15)
			{
				if (!opt.noansi)
					g_string_append_printf(s, "\x1b[%sm", colormap[c]);
				ADVANCE_BUFFER(msg, 3);
				continue;
			}
		}

		g_string_append_c(s, *msg.data);
		ADVANCE_BUFFER(msg, 1);
	}

	char *str = g_string_free(s, false);
	if (opt.noansi)
		log_print("%s", str);
	else
		log_print("%s\x1b[0m", str);
	g_free(str);
}
