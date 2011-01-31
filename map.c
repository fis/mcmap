#include <math.h>
#include <stdlib.h>
#include <SDL.h>

#include <GL/gl.h>

#include "protocol.h"
#include "common.h"
#include "config.h"
#include "map.h"
#include "world.h"

/* color maps */

enum special_color_names
{
	COLOR_PLAYER,
	COLOR_UNLOADED,
	COLOR_MAX_SPECIAL
};

#ifdef RGB
#undef RGB
#endif

#define RGB(r,g,b) (((r)<<24)|((g)<<16)|(b)<<8)

#define AIR_COLOR RGB(135, 206, 235)
static GLuint block_colors[256] = {
	[0x00] = AIR_COLOR,          /* air */
	[0x01] = RGB(180, 180, 180), /* stone */
	[0x02] = RGB(34,  180, 0),   /* grass */
	[0x03] = RGB(158, 123, 18),  /* dirt */
	[0x04] = RGB(128, 128, 128), /* cobblestone */
	[0x05] = RGB(133, 78,  0),   /* wood */
	[0x06] = RGB(0,   132, 0),   /* sapling */
	[0x07] = RGB(0,   0,   0),   /* bedrock */
	[0x08] = RGB(39,  161, 225), /* water */
	[0x09] = RGB(39,  161, 225), /* stationary water */
	[0x0a] = RGB(255, 81,  0),   /* lava */
	[0x0b] = RGB(255, 81,  0),   /* stationary lava */
	[0x0c] = RGB(245, 245, 69),  /* sand */
	[0x0d] = RGB(170, 146, 105), /* gravel */
	[0x0e] = RGB(255, 180, 0),   /* gold ore */
	[0x0f] = RGB(92,  92,  92),  /* iron ore */
	[0x10] = RGB(51,  51,  51),  /* coal ore */
	[0x11] = RGB(95,  55,  0),   /* log */
	[0x12] = RGB(0,   132, 0),   /* leaves */
	[0x14] = RGB(185, 234, 231), /* glass */
	[0x15] = RGB(65,  102, 245), /* lapis lazuli ore */
	[0x16] = RGB(65,  102, 245), /* lapis lazuli block */
	[0x18] = RGB(245, 245, 69),  /* sandstone */
	[0x23] = RGB(240, 240, 240), /* cloth */
	[0x25] = RGB(137, 180, 0),   /* yellow flower */
	[0x26] = RGB(122, 130, 0),   /* red flower */
	[0x29] = RGB(255, 180, 0),   /* gold block */
	[0x2a] = RGB(92,  92,  92),  /* iron block */
	[0x2b] = RGB(180, 180, 180), /* double step */
	[0x2c] = RGB(180, 180, 180), /* step */
	[0x2d] = RGB(160, 0,   0),   /* brick */
	[0x30] = RGB(0,   255, 0),   /* mossy cobble */
	[0x31] = RGB(61,  0,   61),  /* obsidian */
	[0x32] = RGB(255, 255, 0),   /* torch */
	[0x33] = RGB(255, 108, 0),   /* fire */
	[0x35] = RGB(133, 78,  0),   /* wooden stairs */
	[0x37] = RGB(160, 0,   0),   /* redstone wire */
	[0x38] = RGB(0,   255, 255), /* diamond ore */
	[0x39] = RGB(0,   255, 255), /* diamond block */
	[0x3c] = RGB(114, 76,  9),   /* soil */
	[0x41] = AIR_COLOR,          /* ladder */
	[0x43] = RGB(128, 128, 128), /* cobblestone stairs */
	[0x45] = AIR_COLOR,          /* lever */
	[0x49] = RGB(160, 0,   0),   /* redstone ore */
	[0x4a] = RGB(160, 0,   0),   /* redstone ore (lit) */
	[0x4b] = RGB(160, 0,   0),   /* redstone torch (off) */
	[0x4c] = RGB(160, 0,   0),   /* redstone torch (on) */
	[0x4d] = AIR_COLOR,          /* stone button */
	[0x4e] = AIR_COLOR,          /* snow layer */
	[0x4f] = RGB(211, 255, 255), /* ice */
	[0x50] = RGB(238, 255, 255), /* snow */
	[0x52] = RGB(165, 42,  42),  /* clay */
	[0x56] = RGB(246, 156, 0),   /* pumpkin */
	[0x57] = RGB(121, 17,  0),   /* netherstone */
	[0x58] = RGB(107, 43,  15),  /* slow sand */
	[0x59] = RGB(186, 157, 0),   /* lightstone */
	[0x5b] = RGB(246, 156, 0),   /* pumpkin (lit) */
};

#undef RGB

static GLdouble special_colors[COLOR_MAX_SPECIAL][3] = {
	[COLOR_PLAYER] = { 1.0, 0.0, 1.0 },
	[COLOR_UNLOADED] = { 0.0625, 0.0625, 0.0625 },
};

/* map graphics code */

#define REG_BITS 2 /* 4x4 chunks per texture */

#define REG_SIZE (1 << REG_BITS)
#define REG_IDX(x) ((x) >> REG_BITS)
#define REG_OFF(x) ((x) & (REG_SIZE-1))

#define REG_BW (REG_SIZE*CHUNK_XSIZE)
#define REG_BH (REG_SIZE*CHUNK_ZSIZE)

#define REG_REDRAW_BITMAP  0x01
#define REG_REDRAW_TEXTURE 0x02

struct region_row
{
	int x0;
	GArray *reg;
};

struct region
{
	GLuint *bitmap;
	GLuint tex;
	int flags;
};

double player_dx = 0.0, player_dy = 0.0, player_dz = 0.0;
int player_x = 0, player_y = 0, player_z = 0;
static int map_y = 0;
static int map_darken = 0;

static GArray * volatile reg = 0;
static int volatile reg_z0 = 0;
static GMutex *reg_mutex;

static int map_scale = 1;

static int map_pw, map_ph;
static GLdouble map_w, map_h;
static GLdouble map_isize = 1.0;

static enum map_mode map_mode = MAP_MODE_SURFACE;
static unsigned map_flags = 0;

static GMutex * volatile map_mutex = 0;

static int player_yaw = 0;

void map_init(SDL_Surface *screen)
{
	map_pw = screen->w;
	map_ph = screen->h;

	map_w = map_pw;
	map_h = map_ph;

	reg_mutex = g_mutex_new();

#ifdef FEAT_FULLCHUNK
	map_flags = MAP_FLAG_LIGHTS;
#endif
}

static struct region *reg_get(int cx, int cz)
{
	/* locate the correct row in the region array */

	struct region_row *row = 0;
	int rz = REG_IDX(cz) - reg_z0;

	if (!reg)
	{
		reg = g_array_new(FALSE, TRUE, sizeof(struct region_row));
		g_array_set_size(reg, 1);
		reg_z0 = rz;
		rz = 0;
	}
	else if (rz < 0)
	{
		int olen = reg->len;
		g_array_set_size(reg, olen + (-rz));
		memmove(&g_array_index(reg, struct region_row, -rz),
		        &g_array_index(reg, struct region_row, 0),
		        olen * sizeof(struct region_row));
		memset(&g_array_index(reg, struct region_row, 0), 0, (-rz) * sizeof(struct region_row));
		reg_z0 += rz;
		rz = 0;
	}
	else if (rz >= reg->len)
		g_array_set_size(reg, rz+1);

	row = &g_array_index(reg, struct region_row, rz);

	/* handle row region array creation/resize */

	int rx = REG_IDX(cx) - row->x0;

	if (!row->reg)
	{
		row->reg = g_array_new(FALSE, TRUE, sizeof(struct region));
		g_array_set_size(row->reg, 1);
		row->x0 = rx;
		rx = 0;
	}
	else if (rx < 0)
	{
		int olen = row->reg->len;
		g_array_set_size(row->reg, olen + (-rx));
		memmove(&g_array_index(row->reg, struct region, -rx),
		        &g_array_index(row->reg, struct region, 0),
		        olen * sizeof(struct region));
		memset(&g_array_index(row->reg, struct region, 0), 0, (-rx) * sizeof(struct region));
		row->x0 += rx;
		rx = 0;
	}
	else if (rx >= row->reg->len)
		g_array_set_size(row->reg, rx+1);

	return &g_array_index(row->reg, struct region, rx);
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

void map_change(int x, int z)
{
	g_mutex_lock(reg_mutex);
	reg_get(x, z)->flags |= REG_REDRAW_BITMAP;
	g_mutex_unlock(reg_mutex);
}

void map_update(int forced)
{
	g_mutex_lock(reg_mutex);

	int changes = 0;

	void draw(int x, int z, GLuint *bitmap)
	{
		for (int zo = 0; zo < REG_SIZE; zo++)
		{
			for (int xo = 0; xo < REG_SIZE; xo++)
			{
				struct coord cc = { .x = x+xo, .z = z+zo };
				struct chunk *c = world_chunk(&cc, 0);

				if (!c)
				{
					for (int bz = 0; bz < CHUNK_ZSIZE; bz++)
						memset(&bitmap[(zo*CHUNK_ZSIZE+bz)*REG_BW], 0, REG_BW*sizeof(GLuint));
					continue;
				}

				unsigned char *blocks = &c->blocks[0][0][0];
				unsigned blocks_pitch = CHUNK_YSIZE;

				if (map_mode == MAP_MODE_TOPO)
				{
					blocks = &c->height[0][0];
					blocks_pitch = 1;
				}

				unsigned blocks_xpitch = CHUNK_ZSIZE*blocks_pitch;

				for (int bz = 0; bz < CHUNK_ZSIZE; bz++)
				{
					GLuint *p = &bitmap[(zo*CHUNK_ZSIZE+bz)*REG_BW + xo*CHUNK_XSIZE];
					unsigned char *b = blocks;

					for (int bx = 0; bx < CHUNK_XSIZE; bx++)
					{
						Uint32 y = c->height[bx][bz];

						/* select basic color */

						Uint32 rgb;

						if (map_mode == MAP_MODE_TOPO)
						{
							Uint32 v = *b;
							if (v < 64)
								rgb = ((4*v) << 24) | ((4*v) << 16);
							else
								rgb = (255 << 24) | ((255-4*(v-64)) << 16);
						}
						else
						{
							if (map_mode == MAP_MODE_CROSS)
								y = map_y;

							rgb = block_colors[b[y]];
						}

						/* apply shadings and such */

#define TRANSFORM_RGB(expr)	  \
						do { \
							Uint32 x; \
							Uint32 r = (rgb >> 24) & 0xff, g = (rgb >> 16) & 0xff, b = (rgb >> 8) & 0xff; \
							x = r; r = expr; x = g; g = expr; x = b; b = expr; \
							rgb = (r << 24) | (g << 16) | (b << 8); \
						} while (0)

#ifdef FEAT_FULLCHUNK

#define LIGHT_EXP1 60800
#define LIGHT_EXP2 64000

						if (map_flags & MAP_FLAG_LIGHTS)
						{
							int ly = y+1;
							if (ly >= CHUNK_YSIZE) ly = CHUNK_YSIZE-1;

							int lv_block = c->light_blocks[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2],
								lv_day = c->light_sky[bx*(CHUNK_ZSIZE*CHUNK_YSIZE/2) + bz*(CHUNK_YSIZE/2) + ly/2];

							if (ly & 1)
								lv_block >>= 4, lv_day >>= 4;
							else
								lv_block &= 0xf, lv_day &= 0xf;

							lv_day -= map_darken;
							if (lv_day < 0) lv_day = 0;
							Uint32 block_exp = LIGHT_EXP2 - map_darken*(LIGHT_EXP2-LIGHT_EXP1)/10;

							Uint32 lf = 0x10000;

							for (int i = lv_block; i < 15; i++)
								lf = (lf*block_exp) >> 16;
							for (int i = lv_day; i < 15; i++)
								lf = (lf*LIGHT_EXP1) >> 16;

							TRANSFORM_RGB((x*lf) >> 16);
						}
#endif /* FEAT_FULLCHUNK */

						if (water(c->blocks[bx][bz][y]))
						{
							if (map_mode == MAP_MODE_TOPO)
								rgb = block_colors[0x08];

							int h = y;
							while (--h)
								if (water(c->blocks[bx][bz][h]))
									TRANSFORM_RGB(x*7/8);
								else
									break;
						}

#undef TRANSFORM_RGB

						/* update bitmap */

						*p++ = rgb;
						b += blocks_xpitch;
					}

					blocks += blocks_pitch;
				}
			}
		}
	}

	for (int row = 0; row < reg->len; row++)
	{
		struct region_row *rr = &g_array_index(reg, struct region_row, row);
		if (!rr->reg)
			continue;

		for (int col = 0; col < rr->reg->len; col++)
		{
			struct region *r = &g_array_index(rr->reg, struct region, col);

			if ((r->flags & REG_REDRAW_BITMAP) || (r->bitmap && forced))
			{
				if (!r->bitmap)
					r->bitmap = g_malloc(REG_BW * REG_BH * sizeof(GLuint));
				draw((rr->x0 + col)*REG_SIZE, (reg_z0 + row)*REG_SIZE, r->bitmap);
				r->flags &= ~REG_REDRAW_BITMAP;
				r->flags |= REG_REDRAW_TEXTURE;
				changes = 1;
			}
		}
	}

	g_mutex_unlock(reg_mutex);

	if (changes)
		map_repaint();
}

void map_update_player_pos(double x, double y, double z)
{
	int new_x = floor(x), new_y = floor(y), new_z = floor(z);

	if (new_x == player_x && new_y == player_y && new_z == player_z)
		return;

	player_dx = x;
	player_dy = y;
	player_dz = z;

	player_x = new_x;
	player_y = new_y;
	player_z = new_z;

	if (map_mode == MAP_MODE_CROSS && (map_flags & MAP_FLAG_FOLLOW_Y))
		map_update_alt(new_y, 0);

	map_repaint();
}

void map_update_player_dir(double yaw, double pitch)
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

void map_update_alt(int y, int relative)
{
	int new_y = relative ? map_y + y : y;
	if (new_y < 0) new_y = 0;
	else if (new_y >= CHUNK_YSIZE) new_y = CHUNK_YSIZE-1;

	if (new_y == map_y)
		return;
	map_y = new_y;

	if (map_mode == MAP_MODE_CROSS)
		map_update(1);
}

void map_update_time(int daytime)
{
	/* daytime: 0 at sunrise, 12000 at sunset, 24000 on next sunrise.
	 * 12000 .. 13800 is dusk, 22200 .. 24000 is dawn */

	int darken = 0;

	if (daytime > 12000)
	{
		if (daytime < 13800)
			darken = (daytime-12000)/180;
		else if (daytime > 22200)
			darken = (24000-daytime)/180;
		else
			darken = 10;
	}

	if (map_darken != darken)
	{
		map_darken = darken;
		if (map_flags & MAP_FLAG_LIGHTS)
		{
			map_update(1);
			map_repaint();
		}
	}
}

void map_setmode(enum map_mode mode, unsigned flags_on, unsigned flags_off, unsigned flags_toggle)
{
	static char *modenames[] = {
		[MAP_MODE_SURFACE] = "surface",
		[MAP_MODE_CROSS] = "cross-section",
		[MAP_MODE_TOPO] = "topographic",
	};

	enum map_mode old_mode = map_mode;
	unsigned old_flags = map_flags;

	if (mode != MAP_MODE_NOCHANGE)
		map_mode = mode;

	map_flags |= flags_on;
	map_flags &= ~flags_off;
	map_flags ^= flags_toggle;

	if (mode == MAP_MODE_CROSS && (old_mode != MAP_MODE_CROSS || (map_flags & MAP_FLAG_FOLLOW_Y)))
		map_y = player_y;

	if (map_mode != old_mode || map_flags != old_flags)
		chat("MODE: %s%s%s",
		     modenames[map_mode],
		     (mode == MAP_MODE_CROSS && map_flags & MAP_FLAG_FOLLOW_Y ? " (follow)" : ""),
		     (map_flags & MAP_FLAG_LIGHTS ? " (lights)" : ""));

#if 0
	map_update(map_min_x, map_max_x, map_min_z, map_max_z);
#endif
}

void map_setscale(int scale, int relative)
{
	int s = relative ? map_scale + scale : scale;
	if (s < 1) s = 1;

	if (s == map_scale)
		return;

	map_scale = s;

	map_w = (GLdouble)map_pw / map_scale;
	map_h = (GLdouble)map_ph / map_scale;

	map_isize = 0.8 + 1.6/(1 + exp((GLdouble)s/2.0 - 3));

	map_repaint();
}

/* screen-drawing related code */

void map_s2w(int sx, int sy, int *x, int *z)
{
	/* Pixel screen->w/2 equals left edge of block player_x.
	 * Compute offset from there, divide by scale, round toward negative. */

	int dx = sx - map_pw/2, dy = sy - map_ph/2;

	dx = dx >= 0 ? dx/map_scale : (dx-(map_scale-1))/map_scale;
	dy = dy >= 0 ? dy/map_scale : (dy-(map_scale-1))/map_scale;

	*x = player_x + dx;
	*z = player_z + dy;
}

void map_w2s(int x, int z, int *sx, int *sy)
{
	*sx = map_pw/2 + (x - player_x)*map_scale;
	*sy = map_ph/2 + (z - player_z)*map_scale;
}

static inline void map_draw_player_marker(SDL_Surface *screen)
{
	glColor3dv(special_colors[COLOR_PLAYER]);

	glPushMatrix();
	glTranslated(player_x+0.5, -player_z-0.5, 0.0);
	glRotated(player_yaw * 45.0, 0.0, 0.0, -1.0);

	glBegin(GL_TRIANGLES);
	glVertex2d(-map_isize/2, map_isize/2);
	glVertex2d(0, -map_isize/2);
	glVertex2d(map_isize/2, map_isize/2);
	glEnd();

	glPopMatrix();
}

static void map_draw_entity_marker(struct entity *e, void *userdata)
{
	glColor3dv(special_colors[COLOR_PLAYER]);

	glPushMatrix();
	glTranslated(e->x+0.5, -e->z-0.5, 0.0);

	glBegin(GL_QUADS);
	glVertex2d(-map_isize/2, -map_isize/2);
	glVertex2d( map_isize/2, -map_isize/2);
	glVertex2d( map_isize/2,  map_isize/2);
	glVertex2d(-map_isize/2,  map_isize/2);
	glEnd();

	glPopMatrix();
}

void map_draw(SDL_Surface *screen)
{
	/* clear the window */

	glClear(GL_COLOR_BUFFER_BIT);

	/* position the projection so we get the right tiles */

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(player_x - map_w/2, player_x + map_w/2,
	        -player_z - map_h/2, -player_z + map_h/2,
	        -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	/* draw the map */

	glEnable(GL_TEXTURE_2D);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	int scr_row1, scr_row2;
	int scr_col1, scr_col2;

	{
		int scr_x1, scr_z1;
		map_s2w(0, 0, &scr_x1, &scr_z1);

		scr_row1 = REG_IDX(CHUNK_ZIDX(scr_z1));
		scr_col1 = REG_IDX(CHUNK_XIDX(scr_x1));

		int scr_x2 = scr_x1 + ceil(map_w), scr_z2 = scr_z1 + ceil(map_h);

		scr_row2 = REG_IDX(CHUNK_ZIDX(scr_z2+CHUNK_ZSIZE-1)+REG_SIZE-1);
		scr_col2 = REG_IDX(CHUNK_XIDX(scr_x2+CHUNK_XSIZE-1)+REG_SIZE-1);
	}

	g_mutex_lock(reg_mutex);

	if (scr_row1 < reg_z0) scr_row1 = reg_z0;

	for (int row = scr_row1; row <= scr_row2; row++)
	{
		int rowo = row - reg_z0;
		if (!reg || rowo >= reg->len)
			break;

		struct region_row *rr = &g_array_index(reg, struct region_row, rowo);
		if (!rr->reg)
			continue;

		int col1 = scr_col1;
		if (col1 < rr->x0) col1 = rr->x0;

		for (int col = col1; col <= scr_col2; col++)
		{
			int colo = col - rr->x0;
			if (colo >= rr->reg->len)
				break;

			struct region *r = &g_array_index(rr->reg, struct region, colo);
			if (!r->bitmap)
				continue;

#if 0
			if (!r->tex || (r->flags & REG_REDRAW_TEXTURE))
			{
				if (!r->tex)
					glGenTextures(1, &r->tex);
				glBindTexture(GL_TEXTURE_2D, r->tex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
				             REG_BW, REG_BH, 0,
				             GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
				             r->bitmap);
				r->flags &= ~REG_REDRAW_TEXTURE;
			}
			else
				glBindTexture(GL_TEXTURE_2D, r->tex);
#else
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
			             REG_BW, REG_BH, 0,
			             GL_RGBA, GL_UNSIGNED_INT_8_8_8_8,
			             r->bitmap);
#endif

			glBegin(GL_QUADS);
			glTexCoord2f(0, 0);
			glVertex2d(col*REG_BW,     -row*REG_BH);
			glTexCoord2f(0, 1);
			glVertex2d(col*REG_BW,     -(row+1)*REG_BH);
			glTexCoord2f(1, 1);
			glVertex2d((col+1)*REG_BW, -(row+1)*REG_BH);
			glTexCoord2f(1, 0);
			glVertex2d((col+1)*REG_BW, -row*REG_BH);
			glEnd();
		}
	}

	g_mutex_unlock(reg_mutex);

	glDisable(GL_TEXTURE_2D);

	/* player indicators and such */

	map_draw_player_marker(screen);
	world_entities(map_draw_entity_marker, screen);

	/* update screen buffers */

	glFinish();
	SDL_GL_SwapBuffers();
}
