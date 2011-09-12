#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "cmd.h"
#include "config.h"
#include "protocol.h"
#include "common.h"
#include "console.h"
#include "map.h"
#include "block.h"
#include "world.h"
#include "proxy.h"
#include "ui.h"

/* default command-line options */

struct options opt = {
	.localport = 25565,
	.noansi = false,
	.nomap = false,
	.scale = 1,
	.wndsize = 0,
	.jumpfile = 0,
};

void load_colors(char **lines);

/* main application */

int mcmap_main(int argc, char **argv)
{
	bool upgrading = false;
	int upgrade_fd = -1;
	socket_t sock_srv = -1;
	socket_t sock_cli = -1;
	int wnd_w = 512, wnd_h = 512;

	setlocale(LC_ALL, "");

	init_cmd();

	if (argv[1] && argc >= 3 && !strcmp(argv[1], "--upgrade"))
	{
		upgrading = true;
		upgrade_fd = atoi(argv[2]);
		argv[2] = argv[0];
		argv += 2;
		argc -= 2;
	}

	main_argc = argc;
	main_argv = argv;

	/* command line option grokking */

	static GOptionEntry gopt_entries[] = {
		{ "nocolor", 'c', 0, G_OPTION_ARG_NONE, &opt.noansi, "Disable ANSI color escapes" },
		{ "nomap", 'm', 0, G_OPTION_ARG_NONE, &opt.nomap, "Disable the map" },
		{ "port", 'p', 0, G_OPTION_ARG_INT, &opt.localport, "Local port to listen at", "P" },
		{ "size", 's', 0, G_OPTION_ARG_STRING, &opt.wndsize, "Fixed-size window size", "WxH" },
		{ "scale", 'x', 0, G_OPTION_ARG_INT, &opt.scale, "Zoom factor", "N" },
		{ "jumps", 'j', 0, G_OPTION_ARG_STRING, &opt.jumpfile, "File containing list of jumps", "FILENAME" },
		{ NULL }
	};

	GOptionContext *gopt = g_option_context_new("host[:port]");
	GError *gopt_error = 0;

	g_option_context_add_main_entries(gopt, gopt_entries, 0);
	if (!g_option_context_parse(gopt, &argc, &argv, &gopt_error))
	{
		die(gopt_error->message);
	}

	if (argc != 2)
	{
		char *usage = g_option_context_get_help(gopt, true, 0);
		fputs(usage, stderr);
		return 1;
	}

	if (opt.localport < 1 || opt.localport > 65535)
	{
		dief("Invalid port number: %d", opt.localport);
	}

	if (opt.scale < 1 || opt.scale > 64)
	{
		dief("Unreasonable scale factor: %d", opt.scale);
	}

	if (opt.wndsize)
	{
		if (sscanf(opt.wndsize, "%dx%d", &wnd_w, &wnd_h) != 2
		    || wnd_w < 0 || wnd_h < 0)
		{
			dief("Invalid window size: %s", opt.wndsize);
		}
	}

	jumps = g_hash_table_new(g_str_hash, g_str_equal);

	if (opt.jumpfile)
	{
		char *jump_file;
		GError *error = 0;
		struct Jump *jump;
		char *file_ptr;
		if (!g_file_get_contents(opt.jumpfile, &jump_file, NULL, &error))
			die(error->message);
		#define FIELD(assigner) \
			field = file_ptr; \
			while (!isspace(*file_ptr) && file_ptr[1] != 0) \
				file_ptr++; \
			*file_ptr++ = 0; \
			assigner; \
			while (isspace(*file_ptr)) \
				file_ptr++
		file_ptr = jump_file;
		while (*file_ptr != 0)
		{
			char *field;
			char *name;
			jump = g_new(struct Jump, 1);
			FIELD(name = strdup(field));
			FIELD(jump->x = atoi(field));
			FIELD(jump->z = atoi(field));
			g_hash_table_insert(jumps, name, jump);
		}
		#undef FIELD
		g_free(jump_file);
	}

	/* load colors */

	load_colors(default_colors);

	char *filename = g_strconcat(g_get_home_dir(), "/.mcmap/colors", NULL);
	char *colors;
	GError *error = NULL;
	if (g_file_get_contents(filename, &colors, NULL, &error))
	{
		char **lines = g_strsplit_set(colors, "\n", 0);
		load_colors(lines);
		g_free(colors);
		g_strfreev(lines);
	}
	else if (error->code != G_FILE_ERROR_NOENT)
	{
		die(error->message);
	}
	g_free(filename);

	/* initialization stuff */

	socket_init();

	if (upgrading)
		goto upgrade;

	/* wait for a client to connect to us */

	log_print("[INFO] Waiting for connection...");

	socket_t listener = make_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listener < 0)
		die("network setup: socket() for listener");

	{
		int b = 1;
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char *)&b, sizeof b);
	}

	struct sockaddr_in listener_in = { 0 };
	listener_in.sin_family = AF_INET;
	listener_in.sin_addr.s_addr = htonl(INADDR_ANY);
	listener_in.sin_port = htons(opt.localport);
	if (bind(listener, (struct sockaddr *)&listener_in, sizeof listener_in) != 0)
		die("network setup: bind() for listener");

	if (listen(listener, SOMAXCONN) != 0)
		die("network setup: listen() for listener");

	sock_cli = accept(listener, 0, 0);
	if (sock_cli < 0)
		die("network setup: accept() for listener");

	/* connect to the minecraft server side */

	log_print("[INFO] Connecting to %s...", argv[1]);

	struct addrinfo hints = { 0 }, *serveraddr;

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char *portsep = strrchr(argv[1], ':');

	int aires;
	if (portsep)
	{
		*portsep = 0;
		aires = getaddrinfo(argv[1], portsep+1, &hints, &serveraddr);
	}
	else
		aires = getaddrinfo(argv[1], "25565", &hints, &serveraddr);

	if (aires != 0)
		die("network setup: getaddrinfo() for server");

	sock_srv = make_socket(serveraddr->ai_family, serveraddr->ai_socktype, serveraddr->ai_protocol);

	if (sock_srv < 0)
		die("network setup: socket() for server");

	if (connect(sock_srv, serveraddr->ai_addr, serveraddr->ai_addrlen) != 0)
		die("network setup: connect() for server");

	freeaddrinfo(serveraddr);

	/* start the proxy */

upgrade:

	log_print("[INFO] Starting up...");

	/* required because sometimes SDL initialisation fails after g_thread_init */
	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) != 0)
		dief("Failed to initialize SDL: %s", SDL_GetError());

	if (TTF_Init() != 0)
		dief("Failed to initialize SDL_ttf: %s", TTF_GetError());

	char *font_filename = g_strconcat(g_path_get_dirname(argv[0]), "/../lib/DejaVuSansMono-Bold.ttf", NULL);
	map_font = TTF_OpenFont(font_filename, 13);
	if (map_font == NULL)
		dief("Failed to load font file: %s", TTF_GetError());
	g_free(font_filename);

	SDL_EnableUNICODE(1);
	g_thread_init(0);

	proxy_initialize_state();

	if (upgrading)
	{
		struct buffer buf = read_buffer(upgrade_fd);
		proxy_deserialize_state(buf);
		g_free(buf.data);
	}
	else
		proxy_initialize_socket_state(sock_cli, sock_srv);

	start_proxy();

	/* start the user interface side */

	start_ui(!opt.nomap, opt.scale, !opt.wndsize, wnd_w, wnd_h);

	return 0;
}

void load_colors(char **lines)
{
	int line_number = 0;

	while (*lines)
	{
		line_number++;
		char *line = *lines++;

		if (line[0] == '#' || line[0] == '\0') continue;

		char block_name[256];
		unsigned color_r, color_g, color_b, color_a;
		int fields = sscanf(line, "%255[^:]:%u%u%u%u", block_name, &color_r, &color_g, &color_b, &color_a);

		if (fields < 4)
			dief("Invalid configuration line at ~/.mcmap/colors:%d: %s", line_number, line);
		else if (fields == 4)
			color_a = 255;

		rgba_t color = RGBA(color_r, color_g, color_b, color_a);

		bool ok = false;
		for (int block = 0; block < NELEMS(block_info); block++)
		{
			if (block_info[block].name && strcmp(block_info[block].name, block_name) == 0)
			{
				/* don't break out; multiple blocks can have the same name */
				ok = true;
				block_colors[block] = color;
			}
		}
		if (!ok)
			dief("Invalid block name at ~/.mcmap/colors:%d: %s", line_number, block_name);
	}
}
