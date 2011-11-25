#ifndef MCMAP_MAP_H
#define MCMAP_MAP_H 1

#include <SDL_ttf.h>
#include "world.h"
#include "types.h"

#define MCMAP_EVENT_REPAINT SDL_USEREVENT

enum map_mode
{
	MAP_MODE_NOCHANGE,
	MAP_MODE_SURFACE,
	MAP_MODE_CROSS,
	MAP_MODE_TOPO,
	MAP_MODE_ISOMETRIC
};

#define MAP_FLAG_FOLLOW_Y 0x01
#define MAP_FLAG_LIGHTS 0x02
#define MAP_FLAG_NIGHT 0x04
#define MAP_FLAG_CHOP 0x08

extern enum map_mode map_mode;
extern unsigned map_flags;

extern rgba_t block_colors[256];

extern GHashTable *regions;

extern TTF_Font *map_font;
extern int map_w, map_h;
extern bool map_focused;
extern double player_dx, player_dy, player_dz;
extern coord3_t player_pos;
extern jshort player_health;

void map_init(SDL_Surface *screen);

void map_update(coord_t c1, coord_t c2);

void map_update_player_pos(double x, double y, double z);
void map_update_ceiling(void);
void map_update_player_dir(double yaw);
void map_update_player_id(jint id);

void map_update_alt(jint y, int relative);

void map_update_time(int daytime);

void map_setmode(enum map_mode mode, unsigned flags_on, unsigned flags_off, unsigned flags_toggle);
void map_setscale(int scale, int relative);

coord_t map_s2w(int sx, int sy, jint *xo, jint *zo);
void map_w2s(coord_t cc, int *sx, int *sy);

void map_repaint(void);

void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
