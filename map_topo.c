#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *state)
{
	return g_strdup("topographic");
}

static jint mapped_y(void *state, struct chunk *c, jint bx, jint bz)
{
	return c->height[bx][bz];
}

static rgba_t block_color(void *state, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	if (IS_WATER(b[y]))
		return map_water_color(c, block_colors[0x08], bx, bz, y);
	else if (y < 64)
		return RGB(4*y, 4*y, 0);
	else
		return RGB(255, 255-4*(y-64), 0);
}

struct map_mode *map_init_topo_mode()
{
	struct flat_mode *flat_mode = g_new(struct flat_mode, 1);
	flat_mode->state = 0;
	flat_mode->describe = describe;
	flat_mode->mapped_y = mapped_y;
	flat_mode->block_color = block_color;
	return map_init_flat_mode(flat_mode);
}
