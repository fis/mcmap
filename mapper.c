#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <SDL.h>

#include "config.h"
#include "common.h"
#include "map.h"
#include "world.h"

#include "IMG_savepng.h"

/* standalone mapper */

int mcmap_main(int argc, char **argv)
{
	setlocale(LC_ALL, "");

	/* command line option grokking */

	static GOptionEntry gopt_entries[] = {
		{ NULL }
	};

	GOptionContext *gopt = g_option_context_new("world");
	GError *gopt_error = 0;

	g_option_context_add_main_entries(gopt, gopt_entries, 0);
	if (!g_option_context_parse(gopt, &argc, &argv, &gopt_error))
	{
		die(gopt_error->message);
	}

	if (argc != 2)
	{
		gchar *usage = g_option_context_get_help(gopt, TRUE, 0);
		fputs(usage, stderr);
		return 1;
	}

	log_print("[INFO] Mapping process starting...");

	g_thread_init(0);
	world_init();

	putenv("SDL_VIDEODRIVER=dummy");

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		die("Failed to initialize SDL.");
		return 1;
	}

	SDL_Surface *screen = SDL_SetVideoMode(999, 999, 32, SDL_SWSURFACE);

	if (!screen)
	{
		dief("Failed to create SDL surface: %s", SDL_GetError());
		return 1;
	}

	map_init(screen);
	map_setscale(1, 0);

        struct coord cc = { .x = 0, .z = 0 };
	world_chunk(&cc, 1);

	map_draw(screen);

	if (IMG_SavePNG("map.png", screen, 9) != 0)
	{
		dief("Failed to create PNG: %s", SDL_GetError());
		return 1;
	}
}
