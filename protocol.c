#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "protocol.h"
#include "common.h"

/*
 * reference:
 * https://gist.github.com/727175
 * http://mc.kev009.com/wiki/Protocol
 */

struct packet_format_desc
{
	unsigned nfields;
	enum field_type *ftype;
	unsigned char known;
};

#include "protocol-data.c"
#define MAX_PACKET_FORMAT ((sizeof packet_format)/(sizeof *packet_format))

/* packet reading/writing */

packet_t *packet_read(socket_t sock, packet_state_t *state)
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

		int got = recv(sock, (char*)(buf+buf_end), MAX_PACKET_SIZE - buf_end, 0);
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
		dief("Unknown packet id: 0x%02x (flags %02x)", t, state->p.flags);
	}

	state->p.field_offset = state->offset;

	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		state->p.field_offset[f] = buf_pos - buf_start;

		switch (fmt->ftype[f])
		{
		case FIELD_BYTE:
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

		case FIELD_ITEM:
			t = buf_get_i16();
			if (t != -1)
				if (!buf_skip(3)) return 0;
			break;

		case FIELD_BYTE_ARRAY:
			t = buf_get_i32();
			if (!buf_skip(t)) return 0;
			break;

		case FIELD_BLOCK_ARRAY:
			t = buf_get_i16();
			if (!buf_skip(4*t)) return 0;
			break;

		case FIELD_ITEM_ARRAY:
			t = buf_get_i16();
			for (int i = 0; i < t; i++)
			{
				short itype = buf_get_i16();
				if (itype != -1)
					if (!buf_skip(3)) return 0;
			}
			break;

		case FIELD_EXPLOSION_ARRAY:
			t = buf_get_i32();
			if (!buf_skip(3*t)) return 0;
			break;

		case FIELD_ENTITY_DATA:
			while (1)
			{
				t = buf_getc();
				if (t == 127)
					break;
				switch (t >> 5)
				{
				case 0: if (!buf_skip(1)) return 0; break;
				case 1: if (!buf_skip(2)) return 0; break;
				case 2: case 3: if (!buf_skip(4)) return 0; break;
				case 4: t = buf_get_i16(); if (!buf_skip(t)) return 0; break;
				case 5: if (!buf_skip(5)) return 0; break;
				}
			}
			break;
		}
	}

	state->p.field_offset[fmt->nfields] = buf_pos - buf_start;

	state->p.size = buf_pos - buf_start;
	state->p.bytes = &buf[buf_start];

	state->buf_start = buf_pos;
	state->buf_pos = buf_pos;
	state->buf_end = buf_end;

	return &state->p;
}

int packet_write(socket_t sock, packet_t *packet)
{
	gsize left = packet->size;
	gchar *p = (gchar*)packet->bytes;

	while (left)
	{
		int sent = send(sock, p, left, 0);
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

	newp->flags = packet->flags;
	newp->id = packet->id;
	newp->size = packet->size;
	newp->bytes = g_memdup(packet->bytes, packet->size);
	newp->field_offset = g_memdup(packet->field_offset, packet_format[packet->id].nfields * sizeof *newp->field_offset);

	return newp;
}

packet_t *packet_new(unsigned flags, enum packet_id type, ...)
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

	g_array_append_val(offsets, offset);

	va_end(ap);

	/* construct the actual packet */

	packet_t *p = g_malloc(sizeof *p);

	p->flags = flags;
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

int packet_nfields(packet_t *packet)
{
	return packet_format[packet->id].nfields;
}

int packet_int(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];
	int t = 0;

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_BYTE:
		return *(signed char *)p;

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

long long packet_long(packet_t *packet, unsigned field)
{
	if (packet_format[packet->id].ftype[field] == FIELD_LONG)
	{
		unsigned char *p = &packet->bytes[packet->field_offset[field]];
		return
			((long long)p[0] << 56) | ((long long)p[1] << 48) |
			((long long)p[2] << 40) | ((long long)p[3] << 32) |
			((long long)p[4] << 24) | ((long long)p[5] << 16) |
			((long long)p[6] << 8) | p[7];
	}
	else
		return packet_int(packet, field);
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
