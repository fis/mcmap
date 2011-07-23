#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
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

jint chunk_min_x = 0, chunk_min_z = 0;
jint chunk_max_x = 0, chunk_max_z = 0;

static GHashTable *entity_table = 0;
static GHashTable *anentity_table = 0;
static GMutex *entity_mutex = 0;

static jint entity_player = -1;
static jint entity_vehicle = -1;

static jlong world_seed = 0;
static int spawn_known = 0;
static jint spawn_x = 0, spawn_y = 0, spawn_z = 0;

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

	jint x = coord->x, z = coord->z;

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

unsigned char *world_stack(jint x, jint z, int gen)
{
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

	struct chunk *c = world_chunk(&cc, gen);
	if (!c)
		return 0;

	return c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];
}

jint world_getheight(jint x, jint z)
{
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

	struct chunk *c = world_chunk(&cc, 0);
	if (!c)
		return -1;

	return c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];
}

static gboolean handle_compressed_chunk(jint x0, jint y0, jint z0,
                                        jint xs, jint ys, jint zs,
                                        unsigned zlen, unsigned char *zdata,
                                        gboolean update_map)
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
		stopf("chunk update decompression: inflateInit: %s", zError(err));

	while (zstr.avail_in)
	{
		err = inflate(&zstr, Z_PARTIAL_FLUSH);
		if (err != Z_OK && err != Z_STREAM_END)
			stopf("chunk update decompression: inflate: %s", zError(err));
		if (err == Z_STREAM_END)
			break;
	}

	int zbuf_len = (sizeof zbuf) - zstr.avail_out;
	inflateEnd(&zstr);

	if (zbuf_len < xs*ys*zs)
		stopf("broken decompressed chunk length: %d != %d and < %d",
		      (int)zbuf_len, (int)(5*xs*ys*zs+1)/2, xs*ys*zs);

	struct buffer zb = { zbuf_len, zbuf };
#ifdef FEAT_FULLCHUNK
	struct buffer zb_meta = OFFSET_BUFFER(zb, xs*ys*zs);
	struct buffer zb_light_blocks = OFFSET_BUFFER(zb_meta, (xs*ys*zs+1)/2);
	struct buffer zb_light_sky = OFFSET_BUFFER(zb_light_blocks, (xs*ys*zs+1)/2);
#else
	struct buffer zb_meta = { 0, 0 };
	struct buffer zb_light_blocks = { 0, 0 };
	struct buffer zb_light_sky = { 0, 0 };
#endif
	return world_handle_chunk(x0, y0, z0, xs, ys, zs, zb, zb_meta, zb_light_blocks, zb_light_sky, update_map);
}

gboolean world_handle_chunk(jint x0, jint y0, jint z0,
                            jint xs, jint ys, jint zs,
                            struct buffer zb, struct buffer zb_meta,
                            struct buffer zb_light_blocks, struct buffer zb_light_sky,
                            gboolean update_map)
{
	struct coord current_chunk = { .x = -0x80000000, .z = -0x80000000 };
	struct chunk *c = 0;

	jint yupds = ys;

	if (ys < 0)
	{
		log_print("[WARN] Invalid chunk size! Probably WorldEdit; go yell at the author.");
		return FALSE;
	}

	if (y0 > CHUNK_YSIZE)
		stopf("too high chunk update: %d..%d", y0, y0+ys-1);
	else if (y0 + ys > CHUNK_YSIZE)
		yupds = CHUNK_YSIZE - y0;

	jint c_min_x = INT_MAX, c_min_z = INT_MAX;
	jint c_max_x = INT_MIN, c_max_z = INT_MIN;
	gboolean changed = FALSE;

	for (jint x = x0; x < x0+xs; x++)
	{
		for (jint z = z0; z < z0+zs; z++)
		{
			struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };

			if (!COORD_EQUAL(cc, current_chunk))
			{
				c = world_chunk(&cc, 1);
				current_chunk = cc;
			}

			if (!changed && memcmp(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb.data, yupds) != 0)
				changed = TRUE;

			memcpy(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb.data, yupds);
			ADVANCE_BUFFER(zb, ys);

#ifdef FEAT_FULLCHUNK
			if ((ys+1)/2 <= zb_meta.len)
				memcpy(&c->meta[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_meta.data, (yupds+1)/2);
			if ((ys+1)/2 <= zb_light_blocks.len)
				memcpy(&c->light_blocks[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_light_blocks.data, (yupds+1)/2);
			if ((ys+1)/2 <= zb_light_sky.len)
				memcpy(&c->light_sky[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2)], zb_light_sky.data, (yupds+1)/2);
			ADVANCE_BUFFER(zb_meta, (ys+1)/2);
			ADVANCE_BUFFER(zb_light_blocks, (ys+1)/2);
			ADVANCE_BUFFER(zb_light_sky, (ys+1)/2);
#endif

			jint h = c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

			if (y0+yupds >= h)
			{
				jint newh = y0 + yupds;
				if (newh >= CHUNK_YSIZE)
					newh = CHUNK_YSIZE - 1;

				unsigned char *stack = c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

				while (!stack[newh] && newh > 0)
					newh--;

				c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = newh;
				changed = TRUE;
			}

			if (cc.x < c_min_x) c_min_x = cc.x;
			if (cc.x > c_max_x) c_max_x = cc.x;
			if (cc.z < c_min_z) c_min_z = cc.z;
			if (cc.z > c_max_z) c_max_z = cc.z;
		}
	}

	if (changed && update_map)
		map_update(c_min_x, c_max_x, c_min_z, c_max_z);

	return changed;
}

static inline int block_change(struct chunk *c, jint x, jint y, jint z, unsigned char type)
{
	if (y < 0 || y >= CHUNK_YSIZE)
		return 0; /* sometimes server sends Y=CHUNK_YSIZE block-to-air "updates" */

	int changed = (c->blocks[x][z][y] != type);
	c->blocks[x][z][y] = type;

	if (y >= c->height[x][z])
	{
		jint newh = y;

		if (!type)
			while (!c->blocks[x][z][newh] && newh > 0)
				newh--;

		if (c->height[x][z] != newh)
			changed = 1;
		c->height[x][z] = newh;
	}

	return changed;
}

static void handle_multi_set_block(jint cx, jint cz, jint size, unsigned char *coord, unsigned char *type)
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

static void handle_set_block(jint x, jint y, jint z, jint type)
{	
	struct coord cc = { .x = CHUNK_XIDX(x), .z = CHUNK_ZIDX(z) };
	struct chunk *c = world_chunk(&cc, 0);
	if (!c)
		return; /* edit in an unloaded chunk */

	if (block_change(c, CHUNK_XOFF(x), y, CHUNK_ZOFF(z), type))
		map_update(cc.x, cc.x, cc.z, cc.z);
}

static void entity_add(jint id, unsigned char *name, jint x, jint y, jint z)
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

static void entity_del(jint id)
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

static void entity_move(jint id, jint x, jint y, jint z, int relative)
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

	jint ex = e->ax/32, ez = e->az/32;
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

// FIXME: bad
void world_destroy(void)
{
	g_hash_table_destroy(chunk_table);
	g_hash_table_destroy(entity_table);
	g_mutex_free(entity_mutex);
	g_hash_table_destroy(anentity_table);
	chunk_table = entity_table = anentity_table = 0;
	entity_mutex = 0;
}

gpointer world_thread(gpointer data)
{
	GAsyncQueue *q = data;

	while (1)
	{
		packet_t *packet = g_async_queue_pop(q);

		unsigned char *p;
		jint t;
		jlong tl;

		switch (packet->id)
		{
		case PACKET_CHUNK:
			p = &packet->bytes[packet->field_offset[6]];
			handle_compressed_chunk(packet_int(packet, 0), packet_int(packet, 1), packet_int(packet, 2),
			                        packet_int(packet, 3)+1, packet_int(packet, 4)+1, packet_int(packet, 5)+1,
			                        (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3], &p[4], TRUE);
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
			entity_add(packet_int(packet, 0),
			           packet_string(packet, 1, 0),
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
				jint new_vehicle = packet_int(packet, 1);
				if (new_vehicle < 0)
					log_print("[INFO] Unmounted vehicle %d normally", entity_vehicle);
				else
					log_print("[INFO] Mounted vehicle %d", new_vehicle);
				entity_vehicle = packet_int(packet, 1);
			}
			break;

		case PACKET_TIME:
			tl = packet_long(packet, 0);
			tl %= 24000;
			map_update_time(tl);
			break;

		case PACKET_UPDATE_HEALTH:
			player_health = packet_int(packet, 0);
			break;

		case PACKET_CHAT:
			p = packet_string(packet, 0, &t);
			if (t >= 3 && p[0] == '/' && p[1] == '/')
				cmd_parse(p+2, t-2);
			g_free(p);
			break;
		}

		packet_free(packet);
	}

	return NULL;
}

/* world file IO routines */

#ifdef FEAT_FULLCHUNK

#define REG_XBITS 5
#define REG_ZBITS 5

#define REG_XSIZE (1 << REG_XBITS)
#define REG_ZSIZE (1 << REG_ZBITS)

#define REG_XZ (REG_XSIZE * REG_ZSIZE)

#define REG_XCOORD(cx) ((cx) >> REG_XBITS)
#define REG_ZCOORD(cz) ((cz) >> REG_ZBITS)

#define REG_XOFF(cx) ((cx) & (REG_XSIZE - 1))
#define REG_ZOFF(cz) ((cz) & (REG_ZSIZE - 1))

#define SECT_SIZE 4096

struct region
{
	struct coord key;
	unsigned offsets[REG_ZSIZE][REG_XSIZE];
	unsigned char sects[REG_ZSIZE][REG_XSIZE];
	jint tstamps[REG_ZSIZE][REG_XSIZE];
	unsigned nsect;
	int fd;
};

static const char base36_chars[36] = "0123456789abcdefghijklmnopqrstuvwxyz";

static char *base36_encode(jint value, char *buf, int bufsize)
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

static void world_append_chunk(struct region *reg, struct chunk *c)
{
	/* dump the chunk data into compressed NBT */

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

	unsigned clen;
	unsigned char *cdata = nbt_compress(data, &clen);

	nbt_free(data);

	/* append it to our current region file */

	unsigned csect = (5 + clen + SECT_SIZE - 1)/SECT_SIZE;

	int zo = REG_ZOFF(c->key.z), xo = REG_XOFF(c->key.x);

	reg->offsets[zo][xo] = 2 + reg->nsect;
	reg->sects[zo][xo] = csect;
	reg->tstamps[zo][xo] = 0; /* TODO: proper timestamps */
	reg->nsect += csect;

	unsigned char bhdr[5];
	bhdr[0] = clen >> 24;
	bhdr[1] = clen >> 16;
	bhdr[2] = clen >> 8;
	bhdr[3] = clen;
	bhdr[4] = 2;

	if (lseek(reg->fd, reg->offsets[zo][xo] * SECT_SIZE, SEEK_SET) == (off_t)-1)
		die("lseek failed for region chunk");
	if (write(reg->fd, bhdr, sizeof bhdr) != sizeof bhdr)
		die("write failed for region chunk header");
	if (write(reg->fd, cdata, clen) != clen)
		die("write failed for region chunk data");

	g_free(cdata);
}

int world_save(char *dir)
{
	/* write the top-level level.dat */

	int pathbufsize = strlen(dir) + 64;
	char pathbuf[pathbufsize];

	g_snprintf(pathbuf, pathbufsize, "%s/level.dat", dir);
	FILE *f = fopen(pathbuf, "wb");
	if (!f)
		return 0;

	struct nbt_tag *data = nbt_new_struct("Data");

	nbt_struct_add(data, nbt_new_long("Time", 0));
	nbt_struct_add(data, nbt_new_long("LastPlayed", 0));

	nbt_struct_add(data, nbt_new_int("SpawnX", NBT_TAG_INT, spawn_x));
	nbt_struct_add(data, nbt_new_int("SpawnY", NBT_TAG_INT, spawn_y));
	nbt_struct_add(data, nbt_new_int("SpawnZ", NBT_TAG_INT, spawn_z));

	nbt_struct_add(data, nbt_new_long("RandomSeed", world_seed));

	nbt_struct_add(data, nbt_new_int("version", NBT_TAG_INT, 19132));
	nbt_struct_add(data, nbt_new_str("LevelName", "mcmap dump"));

	unsigned clen;
	unsigned char *cdata = nbt_compress(data, &clen);
	nbt_free(data);

	int ret = (fwrite(cdata, 1, clen, f) == clen);
	g_free(cdata);

	if (!ret)
		return 0;

	/* write all the chunks into region files */

	GHashTable *region_table = g_hash_table_new_full(coord_hash, coord_equal, 0, g_free);

	g_snprintf(pathbuf, pathbufsize, "%s/region", dir);
	mkdir(pathbuf, 0777); /* ignore errors; might already exist */

	GHashTableIter iter;
	gpointer ckey, cvalue;

	g_hash_table_iter_init(&iter, chunk_table);
	while (g_hash_table_iter_next(&iter, &ckey, &cvalue))
	{
		struct chunk *c = cvalue;

		/* find the corresponding region */

		struct coord rc = { .x = REG_XCOORD(c->key.x), .z = REG_ZCOORD(c->key.z) };
		struct region *reg = g_hash_table_lookup(region_table, &rc);

		if (!reg)
		{
			char file_x_buf[16], file_z_buf[16];
			char *file_x = base36_encode(rc.x, file_x_buf, sizeof file_x_buf);
			char *file_z = base36_encode(rc.z, file_z_buf, sizeof file_z_buf);
			g_snprintf(pathbuf, pathbufsize, "%s/region/r.%s.%s.mcr", dir, file_x, file_z);

			reg = g_malloc0(sizeof *reg);
			reg->key = rc;
			reg->nsect = 0;
			reg->fd = open(pathbuf, O_WRONLY|O_CREAT|O_TRUNC, 0666);
			if (reg->fd == -1)
				dief("can't open region file: %s", pathbuf);

			g_hash_table_insert(region_table, &reg->key, reg);
		}

		/* append a chunk into it */

		world_append_chunk(reg, c);
	}

	/* finally update the region file headers and close them */

	g_hash_table_iter_init(&iter, region_table);
	while (g_hash_table_iter_next(&iter, &ckey, &cvalue))
	{
		struct region *reg = cvalue;
		unsigned char reghdr[REG_XZ*8];

		for (int z = 0; z < REG_ZSIZE; z++)
		{
			for (int x = 0; x < REG_XSIZE; x++)
			{
				int i = z*REG_XSIZE + x;
				reghdr[4*i+0] = reg->offsets[z][x] >> 16;
				reghdr[4*i+1] = reg->offsets[z][x] >> 8;
				reghdr[4*i+2] = reg->offsets[z][x];
				reghdr[4*i+3] = reg->sects[z][x];
				reghdr[REG_XZ*4 + 4*i+0] = reg->tstamps[z][x] >> 24;
				reghdr[REG_XZ*4 + 4*i+1] = reg->tstamps[z][x] >> 16;
				reghdr[REG_XZ*4 + 4*i+2] = reg->tstamps[z][x] >> 8;
				reghdr[REG_XZ*4 + 4*i+3] = reg->tstamps[z][x];
			}
		}

		if (lseek(reg->fd, 0, SEEK_SET) == (off_t)-1)
			die("lseek failed for region header update");
		if (write(reg->fd, reghdr, sizeof reghdr) != sizeof reghdr)
			die("write failed for region header update");

		close(reg->fd);
	}

	g_hash_table_unref(region_table);

	return 1;
}

#endif /* FEAT_FULLCHUNK */
