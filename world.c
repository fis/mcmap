#include <stdlib.h>

#include <glib.h>
#include <zlib.h>

#include "protocol.h"
#include "world.h"

#define CHUNK_XBITS 4
#define CHUNK_ZBITS 4

#define CHUNK_XSIZE (1 << CHUNK_XBITS)
#define CHUNK_ZSIZE (1 << CHUNK_ZBITS)
#define CHUNK_YSIZE 128 /* world height */

#define CHUNK_XIDX(coord) ((coord) >> CHUNK_XBITS)
#define CHUNK_ZIDX(coord) ((coord) >> CHUNK_ZBITS)

#define CHUNK_XOFF(coord) ((coord) & (CHUNK_XSIZE-1))
#define CHUNK_ZOFF(coord) ((coord) & (CHUNK_ZSIZE-1))

union chunk_coord
{
	gint xz[2];
	gint64 i64;
};

struct chunk
{
	union chunk_coord key;
	unsigned char blocks[CHUNK_XSIZE][CHUNK_ZSIZE][CHUNK_YSIZE];
	unsigned char height[CHUNK_XSIZE][CHUNK_ZSIZE];
	unsigned char surface[CHUNK_XSIZE][CHUNK_ZSIZE];
};

static GHashTable *chunk_table = 0;

static struct chunk *get_chunk(guint64 coord)
{
	struct chunk *c = g_hash_table_lookup(chunk_table, &coord);
	if (c)
		return c;

	c = g_malloc0(sizeof *c);
	c->key.i64 = coord;
	g_hash_table_insert(chunk_table, &c->key.i64, c);

	return c;
}

static void handle_chunk(int x0, int y0, int z0,
                         int xs, int ys, int zs,
                         unsigned zlen, unsigned char *zdata)
{
	z_stream zstr;

	zstr.zalloc = 0;
	zstr.zfree = 0;
	if (inflateInit(&zstr) != Z_OK)
		abort();

	zstr.next_in = zdata;
	zstr.avail_in = zlen;

	gint64 current_chunk = 0x7fffffffffffffffll;
	struct chunk *c = 0;

	if (y0 < 0 || y0+ys > CHUNK_YSIZE)
		abort();

	for (int x = x0; x < x0+xs; x++)
	{
		for (int z = z0; z < z0+zs; z++)
		{
			union chunk_coord cc;
			cc.xz[0] = CHUNK_XIDX(x); cc.xz[1] = CHUNK_ZIDX(z);

			if (cc.i64 != current_chunk)
			{
				c = get_chunk(cc.i64);
				current_chunk = cc.i64;
			}

			zstr.next_out = &c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0];
			zstr.avail_out = ys;

			while (inflate(&zstr, Z_SYNC_FLUSH) == Z_OK && zstr.avail_out > 0)
				/* loop */;

			int h = c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

			if (y0+ys >= h)
			{
				unsigned char surfblock = 0x00; /* air */
				int newh = h;

				unsigned char *stack = c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];
				for (; h < y0+ys; h++)
				{
					if (!stack[h])
						continue; /* air */
					surfblock = stack[h];
					newh = h;
				}

				c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = newh;
				c->surface[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = surfblock;
			}
		}
	}
}

void world_init(void)
{
	chunk_table = g_hash_table_new_full(g_int64_hash, g_int64_equal, 0, g_free);
}

gpointer world_thread(gpointer data)
{
	GAsyncQueue *q = data;

	while (1)
	{
		packet_t *packet = g_async_queue_pop(q);

		unsigned char *p;

		switch (packet->id)
		{
		case PACKET_CHUNK:
			p = &packet->bytes[packet->field_offset[6]];
			handle_chunk(packet_int(packet, 0), packet_int(packet, 1), packet_int(packet, 2),
			             packet_int(packet, 3)+1, packet_int(packet, 4)+1, packet_int(packet, 5)+1,
			             (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3], &p[4]);
			break;
		}
	}
}
