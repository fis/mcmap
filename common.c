#include <unistd.h>
#include <math.h>
#include <SDL.h>
#include <glib.h>

#include "protocol.h"
#include "common.h"
#include "map.h"
#include "world.h"
#include "proxy.h"

guint coord_hash(gconstpointer key)
{
	const coord_t *c = key;
	uint32_t x = c->x, z = c->z;
	return x ^ ((z << 16) | (z >> 16));
}

gboolean coord_equal(gconstpointer a, gconstpointer b)
{
	const coord_t *ca = a, *cb = b;
	return COORD_EQUAL(*ca, *cb);
}

void teleport(int x, int z)
{
	tell("Sorry, teleportation is currently out of order!");
}

jshort jshort_read(unsigned char *p)
{
	return p[0] << 8 | p[1];
}

jint jint_read(unsigned char *p)
{
	return (jint)p[0] << 24 | (jint)p[1] << 16 | p[2] << 8 | p[3];
}

jlong jlong_read(unsigned char *p)
{
	return (jlong)p[0] << 56 | (jlong)p[1] << 48 | (jlong)p[2] << 40 | (jlong)p[3] << 32 |
	       (jlong)p[4] << 24 | (jlong)p[5] << 16 | p[6] << 8 | p[7];
}

/* TODO: eliminate dependence on byte order, IEEE */
jfloat jfloat_read(unsigned char *p)
{
	jfloat f;
	uint8_t *fp = (uint8_t *)&f;
	fp[3] = p[0];
	fp[2] = p[1];
	fp[1] = p[2];
	fp[0] = p[3];
	return f;
}

jdouble jdouble_read(unsigned char *p)
{
	jdouble f;
	uint8_t *fp = (uint8_t *)&f;
	fp[7] = p[0];
	fp[6] = p[1];
	fp[5] = p[2];
	fp[4] = p[3];
	fp[3] = p[4];
	fp[2] = p[5];
	fp[1] = p[6];
	fp[0] = p[7];
	return f;
}

void jshort_write(unsigned char *p, jshort v)
{
	p[0] = v >> 8;
	p[1] = v;
}

void jint_write(unsigned char *p, jint v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

void jlong_write(unsigned char *p, jlong v)
{
	p[0] = v >> 56;
	p[1] = v >> 48;
	p[2] = v >> 40;
	p[3] = v >> 32;
	p[4] = v >> 24;
	p[5] = v >> 16;
	p[6] = v >> 8;
	p[7] = v;
}

/* TODO: as above */

void jfloat_write(unsigned char *p, jfloat v)
{
	jfloat f = v;
	uint8_t *fp = (uint8_t *)&f;
	p[3] = fp[0];
	p[2] = fp[1];
	p[1] = fp[2];
	p[0] = fp[3];
}

void jdouble_write(unsigned char *p, jdouble v)
{
	jdouble f = v;
	uint8_t *fp = (uint8_t *)&f;
	p[7] = fp[0];
	p[6] = fp[1];
	p[5] = fp[2];
	p[4] = fp[3];
	p[3] = fp[4];
	p[2] = fp[5];
	p[1] = fp[6];
	p[0] = fp[7];
}
