#ifndef MCMAP_COMMON_H
#define MCMAP_COMMON_H

#include "platform.h"
#include "types.h"

#define NELEMS(array) (sizeof(array) / sizeof((array)[0]))

/* for glib */
guint coord_hash(gconstpointer key);
gboolean coord_equal(gconstpointer a, gconstpointer b);

/* logging and information */

void log_print(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_die(int is_stop, char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));

/* fatal error handling */

#define dief(fmt, ...) log_die(0, fmt, ## __VA_ARGS__)
#define die(msg) dief("%s", msg)

#define stopf(fmt, ...) log_die(1, fmt, ## __VA_ARGS__)
#define stop(msg) stopf("%s", msg)

#define wtff(fmt, ...) dief("%s:%d: " fmt, __FILE__, __LINE__, ## __VA_ARGS__)
#define wtf(msg) wtff("%s", msg)

/* options */

struct options
{
	int localport;
	bool noansi;
	bool nomap;
	int scale;
	char *wndsize;
	char *jumpfile;
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
