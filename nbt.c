#include <string.h>

#include <glib.h>
#include <zlib.h>

#include "protocol.h"
#include "common.h"
#include "nbt.h"

/* NBT tag data structure */

struct nbt_tag
{
	enum nbt_tag_type type;

	unsigned char namelen[2];
	char *name;

	union
	{
		int intv;
		long long longv;
		double doublev;
		struct {
			int len;
			unsigned char *data;
		} blobv;
		GPtrArray *structv;
	} data;
};

/* in-memory NBT structure handling */

static struct nbt_tag *nbt_new(char *name, enum nbt_tag_type type)
{
	struct nbt_tag *tag = g_new(struct nbt_tag, 1);

	tag->type = type;
	tag->name = g_strdup(name);

	int namelen = strlen(name);
	tag->namelen[0] = namelen >> 8;
	tag->namelen[1] = namelen;

	return tag;
}

struct nbt_tag *nbt_new_int(char *name, enum nbt_tag_type type, jint intv)
{
	if (type != NBT_TAG_BYTE && type != NBT_TAG_SHORT && type != NBT_TAG_INT)
		dief("nbt_new_int: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(name, type);
	tag->data.intv = intv;
	return tag;
}

struct nbt_tag *nbt_new_long(char *name, jlong longv)
{
	struct nbt_tag *tag = nbt_new(name, NBT_TAG_LONG);
	tag->data.longv = longv;
	return tag;
}

struct nbt_tag *nbt_new_double(char *name, enum nbt_tag_type type, double doublev)
{
	if (type != NBT_TAG_FLOAT && type != NBT_TAG_DOUBLE)
		dief("nbt_new_double: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(name, type);
	tag->data.doublev = doublev;
	return tag;
}

struct nbt_tag *nbt_new_blob(char *name, enum nbt_tag_type type, const void *data, int len)
{
	if (type != NBT_TAG_BLOB && type != NBT_TAG_STR)
		dief("nbt_new_blob: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(name, type);
	tag->data.blobv.len = len;
	tag->data.blobv.data = g_memdup(data, len);
	return tag;
}

struct nbt_tag *nbt_new_str(char *name, const char *str)
{
	return nbt_new_blob(name, NBT_TAG_STR, str, strlen(str));
}

struct nbt_tag *nbt_new_struct(char *name)
{
	struct nbt_tag *tag = nbt_new(name, NBT_TAG_STRUCT);
	tag->data.structv = g_ptr_array_new_with_free_func(nbt_free);
	return tag;
}

void nbt_free(gpointer tag)
{
	struct nbt_tag *t = tag;

	switch (t->type)
	{
	case NBT_TAG_BLOB:
	case NBT_TAG_STR:
		g_free(t->data.blobv.data);
		break;

	case NBT_TAG_STRUCT:
		g_ptr_array_unref(t->data.structv);
		break;

	default:
		/* no special actions */
		break;
	}

	g_free(tag);
}

void nbt_struct_add(struct nbt_tag *s, struct nbt_tag *field)
{
	if (s->type != NBT_TAG_STRUCT)
		dief("nbt_struct_add: not a structure: %d", s->type);

	g_ptr_array_add(s->data.structv, field);
}

/* NBT file IO code */

static void format_tag(GByteArray *arr, struct nbt_tag *tag, int only_payload)
{
	if (!only_payload)
	{
		guint at = arr->len;
		size_t nlen = strlen(tag->name);

		g_byte_array_set_size(arr, at + 3 + nlen);

		arr->data[at] = tag->type;
		arr->data[at+1] = tag->namelen[0];
		arr->data[at+2] = tag->namelen[1];
		memcpy(&arr->data[at+3], tag->name, nlen);
	}

	guint at = arr->len;
	unsigned char *p;

	switch (tag->type)
	{
	case NBT_TAG_END:
		/* no payload */
		break;

	case NBT_TAG_BYTE:
		g_byte_array_set_size(arr, at + 1);
		arr->data[at] = tag->data.intv;
		break;

	case NBT_TAG_SHORT:
		g_byte_array_set_size(arr, at + 2);
		arr->data[at]   = tag->data.intv >> 8;
		arr->data[at+1] = tag->data.intv;
		break;

	case NBT_TAG_INT:
		g_byte_array_set_size(arr, at + 4);
		arr->data[at]   = tag->data.intv >> 24;
		arr->data[at+1] = tag->data.intv >> 16;
		arr->data[at+2] = tag->data.intv >> 8;
		arr->data[at+3] = tag->data.intv;
		break;

	case NBT_TAG_LONG:
		g_byte_array_set_size(arr, at + 8);
		arr->data[at]   = tag->data.longv >> 56;
		arr->data[at+1] = tag->data.longv >> 48;
		arr->data[at+2] = tag->data.longv >> 40;
		arr->data[at+3] = tag->data.longv >> 32;
		arr->data[at+4] = tag->data.longv >> 24;
		arr->data[at+5] = tag->data.longv >> 16;
		arr->data[at+6] = tag->data.longv >> 8;
		arr->data[at+7] = tag->data.longv;
		break;

	case NBT_TAG_FLOAT:
		die("nbt format_tag: NBT_TAG_FLOAT unimplemented");

	case NBT_TAG_DOUBLE:
		g_byte_array_set_size(arr, at + 8);
		p = (unsigned char *)&tag->data.doublev;
		arr->data[at+7] = p[0]; arr->data[at+6] = p[1]; arr->data[at+5] = p[2]; arr->data[at+4] = p[3];
		arr->data[at+3] = p[4]; arr->data[at+2] = p[5]; arr->data[at+1] = p[6]; arr->data[at]   = p[7];
		break;

	case NBT_TAG_BLOB:
		g_byte_array_set_size(arr, at + 4 + tag->data.blobv.len);
		arr->data[at]   = tag->data.blobv.len >> 24;
		arr->data[at+1] = tag->data.blobv.len >> 16;
		arr->data[at+2] = tag->data.blobv.len >> 8;
		arr->data[at+3] = tag->data.blobv.len;
		memcpy(&arr->data[at+4], tag->data.blobv.data, tag->data.blobv.len);
		break;

	case NBT_TAG_STR:
		g_byte_array_set_size(arr, at + 2 + tag->data.blobv.len);
		arr->data[at]   = tag->data.blobv.len >> 8;
		arr->data[at+1] = tag->data.blobv.len;
		memcpy(&arr->data[at+2], tag->data.blobv.data, tag->data.blobv.len);
		break;

	case NBT_TAG_ARRAY:
		die("nbt format_tag: NBT_TAG_ARRAY unimplemented");

	case NBT_TAG_STRUCT:
		for (guint i = 0; i < tag->data.structv->len; i++)
			format_tag(arr, g_ptr_array_index(tag->data.structv, i), 0);
		g_byte_array_append(arr, (guint8*)"\0", 2);
		break;
	}
}

unsigned char *nbt_compress(struct nbt_tag *tag, unsigned *len)
{
	GByteArray *arr = g_byte_array_new();

	g_byte_array_append(arr, (guint8*)"\x0a\x00", 3);
	format_tag(arr, tag, 0);

	uLongf clen = compressBound(arr->len);
	Bytef *cbuf = g_malloc(clen);
	int ret = compress(cbuf, &clen, arr->data, arr->len);

	g_byte_array_unref(arr);

	if (ret != Z_OK)
		dief("zlib broke badly: %d", ret);

	*len = clen;
	return cbuf;
}

struct nbt_tag *nbt_uncompress(unsigned char *data, unsigned len)
{
	die("nbt_uncompress: unimplemented");
}
