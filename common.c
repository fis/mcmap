#include <unistd.h>
#include <math.h>

#include <glib.h>
#include <libguile.h>
#include <SDL.h>

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

void write_buffer(int fd, struct buffer buf)
{
	if (write(fd, &buf.len, sizeof(unsigned)) != sizeof(unsigned))
		die("write_buffer failed to write length");
	if (write(fd, buf.data, buf.len) != buf.len)
		die("write_buffer failed to write data");
}

struct buffer read_buffer(int fd)
{
	struct buffer buf;
	if (read(fd, &buf.len, sizeof(unsigned)) != sizeof(unsigned))
		die("read_buffer failed to read length");
	buf.data = g_malloc(buf.len);
	if (read(fd, buf.data, buf.len) != buf.len)
		die("read_buffer failed to read data");
	return buf;
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
	return (jint)p[0] << 24 | (jint)p[1] << 16 | (jint)p[2] << 8 | p[3];
}

jlong jlong_read(unsigned char *p)
{
	return
		(jlong)p[0] << 56 | (jlong)p[1] << 48 | (jlong)p[2] << 40 | (jlong)p[3] << 32 |
		(jlong)p[4] << 24 | (jlong)p[5] << 16 | (jlong)p[6] << 8  | p[7];
}

/* About the fast, non-portable variants of these functions:
   <fizzie> Though it will break on systems where the float and int endianness differs. */

jfloat jfloat_read(unsigned char *p)
{
	jint i = jint_read(p);
	jfloat f;

#if !FEAT_PORTABLE_FLOATS
	memcpy(&f, &i, sizeof f);
#else
	jint fraction = i & 0x7fffff;
	jint exponent = (i >> 23) & 0xff;

	if (exponent == 0)
	{
		if (fraction == 0)
			f = 0.0f;
		else
			f = scalbf(fraction, -149);
	}
	else if (exponent == 255)
	{
		if (fraction == 0)
			f = INFINITY;
		else
			f = NAN;
	}
	else
	{
		f = scalbf(0x800000|fraction, exponent-150);
	}

	f = copysignf(f, (i >> 31 ? -1.0f : 1.0f));
#endif

	return f;
}

jdouble jdouble_read(unsigned char *p)
{
	jlong i = jlong_read(p);
	jdouble f;

#if !FEAT_PORTABLE_FLOATS
	memcpy(&f, &i, sizeof f);
#else
	jlong fraction = i & 0xfffffffffffff;
	jint exponent = (i >> 52) & 0x7ff;

	if (exponent == 0)
	{
		if (fraction == 0)
			f = 0.0;
		else
			f = scalb(fraction, -1074);
	}
	else if (exponent == 2047)
	{
		if (fraction == 0)
			f = INFINITY;
		else
			f = NAN;
	}
	else
	{
		f = scalb(0x10000000000000|fraction, exponent-1075);
	}

	f = copysign(f, (i >> 63 ? -1.0 : 1.0));
#endif

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

void jfloat_write(unsigned char *p, jfloat v)
{
	jint i;

#if !FEAT_PORTABLE_FLOATS
	memcpy(&i, &v, sizeof i);
#else
	jint fraction;
	jint exponent;

	if (isinf(v))
		fraction = 0, exponent = 255;
	else if (isnan(v))
		fraction = 0x400000, exponent = 255;
	else if (v == 0.0f)
		fraction = 0, exponent = 0;
	else
	{
		exponent = ilogbf(v);
		if (exponent < -127) fraction = 1, exponent = -127;
		fraction = (jint)scalbf(v, -exponent + 23 - (exponent == -127)) & 0x7fffff;
		exponent += 127;
		if (exponent > 254) fraction = 0x7fffff, exponent = 254;
	}

	i = fraction | (exponent << 23);
	if (signbit(v))
		i |= ((jint)1 << 31);
#endif

	jint_write(p, i);
}

void jdouble_write(unsigned char *p, jdouble v)
{
	jlong i;

#if !FEAT_PORTABLE_FLOATS
	memcpy(&i, &v, sizeof i);
#else
	jlong fraction;
	jint exponent;

	if (isinf(v))
		fraction = 0, exponent = 2047;
	else if (isnan(v))
		fraction = 0x8000000000000, exponent = 2047;
	else if (v == 0.0)
		fraction = 0, exponent = 0;
	else
	{
		exponent = ilogb(v);
		if (exponent < -1023) fraction = 1, exponent = -1023;
		fraction = (jlong)scalb(v, -exponent + 52 - (exponent == -1023)) & 0xfffffffffffff;
		exponent += 1023;
		if (exponent > 2046) fraction = 0xfffffffffffff, exponent = 2046;
	}

	i = fraction | ((jlong)exponent << 52);
	if (signbit(v))
		i |= ((jlong)1 << 63);
#endif

	jlong_write(p, i);
}
