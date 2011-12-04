#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

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
#include "proxy.h"

/* proxying thread function to pass packets */

struct proxy_config
{
	packet_state_t state_cli;
	packet_state_t state_srv;
};

static GAsyncQueue *iq = 0;

gpointer proxy_thread(gpointer data);

void start_proxy(socket_t sock_cli, socket_t sock_srv)
{
	iq = g_async_queue_new_full(packet_free);

	/* TODO FIXME; call as world_start("world") or some-such to enable alpha-quality region persistence */
	world_start(0);

	/* start the proxying thread */
	struct proxy_config *cfg = g_new(struct proxy_config, 1);
	cfg->state_cli = (packet_state_t) PACKET_STATE_INIT(sock_cli);
	cfg->state_srv = (packet_state_t) PACKET_STATE_INIT(sock_srv);
	g_thread_create(proxy_thread, cfg, false, 0);
}

gpointer proxy_thread(gpointer data)
{
	struct proxy_config *cfg = data;
	socket_t sock_cli = cfg->state_cli.sock;
	socket_t sock_srv = cfg->state_srv.sock;

	while (1)
	{
		/* read in one packet from the injection queue or socket */

		bool packet_must_free = true;

		struct directed_packet *dpacket = g_async_queue_try_pop(iq);
		struct directed_packet net_dpacket;

		if (!dpacket)
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
				wtf("select returned 0!");

			if (FD_ISSET(sock_cli, &rfds))
			{
				net_dpacket.from = PACKET_FROM_CLIENT;
				net_dpacket.p = packet_read(&cfg->state_cli);
			}
			else if (FD_ISSET(sock_srv, &rfds))
			{
				net_dpacket.from = PACKET_FROM_SERVER;
				net_dpacket.p = packet_read(&cfg->state_srv);
			}
			else
				wtf("Neither sock_cli nor sock_srv set in select's result");

			dpacket = &net_dpacket;
			packet_must_free = false;
		}

		if (!dpacket->p)
		{
			SDL_Event e = { .type = SDL_QUIT };
			SDL_PushEvent(&e);
			return 0;
		}

		packet_t *p = dpacket->p;
		bool from_client = dpacket->from == PACKET_FROM_CLIENT;
		socket_t sto = from_client ? sock_srv : sock_cli;
		char *desc = from_client ? "client -> server" : "server -> client";

#if DEBUG_PROTOCOL == 2 /* use for packet dumping for protocol analysis */
		if (p->type == PACKET_UPDATE_HEALTH /*|| p->type == PACKET_PLAYER_POSITION || p->type == PACKET_PLAYER_POSITION_AND_LOOK*/)
			packet_dump(p);
#endif

#if DEBUG_PROTOCOL >= 3
		packet_dump(p);
#endif

		/* either write it out or handle if it's a command to us */

		if (from_client
		    && p->type == PACKET_CHAT_MESSAGE
		    && (p->bytes[1] || p->bytes[2] > 2)
		    && memcmp(&p->bytes[3], "\x00/\x00/", 4) == 0)
		{
			world_push(dpacket);
		}
		else
		{
			if (!packet_write(sto, p))
				dief("proxy thread (%s) write failed: %s", desc, strerror(errno));
		}

		/* communicate interesting chunks to world thread */

		switch (p->type)
		{
		case PACKET_MAP_CHUNK:
		case PACKET_MULTI_BLOCK_CHANGE:
		case PACKET_BLOCK_CHANGE:
			if (opt.nomap)
				break;
			/* fall-through to processing */

		case PACKET_LOGIN_REQUEST:
		case PACKET_PLAYER_POSITION:
		case PACKET_PLAYER_LOOK:
		case PACKET_PLAYER_POSITION_AND_LOOK:
		case PACKET_NAMED_ENTITY_SPAWN:
		case PACKET_PICKUP_SPAWN:
		case PACKET_MOB_SPAWN:
		case PACKET_DESTROY_ENTITY:
		case PACKET_ENTITY_RELATIVE_MOVE:
		case PACKET_ENTITY_LOOK_AND_RELATIVE_MOVE:
		case PACKET_ENTITY_TELEPORT:
		case PACKET_ATTACH_ENTITY:
		case PACKET_TIME_UPDATE:
		case PACKET_UPDATE_HEALTH:
			world_push(dpacket);
			break;

		case PACKET_CHAT_MESSAGE:
			if (!from_client)
			{
				struct buffer msg = packet_string(p, 0);
				unsigned char *str = msg.data;
				handle_chat(msg);
				g_free(str);
			}
			break;
		}

		if (packet_must_free)
		{
			packet_free(p);
			g_free(dpacket);
		}
	}
	return NULL;
}

void inject_to_client(packet_t *p)
{
	struct directed_packet *dpacket = g_new(struct directed_packet, 1);
	dpacket->from = PACKET_FROM_SERVER;
	dpacket->p = p;
	g_async_queue_push(iq, dpacket);
}

void inject_to_server(packet_t *p)
{
	struct directed_packet *dpacket = g_new(struct directed_packet, 1);
	dpacket->from = PACKET_FROM_CLIENT;
	dpacket->p = p;
	g_async_queue_push(iq, dpacket);
}

void tell(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	static const char prefix[4] = { '\xc2', '\xa7', 'b', 0 };
	char *cmsg = g_strjoin("", prefix, msg, NULL);

	inject_to_client(packet_new(PACKET_CHAT_MESSAGE, cmsg));

	g_free(cmsg);
	g_free(msg);
}

void say(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	inject_to_server(packet_new(PACKET_CHAT_MESSAGE, msg));
	g_free(msg);
}
