#ifndef MCMAP_PROXY_H
#define MCMAP_PROXY_H 1

void start_proxy(socket_t sock_cli, socket_t sock_srv);

/* packet injection */
void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

void chat(char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

#endif /* MCMAP_PROXY_H */
