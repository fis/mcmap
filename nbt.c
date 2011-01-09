#include <string.h>

#include <glib.h>
#include <zlib.h>

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

struct nbt_tag *nbt_new_int(char *name, enum nbt_tag_type type, int intv)
{
	if (type != NBT_TAG_BYTE && type != NBT_TAG_SHORT && type != NBT_TAG_INT)
		dief("nbt_new_int: bad type: %d", type);

	struct nbt_tag *tag = nbt_new(name, type);
	tag->data.intv = intv;
	return tag;
}

struct nbt_tag *nbt_new_long(char *name, long long longv)
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

static int nbt_gzwrite(gzFile f, struct nbt_tag *tag, int only_payload)
{
	if (!only_payload)
	{
		if (gzputc(f, tag->type) < 0)
			return 0;

		if (gzwrite(f, tag->namelen, 2) != 2)
			return 0;
		if (gzputs(f, tag->name) < 0)
			return 0;
	}

	unsigned char buf[8];
	unsigned char *p;

	switch (tag->type)
	{
	case NBT_TAG_END:
		/* no payload */
		break;

	case NBT_TAG_BYTE:
		if (gzputc(f, tag->data.intv) < 0)
			return 0;
		break;

	case NBT_TAG_SHORT:
		buf[0] = tag->data.intv >> 8;
		buf[1] = tag->data.intv;
		if (gzwrite(f, buf, 2) != 2)
			return 0;
		break;

	case NBT_TAG_INT:
		buf[0] = tag->data.intv >> 24;
		buf[1] = tag->data.intv >> 16;
		buf[2] = tag->data.intv >> 8;
		buf[3] = tag->data.intv;
		if (gzwrite(f, buf, 4) != 4)
			return 0;
		break;

	case NBT_TAG_LONG:
		buf[0] = tag->data.longv >> 56;
		buf[1] = tag->data.longv >> 48;
		buf[2] = tag->data.longv >> 40;
		buf[3] = tag->data.longv >> 32;
		buf[4] = tag->data.longv >> 24;
		buf[5] = tag->data.longv >> 16;
		buf[6] = tag->data.longv >> 8;
		buf[7] = tag->data.longv;
		if (gzwrite(f, buf, 8) != 8)
			return 0;
		break;

	case NBT_TAG_FLOAT:
		die("nbt_gzwrite: NBT_TAG_FLOAT unimplemented");

	case NBT_TAG_DOUBLE:
		p = (unsigned char *)&tag->data.doublev;
		buf[7] = p[0]; buf[6] = p[1]; buf[5] = p[2]; buf[4] = p[3];
		buf[3] = p[4]; buf[2] = p[5]; buf[1] = p[6]; buf[0] = p[7];
		if (gzwrite(f, buf, 8) != 8)
			return 0;
		break;

	case NBT_TAG_BLOB:
		buf[0] = tag->data.blobv.len >> 24;
		buf[1] = tag->data.blobv.len >> 16;
		buf[2] = tag->data.blobv.len >> 8;
		buf[3] = tag->data.blobv.len;
		if (gzwrite(f, buf, 4) != 4)
			return 0;
		if (gzwrite(f, tag->data.blobv.data, tag->data.blobv.len) != tag->data.blobv.len)
			return 0;
		break;

	case NBT_TAG_STR:
		buf[0] = tag->data.blobv.len >> 8;
		buf[1] = tag->data.blobv.len;
		if (gzwrite(f, buf, 2) != 2)
			return 0;
		if (gzwrite(f, tag->data.blobv.data, tag->data.blobv.len) != tag->data.blobv.len)
			return 0;
		break;

	case NBT_TAG_ARRAY:
		die("nbt_gzwrite: NBT_TAG_ARRAY unimplemented");

	case NBT_TAG_STRUCT:
		for (guint i = 0; i < tag->data.structv->len; i++)
			if (!nbt_gzwrite(f, g_ptr_array_index(tag->data.structv, i), 0))
				return 0;
		if (gzwrite(f, "\0", 2) != 2)
			return 0;
		break;
	}

	return 1;
}

int nbt_save(char *file, struct nbt_tag *tag)
{
	gzFile f = gzopen(file, "wb9");
	if (!f)
		return 0;

	if (gzwrite(f, "\x0a\x00\x00", 3) != 3)
	{
		gzclose(f);
		return 0;
	}

	int ret = nbt_gzwrite(f, tag, 0);
	gzclose(f);
	return ret;
}

struct nbt_tag *nbt_load(char *file)
{
	die("nbt_load: unimplemented");
}
