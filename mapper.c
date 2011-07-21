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
#include "nbt.h"

#include "IMG_savepng.h"

/* standalone mapper */

enum compression
{
	COMPRESSION_GZIP = 1,
	COMPRESSION_ZLIB = 2
};

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
		die("Failed to initialize SDL.");

	SDL_Surface *screen = SDL_SetVideoMode(999, 999, 32, SDL_SWSURFACE);

	if (!screen)
		dief("Failed to create SDL surface: %s", SDL_GetError());

	map_init(screen);
	map_setscale(1, 0);

	log_print("[INFO] Processing region (0,0)...");

	gchar *region;
	g_file_get_contents("/home/elliott/.minecraft/saves/server/region/r.0.0.mcr", &region, NULL, NULL);
	region += 8192;
	uint32_t len = (region[0] << 24) | (region[1] << 16) | (region[2] << 8) | region[3];
	enum compression comp = region[4];
	if (comp != COMPRESSION_ZLIB)
		dief("Wrong compression type %d", comp);
	struct buffer buf = { len, (unsigned char *)(region + 5) };

	struct nbt_tag *chunk = nbt_uncompress(buf);
	log_print("%p", nbt_struct_field(chunk, "Blocks"));

	log_print("[INFO] Saving map...");
	map_draw(screen);
	if (IMG_SavePNG("map.png", screen, 9) != 0)
		dief("Failed to create PNG: %s", SDL_GetError());

	log_print("[INFO] Mapping complete.");

	return 0;
}
