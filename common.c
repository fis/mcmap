#include <unistd.h>
#include <math.h>
#include <SDL.h>

#include <glib.h>

#include "protocol.h"
#include "common.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

guint coord_hash(gconstpointer key)
{
	const struct coord *c = key;
	return c->x ^ ((c->z << 16) | (c->z >> 16));
}

gboolean coord_equal(gconstpointer a, gconstpointer b)
{
	const struct coord *ca = a, *cb = b;
	return COORD_EQUAL(*ca, *cb);
}

#ifdef FEAT_SAFE_TELEPORT
struct teleport_fall_data
{
	int x;
	int z;
};

static gpointer teleport_fall(gpointer data)
{
	struct teleport_fall_data *tfd = data;
	int x = tfd->x, z = tfd->z;

	packet_t *p;
	unsigned char *stack;

	g_free(tfd);
	tfd = 0;

	/* wait until we know where the ground is */

	stack = world_stack(x, z, 0);

	while (!stack)
	{

		p = packet_new(0, PACKET_PLAYER_MOVE,
		               x+0.5, 130.0, 131.62, z+0.5, 0);
		inject_to_client(p);

		usleep(50*1000);

		stack = world_stack(x, z, 0);
	}

	/* all done if fall is short enough, or on top of water */

	int surfh = 127;
	while (!stack[surfh] && surfh > 0)
		surfh--;

	if (surfh >= 125 ||
	    (surfh >= 2 &&
	     water(stack[surfh]) && water(stack[surfh-1]) && water(stack[surfh-2])))
	{
		return 0;
	}

	/* otherwise, move us down in a hacky manner */

	for (int y = 128; y > surfh+1; y--)
	{
		p = packet_new(0, PACKET_PLAYER_MOVE,
		               x+0.5, y+0.5, y+0.5+1.62, z+0.5, 0);
		inject_to_client(packet_dup(p));
		inject_to_server(p);

		p = packet_new(0, PACKET_PLAYER_MOVE,
		               x+0.5, (double)y, y+1.62, z+0.5, 1);
		inject_to_client(packet_dup(p));
		inject_to_server(p);

		usleep(200*1000);
	}

	return 0;
}
#endif

void teleport(int x, int z)
{
	if (!opt.nomap)
	{
		/* check that the sky is free */

		int px1 = floor(player_dx - 0.32);
		int px2 = floor(player_dx + 0.32);

		int pz1 = floor(player_dz - 0.32);
		int pz2 = floor(player_dz + 0.32);

		unsigned char *stacks[4] = {0, 0, 0, 0};

		stacks[0] = world_stack(px1, pz1, 0);
		unsigned nstacks = 1;

		if (px2 != px1)
			stacks[nstacks++] = world_stack(px2, pz1, 0);

		if (pz2 != pz1)
		{
			stacks[nstacks++] = world_stack(px1, pz2, 0);
			if (px2 != px1)
				stacks[nstacks++] = world_stack(px2, pz2, 0);
		}

		for (unsigned i = 0; i < nstacks; i++)
		{
			if (!stacks[i])
			{
				tell("//goto: impossible: jump from unloaded chunk");
				return;
			}
		}

		if (player_y == -999)
			tell("//goto: jumping from a minecart, who knows if this will work");
		else if (player_y < 0)
			tell("//goto: you're below level 0! this should never happen (y=%d)", player_y);

		for (int h = (player_y == -999 ? 0 : player_y + 1); h < CHUNK_YSIZE; h++)
		{
			for (unsigned i = 0; i < nstacks; i++)
			{
				if (!hollow(stacks[i][h]))
				{
					tell("//goto: blocked: the skies are not clear");
					return;
				}
			}
		}
	}

	/* inject jumping packets */

	packet_t *pjump1 = packet_new(0, PACKET_PLAYER_MOVE,
	                              player_x+0.5, 130.0, 131.62, player_z+0.5, 0);
	packet_t *pjump2 = packet_dup(pjump1);

	packet_t *pmove1 = packet_new(0, PACKET_PLAYER_MOVE,
	                              x+0.5, 130.0, 131.62, z+0.5, 0);
	packet_t *pmove2 = packet_dup(pmove1);

	inject_to_server(pjump1);
	inject_to_client(pjump2);

	inject_to_server(pmove1);
	inject_to_client(pmove2);

	tell("//goto: jumping to (%d,%d)", x, z);

	/* fake safe landing */

#ifdef FEAT_SAFE_TELEPORT
	struct teleport_fall_data *tfd = g_malloc(sizeof *tfd);
	tfd->x = x;
	tfd->z = z;
	g_thread_create(teleport_fall, tfd, FALSE, NULL);
#endif
}
