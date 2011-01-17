#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

#include "config.h"

void cmd_parse(unsigned char *cmd, int cmdlen);
void teleport(int x, int z);

#define command(name) void cmd_##name (int, gchar **);
#include "cmddefs.h"
#undef command

#endif /* MCMAP_CMD_H */
