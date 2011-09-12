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

#define PACKET(id, cname, scmname, nfields, ...) \
	static enum field_type packet_format_##cname[nfields ? nfields : 1] = { __VA_ARGS__ };
#define FIELD(type, cname, scmname) type
#include "protocol.def"
#undef FIELD
#undef PACKET

struct packet_format_desc packet_format[] = {
#define PACKET(id, cname, scmname, nfields, ...) \
	[id] = { nfields, packet_format_##cname, 1 },
#include "protocol.def"
#undef PACKET
};

#define MAX_PACKET_FORMAT NELEMS(packet_format)

/* packet reading/writing */

static int buf_fill(packet_state_t *state)
{
	if (state->buf_start > 0 && state->buf_end == MAX_PACKET_SIZE)
	{
		memmove(state->buf, state->buf + state->buf_start, state->buf_end - state->buf_start);
		state->buf_pos -= state->buf_start;
		state->buf_end -= state->buf_start;
		state->buf_start = 0;
	}

	int got = recv(state->sock, (char *)(state->buf + state->buf_end), MAX_PACKET_SIZE - state->buf_end, 0);
	if (got <= 0)
	{
		state->buf_pos = state->buf_start = state->buf_end = 0;
		return 0;
	}

	state->buf_end += got;
	return 1;
}

static int buf_skip(packet_state_t *state, unsigned n)
{
	while (state->buf_pos + n > state->buf_end)
		if (!buf_fill(state))
			return 0;
	state->buf_pos += n;
	return 1;
}

static int buf_getc(packet_state_t *state)
{
	if (state->buf_pos == state->buf_end)
		if (!buf_fill(state))
			return -1;
	return state->buf[state->buf_pos++];
}

static jshort buf_get_jshort(packet_state_t *state)
{
	if (!buf_skip(state, 2))
			die("out of data while getting jshort");
	return jshort_read(&state->buf[state->buf_pos-2]);
}

static jint buf_get_jint(packet_state_t *state)
{
	if (!buf_skip(state, 4))
		die("out of data while getting jint");
	return jint_read(&state->buf[state->buf_pos-4]);
}

packet_t *packet_read(packet_state_t *state)
{
	int t = buf_getc(state);
	if (t < 0)
		return 0;

	state->p.type = t;

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
		dief("Unknown packet id: 0x%02x", t);
	}

	state->p.field_offset = state->offset;

	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		state->p.field_offset[f] = state->buf_pos - state->buf_start;

		switch (fmt->ftype[f])
		{
		case FIELD_BYTE:
			if (!buf_skip(state, 1)) return 0;
			break;

		case FIELD_SHORT:
			if (!buf_skip(state, 2)) return 0;
			break;

		case FIELD_INT:
		case FIELD_FLOAT:
			if (!buf_skip(state, 4)) return 0;
			break;

		case FIELD_LONG:
		case FIELD_DOUBLE:
			if (!buf_skip(state, 8)) return 0;
			break;

		case FIELD_STRING:
			t = buf_get_jshort(state);
			if (!buf_skip(state, t*2)) return 0;
			break;

		case FIELD_STRING_UTF8:
			t = buf_get_jshort(state);
			if (!buf_skip(state, t)) return 0;
			break;

		case FIELD_ITEM:
			t = buf_get_jshort(state);
			if (t != -1)
				if (!buf_skip(state, 3)) return 0;
			break;

		case FIELD_BYTE_ARRAY:
			t = buf_get_jint(state);
			if (!buf_skip(state, t)) return 0;
			break;

		case FIELD_BLOCK_ARRAY:
			t = buf_get_jshort(state);
			if (!buf_skip(state, 4*t)) return 0;
			break;

		case FIELD_ITEM_ARRAY:
			t = buf_get_jshort(state);
			for (int i = 0; i < t; i++)
			{
				jshort itype = buf_get_jshort(state);
				if (itype != -1)
					if (!buf_skip(state, 3)) return 0;
			}
			break;

		case FIELD_EXPLOSION_ARRAY:
			t = buf_get_jint(state);
			if (!buf_skip(state, 3*t)) return 0;
			break;

		case FIELD_MAP_ARRAY:
			t = buf_getc(state); // Note: Unsigned
			if (!buf_skip(state, t)) return 0;
			break;

		case FIELD_ENTITY_DATA:
			while (1)
			{
				t = buf_getc(state);
				if (t == 127)
					break;
				switch (t >> 5)
				{
				case 0: if (!buf_skip(state, 1)) return 0; break;
				case 1: if (!buf_skip(state, 2)) return 0; break;
				case 2: case 3: if (!buf_skip(state, 4)) return 0; break;
				case 4: t = buf_get_jshort(state); if (!buf_skip(state, t)) return 0; break;
				case 5: if (!buf_skip(state, 5)) return 0; break;
				}
			}
			break;

		case FIELD_OBJECT_DATA:
			t = buf_get_jint(state);
			if (t > 0)
				if (!buf_skip(state, 6)) return 0; // Skip 3 short
			break;
		}
	}

	state->p.field_offset[fmt->nfields] = state->buf_pos - state->buf_start;

	state->p.size = state->buf_pos - state->buf_start;
	state->p.bytes = &state->buf[state->buf_start];

	state->buf_start = state->buf_pos;

	return &state->p;
}

int packet_write(socket_t sock, packet_t *packet)
{
	gsize left = packet->size;
	char *p = (char*)packet->bytes;

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

	newp->type = packet->type;
	newp->size = packet->size;
	newp->bytes = g_memdup(packet->bytes, packet->size);
	newp->field_offset = g_memdup(packet->field_offset, (packet_format[packet->type].nfields + 1) * sizeof *newp->field_offset);

	return newp;
}

packet_constructor_t packet_create(enum packet_id type)
{
	packet_constructor_t pc;
	pc.type = type;
	pc.data = g_byte_array_new();
	pc.offsets = g_array_new(false, false, sizeof(unsigned));
	pc.offset = 1;

	/* add the type byte */

	{
		uint8_t typebyte = type;
		g_byte_array_append(pc.data, &typebyte, 1);
	}

	return pc;
}

static void packet_add_field(packet_constructor_t *pc)
{
	g_array_append_val(pc->offsets, pc->offset);
}

void packet_add_jbyte(packet_constructor_t *pc, jbyte v)
{
	packet_add_field(pc);
	g_byte_array_append(pc->data, (uint8_t *) &v, 1);
	pc->offset++;
}

void packet_add_jshort(packet_constructor_t *pc, jshort v)
{
	packet_add_field(pc);
	g_byte_array_set_size(pc->data, pc->offset + 2);
	jshort_write(pc->data->data + pc->offset, v);
	pc->offset += 2;
}

void packet_add_jint(packet_constructor_t *pc, jint v)
{
	packet_add_field(pc);
	g_byte_array_set_size(pc->data, pc->offset + 4);
	jint_write(pc->data->data + pc->offset, v);
	pc->offset += 4;
}

void packet_add_jlong(packet_constructor_t *pc, jlong v)
{
	packet_add_field(pc);
	g_byte_array_set_size(pc->data, pc->offset + 8);
	jlong_write(pc->data->data + pc->offset, v);
	pc->offset += 8;
}

void packet_add_jfloat(packet_constructor_t *pc, jfloat v)
{
	packet_add_field(pc);
	g_byte_array_set_size(pc->data, pc->offset + 4);
	jfloat_write(pc->data->data + pc->offset, v);
	pc->offset += 4;
}

void packet_add_jdouble(packet_constructor_t *pc, jdouble v)
{
	packet_add_field(pc);
	g_byte_array_set_size(pc->data, pc->offset + 8);
	jdouble_write(pc->data->data + pc->offset, v);
	pc->offset += 8;
}

void packet_add_string(packet_constructor_t *pc, unsigned char *v)
{
	packet_add_field(pc);
	GError *error = NULL;
	gsize conv_len;
	char *conv = g_convert((char *) v, -1, "UTF16BE", "UTF8", NULL, &conv_len, &error);
	if (!conv)
		dief("g_convert UTF8->UTF16BE failed (error: %s, string: '%s')", error->message, (char *) v);
	unsigned char lenb[2];
	jshort_write(lenb, conv_len/2);
	g_byte_array_append(pc->data, lenb, 2);
	g_byte_array_append(pc->data, (unsigned char*)conv, conv_len);
	pc->offset += 2 + conv_len;
	g_free(conv);
}

void packet_add_string_utf8(packet_constructor_t *pc, unsigned char *v)
{
	packet_add_field(pc);
	int len = strlen((char *) v);
	unsigned char lenb[2];
	jshort_write(lenb, len);
	g_byte_array_append(pc->data, lenb, 2);
	g_byte_array_append(pc->data, v, len);
	pc->offset += 2 + len;
}

void packet_constructor_free(packet_constructor_t *pc)
{
	g_byte_array_free(pc->data, true);
	g_array_free(pc->offsets, true);
}

packet_t *packet_construct(packet_constructor_t *pc)
{
	g_array_append_val(pc->offsets, pc->offset);

	/* construct the actual packet */

	packet_t *p = g_malloc(sizeof *p);

	p->type = pc->type;
	p->size = pc->offset;
	p->bytes = g_byte_array_free(pc->data, false);
	p->field_offset = (unsigned *)g_array_free(pc->offsets, false);

	return p;
}

packet_t *packet_new(enum packet_id type, ...)
{
	packet_constructor_t pc = packet_create(type);

	/* build the fields */

	va_list ap;
	va_start(ap, type);

	unsigned nfields = packet_format[type].nfields;
	for (unsigned field = 0; field < nfields; field++)
	{
		switch (packet_format[type].ftype[field])
		{
		case FIELD_BYTE:
			packet_add_jbyte(&pc, va_arg(ap, int));
			break;

		case FIELD_SHORT:
			packet_add_jshort(&pc, va_arg(ap, int));
			break;

		case FIELD_INT:
			/* FIXME: jint could be bigger than int */
			packet_add_jint(&pc, va_arg(ap, int));
			break;

		case FIELD_LONG:		
			/* FIXME: ditto here, I think */
			packet_add_jlong(&pc, va_arg(ap, long long));
			break;

		case FIELD_FLOAT:
			/* FIXME: maybe here and in the double case, too? */
			packet_add_jfloat(&pc, va_arg(ap, double));
			break;

		case FIELD_DOUBLE:
			packet_add_jdouble(&pc, va_arg(ap, double));
			break;

		case FIELD_STRING:
			packet_add_string(&pc, va_arg(ap, unsigned char *));
			break;

		case FIELD_STRING_UTF8:
			packet_add_string_utf8(&pc, va_arg(ap, unsigned char *));
			break;

		default:
			dief("unhandled field type %d (packet 0x%02x, field %u)",
			     packet_format[type].ftype[field], type, field);
		}
	}

	va_end(ap);

	return packet_construct(&pc);
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
	return packet_format[packet->type].nfields;
}

jint packet_int(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	switch (packet_format[packet->type].ftype[field])
	{
	case FIELD_BYTE:
		return *(jbyte *)p;

	case FIELD_SHORT:
		return jshort_read(p);

	case FIELD_INT:
		return jint_read(p);

	default:
		dief("can't interpret field type %d as int (packet 0x%02x, field %u)",
		     packet_format[packet->type].ftype[field], packet->type, field);
	}
}

jlong packet_long(packet_t *packet, unsigned field)
{
	if (packet_format[packet->type].ftype[field] == FIELD_LONG)
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

	switch (packet_format[packet->type].ftype[field])
	{
	case FIELD_FLOAT:
		return jfloat_read(p);

	case FIELD_DOUBLE:
		return jdouble_read(p);

	default:
		return packet_int(packet, field);
	}
}

struct buffer packet_string(packet_t *packet, unsigned field)
{
	unsigned char *p = &packet->bytes[packet->field_offset[field]];

	struct buffer buffer;

	switch (packet_format[packet->type].ftype[field])
	{
	case FIELD_STRING:
		{
			int l = jshort_read(p);
			GError *error = NULL;
			gsize conv_len;
			buffer.data = (unsigned char *)g_convert((char*)&p[2], l*2, "UTF8", "UTF16BE", NULL, &conv_len, &error);
			if (!buffer.data)
				dief("g_convert UTF16BE->UTF8 failed (error: %s)", error->message);
			buffer.len = conv_len;
		}
		break;

	case FIELD_STRING_UTF8:
		buffer.len = jshort_read(p);
		buffer.data = (unsigned char *)g_strndup((char*)&p[2], buffer.len);
		break;

	default:
		dief("can't interpret field type %d as string (packet 0x%02x, field %u)",
		     packet_format[packet->type].ftype[field], packet->type, field);
	}

	return buffer;
}
