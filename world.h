#ifndef MCMAP_WORLD_H
#define MCMAP_WORLD_H 1

#include <glib.h>

void world_init(void);

gpointer world_thread(gpointer data);

#endif /* MCMAP_WORLD_H */
