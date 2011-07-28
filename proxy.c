#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <sys/select.h>

#include <glib.h>
#include <SDL.h>

#include "cmd.h"
#include "config.h"
#include "protocol.h"
#include "common.h"
#include "console.h"
#include "map.h"
#include "world.h"
#include "ui.h"

/* proxying thread function to pass packets */

struct proxy_config
{
	socket_t sock_cli, sock_srv;
	GAsyncQueue *worldq;
};

static GAsyncQueue *iq = 0;

gpointer proxy_thread(gpointer data);

void start_proxy(socket_t sock_cli, socket_t sock_srv)
{
	socket_prepare(sock_cli);
	socket_prepare(sock_srv);

	iq = g_async_queue_new_full(packet_free);

	GAsyncQueue *worldq = g_async_queue_new_full(packet_free);

	/* TODO FIXME; call as world_init("world") or some-such to enable alpha-quality region persistence */
	world_init(0);
	g_thread_create(world_thread, worldq, FALSE, 0);

	/* start the proxying thread */
	struct proxy_config *cfg = g_new(struct proxy_config, 1);
	cfg->sock_cli = sock_cli;
	cfg->sock_srv = sock_srv;
	cfg->worldq = worldq;
	g_thread_create(proxy_thread, cfg, FALSE, 0);
}

gpointer proxy_thread(gpointer data)
{
	struct proxy_config *cfg = data;
	socket_t sock_cli = cfg->sock_cli, sock_srv = cfg->sock_srv;
	GAsyncQueue *worldq = cfg->worldq;

//	char *desc = cfg->client_to_server ? "client -> server" : "server -> client";

	packet_state_t state = PACKET_STATE_INIT(); // cfg->client_to_server ? PACKET_TO_SERVER : PACKET_TO_CLIENT);

	while (1)
	{
		/* read in one packet from the injection queue or socket */

		gboolean packet_must_free = TRUE;

		packet_t *p = g_async_queue_try_pop(iq);

		if (!p)
		{
			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(sock_cli, &rfds);
			FD_SET(sock_srv, &rfds);
			int nfds = (sock_cli > sock_srv ? sock_cli : sock_srv) + 1;
			int ret = select(nfds, &rfds, NULL, NULL, NULL);
			if (ret == -1)
				dief("select: %s", strerror(errno));
			else if (ret == 0)
				wtff("select returned 0! %s", strerror(errno));
			if (FD_ISSET(sock_cli, &rfds))
			{
				p = packet_read(sock_cli, &state);
				p->flags |= PACKET_TO_SERVER;
				packet_must_free = FALSE;
			}
			else if (FD_ISSET(sock_srv, &rfds))
			{
				p = packet_read(sock_srv, &state);
				p->flags |= PACKET_TO_CLIENT;
				packet_must_free = FALSE;
			}
			else
				wtf("Neither sock_cli nor sock_srv set in select's result");
		}

		if (!p)
		{
			SDL_Event e = { .type = SDL_QUIT };
			SDL_PushEvent(&e);
			return 0;
		}

		gboolean from_client = p->flags & PACKET_TO_SERVER;

#if DEBUG_PROTOCOL >= 2 /* use for packet dumping for protocol analysis */
		if (p->id == PACKET_UPDATE_HEALTH /*|| p->id == PACKET_PLAYER_MOVE || p->id == PACKET_PLAYER_MOVE_ROTATE*/)
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

		if (from_client
		    && p->id == PACKET_CHAT
		    && (p->bytes[1] || p->bytes[2] > 2)
		    && memcmp(&p->bytes[3], "\x00/\x00/", 4) == 0)
		{
			g_async_queue_push(worldq, packet_dup(p));
		}
		else
		{
			if (!packet_write(from_client ? sock_srv : sock_cli, p))
				dief("proxy thread (%s) write failed", from_client ? "client -> server" : "server -> client");
		}

		/* communicate interesting chunks to world thread */

		if (!world_running || (p->flags & PACKET_FLAG_IGNORE))
			goto next;

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
		case PACKET_UPDATE_HEALTH:
			g_async_queue_push(worldq, packet_dup(p));
			break;

		case PACKET_CHAT:
			if (!from_client)
			{
				int msglen;
				unsigned char *msg = packet_string(p, 0, &msglen);
				handle_chat(msg, msglen);
				g_free(msg);
			}
			break;
		}

next:
		if (packet_must_free)
			packet_free(p);
	}
	return NULL;
}

void inject_to_client(packet_t *p)
{
	p->flags |= PACKET_TO_CLIENT;
	g_async_queue_push(iq, p);
}

void inject_to_server(packet_t *p)
{
	p->flags |= PACKET_TO_SERVER;
	g_async_queue_push(iq, p);
}

void tell(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	static const char prefix[4] = { 0xc2, 0xa7, 'b', 0 };
	char *cmsg = g_strjoin("", prefix, msg, NULL);

	inject_to_client(packet_new(0, PACKET_CHAT, cmsg));

	g_free(cmsg);
	g_free(msg);
}

void say(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	inject_to_server(packet_new(0, PACKET_CHAT, msg));
	g_free(msg);
}
