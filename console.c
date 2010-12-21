#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "console.h"
#include "protocol.h"
#include "world.h"

static int console_readline = 0;
static int console_outfd = 1;

static int opipe_read, opipe_write;

/* setting up */

static gpointer console_thread(gpointer userdata);
static void console_cleanup(void);

void console_init(void)
{
	if (isatty(0) && isatty(1))
	{
		int pipefd[2];
		if (pipe(pipefd) != 0)
			goto no_terminal;

		opipe_read = pipefd[0];
		opipe_write = pipefd[1];

		rl_readline_name = "mcmap";

		console_readline = 1;
		console_outfd = opipe_write;
		atexit(console_cleanup);

		g_thread_create(console_thread, NULL, FALSE, 0);
	}

no_terminal:
	return;
}

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
	if (console_readline && !is_stop)
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

/* readline input and log output interlacing */

static void console_line_ready(char *line)
{
	if (line)
	{
		add_history(line);
		inject_to_server(packet_new(PACKET_TO_SERVER, PACKET_CHAT, line));
	}
	else /* ^D */
		exit(0);

	free(line);

	rl_callback_handler_install("> ", console_line_ready);
}

static gpointer console_thread(gpointer userdata)
{
	struct pollfd pfds[2] = {
		{ .fd = opipe_read, .events = POLLIN },
		{ .fd = 0, .events = POLLIN }
	};

	GIOChannel *och = g_io_channel_unix_new(opipe_read);
	rl_callback_handler_install("> ", console_line_ready);

	while (1)
	{
		/* wait for input (stdin) or output (pipe) */

		int i = poll(pfds, 2, -1);
		if (i < 0 && errno == EINTR)
			continue;

		if (pfds[0].revents & POLLIN)
		{
			/* clean up the prompt and its contents */

			int old_point = rl_point, old_mark = rl_mark;
			char *old_text = strdup(rl_line_buffer);

			rl_replace_line("", 0);
			rl_redisplay();

			fputs("\r\x1b[K", stdout);

			/* display pending output lines (TODO: more than one) */

			gchar *line = 0;
			gsize line_eol = 0;

			g_io_channel_read_line(och, &line, 0, &line_eol, 0);
			if (!line || !*line)
			{
				rl_callback_handler_remove();
				console_cleanup();
				return NULL;
			}

			fputs(line, stdout);

			/* restore the readline prompt and contents */

			rl_insert_text(old_text);
			rl_point = old_point;
			rl_mark = old_mark;
			free(old_text);

			rl_forced_update_display();
		}

		if (pfds[1].revents & POLLIN)
		{
			/* let readline eat from stdin */
			rl_callback_read_char();
		}
	}

	exit(0);
}

static void console_cleanup(void)
{
	if (console_readline)
	{
		rl_deprep_terminal();
		putchar('\n');

		close(opipe_write);
	}

	console_readline = 0;
	console_outfd = 1;
}
