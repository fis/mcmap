#include <math.h>
#include <stdlib.h>

#include <glib.h>
#include <SDL.h>
#include <SDL_ttf.h>

#include "protocol.h"
#include "common.h"
#include "console.h"
#include "config.h"
#include "map.h"
#include "block.h"
#include "world.h"
#include "proxy.h"

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

double player_dx = 0.0, player_dy = 0.0, player_dz = 0.0;
coord3_t player_pos = { .x = 0, .y = 0, .z = 0 };
int player_yaw = 0;
jshort player_health = 0;

GHashTable *regions = 0;
TTF_Font *map_font = 0;
SDL_PixelFormat *screen_fmt = 0;
bool map_focused = true;
jint map_w = 0, map_h = 0;
static unsigned map_rshift, map_gshift, map_bshift;

struct map_mode *map_mode = 0;

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
	regions = g_hash_table_new_full(coord_hash, coord_equal, 0, map_destroy_region);
	map_mode = &map_mode_flat;
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

void map_update_chunk(coord_t cc)
{
	struct chunk *c = world_chunk(cc, false);

	if (!c)
		return;

	struct map_region *region = map_get_region(cc, true);
	region->dirty_flag = 1;
	BITSET_SET(region->dirty_chunk, CHUNK_ZIDX(REGION_ZOFF(cc.z))*REGION_SIZE + CHUNK_XIDX(REGION_XOFF(cc.x)));
}

void map_update_region(coord_t cc)
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

void map_update_player_pos(double x, double y, double z)
{
	coord3_t new_pos = COORD3(floor(x), floor(y), floor(z));

	if (COORD3_EQUAL(player_pos, new_pos))
		return;

	player_dx = x;
	player_dy = y;
	player_dz = z;

	player_pos = new_pos;

	// ...

	map_repaint();
}

void map_update_player_dir(double yaw)
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

/* screen-drawing related code */

inline void map_blit_scaled(SDL_Surface *dest, SDL_Surface *src, int sx, int sy, int sw, int sh)
{
	int map_scale = 1; // FIXME
	for (int py = 0; py < sh; py++)
	{
		int y0 = sy + py*map_scale;
		for (int y = y0; y < y0+map_scale && y < map_h; y++)
		{
			if (y < 0) continue;

			for (int px = 0; px < sw; px++)
			{
				int x0 = sx + px*map_scale;
				for (int x = x0; x < x0+map_scale && x < map_w; x++)
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
	map_mode->draw_entity((SDL_Surface *) userdata, (struct entity *) ep);
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
		hcc = map_mode->s2w(mx, my);
		struct chunk *hc = world_chunk(COORD3_XZ(hcc), false);
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
	SDL_FillRect(screen, &rect_screen, pack_rgb(IGNORE_ALPHA(special_colors[COLOR_UNLOADED])));

	/* draw the map */

	map_mode->draw_map(screen);

	/* player indicators and such */

	map_mode->draw_player(screen);

	G_LOCK(entity_mutex);
	g_hash_table_foreach(world_entities, map_draw_entity_marker, screen);
	G_UNLOCK(entity_mutex);

	/* the status bar */

	map_draw_status_bar(screen);
}
