#include <math.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include <glib.h>

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

struct rgba block_colors[256];

// TODO: Move this out, make it configurable
static struct rgba special_colors[COLOR_MAX_SPECIAL] = {
	[COLOR_PLAYER] = {255, 0, 255, 255},
	[COLOR_UNLOADED] = {16, 16, 16, 255},
};

/* map graphics code */

struct map_region
{
	struct coord key;
	SDL_Surface *surf;
	int dirty_flag;
	BITSET(dirty_chunk, REGION_SIZE*REGION_SIZE);
};

double player_dx = 0.0, player_dy = 0.0, player_dz = 0.0;
jint player_x = 0, player_y = 0, player_z = 0;
jshort player_health = 0;

jint ceiling_y = CHUNK_YSIZE;

GHashTable *regions = 0;
TTF_Font *map_font = 0;
SDL_PixelFormat *screen_fmt = 0;
gboolean map_focused = TRUE;
jint map_w = 0, map_h = 0;
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

static void map_destroy_region(gpointer gp);

void map_init(SDL_Surface *screen)
{
	screen_fmt = screen->format;
	map_rshift = screen_fmt->Rshift;
	map_gshift = screen_fmt->Gshift;
	map_bshift = screen_fmt->Bshift;
	regions = g_hash_table_new_full(coord_hash, coord_equal, 0, map_destroy_region);
	map_mutex = g_mutex_new();
	map_flags |= MAP_FLAG_CHOP;
#ifdef FEAT_FULLCHUNK
	map_flags |= MAP_FLAG_LIGHTS;
#endif
}

static struct map_region *map_create_region(struct coord cc)
{
	struct map_region *region = g_new(struct map_region, 1);

	region->key = cc;
	region->surf = SDL_CreateRGBSurface(SDL_SWSURFACE, REGION_XSIZE, REGION_ZSIZE, 32,
	                                    screen_fmt->Rmask, screen_fmt->Gmask, screen_fmt->Bmask, 0);
	if (!region->surf)
		dief("SDL map surface init: %s", SDL_GetError());

	g_hash_table_insert(regions, &region->key, region);

	SDL_LockSurface(region->surf);
	SDL_Rect r = { .x = 0, .y = 0, .w = REGION_XSIZE, .h = REGION_ZSIZE };
	SDL_FillRect(region->surf, &r, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_UNLOADED])));
	SDL_UnlockSurface(region->surf);

	return region;
}

static void map_destroy_region(gpointer rp)
{
	struct map_region *region = rp;
	SDL_FreeSurface(region->surf);
	g_free(rp);
}

static struct map_region *map_get_region(struct coord cc, unsigned create)
{
	cc.x = REGION_IDX(cc.x);
	cc.z = REGION_IDX(cc.z);
	struct map_region *region = g_hash_table_lookup(regions, &cc);
	return region ? region : (create ? map_create_region(cc) : 0);
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

// FIXME: Should we transform alpha too?
#define TRANSFORM_RGB(expr) \
	do { \
		Uint8 x; \
		x = rgba.r; rgba.r = (expr); \
		x = rgba.g; rgba.g = (expr); \
		x = rgba.b; rgba.b = (expr); \
	} while (0)

static struct rgba map_water_color(struct chunk *c, struct rgba rgba, jint bx, jint bz, Uint32 y)
{
	while (--y && IS_WATER(c->blocks[bx][bz][y]))
		TRANSFORM_RGB(x*7/8);
	return rgba;
}

static struct rgb map_block_color(struct chunk *c, unsigned char *b, jint bx, jint bz, Uint32 y)
{
	struct rgba rgba = block_colors[b[y]];

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
		Uint32 block_exp = LIGHT_EXP2 - map_darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

		Uint32 lf = 0x10000;

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

	if (rgba.a == 255 || y == 1)
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
	struct rgb below = map_block_color(c, b, bx, bz, below_y);

	return RGB((below.r * (255 - rgba.a) + rgba.r * rgba.a)/255,
	           (below.g * (255 - rgba.a) + rgba.g * rgba.a)/255,
	           (below.b * (255 - rgba.a) + rgba.b * rgba.a)/255);
}

static void map_paint_chunk(SDL_Surface *region, jint cx, jint cz)
{
	jint cxo = REGION_OFF(cx);
	jint czo = REGION_OFF(cz);

	struct coord cc = { .x = cx, .z = cz };
	struct chunk *c = world_chunk(&cc, 0);

	if (!c)
		return;

	SDL_LockSurface(region);
	Uint32 pitch = region->pitch;

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
				if (IS_WATER(c->blocks[bx][bz][y]))
					rgb = IGNORE_ALPHA(map_water_color(c, block_colors[0x08], bx, bz, y));
				else if (v < 64)
					rgb = RGB(4*v, 4*v, 0);
				else
					rgb = RGB(255, 255-4*(v-64), 0);
			}
			else if (map_mode == MAP_MODE_CROSS)
				rgb = IGNORE_ALPHA(block_colors[b[map_y]]);
			else if (map_mode == MAP_MODE_SURFACE)
			{
				if (map_flags & MAP_FLAG_CHOP && y >= ceiling_y)
				{
					y = ceiling_y - 1;
					while (IS_AIR(b[y]) && y > 1)
						y--;
				}
				rgb = map_block_color(c, b, bx, bz, y);
			}
			else
				wtff("invalid map_mode %d", map_mode);

			*p++ = pack_rgb(rgb);
			b += blocks_xpitch;
		}

		pixels += pitch;
		blocks += blocks_pitch;
	}

	SDL_UnlockSurface(region);
}

static void map_paint_region(struct map_region *region)
{
	jint cidx = 0;

	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (BITSET_TEST(region->dirty_chunk, cidx))
			{
				map_paint_chunk(region->surf,
				                region->key.x * REGION_SIZE + cx,
				                region->key.z * REGION_SIZE + cz);
				BITSET_CLEAR(region->dirty_chunk, cidx);
			}

			cidx++;
		}
	}

	region->dirty_flag = 0;
}

void map_update_chunk(jint cx, jint cz)
{
	struct coord cc = { .x = cx, .z = cz };
	struct chunk *c = world_chunk(&cc, 0);

	if (!c)
		return;

	struct map_region *region = map_get_region(cc, 1);
	region->dirty_flag = 1;
	BITSET_SET(region->dirty_chunk, REGION_OFF(cz)*REGION_SIZE + REGION_OFF(cx));
}

void map_update(jint x1, jint x2, jint z1, jint z2)
{
	g_mutex_lock(map_mutex);

	for (jint cz = z1; cz <= z2; cz++)
		for (jint cx = x1; cx <= x2; cx++)
			map_update_chunk(cx, cz);

	g_mutex_unlock(map_mutex);
	map_repaint();
}

static void map_update_all()
{
	GHashTableIter region_iter;
	struct map_region *region;

	g_hash_table_iter_init(&region_iter, regions);

	// FIXME all terrible
	while (g_hash_table_iter_next(&region_iter, NULL, (gpointer *) &region))
	{
		map_update_chunk(region->key.x, region->key.z);
	}
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
			if (!IS_HOLLOW(stack[ceiling_y]))
				break;
		if (ceiling_y != old_ceiling_y)
			map_update_all();
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
		     (mode == MAP_MODE_SURFACE && map_flags & MAP_FLAG_LIGHTS ? " (lights)" : ""),
		     (mode == MAP_MODE_SURFACE && (map_flags & MAP_FLAG_LIGHTS) && (map_flags & MAP_FLAG_NIGHT) ? " (night)" : ""));

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

void map_s2w(SDL_Surface *screen, int sx, int sy, jint *x, jint *z, jint *xo, jint *zo)
{
	/* Pixel map_w/2 equals middle (rounded down) of block player_x.
	 * Pixel map_w/2 - (map_scale-1)/2 equals left edge of block player_x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

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
	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

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
		// TODO: Handle alpha in surface mode
		*p = pack_rgb(IGNORE_ALPHA(special_colors[COLOR_PLAYER]));
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

	if (ex < 0 || ey < 0 || ex+map_scale_indicator > map_w || ey+map_scale_indicator > map_h)
		return;

	SDL_Rect r = { .x = ex, .y = ey, .w = map_scale_indicator, .h = map_scale_indicator };
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

	/* find top-left and bottom-right corners of screen in (sub)chunk coords */

	jint scr_x1, scr_z1;
	jint scr_x1o, scr_z1o;

	map_s2w(screen, 0, 0, &scr_x1, &scr_z1, &scr_x1o, &scr_z1o);

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

	/* find out which regions intersect with visible coords */

	jint reg_x1, reg_x2, reg_z1, reg_z2;

	reg_x1 = REGION_IDX(CHUNK_XIDX(scr_x1));
	reg_z1 = REGION_IDX(CHUNK_ZIDX(scr_z1));

	reg_x2 = REGION_IDX(CHUNK_XIDX(scr_x2 + CHUNK_XSIZE - 1) + REGION_SIZE);
	reg_z2 = REGION_IDX(CHUNK_ZIDX(scr_z2 + CHUNK_ZSIZE - 1) + REGION_SIZE);

	/* draw those regions */

	SDL_LockSurface(screen);

	for (jint reg_z = reg_z1; reg_z <= reg_z2; reg_z++)
	{
		for (jint reg_x = reg_x1; reg_x <= reg_x2; reg_x++)
		{
			/* get the region surface, paint it if dirty */

			struct coord rc = { .x = reg_x*REGION_SIZE, .z = reg_z*REGION_SIZE };
			struct map_region *region = map_get_region(rc, 0);

			if (!region)
				continue; /* nothing to draw */

			if (region->dirty_flag)
				map_paint_region(region);

			SDL_Surface *regs = region->surf;
			SDL_LockSurface(regs);

			/* try to find where to place the region */

			int reg_sx = (reg_x*REGION_XSIZE - scr_x1)*map_scale - scr_x1o;
			int reg_sy = (reg_z*REGION_ZSIZE - scr_z1)*map_scale - scr_z1o;

			for (int reg_py = 0; reg_py < REGION_ZSIZE; reg_py++)
			{
				int y0 = reg_sy + reg_py*map_scale;
				for (int y = y0; y < y0+map_scale && y < map_h; y++)
				{
					if (y < 0) continue;

					for (int reg_px = 0; reg_px < REGION_XSIZE; reg_px++)
					{
						int x0 = reg_sx + reg_px*map_scale;
						for (int x = x0; x < x0+map_scale && x < map_w; x++)
						{
							if (x < 0) continue;

							void *s = (unsigned char *)screen->pixels + y*screen->pitch + 4*x;
							void *m = (unsigned char *)regs->pixels + reg_py*regs->pitch + 4*reg_px;
							*(Uint32 *)s = *(Uint32 *)m;
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
	world_entities(map_draw_entity_marker, screen);

	SDL_Rect r = { .x = 0, .y = screen->h - 24, .w = screen->w, .h = 24 };
	SDL_FillRect(screen, &r, 0);

	jint hx, hz;
	if (map_focused)
	{
		int mx, my;
		SDL_GetMouseState(&mx, &my);
		if (my >= map_h) goto no_block_info;

		map_s2w(screen, mx, my, &hx, &hz, 0, 0);
	}
	else
	{
		hx = player_x;
		hz = player_z;
	}

	struct coord hcc = { .x = CHUNK_XIDX(hx), .z = CHUNK_ZIDX(hz) };
	struct chunk *hc = world_chunk(&hcc, 0);
	hc = world_chunk(&hcc, 0);
	if (!hc) goto no_block_info;

	jint hcx = CHUNK_XOFF(hx);
	jint hcz = CHUNK_ZOFF(hz);

	jint hcy;
	if (map_focused)
		hcy = map_mode == MAP_MODE_CROSS ? map_y : hc->height[hcx][hcz];
	else
		hcy = player_y;

	unsigned char block = hc->blocks[hcx][hcz][hcy];

	SDL_Color white = {255, 255, 255};
	SDL_Color black = {0, 0, 0};

	if (map_focused)
	{
		gchar *left_text;
		if (IS_WATER(block))
		{
			int depth = 1;
			jint h = hcy;
			// FIXME: Off-by-one when water goes into void? Also in corresponding map_update code.
			// I think not, since bottom bedrock is at y=1.
			while (--h && IS_WATER(hc->blocks[hcx][hcz][h]))
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

	gchar *right_text = g_strdup_printf("x: %-5d z: %-5d y: %-3d", hx, hz, hcy);
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
