#ifndef MCMAP_TYPES_H
#define MCMAP_TYPES_H 1

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

/* points */

struct coord
{
	jint x, z;
};

struct coord3
{
	jint x, y, z;
};

typedef struct coord coord_t;
typedef struct coord3 coord3_t;

#define COORD(xv, zv) ((coord_t){ .x = (xv), .z = (zv) })
#define COORD3(xv, yv, zv) ((coord3_t){ .x = (xv), .y = (yv), .z = (zv) })

bool coord_equal(coord_t a, coord_t b);
bool coord3_equal(coord3_t a, coord3_t b);
coord_t coord3_xz(coord3_t cc);

unsigned coord_glib_hash(const void *p);
int coord_glib_equal(const void *a, const void *b);

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

rgba_t ignore_alpha(rgba_t rgba);

// FIXME: Should we transform alpha too?
#define TRANSFORM_RGB(expr) \
	do { \
		uint8_t x; \
		x = rgba.r; rgba.r = (expr); \
		x = rgba.g; rgba.g = (expr); \
		x = rgba.b; rgba.b = (expr); \
	} while (0)

/* general-purpose (bytes, len) buffers */

struct buffer
{
	size_t len;
	unsigned char *data;
};

struct buffer offset_buffer(struct buffer buf, size_t n);

#define ADVANCE_BUFFER(buf, n) do { (buf) = offset_buffer((buf), (n)); } while (0)

/* fixed-size bitsets */

/* FIXME:
   maybe byte-arrays would be better, but...
   this is at least least appropriately retro. */

#define BITSET(name,len) uint8_t name[(len)>>3]
#define BITSET_SET(set,idx) ((set)[(idx)>>3] |= 1 << ((idx) & 7))
#define BITSET_CLEAR(set,idx) ((set)[(idx)>>3] &= ~(1 << ((idx) & 7)))
#define BITSET_TEST(set,idx) ((set)[(idx)>>3] & 1 << ((idx) & 7))

#endif /* MCMAP_TYPES_H */
