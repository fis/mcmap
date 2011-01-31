#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <SDL.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <zlib.h>

#include "cmd.h"
#include "protocol.h"
#include "common.h"
#include "map.h"
#include "nbt.h"
#include "world.h"

static GHashTable *chunk_table = 0;

int chunk_min_x = 0, chunk_min_z = 0;
int chunk_max_x = 0, chunk_max_z = 0;

static GHashTable *entity_table = 0;
static GHashTable *anentity_table = 0;
static GMutex *entity_mutex = 0;

static int entity_player = -1;
static int entity_vehicle = -1;

static long long world_seed = 0;
static int spawn_known = 0;
static int spawn_x = 0, spawn_y = 0, spawn_z = 0;

volatile int world_running = 1;

struct chunk *world_chunk(struct coord *coord, int gen)
{
	struct chunk *c = g_hash_table_lookup(chunk_table, coord);
	if (c)
		return c;
	if (!gen)
		return 0;

	c = g_malloc0(sizeof *c);
	c->key = *coord;
	g_hash_table_insert(chunk_table, &c->key, c);

	int x = coord->x, z = coord->z;

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

unsigned char *world_stack(int x, int z, int gen)
{
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

	struct chunk *c = world_chunk(&cc, gen);
	if (!c)
		return 0;

	return c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];
}

int world_getheight(int x, int z)
{
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

	struct chunk *c = world_chunk(&cc, 0);
	if (!c)
		return -1;

	return c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];
}

static void handle_chunk(int x0, int y0, int z0,
                         int xs, int ys, int zs,
                         unsigned zlen, unsigned char *zdata)
{
	static unsigned char zbuf[256*1024];
	int err;

	z_stream zstr = {
		.next_in  = zdata,
		.avail_in = zlen,
		.next_out = zbuf,
		.avail_out = sizeof zbuf
	};

	if ((err = inflateInit(&zstr)) != Z_OK)
		stopf("chunk update decompression: inflateInit: %d", err);

	while (zstr.avail_in)
	{
		err = inflate(&zstr, Z_PARTIAL_FLUSH);
		if (err != Z_OK && err != Z_STREAM_END)
			stopf("chunk update decompression: inflate: %d", err);
		if (err == Z_STREAM_END)
			break;
	}

	int zbuf_len = (sizeof zbuf) - zstr.avail_out;
	inflateEnd(&zstr);

	if (zbuf_len < xs*ys*zs)
		stopf("broken decompressed chunk length: %d != %d and < %d",
		      (int)zbuf_len, (int)(5*xs*ys*zs+1)/2, xs*ys*zs);

	struct coord current_chunk = { .x = -0x80000000, .z = -0x80000000 };
	struct chunk *c = 0;

	int yupds = ys;

	if (y0 > CHUNK_YSIZE)
		stopf("too high chunk update: %d..%d", y0, y0+ys-1);
	else if (y0 + ys > CHUNK_YSIZE)
		yupds = CHUNK_YSIZE - y0;

	int c_min_x = INT_MAX, c_min_z = INT_MAX;
	int c_max_x = INT_MIN, c_max_z = INT_MIN;

	unsigned char *zb = zbuf;
#ifdef FEAT_FULLCHUNK
	unsigned char *zb_meta = zbuf + xs*ys*zs;
	unsigned char *zb_light_blocks = zb_meta + (xs*ys*zs+1)/2;
	unsigned char *zb_light_sky = zb_light_blocks + (xs*ys*zs+1)/2;
#endif

	int changed = 0;

	for (int x = x0; x < x0+xs; x++)
	{
		for (int z = z0; z < z0+zs; z++)
		{
			struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

			if (!COORD_EQUAL(cc, current_chunk))
			{
				c = world_chunk(&cc, 1);
				current_chunk = cc;
			}

			if (!changed && memcmp(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb, yupds) != 0)
				changed = 1;

			memcpy(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb, yupds);
			zb += ys;

#ifdef FEAT_FULLCHUNK
			if (zb_meta + (ys+1)/2 - zbuf <= zbuf_len)
				memcpy(&c->meta[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_meta, (yupds+1)/2);
			if (zb_light_blocks + (ys+1)/2 - zbuf <= zbuf_len)
				memcpy(&c->light_blocks[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_light_blocks, (yupds+1)/2);
			if (zb_light_sky + (ys+1)/2 - zbuf <= zbuf_len)
				memcpy(&c->light_sky[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_light_sky, (yupds+1)/2);
			zb_meta += (ys+1)/2;
			zb_light_blocks += (ys+1)/2;
			zb_light_sky += (ys+1)/2;
#endif

			int h = c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

			if (y0+yupds >= h)
			{
				int newh = y0 + yupds;
				if (newh >= CHUNK_YSIZE)
					newh = CHUNK_YSIZE - 1;

				unsigned char *stack = c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

				while (!stack[newh] && newh > 0)
					newh--;

				c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = newh;
				changed = 1;
			}

			if (cc.x < c_min_x) c_min_x = cc.x;
			if (cc.x > c_max_x) c_max_x = cc.x;
			if (cc.z < c_min_z) c_min_z = cc.z;
			if (cc.z > c_max_z) c_max_z = cc.z;
		}
	}

	if (changed)
		map_update(c_min_x, c_max_x, c_min_z, c_max_z);
}

static inline int block_change(struct chunk *c, int x, int y, int z, unsigned char type)
{
	if (y < 0 || y >= CHUNK_YSIZE)
		return 0; /* sometimes server sends Y=CHUNK_YSIZE block-to-air "updates" */

	int changed = (c->blocks[x][z][y] == type);
	c->blocks[x][z][y] = type;

	if (y >= c->height[x][z])
	{
		int newh = y;

		if (!type)
			while (!c->blocks[x][z][newh] && newh > 0)
				newh--;

		if (c->height[x][z] != newh)
			changed = 1;
		c->height[x][z] = newh;
	}

	return changed;
}

static void handle_multi_set_block(int cx, int cz, int size, unsigned char *coord, unsigned char *type)
{
	struct coord cc = { .x = cx, .z = cz };
	struct chunk *c = world_chunk(&cc, 0);
	if (!c)
		return; /* edit in an unloaded chunk */

	int changed = 0;

	while (size--)
	{
		int x = coord[0] >> 4, y = coord[1], z = coord[0] & 0x0f;
		coord += 2;
		if (block_change(c, x, y, z, *type++))
			changed = 1;
	}

	if (changed)
		map_update(cx, cx, cz, cz);
}

static void handle_set_block(int x, int y, int z, int type)
{	
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };
	struct chunk *c = world_chunk(&cc, 0);
	if (!c)
		return; /* edit in an unloaded chunk */

	if (block_change(c, CHUNK_XOFF(x), y, CHUNK_ZOFF(z), type))
		map_update(cc.x, cc.x, cc.z, cc.z);
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

	if (name)
	{
		log_print("[INFO] Player appeared: %s", name);
		g_mutex_lock(entity_mutex);
		g_hash_table_replace(entity_table, &e->id, e);
		g_mutex_unlock(entity_mutex);
		map_repaint();
	}
	else
		g_hash_table_replace(anentity_table, &e->id, e);
}

static void entity_del(int id)
{
	if (id == entity_vehicle)
	{
		entity_vehicle = -1;
		log_print("[INFO] Unmounted vehicle %d by destroying", id);
	}

	struct entity *e = g_hash_table_lookup(entity_table, &id);
	if (e)
	{
		log_print("[INFO] Player disappeared: %s", e->name);
		g_mutex_lock(entity_mutex);
		g_hash_table_remove(entity_table, &id);
		g_mutex_unlock(entity_mutex);
		map_repaint();
		return;
	}

	g_hash_table_remove(anentity_table, &id);
}

static void entity_move(int id, int x, int y, int z, int relative)
{
	struct entity *e;

	e = g_hash_table_lookup(anentity_table, &id);
	if (!e)
		e = g_hash_table_lookup(entity_table, &id);

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

	if (id == entity_vehicle)
		map_update_player_pos(ex, e->ay/32, ez);
	else
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
	chunk_table = g_hash_table_new_full(coord_hash, coord_equal, 0, g_free);

	entity_table = g_hash_table_new_full(g_int_hash, g_int_equal, 0, entity_free);
	entity_mutex = g_mutex_new();
	anentity_table = g_hash_table_new_full(g_int_hash, g_int_equal, 0, entity_free);
}

gpointer world_thread(gpointer data)
{
	GAsyncQueue *q = data;

	while (1)
	{
		packet_t *packet = g_async_queue_pop(q);

		unsigned char *p;
		int t;
		long long ll;

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
			                       t, p+2, p+2+t*2);
			break;

		case PACKET_SET_BLOCK:
			handle_set_block(packet_int(packet, 0),
			                 packet_int(packet, 1),
			                 packet_int(packet, 2),
			                 packet_int(packet, 3));
			break;

		case PACKET_LOGIN:
			if (packet->flags & PACKET_TO_CLIENT)
			{
				entity_player = packet_int(packet, 0);
				world_seed = packet_long(packet, 3);
			}
			break;

		case PACKET_PLAYER_ROTATE:
			map_update_player_dir(packet_double(packet, 0),
			                      packet_double(packet, 1));
			break;

		case PACKET_PLAYER_MOVE_ROTATE:
			map_update_player_dir(packet_double(packet, 4),
			                      packet_double(packet, 5));

			/* fall-thru to PACKET_PLAYER_MOVE */

		case PACKET_PLAYER_MOVE:
			if (entity_vehicle < 0)
				map_update_player_pos(packet_double(packet, 0),
				                      packet_double(packet, 1),
				                      packet_double(packet, 3));

			if ((packet->flags & PACKET_TO_CLIENT) && !spawn_known)
			{
				spawn_known = 1;
				spawn_x = packet_double(packet, 0);
				spawn_y = packet_double(packet, 1);
				spawn_z = packet_double(packet, 3);
			}

			break;


		case PACKET_ENTITY_SPAWN_NAMED:
			p = packet_string(packet, 1, &t);
			entity_add(packet_int(packet, 0),
			           (unsigned char *)g_strndup((gchar *)p, t),
			           packet_int(packet, 2),
			           packet_int(packet, 3),
			           packet_int(packet, 4));
			break;

		case PACKET_ENTITY_SPAWN_OBJECT:
			entity_add(packet_int(packet, 0),
			           0,
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

		case PACKET_ENTITY_ATTACH:
			if (packet_int(packet, 0) == entity_player)
			{
				int new_vehicle = packet_int(packet, 1);
				if (new_vehicle < 0)
					log_print("[INFO] Unmounted vehicle %d normally", entity_vehicle);
				else
					log_print("[INFO] Mounted vehicle %d", new_vehicle);
				entity_vehicle = packet_int(packet, 1);
			}
			break;

		case PACKET_TIME:
			ll = packet_long(packet, 0);
			ll %= 24000;
			map_update_time(ll);
			break;

		case PACKET_CHAT:
			p = packet_string(packet, 0, &t);
			if (t >= 3 && p[0] == '/' && p[1] == '/')
				cmd_parse(p+2, t-2);
			break;
		}

		packet_free(packet);
	}

	return NULL;
}

/* world file IO routines */

#ifdef FEAT_FULLCHUNK

static const char base36_chars[36] = "0123456789abcdefghijklmnopqrstuvwxyz";

static char *base36_encode(int value, char *buf, int bufsize)
{
	buf[bufsize-1] = 0;
	bufsize -= 2;

	int neg = 0;

	if (value < 0)
		neg = 1, value = -value;
	else if (value == 0)
	{
		buf[bufsize] = '0';
		return buf+bufsize;
	}

	while (value && bufsize >= 0)
	{
		buf[bufsize--] = base36_chars[value % 36];
		value /= 36;
	}

	if (neg && bufsize >= 0)
		buf[bufsize--] = '-';

	return buf + bufsize + 1;
}

static void world_save_block(gpointer key, gpointer value, gpointer userdata)
{
	struct chunk *c = value;
	char *dir = userdata;

	int pathbufsize = strlen(dir) + 64;
	char pathbuf[pathbufsize];

	/* compute and create directory part of chunk path */

	char dir_x_buf[3], dir_z_buf[3];

	char *dir_x = base36_encode(c->key.x & 63, dir_x_buf, sizeof dir_x_buf);
	char *dir_z = base36_encode(c->key.z & 63, dir_z_buf, sizeof dir_z_buf);

	g_snprintf(pathbuf, pathbufsize, "%s/%s", dir, dir_x);
	g_mkdir(pathbuf, 0777);
	g_snprintf(pathbuf, pathbufsize, "%s/%s/%s", dir, dir_x, dir_z);
	g_mkdir(pathbuf, 0777);

	/* construct and open the chunk file */

	char file_x_buf[16], file_z_buf[16];

	char *file_x = base36_encode(c->key.x, file_x_buf, sizeof file_x_buf);
	char *file_z = base36_encode(c->key.z, file_z_buf, sizeof file_z_buf);

	g_snprintf(pathbuf, pathbufsize, "%s/%s/%s/c.%s.%s.dat", dir, dir_x, dir_z, file_x, file_z);

	/* dump the chunk data */

	struct nbt_tag *data = nbt_new_struct("Level");

	nbt_struct_add(data, nbt_new_blob("Blocks", NBT_TAG_BLOB, c->blocks, CHUNK_NBLOCKS));
	nbt_struct_add(data, nbt_new_blob("Data", NBT_TAG_BLOB, c->meta, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("BlockLight", NBT_TAG_BLOB, c->light_blocks, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("SkyLight", NBT_TAG_BLOB, c->light_sky, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("HeightMap", NBT_TAG_BLOB, c->height, CHUNK_XSIZE*CHUNK_ZSIZE)); /* TODO FIXME: indexing X/Z */

	/* TODO: Entities, TileEntities */

	nbt_struct_add(data, nbt_new_long("LastUpdate", 0));

	nbt_struct_add(data, nbt_new_int("xPos", NBT_TAG_INT, c->key.x));
	nbt_struct_add(data, nbt_new_int("zPos", NBT_TAG_INT, c->key.z));

	nbt_struct_add(data, nbt_new_int("TerrainPopulated", NBT_TAG_BYTE, 1));

	nbt_save(pathbuf, data);
	nbt_free(data);
}

int world_save(char *dir)
{
	/* write the top-level level.dat */

	int pathbufsize = strlen(dir) + 16;
	char pathbuf[pathbufsize];

	g_snprintf(pathbuf, pathbufsize, "%s/level.dat", dir);

	struct nbt_tag *data = nbt_new_struct("Data");

	nbt_struct_add(data, nbt_new_long("Time", 0));
	nbt_struct_add(data, nbt_new_long("LastPlayed", 0));

	nbt_struct_add(data, nbt_new_int("SpawnX", NBT_TAG_INT, spawn_x));
	nbt_struct_add(data, nbt_new_int("SpawnY", NBT_TAG_INT, spawn_y));
	nbt_struct_add(data, nbt_new_int("SpawnZ", NBT_TAG_INT, spawn_z));

	nbt_struct_add(data, nbt_new_long("RandomSeed", world_seed));

	int ret = nbt_save(pathbuf, data);
	nbt_free(data);

	if (!ret)
		return 0;

	/* write the chunk files */

	g_hash_table_foreach(chunk_table, world_save_block, dir);

	return 1;
}

#endif /* FEAT_FULLCHUNK */
