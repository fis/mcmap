#include <math.h>
#include <stdlib.h>
#include <SDL.h>

#include <glib.h>

#include "protocol.h"
#include "common.h"
#include "config.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

/* color maps */

enum special_color_names
{
	COLOR_PLAYER,
	COLOR_UNLOADED,
	COLOR_MAX_SPECIAL
};

#define AIR_COLOR {135, 206, 235}
static struct rgb block_colors[256] = {
	[0x00] = AIR_COLOR,       /* air */
	[0x01] = {180, 180, 180}, /* stone */
	[0x02] = {34,  180, 0},   /* grass */
	[0x03] = {158, 123, 18},  /* dirt */
	[0x04] = {128, 128, 128}, /* cobblestone */
	[0x05] = {133, 78,  0},   /* wood */
	[0x06] = {0,   132, 0},   /* sapling */
	[0x07] = {0,   0,   0},   /* bedrock */
	[0x08] = {39,  161, 225}, /* water */
	[0x09] = {39,  161, 225}, /* stationary water */
	[0x0a] = {255, 81,  0},   /* lava */
	[0x0b] = {255, 81,  0},   /* stationary lava */
	[0x0c] = {245, 245, 69},  /* sand */
	[0x0d] = {222, 190, 160}, /* gravel */
	[0x0e] = {255, 180, 0},   /* gold ore */
	[0x0f] = {92,  92,  92},  /* iron ore */
	[0x10] = {51,  51,  51},  /* coal ore */
	[0x11] = {95,  55,  0},   /* log */
	[0x12] = {0,   132, 0},   /* leaves */
	[0x14] = {185, 234, 231}, /* glass */
	[0x15] = {65,  102, 245}, /* lapis lazuli ore */
	[0x16] = {65,  102, 245}, /* lapis lazuli block */
	[0x18] = {245, 245, 69},  /* sandstone */
	[0x1f] = {34,  180, 0},   /* tall grass (same as grass) */
	[0x23] = {240, 240, 240}, /* cloth */
	[0x25] = {137, 180, 0},   /* yellow flower */
	[0x26] = {122, 130, 0},   /* red flower */
	[0x29] = {255, 180, 0},   /* gold block */
	[0x2a] = {92,  92,  92},  /* iron block */
	[0x2b] = {180, 180, 180}, /* double step */
	[0x2c] = {180, 180, 180}, /* step */
	[0x2d] = {160, 0,   0},   /* brick */
	[0x30] = {0,   255, 0},   /* mossy cobble */
	[0x31] = {61,  0,   61},  /* obsidian */
	[0x32] = {255, 255, 0},   /* torch */
	[0x33] = {255, 108, 0},   /* fire */
	[0x35] = {133, 78,  0},   /* wooden stairs */
	[0x37] = {160, 0,   0},   /* redstone wire */
	[0x38] = {0,   255, 255}, /* diamond ore */
	[0x39] = {0,   255, 255}, /* diamond block */
	[0x3c] = {114, 76,  9},   /* soil */
	[0x41] = AIR_COLOR,       /* ladder */
	[0x43] = {128, 128, 128}, /* cobblestone stairs */
	[0x45] = AIR_COLOR,       /* lever */
	[0x49] = {160, 0,   0},   /* redstone ore */
	[0x4a] = {160, 0,   0},   /* redstone ore (lit) */
	[0x4b] = {160, 0,   0},   /* redstone torch (off) */
	[0x4c] = {160, 0,   0},   /* redstone torch (on) */
	[0x4d] = AIR_COLOR,       /* stone button */
	[0x4e] = AIR_COLOR,       /* snow layer */
	[0x4f] = {211, 255, 255}, /* ice */
	[0x50] = {238, 255, 255}, /* snow */
	[0x52] = {165, 42,  42},  /* clay */
	[0x53] = {0,   255, 0},   /* reed^H^H^H^Hsugar cane */
	[0x56] = {246, 156, 0},   /* pumpkin */
	[0x57] = {121, 17,  0},   /* netherstone */
	[0x58] = {107, 43,  15},  /* slow sand */
	[0x59] = {186, 157, 0},   /* lightstone */
	[0x5b] = {246, 156, 0},   /* pumpkin (lit) */
};

static struct rgb special_colors[COLOR_MAX_SPECIAL] = {
	[COLOR_PLAYER] = {255, 0, 255},
	[COLOR_UNLOADED] = {16, 16, 16},
};

/* map graphics code */

double player_dx = 0.0, player_dy = 0.0, player_dz = 0.0;
jint player_x = 0, player_y = 0, player_z = 0;
jshort player_health = 0;

jint ceiling_y = CHUNK_YSIZE;

static SDL_Surface *map = 0;
static jint map_min_x = 0, map_min_z = 0;
static jint map_max_x = 0, map_max_z = 0;
static jint map_y = 0;
static int map_darken = 0;
static unsigned map_rshift, map_gshift, map_bshift;

static int map_scale = 1;
static int map_scale_indicator = 3;

static enum map_mode map_mode = MAP_MODE_SURFACE;
static unsigned map_flags = 0;

static GMutex * volatile map_mutex = 0;

static int player_yaw = 0;

static inline Uint32 pack_rgb(struct rgb rgb)
{
	return (rgb.r << map_rshift) |
	       (rgb.g << map_gshift) |
	       (rgb.b << map_bshift);
}

void map_init(SDL_Surface *screen)
{
	SDL_PixelFormat *fmt = screen->format;

	map = SDL_CreateRGBSurface(SDL_SWSURFACE, CHUNK_XSIZE, CHUNK_ZSIZE, 32, fmt->Rmask, fmt->Gmask, fmt->Bmask, 0);
	map_rshift = fmt->Rshift;
	map_gshift = fmt->Gshift;
	map_bshift = fmt->Bshift;

	if (!map)
		die("SDL map surface init");

	map_mutex = g_mutex_new();

	map_flags |= MAP_FLAG_CHOP;

#ifdef FEAT_FULLCHUNK
	map_flags |= MAP_FLAG_LIGHTS;
#endif
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

void map_update(jint x1, jint x2, jint z1, jint z2)
{
	g_mutex_lock(map_mutex);

	if (map_min_x != chunk_min_x || map_max_x != chunk_max_x
	    || map_min_z != chunk_min_z || map_max_z != chunk_max_z)
	{
		x1 = chunk_min_x; x2 = chunk_max_x;
		z1 = chunk_min_z; z2 = chunk_max_z;

		map_min_x = x1; map_max_x = x2;
		map_min_z = z1; map_max_z = z2;

		jint xs = x2 - x1 + 1, zs = z2 - z1 + 1;

		Uint32 rmask = map->format->Rmask, gmask = map->format->Gmask, bmask = map->format->Bmask;

		SDL_FreeSurface(map);
		map = SDL_CreateRGBSurface(SDL_SWSURFACE, xs*CHUNK_XSIZE, zs*CHUNK_ZSIZE, 32, rmask, gmask, bmask, 0);
		if (!map)
			die("SDL map resize");
	}

	SDL_LockSurface(map);
	Uint32 pitch = map->pitch;

	for (jint cz = z1; cz <= z2; cz++)
	{
		jint czo = cz - map_min_z;

		for (jint cx = x1; cx <= x2; cx++)
		{
			jint cxo = cx - map_min_x;

			struct coord cc = { .x = cx, .z = cz };
			struct chunk *c = world_chunk(&cc, 0);

			if (!c)
			{
				SDL_Rect r = { .x = cxo*CHUNK_XSIZE, .y = czo*CHUNK_ZSIZE, .w = CHUNK_XSIZE, .h = CHUNK_ZSIZE };
				SDL_FillRect(map, &r, pack_rgb(special_colors[COLOR_UNLOADED]));
				continue;
			}

			unsigned char *pixels = (unsigned char *)map->pixels + czo*CHUNK_ZSIZE*pitch + cxo*CHUNK_XSIZE*4;

			unsigned char *blocks = &c->blocks[0][0][0];
			unsigned blocks_pitch = CHUNK_YSIZE;

			if (map_mode == MAP_MODE_TOPO)
			{
				blocks = &c->height[0][0];
				blocks_pitch = 1;
			}

			unsigned blocks_xpitch = CHUNK_ZSIZE*blocks_pitch;

			for (jint bz = 0; bz < CHUNK_ZSIZE; bz++)
			{
				Uint32 *p = (Uint32*)pixels;
				unsigned char *b = blocks;

				for (jint bx = 0; bx < CHUNK_XSIZE; bx++)
				{
					Uint32 y = c->height[bx][bz];

					/* select basic color */

					struct rgb rgb;

					if (map_mode == MAP_MODE_TOPO)
					{
						Uint32 v = *b;
						if (v < 64)
							rgb = RGB(4*v, 4*v, 0);
						else
							rgb = RGB(255, 255-4*(v-64), 0);
					}
					else
					{
						if (map_mode == MAP_MODE_CROSS)
							y = map_y;
						else if (map_flags & MAP_FLAG_CHOP && y >= ceiling_y)
						{
							y = ceiling_y - 1;
							while (air(b[y]) && y > 1)
								y--;
						}

						rgb = block_colors[b[y]];
					}

					/* apply shadings and such */

					#define TRANSFORM_RGB(expr) \
						do { \
							Uint8 x; \
							x = rgb.r; rgb.r = (expr); \
							x = rgb.g; rgb.g = (expr); \
							x = rgb.b; rgb.b = (expr); \
						} while (0)

#ifdef FEAT_FULLCHUNK

#define LIGHT_EXP1 60800
#define LIGHT_EXP2 64000

					if (map_flags & MAP_FLAG_LIGHTS)
					{
						int ly = y+1;
						if (ly >= CHUNK_YSIZE) ly = CHUNK_YSIZE-1;

						int lv_block = c->light_blocks[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2],
							lv_day = c->light_sky[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];

						if (ly & 1)
							lv_block >>= 4, lv_day >>= 4;
						else
							lv_block &= 0xf, lv_day &= 0xf;

						lv_day -= map_darken;
						if (lv_day < 0) lv_day = 0;
						Uint32 block_exp = LIGHT_EXP2 - map_darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

						Uint32 lf = 0x10000;

						for (int i = lv_block; i < 15; i++)
							lf = (lf*block_exp) >> 16;
						for (int i = lv_day; i < 15; i++)
							lf = (lf*LIGHT_EXP1) >> 16;

						TRANSFORM_RGB((x*lf) >> 16);
					}
#endif /* FEAT_FULLCHUNK */

					if (water(c->blocks[bx][bz][y]))
					{
						if (map_mode == MAP_MODE_TOPO)
							rgb = block_colors[0x08];

						jint h = y;
						while (--h)
							if (water(c->blocks[bx][bz][h]))
								TRANSFORM_RGB(x*7/8);
							else
								break;
					}

#undef TRANSFORM_RGB

					/* update bitmap */

					*p++ = pack_rgb(rgb);
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
	jint new_x = floor(x), new_y = floor(y), new_z = floor(z);

	if (new_x == player_x && new_y == player_y && new_z == player_z)
		return;

	player_dx = x;
	player_dy = y;
	player_dz = z;

	player_x = new_x;
	player_y = new_y;
	player_z = new_z;

	if (map_mode == MAP_MODE_CROSS && (map_flags & MAP_FLAG_FOLLOW_Y))
		map_update_alt(new_y, 0);
	else if (map_mode == MAP_MODE_SURFACE && (map_flags & MAP_FLAG_CHOP))
		map_update_ceiling();

	map_repaint();
}

void map_update_ceiling()
{	
	unsigned char *stack = world_stack(player_x, player_z, 0);
	jint old_ceiling_y = ceiling_y;
	if (stack && player_y >= 0 && player_y < CHUNK_YSIZE)
	{
		for (ceiling_y = player_y + 2; ceiling_y < CHUNK_YSIZE; ceiling_y++)
			if (!hollow(stack[ceiling_y]))
				break;
		if (ceiling_y != old_ceiling_y)
			map_update(map_min_x, map_max_x, map_min_z, map_max_z);
	}
}

void map_update_player_dir(double yaw, double pitch)
{
	int new_yaw = 0;

	yaw = fmod(yaw, 360.0);

	if (yaw < 0.0) yaw += 360.0;
	if (yaw > 360-22.5) yaw -= 360;

	while (new_yaw < 7 && yaw > 22.5)
		new_yaw++, yaw -= 45.0;

	if (new_yaw == player_yaw)
		return;

	player_yaw = new_yaw;

	map_repaint();
}

void map_update_alt(jint y, int relative)
{
	jint new_y = relative ? map_y + y : y;
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

void map_update_time(int daytime)
{
	/* daytime: 0 at sunrise, 12000 at sunset, 24000 on next sunrise.
	 * 12000 .. 13800 is dusk, 22200 .. 24000 is dawn */

	int darken = 0;

	if (daytime > 12000)
	{
		if (daytime < 13800)
			darken = (daytime-12000)/180;
		else if (daytime > 22200)
			darken = (24000-daytime)/180;
		else
			darken = 10;
	}

	if (map_darken != darken && (map_flags & MAP_FLAG_LIGHTS) && (map_flags & MAP_FLAG_NIGHT))
	{
		map_darken = darken;
		map_update(map_min_x, map_max_x, map_min_z, map_max_z);
		map_repaint();
	}
}

void map_setmode(enum map_mode mode, unsigned flags_on, unsigned flags_off, unsigned flags_toggle)
{
	static char *modenames[] = {
		[MAP_MODE_SURFACE] = "surface",
		[MAP_MODE_CROSS] = "cross-section",
		[MAP_MODE_TOPO] = "topographic",
	};

	enum map_mode old_mode = map_mode;
	unsigned old_flags = map_flags;

	if (mode != MAP_MODE_NOCHANGE)
		map_mode = mode;

	map_flags |= flags_on;
	map_flags &= ~flags_off;
	map_flags ^= flags_toggle;

	if (mode == MAP_MODE_CROSS && (old_mode != MAP_MODE_CROSS || (map_flags & MAP_FLAG_FOLLOW_Y)))
		map_y = player_y;

	if (map_mode != old_mode || map_flags != old_flags)
		tell("MODE: %s%s%s%s%s",
		     modenames[map_mode],
		     (mode == MAP_MODE_CROSS && map_flags & MAP_FLAG_FOLLOW_Y ? " (follow)" : ""),
		     (mode == MAP_MODE_SURFACE && map_flags & MAP_FLAG_CHOP ? " (chop)" : ""),
		     (map_flags & MAP_FLAG_LIGHTS ? " (lights)" : ""),
		     ((map_flags & MAP_FLAG_LIGHTS) && (map_flags & MAP_FLAG_NIGHT) ? " (night)" : ""));

	map_update(map_min_x, map_max_x, map_min_z, map_max_z);
}

void map_setscale(int scale, int relative)
{
	int s = relative ? map_scale + scale : scale;
	if (s < 1) s = 1;

	if (s == map_scale)
		return;

	map_scale = s;

	if (map_scale <= 5)
		map_scale_indicator = map_scale+2;
	else if (map_scale <= 9)
		map_scale_indicator = map_scale;
	else
		map_scale_indicator = map_scale-2;

	map_repaint();
}

/* screen-drawing related code */

void map_s2w(SDL_Surface *screen, int sx, int sy, jint *x, jint *z, jint *xo, jint *zo)
{
	/* Pixel screen->w/2 equals middle (rounded down) of block player_x.
	 * Pixel screen->w/2 - (map_scale-1)/2 equals left edge of block player_x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int px = screen->w/2 - (map_scale-1)/2;
	int py = screen->h/2 - (map_scale-1)/2;

	int dx = sx - px, dy = sy - py;

	dx = dx >= 0 ? dx/map_scale : (dx-(map_scale-1))/map_scale;
	dy = dy >= 0 ? dy/map_scale : (dy-(map_scale-1))/map_scale;

	*x = player_x + dx;
	*z = player_z + dy;

	if (xo) *xo = sx - (px + dx*map_scale);
	if (zo) *zo = sy - (py + dy*map_scale);
}

void map_w2s(SDL_Surface *screen, jint x, jint z, int *sx, int *sy)
{
	int px = screen->w/2 - (map_scale-1)/2;
	int py = screen->h/2 - (map_scale-1)/2;

	*sx = px + (x - player_x)*map_scale;
	*sy = py + (z - player_z)*map_scale;
}

static inline void map_draw_player_marker(SDL_Surface *screen)
{
	/* determine transform from player direction */

	int txx, txy, tyx, tyy;

	switch (player_yaw >> 1)
	{
	case 0: txx = 1,  txy = 0,  tyx = 0,  tyy = 1;  break;
	case 1: txx = 0,  txy = -1, tyx = 1,  tyy = 0;  break;
	case 2: txx = -1, txy = 0,  tyx = 0,  tyy = -1; break;
	case 3: txx = 0,  txy = 1,  tyx = -1, tyy = 0;  break;
	}

	int s = map_scale_indicator;

	int x0, y0;
	map_w2s(screen, player_x, player_z, &x0, &y0);
	x0 += (map_scale - s)/2;
	y0 += (map_scale - s)/2;

	if (txx < 0 || txy < 0) x0 += s-1;
	if (tyx < 0 || tyy < 0) y0 += s-1;

	/* mechanism for drawing points */

	SDL_LockSurface(screen);

	unsigned char *pixels = screen->pixels;
	Uint16 pitch = screen->pitch;

	void pt(int ix, int iy)
	{
		int sx = x0 + txx*ix + txy*iy;
		int sy = y0 + tyx*ix + tyy*iy;
		Uint32 *p = (Uint32 *)&pixels[sy*pitch + sx*4];
		*p = pack_rgb(special_colors[COLOR_PLAYER]);
	}

	/* draw the triangle shape */

	if (player_yaw & 1)
	{
		/* diagonal */
		for (int iy = 0; iy < s; iy++)
			for (int ix = 0; ix <= iy; ix++)
				pt(ix, iy);
	}
	else
	{
		/* cardinal */
		int gap = 0;
		for (int iy = s == 3 ? 1 : s/4; iy < s; iy++)
		{
			for (int ix = gap; ix < s-gap; ix++)
				pt(ix, iy);
			gap++;
		}
	}

	SDL_UnlockSurface(screen);
}

static void map_draw_entity_marker(struct entity *e, void *userdata)
{
	SDL_Surface *screen = userdata;

	int ex, ey;
	map_w2s(screen, e->x, e->z, &ex, &ey);
	ex += (map_scale - map_scale_indicator)/2;
	ey += (map_scale - map_scale_indicator)/2;

	if (ex < 0 || ey < 0 || ex+map_scale_indicator > screen->w || ey+map_scale_indicator > screen->h)
		return;

	SDL_Rect r = { .x = ex, .y = ey, .w = map_scale_indicator, .h = map_scale_indicator };
	SDL_FillRect(screen, &r, pack_rgb(special_colors[COLOR_PLAYER]));
}

void map_draw(SDL_Surface *screen)
{
	/* clear the window */

	SDL_Rect rect_screen = { .x = 0, .y = 0, .w = screen->w, .h = screen->h };
	SDL_FillRect(screen, &rect_screen, 0);

	/* draw the map */

	g_mutex_lock(map_mutex);

	jint scr_x0, scr_z0;
	jint scr_xo, scr_zo;
	map_s2w(screen, 0, 0, &scr_x0, &scr_z0, &scr_xo, &scr_zo);

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

		jint m_x0 = scr_x0 - map_min_x*CHUNK_XSIZE;
		jint m_z = scr_z0 - map_min_z*CHUNK_ZSIZE;

		jint m_xo0 = scr_xo, m_zo = scr_zo;

		for (int s_y = 0; s_y < screen->h && m_z < map->h; s_y++)
		{
			if (m_z >= 0)
			{
				jint m_x = m_x0, m_xo = m_xo0;
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

	map_draw_player_marker(screen);
	world_entities(map_draw_entity_marker, screen);

	/* update screen buffers */

	SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
}
