#ifndef MCMAP_PROTOCOL_H
#define MCMAP_PROTOCOL_H 1

#include <glib.h>

#include "protocol-data.h"

enum packet_dir
{
	PACKET_TO_ANY,
	PACKET_TO_CLIENT,
	PACKET_TO_SERVER
};

enum field_type
{
	FIELD_BYTE,
	FIELD_SHORT,
	FIELD_INT,
	FIELD_LONG,
	FIELD_FLOAT,
	FIELD_DOUBLE,
	FIELD_STRING,
	FIELD_ITEM,
	FIELD_BYTE_ARRAY,
	FIELD_BLOCK_ARRAY,
	FIELD_ITEM_ARRAY,
	FIELD_EXPLOSION_ARRAY,
	FIELD_ENTITY_DATA
};

struct packet
{
	enum packet_dir dir;
	unsigned id;
	unsigned size;
	unsigned char *bytes;
	unsigned *field_offset;
};

typedef struct packet packet_t;

#define MAX_PACKET_SIZE 262144
#define MAX_FIELDS 16

struct packet_state
{
	unsigned char buf[MAX_PACKET_SIZE];
	unsigned buf_start, buf_pos, buf_end;
	unsigned offset[MAX_FIELDS];
	struct packet p;
};

typedef struct packet_state packet_state_t;

#define PACKET_STATE_INIT(d) { .buf_start = 0, .buf_pos = 0, .buf_end = 0, .p = { .dir = d } }

packet_t *packet_read(socket_t sock, packet_state_t *state);

int packet_write(socket_t sock, packet_t *packet);

packet_t *packet_dup(packet_t *packet);

packet_t *packet_new(enum packet_dir dir, enum packet_id type, ...);

void packet_free(gpointer packet);

int packet_nfields(packet_t *packet);

int packet_int(packet_t *packet, unsigned field);
long long packet_long(packet_t *packet, unsigned field);
double packet_double(packet_t *packet, unsigned field);
unsigned char *packet_string(packet_t *packet, unsigned field, int *len);

#endif /* MCMAP_PROTOCOL_H */
