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

void init_proxy(void);
void start_proxy(socket_t sock_cli, socket_t sock_srv);

/* packet injection */
void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

void tell(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* MCMAP_PROXY_H */
