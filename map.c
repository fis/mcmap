#include <stdlib.h>

#include "map.h"
#include "world.h"

#define RGB(r,g,b) (((r)<<16)|((g)<<8)|(b))
static Uint32 block_colors[256] = {
	[0x00] = RGB(0,   255, 255), /* air */
	[0x01] = RGB(180, 180, 180), /* stone */
	[0x02] = RGB(34,  180, 0),   /* grass */
	[0x03] = RGB(158, 123, 18),  /* dirt */
	[0x04] = RGB(128, 128, 128), /* cobblestone */
	[0x05] = RGB(133, 78,  0),   /* wood */
	[0x06] = RGB(0,   132, 0),   /* sapling */
	[0x07] = RGB(0,   0,   0),   /* bedrock */
	[0x08] = RGB(39,  161, 225), /* water */
	[0x09] = RGB(39,  161, 225), /* stationary water */
	[0x0a] = RGB(255, 81,  0),   /* lava */
	[0x0b] = RGB(255, 81,  0),   /* stationary lava */
	[0x0c] = RGB(245, 245, 69),  /* sand */
	[0x0d] = RGB(170, 146, 105), /* gravel */
	[0x12] = RGB(0,   132, 0),   /* leaves */
};
#undef RGB

static SDL_Surface *map = 0;
static int map_min_x = 0, map_min_z = 0;
static int map_max_x = 0, map_max_z = 0;

static GMutex *map_mutex;

void map_init(SDL_Surface *screen)
{
	SDL_PixelFormat *fmt = screen->format;

	if (fmt->Rshift != 16 || fmt->Gshift != 8 || fmt->Bshift != 0)
	{
		for (int i = 0; i < 256; i++)
		{
			Uint32 c = block_colors[i];
			Uint32 r = c >> 16, g = (c >> 8) & 0xff, b = c & 0xff;
			block_colors[i] = (r << fmt->Rshift) | (g << fmt->Gshift) | (b << fmt->Bshift);
		}
	}

	map = SDL_CreateRGBSurface(SDL_SWSURFACE, CHUNK_XSIZE, CHUNK_ZSIZE, 32, fmt->Rmask, fmt->Gmask, fmt->Bmask, 0);
	if (!map)
		abort();

	map_mutex = g_mutex_new();
}

void map_update(int x1, int x2, int z1, int z2)
{
	printf("map update chunks (%d,%d)-(%d,%d)\n", x1, z1, x2, z2);

	g_mutex_lock(map_mutex);

	if (map_min_x != chunk_min_x || map_max_x != chunk_max_x
	    || map_min_z != chunk_min_z || map_max_z != chunk_max_z)
	{
		x1 = chunk_min_x; x2 = chunk_max_x;
		z1 = chunk_min_z; z2 = chunk_max_z;

		printf("resizing map (%d,%d)-(%d,%d) -> (%d,%d)-(%d,%d)\n",
		       map_min_x, map_min_z, map_max_x, map_max_z,
		       x1, z1, x2, z2);

		map_min_x = x1; map_max_x = x2;
		map_min_z = z1; map_max_z = z2;

		int xs = x2 - x1 + 1, zs = z2 - z1 + 1;

		Uint32 rmask = map->format->Rmask, gmask = map->format->Gmask, bmask = map->format->Bmask;
		printf("new map size: %dx%d (%dx%d px), masks %08x %08x %08x\n",
		       xs, zs, xs*CHUNK_XSIZE, zs*CHUNK_ZSIZE, rmask, gmask, bmask);

		SDL_FreeSurface(map);
		map = SDL_CreateRGBSurface(SDL_SWSURFACE, xs*CHUNK_XSIZE, zs*CHUNK_ZSIZE, 32, rmask, gmask, bmask, 0);
		if (!map)
			abort();
	}

	SDL_LockSurface(map);
	Uint32 pitch = map->pitch;

	for (int cx = x1; cx <= x2; cx++)
	{
		int cxo = cx - map_min_x;

		for (int cz = z1; cz <= z2; cz++)
		{
			int czo = cz - map_min_z;

			union chunk_coord cc;
			cc.xz[0] = cx; cc.xz[1] = cz;
			struct chunk *c = world_chunk(cc.i64, 0);

			if (!c)
			{
				SDL_Rect r = { .x = cxo*CHUNK_XSIZE, .y = czo*CHUNK_ZSIZE, .w = CHUNK_XSIZE, .h = CHUNK_ZSIZE };
				SDL_FillRect(map, &r, block_colors[0]);
				continue;
			}

			unsigned char *pixels = (unsigned char *)map->pixels + cxo*CHUNK_XSIZE*pitch + czo*CHUNK_ZSIZE*4;
			unsigned char *blocks = &c->surface[0][0];

			for (int bx = 0; bx < CHUNK_XSIZE; bx++)
			{
				Uint32 *p = (Uint32*)pixels;
				unsigned char *b = blocks;

				for (int bz = 0; bz < CHUNK_ZSIZE; bz++)
					*p++ = block_colors[*b++];

				pixels += pitch;
				blocks += CHUNK_ZSIZE;
			}
		}
	}

	SDL_UnlockSurface(map);

	g_mutex_unlock(map_mutex);

	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

void map_draw(SDL_Surface *screen)
{
	g_mutex_lock(map_mutex);

	printf("repainting map...\n");

	SDL_Rect r = { .x = 0, .y = 0, .w = map->w, .h = map->h };
	if (r.w > screen->w) r.w = screen->w;
	if (r.h > screen->h) r.h = screen->h;

	SDL_BlitSurface(map, &r, screen, &r);
	SDL_UpdateRect(screen, r.x, r.y, r.w, r.h);

	g_mutex_unlock(map_mutex);
}
