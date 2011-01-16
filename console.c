#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include "common.h"
#include "console.h"
#include "protocol.h"
#include "world.h"

int console_outfd = 1;

/* log-printing methods in common.h */

static GString *tstamp(void)
{
	char stamp[sizeof "HH:MM:SS "];

	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	strftime(stamp, sizeof stamp, "%H:%M:%S ", tm);

	return g_string_new(stamp);
}

static inline void log_vput(GString *prefix, char *fmt, va_list ap)
{
	g_string_append_vprintf(prefix, fmt, ap);
	g_string_append_c(prefix, '\n');

	if (write(console_outfd, prefix->str, prefix->len) <= 0)
		/* don't do a thing */;

	g_string_free(prefix, TRUE);
}

void log_print(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_vput(tstamp(), fmt, ap);
	va_end(ap);
}

void log_die(char *file, int line, int is_stop, char *fmt, ...)
{
	if (!is_stop)
		console_cleanup();

	GString *msg = tstamp();
	g_string_append_printf(msg, "[DIED] %s: %d: ", file, line);

	va_list ap;
	va_start(ap, fmt);
	log_vput(msg, fmt, ap);
	va_end(ap);

	if (is_stop)
	{
		world_running = 0;
		g_thread_exit(0);
		/* never reached */
	}

	exit(1);
}
