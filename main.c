#include <stdio.h>

#include <gio/gio.h>
#include <SDL.h>

#include "map.h"
#include "protocol.h"
#include "world.h"

/* proxying thread function to pass packets */

struct proxy_config
{
	GSocket *sock_from, *sock_to;
	GAsyncQueue *q;
};

gpointer proxy_thread(gpointer data)
{
	struct proxy_config *cfg = data;
	GSocket *sfrom = cfg->sock_from, *sto = cfg->sock_to;
	int sfromfd = g_socket_get_fd(sfrom), stofd = g_socket_get_fd(sto);

	packet_state_t state = PACKET_STATE_INIT;

	while (1)
	{
		packet_t *p = packet_read(sfrom, &state);
		if (!p)
		{
			fprintf(stderr, "proxy thread (%d -> %d) read failed\n", sfromfd, stofd);
			return 0;
		}

		if (!packet_write(sto, p))
		{
			fprintf(stderr, "proxy thread (%d -> %d) write failed\n", sfromfd, stofd);
			return 0;
		}

		/* communicate interesting chunks back */

		switch (p->id)
		{
		case PACKET_CHUNK:
		case PACKET_PLAYER_MOVE:
		case PACKET_PLAYER_ROTATE:
		case PACKET_PLAYER_MOVE_ROTATE:
			g_async_queue_push(cfg->q, packet_dup(p));
			break;
		}
	}

	return 0;
}

/* main function */

int main(int argc, char **argv)
{
	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) != 0)
	{
		fprintf(stderr, "SDL init failed\n");
		return 1;
	}
	g_thread_init(0);
	g_type_init();

	/* build up the world model */

	world_init();

	GAsyncQueue *packetq = g_async_queue_new_full(g_free);
	g_thread_create(world_thread, packetq, FALSE, 0);

	/* wait for a client to connect to us */

	GSocketListener *listener = g_socket_listener_new();

	if (!g_socket_listener_add_inet_port(listener, 25566, 0, 0))
	{
		fprintf(stderr, "unable to set up sockets\n");
		return 1;
	}

	GSocketConnection *conn_cli = g_socket_listener_accept(listener, 0, 0, 0);

	if (!conn_cli)
	{
		fprintf(stderr, "client never connected\n");
		return 1;
	}

	/* connect to the minecraft server side */

	GSocketClient *client = g_socket_client_new();

	GSocketConnection *conn_srv = g_socket_client_connect_to_host(client, "a322.org:25566" /* "localhost" */, 25565, 0, 0);

	if (!conn_srv)
	{
		fprintf(stderr, "unable to connect to server\n");
		return 1;
	}

	/* start the proxying threads */

	GSocket *sock_cli = g_socket_connection_get_socket(conn_cli);
	GSocket *sock_srv = g_socket_connection_get_socket(conn_srv);

	struct proxy_config proxy_client_server = {
		.sock_from = sock_cli,
		.sock_to = sock_srv,
		.q = packetq
	};

	struct proxy_config proxy_server_client = {
		.sock_from = sock_srv,
		.sock_to = sock_cli,
		.q = packetq
	};

	g_thread_create(proxy_thread, &proxy_client_server, FALSE, 0);
	g_thread_create(proxy_thread, &proxy_server_client, FALSE, 0);

	/* start the user interface side */

	SDL_Surface *screen = SDL_SetVideoMode(600, 600, 32, SDL_SWSURFACE);
	if (!screen)
	{
		fprintf(stderr, "video mode setting failed: %s\n", SDL_GetError());
		return 1;
	}

	map_init(screen);

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

			case MCMAP_EVENT_REPAINT:
				repaint = 1;
				break;
			}
		}

		/* repaint dirty bits if necessary */

		if (repaint)
			map_draw(screen);

		/* wait for something interesting to happen */

		SDL_WaitEvent(0);
	}
}
