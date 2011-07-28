#ifndef MCMAP_WORLD_H
#define MCMAP_WORLD_H 1

#include "config.h"
#include "types.h"

#define CHUNK_XBITS 4
#define CHUNK_ZBITS 4

#define CHUNK_XSIZE (1 << CHUNK_XBITS)
#define CHUNK_ZSIZE (1 << CHUNK_ZBITS)
#define CHUNK_YSIZE 128 /* world height */

#define CHUNK_NBLOCKS (CHUNK_XSIZE*CHUNK_YSIZE*CHUNK_ZSIZE)

#define CHUNK_XIDX(coord) ((coord) >> CHUNK_XBITS)
#define CHUNK_ZIDX(coord) ((coord) >> CHUNK_ZBITS)

#define CHUNK_XMASK(coord) ((coord) & ~(CHUNK_XSIZE-1))
#define CHUNK_ZMASK(coord) ((coord) & ~(CHUNK_ZSIZE-1))
#define CHUNK_XOFF(coord) ((coord) & (CHUNK_XSIZE-1))
#define CHUNK_ZOFF(coord) ((coord) & (CHUNK_ZSIZE-1))

#define REGION_BITS 5
#define REGION_SIZE (1 << REGION_BITS)
#define REGION_XSIZE (CHUNK_XSIZE*REGION_SIZE)
#define REGION_ZSIZE (CHUNK_ZSIZE*REGION_SIZE)

/* relies on implementation-defined arithmetic shift behaviour */
#define REGION_XIDX(coord) ((coord) >> (REGION_BITS+CHUNK_XBITS))
#define REGION_ZIDX(coord) ((coord) >> (REGION_BITS+CHUNK_ZBITS))
#define REGION_XMASK(coord) ((coord) & ~(REGION_XSIZE-1))
#define REGION_ZMASK(coord) ((coord) & ~(REGION_ZSIZE-1))
#define REGION_XOFF(coord) ((coord) & (REGION_XSIZE-1))
#define REGION_ZOFF(coord) ((coord) & (REGION_ZSIZE-1))

struct region_file;

struct region
{
	coord_t key;
	struct chunk *chunks[REGION_SIZE][REGION_SIZE];
	struct region_file *file; /* can be null when non-persistent */
};

struct chunk
{
	coord_t key;
	unsigned char blocks[CHUNK_XSIZE][CHUNK_ZSIZE][CHUNK_YSIZE];
	unsigned char height[CHUNK_XSIZE][CHUNK_ZSIZE];
#ifdef FEAT_FULLCHUNK
	unsigned char meta[CHUNK_NBLOCKS/2];
	unsigned char light_blocks[CHUNK_NBLOCKS/2];
	unsigned char light_sky[CHUNK_NBLOCKS/2];
#endif
};

struct entity
{
	jint id;
	unsigned char *name;
	coord_t pos;
	jint ax, ay, az; /* in absolute-int format */
};

extern volatile int world_running;

void world_init(const char *path);
void world_destroy(void);

gpointer world_thread(gpointer data);

struct region *world_region(coord_t cc, int gen);
struct chunk *world_chunk(coord_t cc, int gen);
unsigned char *world_stack(coord_t cc, int gen);

gboolean world_handle_chunk(jint x0, jint y0, jint z0, jint xs, jint ys, jint zs, struct buffer zb, struct buffer zb_meta, struct buffer zb_light_blocks, struct buffer zb_light_sky, gboolean update_map);

jint world_getheight(coord_t cc);

void world_entities(void (*callback)(struct entity *e, void *userdata), void *userdata);

struct region_file *world_regfile_open(const char *path);
void world_regfile_sync(struct region *region);
void world_regfile_load(struct region *region);

void world_regfile_sync_all(void); /* FIXME testing code */

int world_save(char *dir);

#endif /* MCMAP_WORLD_H */
