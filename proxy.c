#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

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
	socket_t sock_from, sock_to;
	int client_to_server;
	GAsyncQueue *q;
	GAsyncQueue *iq;
};

static GAsyncQueue *iq_client = 0;
static GAsyncQueue *iq_server = 0;

gpointer proxy_thread(gpointer data);

void start_proxy(socket_t sock_cli, socket_t sock_srv)
{
	iq_client = g_async_queue_new_full(packet_free);
	iq_server = g_async_queue_new_full(packet_free);

	GAsyncQueue *packetq = g_async_queue_new_full(packet_free);

	/* TODO FIXME; call as world_init("world") or some-such to enable alpha-quality region persistence */
	world_init(0);
	g_thread_create(world_thread, packetq, FALSE, 0);

	struct proxy_config *proxy_client_server = g_new(struct proxy_config, 2);
	struct proxy_config *proxy_server_client = proxy_client_server + 1;

	proxy_client_server->sock_from = sock_cli;
	proxy_client_server->sock_to = sock_srv;
	proxy_client_server->client_to_server = 1;
	proxy_client_server->q = packetq;
	proxy_client_server->iq = iq_server;

	proxy_server_client->sock_from = sock_srv;
	proxy_server_client->sock_to = sock_cli;
	proxy_server_client->client_to_server = 0;
	proxy_server_client->q = packetq;
	proxy_server_client->iq = iq_client;

	/* start the proxying threads */

	g_thread_create(proxy_thread, proxy_client_server, FALSE, 0);
	g_thread_create(proxy_thread, proxy_server_client, FALSE, 0);
}

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

		if (cfg->client_to_server
		    && p->id == PACKET_CHAT
		    && (p->bytes[1] || p->bytes[2] > 2)
		    && memcmp(&p->bytes[3], "\x00/\x00/", 4) == 0)
		{
			g_async_queue_push(cfg->q, packet_dup(p));
		}
		else
		{
			if (!packet_write(sto, p))
				dief("proxy thread (%s) write failed", desc);
		}

		/* communicate interesting chunks to world thread */

		if (!world_running || (p->flags & PACKET_FLAG_IGNORE))
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
		case PACKET_UPDATE_HEALTH:
			g_async_queue_push(cfg->q, packet_dup(p));
			break;

		case PACKET_CHAT:
			if (!cfg->client_to_server)
			{
				int msglen;
				unsigned char *msg = packet_string(p, 0, &msglen);
				handle_chat(msg, msglen);
				g_free(msg);
			}
			break;
		}

		if (packet_must_free)
			packet_free(p);
	}
	return NULL;
}

void inject_to_client(packet_t *p)
{
	g_async_queue_push(iq_client, p);
}

void inject_to_server(packet_t *p)
{
	g_async_queue_push(iq_server, p);
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
