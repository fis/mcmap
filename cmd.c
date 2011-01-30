#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "cmd.h"
#include "common.h"
#include "map.h"
#include "protocol.h"
#include "world.h"

static struct { char *name; void (*run)(int, gchar **); } commands[] = {
#define command(name) { #name, cmd_##name },
#include "cmddefs.h"
#undef command
};

void cmd_parse(unsigned char *cmd, int cmdlen)
{
	gchar *cmdstr = g_strndup((gchar *)cmd, cmdlen);
	gchar **cmdv = g_strsplit_set(cmdstr, " ", -1);
	g_free(cmdstr);

	gint cmdc = 0;
	while (cmdv[cmdc])
		cmdc++;

	for (int i = 0; i < NELEMS(commands); i++) 
	{
		if (strcmp(cmdv[0], commands[i].name) == 0)
		{
			commands[i].run(cmdc, cmdv);
			goto done;
		}
	}

	chat("unknown command: //%s", cmdv[0]);

done:
	g_strfreev(cmdv);
}

void cmd_coords(int cmdc, gchar **cmdv)
{
	if (cmdc == 2 && strcmp(cmdv[1], "-say") == 0)
	{
		char *msg = g_strdup_printf("/me is at (%d,%d) (y=%d)", player_x, player_z, player_y);
		inject_to_server(packet_new(PACKET_TO_ANY, PACKET_CHAT, msg));
		g_free(msg);
	}
	else if (cmdc == 1)
		chat("//coords: (%d,%d) (y=%d)", player_x, player_z, player_y);
	else
		chat("usage: //coords [-say]");
}

void cmd_goto(int cmdc, gchar **cmdv)
{
	if (cmdc == 2)
	{
		struct Jump *jump = g_hash_table_lookup(jumps, cmdv[1]);
		if (jump == NULL)
			chat("//goto: no such jump (try //jumps list)");
		else
			teleport(jump->x, jump->z);
	}
	else if (cmdc == 3)
	{
		teleport(atoi(cmdv[1]), atoi(cmdv[2]));
	}
	else
	{
		chat("usage: //goto x z or //goto name (try //jumps list)");
	}
}

void cmd_jumps(int cmdc, gchar **cmdv)
{
	if (cmdc < 2)
	{
usage:
		chat("usage: //jumps (list | save [filename] | add name x z | rm name)");
		return;
	}

	if (strcmp(cmdv[1], "list") == 0)
		jumps_list();
	else if (strcmp(cmdv[1], "save") == 0 && (cmdc == 2 || cmdc == 3))
		jumps_save(cmdc == 3 ? cmdv[2] : opt.jumpfile);
	else if (strcmp(cmdv[1], "add") == 0 && cmdc == 5)
		jumps_add(cmdv[2], atoi(cmdv[3]), atoi(cmdv[4]), TRUE);
	else if (strcmp(cmdv[1], "rm") == 0 && cmdc == 3)
		jumps_rm(cmdv[2]);
	else
		goto usage;
}

#define FOREACH_JUMP(name, jump) \
	GHashTableIter jump_iter; \
	gchar *name; \
	struct Jump *jump; \
	g_hash_table_iter_init(&jump_iter, jumps); \
	while (g_hash_table_iter_next(&jump_iter, (gpointer *) &name, (gpointer *) &jump))

void jumps_list()
{
	if (g_hash_table_size(jumps) == 0)
	{
		chat("//jumps: no jumps exist");
		return;
	}

	FOREACH_JUMP (name, jump)
		chat("//jumps: %s (%d,%d)", name, jump->x, jump->z);
}

void jumps_save(gchar *filename)
{
	if (filename == NULL)
	{
		chat("//jumps: no jump file; please use //jumps save filename");
		return;
	}

	FILE *jump_file = fopen(filename, "w");

	if (jump_file == NULL)
	{
		chat("//jumps: opening %s: %s", filename, strerror(errno));
		return;
	}

	FOREACH_JUMP (name, jump)
		fprintf(jump_file, "%s\t\t%d %d\n", name, jump->x, jump->z);

	fclose(jump_file);
	chat("//jumps: saved to %s", filename);

	opt.jumpfile = filename; /* FIXME: do we want this? */
}

void jumps_add(gchar *name, int x, int z, gboolean is_command)
{
	struct Jump *jump = g_malloc(sizeof(struct Jump));
	jump->x = x;
	jump->z = z;
	g_hash_table_insert(jumps, strdup(name), jump);
	if (is_command)
		chat("//jumps: added %s (%d,%d)", name, x, z);
}

void jumps_rm(gchar *name)
{
	if (g_hash_table_remove(jumps, name))
		chat("//jumps: removed %s", name);
	else
		chat("//jumps: no such jump");
}

#undef FOREACH_JUMP

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

		for (int h = player_y + 1; h < CHUNK_YSIZE; h++)
		{
			if (h < 0)
				continue;
			for (unsigned i = 0; i < nstacks; i++)
			{
				if (!(stacks[i][h] == 0x00 || stacks[i][h] == 0x08 || stacks[i][h] == 0x09))
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

#ifdef FEAT_FULLCHUNK
void cmd_save(int cmdc, gchar **cmdv)
{
	gchar *dir;

	if (cmdc == 1)
		dir = 0;
	else if (cmdc == 2)
		dir = cmdv[1];
	else {
		chat("usage: //save [dir]");
		return;
	}

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
		if (g_mkdir(dir, 0777) != 0)
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

void cmd_slap(int cmdc, gchar **cmdv)
{
	if (cmdc != 2)
	{
		chat("usage: //slap name");
		return;
	}

	char *msg = g_strdup_printf("/me slaps %s around a bit with a large trout", cmdv[1]);
	inject_to_server(packet_new(PACKET_TO_ANY, PACKET_CHAT, msg));
	g_free(msg);
}
