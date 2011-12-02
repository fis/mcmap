#include <glib.h>
#include <SDL.h>

#include "common.h"
#include "protocol.h"
#include "map.h"

struct state
{
	bool follow_y;
	jint y;
};

static void update_player_pos(void *data);

static char *describe(void *data, GPtrArray *attribs)
{
	struct state *state = data;
	if (state->follow_y) g_ptr_array_add(attribs, "follow");
	return "cross-section";
}

static bool update_alt(struct state *state, jint y)
{
	if (y < 0)
		y = 0;
	else if (y >= CHUNK_YSIZE)
		y = CHUNK_YSIZE - 1;

	if (y != state->y)
	{
		state->y = y;
		map_update_all();
		return true;
	}

	return false;
}

static bool handle_key(void *data, SDL_KeyboardEvent *e)
{
	struct state *state = data;

	switch (e->keysym.unicode)
	{
	case 'f':
		state->follow_y ^= true;
		update_player_pos(data);
		map_mode_changed();
		return false;
	}

	switch (e->keysym.sym)
	{
	case SDLK_UP:
		return update_alt(state, state->y + 1);

	case SDLK_DOWN:
		return update_alt(state, state->y - 1);

	default:
		return false;
	}
}

static void update_player_pos(void *data)
{
	struct state *state = data;

	if (state->follow_y)
	{
		update_alt(state, player_pos.y);
		map_repaint();
	}
}

static void update_time(void *data)
{
	return;
}

static jint mapped_y(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz)
{
	struct state *state = data;
	return state->y;
}

static rgba_t block_color(void *data, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y)
{
	return block_colors[b[y]];
}

struct map_mode *map_init_cross_mode()
{
	struct state *state = g_new(struct state, 1);
	state->follow_y = true;
	state->y = 0;

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
