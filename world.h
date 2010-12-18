#ifndef MCMAP_WORLD_H
#define MCMAP_WORLD_H 1

#include <glib.h>

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

struct chunk *world_chunk(guint64 coord, int gen);

void world_entities(void (*callback)(struct entity *e, void *userdata), void *userdata);

#endif /* MCMAP_WORLD_H */
