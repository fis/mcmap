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

gchar *world;

static void process_region(int x, int z)
{
	gchar *filename = g_strdup_printf("%s/region/r.%d.%d.mcr", world, x, z);
	gchar *region;

	GError *error = NULL;
	gsize len;
	gboolean ok = g_file_get_contents(filename, &region, &len, &error);
	g_free(filename);
	if (!ok)
		die(error->message);

	/* for each chunk... */
	for (int i = 0; i < 1024; i++)
	{
		gchar *p = region + i*4;
		uint32_t offset = (p[0] << 16) | (p[1] << 8) | p[2];
		/* seek there */
		p = region + offset*4096;
		printf("%ld bytes from EOF\n", (region + len) - p);
		/* and process it */
		uint32_t len = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
		enum compression comp = p[4];
		if (comp != COMPRESSION_ZLIB)
			dief("Wrong compression type %d", comp);
		struct buffer buf = { len, (unsigned char *)(p + 5) };
		struct nbt_tag *chunk = nbt_uncompress(buf);
		struct buffer zb = nbt_blob(nbt_struct_field(chunk, "Blocks"));
		struct buffer zb_meta = nbt_blob(nbt_struct_field(chunk, "Data"));
		struct buffer zb_light_blocks = nbt_blob(nbt_struct_field(chunk, "BlockLight"));
		struct buffer zb_light_sky = nbt_blob(nbt_struct_field(chunk, "SkyLight"));
		world_handle_chunk(0, 0, 0, CHUNK_XSIZE, CHUNK_YSIZE, CHUNK_ZSIZE, zb, zb_meta, zb_light_blocks, zb_light_sky);
	}
}

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
		die(gopt_error->message);

	if (argc != 2)
	{
		gchar *usage = g_option_context_get_help(gopt, TRUE, 0);
		fputs(usage, stderr);
		return 1;
	}

	world = (gchar *) argv[1];

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
	process_region(0, 0);

	log_print("[INFO] Saving map...");
	map_draw(screen);
	if (IMG_SavePNG("map.png", screen, 9) != 0)
		dief("Failed to create PNG: %s", SDL_GetError());

	log_print("[INFO] Mapping complete.");

	return 0;
}
