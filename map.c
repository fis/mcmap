#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "config.h"
#include "types.h"
#include "platform.h"
#include "block.h"
#include "protocol.h"
#include "proxy.h"
#include "world.h"
#include "map.h"

/* color maps */

rgba_t block_colors[256];

// TODO: Move this out, make it configurable
rgba_t special_colors[COLOR_MAX_SPECIAL] = {
	[COLOR_PLAYER] = {255, 0, 255, 255},
	[COLOR_MOB] = {0, 0, 255, 255},
	[COLOR_PICKUP] = {0, 255, 0, 255},
	[COLOR_UNLOADED] = {16, 16, 16, 255},
};

/* map graphics code */

GHashTable *regions = 0;
TTF_Font *map_font = 0;
SDL_PixelFormat *screen_fmt = 0;
bool map_focused = true;
jint map_w = 0, map_h = 0;
static unsigned map_rshift, map_gshift, map_bshift;

static int map_base_scale = 1;
int map_scale = 1;

struct map_mode *map_mode = 0;
struct map_mode *map_modes[256];

G_LOCK_DEFINE_STATIC(map_mutex);

inline uint32_t pack_rgb(rgba_t rgba)
{
	return (rgba.r << map_rshift) |
	       (rgba.g << map_gshift) |
	       (rgba.b << map_bshift);
}

static void map_destroy_region(gpointer gp);

void map_init(SDL_Surface *screen)
{
	screen_fmt = screen->format;
	map_rshift = screen_fmt->Rshift;
	map_gshift = screen_fmt->Gshift;
	map_bshift = screen_fmt->Bshift;
	regions = g_hash_table_new_full(coord_glib_hash, coord_glib_equal, 0, map_destroy_region);

	/* initialize map modes */
	map_modes['1'] = map_init_surface_mode();
	map_modes['2'] = map_init_cross_mode();
	map_modes['4'] = map_init_topo_mode();
	map_mode = map_modes['1'];
}

rgba_t map_water_color(struct chunk *c, rgba_t rgba, jint bx, jint bz, jint y)
{
	while (--y >= 0 && IS_WATER(c->blocks[bx][bz][y]))
		TRANSFORM_RGB(x*7/8);
	return rgba;
}

bool map_zoom(int dscale)
{
	int bs = map_base_scale + dscale;

	if (bs < 1)
		return false;

	int s;

	if (bs > 5)
		s = (bs - 3) * (bs - 3);
	else
		s = bs;

	if (s > 256)
		s = 256;

	if (s != map_scale)
	{
		map_base_scale = bs;
		map_scale = s;
		return true;
	}

	return false;
}

static struct map_region *map_create_region(coord_t rc)
{
	struct map_region *region = g_new(struct map_region, 1);

	region->key = COORD(REGION_XMASK(rc.x), REGION_ZMASK(rc.z));
	region->surf = 0;

	g_hash_table_insert(regions, &region->key, region);

	region->dirty_flag = 0;
	memset(region->dirty_chunk, 0, sizeof region->dirty_chunk);

	return region;
}

static void map_destroy_region(gpointer rp)
{
	struct map_region *region = rp;
	if (region->surf)
		SDL_FreeSurface(region->surf);
	g_free(rp);
}

struct map_region *map_get_region(coord_t cc, bool gen)
{
	coord_t rc = COORD(REGION_XMASK(cc.x), REGION_ZMASK(cc.z));
	struct map_region *region = g_hash_table_lookup(regions, &rc);
	return region ? region : (gen ? map_create_region(rc) : 0);
}

inline void map_repaint(void)
{
	SDL_Event e = { .type = MCMAP_EVENT_REPAINT };
	SDL_PushEvent(&e);
}

static void map_update_chunk(coord_t cc)
{
	struct chunk *c = world_chunk(cc, false);

	if (!c)
		return;

	struct map_region *region = map_get_region(cc, true);
	region->dirty_flag = 1;
	BITSET_SET(region->dirty_chunk, CHUNK_ZIDX(REGION_ZOFF(cc.z))*REGION_SIZE + CHUNK_XIDX(REGION_XOFF(cc.x)));
}

static void map_update_region(coord_t cc)
{
	struct region *r = world_region(cc, false);

	if (!r)
		return;

	struct map_region *region = map_get_region(cc, true);
	region->dirty_flag = 1;
	for (jint cz = 0; cz < REGION_SIZE; cz++)
		for (jint cx = 0; cx < REGION_SIZE; cx++)
			if (r->chunks[cx][cz])
				BITSET_SET(region->dirty_chunk, cz*REGION_SIZE+cx);
}

void map_update(coord_t c1, coord_t c2)
{
	G_LOCK(map_mutex);

	for (jint cz = c1.z; cz <= c2.z; cz += CHUNK_ZSIZE)
		for (jint cx = c1.x; cx <= c2.x; cx += CHUNK_XSIZE)
			map_update_chunk(COORD(cx, cz));

	G_UNLOCK(map_mutex);

	map_repaint();
}

void map_update_all(void)
{
	GHashTableIter region_iter;
	struct map_region *region;

	G_LOCK(map_mutex);

	g_hash_table_iter_init(&region_iter, regions);

	while (g_hash_table_iter_next(&region_iter, NULL, (gpointer *) &region))
	{
		map_update_region(region->key);
	}

	G_UNLOCK(map_mutex);
}

void map_set_mode(struct map_mode *mode)
{
	map_mode = mode;
	map_mode->update_player_pos(map_mode->data);
	map_update_all();
	map_mode_changed();
}

void map_mode_changed(void)
{
	GPtrArray *attribs = g_ptr_array_new();
	char *name = map_mode->describe(map_mode->data, attribs);
	if (attribs->len)
	{
		g_ptr_array_add(attribs, NULL);
		tell("MODE: %s (%s)", name, g_strjoinv(", ", (char **) attribs->pdata));
	}
	else
		tell("MODE: %s", name);
	g_ptr_array_free(attribs, true);
}

/* screen-drawing related code */

inline void map_blit_scaled(SDL_Surface *dest, SDL_Surface *src, int sx, int sy, int sw, int sh, int scale)
{
	for (int py = 0; py < sh; py++)
	{
		int y0 = sy + py*scale;
		for (int y = y0; y < y0+scale && y < map_h; y++)
		{
			if (y < 0) continue;

			for (int px = 0; px < sw; px++)
			{
				int x0 = sx + px*scale;
				for (int x = x0; x < x0+scale && x < map_w; x++)
				{
					if (x < 0) continue;

					void *s = (unsigned char *)dest->pixels + y*dest->pitch + 4*x;
					void *m = (unsigned char *)src->pixels + py*src->pitch + 4*px;
					*(uint32_t *)s = *(uint32_t *)m;
				}
			}
		}
	}
}

static void map_draw_entity_marker(void *idp, void *ep, void *userdata)
{
	map_mode->draw_entity(map_mode->data, (SDL_Surface *) userdata, (struct entity *) ep);
}

static void map_draw_status_bar(SDL_Surface *screen)
{
	SDL_Rect r = { .x = 0, .y = screen->h - 24, .w = screen->w, .h = 24 };
	SDL_FillRect(screen, &r, 0);

	SDL_Color white = {255, 255, 255};
	SDL_Color black = {0, 0, 0};

	coord3_t hcc;

	if (map_focused)
	{
		int mx, my;
		SDL_GetMouseState(&mx, &my);
		hcc = map_mode->s2w(map_mode->data, mx, my);
		struct chunk *hc = world_chunk(coord3_xz(hcc), false);
		if (!hc) goto no_block_info;
		jint hcx = CHUNK_XOFF(hcc.x);
		jint hcz = CHUNK_ZOFF(hcc.z);
		unsigned char block = hc->blocks[hcx][hcz][hcc.y];

		char *left_text;
		if (IS_WATER(block))
		{
			int depth = 1;
			jint h = hcc.y;
			while (--h >= 0 && IS_WATER(hc->blocks[hcx][hcz][h]))
				depth++;
			left_text = g_strdup_printf("water (%d deep)", depth);
		}
		else
			left_text = g_strdup_printf("%s", block_info[block].name);

		SDL_Surface *left_surface = TTF_RenderText_Shaded(map_font, left_text, white, black);
		SDL_Rect left_src = { .x = 0, .y = 0, .w = left_surface->w, .h = left_surface->h };
		SDL_Rect left_dst = { .x = 4, .y = screen->h - 22, .w = left_src.w, .h = left_src.h };
		SDL_BlitSurface(left_surface, &left_src, screen, &left_dst);
		g_free(left_text);
	}
	else
	{
		hcc = player_pos;
	}

	char *right_text = g_strdup_printf("x: %-5d z: %-5d y: %-3d", hcc.x, hcc.z, hcc.y);
	SDL_Surface *right_surface = TTF_RenderText_Shaded(map_font, right_text, white, black);
	SDL_Rect right_src = { .x = 0, .y = 0, .w = right_surface->w, .h = right_surface->h };
	int right_offset_width;
	TTF_SizeText(map_font, "x: 00000, z: 00000, y: 000", &right_offset_width, NULL);
	SDL_Rect right_dst = { .x = screen->w - 4 - right_offset_width, .y = screen->h - 22, .w = right_src.w, .h = right_src.h };
	SDL_BlitSurface(right_surface, &right_src, screen, &right_dst);
	g_free(right_text);

	/* update screen buffers */

no_block_info:
	SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
}

void map_draw(SDL_Surface *screen)
{
	/* clear the window */

	SDL_Rect rect_screen = { .x = 0, .y = 0, .w = screen->w, .h = screen->h };
	SDL_FillRect(screen, &rect_screen, pack_rgb(ignore_alpha(special_colors[COLOR_UNLOADED])));

	/* draw the map */

	map_mode->draw_map(map_mode->data, screen);

	/* player indicators and such */

	map_mode->draw_player(map_mode->data, screen);

	G_LOCK(entity_mutex);
	g_hash_table_foreach(world_entities, map_draw_entity_marker, screen);
	G_UNLOCK(entity_mutex);

	/* the status bar */

	map_draw_status_bar(screen);
}
