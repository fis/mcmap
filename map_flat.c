#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "console.h"
#include "protocol.h"
#include "map.h"

struct state {
	int scale;
};

static struct state state_surface = {
	.scale = 1,
};

static void *initialize_surface()
{
	return &state_surface;
}

static int indicator_scale(int scale)
{
	if (scale <= 5)
		return scale + 2;
	else if (scale <= 9)
		return scale;
	else
		return scale - 2;
}

static coord_t s2w_offset(struct state *state, int sx, int sy, jint *xo, jint *zo)
{
	/* Pixel map_w/2 equals middle (rounded down) of block player_pos.x.
	 * Pixel map_w/2 - (state->scale-1)/2 equals left edge of block player_pos.x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int px = map_w/2 - (state->scale-1)/2;
	int py = map_h/2 - (state->scale-1)/2;

	int dx = sx - px, dy = sy - py;

	dx = dx >= 0 ? dx/state->scale : (dx-(state->scale-1))/state->scale;
	dy = dy >= 0 ? dy/state->scale : (dy-(state->scale-1))/state->scale;

	*xo = sx - (px + dx*state->scale);
	*zo = sy - (py + dy*state->scale);

	return COORD(player_pos.x + dx, player_pos.z + dy);
}

static coord3_t s2w(void *data, int sx, int sy)
{
	jint xo, zo;
	coord_t cc = s2w_offset((struct state *) data, sx, sy, &xo, &zo);
	return COORD3(cc.x, world_getheight(cc), cc.z);
}

static void w2s(void *data, coord_t cc, int *sx, int *sy)
{
	struct state *state = data;
	int px = map_w/2 - (state->scale-1)/2;
	int py = map_h/2 - (state->scale-1)/2;

	*sx = px + (cc.x - player_pos.x)*state->scale;
	*sy = py + (cc.z - player_pos.z)*state->scale;
}

static void draw_player(void *data, SDL_Surface *screen)
{
	struct state *state = data;

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

	int s = indicator_scale(state->scale);

	int x0, y0;
	w2s(state, COORD3_XZ(player_pos), &x0, &y0);
	x0 += (state->scale - s)/2;
	y0 += (state->scale - s)/2;

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
			/* TODO: Handle alpha in flat mode */ \
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

static void draw_entity(void *data, SDL_Surface *screen, struct entity *e)
{
	struct state *state = data;

#if 0
	if (e->type == ENTITY_MOB && !(map_flags & MAP_FLAG_MOBS))
		return;

	if (e->type == ENTITY_PICKUP && !(map_flags & MAP_FLAG_PICKUPS))
		return;
#endif

	rgba_t color;

	switch (e->type)
	{
	case ENTITY_PLAYER: color = special_colors[COLOR_PLAYER]; break;
	case ENTITY_MOB: color = special_colors[COLOR_MOB]; break;
	case ENTITY_PICKUP: color = special_colors[COLOR_PICKUP]; break;
	default: wtff("bad entity type: %d", e->type);
	}

	int s = indicator_scale(state->scale);

	int ex, ez;
	map_mode->w2s(state, e->pos, &ex, &ez);
	ex += (state->scale - s)/2;
	ez += (state->scale - s)/2;

	if (ex < 0 || ez < 0 || ex+s > map_w || ez+s > map_h)
		return;

	SDL_Rect r = { .x = ex, .y = ez, .w = s, .h = s };
	// TODO: handle alpha in surface mode
	SDL_FillRect(screen, &r, pack_rgb(IGNORE_ALPHA(color)));
}

// FIXME: Should we transform alpha too?
#define TRANSFORM_RGB(expr) \
	do { \
		uint8_t x; \
		x = rgba.r; rgba.r = (expr); \
		x = rgba.g; rgba.g = (expr); \
		x = rgba.b; rgba.b = (expr); \
	} while (0)

static rgba_t water_color(struct chunk *c, rgba_t rgba, jint bx, jint bz, jint y)
{
	while (--y >= 0 && IS_WATER(c->blocks[bx][bz][y]))
		TRANSFORM_RGB(x*7/8);
	return rgba;
}

static rgba_t block_color(struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	rgba_t rgba = block_colors[b[y]];

	/* apply shadings and such */

	#ifdef FEAT_FULLCHUNK

	#define LIGHT_EXP1 60800
	#define LIGHT_EXP2 64000

#if 0
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
#endif

	#endif /* FEAT_FULLCHUNK */

	if (IS_WATER(b[y]))
		rgba = water_color(c, rgba, bx, bz, y);

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
	rgba_t below = block_color(c, b, bx, bz, below_y);

	return RGB((below.r * (255 - rgba.a) + rgba.r * rgba.a)/255,
	           (below.g * (255 - rgba.a) + rgba.g * rgba.a)/255,
	           (below.b * (255 - rgba.a) + rgba.b * rgba.a)/255);
}

static void paint_chunk(SDL_Surface *region, coord_t cc)
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

#if 0
	if (map_mode == MAP_MODE_TOPO)
	{
		blocks = &c->height[0][0];
		blocks_pitch = 1;
	}
#endif

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

#if 0
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
#endif
				rgba = block_color(c, b, bx, bz, y);
#if 0
			}
			else
				wtff("invalid map_mode %d", map_mode);
#endif

			*p++ = pack_rgb(rgba);
			b += blocks_xpitch;
		}

		pixels += pitch;
		blocks += blocks_pitch;
	}

	SDL_UnlockSurface(region);
}

static void paint_region(struct map_region *region)
{
	jint cidx = 0;

	/* make sure the region has a surface for painting */

	if (!region->surf)
	{
		region->surf = SDL_CreateRGBSurface(SDL_SWSURFACE, REGION_XSIZE, REGION_ZSIZE, 32,
		                                    screen_fmt->Rmask, screen_fmt->Gmask, screen_fmt->Bmask, 0);
		if (!region->surf)
			dief("SDL map surface init: %s", SDL_GetError());

		SDL_LockSurface(region->surf);
		SDL_Rect r = { .x = 0, .y = 0, .w = REGION_XSIZE, .h = REGION_ZSIZE };
		SDL_FillRect(region->surf, &r, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_UNLOADED])));
		SDL_UnlockSurface(region->surf);
	}

	/* paint all dirty chunks */

	region->dirty_flag = 0;

	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (BITSET_TEST(region->dirty_chunk, cidx))
			{
				coord_t cc;
				cc.x = region->key.x + (cx * CHUNK_XSIZE);
				cc.z = region->key.z + (cz * CHUNK_ZSIZE);
				paint_chunk(region->surf, cc);
				BITSET_CLEAR(region->dirty_chunk, cidx);
			}

			cidx++;
		}
	}
}

static void draw_map(void *data, SDL_Surface *screen)
{
	struct state *state = data;

	/* locate the screen corners in (sub-)block coordinates */

	jint scr_x1, scr_z1;
	jint scr_x1o, scr_z1o;

	coord_t scr1 = s2w_offset(state, 0, 0, &scr_x1o, &scr_z1o);
	scr_x1 = scr1.x;
	scr_z1 = scr1.z;

	jint scr_x2, scr_z2;
	jint scr_x2o, scr_z2o;

	scr_x2 = scr_x1;
	scr_z2 = scr_z1;

	scr_x2o = scr_x1o + map_w;
	scr_z2o = scr_z1o + map_h;

	scr_x2 += scr_x2o / state->scale;
	scr_z2 += scr_z2o / state->scale;

	scr_x2o = scr_x2 % state->scale;
	scr_z2o = scr_z2 % state->scale;

	/* find the range of regions that intersect with the screen */

	jint reg_x1, reg_x2, reg_z1, reg_z2;

	reg_x1 = REGION_XIDX(scr_x1);
	reg_z1 = REGION_ZIDX(scr_z1);

	reg_x2 = REGION_XIDX(scr_x2 + REGION_XSIZE - 1);
	reg_z2 = REGION_ZIDX(scr_z2 + REGION_ZSIZE - 1);

	/* draw those regions */

	SDL_LockSurface(screen);

	for (jint reg_z = reg_z1; reg_z <= reg_z2; reg_z++)
	{
		for (jint reg_x = reg_x1; reg_x <= reg_x2; reg_x++)
		{
			/* get the region flat, paint it if dirty */

			coord_t rc = COORD(reg_x * REGION_XSIZE, reg_z * REGION_ZSIZE);
			struct map_region *region = map_get_region(rc, false);

			if (!region)
				continue; /* nothing to draw */

			if (region->dirty_flag)
				paint_region(region);

			SDL_Surface *regs = region->surf;
			if (!regs)
				continue; /* hasn't been painted yet... */
			SDL_LockSurface(regs);

			/* try to find where to place the region */

			int reg_sx, reg_sy;

			reg_sx = (rc.x - scr_x1)*state->scale - scr_x1o;
			reg_sy = (rc.z - scr_z1)*state->scale - scr_z1o;

			map_blit_scaled(screen, regs, reg_sx, reg_sy, REGION_XSIZE, REGION_ZSIZE);

			SDL_UnlockSurface(regs);
		}
	}

	SDL_UnlockSurface(screen);
}

struct map_mode map_mode_surface = {
	.initialize = initialize_surface,
	.s2w = s2w,
	.w2s = w2s,
	.draw_map = draw_map,
	.draw_player = draw_player,
	.draw_entity = draw_entity,
};
