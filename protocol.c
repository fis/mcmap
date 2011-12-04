#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include "protocol.h"
#include "common.h"
#include "console.h"

/*
 * reference:
 * https://gist.github.com/727175
 * http://mc.kev009.com/wiki/Protocol
 */

#define PACKET(id, cname, nfields, ...) \
	static enum field_type packet_format_##cname[nfields ? nfields : 1] = { __VA_ARGS__ };
#define FIELD(type, cname) type
#include "protocol.def"
#undef FIELD
#undef PACKET

struct packet_format_desc packet_format[] = {
#define PACKET(id, cname, nfields, ...) \
	[id] = { nfields, packet_format_##cname, 1 },
#include "protocol.def"
#undef PACKET
};

#define MAX_PACKET_FORMAT NELEMS(packet_format)

static const char *packet_names[] = {
#define PACKET(id, cname, nfields, ...) \
	[id] = #cname,
#include "protocol.def"
#undef PACKET
};

#define PACKET(id, cname, nfields, ...) \
	static const char *packet_field_names_##cname[nfields ? nfields : 1] = { __VA_ARGS__ };
#define FIELD(ftype, cname) \
	#cname
#include "protocol.def"
#undef FIELD
#undef PACKET

static const char **packet_field_names[] = {
#define PACKET(id, cname, nfields, ...) \
	[id] = packet_field_names_##cname,
#include "protocol.def"
#undef PACKET
};

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
	if (n < 0)
		dief("%d passed to buf_skip! Broken server or desync", n);
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

static bool buf_skip_item(packet_state_t *state)
{
	jshort item = buf_get_jshort(state);
	if (item == -1) return true;
	if (!buf_skip(state, 3)) return false;

	/* TODO FIXME: Scrape this list from the wiki, too */
	jshort len;
	switch (item)
	{
	case 0x103: case 0x105: case 0x15A: case 0x167: case 0x10C:
	case 0x10D: case 0x10E: case 0x10F: case 0x122: case 0x110:
	case 0x111: case 0x112: case 0x113: case 0x123: case 0x10B:
	case 0x100: case 0x101: case 0x102: case 0x124: case 0x114:
	case 0x115: case 0x116: case 0x117: case 0x125: case 0x11B:
	case 0x11C: case 0x11D: case 0x11E: case 0x126: case 0x12A:
	case 0x12B: case 0x12C: case 0x12D: case 0x12E: case 0x12F:
	case 0x130: case 0x131: case 0x132: case 0x133: case 0x134:
	case 0x135: case 0x136: case 0x137: case 0x138: case 0x139:
	case 0x13A: case 0x13B: case 0x13C: case 0x13D:
		len = buf_get_jshort(state);
		if (!buf_skip(state, len)) return false;
		break;
	}

	return true;
}

packet_t *packet_read(packet_state_t *state)
{
	jint t = buf_getc(state);
	if (t < 0)
		return 0;

	unsigned type = t;

	state->p.type = type;

	struct packet_format_desc *fmt;
	if (type >= MAX_PACKET_FORMAT || !(fmt = &packet_format[t])->known)
	{
#if DEBUG_PROTOCOL >= 1
		log_print("IMMINENT CRASH, reading tail for log");
		unsigned char buf[256];
		for (int i = 0; i < sizeof buf; i++) buf[i] = buf_getc(state);
		for (int i = 0; i < sizeof buf; i+=16)
			log_print("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			          buf[i+0],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7],
			          buf[i+8],buf[i+9],buf[i+10],buf[i+11],buf[i+12],buf[i+13],buf[i+14],buf[i+15]);
#endif
		dief("Unknown packet id: 0x%02x", type);
	}

	state->p.field_offset = state->offset;

	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		state->p.field_offset[f] = state->buf_pos - state->buf_start;

		switch (fmt->ftype[f])
		{
		case FIELD_BYTE:
		case FIELD_UBYTE:
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

		case FIELD_ITEM:
			if (!buf_skip_item(state)) return 0;
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
				if (!buf_skip_item(state)) return 0;
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

/* FIXME: duplication with log_vput in console.c */
int packet_write(socket_t sock, packet_t *packet)
{
	struct buffer buf = { packet->size, (unsigned char *) packet->bytes };

	while (buf.len > 0)
	{
		ssize_t sent = send(sock, buf.data, buf.len, 0);
		if (sent < 0 && errno == EINTR)
			continue;
		else if (sent < 0)
			return 0;
		ADVANCE_BUFFER(buf, sent);
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

void packet_add_jubyte(packet_constructor_t *pc, jubyte v)
{
	packet_add_field(pc);
	g_byte_array_append(pc->data, &v, 1);
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
	size_t conv_len;
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

		case FIELD_UBYTE:
			packet_add_jubyte(&pc, va_arg(ap, int));
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

	case FIELD_UBYTE:
		return *(jubyte *)p;

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
			size_t conv_len;
			buffer.data = (unsigned char *)g_convert((char*)&p[2], l*2, "UTF8", "UTF16BE", NULL, &conv_len, &error);
			if (!buffer.data)
				dief("g_convert UTF16BE->UTF8 failed (error: %s; packet 0x%02x, field %u)",
				     error->message, packet->type, field);
			buffer.len = conv_len;
		}
		break;

	default:
		dief("can't interpret field type %d as string (packet 0x%02x, field %u)",
		     packet_format[packet->type].ftype[field], packet->type, field);
	}

	return buffer;
}

void packet_dump(packet_t *packet)
{
	unsigned t = packet->type;

	static const char *field_type_names[] =
	{
		[FIELD_BYTE] = "byte",
		[FIELD_UBYTE] = "ubyte",
		[FIELD_SHORT] = "short",
		[FIELD_INT] = "int",
		[FIELD_LONG] = "long",
		[FIELD_FLOAT] = "float",
		[FIELD_DOUBLE] = "double",
		[FIELD_STRING] = "string",
		[FIELD_ITEM] = "item",
		[FIELD_BYTE_ARRAY] = "byte-array",
		[FIELD_BLOCK_ARRAY] = "block-array",
		[FIELD_ITEM_ARRAY] = "item-array",
		[FIELD_EXPLOSION_ARRAY] = "explosion-array",
		[FIELD_MAP_ARRAY] = "map-array",
		[FIELD_ENTITY_DATA] = "entity-data",
		[FIELD_OBJECT_DATA] = "object-data",
	};

	struct packet_format_desc *fmt;
	if (t >= MAX_PACKET_FORMAT || !(fmt = &packet_format[t])->known)
	{
		log_print("[DUMP] Packet %u (0x%02x, unknown)", t, t);
		return;
	}
	else
		log_print("[DUMP] Packet %u (0x%02x, %s), %u field(s):", t, t, packet_names[t], fmt->nfields);

	unsigned pad = 0;
	char padding[256];
	memset(padding, ' ', 255);
	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		unsigned field_pad =
			strlen(field_type_names[fmt->ftype[f]]) +
			strlen(packet_field_names[t][f]) +
			1; /* the space between them */
		if (field_pad > pad)
			pad = field_pad;
	}
	padding[pad] = '\0';

	for (unsigned f = 0; f < fmt->nfields; f++)
	{
		const char *ftype = field_type_names[fmt->ftype[f]];
		const char *fname = packet_field_names[t][f];

		jint ti;
		jlong tl;
		double td;
		struct buffer tb;
		char hexdump[64*3+1];

		#define DUMP(fmt, ...) \
			log_print("[DUMP]   %d. %s %s:%s" fmt, \
			          f, ftype, fname, padding + strlen(ftype) + strlen(fname) + 1, __VA_ARGS__)

		switch (fmt->ftype[f])
		{
		case FIELD_BYTE:
		case FIELD_UBYTE:
		case FIELD_SHORT:
		case FIELD_INT:
			ti = packet_int(packet, f);
			DUMP(" %"PRId32" (%08"PRIx32")", ti, (uint32_t)ti);
			break;

		case FIELD_LONG:
			tl = packet_long(packet, f);
			DUMP(" %"PRId64" (%016"PRIx64")", tl, (uint64_t)tl);
			break;

		case FIELD_FLOAT:
		case FIELD_DOUBLE:
			td = packet_double(packet, f);
			DUMP(" %.6f (%.2g)", td, td);
			break;

		case FIELD_STRING:
			tb = packet_string(packet, f);
			DUMP(" '%.*s'", tb.len, tb.data);
			g_free(tb.data);
			break;

		case FIELD_ITEM:
		case FIELD_BYTE_ARRAY:
		case FIELD_BLOCK_ARRAY:
		case FIELD_ITEM_ARRAY:
		case FIELD_EXPLOSION_ARRAY:
		case FIELD_MAP_ARRAY:
		case FIELD_ENTITY_DATA:
		case FIELD_OBJECT_DATA:
			for (unsigned start = packet->field_offset[f], end = packet->field_offset[f+1], at = 0;
			     at < 64 && start < end; at++, start++)
				sprintf(hexdump + at*3, " %02x", (unsigned)packet->bytes[start]);
			DUMP("%s", hexdump);
		}

		#undef DUMP
	}
}
