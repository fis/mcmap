#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "protocol.h"

/*
 * reference:
 * https://gist.github.com/727175
 * http://mc.kev009.com/wiki/Protocol
 */

/* packet formats */

enum field_type packet_format_login[] = {
	FIELD_INT,    /* cli->srv: protocol version; srv->cli: entity id */
	FIELD_STRING, /* cli->srv: nickname */
	FIELD_STRING, /* cli->srv: password */
	FIELD_LONG,
	FIELD_UBYTE
};

enum field_type packet_format_handshake[] = {
	FIELD_STRING
};

enum field_type packet_format_chat[] = {
	FIELD_STRING
};

enum field_type packet_format_time[] = {
	FIELD_LONG
};

enum field_type packet_format_inventory[] = {
	FIELD_INT, FIELD_IARRAY
};

enum field_type packet_format_spawn_position[] = {
	FIELD_INT, FIELD_INT, FIELD_INT
};

enum field_type packet_format_use_entity[] = {
	FIELD_INT, FIELD_INT, FIELD_BYTE
};

enum field_type packet_format_update_health[] = {
	FIELD_UBYTE
};

enum field_type packet_format_player_ping[] = {
	FIELD_BYTE
};

enum field_type packet_format_player_move[] = {
	FIELD_DOUBLE, FIELD_DOUBLE, FIELD_DOUBLE, FIELD_DOUBLE, FIELD_BYTE
};

enum field_type packet_format_player_rotate[] = {
	FIELD_FLOAT, FIELD_FLOAT, FIELD_BYTE
};

enum field_type packet_format_player_move_rotate[] = {
	FIELD_DOUBLE, FIELD_DOUBLE, FIELD_DOUBLE, FIELD_DOUBLE,
	FIELD_FLOAT, FIELD_FLOAT, FIELD_BYTE
};

enum field_type packet_format_dig[] = {
	FIELD_UBYTE, FIELD_INT, FIELD_UBYTE, FIELD_INT, FIELD_UBYTE
};

enum field_type packet_format_place[] = {
	FIELD_SHORT, FIELD_INT, FIELD_UBYTE, FIELD_INT, FIELD_UBYTE
};

enum field_type packet_format_entity_holding[] = {
	FIELD_INT, FIELD_SHORT
};

enum field_type packet_format_entity_collect[] = {
	FIELD_INT, FIELD_INT
};

enum field_type packet_format_add_object_or_vehicle[] = {
	FIELD_INT, FIELD_UBYTE, FIELD_INT, FIELD_INT, FIELD_INT
};

enum field_type packet_format_mob_spawn[] = {
	FIELD_INT, FIELD_UBYTE, FIELD_INT, FIELD_INT, FIELD_INT, FIELD_UBYTE, FIELD_BYTE
};

enum field_type packet_format_inventory_add[] = {
	FIELD_SHORT, FIELD_BYTE, FIELD_SHORT
};

enum field_type packet_format_entity_animate[] = {
	FIELD_INT, FIELD_UBYTE
};

enum field_type packet_format_entity_spawn_named[] = {
	FIELD_INT, FIELD_STRING, FIELD_INT, FIELD_INT, FIELD_INT,
	FIELD_UBYTE, FIELD_BYTE, FIELD_SHORT
};

enum field_type packet_format_entity_spawn_typed[] = {
	FIELD_INT, FIELD_SHORT, FIELD_UBYTE, FIELD_INT, FIELD_INT, FIELD_INT,
	FIELD_UBYTE, FIELD_BYTE, FIELD_BYTE
};

enum field_type packet_format_entity_velocity[] = {
	FIELD_INT, FIELD_SHORT, FIELD_SHORT, FIELD_SHORT
};

enum field_type packet_format_entity_destroy[] = {
	FIELD_INT
};

enum field_type packet_format_entity[] = {
	FIELD_INT
};

enum field_type packet_format_entity_rel_move[] = {
	FIELD_INT, FIELD_BYTE, FIELD_BYTE, FIELD_BYTE
};

enum field_type packet_format_entity_look[] = {
	FIELD_INT, FIELD_UBYTE, FIELD_BYTE
};

enum field_type packet_format_entity_rel_move_look[] = {
	FIELD_INT, FIELD_BYTE, FIELD_BYTE, FIELD_BYTE, FIELD_UBYTE, FIELD_BYTE
};

enum field_type packet_format_entity_move[] = {
	FIELD_INT, FIELD_INT, FIELD_INT, FIELD_INT, FIELD_UBYTE, FIELD_BYTE
};

enum field_type packet_format_entity_damage[] = {
	FIELD_INT, FIELD_BYTE
};

enum field_type packet_format_entity_attach[] = {
	FIELD_INT, FIELD_INT
};

enum field_type packet_format_prechunk[] = {
	FIELD_INT, FIELD_INT, FIELD_UBYTE
};

enum field_type packet_format_multi_set_block[] = {
	FIELD_INT, FIELD_INT, FIELD_SBBARRAY
};

enum field_type packet_format_set_block[] = {
	FIELD_INT, FIELD_UBYTE, FIELD_INT, FIELD_UBYTE, FIELD_UBYTE
};

enum field_type packet_format_chunk[] = {
	FIELD_INT,   /* lowest X coordinate */
	FIELD_SHORT, /* lowest Y coordinate (height) */
	FIELD_INT,   /* lowest Z coordinate */
	FIELD_UBYTE, /* chunk X-size */
	FIELD_UBYTE, /* chunk Y-size */
	FIELD_UBYTE, /* chunk Z-size */
	FIELD_ARRAY  /* blocks */
};

enum field_type packet_format_complex_entity[] = {
	FIELD_INT, FIELD_SHORT, FIELD_INT, FIELD_STRING
};

enum field_type packet_format_explosion[] = {
	FIELD_DOUBLE, FIELD_DOUBLE, FIELD_DOUBLE, FIELD_FLOAT, FIELD_EXPLOSION_RECORDS
};

enum field_type packet_format_disconnect[] = {
	FIELD_STRING
};

struct packet_format_desc
{
	unsigned nfields;
	enum field_type *ftype;
	unsigned char known;
};

#define P(a) { ((sizeof a)/(sizeof *a)), a, 1 }
struct packet_format_desc packet_format[] =
{
	[PACKET_KEEPALIVE] = { 0, 0, 1 },
	[PACKET_LOGIN] = P(packet_format_login),
	[PACKET_HANDSHAKE] = P(packet_format_handshake),
	[PACKET_CHAT] = P(packet_format_chat),
	[PACKET_TIME] = P(packet_format_time),
	[PACKET_INVENTORY] = P(packet_format_inventory),
	[PACKET_SPAWN_POSITION] = P(packet_format_spawn_position),
	[PACKET_USE_ENTITY] = P(packet_format_use_entity),
	[PACKET_UPDATE_HEALTH] = P(packet_format_update_health),
	[PACKET_RESPAWN] = { 0, 0, 1 },
	[PACKET_PLAYER_PING] = P(packet_format_player_ping),
	[PACKET_PLAYER_MOVE] = P(packet_format_player_move),
	[PACKET_PLAYER_ROTATE] = P(packet_format_player_rotate),
	[PACKET_PLAYER_MOVE_ROTATE] = P(packet_format_player_move_rotate),
	[PACKET_DIG] = P(packet_format_dig),
	[PACKET_PLACE] = P(packet_format_place),
	[PACKET_ENTITY_HOLDING] = P(packet_format_entity_holding),
	[PACKET_INVENTORY_ADD] = P(packet_format_inventory_add),
	[PACKET_ENTITY_ANIMATE] = P(packet_format_entity_animate),
	[PACKET_ENTITY_SPAWN_NAMED] = P(packet_format_entity_spawn_named),
	[PACKET_ENTITY_SPAWN_TYPED] = P(packet_format_entity_spawn_typed),
	[PACKET_ENTITY_COLLECT] = P(packet_format_entity_collect),
	[PACKET_ADD_OBJECT_OR_VEHICLE] = P(packet_format_add_object_or_vehicle),
	[PACKET_MOB_SPAWN] = P(packet_format_mob_spawn),
	[PACKET_ENTITY_VELOCITY] = P(packet_format_entity_velocity),
	[PACKET_ENTITY_DESTROY] = P(packet_format_entity_destroy),
	[PACKET_ENTITY] = P(packet_format_entity),
	[PACKET_ENTITY_REL_MOVE] = P(packet_format_entity_rel_move),
	[PACKET_ENTITY_LOOK] = P(packet_format_entity_look),
	[PACKET_ENTITY_REL_MOVE_LOOK] = P(packet_format_entity_rel_move_look),
	[PACKET_ENTITY_MOVE] = P(packet_format_entity_move),
	[PACKET_ENTITY_DAMAGE] = P(packet_format_entity_damage),
	[PACKET_ENTITY_ATTACH] = P(packet_format_entity_attach),
	[PACKET_PRECHUNK] = P(packet_format_prechunk),
	[PACKET_CHUNK] = P(packet_format_chunk),
	[PACKET_MULTI_SET_BLOCK] = P(packet_format_multi_set_block),
	[PACKET_SET_BLOCK] = P(packet_format_set_block),
	[PACKET_COMPLEX_ENTITY] = P(packet_format_complex_entity),
	[PACKET_EXPLOSION] = P(packet_format_explosion),
	[PACKET_DISCONNECT] = P(packet_format_disconnect)
};
#undef P
#define MAX_PACKET_FORMAT ((sizeof packet_format)/(sizeof *packet_format))

/* packet reading/writing */

packet_t *packet_read(GSocket *sock, packet_state_t *state)
{
	unsigned char *buf = state->buf;
	unsigned buf_start = state->buf_start, buf_pos = state->buf_pos, buf_end = state->buf_end;

	int buf_fill(void)
	{
		if (buf_start > 0 && buf_end == MAX_PACKET_SIZE)
		{
			memmove(buf, buf+buf_start, buf_end-buf_start);
			buf_pos -= buf_start;
			buf_end -= buf_start;
			buf_start = 0;
		}

		gssize got = g_socket_receive(sock, (gchar*)(buf+buf_end), MAX_PACKET_SIZE - buf_end, 0, 0);
		if (got <= 0)
		{
			buf_pos = buf_start = buf_end = 0;
			return 0;
		}

		buf_end += got;
		return 1;
	}

	int buf_skip(unsigned n)
	{
		while (buf_pos + n > buf_end)
			if (!buf_fill())
				return 0;
		buf_pos += n;
		return 1;
	}

	int buf_getc(void)
	{
		if (buf_pos == buf_end)
			if (!buf_fill())
				return -1;
		return buf[buf_pos++];
	}

	int buf_get_i16(void)
	{
		if (!buf_skip(2))
			die("out of data while getting i16");
		int v = (buf[buf_pos-2] << 8) | buf[buf_pos-1];
		return v >= 0x8000 ? v - 0x10000 : v;
	}

	int buf_get_i32(void)
	{
		if (!buf_skip(4))
			die("out of data while getting i32");
		return (buf[buf_pos-4] << 24) | (buf[buf_pos-3] << 16) | (buf[buf_pos-2] << 8) | buf[buf_pos-1];
	}

	int t = buf_getc();
	if (t < 0)
		return 0;

	state->p.id = t;

	struct packet_format_desc *fmt;
	if (t >= MAX_PACKET_FORMAT || !(fmt = &packet_format[t])->known)
	{
		fprintf(stderr, "unknown packet id: 0x%02x\n", t);
		return 0;
	}

	state->p.field_offset = state->offset;

	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		state->p.field_offset[f] = buf_pos - buf_start;

		switch (fmt->ftype[f])
		{
		case FIELD_BYTE:
		case FIELD_UBYTE:
			if (!buf_skip(1)) return 0;
			break;

		case FIELD_SHORT:
			if (!buf_skip(2)) return 0;
			break;

		case FIELD_INT:
		case FIELD_FLOAT:
			if (!buf_skip(4)) return 0;
			break;

		case FIELD_LONG:
		case FIELD_DOUBLE:
			if (!buf_skip(8)) return 0;
			break;

		case FIELD_STRING:
			t = buf_get_i16();
			if (!buf_skip(t)) return 0;
			break;

		case FIELD_ARRAY:
			t = buf_get_i32();
			if (!buf_skip(t)) return 0;
			break;

		case FIELD_IARRAY:
			t = buf_get_i16();
			for (int i = 0; i < t; i++)
			{
				short itype = buf_get_i16();
				if (itype != -1)
					if (!buf_skip(3)) return 0;
			}
			break;

		case FIELD_SBBARRAY:
			t = buf_get_i16();
			if (!buf_skip(4*t)) return 0;
			break;

		case FIELD_EXPLOSION_RECORDS:
			t = buf_get_i32();
			if (!buf_skip(3*t)) return 0;
			break;
		}
	}

	state->p.size = buf_pos - buf_start;
	state->p.bytes = &buf[buf_start];

	state->buf_start = buf_pos;
	state->buf_pos = buf_pos;
	state->buf_end = buf_end;

	return &state->p;
}

int packet_write(GSocket *sock, packet_t *packet)
{
	gsize left = packet->size;
	gchar *p = (gchar*)packet->bytes;

	while (left)
	{
		gssize sent = g_socket_send(sock, p, left, 0, 0);
		if (sent < 0)
			return 0;
		left -= sent;
		p += sent;
	}

	return 1;
}

packet_t *packet_dup(packet_t *packet)
{
	packet_t *newp = g_malloc(sizeof *newp);

	newp->id = packet->id;
	newp->size = packet->size;
	newp->bytes = g_memdup(packet->bytes, packet->size);
	newp->field_offset = g_memdup(packet->field_offset, packet_format[packet->id].nfields * sizeof *newp->field_offset);

	return newp;
}

void packet_free(packet_t *packet)
{
	g_free(packet->bytes);
	g_free(packet->field_offset);
	g_free(packet);
}

int packet_int(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];
	int t = 0;

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_BYTE:
		return *(signed char *)p;

	case FIELD_UBYTE:
		return *p;

	case FIELD_SHORT:
		t = (p[0] << 8) | p[1];
		return t >= 0x8000 ? t - 0x10000 : t;

	case FIELD_INT:
		return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];

	default:
		dief("can't interpret field type %d as int (packet 0x%02x, field %u)",
		     packet_format[packet->id].ftype[field], packet->id, field);
	}
}

double packet_double(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];
	unsigned char buf[8];

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_FLOAT:
		buf[0] = p[3]; buf[1] = p[2]; buf[2] = p[1]; buf[3] = p[0];
		return *(float *)buf;

	case FIELD_DOUBLE:
		buf[0] = p[7]; buf[1] = p[6]; buf[2] = p[5]; buf[3] = p[4];
		buf[4] = p[3]; buf[5] = p[2]; buf[6] = p[1]; buf[7] = p[0];
		return *(double *)buf;

	default:
		return packet_int(packet, field);
	}
}

unsigned char *packet_string(packet_t *packet, unsigned field, int *len)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_STRING:
		*len = (p[0] << 8) | p[1];
		return &p[2];

	default:
		dief("can't interpret field type %d as string (packet 0x%02x, field %u)",
		     packet_format[packet->id].ftype[field], packet->id, field);
	}
}
