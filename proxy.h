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

extern volatile bool kill_proxy;

void start_proxy();
void proxy_initialize_state();
void proxy_initialize_socket_state(packet_state_t *state_cli, packet_state_t *state_srv);
struct buffer proxy_serialize_state();
void proxy_deserialize_state(struct buffer state);

/* packet injection */
void inject_to_client(packet_t *p);
void inject_to_server(packet_t *p);

void tell(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* MCMAP_PROXY_H */
