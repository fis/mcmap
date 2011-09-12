#ifndef MCMAP_PROXY_H
#define MCMAP_PROXY_H 1

enum packet_origin
{
	PACKET_FROM_CLIENT,
	PACKET_FROM_SERVER,
};

struct directed_packet
{
	enum packet_origin from;
	packet_t *p;
};

SCM packet_hooks[256];

extern volatile bool kill_proxy;

void init_proxy(void);
void start_proxy();
void proxy_initialize_state();
void proxy_initialize_socket_state(socket_t sock_cli, socket_t sock_srv);
struct buffer proxy_serialize_state();
void proxy_deserialize_state(struct buffer state);

/* packet injection */
void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

void tell(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* MCMAP_PROXY_H */
