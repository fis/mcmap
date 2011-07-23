#ifndef MCMAP_BLOCK_H
#define MCMAP_BLOCK_H 1

#include "types.h"

extern struct block_info
{
	char *name;
	enum block_type
	{
		AIR,
		LIQUID,
		SOLID
	} type;
	enum block_traits
	{
		NO_TRAIT,
		WATER_TRAIT,
		LAVA_TRAIT
	} trait;
} block_info[256];

#define IS_AIR(b) (block_info[b].type == AIR)
#define IS_LIQUID(b) (block_info[b].type == LIQUID)
#define IS_SOLID(b) (block_info[b].type == SOLID)
#define IS_HOLLOW(b) (IS_AIR(b) || IS_LIQUID(b))

#define IS_WATER(b) (block_info[b].trait == WATER_TRAIT)
#define IS_LAVA(b) (block_info[b].trait == LAVA_TRAIT)

extern char *default_colors[];

#endif /* MCMAP_BLOCK_H */
