#ifndef MCMAP_TYPES_H
#define MCMAP_TYPES_H 1

/* Java types used by the protocol */

#include <stdint.h>

typedef int8_t jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;

/* hope your system has suitable little-endian IEEE-compatible types... */
typedef float jfloat;
typedef double jdouble;

#define jshort_read(p) ((jshort)((p)[0] << 8 | (p)[1]))
#define jint_read(p) ((jint)((jint)(p)[0] << 24 | (jint)(p)[1] << 16 | (p)[2] << 8 | (p)[3]))
#define jlong_read(p)	  \
	((jlong)((jlong)(p)[0] << 56 | (jlong)(p)[1] << 48 | (jlong)(p)[2] << 40 | (jlong)(p)[3] << 32 | \
	         (jlong)(p)[4] << 24 | (jlong)(p)[5] << 16 | (p)[6] << 8 | (p)[7]))

#define jshort_write(p,v) do { (p)[0] = (v) >> 8; (p)[1] = (v); } while (0)
#define jint_write(p,v) do { (p)[0] = (v) >> 24; (p)[1] = (v) >> 16; (p)[2] = (v) >> 8; (p)[3] = (v); } while (0)
#define jlong_write(p,v) do {	  \
		(p)[0] = (v) >> 56; (p)[1] = (v) >> 48; (p)[2] = (v) >> 40; (p)[3] = (v) >> 32; \
		(p)[4] = (v) >> 24; (p)[5] = (v) >> 16; (p)[6] = (v) >> 8; (p)[7] = (v); \
	} while (0)

/* TODO: consider depending on <glib.h> here and testing for G_BYTE_ORDER; or making these functions */
#define jfloat_read(p) ({	  \
			jfloat f_; uint8_t *fp_ = (uint8_t *)&f_; \
			fp_[3] = (p)[0]; fp_[2] = (p)[1]; fp_[1] = (p)[2]; fp_[0] = (p)[3]; \
			f_; })
#define jdouble_read(p) ({	  \
			jdouble f_; uint8_t *fp_ = (uint8_t *)&f_; \
			fp_[7] = (p)[0]; fp_[6] = (p)[1]; fp_[5] = (p)[2]; fp_[4] = (p)[3]; \
			fp_[3] = (p)[4]; fp_[2] = (p)[5]; fp_[1] = (p)[6]; fp_[0] = (p)[7]; \
			f_; })

#define jfloat_write(p,v) do {	  \
		jfloat f_ = (v); uint8_t *fp_ = (uint8_t *)&f_; \
		(p)[3] = fp_[0]; (p)[2] = fp_[1]; (p)[1] = fp_[2]; (p)[0] = fp_[3]; \
	} while (0)
#define jdouble_write(p,v) do {	  \
		jdouble f_ = (v); uint8_t *fp_ = (uint8_t *)&f_; \
		(p)[7] = fp_[0]; (p)[6] = fp_[1]; (p)[5] = fp_[2]; (p)[4] = fp_[3]; \
		(p)[3] = fp_[4]; (p)[2] = fp_[5]; (p)[1] = fp_[6]; (p)[0] = fp_[7]; \
	} while (0)

/* 2d points for hash table keys */

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
