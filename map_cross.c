#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *state)
{
	return g_strdup("cross-section");
}

static bool handle_key(void *state, SDL_KeyboardEvent *e)
{
	return false;
}

static void update_player_pos(void *state)
{
	map_update_all();
}

static jint mapped_y(void *state, struct chunk *c, jint bx, jint bz)
{
	jint y = player_pos.y;
	if (y < 0)
		return 0;
	else if (y >= CHUNK_YSIZE)
		return CHUNK_YSIZE - 1;
	else
		return y;
}

static rgba_t block_color(void *state, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	return block_colors[b[y]];
}

struct map_mode *map_init_cross_mode()
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
