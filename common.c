#include <unistd.h>
#include <math.h>

#include "common.h"
#include "map.h"
#include "world.h"

struct teleport_fall_data
{
	int x;
	int z;
};

static gpointer teleport_fall(gpointer data)
{
	struct teleport_fall_data *tfd = data;
	int x = tfd->x, z = tfd->z;

	int h = 127, desth = 64;
	int delay = 50*1000; /* 50 ms delay for normal safe fall */
	int seen_ground = 0;

	while (h > desth)
	{
		/* check for loaded chunks for insta-drop knowledge */

		if (!seen_ground)
		{
			unsigned char *s = world_stack(x, z, 0);
			if (s)
			{
				for (desth = 127; desth >= 0; desth--)
					if (s[desth])
						break;
				desth += 3;
				if (h < desth)
					h = desth;
				delay = 0;
			}
			seen_ground = 1;
		}

		/* fall slowly down */

		packet_t *pfall1 = packet_new(PACKET_TO_ANY, PACKET_PLAYER_MOVE,
		                              (double)x, (double)h, h+1.62, (double)z, 0);
		packet_t *pfall2 = packet_dup(pfall1);

		inject_to_server(pfall1);
		inject_to_client(pfall2);

		if (delay)
			usleep(delay);

		h--;
	}

	g_free(tfd);
	return 0;
}

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
	                              (double)player_x, 128.0, 129.62, (double)player_z, 0);
	packet_t *pjump2 = packet_dup(pjump1);

	packet_t *pmove1 = packet_new(PACKET_TO_ANY, PACKET_PLAYER_MOVE,
	                              (double)x, 128.0, 129.62, (double)z, 0);
	packet_t *pmove2 = packet_dup(pmove1);

	inject_to_server(pjump1);
	inject_to_client(pjump2);

	inject_to_server(pmove1);
	inject_to_client(pmove2);

	chat("//goto: jumping to (%d,%d)", x, z);

	/* fake safe landing */

	struct teleport_fall_data *tfd = g_malloc(sizeof *tfd);
	tfd->x = x;
	tfd->z = z;
	g_thread_create(teleport_fall, tfd, FALSE, NULL);
}
