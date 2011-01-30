#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

#include "config.h"

void cmd_parse(unsigned char *cmd, int cmdlen);

struct Jump {
	int x;
	int z;
};

GHashTable *jumps;

void jumps_list(void);
void jumps_save(gchar *filename);
void jumps_add(gchar *name, int x, int z, gboolean is_command);
void jumps_rm(gchar *name);

void teleport(int x, int z);

#define command(name) void cmd_##name (int, gchar **);
#include "cmddefs.h"
#undef command

#endif /* MCMAP_CMD_H */
