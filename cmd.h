#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

void init_cmd(void);

void cmd_parse(struct buffer cmd);

void jumps_list(void);
void jumps_save(char *filename);
void jumps_add(char *name, int x, int z, bool is_command);
void jumps_rm(char *name);

#define COMMAND(name) void cmd_##name (int, char **);
#include "cmd.def"
#undef COMMAND

#endif /* MCMAP_CMD_H */
