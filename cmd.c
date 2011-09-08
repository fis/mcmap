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
#include <libguile.h>
#include <SDL.h>

#include "cmd.h"
#include "protocol.h"
#include "common.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

static struct { char *name; void (*run)(int, char **); } commands[] = {
#define command(name) { #name, cmd_##name },
#include "cmddefs.h"
#undef command
};

void cmd_parse(unsigned char *cmd, int cmdlen)
{
	char *cmdstr = g_strndup((char *) cmd, cmdlen);
	char **cmdv = g_strsplit_set(cmdstr, " ", -1);
	g_free(cmdstr);

	int cmdc = 0;
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

	tell("unknown command: //%s", cmdv[0]);

done:
	g_strfreev(cmdv);
}

void cmd_coords(int cmdc, char **cmdv)
{
	if (cmdc == 2 && strcmp(cmdv[1], "-say") == 0)
		say("/me is at (%d,%d) (y=%d)", player_x, player_z, player_y);
	else if (cmdc == 1)
		tell("//coords: (%d,%d) (y=%d)", player_x, player_z, player_y);
	else
		tell("usage: //coords [-say]");
}

void cmd_goto(int cmdc, char **cmdv)
{
	if (cmdc == 2)
	{
		struct Jump *jump = g_hash_table_lookup(jumps, cmdv[1]);
		if (jump == NULL)
			tell("//goto: no such jump (try //jumps list)");
		else
			teleport(jump->x, jump->z);
	}
	else if (cmdc == 3)
	{
		teleport(atoi(cmdv[1]), atoi(cmdv[2]));
	}
	else
	{
		tell("usage: //goto x z or //goto name (try //jumps list)");
	}
}

void cmd_jumps(int cmdc, char **cmdv)
{
	if (cmdc < 2)
	{
usage:
		tell("usage: //jumps (list | save [filename] | add name x z | rm name)");
		return;
	}

	if (strcmp(cmdv[1], "list") == 0)
		jumps_list();
	else if (strcmp(cmdv[1], "save") == 0 && (cmdc == 2 || cmdc == 3))
		jumps_save(cmdc == 3 ? cmdv[2] : opt.jumpfile);
	else if (strcmp(cmdv[1], "add") == 0 && cmdc == 5)
		jumps_add(cmdv[2], atoi(cmdv[3]), atoi(cmdv[4]), true);
	else if (strcmp(cmdv[1], "rm") == 0 && cmdc == 3)
		jumps_rm(cmdv[2]);
	else
		goto usage;
}

#define FOREACH_JUMP(name, jump) \
	GHashTableIter jump_iter; \
	char *name; \
	struct Jump *jump; \
	g_hash_table_iter_init(&jump_iter, jumps); \
	while (g_hash_table_iter_next(&jump_iter, (gpointer *) &name, (gpointer *) &jump))

void jumps_list()
{
	if (g_hash_table_size(jumps) == 0)
	{
		tell("//jumps: no jumps exist");
		return;
	}

	FOREACH_JUMP (name, jump)
		tell("//jumps: %s (%d,%d)", name, jump->x, jump->z);
}

void jumps_save(char *filename)
{
	if (filename == NULL)
	{
		tell("//jumps: no jump file; please use //jumps save filename");
		return;
	}

	FILE *jump_file = fopen(filename, "w");

	if (jump_file == NULL)
	{
		tell("//jumps: opening %s: %s", filename, strerror(errno));
		return;
	}

	FOREACH_JUMP (name, jump)
		fprintf(jump_file, "%s\t\t%d %d\n", name, jump->x, jump->z);

	fclose(jump_file);
	tell("//jumps: saved to %s", filename);

	opt.jumpfile = filename; /* FIXME: do we want this? */
}

void jumps_add(char *name, int x, int z, bool is_command)
{
	struct Jump *jump = g_malloc(sizeof(struct Jump));
	jump->x = x;
	jump->z = z;
	g_hash_table_insert(jumps, strdup(name), jump);
	if (is_command)
		tell("//jumps: added %s (%d,%d)", name, x, z);
}

void jumps_rm(char *name)
{
	if (g_hash_table_remove(jumps, name))
		tell("//jumps: removed %s", name);
	else
		tell("//jumps: no such jump");
}

#undef FOREACH_JUMP

#ifdef FEAT_FULLCHUNK
void cmd_save(int cmdc, char **cmdv)
{
	char *dir;

	if (cmdc == 1)
		dir = 0;
	else {
		tell("usage: //save");
		return;
	}

	(void)dir;
#if 0 /* OBSOLETED STUFF; remove when world-dir location better */

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
			tell("//save: can't create dir: %s", dir);
			return;
		}
	}
	else if (t != 0)
	{
		tell("//save: can't stat: %s", dir);
		return;
	}
	else if (!S_ISDIR(sbuf.st_mode))
	{
		tell("//save: not a directory: %s", dir);
		return;
	}

#endif /* END OF OBSOLETED STUFF */

	/* TODO FIXME: here just for testing the on-disk syncing code */

	tell("//save: calling world_regfile_sync_all");
	world_regfile_sync_all();
}
#endif /* FEAT_FULLCHUNK */

void cmd_slap(int cmdc, char **cmdv)
{
	if (cmdc != 2)
	{
		tell("usage: //slap name");
		return;
	}

	say("/me slaps %s around a bit with a large trout", cmdv[1]);
}
