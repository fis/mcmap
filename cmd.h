#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

void cmd_parse(unsigned char *cmd, int cmdlen);

void cmd_coords(void);
void cmd_goto(int x, int z);
void cmd_save(char *dir);
void cmd_slap(char *name);

#endif /* MCMAP_CMD_H */
