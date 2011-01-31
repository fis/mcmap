#include <unistd.h>
#include <math.h>
#include <SDL.h>

#include <glib.h>

#include "protocol.h"
#include "common.h"
#include "map.h"
#include "world.h"

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

	stack = world_stack(x, z, 0);

	if (!stack)
	{
		/* put in the stepping stone */

		p = packet_new(PACKET_TO_ANY, PACKET_SET_BLOCK,
		               x, 127, z, 3, 0);
		inject_to_client(p);

		/* wait for the world to materalize, if necessary */

		while (!stack)
		{
			usleep(100*1000);
			stack = world_stack(x, z, 0);
		}

		/* remove the stepping stone */

		p = packet_new(PACKET_TO_ANY, PACKET_SET_BLOCK,
		               x, 127, z, stack[127], 0); /* TODO: fix metadata */
		inject_to_client(p);
	}

	/* all done if fall is short enough, or on top of water */

	int surfh = 127;
	while (!stack[surfh] && surfh > 0)
		surfh--;

	if (surfh >= 125 ||
	    (surfh >= 2 &&
	     water(stack[surfh]) && water(stack[surfh-1]) && water(stack[surfh-2])))
		return 0;

	/* otherwise, put in some water to stop the fall */

	for (int y = surfh+1; y <= surfh+3; y++)
	{
		p = packet_new(PACKET_TO_ANY, PACKET_SET_BLOCK,
		               x, y, z, 9, 8);
		inject_to_client(p);
	}

	/* wait for up to 30.0 seconds for the player to reach it */

	volatile int *py = &player_y; /* TODO: fix thread-safety, maybe do with events */

	for (int i = 0; i < 300; i++)
	{
		if (*py <= surfh+1)
			break;
		usleep(100*1000);
	}

	/* remove the water */
	/* actually this won't work as the water also ends up in stack[]
	   due to inject_to_client() being handled as a server-sent packet;
	   TODO: fix the injection mechanism, make that specifiable */

	for (int y = surfh+1; y <= surfh+3; y++)
	{
		p = packet_new(PACKET_TO_ANY, PACKET_SET_BLOCK,
		               x, y, z, stack[y], 0); /* TODO: fix metadata */
		inject_to_client(p);
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
				chat("//goto: impossible: jump from unloaded chunk");
				return;
			}
		}

		if (player_y == -999)
			chat("//goto: jumping from a minecart, who knows if this will work");
		else if (player_y < 0)
			chat("//goto: you're below level 0! this should never happen (y=%d)", player_y);

		for (int h = (player_y == -999 ? 0 : player_y + 1); h < CHUNK_YSIZE; h++)
		{
			for (unsigned i = 0; i < nstacks; i++)
			{
				if (!hollow(stacks[i][h]))
				{
					chat("//goto: blocked: the skies are not clear");
					return;
				}
			}
		}
	}

	/* inject jumping packets */

	packet_t *pjump1 = packet_new(PACKET_TO_ANY, PACKET_PLAYER_MOVE,
	                              player_x+0.5, 130.0, 131.62, player_z+0.5, 0);
	packet_t *pjump2 = packet_dup(pjump1);

	packet_t *pmove1 = packet_new(PACKET_TO_ANY, PACKET_PLAYER_MOVE,
	                              x+0.5, 130.0, 131.62, z+0.5, 0);
	packet_t *pmove2 = packet_dup(pmove1);

	inject_to_server(pjump1);
	inject_to_client(pjump2);

	inject_to_server(pmove1);
	inject_to_client(pmove2);

	chat("//goto: jumping to (%d,%d)", x, z);

	/* fake safe landing */

#ifdef FEAT_SAFE_TELEPORT
	struct teleport_fall_data *tfd = g_malloc(sizeof *tfd);
	tfd->x = x;
	tfd->z = z;
	g_thread_create(teleport_fall, tfd, FALSE, NULL);
#endif
}
