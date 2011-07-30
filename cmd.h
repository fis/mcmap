#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

#include "config.h"
#include "types.h"

void cmd_parse(unsigned char *cmd, int cmdlen);

void jumps_list(void);
void jumps_save(char *filename);
void jumps_add(char *name, int x, int z, bool is_command);
void jumps_rm(char *name);

#define command(name) void cmd_##name (int, char **);
#include "cmddefs.h"
#undef command

#endif /* MCMAP_CMD_H */
