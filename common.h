#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#include "protocol.h"

/* packet injection */

void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

void log_print(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void chat(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

/* fatal error handling */

#define die(msg) do_die(__FILE__, __LINE__, 0, "%s", msg)
#define dief(fmt, ...) do_die(__FILE__, __LINE__, 0, fmt, __VA_ARGS__)

#define stop(msg) do_die(__FILE__, __LINE__, 1, "%s", msg)
#define stopf(fmt, ...) do_die(__FILE__, __LINE__, 1, fmt, __VA_ARGS__)

void do_die(char *file, int line, int is_stop, char *fmt, ...) __attribute__ ((noreturn, format (printf, 4, 5)));

#endif /* MCMAP_COMMON_H */
