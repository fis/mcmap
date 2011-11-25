#include <math.h>
#include <stdlib.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "protocol.h"
#include "common.h"
#include "config.h"
#include "map.h"
#include "block.h"
#include "world.h"
#include "proxy.h"

/* color maps */

enum special_color_names
{
	COLOR_PLAYER,
	COLOR_UNLOADED,
	COLOR_MAX_SPECIAL
};

rgba_t block_colors[256];

// TODO: Move this out, make it configurable
static rgba_t special_colors[COLOR_MAX_SPECIAL] = {
	[COLOR_PLAYER] = {255, 0, 255, 255},
	[COLOR_UNLOADED] = {16, 16, 16, 255},
};

/* used for color-keyed blitting in isometric mode */
static rgba_t color_key = { .r = 255, .g = 0, .b = 128 };

/* map graphics code */

struct map_region
{
	coord_t key;
	SDL_Surface *surf;
	int dirty_flag;
	BITSET(dirty_chunk, REGION_SIZE*REGION_SIZE);
};

#define REGION_ISO_W (REGION_XSIZE + REGION_ZSIZE)
#define REGION_ISO_H (REGION_XSIZE + REGION_ZSIZE + CHUNK_YSIZE - 1)

double player_dx = 0.0, player_dy = 0.0, player_dz = 0.0;
coord3_t player_pos = { .x = 0, .y = 0, .z = 0 };
jshort player_health = 0;

jint ceiling_y = CHUNK_YSIZE;

GHashTable *regions = 0;
TTF_Font *map_font = 0;
SDL_PixelFormat *screen_fmt = 0;
bool map_focused = true;
jint map_w = 0, map_h = 0;
static jint map_y = 0;
static int map_darken = 0;
static unsigned map_rshift, map_gshift, map_bshift;

static int map_scale = 1;
static int map_scale_indicator = 3;

enum map_mode map_mode = MAP_MODE_SURFACE;
unsigned map_flags = 0;

static GMutex * volatile map_mutex = 0;

static int player_yaw = 0;

static inline uint32_t pack_rgb(rgba_t rgba)
{
	return (rgba.r << map_rshift) |
	       (rgba.g << map_gshift) |
	       (rgba.b << map_bshift);
}

static void map_destroy_region(gpointer gp);

void map_init(SDL_Surface *screen)
{
	screen_fmt = screen->format;
	map_rshift = screen_fmt->Rshift;
	map_gshift = screen_fmt->Gshift;
	map_bshift = screen_fmt->Bshift;
	regions = g_hash_table_new_full(coord_hash, coord_equal, 0, map_destroy_region);
	map_mutex = g_mutex_new();
	/* flags on by default: */
	map_flags |= MAP_FLAG_CHOP | MAP_FLAG_FOLLOW_Y;
#ifdef FEAT_FULLCHUNK
	map_flags |= MAP_FLAG_LIGHTS;
#endif
}

static struct map_region *map_create_region(coord_t rc)
{
	struct map_region *region = g_new(struct map_region, 1);

	region->key = COORD(REGION_XMASK(rc.x), REGION_ZMASK(rc.z));
	region->surf = 0;

	g_hash_table_insert(regions, &region->key, region);

	region->dirty_flag = 0;
	memset(region->dirty_chunk, 0, sizeof region->dirty_chunk);

	return region;
}

static void map_destroy_region(gpointer rp)
{
	struct map_region *region = rp;
	if (region->surf)
		SDL_FreeSurface(region->surf);
	g_free(rp);
}

static struct map_region *map_get_region(coord_t cc, bool gen)
{
	coord_t rc = COORD(REGION_XMASK(cc.x), REGION_ZMASK(cc.z));
	struct map_region *region = g_hash_table_lookup(regions, &rc);
	return region ? region : (gen ? map_create_region(rc) : 0);
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

// FIXME: Should we transform alpha too?
#define TRANSFORM_RGB(expr) \
	do { \
		uint8_t x; \
		x = rgba.r; rgba.r = (expr); \
		x = rgba.g; rgba.g = (expr); \
		x = rgba.b; rgba.b = (expr); \
	} while (0)

static rgba_t map_water_color(struct chunk *c, rgba_t rgba, jint bx, jint bz, jint y)
{
	while (--y >= 0 && IS_WATER(c->blocks[bx][bz][y]))
		TRANSFORM_RGB(x*7/8);
	return rgba;
}

static rgba_t map_block_color(struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	rgba_t rgba = block_colors[b[y]];

	/* apply shadings and such */

	#ifdef FEAT_FULLCHUNK

	#define LIGHT_EXP1 60800
	#define LIGHT_EXP2 64000

	if (map_flags & MAP_FLAG_LIGHTS)
	{
		int ly = y+1;
		if (ly >= CHUNK_YSIZE) ly = CHUNK_YSIZE-1;

		int lv_block = c->light_blocks[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];
		int lv_day = c->light_sky[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];

		if (ly & 1)
			lv_block >>= 4, lv_day >>= 4;
		else
			lv_block &= 0xf, lv_day &= 0xf;

		lv_day -= map_darken;
		if (lv_day < 0) lv_day = 0;
		uint32_t block_exp = LIGHT_EXP2 - map_darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

		uint32_t lf = 0x10000;

		for (int i = lv_block; i < 15; i++)
			lf = (lf*block_exp) >> 16;
		for (int i = lv_day; i < 15; i++)
			lf = (lf*LIGHT_EXP1) >> 16;

		TRANSFORM_RGB((x*lf) >> 16);
	}

	#endif /* FEAT_FULLCHUNK */

	if (IS_WATER(b[y]))
		rgba = map_water_color(c, rgba, bx, bz, y);

	/* alpha transform */

	if (rgba.a == 255 || y <= 1)
		return IGNORE_ALPHA(rgba);

	int below_y = y - 1;
	while (IS_AIR(b[below_y]) && below_y > 1)
	{
		below_y--;
		rgba.r = (block_colors[0x00].r * (255 - rgba.a) + rgba.r * rgba.a)/255;
		rgba.g = (block_colors[0x00].g * (255 - rgba.a) + rgba.r * rgba.a)/255;
		rgba.b = (block_colors[0x00].b * (255 - rgba.a) + rgba.r * rgba.a)/255;
	}

	// TODO: Stop going below when the colour stops changing
	rgba_t below = map_block_color(c, b, bx, bz, below_y);

	return RGB((below.r * (255 - rgba.a) + rgba.r * rgba.a)/255,
	           (below.g * (255 - rgba.a) + rgba.g * rgba.a)/255,
	           (below.b * (255 - rgba.a) + rgba.b * rgba.a)/255);
}

static void map_paint_chunk(SDL_Surface *region, coord_t cc)
{
	jint cxo = CHUNK_XIDX(REGION_XOFF(cc.x));
	jint czo = CHUNK_ZIDX(REGION_ZOFF(cc.z));

	struct chunk *c = world_chunk(cc, false);

	if (!c)
		return;

	SDL_LockSurface(region);
	uint32_t pitch = region->pitch;

	unsigned char *pixels = (unsigned char *)region->pixels + czo*CHUNK_ZSIZE*pitch + cxo*CHUNK_XSIZE*4;

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
		uint32_t *p = (uint32_t*)pixels;
		unsigned char *b = blocks;

		for (jint bx = 0; bx < CHUNK_XSIZE; bx++)
		{
			jint y = c->height[bx][bz];

			/* select basic color */

			rgba_t rgba;

			if (map_mode == MAP_MODE_TOPO)
			{
				uint32_t v = *b;
				if (IS_WATER(c->blocks[bx][bz][y]))
					rgba = map_water_color(c, block_colors[0x08], bx, bz, y);
				else if (v < 64)
					rgba = RGB(4*v, 4*v, 0);
				else
					rgba = RGB(255, 255-4*(v-64), 0);
			}
			else if (map_mode == MAP_MODE_CROSS)
				rgba = block_colors[b[map_y]];
			else if (map_mode == MAP_MODE_SURFACE)
			{
				if (map_flags & MAP_FLAG_CHOP && y >= ceiling_y)
				{
					y = ceiling_y - 1;
					while (IS_AIR(b[y]) && y > 1)
						y--;
				}
				rgba = map_block_color(c, b, bx, bz, y);
			}
			else
				wtff("invalid map_mode %d", map_mode);

			*p++ = pack_rgb(rgba);
			b += blocks_xpitch;
		}

		pixels += pitch;
		blocks += blocks_pitch;
	}

	SDL_UnlockSurface(region);
}

/* isometric mode painting */

static inline unsigned char iso_get_block(struct region *wreg, jint x, jint y, jint z)
{
	if (REGION_XIDX(x) != 0 || REGION_ZIDX(z) != 0)
		return 0;
	struct chunk *c = wreg->chunks[CHUNK_XIDX(x)][CHUNK_ZIDX(z)];
	return c ? c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y] : 0;
}

static inline rgba_t iso_apply_light(struct region *wreg, rgba_t c, jint x, jint y, jint z)
{
#ifdef FEAT_FULLCHUNK
	/* TODO remove code duplication and/or do lights differently... */

	int lv_block = 0x00, lv_day = 0xff;

	if (REGION_XIDX(x) == 0 && REGION_ZIDX(z) == 0)
	{
		struct chunk *ch = wreg->chunks[CHUNK_XIDX(x)][CHUNK_ZIDX(z)];
		if (ch)
		{
			jint bx = CHUNK_XOFF(x), bz = CHUNK_ZOFF(z);
			if (y >= CHUNK_YSIZE) y = CHUNK_YSIZE-1;
			lv_block = ch->light_blocks[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + y/2];
			lv_day = ch->light_sky[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + y/2];
		}
	}

	if (y & 1)
		lv_block >>= 4, lv_day >>= 4;
	else
		lv_block &= 0xf, lv_day &= 0xf;

	lv_day -= map_darken;
	if (lv_day < 0) lv_day = 0;
	uint32_t block_exp = LIGHT_EXP2 - map_darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

	uint32_t lf = 0x10000;

	for (int i = lv_block; i < 15; i++)
		lf = (lf*block_exp) >> 16;
	for (int i = lv_day; i < 15; i++)
		lf = (lf*LIGHT_EXP1) >> 16;

	c.r = (c.r*lf) >> 16;
	c.g = (c.g*lf) >> 16;
	c.b = (c.b*lf) >> 16;
#endif /* FEAT_FULLCHUNK */

	return c;
}

static void map_paint_region_iso(struct map_region *region)
{
	SDL_Surface *regs = region->surf;
	SDL_LockSurface(regs);

	struct region *wreg = world_region(region->key, false);
	if (!wreg)
	{
		SDL_UnlockSurface(regs);
		return;
	}

	/* block lighting handler */


	/* find the bounding box for dirty chunks */

	jint dirty_x1 = REGION_ISO_W, dirty_x2 = 0;
	jint dirty_y1 = REGION_ISO_H, dirty_y2 = 0;

	jint cidx = 0;
	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (BITSET_TEST(region->dirty_chunk, cidx))
			{
				jint dx1 = cx*CHUNK_XSIZE + cz*CHUNK_ZSIZE;
				jint dy1 = (REGION_SIZE-cx)*CHUNK_XSIZE + cz*CHUNK_ZSIZE;
				jint dx2 = dx1 + CHUNK_XSIZE + CHUNK_ZSIZE;
				jint dy2 = dy1 + CHUNK_XSIZE + CHUNK_YSIZE - 1;
				if (dx1 < dirty_x1) dirty_x1 = dx1;
				if (dx2 > dirty_x2) dirty_x2 = dx2;
				if (dy1 < dirty_y1) dirty_y1 = dy1;
				if (dy2 > dirty_y2) dirty_y2 = dy2;
			}

			cidx++;
		}
	}

	/* paint the region surface */

	for (jint y = dirty_y1; y < dirty_y2; y++)
	{
		for (jint x = dirty_x1; x < dirty_x2; x++)
		{
			/* probe blocks to find the visible ones */

			jint wy = (map_flags & MAP_FLAG_CHOP ? ceiling_y-1 : CHUNK_YSIZE-1);

			unsigned visible_blocks = 0;
			rgba_t visible_colors[16]; /* at most this many transparent blocks */
			int first_face = 0; /* 0 = left, 1 = right, 2 = top */

			while (wy >= 0 && visible_blocks < NELEMS(visible_colors))
			{
				/* convert screen coordinates to world coordinates */

				jint ny = y - (CHUNK_YSIZE - 1 - wy); /* depth offset */

				/* assumes arithmetic shift */
				jint wx = x - ny + REGION_XSIZE - 1;
				jint wz = (x + ny - REGION_XSIZE + 1) >> 1;

				/* consider first the top, then the front face */

				for (int face = 0; face < 2; face++)
				{
					unsigned char block = iso_get_block(wreg, wx>>1, wy, wz);

					if (!IS_AIR(block)) /* top surface visible */
					{
						/* calculate block color */
						rgba_t block_color = block_colors[block];
						if (IS_WATER(block))
							block_color = map_water_color(wreg->chunks[CHUNK_XIDX(wx>>1)][CHUNK_ZIDX(wz)],
							                              block_color, CHUNK_XOFF(wx>>1), CHUNK_ZOFF(wz), wy);
						if (map_flags & MAP_FLAG_LIGHTS)
						{
							int lx = face ? (wx>>1)-1+(wx&1) : wx>>1;
							int ly = face ? wy : wy+1;
							int lz = face ? wz+(wx&1) : wz;
							block_color = iso_apply_light(wreg, block_color, lx, ly, lz);
						}
						visible_colors[visible_blocks++] = block_color;
						if (visible_blocks == 1)
							first_face = face ? wx&1 : 2;
						if (block_colors[block].a == 255 || visible_blocks >= NELEMS(visible_colors))
							break;
					}

					/* move to the next face */

					wx++;
					wz = (x + ny - REGION_XSIZE) >> 1;
				}

				wy--;
			}

			/* construct the color of the pixel */

			rgba_t rgb = color_key;

			if (visible_blocks > 0)
			{
				visible_blocks--;
				rgb = IGNORE_ALPHA(visible_colors[visible_blocks]);
				while (visible_blocks > 0)
				{
					rgba_t n = visible_colors[--visible_blocks];
					rgb.r = (rgb.r * (255 - n.a) + n.r * n.a)/255;
					rgb.g = (rgb.g * (255 - n.a) + n.g * n.a)/255;
					rgb.b = (rgb.b * (255 - n.a) + n.b * n.a)/255;
				}
				unsigned mult = 10 + 3*first_face;
				rgb.r = (rgb.r * mult) >> 4;
				rgb.g = (rgb.g * mult) >> 4;
				rgb.b = (rgb.b * mult) >> 4;
			}

			/* dump map_scale pixels on screen */

			*(uint32_t*)((unsigned char *)regs->pixels + y*regs->pitch + 4*x) = pack_rgb(rgb);
		}
	}

	SDL_UnlockSurface(regs);
}

static void map_paint_region(struct map_region *region)
{
	jint cidx = 0;

	/* make sure the region has a surface for painting */

	if (!region->surf)
	{
		int w = map_mode == MAP_MODE_ISOMETRIC ? REGION_ISO_W : REGION_XSIZE;
		int h = map_mode == MAP_MODE_ISOMETRIC ? REGION_ISO_H : REGION_ZSIZE;

		region->surf = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32,
		                                    screen_fmt->Rmask, screen_fmt->Gmask, screen_fmt->Bmask, 0);
		if (!region->surf)
			dief("SDL map surface init: %s", SDL_GetError());

		SDL_LockSurface(region->surf);
		SDL_Rect r = { .x = 0, .y = 0, .w = w, .h = h };
		SDL_FillRect(region->surf, &r, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_UNLOADED])));
		SDL_UnlockSurface(region->surf);
	}

	/* paint all dirty chunks */

	region->dirty_flag = 0;

	if (map_mode == MAP_MODE_ISOMETRIC)
	{
		map_paint_region_iso(region);
		memset(region->dirty_chunk, 0, sizeof region->dirty_chunk);
		return;
	}

	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (BITSET_TEST(region->dirty_chunk, cidx))
			{
				coord_t cc;
				cc.x = region->key.x + (cx * CHUNK_XSIZE);
				cc.z = region->key.z + (cz * CHUNK_ZSIZE);
				map_paint_chunk(region->surf, cc);
				BITSET_CLEAR(region->dirty_chunk, cidx);
			}

			cidx++;
		}
	}
}

void map_update_chunk(coord_t cc)
{
	struct chunk *c = world_chunk(cc, false);

	if (!c)
		return;

	struct map_region *region = map_get_region(cc, true);
	region->dirty_flag = 1;
	BITSET_SET(region->dirty_chunk, CHUNK_ZIDX(REGION_ZOFF(cc.z))*REGION_SIZE + CHUNK_XIDX(REGION_XOFF(cc.x)));
}

void map_update_region(coord_t cc)
{
	struct region *r = world_region(cc, false);

	if (!r)
		return;

	struct map_region *region = map_get_region(cc, true);
	region->dirty_flag = 1;
	for (jint cz = 0; cz < REGION_SIZE; cz++)
		for (jint cx = 0; cx < REGION_SIZE; cx++)
			if (r->chunks[cx][cz])
				BITSET_SET(region->dirty_chunk, cz*REGION_SIZE+cx);
}

void map_update(coord_t c1, coord_t c2)
{
	if (!map_mutex)
		return; /* defensive code if called by world before map_init */

	g_mutex_lock(map_mutex);

	for (jint cz = c1.z; cz <= c2.z; cz += CHUNK_ZSIZE)
		for (jint cx = c1.x; cx <= c2.x; cx += CHUNK_XSIZE)
			map_update_chunk(COORD(cx, cz));

	g_mutex_unlock(map_mutex);
	map_repaint();
}

static void map_update_all()
{
	GHashTableIter region_iter;
	struct map_region *region;

	if (!map_mutex)
		return; /* defensive code if called by world before map_init */
	g_mutex_lock(map_mutex);

	g_hash_table_iter_init(&region_iter, regions);

	while (g_hash_table_iter_next(&region_iter, NULL, (gpointer *) &region))
	{
		map_update_region(region->key);
	}

	g_mutex_unlock(map_mutex);
}

void map_update_player_pos(double x, double y, double z)
{
	coord3_t new_pos = COORD3(floor(x), floor(y), floor(z));

	if (COORD3_EQUAL(player_pos, new_pos))
		return;

	player_dx = x;
	player_dy = y;
	player_dz = z;

	player_pos = new_pos;

	if (map_mode == MAP_MODE_CROSS && (map_flags & MAP_FLAG_FOLLOW_Y))
		map_update_alt(new_pos.y, 0);
	else if ((map_mode == MAP_MODE_SURFACE || map_mode == MAP_MODE_ISOMETRIC)
	         && (map_flags & MAP_FLAG_CHOP))
		map_update_ceiling();

	map_repaint();
}

void map_update_ceiling()
{
	unsigned char *stack = world_stack(COORD3_XZ(player_pos), false);
	jint old_ceiling_y = ceiling_y;
	if (stack && player_pos.y >= 0 && player_pos.y < CHUNK_YSIZE)
	{
		for (ceiling_y = player_pos.y + 2; ceiling_y < CHUNK_YSIZE; ceiling_y++)
			if (!IS_HOLLOW(stack[ceiling_y]))
				break;
		if (ceiling_y != old_ceiling_y)
			map_update_all();
	}
}

void map_update_player_dir(double yaw)
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
		map_update_all();
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
		map_update_all();
		map_repaint();
	}
}

void map_setmode(enum map_mode mode, unsigned flags_on, unsigned flags_off, unsigned flags_toggle)
{
	static char *modenames[] = {
		[MAP_MODE_SURFACE] = "surface",
		[MAP_MODE_CROSS] = "cross-section",
		[MAP_MODE_TOPO] = "topographic",
		[MAP_MODE_ISOMETRIC] = "isometric",
	};

	enum map_mode old_mode = map_mode;
	unsigned old_flags = map_flags;

	if (mode != MAP_MODE_NOCHANGE)
		map_mode = mode;

	map_flags |= flags_on;
	map_flags &= ~flags_off;
	map_flags ^= flags_toggle;

	if (mode == MAP_MODE_CROSS && (old_mode != MAP_MODE_CROSS || (map_flags & MAP_FLAG_FOLLOW_Y)))
		map_y = player_pos.y;

	if (map_mode != old_mode || map_flags != old_flags)
	{
		GString *modestr = g_string_new("MODE: ");
		g_string_append(modestr, modenames[map_mode]);

		switch (map_mode)
		{
		case MAP_MODE_SURFACE:
		case MAP_MODE_ISOMETRIC:
			if (map_flags & MAP_FLAG_CHOP)
				g_string_append(modestr, " (chop)");
			if ((map_flags & MAP_FLAG_LIGHTS) && (map_flags & MAP_FLAG_NIGHT))
				g_string_append(modestr, " (lights/night)");
			else if (map_flags & MAP_FLAG_LIGHTS)
				g_string_append(modestr, " (lights)");
			break;

		case MAP_MODE_CROSS:
			if (map_flags & MAP_FLAG_FOLLOW_Y)
				g_string_append(modestr, " (follow)");
			break;

		default:
			/* no applicable modes */
			break;
		}

		tell("%s", modestr->str);
		g_string_free(modestr, TRUE);
	}

	/* for isometric mode toggling, also drop all painted surfaces */

	if ((old_mode == MAP_MODE_ISOMETRIC) ^ (map_mode == MAP_MODE_ISOMETRIC))
	{
		GHashTableIter region_iter;
		struct map_region *region;

		g_mutex_lock(map_mutex);

		g_hash_table_iter_init(&region_iter, regions);

		while (g_hash_table_iter_next(&region_iter, NULL, (gpointer *) &region))
		{
			if (!region->surf) continue;
			SDL_FreeSurface(region->surf);
			region->surf = 0;
		}

		g_mutex_unlock(map_mutex);
	}

	/* mark everything dirty for next repaint */

	map_update_all();
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

coord_t map_s2w(int sx, int sy, jint *xo, jint *zo)
{
	/* Pixel map_w/2 equals middle (rounded down) of block player_pos.x.
	 * Pixel map_w/2 - (map_scale-1)/2 equals left edge of block player_pos.x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

	int dx = sx - px, dy = sy - py;

	dx = dx >= 0 ? dx/map_scale : (dx-(map_scale-1))/map_scale;
	dy = dy >= 0 ? dy/map_scale : (dy-(map_scale-1))/map_scale;

	if (xo) *xo = sx - (px + dx*map_scale);
	if (zo) *zo = sy - (py + dy*map_scale);

	return COORD(player_pos.x + dx, player_pos.z + dy);
}

void map_w2s(coord_t cc, int *sx, int *sy)
{
	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

	*sx = px + (cc.x - player_pos.x)*map_scale;
	*sy = py + (cc.z - player_pos.z)*map_scale;
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
	default: wtff("player_yaw = %d", player_yaw);
	}

	int s = map_scale_indicator;

	int x0, y0;
	map_w2s(COORD3_XZ(player_pos), &x0, &y0);
	x0 += (map_scale - s)/2;
	y0 += (map_scale - s)/2;

	if (txx < 0 || txy < 0) x0 += s-1;
	if (tyx < 0 || tyy < 0) y0 += s-1;

	/* mechanism for drawing points */

	SDL_LockSurface(screen);

	unsigned char *pixels = screen->pixels;
	uint16_t pitch = screen->pitch;

	#define PT(ix, iy) \
		do { \
			int sx = x0 + txx*ix + txy*iy; \
			int sy = y0 + tyx*ix + tyy*iy; \
			uint32_t *p = (uint32_t *)&pixels[sy*pitch + sx*4]; \
			/* TODO: Handle alpha in surface mode */ \
			*p = pack_rgb(IGNORE_ALPHA(special_colors[COLOR_PLAYER])); \
		} while (0)

	/* draw the triangle shape */

	if (player_yaw & 1)
	{
		/* diagonal */
		for (int iy = 0; iy < s; iy++)
			for (int ix = 0; ix <= iy; ix++)
				PT(ix, iy);
	}
	else
	{
		/* cardinal */
		int gap = 0;
		for (int iy = s == 3 ? 1 : s/4; iy < s; iy++)
		{
			for (int ix = gap; ix < s-gap; ix++)
				PT(ix, iy);
			gap++;
		}
	}

	#undef PT

	SDL_UnlockSurface(screen);
}

static void map_draw_entity_marker(void *idp, void *ep, void *userdata)
{
	struct entity *e = ep;
	SDL_Surface *screen = userdata;

	if (!e->name)
		return;

	int ex, ez;
	map_w2s(e->pos, &ex, &ez);
	ex += (map_scale - map_scale_indicator)/2;
	ez += (map_scale - map_scale_indicator)/2;

	if (ex < 0 || ez < 0 || ex+map_scale_indicator > map_w || ez+map_scale_indicator > map_h)
		return;

	SDL_Rect r = { .x = ex, .y = ez, .w = map_scale_indicator, .h = map_scale_indicator };
	// TODO: handle alpha in surface mode
	SDL_FillRect(screen, &r, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_PLAYER])));
}

void map_draw(SDL_Surface *screen)
{
	/* clear the window */

	SDL_Rect rect_screen = { .x = 0, .y = 0, .w = screen->w, .h = screen->h };
	SDL_FillRect(screen, &rect_screen, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_UNLOADED])));

	/* draw the map */

	g_mutex_lock(map_mutex);

	/* locate the screen corners in (sub-)block coordinates */

	jint scr_x1, scr_z1;
	jint scr_x1o, scr_z1o;

	coord_t scr1 = map_s2w(0, 0, &scr_x1o, &scr_z1o);
	scr_x1 = scr1.x;
	scr_z1 = scr1.z;

	jint scr_x2, scr_z2;
	jint scr_x2o, scr_z2o;

	scr_x2 = scr_x1;
	scr_z2 = scr_z1;

	scr_x2o = scr_x1o + map_w;
	scr_z2o = scr_z1o + map_h;

	scr_x2 += scr_x2o / map_scale;
	scr_z2 += scr_z2o / map_scale;

	scr_x2o = scr_x2 % map_scale;
	scr_z2o = scr_z2 % map_scale;

	/* find the range of regions that intersect with the screen */

	jint reg_x1, reg_x2, reg_z1, reg_z2;

	if (map_mode == MAP_MODE_ISOMETRIC)
	{
		/* TODO FIXME... */
		reg_x1 = REGION_XIDX(player_pos.x);
		reg_z1 = REGION_ZIDX(player_pos.z);
		reg_x2 = reg_x1;
		reg_z2 = reg_z1;
	}
	else
	{
		reg_x1 = REGION_XIDX(scr_x1);
		reg_z1 = REGION_ZIDX(scr_z1);

		reg_x2 = REGION_XIDX(scr_x2 + REGION_XSIZE - 1);
		reg_z2 = REGION_ZIDX(scr_z2 + REGION_ZSIZE - 1);
	}

	/* draw those regions */

	SDL_LockSurface(screen);

	for (jint reg_z = reg_z1; reg_z <= reg_z2; reg_z++)
	{
		for (jint reg_x = reg_x1; reg_x <= reg_x2; reg_x++)
		{
			/* get the region surface, paint it if dirty */

			coord_t rc = COORD(reg_x * REGION_XSIZE, reg_z * REGION_ZSIZE);
			struct map_region *region = map_get_region(rc, false);

			if (!region)
				continue; /* nothing to draw */

			if (region->dirty_flag)
				map_paint_region(region);

			SDL_Surface *regs = region->surf;
			if (!regs)
				continue; /* hasn't been painted yet... */
			SDL_LockSurface(regs);

			/* try to find where to place the region */

			int reg_sx, reg_sy, reg_sw, reg_sh;

			if (map_mode == MAP_MODE_ISOMETRIC)
			{
				int px = (player_pos.x - rc.x)*map_scale;
				int py = (CHUNK_YSIZE-1 - player_pos.y)*map_scale;
				int pz = (player_pos.z - rc.z)*map_scale;
				reg_sx = map_w / 2 - px - pz;
				reg_sy = map_h / 2 - (REGION_XSIZE-1)*map_scale + px - pz - py;
				reg_sw = REGION_ISO_W;
				reg_sh = REGION_ISO_H;
			}
			else
			{
				reg_sx = (rc.x - scr_x1)*map_scale - scr_x1o;
				reg_sy = (rc.z - scr_z1)*map_scale - scr_z1o;
				reg_sw = REGION_XSIZE;
				reg_sh = REGION_ZSIZE;
			}

			/* scaled, color-keyed blit */

			uint32_t ckey = pack_rgb(color_key);

			for (int reg_py = 0; reg_py < reg_sh; reg_py++)
			{
				int y0 = reg_sy + reg_py*map_scale;
				for (int y = y0; y < y0+map_scale && y < map_h; y++)
				{
					if (y < 0) continue;

					for (int reg_px = 0; reg_px < reg_sw; reg_px++)
					{
						int x0 = reg_sx + reg_px*map_scale;
						for (int x = x0; x < x0+map_scale && x < map_w; x++)
						{
							if (x < 0) continue;

							void *s = (unsigned char *)screen->pixels + y*screen->pitch + 4*x;
							void *m = (unsigned char *)regs->pixels + reg_py*regs->pitch + 4*reg_px;
							uint32_t c = *(uint32_t *)m;
							if (c != ckey) *(uint32_t *)s = c;
						}
					}
				}
			}

			SDL_UnlockSurface(regs);
		}
	}

	SDL_UnlockSurface(screen);

	g_mutex_unlock(map_mutex);

	/* player indicators and such */

	map_draw_player_marker(screen);
	
	g_mutex_lock(entity_mutex);
	g_hash_table_foreach(world_entities, map_draw_entity_marker, screen);
	g_mutex_unlock(entity_mutex);

	/* the status bar */

	SDL_Rect r = { .x = 0, .y = screen->h - 24, .w = screen->w, .h = 24 };
	SDL_FillRect(screen, &r, 0);

	coord_t hcc;
	if (map_focused)
	{
		int mx, my;
		SDL_GetMouseState(&mx, &my);
		if (my >= map_h) goto no_block_info;

		hcc = map_s2w(mx, my, 0, 0);
	}
	else
	{
		hcc = COORD3_XZ(player_pos);
	}

	struct chunk *hc = world_chunk(hcc, false);
	if (!hc) goto no_block_info;

	jint hcx = CHUNK_XOFF(hcc.x);
	jint hcz = CHUNK_ZOFF(hcc.z);

	jint hcy;
	if (map_focused)
		hcy = map_mode == MAP_MODE_CROSS ? map_y : hc->height[hcx][hcz];
	else
		hcy = player_pos.y;

	unsigned char block = hc->blocks[hcx][hcz][hcy];

	SDL_Color white = {255, 255, 255};
	SDL_Color black = {0, 0, 0};

	if (map_focused)
	{
		char *left_text;
		if (IS_WATER(block))
		{
			int depth = 1;
			jint h = hcy;
			while (--h >= 0 && IS_WATER(hc->blocks[hcx][hcz][h]))
				depth++;
			left_text = g_strdup_printf("water (%d deep)", depth);
		}
		else
			left_text = g_strdup_printf("%s", block_info[block].name);

		SDL_Surface *left_surface = TTF_RenderText_Shaded(map_font, left_text, white, black);
		SDL_Rect left_src = { .x = 0, .y = 0, .w = left_surface->w, .h = left_surface->h };
		SDL_Rect left_dst = { .x = 4, .y = screen->h - 22, .w = left_src.w, .h = left_src.h };
		SDL_BlitSurface(left_surface, &left_src, screen, &left_dst);
		g_free(left_text);
	}

	char *right_text = g_strdup_printf("x: %-5d z: %-5d y: %-3d", hcc.x, hcc.z, hcy);
	SDL_Surface *right_surface = TTF_RenderText_Shaded(map_font, right_text, white, black);
	SDL_Rect right_src = { .x = 0, .y = 0, .w = right_surface->w, .h = right_surface->h };
	int right_offset_width;
	TTF_SizeText(map_font, "x: 00000, z: 00000, y: 000", &right_offset_width, NULL);
	SDL_Rect right_dst = { .x = screen->w - 4 - right_offset_width, .y = screen->h - 22, .w = right_src.w, .h = right_src.h };
	SDL_BlitSurface(right_surface, &right_src, screen, &right_dst);
	g_free(right_text);

	/* update screen buffers */

no_block_info:
	SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
}
