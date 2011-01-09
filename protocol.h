#ifndef MCMAP_PROTOCOL_H
#define MCMAP_PROTOCOL_H 1

#include <gio/gio.h>

enum packet_id
{
	PACKET_KEEPALIVE = 0x00,
	PACKET_LOGIN = 0x01,
	PACKET_HANDSHAKE = 0x02,
	PACKET_CHAT = 0x03,
	PACKET_TIME = 0x04,
	PACKET_ENTITY_EQUIPMENT = 0x05,
	PACKET_SPAWN_POSITION = 0x06,
	PACKET_USE_ENTITY = 0x07,
	PACKET_UPDATE_HEALTH = 0x08,
	PACKET_RESPAWN = 0x09,
	PACKET_PLAYER_PING = 0x0a,
	PACKET_PLAYER_MOVE = 0x0b,
	PACKET_PLAYER_ROTATE = 0x0c,
	PACKET_PLAYER_MOVE_ROTATE = 0x0d,
	PACKET_DIG = 0x0e,
	PACKET_PLACE = 0x0f,
	PACKET_ENTITY_HOLDING = 0x10,
	PACKET_ENTITY_ANIMATE = 0x12,
	PACKET_ENTITY_SPAWN_NAMED = 0x14,
	PACKET_ENTITY_SPAWN_PICKUP = 0x15,
	PACKET_ENTITY_COLLECT = 0x16,
	PACKET_ENTITY_SPAWN_OBJECT = 0x17,
	PACKET_MOB_SPAWN = 0x18,
	PACKET_ENTITY_VELOCITY = 0x1c,
	PACKET_ENTITY_DESTROY = 0x1d,
	PACKET_ENTITY = 0x1e,
	PACKET_ENTITY_REL_MOVE = 0x1f,
	PACKET_ENTITY_LOOK = 0x20,
	PACKET_ENTITY_REL_MOVE_LOOK = 0x21,
	PACKET_ENTITY_MOVE = 0x22,
	PACKET_ENTITY_DAMAGE = 0x26,
	PACKET_ENTITY_ATTACH = 0x27,
	PACKET_PRECHUNK = 0x32,
	PACKET_CHUNK = 0x33,
	PACKET_MULTI_SET_BLOCK = 0x34,
	PACKET_SET_BLOCK = 0x35,
	PACKET_EXPLOSION = 0x3c,
	PACKET_INVENTORY_OPEN = 0x64,
	PACKET_INVENTORY_CLOSE = 0x65,
	PACKET_INVENTORY_CLICK = 0x66,
	PACKET_INVENTORY_UPDATE  = 0x67,
	PACKET_INVENTORY_DATA = 0x68,
	PACKET_INVENTORY_PROGRESS = 0x69,
	PACKET_INVENTORY_ACK = 0x6a,
	PACKET_SIGN_UPDATE = 0x82,
	PACKET_DISCONNECT = 0xff
};

enum packet_dir
{
	PACKET_TO_ANY,
	PACKET_TO_CLIENT,
	PACKET_TO_SERVER
};

enum field_type
{
	FIELD_BYTE,
	FIELD_UBYTE,
	FIELD_SHORT,
	FIELD_INT,
	FIELD_LONG,
	FIELD_FLOAT,
	FIELD_DOUBLE,
	FIELD_STRING,
	FIELD_ARRAY,
	FIELD_IARRAY,
	FIELD_IITEM,
	FIELD_SBBARRAY,
	FIELD_EXPLOSION_RECORDS
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

packet_t *packet_read(GSocket *sock, packet_state_t *state);

int packet_write(GSocket *sock, packet_t *packet);

packet_t *packet_dup(packet_t *packet);

packet_t *packet_new(enum packet_dir dir, enum packet_id type, ...);

void packet_free(gpointer packet);

int packet_int(packet_t *packet, unsigned field);
long long packet_long(packet_t *packet, unsigned field);
double packet_double(packet_t *packet, unsigned field);
unsigned char *packet_string(packet_t *packet, unsigned field, int *len);

#endif /* MCMAP_PROTOCOL_H */
