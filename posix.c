#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <glib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "protocol.h"
#include "common.h"
#include "console.h"
#include "proxy.h"

static int console_readline = 0;
static int opipe_read, opipe_write;

static gpointer console_thread(gpointer userdata);

void socket_init() {}

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

                g_thread_create(console_thread, NULL, false, 0);
        }

no_terminal:
        return;
}

static void console_line_ready(char *line)
{
	if (line)
	{
		add_history(line);
		inject_to_server(packet_new(PACKET_CHAT, line));
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

			char *line = 0;
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

/* mmap/mremap-based solution for mmapping */

mmap_handle_t make_mmap(int fd, size_t len, void **addr)
{
	void *m = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	if (m == MAP_FAILED)
		*addr = 0;
	else
		*addr = m;

	return *addr;
}

mmap_handle_t resize_mmap(mmap_handle_t old, void *old_addr, int fd, size_t old_len, size_t new_len, void **addr)
{
#ifdef __linux
	/* Linux at least has mremap... */

	void *new_addr = mremap(old_addr, old_len, new_len, MREMAP_MAYMOVE);

	if (new_addr == MAP_FAILED)
		*addr = 0;
	else
		*addr = new_addr;

	return *addr;
#else
	munmap(old_addr, old_len);
	return make_mmap(fd, new_len, addr);
#endif
}

void sync_mmap(void *addr, size_t len)
{
	msync(addr, len, MS_ASYNC);
}
