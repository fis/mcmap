#include <stdlib.h>
#include <stdio.h> /* NihilistDandy needed this */
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <poll.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "protocol.h"
#include "common.h"
#include "console.h"
#include "proxy.h"

static int console_readline = 0;
static int opipe_read, opipe_write;

static gpointer console_thread(gpointer userdata);

/* readline input and log output interlacing */

void console_init()
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

			g_free(line);

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

void console_cleanup(void)
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
