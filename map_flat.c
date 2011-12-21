#include <stdbool.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "types.h"
#include "platform.h"
#include "console.h"
#include "protocol.h"
#include "world.h"
#include "map.h"

struct state
{
	struct flat_mode flat_mode;
	bool track_pickups;
	bool track_mobs;
};

static char *describe(void *data, GPtrArray *attribs)
{
	struct state *state = data;
	char *name = state->flat_mode.describe(state->flat_mode.data, attribs);
	if (state->track_pickups) g_ptr_array_add(attribs, "pickups");
	if (state->track_mobs) g_ptr_array_add(attribs, "mobs");
	return name;
}

static int indicator_scale(void)
{
	if (map_scale <= 5)
		return map_scale + 2;
	else if (map_scale <= 9)
		return map_scale;
	else
		return map_scale - 2;
}

static coord_t s2w_offset(int sx, int sy, jint *xo, jint *zo)
{
	/* Pixel map_w/2 equals middle (rounded down) of block player_pos.x.
	 * Pixel map_w/2 - (map_scale-1)/2 equals left edge of block player_pos.x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

	int dx = sx - px, dy = sy - py;

	dx = dx >= 0 ? dx/map_scale : (dx-(map_scale-1))/map_scale;
	dy = dy >= 0 ? dy/map_scale : (dy-(map_scale-1))/map_scale;

	*xo = sx - (px + dx*map_scale);
	*zo = sy - (py + dy*map_scale);

	return COORD(player_pos.x + dx, player_pos.z + dy);
}

static coord3_t s2w(void *data, int sx, int sy)
{
	struct state *state = data;

	jint xo, zo;
	coord_t cc = s2w_offset(sx, sy, &xo, &zo);

	jint y = -1;
	struct chunk *c = world_chunk(cc, false);
	if (c)
	{
		jint cx = CHUNK_XOFF(cc.x);
		jint cz = CHUNK_ZOFF(cc.z);
		y = state->flat_mode.mapped_y(state->flat_mode.data, c, c->blocks[cx][cz], cx, cz);
	}

	return COORD3(cc.x, y, cc.z);
}

static void w2s(void *data, coord_t cc, int *sx, int *sy)
{
	int px = map_w/2 - (map_scale-1)/2;
	int py = map_h/2 - (map_scale-1)/2;

	*sx = px + (cc.x - player_pos.x)*map_scale;
	*sy = py + (cc.z - player_pos.z)*map_scale;
}

static bool handle_key(void *data, SDL_KeyboardEvent *e)
{
	struct state *state = data;

	switch (e->keysym.unicode)
	{
	case 'p':
		state->track_pickups ^= true;
		map_mode_changed();
		return true;

	case 'm':
		state->track_mobs ^= true;
		map_mode_changed();
		return true;

	default:
		return state->flat_mode.handle_key(state->flat_mode.data, e);
	}
}

static void update_player_pos(void *data)
{
	struct state *state = data;
	state->flat_mode.update_player_pos(state->flat_mode.data);
}

static void update_time(void *data)
{
	struct state *state = data;
	state->flat_mode.update_time(state->flat_mode.data);
}

static void draw_player(void *data, SDL_Surface *screen)
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

	int s = indicator_scale();

	int x0, y0;
	w2s(data, coord3_xz(player_pos), &x0, &y0);
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
			/* TODO: Handle alpha in flat mode */ \
			*p = pack_rgb(ignore_alpha(special_colors[COLOR_PLAYER])); \
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

	if (e->type == ENTITY_MOB && !state->track_mobs)
		return;

	if (e->type == ENTITY_PICKUP && !state->track_pickups)
		return;

	rgba_t color;

	switch (e->type)
	{
	case ENTITY_PLAYER: color = special_colors[COLOR_PLAYER]; break;
	case ENTITY_MOB: color = special_colors[COLOR_MOB]; break;
	case ENTITY_PICKUP: color = special_colors[COLOR_PICKUP]; break;
	default: wtff("bad entity type: %d", e->type);
	}

	int s = indicator_scale();

	int ex, ez;
	w2s(data, e->pos, &ex, &ez);
	ex += (map_scale - s)/2;
	ez += (map_scale - s)/2;

	if (ex < 0 || ez < 0 || ex+s > map_w || ez+s > map_h)
		return;

	SDL_Rect r = { .x = ex, .y = ez, .w = s, .h = s };
	// TODO: handle alpha in surface mode
	SDL_FillRect(screen, &r, pack_rgb(ignore_alpha(color)));
}

static void paint_chunk(void *data, SDL_Surface *region, coord_t cc)
{
	struct state *state = data;

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

	unsigned blocks_xpitch = CHUNK_ZSIZE*blocks_pitch;

	for (jint bz = 0; bz < CHUNK_ZSIZE; bz++)
	{
		uint32_t *p = (uint32_t*)pixels;
		unsigned char *b = blocks;

		for (jint bx = 0; bx < CHUNK_XSIZE; bx++)
		{
			jint y = state->flat_mode.mapped_y(state->flat_mode.data, c, b, bx, bz);
			rgba_t rgba = state->flat_mode.block_color(state->flat_mode.data, c, b, bx, bz, y);
			*p++ = pack_rgb(rgba);
			b += blocks_xpitch;
		}

		pixels += pitch;
		blocks += blocks_pitch;
	}

	SDL_UnlockSurface(region);
}

static void paint_region(void *data, struct map_region *region)
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
		SDL_FillRect(region->surf, &r, pack_rgb(ignore_alpha(special_colors[COLOR_UNLOADED])));
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
				paint_chunk(data, region->surf, cc);
				BITSET_CLEAR(region->dirty_chunk, cidx);
			}

			cidx++;
		}
	}
}

static void draw_map(void *data, SDL_Surface *screen)
{
	/* locate the screen corners in (sub-)block coordinates */

	jint scr_x1, scr_z1;
	jint scr_x1o, scr_z1o;

	coord_t scr1 = s2w_offset(0, 0, &scr_x1o, &scr_z1o);
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
				paint_region(data, region);

			SDL_Surface *regs = region->surf;
			if (!regs)
				continue; /* hasn't been painted yet... */
			SDL_LockSurface(regs);

			/* try to find where to place the region */

			int reg_sx, reg_sy;

			reg_sx = (rc.x - scr_x1)*map_scale - scr_x1o;
			reg_sy = (rc.z - scr_z1)*map_scale - scr_z1o;

			map_blit_scaled(screen, regs, reg_sx, reg_sy, REGION_XSIZE, REGION_ZSIZE, map_scale);

			SDL_UnlockSurface(regs);
		}
	}

	SDL_UnlockSurface(screen);
}

struct map_mode *map_init_flat_mode(struct flat_mode flat_mode)
{
	struct state *state = g_new(struct state, 1);
	state->flat_mode = flat_mode;
	state->track_pickups = false;
	state->track_mobs = false;

	struct map_mode *mode = g_new(struct map_mode, 1);
	mode->data = state;
	mode->describe = describe;
	mode->s2w = s2w;
	mode->w2s = w2s;
	mode->handle_key = handle_key;
	mode->update_player_pos = update_player_pos;
	mode->update_time = update_time;
	mode->draw_map = draw_map;
	mode->draw_player = draw_player;
	mode->draw_entity = draw_entity;
	return mode;
}
