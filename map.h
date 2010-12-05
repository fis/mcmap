#ifndef MCMAP_MAP_H
#define MCMAP_MAP_H 1

#include <SDL.h>

#define MCMAP_EVENT_REPAINT SDL_USEREVENT

void map_init(SDL_Surface *screen);

void map_update(int x1, int x2, int z1, int z2);

void map_draw(SDL_Surface *screen);

#endif /* MCMAP_MAP_H */
