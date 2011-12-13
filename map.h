#ifndef MCMAP_MAP_H
#define MCMAP_MAP_H 1

#include <SDL_ttf.h>
#include "world.h"
#include "types.h"

#define MCMAP_EVENT_REPAINT SDL_USEREVENT

struct map_region
{
	coord_t key;
	SDL_Surface *surf;
	int dirty_flag;
	BITSET(dirty_chunk, REGION_SIZE*REGION_SIZE);
};

struct map_mode
{
	void *state;
	void *(*initialize)(void);
	coord3_t (*s2w)(void *state, int sx, int sy);
	void (*w2s)(void *state, coord_t cc, int *sx, int *sy);
	bool (*handle_key)(void *state, SDL_KeyboardEvent *e);
	bool (*handle_mouse)(void *state, SDL_MouseButtonEvent *e);
	void (*draw_map)(void *state, SDL_Surface *screen);
	void (*draw_player)(void *state, SDL_Surface *screen);
	void (*draw_entity)(void *state, SDL_Surface *screen, struct entity *e);
};

extern struct map_mode *map_mode;
extern struct map_mode map_mode_surface;

extern rgba_t block_colors[256];

enum special_color_names
{
	COLOR_PLAYER,
	COLOR_MOB,
	COLOR_PICKUP,
	COLOR_UNLOADED,
	COLOR_MAX_SPECIAL
};

rgba_t special_colors[COLOR_MAX_SPECIAL];

extern GHashTable *regions;
extern SDL_PixelFormat *screen_fmt;

extern TTF_Font *map_font;
extern int map_w, map_h;
extern bool map_focused;
extern double player_dx, player_dy, player_dz;
extern coord3_t player_pos;
extern int player_yaw;
extern jshort player_health;

uint32_t pack_rgb(rgba_t rgba);

void map_init(SDL_Surface *screen);

int map_compute_scale(int base_scale);

struct map_region *map_get_region(coord_t cc, bool gen);

void map_update(coord_t c1, coord_t c2);

void map_update_player_pos(double x, double y, double z);
void map_update_player_dir(double yaw);
void map_update_player_id(jint id);

void map_repaint(void);

void map_blit_scaled(SDL_Surface *dest, SDL_Surface *src, int sx, int sy, int sw, int sh, int scale);
void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
