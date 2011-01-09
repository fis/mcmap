#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

	if (strcmp(cmdv[0], "coords") == 0)
		cmd_coords();
	else if (strcmp(cmdv[0], "goto") == 0)
	{
		if (cmdc == 3)
			cmd_goto(atoi(cmdv[1]), atoi(cmdv[2]));
		else
			chat("usage: //goto x z");
	}
	else if (strcmp(cmdv[0], "slap") == 0)
	{
		if (cmdc == 2)
			cmd_slap(cmdv[1]);
		else
			chat("usage: //slap name");
	}
#ifdef FEAT_FULLCHUNK
	else if (strcmp(cmdv[0], "save") == 0)
	{
		if (cmdc == 1)
			cmd_save(0);
		else if (cmdc == 2)
			cmd_save(cmdv[1]);
		else
			chat("usage: //save [dir]");
	}
#endif /* FEAT_FULLCHUNK */
	else
		chat("unknown command: //%s", cmdv[0]);

	g_strfreev(cmdv);
}

void cmd_coords()
{
	chat("//coords: x=%d, z=%d, y=%d", player_x, player_z, player_y);
}

void cmd_goto(int x, int z)
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
				chat("//goto: impossible: jump from unloaded block");
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
}

#ifdef FEAT_FULLCHUNK
void cmd_save(char *dir)
{
	/* construct the target directory */

	char dirbuf[PATH_MAX+1];

	if (!dir || *dir != '/')
	{
		if (!getcwd(dirbuf, sizeof dirbuf))
			g_snprintf(dirbuf, sizeof dirbuf, ".");

		g_strlcat(dirbuf, "/", sizeof dirbuf);

		if (dir)
			g_strlcat(dirbuf, dir, sizeof dirbuf);
		else
			g_strlcat(dirbuf, "world", sizeof dirbuf);

		dir = dirbuf;
	}

	/* check the target dir for suitability */

	struct stat sbuf;

	int t = stat(dir, &sbuf);

	if (t != 0 && errno == ENOENT)
	{
		if (mkdir(dir, 0777) != 0)
		{
			chat("//save: can't create dir: %s", dir);
			return;
		}
	}
	else if (t != 0)
	{
		chat("//save: can't stat: %s", dir);
		return;
	}
	else if (!S_ISDIR(sbuf.st_mode))
	{
		chat("//save: not a directory: %s", dir);
		return;
	}

	/* dump the world */

	chat("//save: dumping world to: %s", dir);

	if (!world_save(dir))
		chat("//save: failed");
	else
		chat("//save: successful");
}
#endif /* FEAT_FULLCHUNK */

void cmd_slap(char *name)
{
	char *msg = g_strdup_printf("/me slaps %s around a bit with a large trout", name);
	inject_to_server(packet_new(PACKET_TO_ANY, PACKET_CHAT, msg));
	g_free(msg);
}
