#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <curses.h>
#include <glib.h>

#include "common.h"
#include "console.h"
#include "protocol.h"

static int console_curses = 0;
static int console_fd = 1;

static int opipe_read, opipe_write;

static WINDOW *win_output, *win_input;

/* setting up */

static gpointer console_thread(gpointer userdata);
static void console_cleanup(void);

void console_init(void)
{
	if (isatty(0) && isatty(1))
	{
		/* make a pipe for sending output */

		int pipefd[2];
		if (pipe(pipefd) != 0)
			goto no_terminal;

		opipe_read = pipefd[0];
		opipe_write = pipefd[1];

		/* initialize curses */

		initscr();
		cbreak();
		//noecho();
		nonl();
		intrflush(stdscr, FALSE);

		console_curses = 1;
		console_fd = opipe_write;
		atexit(console_cleanup);

		/* create input and output windows */

		win_output = newwin(LINES-1, 0, 0, 0);
		win_input = newwin(1, 0, LINES-1, 0);

		idlok(win_output, TRUE);
		scrollok(win_output, TRUE);

		nodelay(win_input, TRUE);
		keypad(win_input, TRUE);

		/* start the input-handling thread */

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

void log_print(char *fmt, ...)
{
	GString *line = tstamp();

	va_list ap;
	va_start(ap, fmt);
	g_string_append_vprintf(line, fmt, ap);
	va_end(ap);

	g_string_append_c(line, '\n');

	if (write(console_fd, line->str, line->len) <= 0)
		/* don't do a thing */;

	g_string_free(line, TRUE);
}

/* terminal handling curses mess */

static gpointer console_thread(gpointer userdata)
{
	struct pollfd pfds[2] = {
		{ .fd = opipe_read, .events = POLLIN },
		{ .fd = 0, .events = POLLIN }
	};

	//GString *input = g_string_new("");
	//int input_start = 0, input_cursor = 0;
	//int input_col = 2;

	GIOChannel *och = g_io_channel_unix_new(opipe_read);

	wclear(win_input);
	mvwaddstr(win_input, 0, 0, "> ");

	wclear(win_output);
	wmove(win_output, 0, 0);

	wnoutrefresh(win_output);
	wnoutrefresh(win_input);
	doupdate();

	while (1)
	{
		/* wait for input (stdin) or output (pipe) */

		int i = poll(pfds, 2, -1);
		if (i < 0 && errno == EINTR)
			continue;

		if (pfds[0].revents & POLLIN)
		{
			/* a line of output is waiting, read and show */

			gchar *line = 0;
			gsize line_eol = 0;

			g_io_channel_read_line(och, &line, 0, &line_eol, 0);

			if (line && *line)
			{
				gchar *p = line;
				while (line_eol > 0)
				{
					int n = line_eol < COLS ? line_eol : COLS;
					waddnstr(win_output, p, n);
					waddch(win_output, '\n');
					line_eol -= n;
					p += n;
				}
				wnoutrefresh(win_output);
			}

			g_free(line);
		}

		if (pfds[1].revents & POLLIN)
		{
			/* some input to handle */

			int ch;
			while ((ch = wgetch(win_input)) != ERR)
			{
				log_print("[KEYP] ch = %d", ch);
				//waddch(win_input, ch);
				//waddstr(win_input, "ä");
			}
		}

		//wmove(win_input, 0, input_col);
		wnoutrefresh(win_input);
		doupdate();
	}

	exit(0);
	return NULL;
}

static void console_cleanup(void)
{
	if (console_curses)
		endwin();

	console_curses = 0;
}
