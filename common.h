#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#define die(msg) do_die(__FILE__, __LINE__, "%s", msg)
#define dief(fmt, ...) do_die(__FILE__, __LINE__, fmt, __VA_ARGS__)

void do_die(char *file, int line, char *fmt, ...) __attribute__ ((noreturn, format (printf, 3, 4)));

#endif /* MCMAP_COMMON_H */
