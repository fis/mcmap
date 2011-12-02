#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "protocol.h"
#include "map.h"

struct state
{
	struct flat_mode flat_mode;
	bool chop;
	jint ceiling_y;
#ifdef FEAT_FULLCHUNK
	bool lights;
	bool lights_dynamic;
	int darken;
#endif
};

static void update_player_pos(void *data);
static void update_time(void *data);

static char *describe(void *data, GPtrArray *attribs)
{
	struct state *state = data;

#ifdef FEAT_FULLCHUNK
	if (state->lights)
	{
		if (state->lights_dynamic) g_ptr_array_add(attribs, "lights/dynamic");
		else g_ptr_array_add(attribs, "lights");
	}
#endif
	if (state->chop) g_ptr_array_add(attribs, "chop");

	return "surface";
}

static bool handle_key(void *data, SDL_KeyboardEvent *e)
{
	struct state *state = data;

	switch (e->keysym.unicode)
	{
#ifdef FEAT_FULLCHUNK
	case 'l':
		state->lights ^= true;
		map_update_all();
		map_mode_changed();
		return true;

	case 'd':
		if (state->lights)
		{
			state->lights_dynamic ^= true;
			if (!state->lights_dynamic)
				state->darken = 0;
		}
		else
		{
			state->lights = true;
			state->lights_dynamic = true;
		}
		update_time(data);
		map_update_all();
		map_mode_changed();
		return true;
#endif

	case 'c':
		state->chop ^= true;
		update_player_pos(data);
		map_update_all();
		map_mode_changed();
		return true;

	default:
		return false;
	}
}

static void update_player_pos(void *data)
{
	struct state *state = data;

	if (state->chop)
	{
		unsigned char *stack = world_stack(COORD3_XZ(player_pos), false);
		jint old_ceiling_y = state->ceiling_y;
		if (stack && player_pos.y >= 0 && player_pos.y < CHUNK_YSIZE)
		{
			for (state->ceiling_y = player_pos.y + 2; state->ceiling_y < CHUNK_YSIZE; state->ceiling_y++)
				if (!IS_HOLLOW(stack[state->ceiling_y]))
					break;
			if (state->ceiling_y != old_ceiling_y)
				map_update_all();
		}
	}
}

static void update_time(void *data)
{
	/* world_time: 0 at sunrise, 12000 at sunset, 24000 on next sunrise.
	 * 12000 .. 13800 is dusk, 22200 .. 24000 is dawn */

	struct state *state = data;

	if (state->lights && state->lights_dynamic)
	{
		int darken;

		if (world_time > 12000)
		{
			if (world_time < 13800)
				darken = (world_time-12000)/180;
			else if (world_time > 22200)
				darken = (24000-world_time)/180;
			else
				darken = 10;
		}
		else
			darken = 0;

		if (state->darken != darken)
		{
			state->darken = darken;
			map_update_all();
			map_repaint();
		}
	}
}

static jint mapped_y(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz)
{
	struct state *state = data;

	jint y = c->height[bx][bz];

	if (state->chop && y >= state->ceiling_y)
	{
		y = state->ceiling_y - 1;
		while (IS_AIR(b[y]) && y > 1)
			y--;
	}

	return y;
}

static rgba_t block_color(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	struct state *state = data;

	rgba_t rgba = block_colors[b[y]];

	/* apply shadings and such */

#ifdef FEAT_FULLCHUNK

#define LIGHT_EXP1 60800
#define LIGHT_EXP2 64000

	if (state->lights)
	{
		int ly = y+1;
		if (ly >= CHUNK_YSIZE) ly = CHUNK_YSIZE-1;

		int lv_block = c->light_blocks[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];
		int lv_day = c->light_sky[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];

		if (ly & 1)
			lv_block >>= 4, lv_day >>= 4;
		else
			lv_block &= 0xf, lv_day &= 0xf;

		lv_day -= state->darken;
		if (lv_day < 0) lv_day = 0;
		uint32_t block_exp = LIGHT_EXP2 - state->darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

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
	rgba_t below = block_color(state, c, b, bx, bz, below_y);

	return RGB((below.r * (255 - rgba.a) + rgba.r * rgba.a)/255,
	           (below.g * (255 - rgba.a) + rgba.g * rgba.a)/255,
	           (below.b * (255 - rgba.a) + rgba.b * rgba.a)/255);
}

struct map_mode *map_init_surface_mode()
{
	struct state *state = g_new(struct state, 1);
	state->chop = true;
	state->ceiling_y = CHUNK_YSIZE;
	state->lights = true;
	state->lights_dynamic = false;
	state->darken = 0;

	struct flat_mode flat_mode;
	flat_mode.data = state;
	flat_mode.describe = describe;
	flat_mode.handle_key = handle_key;
	flat_mode.update_player_pos = update_player_pos;
	flat_mode.update_time = update_time;
	flat_mode.mapped_y = mapped_y;
	flat_mode.block_color = block_color;
	return map_init_flat_mode(flat_mode);
}
