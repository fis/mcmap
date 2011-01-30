#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <SDL.h>

#include "cmd.h"
#include "config.h"
#include "common.h"
#include "console.h"
#include "map.h"
#include "protocol.h"
#include "world.h"

/* default command-line options */

struct options opt = {
	.localport = 25565,
	.noansi = FALSE,
	.nomap = FALSE,
	.scale = 1,
	.wndsize = 0,
	.jumpfile = 0,
};

/* miscellaneous helper routines */

static void handle_key(SDL_KeyboardEvent *e, int *repaint);
static void handle_mouse(SDL_MouseButtonEvent *e, SDL_Surface *screen);
static void handle_chat(unsigned char *msg, int msglen);

/* proxying thread function to pass packets */

struct proxy_config
{
	socket_t sock_from, sock_to;
	int client_to_server;
	GAsyncQueue *q;
	GAsyncQueue *iq;
};

static GAsyncQueue *iq_client = 0;
static GAsyncQueue *iq_server = 0;

gpointer proxy_thread(gpointer data)
{
	struct proxy_config *cfg = data;
	socket_t sfrom = cfg->sock_from, sto = cfg->sock_to;
	char *desc = cfg->client_to_server ? "client -> server" : "server -> client";

	packet_state_t state = PACKET_STATE_INIT(cfg->client_to_server ? PACKET_TO_SERVER : PACKET_TO_CLIENT);

	while (1)
	{
		/* read in one packet from the injection queue or socket */

		int packet_must_free = 1;

		packet_t *p = g_async_queue_try_pop(cfg->iq);
		if (!p)
		{
			p = packet_read(sfrom, &state);
			packet_must_free = 0;
		}

		if (!p)
		{
			SDL_Event e = { .type = SDL_QUIT };
			SDL_PushEvent(&e);
			return 0;
		}

#if DEBUG_PROTOCOL >= 2 /* use for packet dumping for protocol analysis */
		if (p->id != PACKET_CHUNK)
		{
			int i, nf = packet_nfields(p);

			fprintf(stderr, "packet: %u [%s]\n", p->id, desc);
			for (i = 0; i < nf; i++)
			{
				fprintf(stderr, "  field %d:", i);
				for (unsigned u = p->field_offset[i]; u < p->field_offset[i+1]; u++)
					fprintf(stderr, " %02x", p->bytes[u]);
				fprintf(stderr, "\n");
			}
		}
#endif

		/* either write it out or handle if it's a command to us */

		if (cfg->client_to_server
		    && p->id == PACKET_CHAT
		    && p->bytes[3] == '/' && p->bytes[4] == '/')
		{
			g_async_queue_push(cfg->q, packet_dup(p));
		}
		else
		{
			if (!packet_write(sto, p))
				dief("proxy thread (%s) write failed", desc);
		}

		/* communicate interesting chunks back */

		if (!world_running)
			continue;

		switch (p->id)
		{
		case PACKET_CHUNK:
		case PACKET_MULTI_SET_BLOCK:
		case PACKET_SET_BLOCK:
			if (opt.nomap)
				break;
			/* fall-through to processing */

		case PACKET_LOGIN:
		case PACKET_PLAYER_MOVE:
		case PACKET_PLAYER_ROTATE:
		case PACKET_PLAYER_MOVE_ROTATE:
		case PACKET_ENTITY_SPAWN_NAMED:
		case PACKET_ENTITY_SPAWN_OBJECT:
		case PACKET_ENTITY_DESTROY:
		case PACKET_ENTITY_REL_MOVE:
		case PACKET_ENTITY_REL_MOVE_LOOK:
		case PACKET_ENTITY_MOVE:
		case PACKET_ENTITY_ATTACH:
		case PACKET_TIME:
			g_async_queue_push(cfg->q, packet_dup(p));
			break;

		case PACKET_CHAT:
			if (!cfg->client_to_server)
			{
				int msglen;
				unsigned char *msg = packet_string(p, 0, &msglen);
				handle_chat(msg, msglen);
			}
			break;
		}

		if (packet_must_free)
			packet_free(p);
	}
	return NULL;
}

/* main application */

int mcmap_main(int argc, char **argv)
{
	setlocale(LC_ALL, "");

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
		gchar *usage = g_option_context_get_help(gopt, TRUE, 0);
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

	int wnd_w = 512, wnd_h = 512;

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
		gchar *jump_file;
		GError *error = 0;
		struct Jump *jump;
		gchar *file_ptr;
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
			gchar *field;
			gchar *name;
			jump = g_malloc(sizeof(struct Jump));
			FIELD(name = strdup(field));
			FIELD(jump->x = atoi(field));
			FIELD(jump->z = atoi(field));
			g_hash_table_insert(jumps, name, jump);
		}
		#undef FIELD
		g_free(jump_file);
	}

	/* initialization stuff */

	g_thread_init(0);

	iq_client = g_async_queue_new_full(packet_free);
	iq_server = g_async_queue_new_full(packet_free);

	/* build up the world model */

	world_init();

	GAsyncQueue *packetq = g_async_queue_new_full(packet_free);
	g_thread_create(world_thread, packetq, FALSE, 0);

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

	socket_t sock_cli = accept(listener, 0, 0);
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

	socket_t sock_srv = make_socket(serveraddr->ai_family, serveraddr->ai_socktype, serveraddr->ai_protocol);

	if (sock_srv < 0)
		die("network setup: socket() for server");

	if (connect(sock_srv, serveraddr->ai_addr, serveraddr->ai_addrlen) != 0)
		die("network setup: connect() for server");

	freeaddrinfo(serveraddr);

	/* start the user interface side */

	console_init();

	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) != 0)
	{
		die("Failed to initialize SDL.");
		return 1;
	}

	SDL_Surface *screen = NULL;

	if (!opt.nomap)
	{
		screen = SDL_SetVideoMode(wnd_w, wnd_h, 32, SDL_SWSURFACE|(opt.wndsize ? 0 : SDL_RESIZABLE));

		if (!screen)
		{
			dief("Failed to set video mode: %s", SDL_GetError());
			return 1;
		}

		SDL_WM_SetCaption("mcmap", "mcmap");
		SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

		map_init(screen);
		map_setscale(opt.scale, 0);
	}

	/* start the proxying threads */

	log_print("[INFO] Starting up...");

	struct proxy_config proxy_client_server = {
		.sock_from = sock_cli,
		.sock_to = sock_srv,
		.client_to_server = 1,
		.q = packetq,
		.iq = iq_server
	};

	struct proxy_config proxy_server_client = {
		.sock_from = sock_srv,
		.sock_to = sock_cli,
		.client_to_server = 0,
		.q = packetq,
		.iq = iq_client
	};

	g_thread_create(proxy_thread, &proxy_client_server, FALSE, 0);
	g_thread_create(proxy_thread, &proxy_server_client, FALSE, 0);
	
	/* enter SDL main loop */

	while (1)
	{
		int repaint = 0;

		/* process pending events, coalesce repaints */

		SDL_Event e;

		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				SDL_Quit();
				return 0;

			case SDL_KEYDOWN:
				handle_key(&e.key, &repaint);
				break;

			case SDL_MOUSEBUTTONDOWN:
				handle_mouse(&e.button, screen);
				break;

			case SDL_VIDEORESIZE:
				screen = SDL_SetVideoMode(e.resize.w, e.resize.h, 32, SDL_SWSURFACE|SDL_RESIZABLE);
				repaint = 1;
				break;

			case SDL_VIDEOEXPOSE:
			case MCMAP_EVENT_REPAINT:
				repaint = 1;
				break;
			}
		}

		/* repaint dirty bits if necessary */

		if (repaint && !opt.nomap)
			map_draw(screen);

		/* wait for something interesting to happen */

		SDL_WaitEvent(0);
	}
}

/* helper routine implementations */

static void handle_key(SDL_KeyboardEvent *e, int *repaint)
{
	switch (e->keysym.sym)
	{
	case SDLK_1:
		map_setmode(MAP_MODE_SURFACE, 0, 0, 0);
		*repaint = 1;
		break;

	case SDLK_2:
		map_setmode(MAP_MODE_CROSS, MAP_FLAG_FOLLOW_Y, 0, 0);
		*repaint = 1;
		break;

	case SDLK_3:
		map_setmode(MAP_MODE_CROSS, 0, MAP_FLAG_FOLLOW_Y, 0);
		*repaint = 1;
		break;

	case SDLK_4:
		map_setmode(MAP_MODE_TOPO, 0, 0, 0);
		*repaint = 1;
		break;

#ifdef FEAT_FULLCHUNK
	case SDLK_n:
		map_setmode(MAP_MODE_NOCHANGE, 0, 0, MAP_FLAG_LIGHTS);
		*repaint = 1;
		break;
	case SDLK_a:
		map_update_time(6000);
		break;
	case SDLK_s:
		map_update_time(18000);
		break;
#endif

	case SDLK_UP:
		map_update_alt(+1, 1);
		break;

	case SDLK_DOWN:
		map_update_alt(-1, 1);
		break;

	case SDLK_PAGEUP:
		map_setscale(+1, 1);
		break;

	case SDLK_PAGEDOWN:
		map_setscale(-1, 1);
		break;

	default:
		break;
	}
}

static void handle_mouse(SDL_MouseButtonEvent *e, SDL_Surface *screen)
{
	if (e->button == SDL_BUTTON_RIGHT)
	{
		/* teleport */
		int x, z;
		map_s2w(screen, e->x, e->y, &x, &z, 0, 0);
		teleport(x, z);
	}
}

static void handle_chat(unsigned char *msg, int msglen)
{
	static char *colormap[16] =
	{
		"30",   "34",   "32",   "36",   "31",   "35",   "33",   "37",
		"30;1", "34;1", "32;1", "36;1", "31;1", "35;1", "33;1", "0"
	};
	unsigned char *p = msg;
	GString *s = g_string_new("");

	while (msglen > 0)
	{
		if (msglen >= 3 && p[0] == 0xc2 && p[1] == 0xa7)
		{
			unsigned char cc = p[2];
			int c = -1;

			if (cc >= '0' && cc <= '9') c = cc - '0';
			else if (cc >= 'a' && cc <= 'f') c = cc - 'a' + 10;

			if (c >= 0 && c <= 15)
			{
				if (!opt.noansi)
					g_string_append_printf(s, "\x1b[%sm", colormap[c]);
				p += 3;
				msglen -= 3;
				continue;
			}
		}

		g_string_append_c(s, *p++);
		msglen--;
	}

	gchar *str = g_string_free(s, FALSE);
	if (opt.noansi)
		log_print("[CHAT] %s", str);
	else
		log_print("[CHAT] %s\x1b[0m", str);
	g_free(str);
}

/* common.h functions */

guint coord_hash(gconstpointer key)
{
	const struct coord *c = key;
	return c->x ^ ((c->z << 16) | (c->z >> 16));
}

gboolean coord_equal(gconstpointer a, gconstpointer b)
{
	const struct coord *ca = a, *cb = b;
	return COORD_EQUAL(*ca, *cb);
}

void inject_to_client(packet_t *p)
{
	g_async_queue_push(iq_client, p);
}

void inject_to_server(packet_t *p)
{
	g_async_queue_push(iq_server, p);
}

void chat(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	static const char prefix[4] = { 0xc2, 0xa7, 'b', 0 };
	char *cmsg = g_strjoin("", prefix, msg, NULL);

	inject_to_client(packet_new(PACKET_TO_ANY, PACKET_CHAT, cmsg));

	g_free(cmsg);
	g_free(msg);
}
