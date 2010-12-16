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

static GHashTable *entity_table = 0;
static GMutex *entity_mutex = 0;

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
				for (h = 0; h < y0+ys; h++)
				{
					if (!stack[h])
						continue; /* air */
					surfblock = stack[h];
					newh = h;
				}

				if (surfblock)
				{
					c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = newh;
					c->surface[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = surfblock;
				}
			}

			int cx = cc.xz[0], cz = cc.xz[1];
			if (cx < c_min_x) c_min_x = cx;
			if (cx > c_max_x) c_max_x = cx;
			if (cz < c_min_z) c_min_z = cz;
			if (cz > c_max_z) c_max_z = cz;
		}
	}

	map_update(c_min_x, c_max_x, c_min_z, c_max_z);
}

static inline void block_change(struct chunk *c, int x, int y, int z, unsigned char type)
{
	c->blocks[x][z][y] = type;
	if (y >= c->height[x][z])
	{
		c->surface[x][z] = type;
		c->height[x][z] = y;

		if (!type)
		{
			int h;
			for (h = y; h > 0; h--)
				if (c->blocks[x][z][h])
					break;
			c->surface[x][z] = c->blocks[x][z][h];
			c->height[x][z] = h;
		}
	}
}

static void handle_multi_set_block(int cx, int cz, int size, short *coord, unsigned char *type)
{
	union chunk_coord cc;
	cc.xz[0] = cx; cc.xz[1] = cz;

	struct chunk *c = world_chunk(cc.i64, 0);
	if (!c)
		return; /* edit in an unloaded chunk */

	while (size--)
	{
		short bc = *coord++;
		int x = (bc >> 12) & 0xf, y = bc & 0xff, z = (bc >> 8) & 0xf;
		block_change(c, x, y, z, *type++);
	}

	map_update(cx, cx, cz, cz);
}

static void handle_set_block(int x, int y, int z, int type)
{	
	union chunk_coord cc;
	cc.xz[0] = CHUNK_XIDX(x); cc.xz[1] = CHUNK_ZIDX(z);

	struct chunk *c = world_chunk(cc.i64, 0);
	if (!c)
		return; /* edit in an unloaded chunk */

	block_change(c, CHUNK_XOFF(x), y, CHUNK_ZOFF(z), type);
	map_update(cc.xz[0], cc.xz[0], cc.xz[1], cc.xz[1]);
}

static void entity_add(int id, unsigned char *name, int x, int y, int z)
{
	struct entity *e = g_malloc(sizeof *e);

	e->id = id;
	e->name = name;
	e->ax = x;
	e->ay = y;
	e->az = z;

	e->x = x/32;
	e->z = z/32;

	printf("[PLAYER] appear: %s\n", name);

	g_mutex_lock(entity_mutex);
	g_hash_table_replace(entity_table, &e->id, e);
	g_mutex_unlock(entity_mutex);

	map_repaint();
}

static void entity_del(int id)
{
	struct entity *e = g_hash_table_lookup(entity_table, &id);
	if (!e)
		return;

	printf("[PLAYER] disappear: %s\n", e->name);

	g_mutex_lock(entity_mutex);
	g_hash_table_remove(entity_table, &id);
	g_mutex_unlock(entity_mutex);

	map_repaint();
}

static void entity_move(int id, int x, int y, int z, int relative)
{
	struct entity *e = g_hash_table_lookup(entity_table, &id);
	if (!e)
		return;

	if (relative)
	{
		e->ax += x;
		e->ay += y;
		e->az += z;
	}
	else
	{
		e->ax = x;
		e->ay = y;
		e->az = z;
	}

	int ex = e->ax/32, ez = e->az/32;
	if (e->x == ex && e->z == ez)
		return;

	e->x = ex;
	e->z = ez;
	map_repaint();
}

static void entity_free(gpointer ep)
{
	struct entity *e = ep;
	g_free(e->name);
	g_free(e);
}

struct entity_walk_callback_data
{
	void (*callback)(struct entity *e, void *userdata);
	void *userdata;
};

static void entity_walk_callback(gpointer key, gpointer value, gpointer userdata)
{
	struct entity_walk_callback_data *d = userdata;
	d->callback(value, d->userdata);
}

void world_entities(void (*callback)(struct entity *e, void *userdata), void *userdata)
{
	struct entity_walk_callback_data d = { .callback = callback, .userdata = userdata };
	g_mutex_lock(entity_mutex);
	g_hash_table_foreach(entity_table, entity_walk_callback, &d);
	g_mutex_unlock(entity_mutex);
}

void world_init(void)
{
	chunk_table = g_hash_table_new_full(g_int64_hash, g_int64_equal, 0, g_free);

	entity_table = g_hash_table_new_full(g_int_hash, g_int_equal, 0, entity_free);
	entity_mutex = g_mutex_new();
}

gpointer world_thread(gpointer data)
{
	GAsyncQueue *q = data;

	while (1)
	{
		packet_t *packet = g_async_queue_pop(q);

		unsigned char *p;
		int t;

		switch (packet->id)
		{
		case PACKET_CHUNK:
			p = &packet->bytes[packet->field_offset[6]];
			handle_chunk(packet_int(packet, 0), packet_int(packet, 1), packet_int(packet, 2),
			             packet_int(packet, 3)+1, packet_int(packet, 4)+1, packet_int(packet, 5)+1,
			             (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3], &p[4]);
			break;

		case PACKET_MULTI_SET_BLOCK:
			p = &packet->bytes[packet->field_offset[2]];
			t = (p[0] << 8) | p[1];
			handle_multi_set_block(packet_int(packet, 0), packet_int(packet, 1),
			                       t, (short *)(p+2), p+2+t*2);
			break;

		case PACKET_SET_BLOCK:
			handle_set_block(packet_int(packet, 0),
			                 packet_int(packet, 1),
			                 packet_int(packet, 2),
			                 packet_int(packet, 3));
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

		case PACKET_ENTITY_SPAWN_NAMED:
			p = packet_string(packet, 1, &t);
			entity_add(packet_int(packet, 0),
			           (unsigned char *)g_strndup((gchar *)p, t),
			           packet_int(packet, 2),
			           packet_int(packet, 3),
			           packet_int(packet, 4));
			break;

		case PACKET_ENTITY_DESTROY:
			entity_del(packet_int(packet, 0));
			break;

		case PACKET_ENTITY_REL_MOVE:
		case PACKET_ENTITY_REL_MOVE_LOOK:
			entity_move(packet_int(packet, 0),
			            packet_int(packet, 1),
			            packet_int(packet, 2),
			            packet_int(packet, 3),
			            1);
			break;

		case PACKET_ENTITY_MOVE:
			entity_move(packet_int(packet, 0),
			            packet_int(packet, 1),
			            packet_int(packet, 2),
			            packet_int(packet, 3),
			            0);
			break;
		}

		packet_free(packet);
	}
}
