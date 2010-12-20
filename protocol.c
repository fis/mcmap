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
	FIELD_SHORT
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

enum field_type packet_format_entity_spawn_object[] = {
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

enum field_type packet_format_entity_spawn_pickup[] = {
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

enum field_type packet_format_beta_inventory_close[] = {
	FIELD_UBYTE /* unknown */
};

enum field_type packet_format_beta_inventory_click[] = {
	FIELD_UBYTE,              /* unknown */
	FIELD_SHORT,              /* inventory position index */
	FIELD_UBYTE, FIELD_SHORT, /* unknown */
	FIELD_IITEM               /* item that was clicked */
};

enum field_type packet_format_beta_unknown1[] = {
	FIELD_SHORT, /* unknown */
	FIELD_UBYTE, /* unknown */
	FIELD_IITEM  /* unknown */
};

enum field_type packet_format_beta_inventory_data[] = {
	FIELD_UBYTE, /* unknown */
	FIELD_IARRAY /* inventory array */
};

enum field_type packet_format_beta_inventory_ack[] = {
	FIELD_SHORT, FIELD_SHORT /* unknown */
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
	[PACKET_ENTITY_SPAWN_PICKUP] = P(packet_format_entity_spawn_pickup),
	[PACKET_ENTITY_COLLECT] = P(packet_format_entity_collect),
	[PACKET_ENTITY_SPAWN_OBJECT] = P(packet_format_entity_spawn_object),
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
	[PACKET_BETA_INVENTORY_CLOSE] = P(packet_format_beta_inventory_close),
	[PACKET_BETA_INVENTORY_CLICK] = P(packet_format_beta_inventory_click),
	[PACKET_BETA_UNKNOWN1] = P(packet_format_beta_unknown1),
	[PACKET_BETA_INVENTORY_DATA] = P(packet_format_beta_inventory_data),
	[PACKET_BETA_INVENTORY_ACK] = P(packet_format_beta_inventory_ack),
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
#if DEBUG_PROTOCOL >= 1
		log_print("IMMINENT CRASH, reading tail for log");
		unsigned char buf[256];
		for (int i = 0; i < sizeof buf; i++) buf[i] = buf_getc();
		for (int i = 0; i < sizeof buf; i+=16)
			log_print("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			          buf[i+0],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7],
			          buf[i+8],buf[i+9],buf[i+10],buf[i+11],buf[i+12],buf[i+13],buf[i+14],buf[i+15]);
#endif
		dief("Unknown packet id: 0x%02x (dir %d)", t, state->p.dir);
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

		case FIELD_IITEM:
			t = buf_get_i16();
			if (t != -1)
				if (!buf_skip(2)) return 0;
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

	newp->dir = packet->dir;
	newp->id = packet->id;
	newp->size = packet->size;
	newp->bytes = g_memdup(packet->bytes, packet->size);
	newp->field_offset = g_memdup(packet->field_offset, packet_format[packet->id].nfields * sizeof *newp->field_offset);

	return newp;
}

packet_t *packet_new(enum packet_dir dir, enum packet_id type, ...)
{
	GByteArray *data = g_byte_array_new();
	GArray *offsets = g_array_new(FALSE, FALSE, sizeof(unsigned));

	/* add the type byte */

	{
		guint8 typebyte = type;
		g_byte_array_append(data, &typebyte, 1);
	}

	unsigned offset = 1;

	/* build the fields */

	va_list ap;
	va_start(ap, type);

	unsigned nfields = packet_format[type].nfields;
	for (unsigned field = 0; field < nfields; field++)
	{
		int t;
		long long tl;
		double td;
		unsigned char *tp;

		g_array_append_val(offsets, offset);

		switch (packet_format[type].ftype[field])
		{
		case FIELD_BYTE:
			t = va_arg(ap, int);
			{
				signed char v = t;
				g_byte_array_append(data, (unsigned char *)&v, 1);
				offset++;
			}
			break;

		case FIELD_UBYTE:
			t = va_arg(ap, int);
			{
				unsigned char v = t;
				g_byte_array_append(data, &v, 1);
				offset++;
			}
			break;

		case FIELD_SHORT:
			t = va_arg(ap, int);
			{
				unsigned char v[2] = { t >> 8, t };
				g_byte_array_append(data, v, 2);
				offset += 2;
			}
			break;

		case FIELD_INT:
			t = va_arg(ap, int);
			{
				unsigned char v[4] = { t >> 24, t >> 16, t >> 8, t };
				g_byte_array_append(data, v, 4);
				offset += 4;
			}
			break;

		case FIELD_LONG:
			tl = va_arg(ap, long long);
			{
				unsigned char v[8] = {
					tl >> 56, tl >> 48, tl >> 40, tl >> 32,
					tl >> 24, tl >> 16, tl >> 8, tl
				};
				g_byte_array_append(data, v, 8);
				offset += 8;
			}
			break;

		case FIELD_FLOAT:
			td = va_arg(ap, double);
			{
				unsigned char *p = (unsigned char *)&td;
				unsigned char v[4] = { p[3], p[2], p[1], p[0] };
				g_byte_array_append(data, v, 4);
				offset += 4;
			}
			break;

		case FIELD_DOUBLE:
			td = va_arg(ap, double);
			{
				unsigned char *p = (unsigned char *)&td;
				unsigned char v[8] = { p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0] };
				g_byte_array_append(data, v, 8);
				offset += 8;
			}
			break;

		case FIELD_STRING:
			tp = va_arg(ap, unsigned char *);
			{
				int len = strlen((char *)tp);
				unsigned char lenb[2] = { len >> 8, len };
				g_byte_array_append(data, lenb, 2);
				g_byte_array_append(data, tp, len);
				offset += 2 + len;
			}
			break;

		default:
			dief("unhandled field type %d (packet 0x%02x, field %u)",
			     packet_format[type].ftype[field], type, field);
		}
	}

	va_end(ap);

	/* construct the actual packet */

	packet_t *p = g_malloc(sizeof *p);

	p->dir = dir;
	p->id = type;
	p->size = offset;
	p->bytes = g_byte_array_free(data, FALSE);
	p->field_offset = (unsigned *)g_array_free(offsets, FALSE);

	return p;
}

void packet_free(gpointer packet)
{
	packet_t *p = packet;
	g_free(p->bytes);
	g_free(p->field_offset);
	g_free(p);
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

	union {
		float f;
		unsigned char b[4];
	} bfloat;

	union {
		double d;
		unsigned char b[8];
	} bdouble;

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_FLOAT:
		bfloat.b[0] = p[3]; bfloat.b[1] = p[2]; bfloat.b[2] = p[1]; bfloat.b[3] = p[0];
		return bfloat.f;

	case FIELD_DOUBLE:
		bdouble.b[0] = p[7]; bdouble.b[1] = p[6]; bdouble.b[2] = p[5]; bdouble.b[3] = p[4];
		bdouble.b[4] = p[3]; bdouble.b[5] = p[2]; bdouble.b[6] = p[1]; bdouble.b[7] = p[0];
		return bdouble.d;

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
