#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *state)
{
	return g_strdup("cross-section");
}

static jint mapped_y(void *state, struct chunk *c, jint bx, jint bz)
{
	return player_pos.y;
}

static rgba_t block_color(void *state, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	return block_colors[b[y]];
}

struct map_mode *map_init_cross_mode()
{
	struct flat_mode *flat_mode = g_new(struct flat_mode, 1);
	flat_mode->state = 0;
	flat_mode->describe = describe;
	flat_mode->mapped_y = mapped_y;
	flat_mode->block_color = block_color;
	return map_init_flat_mode(flat_mode);
}
