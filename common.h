#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#include "platform.h"
#include "protocol.h"

#include <glib.h>

/* 2d points for hash table keys */

struct coord
{
	int x, z;
};

#define COORD_EQUAL(a,b) ((a).x == (b).x && (a).z == (b).z)

guint coord_hash(gconstpointer key);
gboolean coord_equal(gconstpointer a, gconstpointer b);

/* packet injection */

void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

/* logging and information */

void log_print(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void log_die(char *file, int line, int is_stop, char *fmt, ...) __attribute__ ((noreturn, format (printf, 4, 5)));

void chat(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

/* fatal error handling */

#define die(msg) log_die(__FILE__, __LINE__, 0, "%s", msg)
#define dief(fmt, ...) log_die(__FILE__, __LINE__, 0, fmt, __VA_ARGS__)

#define stop(msg) log_die(__FILE__, __LINE__, 1, "%s", msg)
#define stopf(fmt, ...) log_die(__FILE__, __LINE__, 1, fmt, __VA_ARGS__)

/* options */

struct options
{
	gint localport;
	gboolean noansi;
	gboolean nomap;
	gint scale;
	gchar *wndsize;
	gchar *jumpfile;
} opt;

#endif /* MCMAP_COMMON_H */
