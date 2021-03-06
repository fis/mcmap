#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <glib.h>
#include <zlib.h>

#include "config.h"
#include "types.h"
#include "console.h"
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
		jdouble doublev;
		struct buffer blobv;
		GPtrArray *structv;
	} data;
};

/* in-memory NBT structure handling */

static struct nbt_tag *nbt_new(char *name, enum nbt_tag_type type)
{
	struct nbt_tag *tag = g_new(struct nbt_tag, 1);

	tag->type = type;
	tag->name = name;

	size_t namelen = strlen(name);
	tag->namelen[0] = namelen >> 8;
	tag->namelen[1] = namelen;

	return tag;
}

struct nbt_tag *nbt_new_int(char *name, enum nbt_tag_type type, jint intv)
{
	if (type != NBT_TAG_BYTE && type != NBT_TAG_SHORT && type != NBT_TAG_INT)
		dief("nbt_new_int: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(g_strdup(name), type);
	tag->data.intv = intv;
	return tag;
}

struct nbt_tag *nbt_new_long(char *name, jlong longv)
{
	struct nbt_tag *tag = nbt_new(g_strdup(name), NBT_TAG_LONG);
	tag->data.longv = longv;
	return tag;
}

struct nbt_tag *nbt_new_double(char *name, enum nbt_tag_type type, double doublev)
{
	if (type != NBT_TAG_FLOAT && type != NBT_TAG_DOUBLE)
		dief("nbt_new_double: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(g_strdup(name), type);
	tag->data.doublev = doublev;
	return tag;
}

struct nbt_tag *nbt_new_blob(char *name, enum nbt_tag_type type, const void *data, size_t len)
{
	if (type != NBT_TAG_BLOB && type != NBT_TAG_STR)
		dief("nbt_new_blob: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(g_strdup(name), type);
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
	struct nbt_tag *tag = nbt_new(g_strdup(name), NBT_TAG_STRUCT);
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

struct buffer nbt_blob(struct nbt_tag *s)
{
	if (s->type != NBT_TAG_BLOB && s->type != NBT_TAG_STR)
		dief("nbt_blob: not a blob or a string: %d", s->type);

	return s->data.blobv;
}

struct nbt_tag *nbt_struct_field(struct nbt_tag *s, const char *name)
{
	if (s->type != NBT_TAG_STRUCT)
		dief("nbt_struct_field: not a structure: %d", s->type);

	size_t namelen = strlen(name);

	for (unsigned i = 0; i < s->data.structv->len; i++)
	{
		struct nbt_tag *field = g_ptr_array_index(s->data.structv, i);
		size_t field_namelen = (field->namelen[0] << 8) | field->namelen[1];

		if (field_namelen != namelen)
			continue;
		if (memcmp(field->name, name, namelen) != 0)
			continue;

		return field;
	}

	return 0;
}

void nbt_struct_add(struct nbt_tag *s, struct nbt_tag *field)
{
	if (s->type != NBT_TAG_STRUCT)
		dief("nbt_struct_add: not a structure: %d", s->type);

	g_ptr_array_add(s->data.structv, field);
}

/* NBT serialization code */

static void format_tag(GByteArray *arr, struct nbt_tag *tag, bool only_payload)
{
	if (!only_payload)
	{
		unsigned at = arr->len;
		size_t nlen = strlen(tag->name);

		g_byte_array_set_size(arr, at + 3 + nlen);

		arr->data[at] = tag->type;
		arr->data[at+1] = tag->namelen[0];
		arr->data[at+2] = tag->namelen[1];
		memcpy(&arr->data[at+3], tag->name, nlen);
	}

	unsigned at = arr->len;

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
		jshort_write(arr->data + at, tag->data.intv);
		break;

	case NBT_TAG_INT:
		g_byte_array_set_size(arr, at + 4);
		jint_write(arr->data + at, tag->data.intv);
		break;

	case NBT_TAG_LONG:
		g_byte_array_set_size(arr, at + 8);
		jlong_write(arr->data + at, tag->data.longv);
		break;

	case NBT_TAG_FLOAT:
		g_byte_array_set_size(arr, at + 4);
		jfloat_write(arr->data + at, tag->data.doublev);
		break;

	case NBT_TAG_DOUBLE:
		g_byte_array_set_size(arr, at + 8);
		jdouble_write(arr->data + at, tag->data.doublev);
		break;

	case NBT_TAG_BLOB:
		g_byte_array_set_size(arr, at + 4 + tag->data.blobv.len);
		jint_write(arr->data + at, tag->data.blobv.len);
		memcpy(&arr->data[at+4], tag->data.blobv.data, tag->data.blobv.len);
		break;

	case NBT_TAG_STR:
		g_byte_array_set_size(arr, at + 2 + tag->data.blobv.len);
		jshort_write(arr->data + at, tag->data.blobv.len);
		memcpy(&arr->data[at+2], tag->data.blobv.data, tag->data.blobv.len);
		break;

	case NBT_TAG_ARRAY:
		die("nbt format_tag: NBT_TAG_ARRAY unimplemented");

	case NBT_TAG_STRUCT:
		for (unsigned i = 0; i < tag->data.structv->len; i++)
			format_tag(arr, g_ptr_array_index(tag->data.structv, i), 0);
		g_byte_array_append(arr, (uint8_t *) "", 1);
		break;
	}
}

struct buffer nbt_compress(struct nbt_tag *tag)
{
	GByteArray *arr = g_byte_array_new();

	g_byte_array_append(arr, (uint8_t *) "\x0a\x00", 3);
	format_tag(arr, tag, 0);

	uLongf clen = compressBound(arr->len);
	Bytef *cbuf = g_malloc(clen);
	int ret = compress(cbuf, &clen, arr->data, arr->len);

	g_byte_array_unref(arr);

	if (ret != Z_OK)
		dief("zlib broke badly: %s", zError(ret));

	return (struct buffer){ clen, cbuf };
}

static struct nbt_tag *parse_tag(uint8_t *data, size_t len, size_t *taglen)
{
	if (len < 1)
		die("truncated NBT tag: short type");

	uint8_t type = data[0];

	if (type == NBT_TAG_END)
	{
		*taglen = 1;
		return 0;
	}

	if (len < 3)
		die("truncated NBT tag: short namelen");

	size_t namelen = jshort_read(&data[1]);

	if (len < 3+namelen)
		die("truncated NBT tag: short name");

	struct nbt_tag *tag = nbt_new(g_strndup((char*)&data[3], namelen), type);

	data += 3 + namelen;
	len -= 3 + namelen;
	*taglen = 3 + namelen;

	jint t;
	jbyte tb;

	struct nbt_tag *sub;
	size_t sublen;

	switch (tag->type)
	{
	case NBT_TAG_BYTE:
		if (len < 1) die("truncated NBT tag: short byte");
		tag->data.intv = (jbyte)*data;
		*taglen += 1;
		break;

	case NBT_TAG_SHORT:
		if (len < 2) die("truncated NBT tag: short short");
		tag->data.intv = jshort_read(data);
		*taglen += 2;
		break;

	case NBT_TAG_INT:
		if (len < 4) die("truncated NBT tag: short int");
		tag->data.intv = jint_read(data);
		*taglen += 4;
		break;

	case NBT_TAG_LONG:
		if (len < 8) die("truncated NBT tag: short long");
		tag->data.longv = jlong_read(data);
		*taglen += 8;
		break;

	case NBT_TAG_FLOAT:
		if (len < 4) die("truncated NBT tag: short float");
		tag->data.doublev = jfloat_read(data);
		*taglen += 4;
		break;

	case NBT_TAG_DOUBLE:
		if (len < 8) die("truncated NBT tag: short double");
		tag->data.doublev = jdouble_read(data);
		*taglen += 8;
		break;

	case NBT_TAG_BLOB:
		if (len < 4) die("truncated NBT tag: short blob len");
		t = jint_read(data);
		if (len < 4 + (size_t)t) die("truncated NBT tag: short blob data");
		tag->data.blobv.len = t;
		tag->data.blobv.data = g_memdup(&data[4], t);
		*taglen += 4 + t;
		break;

	case NBT_TAG_STR:
		if (len < 2) die("truncated NBT tag: short str len");
		t = jshort_read(data);
		if (len < 2 + (size_t)t) die("truncated NBT tag: short str data");
		tag->data.blobv.len = t;
		tag->data.blobv.data = g_memdup(&data[2], t);
		*taglen += 2 + t;
		break;

	case NBT_TAG_ARRAY:
		tag->data.structv = g_ptr_array_new_with_free_func(nbt_free);
		tb = data[0]; /* type tag byte for the elements */
		t = jint_read(data + 1);
		data += 5; len -= 5; *taglen += 5;
		for (jint i = 0; i < t; i++)
		{
			char old[3] = { data[-3], data[-2], data[-1] }; /* TODO this is horrible, HORRIBLE */
			data[-3] = tb; /* fake a NBT tag byte for the recursive call */
			data[-2] = 0; data[-1] = 0; /* fake an empty name too */
			if ((sub = parse_tag(data-3, len+3, &sublen)) == 0)
				die ("bad NBT tag: failed parsing an array element");
			g_ptr_array_add(tag->data.structv, sub);
			memcpy(&data[-3], old, 3); /* restore the clobbered bytes */
			data += sublen - 3;
			len -= sublen - 3;
			*taglen += sublen - 3;
		}
		break;

	case NBT_TAG_STRUCT:
		tag->data.structv = g_ptr_array_new_with_free_func(nbt_free);
		while ((sub = parse_tag(data, len, &sublen)) != 0)
		{
			nbt_struct_add(tag, sub);
			data += sublen;
			len -= sublen;
			*taglen += sublen;
		}
		*taglen += 1;
		break;

	default:
		dief("nbt parse_tag: unknown tag type: %d", tag->type);
	}

	return tag;
}

struct nbt_tag *nbt_uncompress(struct buffer buf)
{
	GByteArray *arr = g_byte_array_new();

	z_stream zs;
	zs.next_in = buf.data;
	zs.avail_in = buf.len;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	int ret = inflateInit(&zs);
	if (ret != Z_OK)
		dief("zlib broke: inflateInit: %s", zError(ret));

	unsigned char zbuf[4096];

	while (1)
	{
		zs.next_out = zbuf;
		zs.avail_out = sizeof zbuf;
		int ret = inflate(&zs, Z_NO_FLUSH);

		if (ret != Z_OK && ret != Z_STREAM_END)
			dief("zlib broke: inflate: %s", zError(ret));

		if (zs.next_out != zbuf)
			g_byte_array_append(arr, zbuf, zs.next_out - zbuf);

		if (ret == Z_STREAM_END)
			break;
	}

	inflateEnd(&zs);

	if (arr->len < 3 || memcmp(arr->data, "\x0a\x00", 3) != 0)
		die("nbt_uncompress: invalid header in uncompressed NBT");

	size_t t;
	struct nbt_tag *tag = parse_tag(arr->data + 3, arr->len - 3, &t);

	g_byte_array_unref(arr);
	return tag;
}
