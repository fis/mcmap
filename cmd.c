#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "cmd.h"
#include "common.h"
#include "map.h"
#include "protocol.h"
#include "world.h"

void cmd_parse(unsigned char *cmd, int cmdlen)
{
	gchar *cmdstr = g_strndup((gchar *)cmd, cmdlen);
	gchar **cmdv = g_strsplit_set(cmdstr, " ", -1);
	g_free(cmdstr);

	gint cmdc = 0;
	while (cmdv[cmdc])
		cmdc++;

	if (strcmp(cmdv[0], "goto") == 0)
	{
		if (cmdc == 3)
			cmd_goto(atoi(cmdv[1]), atoi(cmdv[2]));
		else
			printf("[CMD] //goto: usage: //goto x z\n");
	}
	else if (strcmp(cmdv[0], "coords") == 0)
		cmd_coords();
	else
		printf("[CMD] unknown command: //%s\n", cmdv[0]);

	g_strfreev(cmdv);
}

/* FIXME: This function is horrible. */
static void cmd_say(char *fmt, ...)
{
	char msg[1024]; /* I have a wonderful proof that this buffer is big
	                 * enough, but this margin is too small to contain it. */
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg+3, sizeof(msg)-3, fmt, ap);
	printf("[CMD] %s\n", msg+3);
	msg[0] = '\xc2';
	msg[1] = '\xa7';
	msg[2] = 'b'; /* cyan */
	inject_to_client(packet_new(PACKET_CHAT, msg));
	va_end(ap);
}

void cmd_goto(int x, int z)
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
			cmd_say("//goto: impossible: jump from unloaded block");
			return;
		}
	}

	for (int h = player_y + 1; h < CHUNK_YSIZE; h++)
	{
		if (h < 0)
			continue;
		for (unsigned i = 0; i < nstacks; i++)
		{
			if (stacks[i][h])
			{
				cmd_say("//goto: blocked: the skies are not clear");
				return;
			}
		}
	}

	/* inject jumping packets */

	packet_t *pjump1 = packet_new(PACKET_PLAYER_MOVE,
	                              (double)player_x, 128.0, 129.62, (double)player_z, 0);
	packet_t *pjump2 = packet_dup(pjump1);

	packet_t *pmove1 = packet_new(PACKET_PLAYER_MOVE,
	                              (double)x, 128.0, 129.62, (double)z, 0);
	packet_t *pmove2 = packet_dup(pmove1);

	inject_to_client(pjump1);
	inject_to_server(pjump2);

	inject_to_client(pmove1);
	inject_to_server(pmove2);

	cmd_say("//goto: jumping to (%d,%d)", x, z);
}

void cmd_coords()
{
	cmd_say("//coords: x=%d, z=%d, y=%d", player_x, player_z, player_y);
}
