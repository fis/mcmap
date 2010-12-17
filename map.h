#ifndef MCMAP_MAP_H
#define MCMAP_MAP_H 1

#include <SDL.h>

#define MCMAP_EVENT_REPAINT SDL_USEREVENT

enum map_mode
{
	MAP_MODE_SURFACE,
	MAP_MODE_CROSS,
	MAP_MODE_TOPO
};

#define MAP_FLAG_FOLLOW_Y 0x01

void map_init(SDL_Surface *screen);

void map_update(int x1, int x2, int z1, int z2);

void map_update_player_pos(double x, double y, double z);
void map_update_player_dir(double yaw, double pitch);
void map_update_alt(int y, int relative);

void map_setmode(enum map_mode mode, unsigned flags);

void map_repaint(void);

void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
