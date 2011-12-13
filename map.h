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
	char *(*describe)(void *state);
	coord3_t (*s2w)(void *state, int sx, int sy);
	void (*w2s)(void *state, coord_t cc, int *sx, int *sy);
	bool (*handle_key)(void *state, SDL_KeyboardEvent *e);
	void (*update_player_pos)(void *state);
	void (*draw_map)(void *state, SDL_Surface *screen);
	void (*draw_player)(void *state, SDL_Surface *screen);
	void (*draw_entity)(void *state, SDL_Surface *screen, struct entity *e);
};

struct flat_mode
{
	void *state;
	char *(*describe)(void *state);
	void (*update_player_pos)(void *state);
	jint (*mapped_y)(void *state, struct chunk *c, jint bx, jint bz);
	rgba_t (*block_color)(void *state, struct chunk *c, unsigned char *b, jint bx, jint bz, jint y);
};

extern struct map_mode *map_mode;
extern struct map_mode *map_modes[256];

struct map_mode *map_init_flat_mode(struct flat_mode *flat_mode);
struct map_mode *map_init_surface_mode(void);
struct map_mode *map_init_cross_mode(void);
struct map_mode *map_init_topo_mode(void);

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

extern int map_scale;

uint32_t pack_rgb(rgba_t rgba);

void map_init(SDL_Surface *screen);

rgba_t map_water_color(struct chunk *c, rgba_t rgba, jint bx, jint bz, jint y);

bool map_zoom(int dscale);

struct map_region *map_get_region(coord_t cc, bool gen);

void map_update(coord_t c1, coord_t c2);
void map_update_all(void);

void map_update_player_pos(double x, double y, double z);
void map_update_player_dir(double yaw);

void map_set_mode(struct map_mode *mode);

void map_repaint(void);

void map_blit_scaled(SDL_Surface *dest, SDL_Surface *src, int sx, int sy, int sw, int sh, int scale);
void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
