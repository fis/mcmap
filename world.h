#ifndef MCMAP_WORLD_H
#define MCMAP_WORLD_H 1

#include "config.h"

#define CHUNK_XBITS 4
#define CHUNK_ZBITS 4

#define CHUNK_XSIZE (1 << CHUNK_XBITS)
#define CHUNK_ZSIZE (1 << CHUNK_ZBITS)
#define CHUNK_YSIZE 128 /* world height */

#define CHUNK_NBLOCKS (CHUNK_XSIZE*CHUNK_YSIZE*CHUNK_ZSIZE)

#define CHUNK_XIDX(coord) ((coord) >> CHUNK_XBITS)
#define CHUNK_ZIDX(coord) ((coord) >> CHUNK_ZBITS)

#define CHUNK_XOFF(coord) ((coord) & (CHUNK_XSIZE-1))
#define CHUNK_ZOFF(coord) ((coord) & (CHUNK_ZSIZE-1))

struct chunk
{
	struct coord key;
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
	int id;
	unsigned char *name;
	int x, z;       /* in blocks */
	int ax, ay, az; /* in absolute-int format */
};

extern int chunk_min_x, chunk_min_z;
extern int chunk_max_x, chunk_max_z;

extern volatile int world_running;

void world_init(void);

gpointer world_thread(gpointer data);

struct chunk *world_chunk(struct coord *coord, int gen);
unsigned char *world_stack(int x, int z, int gen);

int world_getheight(int x, int z);

void world_entities(void (*callback)(struct entity *e, void *userdata), void *userdata);

int world_save(char *dir);

#endif /* MCMAP_WORLD_H */
