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

static void process_region(gchar *filename, jint x, jint z)
{
	gchar *region;
	GError *error = NULL;
	gboolean ok = g_file_get_contents(filename, &region, NULL, &error);
	if (!ok)
		die(error->message);

	/* for each chunk... */
	for (int i = 0; i < 1024; i++)
	{
		unsigned char *p = (unsigned char *)(region + i*4);
		uint32_t offset = (p[0] << 16) | (p[1] << 8) | p[2];
		if (offset == 0)
			continue;
		/* seek there */
		p = (unsigned char *)(region + offset*4096);
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
		jint cx = x*32 + i%32;
		jint cz = z*32 + i/32;
		world_handle_chunk(cx*16, 0, cz*16, CHUNK_XSIZE, CHUNK_YSIZE, CHUNK_ZSIZE, zb, zb_meta, zb_light_blocks, zb_light_sky);
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

	log_print("[INFO] Starting mapping process.");

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

	gchar *dirname = g_strconcat(world, "/region", NULL);
	GError *error = NULL;
	GDir *regions = g_dir_open(dirname, 0, &error);
	g_free(dirname);
	if (!regions)
		die(error->message);
	gchar *filename = NULL;
	while ((filename = (gchar *) g_dir_read_name(regions)))
	{
		gchar xbuf[64], ybuf[64];
		if (sscanf(filename, "r.%[^.].%[^.].mcr", xbuf, ybuf) < 1)
			continue;
		jint x = (jint) strtol(xbuf, NULL, 36);
		jint y = (jint) strtol(ybuf, NULL, 36);
		log_print("[INFO] Processing region (%d,%d)", x, y);
		gchar *full_path = g_strconcat(world, "/region/", filename, NULL);
		process_region(full_path, x, y);
		g_free(full_path);
	}
	g_dir_close(regions);

	log_print("[INFO] Saving map...");
	if (IMG_SavePNG("map.png", screen, 9) != 0)
		dief("Failed to create PNG: %s", SDL_GetError());

	log_print("[INFO] Mapping complete.");

	return 0;
}
