#ifndef MCMAP_NBT_H
#define MCMAP_NBT_H 1

#include "types.h"

enum nbt_tag_type
{
	NBT_TAG_END = 0,
	NBT_TAG_BYTE = 1,
	NBT_TAG_SHORT = 2,
	NBT_TAG_INT = 3,
	NBT_TAG_LONG = 4,
	NBT_TAG_FLOAT = 5,
	NBT_TAG_DOUBLE = 6,
	NBT_TAG_BLOB = 7,
	NBT_TAG_STR = 8,
	NBT_TAG_ARRAY = 9,
	NBT_TAG_STRUCT = 10
};

struct nbt_tag;

struct nbt_tag *nbt_new_int(char *name, enum nbt_tag_type type, jint intv);
struct nbt_tag *nbt_new_long(char *name, jlong longv);
struct nbt_tag *nbt_new_double(char *name, enum nbt_tag_type type, double doublev);

struct nbt_tag *nbt_new_blob(char *name, enum nbt_tag_type type, const void *data, int len);
struct nbt_tag *nbt_new_str(char *name, const char *str);

struct nbt_tag *nbt_new_struct(char *name);

void nbt_free(gpointer tag);

jint nbt_blob_len(struct nbt_tag *s);
unsigned char *nbt_blob_data(struct nbt_tag *s);

struct nbt_tag *nbt_struct_field(struct nbt_tag *s, const char *name);
void nbt_struct_add(struct nbt_tag *s, struct nbt_tag *field);

unsigned char *nbt_compress(struct nbt_tag *tag, unsigned *len);
struct nbt_tag *nbt_uncompress(struct buffer buf);

#endif /* MCMAP_NBT_H */
