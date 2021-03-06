#ifndef MCMAP_PROTOCOL_H
#define MCMAP_PROTOCOL_H 1

enum packet_id {
#define PACKET(id, cname, nfields, ...) \
	PACKET_##cname = id,
#include "protocol.def"
#undef PACKET
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
	FIELD_ITEM,
	FIELD_BYTE_ARRAY,
	FIELD_BLOCK_ARRAY,
	FIELD_ITEM_ARRAY,
	FIELD_EXPLOSION_ARRAY,
	FIELD_MAP_ARRAY,
	FIELD_ENTITY_DATA,
	FIELD_OBJECT_DATA,
};

struct packet_format_desc
{
	unsigned nfields;
	enum field_type *ftype;
	unsigned char known;
};

struct packet_format_desc packet_format[256];

struct packet
{
	unsigned type;
	unsigned size;
	unsigned char *bytes;
	unsigned *field_offset;
};

typedef struct packet packet_t;

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

#define MAX_PACKET_SIZE 262144
#define MAX_FIELDS 16

struct packet_state
{
	socket_t sock;
	unsigned char buf[MAX_PACKET_SIZE];
	unsigned buf_start, buf_pos, buf_end;
	unsigned offset[MAX_FIELDS];
	struct packet p;
};

typedef struct packet_state packet_state_t;

#define PACKET_STATE_INIT(s) \
	{ \
		.sock = (s), \
		.buf_start = 0, \
		.buf_pos = 0, \
		.buf_end = 0, \
		.p = { 0, 0, 0, 0 }, \
	}

packet_t *packet_read(packet_state_t *state);

int packet_write(socket_t sock, packet_t *packet);

packet_t *packet_dup(packet_t *packet);

struct packet_constructor
{
	enum packet_id type;
	GByteArray *data;
	GArray *offsets;
	unsigned offset;
};

typedef struct packet_constructor packet_constructor_t;

packet_constructor_t packet_create(enum packet_id type);
void packet_add_jbyte(packet_constructor_t *pc, jbyte v);
void packet_add_jubyte(packet_constructor_t *pc, jubyte v);
void packet_add_jshort(packet_constructor_t *pc, jshort v);
void packet_add_jint(packet_constructor_t *pc, jint v);
void packet_add_jlong(packet_constructor_t *pc, jlong v);
void packet_add_jfloat(packet_constructor_t *pc, jfloat v);
void packet_add_jdouble(packet_constructor_t *pc, jdouble v);
void packet_add_string(packet_constructor_t *pc, unsigned char *v);
packet_t *packet_construct(packet_constructor_t *pc);

packet_t *packet_new(enum packet_id type, ...);

void packet_free(gpointer packet);

int packet_nfields(packet_t *packet);

jint packet_int(packet_t *packet, unsigned field);
jlong packet_long(packet_t *packet, unsigned field);
double packet_double(packet_t *packet, unsigned field);
struct buffer packet_string(packet_t *packet, unsigned field);

void packet_dump(packet_t *packet);

#endif /* MCMAP_PROTOCOL_H */
