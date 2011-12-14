#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "protocol.h"
#include "map.h"

struct state
{
	struct flat_mode flat_mode;
#ifdef FEAT_FULLCHUNK
	bool lights;
#endif
	bool chop;
	jint ceiling_y;
};

static void update_player_pos(void *data);

static char *describe(void *data)
{
	struct state *state = data;

	GString *str = g_string_new("surface");

#ifdef FEAT_FULLCHUNK
	if (state->lights)
		g_string_append(str, " (lights)");
#endif

	if (state->chop)
		g_string_append(str, " (chop)");

	return g_string_free(str, false);
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
		// FIXME
		static int map_darken = 1;

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
	rgba_t below = block_color(state, c, b, bx, bz, below_y);

	return RGB((below.r * (255 - rgba.a) + rgba.r * rgba.a)/255,
	           (below.g * (255 - rgba.a) + rgba.g * rgba.a)/255,
	           (below.b * (255 - rgba.a) + rgba.b * rgba.a)/255);
}

struct map_mode *map_init_surface_mode()
{
	struct state *state = g_new(struct state, 1);
	state->flat_mode.mapped_y = mapped_y;
	state->flat_mode.block_color = block_color;
	state->lights = true;
	state->chop = true;
	state->ceiling_y = CHUNK_YSIZE;

	struct map_mode *mode = g_new(struct map_mode, 1);
	mode->state = state;
	mode->describe = describe;
	mode->handle_key = handle_key;
	mode->update_player_pos = update_player_pos;
	return map_init_flat_mode(mode);
}
