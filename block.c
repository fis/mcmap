#include <string.h>

#include "block.h"

struct block_info block_info[256] = {
	[0x00] = {"air", AIR, NO_TRAIT},
	[0x01] = {"stone", SOLID, NO_TRAIT},
	[0x02] = {"grass", SOLID, NO_TRAIT},
	[0x03] = {"dirt", SOLID, NO_TRAIT},
	[0x04] = {"cobblestone", SOLID, NO_TRAIT},
	[0x05] = {"wood", SOLID, NO_TRAIT},
	[0x06] = {"sapling", SOLID, NO_TRAIT},
	[0x07] = {"bedrock", SOLID, NO_TRAIT},
	[0x08] = {"water", LIQUID, WATER_TRAIT},
	[0x09] = {"water", LIQUID, WATER_TRAIT},
	[0x0a] = {"lava", LIQUID, LAVA_TRAIT},
	[0x0b] = {"lava", LIQUID, LAVA_TRAIT},
	[0x0c] = {"sand", SOLID, NO_TRAIT},
	[0x0d] = {"gravel", SOLID, NO_TRAIT},
	[0x0e] = {"gold ore", SOLID, NO_TRAIT},
	[0x0f] = {"iron ore", SOLID, NO_TRAIT},
	[0x10] = {"coal ore", SOLID, NO_TRAIT},
	[0x11] = {"wood", SOLID, NO_TRAIT},
	[0x12] = {"leaves", SOLID, NO_TRAIT},
	[0x13] = {"sponge", SOLID, NO_TRAIT},
	[0x14] = {"glass", SOLID, NO_TRAIT},
	[0x15] = {"lapis lazuli ore", SOLID, NO_TRAIT},
	[0x16] = {"lapis lazuli block", SOLID, NO_TRAIT},
	[0x17] = {"dispenser", SOLID, NO_TRAIT},
	[0x18] = {"sandstone", SOLID, NO_TRAIT},
	[0x19] = {"note block", SOLID, NO_TRAIT},
	[0x1a] = {"bed", SOLID, NO_TRAIT},
	[0x1b] = {"powered rail", SOLID, NO_TRAIT},
	[0x1c] = {"detector rail", SOLID, NO_TRAIT},
	[0x1d] = {"sticky piston", SOLID, NO_TRAIT},
	[0x1e] = {"cobweb", SOLID, NO_TRAIT},
	[0x1f] = {"tall grass", SOLID, NO_TRAIT},
	[0x20] = {"dead shrubs", SOLID, NO_TRAIT},
	[0x21] = {"piston", SOLID, NO_TRAIT},
	[0x22] = {"piston extension", SOLID, NO_TRAIT},
	[0x23] = {"wool", SOLID, NO_TRAIT},
	[0x25] = {"dandelion", SOLID, NO_TRAIT},
	[0x26] = {"rose", SOLID, NO_TRAIT},
	[0x27] = {"brown mushroom", SOLID, NO_TRAIT},
	[0x28] = {"red mushroom", SOLID, NO_TRAIT},
	[0x29] = {"gold block", SOLID, NO_TRAIT},
	[0x2a] = {"iron block", SOLID, NO_TRAIT},
	[0x2b] = {"double slab", SOLID, NO_TRAIT},
	[0x2c] = {"slab", SOLID, NO_TRAIT},
	[0x2d] = {"brick", SOLID, NO_TRAIT},
	[0x2e] = {"tnt", SOLID, NO_TRAIT},
	[0x2f] = {"bookshelf", SOLID, NO_TRAIT},
	[0x30] = {"moss stone", SOLID, NO_TRAIT},
	[0x31] = {"obsidian", SOLID, NO_TRAIT},
	[0x32] = {"torch", SOLID, NO_TRAIT},
	[0x33] = {"fire", SOLID, NO_TRAIT},
	[0x34] = {"monster spawner", SOLID, NO_TRAIT},
	[0x35] = {"wooden stairs", SOLID, NO_TRAIT},
	[0x36] = {"chest", SOLID, NO_TRAIT},
	[0x37] = {"redstone wire", SOLID, NO_TRAIT},
	[0x38] = {"diamond ore", SOLID, NO_TRAIT},
	[0x39] = {"diamond block", SOLID, NO_TRAIT},
	[0x3a] = {"crafting table", SOLID, NO_TRAIT},
	[0x3b] = {"seeds", SOLID, NO_TRAIT},
	[0x3c] = {"farmland", SOLID, NO_TRAIT},
	[0x3d] = {"furnace", SOLID, NO_TRAIT},
	[0x3e] = {"burning furnace", SOLID, NO_TRAIT},
	[0x3f] = {"sign", SOLID, NO_TRAIT},
	[0x40] = {"wooden door", SOLID, NO_TRAIT},
	[0x41] = {"ladder", SOLID, NO_TRAIT},
	[0x42] = {"rail", SOLID, NO_TRAIT},
	[0x43] = {"cobblestone stairs", SOLID, NO_TRAIT},
	[0x44] = {"sign", SOLID, NO_TRAIT},
	[0x45] = {"lever", SOLID, NO_TRAIT},
	[0x46] = {"stone pressure plate", SOLID, NO_TRAIT},
	[0x47] = {"iron door", SOLID, NO_TRAIT},
	[0x48] = {"wooden pressure plate", SOLID, NO_TRAIT},
	[0x49] = {"redstone ore", SOLID, NO_TRAIT},
	[0x4a] = {"redstone ore", SOLID, NO_TRAIT},
	[0x4b] = {"redstone torch (off)", SOLID, NO_TRAIT},
	[0x4c] = {"redstone torch (on)", SOLID, NO_TRAIT},
	[0x4d] = {"stone button", SOLID, NO_TRAIT},
	[0x4e] = {"snow", SOLID, NO_TRAIT},
	[0x4f] = {"ice", SOLID, NO_TRAIT},
	[0x50] = {"snow block", SOLID, NO_TRAIT},
	[0x51] = {"cactus", SOLID, NO_TRAIT},
	[0x52] = {"clay", SOLID, NO_TRAIT},
	[0x53] = {"sugar cane", SOLID, NO_TRAIT},
	[0x54] = {"jukebox", SOLID, NO_TRAIT},
	[0x55] = {"fence", SOLID, NO_TRAIT},
	[0x56] = {"pumpkin", SOLID, NO_TRAIT},
	[0x57] = {"netherrack", SOLID, NO_TRAIT},
	[0x58] = {"soul sand", SOLID, NO_TRAIT},
	[0x59] = {"glowstone", SOLID, NO_TRAIT},
	[0x5a] = {"portal", SOLID, NO_TRAIT},
	[0x5b] = {"pumpkin (lit)", SOLID, NO_TRAIT},
	[0x5c] = {"cake", SOLID, NO_TRAIT},
	[0x5d] = {"redstone repeater (off)", SOLID, NO_TRAIT},
	[0x5e] = {"redstone repeater (on)", SOLID, NO_TRAIT},
	[0x5f] = {"locked chest", SOLID, NO_TRAIT},
	[0x60] = {"trapdoor", SOLID}
};

// TODO: Add player and unloaded
char *default_colors[] = {
	"air: 135 206 235",
	"stone: 180 180 180",
	"grass: 34 180 0",
	"dirt: 158 123 18",
	"cobblestone: 128 128 128",
	"wood: 133 78 0",
	"sapling: 0 132 0",
	"bedrock: 0 0 0",
	"water: 39 161 225",
	"water: 39 161 225",
	"lava: 255 81 0",
	"lava: 255 81 0",
	"sand: 245 245 69",
	"gravel: 222 190 160",
	"gold ore: 255 180 0",
	"iron ore: 92 92 92",
	"coal ore: 51 51 51",
	"wood: 95 55 0",
	"leaves: 0 132 0",
	"sponge: 0 0 0",
	"glass: 185 234 231 170",
	"lapis lazuli ore: 65 102 245",
	"lapis lazuli block: 65 102 245",
	"dispenser: 0 0 0",
	"sandstone: 245 245 69",
	"note block: 0 0 0",
	"bed: 0 0 0",
	"powered rail: 0 0 0",
	"detector rail: 0 0 0",
	"sticky piston: 0 0 0",
	"cobweb: 0 0 0",
	"tall grass: 34 180 0",
	"dead shrubs: 0 0 0",
	"piston: 0 0 0",
	"piston extension: 0 0 0",
	"wool: 240 240 240",
	"dandelion: 137 180 0",
	"rose: 122 130 0",
	"brown mushroom: 0 0 0",
	"red mushroom: 0 0 0",
	"gold block: 255 180 0",
	"iron block: 92 92 92",
	"double slab: 180 180 180",
	"slab: 180 180 180",
	"brick: 160 0 0",
	"tnt: 0 0 0",
	"bookshelf: 0 0 0",
	"moss stone: 0 255 0",
	"obsidian: 61 0 61",
	"torch: 255 255 0",
	"fire: 255 108 0",
	"monster spawner: 0 0 0",
	"wooden stairs: 133 78 0",
	"chest: 0 0 0",
	"redstone wire: 160 0 0",
	"diamond ore: 0 255 255",
	"diamond block: 0 255 255",
	"crafting table: 0 0 0",
	"seeds: 0 0 0",
	"farmland: 114 76 9",
	"furnace: 0 0 0",
	"burning furnace: 0 0 0",
	"sign: 0 0 0",
	"wooden door: 0 0 0",
	"ladder: 0 0 0 0",
	"rail: 0 0 0",
	"cobblestone stairs: 128 128 128",
	"sign: 0 0 0",
	"lever: 0 0 0 0",
	"stone pressure plate: 0 0 0",
	"iron door: 0 0 0",
	"wooden pressure plate: 0 0 0",
	"redstone ore: 160 0 0",
	"redstone ore: 160 0 0",
	"redstone torch (off): 160 0 0",
	"redstone torch (on): 160 0 0",
	"stone button: 0 0 0 0",
	"snow: 0 0 0 0",
	"ice: 211 255 255",
	"snow block: 238 255 255",
	"cactus: 0 0 0",
	"clay: 165 42 42",
	"sugar cane: 0 255 0",
	"jukebox: 0 0 0",
	"fence: 0 0 0",
	"pumpkin: 246 156 0",
	"netherrack: 121 17 0",
	"soul sand: 107 43 15",
	"glowstone: 186 157 0",
	"portal: 0 0 0",
	"pumpkin (lit): 246 156 0",
	"cake: 0 0 0",
	"redstone repeater (off): 0 0 0",
	"redstone repeater (on): 0 0 0",
	"locked chest: 0 0 0",
	"trapdoor: 0 0 0",
	0
};
