#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <glib.h>
#include <zlib.h>

#include "map.h"
#include "protocol.h"
#include "world.h"

static GHashTable *chunk_table = 0;

int chunk_min_x = 0, chunk_min_z = 0;
int chunk_max_x = 0, chunk_max_z = 0;

struct chunk *world_chunk(guint64 coord, int gen)
{
	struct chunk *c = g_hash_table_lookup(chunk_table, &coord);
	if (c)
		return c;
	if (!gen)
		return 0;

	c = g_malloc0(sizeof *c);
	c->key.i64 = coord;
	g_hash_table_insert(chunk_table, &c->key.i64, c);

	int x = c->key.xz[0], z = c->key.xz[1];

	if (x < chunk_min_x)
		chunk_min_x = x;
	if (x > chunk_max_x)
		chunk_max_x = x;

	if (z < chunk_min_z)
		chunk_min_z = z;
	if (z > chunk_max_z)
		chunk_max_z = z;

	return c;
}

static void handle_chunk(int x0, int y0, int z0,
                         int xs, int ys, int zs,
                         unsigned zlen, unsigned char *zdata)
{
	static unsigned char zbuf[256*1024];
	uLongf zbuf_len = sizeof zbuf;

	if (uncompress(zbuf, &zbuf_len, zdata, zlen) != Z_OK)
		abort();
	if (zbuf_len != (5*xs*ys*zs+1)/2)
		abort();

	gint64 current_chunk = 0x7fffffffffffffffll;
	struct chunk *c = 0;

	if (y0 < 0 || y0+ys > CHUNK_YSIZE)
		abort();

	int c_min_x = INT_MAX, c_min_z = INT_MAX;
	int c_max_x = INT_MIN, c_max_z = INT_MIN;

	unsigned char *zb = zbuf;

	for (int x = x0; x < x0+xs; x++)
	{
		for (int z = z0; z < z0+zs; z++)
		{
			union chunk_coord cc;
			cc.xz[0] = CHUNK_XIDX(x); cc.xz[1] = CHUNK_ZIDX(z);

			if (cc.i64 != current_chunk)
			{
				c = world_chunk(cc.i64, 1);
				current_chunk = cc.i64;
			}

			memcpy(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb, ys);
			zb += ys;

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

#if 0
				int cx = cc.xz[0], cz = cc.xz[1];
				if (!surface_changed)
				{
					surface_changed = 1;
					sc_min_x = cx; sc_max_x = cx;
					sc_min_z = cz; sc_max_z = cz;
				}
				else
				{
					if (cx < sc_min_x) sc_min_x = cx;
					if (cx > sc_max_x) sc_max_x = cx;
					if (cz < sc_min_z) sc_min_z = cz;
					if (cz > sc_max_z) sc_max_z = cz;
				}
#endif
			}

			int cx = cc.xz[0], cz = cc.xz[1];
			if (cx < c_min_x) c_min_x = cx;
			if (cx > c_max_x) c_max_x = cx;
			if (cz < c_min_z) c_min_z = cz;
			if (cz > c_max_z) c_max_z = cz;
		}
	}

	map_update(c_min_x, c_max_x, c_min_z, c_max_z);
#if 0
	if (surface_changed)
	{
		map_update(sc_min_x, sc_max_x, sc_min_z, sc_max_z);
	}
#endif
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

		case PACKET_PLAYER_MOVE:
			map_update_player_pos(packet_double(packet, 0),
			                      packet_double(packet, 1),
			                      packet_double(packet, 3));
			break;

		case PACKET_PLAYER_ROTATE:
			map_update_player_dir(packet_double(packet, 0),
			                      packet_double(packet, 1));
			break;

		case PACKET_PLAYER_MOVE_ROTATE:
			map_update_player_pos(packet_double(packet, 0),
			                      packet_double(packet, 1),
			                      packet_double(packet, 3));
			map_update_player_dir(packet_double(packet, 4),
			                      packet_double(packet, 5));
			break;
		}

		packet_free(packet);
	}
}
