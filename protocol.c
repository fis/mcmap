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

	jshort buf_get_jshort(void)
	{
		if (!buf_skip(2))
			die("out of data while getting jshort");
		return jshort_read(&buf[buf_pos-2]);
	}

	jint buf_get_jint(void)
	{
		if (!buf_skip(4))
			die("out of data while getting jint");
		return jint_read(&buf[buf_pos-4]);
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
			t = buf_get_jshort();
			if (!buf_skip(t*2)) return 0;
			break;

		case FIELD_STRING_UTF8:
			t = buf_get_jshort();
			if (!buf_skip(t)) return 0;
			break;

		case FIELD_ITEM:
			t = buf_get_jshort();
			if (t != -1)
				if (!buf_skip(3)) return 0;
			break;

		case FIELD_BYTE_ARRAY:
			t = buf_get_jint();
			if (!buf_skip(t)) return 0;
			break;

		case FIELD_BLOCK_ARRAY:
			t = buf_get_jshort();
			if (!buf_skip(4*t)) return 0;
			break;

		case FIELD_ITEM_ARRAY:
			t = buf_get_jshort();
			for (int i = 0; i < t; i++)
			{
				jshort itype = buf_get_jshort();
				if (itype != -1)
					if (!buf_skip(3)) return 0;
			}
			break;

		case FIELD_EXPLOSION_ARRAY:
			t = buf_get_jint();
			if (!buf_skip(3*t)) return 0;
			break;

		case FIELD_MAP_ARRAY:
			t = buf_getc(); // Note: Unsigned
			if (!buf_skip(t)) return 0;
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
				case 4: t = buf_get_jshort(); if (!buf_skip(t)) return 0; break;
				case 5: if (!buf_skip(5)) return 0; break;
				}
			}
			break;

		case FIELD_OBJECT_DATA:
			t = buf_get_jint();
			if (t > 0)
				if (!buf_skip(6)) return 0; // Skip 3 short
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
				jbyte v = t;
				g_byte_array_append(data, (unsigned char *)&v, 1);
				offset++;
			}
			break;

		case FIELD_SHORT:
			t = va_arg(ap, int);
			g_byte_array_set_size(data, offset + 2);
			jshort_write(data->data + offset, t);
			offset += 2;
			break;

		case FIELD_INT:
			t = va_arg(ap, int);
			g_byte_array_set_size(data, offset + 4);
			jint_write(data->data + offset, t);
			offset += 4;
			break;

		case FIELD_LONG:
			tl = va_arg(ap, long long);
			g_byte_array_set_size(data, offset + 8);
			jlong_write(data->data + offset, tl);
			offset += 8;
			break;

		case FIELD_FLOAT:
			td = va_arg(ap, double);
			g_byte_array_set_size(data, offset + 4);
			jfloat_write(data->data + offset, td);
			offset += 4;
			break;

		case FIELD_DOUBLE:
			td = va_arg(ap, double);
			g_byte_array_set_size(data, offset + 8);
			jdouble_write(data->data + offset, td);
			offset += 8;
			break;

		case FIELD_STRING:
			tp = va_arg(ap, unsigned char *);
			{
				GError *error = NULL;
				gsize conv_len;
				gchar *conv = g_convert((gchar*)tp, -1, "UTF16BE", "UTF8", NULL, &conv_len, &error);
				if (!conv)
					dief("g_convert UTF8->UTF16BE failed (error: %s, string: '%s')", error->message, (char*)tp);
				unsigned char lenb[2];
				jshort_write(lenb, conv_len);
				g_byte_array_append(data, lenb, 2);
				g_byte_array_append(data, (unsigned char*)conv, conv_len);
				offset += 2 + conv_len;
				g_free(conv);
			}
			break;

		case FIELD_STRING_UTF8:
			tp = va_arg(ap, unsigned char *);
			{
				int len = strlen((char *)tp);
				unsigned char lenb[2];
				jshort_write(lenb, len);
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

jint packet_int(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_BYTE:
		return *(jbyte *)p;

	case FIELD_SHORT:
		return jshort_read(p);

	case FIELD_INT:
		return jint_read(p);

	default:
		dief("can't interpret field type %d as int (packet 0x%02x, field %u)",
		     packet_format[packet->id].ftype[field], packet->id, field);
	}
}

jlong packet_long(packet_t *packet, unsigned field)
{
	if (packet_format[packet->id].ftype[field] == FIELD_LONG)
	{
		unsigned char *p = &packet->bytes[packet->field_offset[field]];
		return jlong_read(p);
	}
	else
		return packet_int(packet, field);
}

double packet_double(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_FLOAT:
		return jfloat_read(p);

	case FIELD_DOUBLE:
		return jdouble_read(p);

	default:
		return packet_int(packet, field);
	}
}

unsigned char *packet_string(packet_t *packet, unsigned field, int *len)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	unsigned char *str;
	int l;

	switch (packet_format[packet->id].ftype[field])
	{
	case FIELD_STRING:
		l = jshort_read(p);
		{
			GError *error = NULL;
			gsize conv_len;
			str = (unsigned char *)g_convert((gchar*)&p[2], l*2, "UTF8", "UTF16BE", NULL, &conv_len, &error);
			if (!str)
				dief("g_convert UTF16BE->UTF8 failed (error: %s)", error->message);
			l = conv_len;
		}
		break;

	case FIELD_STRING_UTF8:
		l = jshort_read(p);
		str = (unsigned char *)g_strndup((gchar*)&p[2], l);
		break;

	default:
		dief("can't interpret field type %d as string (packet 0x%02x, field %u)",
		     packet_format[packet->id].ftype[field], packet->id, field);
	}

	if (len)
		*len = l;
	return str;
}
