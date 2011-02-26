#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#include "platform.h"
#include "types.h"

#define NELEMS(array) (sizeof(array) / sizeof((array)[0]))

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

/* utilities */

#define hollow(x) (air(x) || water(x) || lava(x))
#define air(x) ((x) == 0x00)
#define water(x) ((x) == 0x08 || (x) == 0x09)
#define lava(x) ((x) == 0x0a || (x) == 0x0b)

/* teleportation */

struct Jump
{
        int x;
        int z;
};

GHashTable *jumps;

void teleport(int x, int z);

#endif /* MCMAP_COMMON_H */
