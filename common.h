#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#include "platform.h"
#include "types.h"

#define NELEMS(array) (sizeof(array) / sizeof((array)[0]))

/* options */

struct options
{
	int localport;
	bool noansi;
	bool nomap;
	int scale;
	char *wndsize;
	char *jumpfile;
} opt;

/* teleportation */

GHashTable *jumps;

void teleport(coord_t cc);

#endif /* MCMAP_COMMON_H */
