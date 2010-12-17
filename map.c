#include <math.h>
#include <stdlib.h>

#include "common.h"
#include "map.h"
#include "world.h"

/* color maps */

enum special_color_names
{
	COLOR_PLAYER,
	COLOR_MAX_SPECIAL
};

#define RGB(r,g,b) (((r)<<16)|((g)<<8)|(b))

#define AIR_COLOR RGB(180, 255, 255)
static Uint32 block_colors[256] = {
	[0x00] = AIR_COLOR,          /* air */
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
	[0x0e] = RGB(255, 180, 0),   /* gold ore */
	[0x0f] = RGB(92,  92,  92),  /* iron ore */
	[0x10] = RGB(51,  51,  51),  /* coal ore */
	[0x11] = RGB(95,  55,  0),   /* log */
	[0x12] = RGB(0,   132, 0),   /* leaves */
	[0x14] = RGB(185, 234, 231), /* glass */
	[0x25] = RGB(137, 180, 0),   /* yellow flower */
	[0x26] = RGB(122, 130, 0),   /* red flower */
	[0x29] = RGB(255, 180, 0),   /* gold block */
	[0x2a] = RGB(92,  92,  92),  /* iron block */
	[0x2b] = RGB(180, 180, 180), /* double step */
	[0x2c] = RGB(180, 180, 180), /* step */
	[0x2d] = RGB(160, 0,   0),   /* brick */
	[0x30] = RGB(0,   255, 0),   /* mossy cobble */
	[0x31] = RGB(91,  0,   104), /* obsidian */
	[0x32] = RGB(255, 255, 0),   /* torch */
	[0x33] = RGB(255, 108, 0),   /* fire */
	[0x35] = RGB(133, 78,  0),   /* wooden stairs */
	[0x37] = RGB(160, 0,   0),   /* redstone wire */
	[0x38] = RGB(0,   255, 255), /* diamond ore */
	[0x39] = RGB(0,   255, 255), /* diamond block */
	[0x3c] = RGB(114, 76,  9),   /* soil */
	[0x41] = AIR_COLOR,          /* ladder */
	[0x43] = RGB(128, 128, 128), /* cobblestone stairs */
	[0x45] = AIR_COLOR,          /* lever */
	[0x49] = RGB(160, 0,   0),   /* redstone ore */
	[0x4a] = RGB(160, 0,   0),   /* redstone ore (lit) */
	[0x4b] = RGB(160, 0,   0),   /* redstone torch (off) */
	[0x4c] = RGB(160, 0,   0),   /* redstone torch (on) */
	[0x4d] = AIR_COLOR,          /* stone button */
	[0x4e] = AIR_COLOR,          /* snow layer */
	[0x4f] = RGB(211, 255, 255), /* ice */
	[0x50] = RGB(238, 255, 255), /* snow */
	[0x52] = RGB(225, 225, 178), /* clay */
};

static Uint32 special_colors[COLOR_MAX_SPECIAL] = {
	[COLOR_PLAYER] = RGB(255, 0, 255),
};

#undef RGB

/* map graphics code */

static SDL_Surface *map = 0;
static int map_min_x = 0, map_min_z = 0;
static int map_max_x = 0, map_max_z = 0;
static int map_y = 0;

static int map_scale = 1;

static enum map_mode map_mode = MAP_MODE_SURFACE;
static unsigned map_flags = 0;

static GMutex *map_mutex;

static SDL_Surface *player_surf = 0;
static int player_x = 0, player_y = 0, player_z = 0;
static int player_yaw = -1;

void map_init(SDL_Surface *screen)
{
	SDL_PixelFormat *fmt = screen->format;

	Uint32 get_color(Uint32 c)
	{
		Uint32 r = c >> 16, g = (c >> 8) & 0xff, b = c & 0xff;
		return (r << fmt->Rshift) | (g << fmt->Gshift) | (b << fmt->Bshift);
	}

	if (fmt->Rshift != 16 || fmt->Gshift != 8 || fmt->Bshift != 0)
	{
		for (int i = 0; i < 256; i++)
			block_colors[i] = get_color(block_colors[i]);

		for (int i = 0; i < COLOR_MAX_SPECIAL; i++)
			special_colors[i] = get_color(special_colors[i]);
	}

	map = SDL_CreateRGBSurface(SDL_SWSURFACE, CHUNK_XSIZE, CHUNK_ZSIZE, 32, fmt->Rmask, fmt->Gmask, fmt->Bmask, 0);
	player_surf = SDL_CreateRGBSurface(SDL_SWSURFACE, 3, 3, 32, fmt->Rmask, fmt->Gmask, fmt->Bmask, 0);

	if (!map || !player_surf)
		die("SDL map surface init");

	SDL_SetColorKey(player_surf, SDL_SRCCOLORKEY, 0);

	map_mutex = g_mutex_new();
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

void map_update(int x1, int x2, int z1, int z2)
{
	g_mutex_lock(map_mutex);

	if (map_min_x != chunk_min_x || map_max_x != chunk_max_x
	    || map_min_z != chunk_min_z || map_max_z != chunk_max_z)
	{
		x1 = chunk_min_x; x2 = chunk_max_x;
		z1 = chunk_min_z; z2 = chunk_max_z;

		map_min_x = x1; map_max_x = x2;
		map_min_z = z1; map_max_z = z2;

		int xs = x2 - x1 + 1, zs = z2 - z1 + 1;

		Uint32 rmask = map->format->Rmask, gmask = map->format->Gmask, bmask = map->format->Bmask;

		SDL_FreeSurface(map);
		map = SDL_CreateRGBSurface(SDL_SWSURFACE, xs*CHUNK_XSIZE, zs*CHUNK_ZSIZE, 32, rmask, gmask, bmask, 0);
		if (!map)
			die("SDL map resize");
	}

	SDL_LockSurface(map);
	Uint32 pitch = map->pitch;

	Uint32 rshift = map->format->Rshift, gshift = map->format->Gshift, bshift = map->format->Bshift;

	for (int cz = z1; cz <= z2; cz++)
	{
		int czo = cz - map_min_z;

		for (int cx = x1; cx <= x2; cx++)
		{
			int cxo = cx - map_min_x;

			union chunk_coord cc;
			cc.xz[0] = cx; cc.xz[1] = cz;
			struct chunk *c = world_chunk(cc.i64, 0);

			if (!c)
			{
				SDL_Rect r = { .x = cxo*CHUNK_XSIZE, .y = czo*CHUNK_ZSIZE, .w = CHUNK_XSIZE, .h = CHUNK_ZSIZE };
				SDL_FillRect(map, &r, block_colors[0]);
				continue;
			}

			unsigned char *pixels = (unsigned char *)map->pixels + czo*CHUNK_ZSIZE*pitch + cxo*CHUNK_XSIZE*4;
			unsigned char *blocks;
			unsigned blocks_pitch;

			if (map_mode == MAP_MODE_SURFACE || map_mode == MAP_MODE_TOPO)
			{
				blocks = map_mode == MAP_MODE_SURFACE ? &c->surface[0][0] : &c->height[0][0];
				blocks_pitch = 1;
			}
			else if (map_mode == MAP_MODE_CROSS)
			{
				int y0 = map_y;
				if (y0 < 0) y0 = 0;
				else if (y0 >= CHUNK_YSIZE) y0 = CHUNK_YSIZE - 1;
				blocks = &c->blocks[0][0][y0];
				blocks_pitch = CHUNK_YSIZE;
			}
			else
				dief("unrecognized map mode: %d", map_mode);

			unsigned blocks_xpitch = CHUNK_ZSIZE*blocks_pitch;

			for (int bz = 0; bz < CHUNK_ZSIZE; bz++)
			{
				Uint32 *p = (Uint32*)pixels;
				unsigned char *b = blocks;

				for (int bx = 0; bx < CHUNK_XSIZE; bx++)
				{
					if (map_mode == MAP_MODE_TOPO)
					{
						Uint32 v = *b;
						if (v < 64)
							*p++ = ((4*v) << rshift) | ((4*v) << gshift) | ((255-4*v) << bshift);
						else
							*p++ = (255 << rshift) | ((255-4*(v-64)) << gshift);
					}
					else
						*p++ = block_colors[*b];
					b += blocks_xpitch;
				}

				pixels += pitch;
				blocks += blocks_pitch;
			}
		}
	}

	SDL_UnlockSurface(map);

	g_mutex_unlock(map_mutex);

	map_repaint();
}

void map_update_player_pos(double x, double y, double z)
{
	int new_x = round(x), new_y = round(y), new_z = round(z);

	if (new_x == player_x && new_y == player_y && new_z == player_z)
		return;

	player_x = new_x;
	player_y = new_y;
	player_z = new_z;

	if (map_mode == MAP_MODE_CROSS && (map_flags & MAP_FLAG_FOLLOW_Y))
		map_update_alt(new_y, 0);

	map_repaint();
}

void map_update_player_dir(double yaw, double pitch)
{
	static unsigned char bitmaps[8][9] = {
		"   " "###" " # ",
		"#  " "## " "###",
		" # " "## " " # ",
		"###" "## " "#  ",
		" # " "###" "   ",
		"###" " ##" "  #",
		" # " " ##" " # ",
		"  #" " ##" "###"
	};

	int new_yaw = 0;

	yaw = fmod(yaw, 360.0);

	if (yaw < 0.0) yaw += 360.0;
	if (yaw > 360-22.5) yaw -= 360;

	while (new_yaw < 7 && yaw > 22.5)
		new_yaw++, yaw -= 45.0;

	if (new_yaw == player_yaw)
		return;

	player_yaw = new_yaw;

	SDL_LockSurface(player_surf);

	unsigned char *b = bitmaps[new_yaw];

	for (int y = 0; y < 3; y++)
	{
		Uint32 *p = (Uint32*)((unsigned char *)player_surf->pixels + y*player_surf->pitch);

		for (int x = 0; x < 3; x++)
			*p++ = *b++ == ' ' ? 0 : special_colors[COLOR_PLAYER];
	}

	SDL_UnlockSurface(player_surf);

	map_repaint();
}

void map_update_alt(int y, int relative)
{
	int new_y = relative ? map_y + y : y;
	if (new_y < 0) new_y = 0;
	else if (new_y >= CHUNK_YSIZE) new_y = CHUNK_YSIZE-1;

	if (new_y == map_y)
		return;
	map_y = new_y;

	if (map_mode == MAP_MODE_CROSS)
	{
		map_update(map_min_x, map_max_x, map_min_z, map_max_z);
		map_repaint();
	}
}

void map_setmode(enum map_mode mode, unsigned flags)
{
	static char *modenames[] = {
		[MAP_MODE_SURFACE] = "surface",
		[MAP_MODE_CROSS] = "cross-section",
		[MAP_MODE_TOPO] = "topographic",
	};

	map_mode = mode;
	map_flags = flags;

	if (mode == MAP_MODE_CROSS)
		map_y = player_y;

	printf("MODE: %s%s\n",
	       modenames[mode],
	       mode == MAP_MODE_CROSS && (flags & MAP_FLAG_FOLLOW_Y) ? " (follow player)" : "");

	map_update(map_min_x, map_max_x, map_min_z, map_max_z);
}

void map_setscale(int scale, int relative)
{
	int s = relative ? map_scale + scale : scale;
	if (s < 1) s = 1;

	if (s == map_scale)
		return;

	map_scale = s;
	map_repaint();
}

static void map_draw_entity_marker(struct entity *e, void *userdata)
{
	SDL_Surface *screen = userdata;

	int ex = screen->w/2 + map_scale*(e->x - player_x) - 1;
	int ez = screen->h/2 + map_scale*(e->z - player_z) - 1;

	if (ex < 0 || ez < 0 || ex+3 > screen->w || ez+3 > screen->h)
		return;

	SDL_Rect r = { .x = ex, .y = ez, .w = 3, .h = 3 };
	SDL_FillRect(screen, &r, special_colors[COLOR_PLAYER]);
}

void map_draw(SDL_Surface *screen)
{
	/* clear the window */

	SDL_Rect rect_screen = { .x = 0, .y = 0, .w = screen->w, .h = screen->h };
	SDL_FillRect(screen, &rect_screen, 0);

	/* draw the map */

	g_mutex_lock(map_mutex);

	int scr_x0 = player_x - screen->w/2/map_scale;
	int scr_z0 = player_z - screen->h/2/map_scale;

	if (map_scale == 1)
	{
		SDL_Rect rect_dst = { .x = 0, .y = 0, .w = screen->w, .h = screen->h };
		SDL_Rect rect_src = {
			.x = scr_x0 - map_min_x*CHUNK_XSIZE,
			.y = scr_z0 - map_min_z*CHUNK_ZSIZE,
			.w = screen->w,
			.h = screen->h
		};

		if (rect_src.x < 0)
		{
			int d = -rect_src.x;
			rect_dst.x += d;
			rect_src.w -= d;
			rect_dst.w -= d;
			rect_src.x = 0;
		}

		if (rect_src.y < 0)
		{
			int d = -rect_src.y;
			rect_dst.y += d;
			rect_src.h -= d;
			rect_dst.h -= d;
			rect_src.y = 0;
		}

		if (rect_src.x + rect_src.w > map->w)
		{
			int d = map->w - (rect_src.x + rect_src.w);
			rect_src.w -= d;
			rect_dst.w -= d;
		}

		if (rect_src.y + rect_src.h > map->h)
		{
			int d = map->h - (rect_src.y + rect_src.h);
			rect_src.h -= d;
			rect_dst.h -= d;
		}

		if (rect_dst.w > 0 && rect_dst.h > 0)
			SDL_BlitSurface(map, &rect_src, screen, &rect_dst);
	}
	else
	{
		SDL_LockSurface(screen);
		SDL_LockSurface(map);

		int m_x0 = scr_x0 - map_min_x*CHUNK_XSIZE;
		int m_z = scr_z0 - map_min_z*CHUNK_ZSIZE;

		int m_xo0 = 0; /* FIXME offsets for smooth scaling, to keep "player at w/2" true */
		int m_zo = 0;

		for (int s_y = 0; s_y < screen->h && m_z < map->h; s_y++)
		{
			if (m_z >= 0)
			{
				int m_x = m_x0, m_xo = m_xo0;
				for (int s_x = 0; s_x < screen->w && m_x < map->w; s_x++)
				{
					if (m_x >= 0)
					{
						void *s = (unsigned char *)screen->pixels + s_y*screen->pitch + 4*s_x;
						void *m = (unsigned char *)map->pixels + m_z*map->pitch + 4*m_x;
						*(Uint32 *)s = *(Uint32 *)m;
					}

					if (++m_xo == map_scale)
						m_xo = 0, m_x++;
				}
			}

			if (++m_zo == map_scale)
				m_zo = 0, m_z++;
		}

		SDL_UnlockSurface(map);
		SDL_UnlockSurface(screen);
	}

	g_mutex_unlock(map_mutex);

	/* player indicators and such */

	{
		SDL_Rect rect_dst = { .x = screen->w/2 - 1, .y = screen->h/2 - 1, .w = 3, .h = 3 };
		SDL_Rect rect_src = { .x = 0, .y = 0, .w = 3, .h = 3 };
		SDL_BlitSurface(player_surf, &rect_src, screen, &rect_dst);
	}

	world_entities(map_draw_entity_marker, screen);

	/* update screen buffers */

	SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
}
