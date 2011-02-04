#ifndef MCMAP_TYPES_H
#define MCMAP_TYPES_H 1

/* Java types used by the protocol */

#include <stdint.h>

typedef int8_t jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;

/* 2d points for hash table keys */

struct coord
{
	jint x, z;
};

#define COORD_EQUAL(a,b) ((a).x == (b).x && (a).z == (b).z)

#endif /* MCMAP_TYPES_H */
