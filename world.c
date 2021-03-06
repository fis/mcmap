#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <zlib.h>

#include "config.h"
#include "types.h"
#include "platform.h"
#include "common.h"
#include "cmd.h"
#include "console.h"
#include "nbt.h"
#include "protocol.h"
#include "world.h"
#include "map.h"

static GHashTable *region_table = 0;

GHashTable *world_entities = 0;
G_LOCK_DEFINE(entity_mutex);

static jint entity_player = -1;
static jint entity_vehicle = -1;

int world_time = 0;
static jlong world_seed = 0;

coord3_t player_pos = { .x = 0, .y = 0, .z = 0 };
int player_yaw = 0;
jshort player_health = 0;

static bool spawn_known = false;
static jint spawn_x = 0, spawn_y = 0, spawn_z = 0;

static char *world_path = 0;
static char *region_path = 0;

static GAsyncQueue *worldq = 0;

static gpointer world_thread(gpointer data);

struct region_file
{
	int fd;
	unsigned nsect;
	unsigned char *contents;
	mmap_handle_t contents_map;
	unsigned offsets[REGION_SIZE][REGION_SIZE];
	uint8_t sects[REGION_SIZE][REGION_SIZE];
	jint tstamps[REGION_SIZE][REGION_SIZE];
	unsigned char dirty_chunks[REGION_SIZE][REGION_SIZE];
	GByteArray *sect_bitmap;
};

static void region_free(gpointer gp)
{
	struct region *region = gp;
	for (size_t i = 0; i < NELEMS(region->chunks); i++)
		for (size_t j = 0; j < NELEMS(region->chunks[i]); j++)
			g_free(region->chunks[i][j]);
	g_free(region);
}

static void entity_free(gpointer ep)
{
	struct entity *e = ep;
	g_free(e->name);
	g_free(e);
}

void world_start(const char *path)
{
	region_table = g_hash_table_new_full(coord_glib_hash, coord_glib_equal, 0, region_free);
	world_entities = g_hash_table_new_full(g_int_hash, g_int_equal, 0, entity_free);
	worldq = g_async_queue_new_full(packet_free);
	g_thread_create(world_thread, 0, false, 0);

	/* locate/create the world directory as required */

	if (!path)
		return;

	world_path = g_strdup(path);
	region_path = g_strdup_printf("%s/region", world_path);

	if (g_file_test(world_path, G_FILE_TEST_EXISTS))
	{
		if (!g_file_test(world_path, G_FILE_TEST_IS_DIR))
			dief("world directory not a directory: %s", world_path);
	}
	else
	{
		if (g_mkdir(world_path, 0777) != 0)
			dief("unable to create world directory: %s", world_path);
		if (g_mkdir(region_path, 0777) != 0)
			dief("unable to create region directory: %s", region_path);
	}

	/* scan and index all existing region files */
	/* TODO: maybe log something about this? shouldn't take long... */

	GError *error = 0;
	GDir *dir = g_dir_open(region_path, 0, &error);
	if (!dir)
		dief("unable to scan region directory contents: %s", error->message);

	const char *region_file = 0;
	while ((region_file = g_dir_read_name(dir)))
	{
		char xbuf[64], zbuf[64], *xend, *zend;
		if (sscanf(region_file, "r.%63[^.].%63[^.].mcr", xbuf, zbuf) != 2)
			continue; /* does not look like a proper region file */
		jint x = strtol(xbuf, &xend, 36);
		jint z = strtol(zbuf, &zend, 36);
		if (!*xbuf || !*zbuf || *xend || *zend)
			continue; /* x/z coords don't look like base36 numbers */

		coord_t rc = COORD(x * REGION_XSIZE, z * REGION_ZSIZE);
		struct region *region = world_region(rc, true);
		char *region_file_path = g_strdup_printf("%s/%s", region_path, region_file);
		region->file = world_regfile_open(region_file_path);
		g_free(region_file_path);

		/* TODO FIXME: debugging code: load all regions to memory at start */
		world_regfile_load(region);
	}
}

void world_push(struct directed_packet *dpacket)
{
	struct directed_packet *dpacket_copy = g_new(struct directed_packet, 1);
	dpacket_copy->from = dpacket->from;
	dpacket_copy->p = packet_dup(dpacket->p);
	g_async_queue_push(worldq, dpacket_copy);
}

struct region *world_region(coord_t cc, bool gen)
{
	coord_t rc = COORD(REGION_XMASK(cc.x), REGION_ZMASK(cc.z));
	struct region *region = g_hash_table_lookup(region_table, &rc);

	if (region)
		return region;

	if (!gen)
		return NULL;

	region = g_new(struct region, 1);
	region->key = rc;

	/* Can't use g_malloc0; NULL might not be all-bits-zero */
	for (size_t i = 0; i < NELEMS(region->chunks); i++)
		for (size_t j = 0; j < NELEMS(region->chunks[i]); j++)
			region->chunks[i][j] = NULL;

	region->file = 0;

	g_hash_table_insert(region_table, &region->key, region);

	return region;
}

struct chunk *world_chunk(coord_t cc, bool gen)
{
	struct region *region = world_region(cc, gen);

	if (!region)
		return 0;

	jint xo = CHUNK_XIDX(REGION_XOFF(cc.x)), zo = CHUNK_ZIDX(REGION_ZOFF(cc.z));

	if (gen && !region->chunks[xo][zo])
		region->chunks[xo][zo] = g_malloc0(sizeof(struct chunk));

	return region->chunks[xo][zo];
}

unsigned char *world_stack(coord_t cc, bool gen)
{
	struct chunk *c = world_chunk(cc, gen);
	if (!c)
		return 0;

	return c->blocks[CHUNK_XOFF(cc.x)][CHUNK_ZOFF(cc.z)];
}

jint world_getheight(coord_t cc)
{
	struct chunk *c = world_chunk(cc, false);
	if (!c)
		return -1;

	return c->height[CHUNK_XOFF(cc.x)][CHUNK_ZOFF(cc.z)];
}

static bool handle_compressed_chunk(jint x0, jint y0, jint z0,
                                    jint xs, jint ys, jint zs,
                                    unsigned zlen, unsigned char *zdata,
                                    bool update_map)
{
	static unsigned char zbuf[256*1024];
	int err;

	z_stream zstr = {
		.next_in = zdata,
		.avail_in = zlen,
		.next_out = zbuf,
		.avail_out = sizeof zbuf
	};

	if ((err = inflateInit(&zstr)) != Z_OK)
	{
		log_print("[WHAT] chunk update decompression: inflateInit: %s", zError(err));
		return false;
	}

	while (zstr.avail_in)
	{
		err = inflate(&zstr, Z_PARTIAL_FLUSH);
		if (err != Z_OK && err != Z_STREAM_END)
		{
			log_print("[WHAT] chunk update decompression: inflate: %s", zError(err));
			return false;
		}
		if (err == Z_STREAM_END)
			break;
	}

	int zbuf_len = (sizeof zbuf) - zstr.avail_out;
	inflateEnd(&zstr);

	if (zbuf_len < xs*ys*zs)
	{
		log_print("[WHAT] broken decompressed chunk length: %d != %d and < %d",
		      (int)zbuf_len, (int)(5*xs*ys*zs+1)/2, xs*ys*zs);
		return false;
	}

	if (y0 > CHUNK_YSIZE)
	{
		log_print("[WHAT] too high chunk update: %d..%d", y0, y0+ys-1);
		return false;
	}
	else if (y0 + ys > CHUNK_YSIZE)
		ys = CHUNK_YSIZE - y0;

	// FIXME: These lengths are too big, because they include the buffers after them.
	// Not really a problem, though.
	struct buffer zb = { zbuf_len, zbuf };
#ifdef FEAT_FULLCHUNK
	struct buffer zb_meta = offset_buffer(zb, xs*ys*zs);
	struct buffer zb_light_blocks = offset_buffer(zb_meta, (xs*ys*zs+1)/2);
	struct buffer zb_light_sky = offset_buffer(zb_light_blocks, (xs*ys*zs+1)/2);
#else
	struct buffer zb_meta = { 0, 0 };
	struct buffer zb_light_blocks = { 0, 0 };
	struct buffer zb_light_sky = { 0, 0 };
#endif
	return world_handle_chunk(x0, y0, z0, xs, ys, zs, zb, zb_meta, zb_light_blocks, zb_light_sky, update_map);
}

bool world_handle_chunk(jint x0, jint y0, jint z0,
                            jint xs, jint ys, jint zs,
                            struct buffer zb, struct buffer zb_meta,
                            struct buffer zb_light_blocks, struct buffer zb_light_sky,
                            bool update_map)
{
	bool set_chunk = false;
	coord_t current_chunk = COORD(0, 0);
	struct chunk *c = 0;

	if (ys < 0)
	{
		log_print("[WARN] Invalid chunk size! Probably WorldEdit; go yell at the author.");
		return false;
	}

	jint c_min_x = INT_MAX, c_min_z = INT_MAX;
	jint c_max_x = INT_MIN, c_max_z = INT_MIN;
	bool changed = false;

	for (jint x = x0; x < x0+xs; x++)
	{
		for (jint z = z0; z < z0+zs; z++)
		{
			coord_t cc = COORD(CHUNK_XMASK(x), CHUNK_ZMASK(z));

			if (!coord_equal(cc, current_chunk) || !set_chunk)
			{
				set_chunk = true;
				c = world_chunk(cc, true);
				current_chunk = cc;

				/* TODO FIXME: marks dirty always, even when loading from disk; also unoptimal */
				struct region *r = world_region(cc, false);
				if (r && r->file)
					r->file->dirty_chunks[CHUNK_ZIDX(REGION_ZOFF(cc.z))][CHUNK_XIDX(REGION_XOFF(cc.x))] = 1;
			}

			if (!changed && memcmp(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb.data, ys) != 0)
				changed = true;

			memcpy(&c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)][y0], zb.data, ys);
			ADVANCE_BUFFER(zb, ys);

#ifdef FEAT_FULLCHUNK
			size_t bytes = (ys+1)/2;

			if (bytes <= zb_meta.len)
			{
				memcpy(&c->meta[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2) + y0/2], zb_meta.data, bytes);
				ADVANCE_BUFFER(zb_meta, bytes);
			}

			if (bytes <= zb_light_blocks.len)
			{
				memcpy(&c->light_blocks[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2) + y0/2], zb_light_blocks.data, bytes);
				ADVANCE_BUFFER(zb_light_blocks, (ys+1)/2);
			}

			if (bytes <= zb_light_sky.len)
			{
				memcpy(&c->light_sky[(CHUNK_XOFF(x)*CHUNK_ZSIZE + CHUNK_ZOFF(z))*(CHUNK_YSIZE/2) + y0/2], zb_light_sky.data, bytes);
				ADVANCE_BUFFER(zb_light_sky, (ys+1)/2);
			}
#endif

			jint h = c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

			if (y0+ys >= h)
			{
				jint newh = y0 + ys;
				if (newh >= CHUNK_YSIZE)
					newh = CHUNK_YSIZE - 1;

				unsigned char *stack = c->blocks[CHUNK_XOFF(x)][CHUNK_ZOFF(z)];

				while (!stack[newh] && newh > 0)
					newh--;

				c->height[CHUNK_XOFF(x)][CHUNK_ZOFF(z)] = newh;
				changed = true;
			}

			if (cc.x < c_min_x) c_min_x = cc.x;
			if (cc.x > c_max_x) c_max_x = cc.x;
			if (cc.z < c_min_z) c_min_z = cc.z;
			if (cc.z > c_max_z) c_max_z = cc.z;
		}
	}

	if (changed && update_map)
		map_update(COORD(c_min_x, c_min_z), COORD(c_max_x, c_max_z));

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
	coord_t cc = COORD(cx, cz);
	struct chunk *c = world_chunk(cc, false);
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
		map_update(cc, cc);
}

static void handle_set_block(jint x, jint y, jint z, jint type)
{	
	coord_t cc = COORD(x, z);
	struct chunk *c = world_chunk(cc, false);
	if (!c)
		return; /* edit in an unloaded chunk */

	if (block_change(c, CHUNK_XOFF(x), y, CHUNK_ZOFF(z), type))
		map_update(cc, cc);
}

static void update_player_pos(double x, double y, double z)
{
	coord3_t new_pos = COORD3(floor(x), floor(y), floor(z));

	if (coord3_equal(player_pos, new_pos))
		return;

	player_pos = new_pos;

	map_mode->update_player_pos(map_mode->data);
	map_repaint();
}

static void update_player_dir(double yaw)
{
	int new_yaw = 0;

	yaw = fmod(yaw, 360.0);

	if (yaw < 0.0) yaw += 360.0;
	if (yaw > 360-22.5) yaw -= 360;

	while (new_yaw < 7 && yaw > 22.5)
		new_yaw++, yaw -= 45.0;

	if (new_yaw == player_yaw)
		return;

	player_yaw = new_yaw;

	map_repaint();
}

static void entity_add(jint id, enum entity_type type, jshort subtype,
                       unsigned char *name, jint x, jint y, jint z)
{
	struct entity *e = g_malloc(sizeof *e);

	e->id = id;
	e->type = type;
	e->subtype = subtype;
	e->name = name;
	e->ax = x;
	e->ay = y;
	e->az = z;

	e->pos = COORD(x/32, z/32);

	G_LOCK(entity_mutex);
	g_hash_table_replace(world_entities, &e->id, e);
	G_UNLOCK(entity_mutex);

	if (name)
	{
		log_print("[INFO] Player appeared: %s", name);
		map_repaint();
	}
}

static void entity_del(jint id)
{
	if (id == entity_vehicle)
	{
		entity_vehicle = -1;
		log_print("[INFO] Unmounted vehicle %d by destroying", id);
	}

	struct entity *e = g_hash_table_lookup(world_entities, &id);

	/* Notch sometimes lies I guess */
	if (!e) return;

	/* FIXME: This is ugly */
	char *name = e->name ? g_strdup((char *) e->name) : 0;

	G_LOCK(entity_mutex);
	g_hash_table_remove(world_entities, &id);

	if (name)
	{
		log_print("[INFO] Player disappeared: %s", name);
		g_free(name);
		map_repaint();
	}

	G_UNLOCK(entity_mutex);
}

static void entity_move(jint id, jint x, jint y, jint z, int relative)
{
	struct entity *e;

	e = g_hash_table_lookup(world_entities, &id);

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

	coord_t ep = COORD(e->ax/32, e->az/32);
	if (coord_equal(e->pos, ep))
		return;

	e->pos = ep;

	if (id == entity_vehicle)
		update_player_pos(ep.x, e->ay/32, ep.z);
	else
		map_repaint();
}

static gpointer world_thread(gpointer data)
{
	while (1)
	{
		struct directed_packet *dpacket = g_async_queue_pop(worldq);
		enum packet_origin from = dpacket->from;
		packet_t *packet = dpacket->p;
		g_free(dpacket);

		struct buffer msg;
		unsigned char *p;
		jint t;
		jlong tl;

		switch (packet->type)
		{
		case PACKET_MAP_CHUNK:
			p = &packet->bytes[packet->field_offset[6]];
			handle_compressed_chunk(packet_int(packet, 0), packet_int(packet, 1), packet_int(packet, 2),
			                        packet_int(packet, 3)+1, packet_int(packet, 4)+1, packet_int(packet, 5)+1,
			                        (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3], &p[4], true);
			break;

		case PACKET_MULTI_BLOCK_CHANGE:
			p = &packet->bytes[packet->field_offset[2]];
			t = (p[0] << 8) | p[1];
			handle_multi_set_block(packet_int(packet, 0), packet_int(packet, 1),
			                       t, p+2, p+2+t*2);
			break;

		case PACKET_BLOCK_CHANGE:
			handle_set_block(packet_int(packet, 0),
			                 packet_int(packet, 1),
			                 packet_int(packet, 2),
			                 packet_int(packet, 3));
			break;

		case PACKET_LOGIN_REQUEST:
			if (from == PACKET_FROM_SERVER)
			{
				entity_player = packet_int(packet, 0);
				world_seed = packet_long(packet, 3);
			}
			break;

		case PACKET_PLAYER_LOOK:
			update_player_dir(packet_double(packet, 0));
			break;

		case PACKET_PLAYER_POSITION_AND_LOOK:
			update_player_dir(packet_double(packet, 4));

			/* fall-thru to PACKET_PLAYER_POSITION */

		case PACKET_PLAYER_POSITION:
			if (entity_vehicle < 0)
				update_player_pos(packet_double(packet, 0),
				                  packet_double(packet, 1),
				                  packet_double(packet, 3));

			if (from == PACKET_FROM_SERVER && !spawn_known)
			{
				spawn_known = true;
				spawn_x = packet_double(packet, 0);
				spawn_y = packet_double(packet, 1);
				spawn_z = packet_double(packet, 3);
			}

			break;


		case PACKET_NAMED_ENTITY_SPAWN:
			entity_add(packet_int(packet, 0),
			           ENTITY_PLAYER,
			           0,
			           packet_string(packet, 1).data,
			           packet_int(packet, 2),
			           packet_int(packet, 3),
			           packet_int(packet, 4));
			break;

		case PACKET_PICKUP_SPAWN:
			entity_add(packet_int(packet, 0),
			           ENTITY_PICKUP,
			           packet_int(packet, 1),
			           0,
			           packet_int(packet, 4),
			           packet_int(packet, 5),
			           packet_int(packet, 6));
			break;

		case PACKET_MOB_SPAWN:
			entity_add(packet_int(packet, 0),
			           ENTITY_MOB,
			           packet_int(packet, 1),
			           0,
			           packet_int(packet, 2),
			           packet_int(packet, 3),
			           packet_int(packet, 4));
			break;

		case PACKET_DESTROY_ENTITY:
			entity_del(packet_int(packet, 0));
			break;

		case PACKET_ENTITY_RELATIVE_MOVE:
		case PACKET_ENTITY_LOOK_AND_RELATIVE_MOVE:
			entity_move(packet_int(packet, 0),
			            packet_int(packet, 1),
			            packet_int(packet, 2),
			            packet_int(packet, 3),
			            1);
			break;

		case PACKET_ENTITY_TELEPORT:
			entity_move(packet_int(packet, 0),
			            packet_int(packet, 1),
			            packet_int(packet, 2),
			            packet_int(packet, 3),
			            0);
			break;

		case PACKET_ATTACH_ENTITY:
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

		case PACKET_TIME_UPDATE:
			tl = packet_long(packet, 0);
			tl %= 24000;
			world_time = tl;
			map_mode->update_time(map_mode->data);
			break;

		case PACKET_UPDATE_HEALTH:
			player_health = packet_int(packet, 0);
			break;

		case PACKET_CHAT_MESSAGE:
			msg = packet_string(packet, 0);
			if (msg.len >= 3 && msg.data[0] == '/' && msg.data[1] == '/')
			{
				struct buffer cmd = offset_buffer(msg, 2);
				cmd_parse(cmd);
			}
			g_free(msg.data);
			break;
		}

		packet_free(packet);
	}

	return NULL;
}

/* world file IO routines */

void world_regfile_sync_all(void)
{
	/* FIXME testing code */
	GHashTableIter region_iter;
	struct region *region;
	g_hash_table_iter_init(&region_iter, region_table);
	while (g_hash_table_iter_next(&region_iter, NULL, (gpointer *)&region))
	{
		world_regfile_sync(region);
	}
}

#define SECTOR_SIZE 4096

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

static struct buffer compress_chunk(struct chunk *c)
{
	/* dump the chunk data into compressed NBT */

	struct nbt_tag *data = nbt_new_struct("Level");

	nbt_struct_add(data, nbt_new_blob("Blocks", NBT_TAG_BLOB, c->blocks, CHUNK_NBLOCKS));
#ifdef FEAT_FULLCHUNK
	nbt_struct_add(data, nbt_new_blob("Data", NBT_TAG_BLOB, c->meta, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("BlockLight", NBT_TAG_BLOB, c->light_blocks, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("SkyLight", NBT_TAG_BLOB, c->light_sky, CHUNK_NBLOCKS/2));
	nbt_struct_add(data, nbt_new_blob("HeightMap", NBT_TAG_BLOB, c->height, CHUNK_XSIZE*CHUNK_ZSIZE)); /* TODO FIXME: indexing X/Z */
#endif /* FEAT_FULLCHUNK */

	/* TODO: Entities, TileEntities */

	nbt_struct_add(data, nbt_new_long("LastUpdate", 0));

	nbt_struct_add(data, nbt_new_int("xPos", NBT_TAG_INT, c->key.x));
	nbt_struct_add(data, nbt_new_int("zPos", NBT_TAG_INT, c->key.z));

	nbt_struct_add(data, nbt_new_int("TerrainPopulated", NBT_TAG_BYTE, 1));

	struct buffer buf = nbt_compress(data);
	nbt_free(data);
	return buf;
}

static void ensure_regfile(struct region *region)
{
	if (region_path && !region->file)
	{
		/* not loaded from disk, so assume new file */
		/* open/create, and flag all existing chunks in region as dirty */

		char file_x_buf[16], file_z_buf[16];
		char *file_x = base36_encode(region->key.x, file_x_buf, sizeof file_x_buf);
		char *file_z = base36_encode(region->key.z, file_z_buf, sizeof file_z_buf);
		char *file_path = g_strdup_printf("%s/r.%s.%s.mcr", region_path, file_x, file_z);

		region->file = world_regfile_open(file_path);

		for (jint cz = 0; cz < REGION_SIZE; cz++)
		{
			for (jint cx = 0; cx < REGION_SIZE; cx++)
			{
				/* NOTE: region->chunks in [cx][cz] order,
				   while file->dirty_chunks the opposite (in-disk-file) order */
				if (region->chunks[cx][cz])
					region->file->dirty_chunks[cz][cx] = 1;
			}
		}
	}
}

struct region_file *world_regfile_open(const char *path)
{
	/* open and/or create a new region file */
	log_print("world_regfile_open: %s", path);

	struct region_file *file = g_malloc0(sizeof *file);

	file->fd = open(path, O_RDWR);

	if (file->fd == -1)
	{
		if (errno != ENOENT)
			dief("unable to read region file: %s: %s", path, g_strerror(errno));

		/* the file does not seem to exist, so make a new empty one */

		file->fd = open(path, O_RDWR|O_CREAT|O_EXCL, 0666);
		if (file->fd == -1)
			dief("unable to create region file: %s: %s", path, g_strerror(errno));

		if (ftruncate(file->fd, 2*SECTOR_SIZE) == -1)
			dief("unable to prepare empty region file: %s: %s", path, g_strerror(errno));

		file->nsect = 2;
		file->sect_bitmap = g_byte_array_new();
		g_byte_array_set_size(file->sect_bitmap, 1);
		file->sect_bitmap->data[0] = 0x03; /* header sectors always taken */
	}
	else
	{
		/* file already existed, so scan index + length */

		uint8_t header_loc[REGION_SIZE][REGION_SIZE][4];
		uint8_t header_tstamp[REGION_SIZE][REGION_SIZE][4];

		if (read(file->fd, header_loc, sizeof header_loc) != sizeof header_loc)
			dief("unable to read region file header (loc): %s: %s", path, g_strerror(errno));
		if (read(file->fd, header_tstamp, sizeof header_tstamp) != sizeof header_tstamp)
			dief("unable to read region file header (tstamp): %s: %s", path, g_strerror(errno));

		off_t len = lseek(file->fd, 0, SEEK_END);
		if (len == (off_t)-1)
			dief("unable to find size of region file: %s: %s", path, g_strerror(errno));

		if (len < 2*SECTOR_SIZE)
			dief("corrupted region file: %s: truncated header", path);
		if (len % SECTOR_SIZE != 0)
		{
			/* not full multiple of sector size, round to avoid problems */
			len += SECTOR_SIZE - (len%SECTOR_SIZE);
			if (ftruncate(file->fd, len) == -1)
				dief("unable to round region file to sector size: %s: %s", path, g_strerror(errno));
		}

		file->nsect = len / SECTOR_SIZE;

		file->sect_bitmap = g_byte_array_sized_new((file->nsect + 7) / 8);
		g_byte_array_set_size(file->sect_bitmap, (file->nsect + 7) / 8);
		file->sect_bitmap->data[0] = 0x03;

		/* parse the header and initialize the data structures */

		for (jint z = 0; z < REGION_SIZE; z++)
		{
			for (jint x = 0; x < REGION_SIZE; x++)
			{
				file->offsets[z][x] = header_loc[z][x][0] << 16 | header_loc[z][x][1] << 8 | header_loc[z][x][2];
				file->sects[z][x] = header_loc[z][x][3];
				file->tstamps[z][x] = jint_read(header_tstamp[z][x]);

				if (!file->offsets[z][x] || !file->sects[z][x])
					continue; /* non-existing block, does not use sectors */
				if (file->offsets[z][x] + file->sects[z][x] > file->nsect)
					dief("corrupted region file: %s: chunk sectors beyond end of file", path);

				for (unsigned s = file->offsets[z][x], sc = 0; sc < file->sects[z][x]; s++, sc++)
					file->sect_bitmap->data[s/8] |= 1 << (s%8);
			}
		}
	}

	/* create shared memory mapping for file contents */

	void *addr;
	file->contents_map = make_mmap(file->fd, file->nsect*SECTOR_SIZE, &addr);
	file->contents = addr;

	if (!file->contents)
		dief("unable to map region file to memory: %s: %s", path, g_strerror(errno));

	return file;
}

void world_regfile_sync(struct region *region)
{
	ensure_regfile(region);
	struct region_file *file = region->file;

	/* for each dirty chunk, serialize and try to put it into file */

	unsigned old_nsect = file->nsect; /* used to notice size changes */

	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (!file->dirty_chunks[cz][cx] || !region->chunks[cx][cz])
				continue; /* not dirty */

			struct buffer data = compress_chunk(region->chunks[cx][cz]);

			/* write the data portion */

			unsigned new_sects = (data.len + 5 + SECTOR_SIZE - 1) / SECTOR_SIZE;

			if (new_sects <= file->sects[cz][cx])
			{
				/* fits in old slot; insert there */
				unsigned char *bytes = &file->contents[file->offsets[cz][cx] * SECTOR_SIZE];
				jint_write(bytes, data.len);
				bytes[4] = 0x02; /* compression type: zlib */
				memcpy(bytes + 5, data.data, data.len);
			}
			else
			{
				/* append to file */
				uint8_t hdr[5];
				jint_write(hdr, data.len);
				hdr[4] = 0x02;
				if (lseek(file->fd, 0, SEEK_END) == -1 ||
				    write(file->fd, hdr, 5) != 5 ||
				    write(file->fd, data.data, data.len) != data.len ||
				    ftruncate(file->fd, (file->nsect + new_sects)*SECTOR_SIZE) == -1)
					dief("IO error when appending to region file: %s", g_strerror(errno));
				/* update sector bitmap array size */
				g_byte_array_set_size(file->sect_bitmap, (file->nsect + new_sects + 7) / 8);
			}

			/* alter the necessary header and other fields */

			if (new_sects < file->sects[cz][cx])
			{
				for (unsigned s = file->offsets[cz][cx] + new_sects, sc = new_sects;
				     sc < file->sects[cz][cx];
				     s++, sc++)
					file->sect_bitmap->data[s/8] &= ~(1 << (s%8));
				file->sects[cz][cx] = new_sects;
				file->contents[(cz*REGION_SIZE+cx)*4+3] = new_sects;
			}
			else if (new_sects > file->sects[cz][cx])
			{
				for (unsigned s = file->offsets[cz][cx], sc = 0; sc < file->sects[cz][cx]; s++, sc++)
					file->sect_bitmap->data[s/8] &= ~(1 << (s%8));
				file->offsets[cz][cx] = file->nsect;
				file->sects[cz][cx] = new_sects;
				unsigned char *bytes = &file->contents[(cz*REGION_SIZE+cx)*4];
				bytes[0] = file->nsect >> 16;
				bytes[1] = file->nsect >> 8;
				bytes[2] = file->nsect;
				bytes[3] = new_sects;
				file->nsect += new_sects;
			}
		}
	}

	/* redo the file mapping if we have had to add new chunks;
	   otherwise just tell the system the file has changed */

	if (file->nsect != old_nsect)
	{
		void *new_addr;
		file->contents_map = resize_mmap(file->contents_map, file->contents, file->fd,
		                                 old_nsect*SECTOR_SIZE, file->nsect*SECTOR_SIZE, &new_addr);
		if (!new_addr)
			dief("unable to resize region file memory mapping: %s", g_strerror(errno));
		file->contents = new_addr;
	}
	else
		sync_mmap(file->contents, file->nsect*SECTOR_SIZE);
}

void world_regfile_load(struct region *region)
{
	ensure_regfile(region);
	struct region_file *file = region->file;

	for (jint cz = 0; cz < REGION_SIZE; cz++)
	{
		for (jint cx = 0; cx < REGION_SIZE; cx++)
		{
			if (!file->offsets[cz][cx] || !file->sects[cz][cx])
				continue; /* chunk not in file */

			unsigned char *bytes = &file->contents[file->offsets[cz][cx] * SECTOR_SIZE];

			if (bytes[4] != 0x02)
				dief("unknown compression type in region file: %d", bytes[4]);

			jint len = jint_read(bytes);
			if (len > file->sects[cz][cx] * SECTOR_SIZE)
				die("compressed length of chunk larger than physically possible");

			struct buffer buf = { .data = bytes+5, .len = len };
			struct nbt_tag *chunk = nbt_uncompress(buf);
			struct buffer zb = nbt_blob(nbt_struct_field(chunk, "Blocks"));
#ifdef FEAT_FULLCHUNK
			struct buffer zb_meta = nbt_blob(nbt_struct_field(chunk, "Data"));
			struct buffer zb_light_blocks = nbt_blob(nbt_struct_field(chunk, "BlockLight"));
			struct buffer zb_light_sky = nbt_blob(nbt_struct_field(chunk, "SkyLight"));
#else /* !FEAT_FULLCHUNK */
			struct buffer zb_meta = { 0 };
			struct buffer zb_light_blocks = { 0 };
			struct buffer zb_light_sky = { 0 };
#endif

			world_handle_chunk((region->key.x*REGION_SIZE + cx)*CHUNK_XSIZE,
			                   0,
			                   (region->key.z*REGION_SIZE + cz)*CHUNK_ZSIZE,
			                   CHUNK_XSIZE, CHUNK_YSIZE, CHUNK_ZSIZE,
			                   zb, zb_meta, zb_light_blocks, zb_light_sky, true);

			nbt_free(chunk);
		}
	}
}
