#ifndef MCMAP_CMD_H
#define MCMAP_CMD_H 1

void cmd_parse(unsigned char *cmd, int cmdlen);

void cmd_goto(int x, int z);
void cmd_coords(void);

#endif /* MCMAP_CMD_H */
