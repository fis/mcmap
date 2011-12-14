#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *data, GPtrArray *attribs)
{
	return "cross-section";
}

static bool handle_key(void *data, SDL_KeyboardEvent *e)
{
	return false;
}

static void update_player_pos(void *data)
{
	map_update_all();
}

static jint mapped_y(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz)
{
	jint y = player_pos.y;
	if (y < 0)
		return 0;
	else if (y >= CHUNK_YSIZE)
		return CHUNK_YSIZE - 1;
	else
		return y;
}

static rgba_t block_color(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	return block_colors[b[y]];
}

struct map_mode *map_init_cross_mode()
{
	struct flat_mode flat_mode;
	flat_mode.data = NULL;
	flat_mode.describe = describe;
	flat_mode.handle_key = handle_key;
	flat_mode.update_player_pos = update_player_pos;
	flat_mode.mapped_y = mapped_y;
	flat_mode.block_color = block_color;
	return map_init_flat_mode(flat_mode);
}
