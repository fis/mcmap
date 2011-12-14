#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *state)
{
	return g_strdup("surface");
}

static bool handle_key(void *state, SDL_KeyboardEvent *e)
{
	return false;
}

static void update_player_pos(void *state)
{
	return;
}

static jint mapped_y(void *state, struct chunk *c, jint bx, jint bz)
{
	return c->height[bx][bz];
}

static rgba_t block_color(void *state, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
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
	struct flat_mode *flat_mode = g_new(struct flat_mode, 1);
	flat_mode->mapped_y = mapped_y;
	flat_mode->block_color = block_color;

	struct map_mode *mode = g_new(struct map_mode, 1);
	mode->state = flat_mode;
	mode->describe = describe;
	mode->handle_key = handle_key;
	mode->update_player_pos = update_player_pos;
	return map_init_flat_mode(mode);
}
