#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "block.h"
#include "protocol.h"
#include "map.h"

static char *describe(void *data, GPtrArray *attribs)
{
	return "topographic";
}

static bool handle_key(void *data, SDL_KeyboardEvent *e)
{
	return false;
}

static void update_player_pos(void *data)
{
	return;
}

static void update_time(void *data)
{
	return;
}

static jint mapped_y(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz)
{
	return c->height[bx][bz];
}

static rgba_t block_color(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	if (IS_WATER(b[y]))
		return map_water_color(c, block_colors[0x08], bx, bz, y);
	else if (y < 64)
		return RGB(4*y, 4*y, 0);
	else
		return RGB(255, 255-4*(y-64), 0);
}

struct map_mode *map_init_topo_mode(void)
{
	struct flat_mode flat_mode;
	flat_mode.data = NULL;
	flat_mode.describe = describe;
	flat_mode.handle_key = handle_key;
	flat_mode.update_player_pos = update_player_pos;
	flat_mode.update_time = update_time;
	flat_mode.mapped_y = mapped_y;
	flat_mode.block_color = block_color;
	return map_init_flat_mode(flat_mode);
}
