#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <libguile.h>
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
#include "scheme.h"

/* proxying thread function to pass packets */

struct proxy_config
{
	packet_state_t state_cli;
	packet_state_t state_srv;
	GAsyncQueue *worldq;
};

static GAsyncQueue *iq = 0;
static GAsyncQueue *worldq = 0;
static struct proxy_config *cfg = 0;

volatile bool kill_proxy = false;

SCM proxy_thread(void *data);

void init_proxy()
{
	for (size_t i = 0; i < NELEMS(packet_hooks); i++)
		packet_hooks[i] = scm_permanent_object(scm_make_hook(scm_from_int(2)));
}

void start_proxy()
{
	/* TODO FIXME; call as world_init("world") or some-such to enable alpha-quality region persistence */
	world_init(0);
	scm_spawn_thread(world_thread, worldq, NULL, NULL);

	/* start the proxying thread */
	scm_spawn_thread(proxy_thread, cfg, NULL, NULL);
}

void proxy_initialize_state()
{
	iq = g_async_queue_new_full(packet_free);
	worldq = g_async_queue_new_full(packet_free);
	cfg = g_new(struct proxy_config, 1);
	cfg->worldq = worldq;
}

void proxy_initialize_socket_state(socket_t sock_cli, socket_t sock_srv)
{
	cfg->state_cli = (packet_state_t) PACKET_STATE_INIT(sock_cli);
	cfg->state_srv = (packet_state_t) PACKET_STATE_INIT(sock_srv);
}

struct buffer proxy_serialize_state()
{
	struct buffer result = { sizeof(packet_state_t)*2, g_malloc(sizeof(packet_state_t)*2) };
	memcpy(result.data, (unsigned char *) &cfg->state_cli, sizeof(packet_state_t));
	memcpy(result.data + sizeof(packet_state_t), (unsigned char *) &cfg->state_srv, sizeof(packet_state_t));
	return result;
}

void proxy_deserialize_state(struct buffer state)
{
	memcpy(&cfg->state_cli, (packet_state_t *) state.data, sizeof(packet_state_t));
	memcpy(&cfg->state_srv, (packet_state_t *) (state.data + sizeof(packet_state_t)), sizeof(packet_state_t));
}


SCM proxy_thread(void *data)
{
	struct proxy_config *cfg = data;
	GAsyncQueue *worldq = cfg->worldq;

	while (1)
	{
		/* read in one packet from the injection queue or socket */

		bool packet_must_free = true;

		if (kill_proxy)
		{
			kill_proxy = false;
			return NULL;
		}

		struct directed_packet *dpacket = g_async_queue_try_pop(iq);
		struct directed_packet net_dpacket;

		if (!dpacket)
		{
			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(cfg->state_cli.sock, &rfds);
			FD_SET(cfg->state_srv.sock, &rfds);
			int nfds = (cfg->state_cli.sock > cfg->state_srv.sock ? cfg->state_cli.sock : cfg->state_srv.sock) + 1;
			int ret = 0;

			do
				ret = select(nfds, &rfds, NULL, NULL, NULL);
			while (ret == -1 && errno == EINTR);

			if (ret == -1)
				dief("select: %s", strerror(errno));
			else if (ret == 0)
				wtf("select returned 0!");

			if (FD_ISSET(cfg->state_cli.sock, &rfds))
			{
				net_dpacket.from = PACKET_FROM_CLIENT;
				net_dpacket.p = packet_read(&cfg->state_cli);
			}
			else if (FD_ISSET(cfg->state_srv.sock, &rfds))
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
		socket_t sto = from_client ? cfg->state_srv.sock : cfg->state_cli.sock;
		char *desc = from_client ? "client -> server" : "server -> client";

#if DEBUG_PROTOCOL >= 2 /* use for packet dumping for protocol analysis */
		if (p->type == PACKET_UPDATE_HEALTH /*|| p->type == PACKET_PLAYER_MOVE || p->type == PACKET_PLAYER_MOVE_ROTATE*/)
		{
			int i, nf = packet_nfields(p);

			fprintf(stderr, "packet: %u [%s]\n", p->type, desc);
			for (i = 0; i < nf; i++)
			{
				fprintf(stderr, "  field %d:", i);
				for (unsigned u = p->field_offset[i]; u < p->field_offset[i+1]; u++)
					fprintf(stderr, " %02x", p->bytes[u]);
				fprintf(stderr, "\n");
			}
		}
#endif

		/* pass it to Scheme */

		SCM packet_smob = make_packet_smob(p);
		SCM hook = packet_hooks[p->type];
		/* FIXME: Need to spawn a thread of some kind to avoid complex handlers lagging
		   everything down (but maybe this should be up to the handlers?) */
		/* FIXME: Need to handle exceptions */
		scm_c_run_hook(hook,
			scm_list_2(from_client ? sym_client : sym_server, packet_smob));

		/* either write it out or handle if it's a command to us */

		if (from_client
		    && p->type == PACKET_CHAT
		    && (p->bytes[1] || p->bytes[2] > 2)
		    && memcmp(&p->bytes[3], "\x00/\x00/", 4) == 0)
		{
			/* TODO: Eliminate duplication with this and the later injection */
			struct directed_packet *dpacket_copy = g_new(struct directed_packet, 1);
			dpacket_copy->from = dpacket->from;
			dpacket_copy->p = packet_dup(p);
			g_async_queue_push(worldq, dpacket_copy);
		}
		else
		{
			if (!packet_write(sto, p))
				dief("proxy thread (%s) write failed", desc);
		}

		/* communicate interesting chunks to world thread */

		if (!world_running)
			goto next;

		switch (p->type)
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
			{
				struct directed_packet *dpacket_copy = g_new(struct directed_packet, 1);
				dpacket_copy->from = dpacket->from;
				dpacket_copy->p = packet_dup(p);
				g_async_queue_push(worldq, dpacket_copy);
			}
			break;

		case PACKET_CHAT:
			if (!from_client)
			{
				struct buffer msg = packet_string(p, 0);
				unsigned char *str = msg.data;
				handle_chat(msg);
				g_free(str);
			}
			break;
		}

next:
		if (packet_must_free)
		{
			packet_free(p);
			g_free(dpacket);
		}
	}

	return SCM_UNSPECIFIED;
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

	inject_to_client(packet_new(PACKET_CHAT, cmsg));

	g_free(cmsg);
	g_free(msg);
}

void say(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	inject_to_server(packet_new(PACKET_CHAT, msg));
	g_free(msg);
}
