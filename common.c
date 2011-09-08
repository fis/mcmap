#include <unistd.h>
#include <math.h>
#include <SDL.h>

#include <glib.h>
#include <libguile.h>

#include "protocol.h"
#include "common.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

guint coord_hash(gconstpointer key)
{
	const coord_t *c = key;
	uint32_t x = c->x, z = c->z;
	return x ^ ((z << 16) | (z >> 16));
}

gboolean coord_equal(gconstpointer a, gconstpointer b)
{
	const coord_t *ca = a, *cb = b;
	return COORD_EQUAL(*ca, *cb);
}

void teleport(int x, int z)
{
	tell("Sorry, teleportation is currently out of order!");
}
