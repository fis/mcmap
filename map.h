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
	MAP_MODE_TOPO
};

#define MAP_FLAG_FOLLOW_Y 0x01
#define MAP_FLAG_LIGHTS 0x02
#define MAP_FLAG_NIGHT 0x04
#define MAP_FLAG_CHOP 0x08

extern struct rgba block_colors[256];

extern GHashTable *regions;

extern TTF_Font *map_font;
extern int map_w, map_h;
extern gboolean map_focused;
extern double player_dx, player_dy, player_dz;
extern jint player_x, player_y, player_z;
extern jshort player_health;

void map_init(SDL_Surface *screen);

void map_update(struct coord c1, struct coord c2);

void map_update_player_pos(double x, double y, double z);
void map_update_ceiling(void);
void map_update_player_dir(double yaw, double pitch);
void map_update_player_id(jint id);

void map_update_alt(jint y, int relative);

void map_update_time(int daytime);

void map_setmode(enum map_mode mode, unsigned flags_on, unsigned flags_off, unsigned flags_toggle);
void map_setscale(int scale, int relative);

void map_s2w(SDL_Surface *screen, int sx, int sy, jint *x, jint *z, jint *xo, jint *zo);
void map_w2s(SDL_Surface *screen, jint x, jint z, int *sx, int *sy);

void map_repaint(void);

void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
