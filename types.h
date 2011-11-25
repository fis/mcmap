#ifndef MCMAP_TYPES_H
#define MCMAP_TYPES_H 1

#include <stdint.h>
#include <stdbool.h>

/* Java types used by the protocol */
typedef int8_t jbyte;
typedef uint8_t jubyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;

jshort jshort_read(unsigned char *p);
jint jint_read(unsigned char *p);
jlong jlong_read(unsigned char *p);
jfloat jfloat_read(unsigned char *p);
jdouble jdouble_read(unsigned char *p);

void jshort_write(unsigned char *p, jshort v);
void jint_write(unsigned char *p, jint v);
void jlong_write(unsigned char *p, jlong v);
void jfloat_write(unsigned char *p, jfloat v);
void jdouble_write(unsigned char *p, jdouble v);

/* 2d points */

struct coord
{
	jint x, z;
};

typedef struct coord coord_t;

#define COORD(xv, zv) ((coord_t){ .x = (xv), .z = (zv) })
#define COORD_EQUAL(a,b) ((a).x == (b).x && (a).z == (b).z)

/* colors */

struct rgba
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

typedef struct rgba rgba_t;

#define RGBA(rv, gv, bv, av) ((rgba_t){ .r = (rv), .g = (gv), .b = (bv), .a = (av) })
#define RGB(rv, gv, bv) RGBA(rv, gv, bv, 255)
#define IGNORE_ALPHA(rgba) RGB((rgba).r, (rgba).g, (rgba).b)

/* general-purpose (bytes, len) buffers */

struct buffer
{
	unsigned len;
	unsigned char *data;
};

#define OFFSET_BUFFER(buf, n) { (buf).len - (n), (buf).data + (n) }
#define ADVANCE_BUFFER(buf, n) do { (buf).len -= n; (buf).data += n; } while (0)

/* fixed-size bitsets */

/* FIXME:
   maybe byte-arrays would be better, but...
   this is at least least appropriately retro. */

#define BITSET(name,len) uint8_t name[(len)>>3]
#define BITSET_SET(set,idx) ((set)[(idx)>>3] |= 1 << ((idx) & 7))
#define BITSET_CLEAR(set,idx) ((set)[(idx)>>3] &= ~(1 << ((idx) & 7)))
#define BITSET_TEST(set,idx) ((set)[(idx)>>3] & 1 << ((idx) & 7))

#endif /* MCMAP_TYPES_H */
