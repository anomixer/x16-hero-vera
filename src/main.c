//-----------------------------------------------------------------------------
// main.c — x16-hero (H.E.R.O. sequel) ported to Apple II VERA (C / llvm-mos).
//
// Faithful port of the ACME 65c02 game: state machine, level/tilemap, player,
// creatures, collision, laser/explosive, view, UI, leaderboard.  Follows the
// TimePilot-IIvera platform pattern (polling VSYNC loop, apple2e.h VERA access,
// MLI disk streaming, PSG audio engine).
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "disk.h"
#include "input.h"
#include "audio.h"
#include "text.h"
#include "asset_table.h"

/* ------------------------------ VRAM layout ------------------------------ */
#define L1_MAP_ADDR            0x0000
#define TILES_ADDR             0x2000
#define PLAYER_SPRITES_ADDR    0x6000   /* bank 0 */
#define CREATURE_SPRITES_ADDR  0x7400   /* bank 0 */
#define NEW_CHAR_ADDR          0x9800
#define L0_MAP_ADDR            0xA000
#define PALETTE_ADDR           0xFA00
#define SPR_ADDR               0xFC00
#define SCREENWIDTH            320
#define SCREENHEIGHT           240
#define TILEWIDTH              16
#define TILEHEIGHT             16

/* Palette indices (from screen.asm). */
#define CREATURE_PALETTE_INDEX          2
#define BLACK_TILE_PALETTE_INDEX        3
#define TILE_PALETTE_INDEX              4
#define BLACK_CREATURE_PALETTE_INDEX    5

/* Sprite attribute registers: 8 bytes per sprite, base SPR_ADDR. */
#define SPR1 (SPR_ADDR + 8)
#define LASER0 (SPR_ADDR + 16)
#define LASER1 (SPR_ADDR + 24)
#define EXPLOSIVES (SPR_ADDR + 32)
#define CREATURE_ADDR_L (SPR_ADDR + 40)

/* ------------------------------ Game state ------------------------------ */
#define ST_INITSTARTSCREEN 0
#define ST_SHOWSTARTSCREEN 1
#define ST_INITMENU        2
#define ST_SHOWMENU        3
#define ST_INITGAME        4
#define ST_INITLEVEL       5
#define ST_RESUMEGAME      6
#define ST_RUNNING         7
#define ST_PAUSED          8
#define ST_KILL            9
#define ST_DEATH_CREATURE  10
#define ST_DEATH_EXPLOSION 11
#define ST_DEATH_LAVA      12
#define ST_RESTARTLEVEL    13
#define ST_LEVELCOMPLETED  14
#define ST_LEVELCOMPLETED2 15
#define ST_GAMECOMPLETED   16
#define ST_GAMECOMPLETED2  17
#define ST_GAMEOVER        18
#define ST_GAMEOVER2       19
#define ST_GAMEOVER3       20
#define ST_ENTERHIGHSCORE  21
#define ST_QUITGAME        22

/* Tile categories (level.asm). */
#define TILECAT_SPACE 0
#define TILECAT_BLOCK 1
#define TILECAT_WALL  2
#define TILECAT_DEATH 3
#define TILECAT_MINER 4

/* Tile indices. */
#define TILE_SPACE  8
#define TILE_PLAYER1 89
#define TILE_PLAYER2 90
#define TILE_FIRST_CREATURE 0
#define TILE_LAST_CREATURE  7

/* Creature types. */
#define TYPE_SPIDER 0
#define TYPE_CLAW 1
#define TYPE_ALIEN 2
#define TYPE_BAT_RIGHT 3
#define TYPE_BAT_DOWN 4
#define TYPE_PLANT 5
#define TYPE_LAMP 6
#define TYPE_MINER 7
#define CREATURE_ALIVE 1
#define MAX_SPRITE_COUNT 64
#define CREATURE_SPRITES_SIZE 128

/* Creature sprite pattern addresses (pattern_vram >> 5).  Cast the sum to
 * uint16_t: llvm-mos uses 16-bit int, so uncast arithmetic overflows. */
/* VERA sprite pattern register value for a 16-bit VRAM address in bank 0.
 * The apple2ts card uses byte1 bit7 as color_mode (not a bank bit), so all
 * sprite data must live in bank 0 and the register is just addr>>5. */
#define SPRITE_REG(addr) ((uint16_t)(((uint32_t)(addr) >> 5) & 0x7FFFu))
#define SPIDER_ADDR   SPRITE_REG((uint16_t)CREATURE_SPRITES_ADDR)
#define CLAW_ADDR     SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*8)))
#define ALIEN_ADDR    SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*16)))
#define BAT_ADDR      SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*24)))
#define PLANT_ADDR    SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*32)))
#define LAMP_ADDR     SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*40)))
#define DIE_ADDR      SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*52)))
#define MINER_ADDR    SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*64)))
#define DARK_LAMP_ADDR SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(CREATURE_SPRITES_SIZE*65)))

#define LEVEL_COUNT 10
#define LIFE_COUNT  5
#define LEVEL_LIMITED_DARK 1
#define LEVEL_MODERATE_DARK 2
#define LEVEL_REALLY_DARK  3
#define LEVEL_LIMITED_DARK_COLS 32
#define LEVEL_LIMITED_DARK_ROWS 7
#define LEVEL_MODERATE_DARK_DIST 9
#define LEVEL_REALLY_DARK_DIST 7
#define FRAME_COUNT 8
#define MOVEMENT_COUNT 64
#define CREATURE_ANIMATION_DELAY 16
#define Z_DEPTH 8
/* Sprite collision masks (collision.asm). */
#define PLAYER_COLLISION_MASK   0xD0   /* player can collide with miner, lamps, creatures */
#define CREATURE_COLLISION_MASK 0x30   /* creatures collide with player or laser */
#define LASER_COLLISION_MASK    0x20   /* laser collides with creatures only */
#define CREATURE_HEIGHT 16
#define CREATURE_WIDTH 16
#define CREATURE_DEAD 0
#define CREATURE_DYING_START 2
#define CREATURE_DYING_STOP 6
#define LIGHT_CREATURE_COLS 72
#define LIGHT_CREATURE_ROWS 72
#define CREATURE_SCREEN_X_OFFSET (SCREENWIDTH/2 - CREATURE_WIDTH/2)
#define CREATURE_SCREEN_Y_OFFSET (SCREENHEIGHT/2 - CREATURE_HEIGHT/2)
#define DEAD_DELAY 120

/* Explosive / dynamite (miscsprites.asm). */
#define EXPLOSIVE_NO             0
#define EXPLOSIVE_PLACE          1
#define EXPLOSIVE_BURN           2
#define EXPLOSIVE_START_DETONATE 3
#define EXPLOSIVE_DETONATE       4
#define EXPLOSIVE_END_DETONATE   5
#define EXPLOSIVE_START 48
#define EXPLOSIVE_STOP  51
#define EXPLOSIVE_YOFFSET        4
#define EXPLOSIVE_XOFFSET        4
#define EXPLOSIVE_SAFE_DISTANCE  36
#define EXPLOSIVE_STUBTHREAD_TIME 80
#define EXPLOSIVE_FRAMEDELAY     4
#define EXPLOSIONCOLORCOUNT      18
#define TILES_PALETTES_ADDR      (PALETTE_ADDR + 0x80)   /* tile palette index 4 (screen.asm) */
#define EXPLOSIVE_PAT_REG SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(EXPLOSIVE_START*128)))
/* Laser beam (miscsprites.asm). */
#define LASER_START 56
#define LASER_FIRE_TIME 45
#define LASER_YOFFSET 8
#define LASER_PAT_REG SPRITE_REG((uint16_t)((uint16_t)CREATURE_SPRITES_ADDR + (uint16_t)(LASER_START*128)))
/* Explosion background colors (miscsprites.asm _explosioncolors). */
static const uint16_t explosionColors[EXPLOSIONCOLORCOUNT] = {
    0x0aa0, 0x0aa8, 0x0ff0, 0x0ffa, 0x0fff, 0x0ff0, 0x0aa8, 0x0aa0, 0x0000,
    0x0aa0, 0x0aa8, 0x0ff0, 0x0ffa, 0x0fff, 0x0ff0, 0x0aa8, 0x0aa0, 0x0000,
};

/* ------------------------------ Static data ------------------------------ */
static const uint8_t tileCategory[89] = {
    1,1,1,1, 1,1,1,4, 0,0,0,0, 0,0,0,0,
    0,0,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
    1,1,1,1, 1,3,3,2, 2,2,1,1, 1,1,1,1,
    1,1,1,1, 1,1,3,0, 0,0,0,2, 2,3,0,0,
    1,1,1,1, 0,0,0,0, 0,0,1,1, 1,1,3,0,
    0,0,0,0, 0,1,1,1, 1
};

/* Level 0..10: [height,width] in VERA tilemap notation (0=32,1=64,2=128,3=256). */
static const uint8_t levelSize[22] = {
    0,1,  0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0
};

/* Level darkness: 0=light, 1..3 = dark. */
static const uint8_t levelDark[11] = {
    0, 0, 0, 0, 0, 0, 0, 0, LEVEL_REALLY_DARK, 0, LEVEL_REALLY_DARK
};

static const uint16_t levelConv[4] = { 32, 64, 128, 256 };

static const uint16_t creatureAddrTable[8] = {
    SPIDER_ADDR, CLAW_ADDR, ALIEN_ADDR, BAT_ADDR, BAT_ADDR, PLANT_ADDR, LAMP_ADDR, MINER_ADDR
};

/* Movement offset tables (creatures.asm).  Offsets are added to the SCREEN
 * position (the world anchor stays fixed), unlike a world-space update. */
#define MOVEMENT_COUNT 64
static const int16_t alienX[64] = {
     0,  2,  3,  5,  6,  8,  9, 10, 11, 12, 13, 14, 15, 15, 16, 16,
    16, 16, 16, 15, 15, 14, 13, 12, 11, 10,  9,  8,  6,  5,  3,  2,
     0, -2, -3, -5, -6, -8, -9,-10,-11,-12,-13,-14,-15,-15,-16,-16,
   -16,-16,-16,-15,-15,-14,-13,-12,-11,-10, -9, -8, -6, -5, -3, -2
};
/* Alien Y has only 16 entries defined; indices 16..63 read into the X table
 * (faithful: .alienyoffsettable is immediately followed by .alienxoffsettable). */
static const int16_t alienY[16] = { -16,-16,-16,-15,-15,-14,-13,-12,-11,-10,-9,-8,-6,-5,-3,-2 };
static const int16_t batOff[64] = {
   -30,-29,-28,-26,-24,-22,-20,-18,-16,-14,-12,-10, -8, -6, -4, -2,
     0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 29,
    30, 29, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10,  8,  6,  4,  2,
     0, -2, -4, -6, -8,-10,-12,-14,-16,-18,-20,-22,-24,-26,-28,-29
};
static int16_t alienOffsetY(uint8_t m) {
    return m < 16 ? alienY[m] : alienX[m - 16];
}

/* 8-bit LFSR (mathlib.asm GetRandomNumber, polynomial $1d). */
static uint8_t rngState = 0x12;
static uint8_t getRandom(void) {
    uint8_t a = rngState;
    if (a == 0) a = 0x1D;
    else {
        uint8_t carry = a & 0x80;
        a = (uint8_t)(a << 1);
        if (a != 0 && carry != 0) a ^= 0x1D;
    }
    rngState = a;
    return a;
}

/* ------------------------------ Global state ------------------------------ */
static uint8_t  gameStatus;
static uint8_t  level;          /* 0-10 */
static uint8_t  levelCompleted;
static uint8_t  lives;
static uint8_t  startLevel = 1;
static uint8_t  minutes, seconds, jiffies;
static uint8_t  levelStartRow, levelStartCol, levelStartDirection;
static uint16_t levelHeight, levelWidth, levelXMaxPos, levelYMaxPos;
static uint8_t  levelPow2Width;
static uint8_t  lightColsLength, lightRowsLength;
static uint8_t  isDarkLevel;
static uint8_t  darkMode;
static uint8_t  graphicPalettes[128];
static uint8_t  menumode;
static uint8_t  levelCompletedDelay;  /* LEVEL_COMPLETED_DELAY=180 */
static uint8_t  gameCompletedDelay;   /* GAME_COMPLETED_DELAY=240 */
static uint8_t  gameOverDelay;        /* GAME_OVER_DELAY=300 */
static uint8_t  deathDelay;           /* DEAD_DELAY=120 */
static uint8_t  lastLevelMinutes, lastLevelSeconds;
static uint8_t  leaderboard_start_high = 6;

static void turn_on_light(void);
static void turn_off_light(void);
static void swap_light(void);

/* Player. */
static uint16_t xpos, ypos;
static uint16_t lastXpos, lastYpos;
static uint8_t  isFlying, isFalling, isTakingOff, isMoving, isMovingLeft;
static uint8_t  flyingspeed;
static uint16_t fallingspeed;
static uint8_t  walkingspeed;
static uint8_t  laserpossible;
static uint8_t  isFlyingFlag;

/* Player physics constants + state (player.asm). */
#define MIN_FLYINGSPEED    1
#define MAX_FLYINGSPEED    2
#define FLYINGTIME         90
#define MIN_WALKINGSPEED   1
#define MAX_WALKINGSPEED   1
#define MIN_FALLINGSPEED   1
#define MAX_FALLINGSPEED   16
#define FALLINGSPEED_DELAY 8
#define GRAVITY            1
#define TILE_GROUND_LEVEL  5   /* player.asm: TILE_GROUND_LEVEL = 5 */
#define TAKEOFF_DELAY      0
static uint8_t  fallingspeedDelay;
/* Player sprite frames (playersprites.asm). */
#define PLAYER_FLYING_START 0
#define PLAYER_FLYING_STOP  5
#define PLAYER_WALKING_START 6
#define PLAYER_WALKING_STOP 11
#define PLAYER_DEAD         12
#define PLAYER_FRAME_COUNT  13
#define PLAYERWIDTH  16
#define PLAYERHEIGHT 32
#define MIN_ANIMATIONDELAY  2
#define MAX_ANIMATIONDELAY  4
static uint8_t  flyingTime;
static uint8_t  takeoffDelay;
static uint8_t  currentTileBelow;
static uint8_t  lastTileBelow;
static int16_t  collQ1x, collQ1y, collQ2x, collQ2y, collQ3x, collQ3y, collQ4x, collQ4y;
static uint8_t  playerFrame;       /* current player sprite frame (0..12) */
static uint8_t  playerAnimDelay;   /* walking animation frame counter */

/* Camera. */
static uint16_t camxpos, camypos;

/* Creatures. */
static uint8_t  creatureCount;
static uint8_t  creatureType[MAX_SPRITE_COUNT];
static uint16_t creatureY[MAX_SPRITE_COUNT];
static uint16_t creatureX[MAX_SPRITE_COUNT];
static uint8_t  creatureLife[MAX_SPRITE_COUNT];
static uint8_t  creatureFrame[MAX_SPRITE_COUNT];
static uint8_t  creatureOffsetIndex[MAX_SPRITE_COUNT];
static uint8_t  creatureLit[MAX_SPRITE_COUNT];
static uint8_t  creatureFlip[MAX_SPRITE_COUNT];   /* h-flip from tile (creatures.asm) */
static uint8_t  creatureDisarmed[MAX_SPRITE_COUNT];
static uint8_t  creatureVisible[MAX_SPRITE_COUNT];
static uint8_t  lastKillerCreature = 0xFF;
static uint8_t  invulnerableTimer = 0;
static uint16_t lastLightRow = 0xFFFF, lastLightCol = 0xFFFF;
static uint8_t  lastBarSec = 0xFF, lastBarLives = 0xFF, lastBarLevel = 0xFF;

/* Laser / explosive. */
static uint8_t  laserEnabled, laserFrame, laserTime;
static uint8_t  explosiveMode, explosiveFrame, explosiveFrameDelay;
static uint16_t explX, explY;
static uint8_t  playerBlasted;
static uint8_t  stubThreadTime;      /* countdown until detonation (miscsprites.asm) */
static uint8_t  explosionColorIndex; /* current background color during detonation */
static uint16_t backgroundColor;     /* background color value during detonation */
static uint16_t originalBgColors[2]; /* saved tile-palette colors 1,2 (4 bytes) */

/* Collision / misc. */
static uint8_t  sprcolinfo;
static uint8_t  frameCount;

/* ------------------------------ VERA helpers ------------------------------ */
static void vera_write16(uint16_t addr, uint16_t val) {
    vera_set_addr(VERA_INC_BANK1, addr);
    VERA.data0 = (uint8_t)(val & 0xFF);
    VERA.data0 = (uint8_t)(val >> 8);
}

static uint8_t vera_read(uint16_t addr) {
    vera_set_addr(0x00, addr);
    return VERA.data0;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Probe the VERA card at the configured slot (VERA_BASE). */
static int detect_vera(void) {
    VERA.control = 0x00;
    VERA.address_hi = VERA_INC_1;
    VERA.address = 0x0000;
    VERA.data0 = 0xA5;
    VERA.address = 0x0000;
    return VERA.data0 == 0xA5;
}

/* Set one sprite's attributes.  On this VERA card (apple2ts) the attribute
 * layout is: attr0 bit 2 = visible/z-depth 1, attr1 = dims | palette_offset.
 * dims 0x50 = 16x16, 0x60 = 32x16, 0 = 8x8 (proven by TimePilot-IIvera). */
static void set_sprite(uint16_t base, uint16_t patReg, uint16_t x, uint16_t y,
                       uint8_t attr0, uint8_t attr1) {
    /* patReg is the VERA sprite pattern address REGISTER value, i.e. the
     * pre-shifted address (address >> 5, bank 0 so bit 15 = 0).  This matches
     * the original: caller passes the pre-shifted address directly. */
    vera_set_addr(VERA_INC_BANK1, base);
    VERA.data0 = (uint8_t)(patReg & 0xFF);
    VERA.data0 = (uint8_t)(patReg >> 8);
    VERA.data0 = (uint8_t)(x & 0xFF);
    VERA.data0 = (uint8_t)(x >> 8);
    VERA.data0 = (uint8_t)(y & 0xFF);
    VERA.data0 = (uint8_t)(y >> 8);
    /* Preserve collision mask (bits 4-7) AND flips (bits 0-1), force z-depth
     * 2 (bit 3).  apple2ts: attr0 bit0 = H-flip, bit1 = V-flip.  The original
     * passes flip|collision_mask|8 here. */
    VERA.data0 = (attr0 & 0xF3) | 0x08;
    VERA.data0 = attr1;
}

static void hide_sprite(uint16_t base) {
    vera_set_addr(VERA_INC_BANK1, base + 6);
    VERA.data0 = 0x00;           /* z-depth 0: hidden */
}

static uint8_t joy(void) { return inputGetJoy(); }

/* ------------------------------ Screen init ------------------------------ */
static void init_screen(void) {
    VERA.control = 0x00;                 /* ADDRSEL=0, DCSEL=0 */
    VERA.display.video = 0x00;           /* disable all */
    VERA.display.hscale = 0x40;          /* 2:1 */
    VERA.display.vscale = 0x40;

    /* Layer 1: 80x30 text layer.  0x10 = text mode (T256C=0), 64 cols, 32 rows, 8x8. */
    VERA.layer1.config = 0x10;
    VERA.layer1.mapbase = 0x00;          /* map at $0000 (L1_MAP_ADDR>>9) */
    VERA.layer1.tilebase = (NEW_CHAR_ADDR >> 9);  /* charset at $9800, 8x8 */
    /* Layer 0 tile mode set up per level via set_layer0_tile_mode(). */

    /* Clear layer 1 map region ($0000-$0FFF) to spaces so layer 1 shows
     * blank instead of garbage (the colorful strip at the bottom). */
    for (uint16_t i = 0; i < 4096; i += 2) {
        vera_set_addr(VERA_INC_BANK0, i);
        VERA.data0 = 0x20;   /* char = space */
        VERA.data0 = 0x01;   /* color = white on black */
    }
}

static void set_layer0_tile_mode(void) {
    VERA.layer0.mapbase = (L0_MAP_ADDR >> 9);
    VERA.layer0.tilebase = (TILES_ADDR >> 9) | 0x03;   /* 16x16 tiles (X16: ora #%00000011) */
}

static void set_layer0_size(uint8_t rows, uint8_t cols) {
    /* rows/cols: 0=32,1=64,2=128,3=256 tiles.
     * VERA L0_CONFIG: bits 1-0 depth(10=4bpp), bits 3-2 map height,
     * bits 5-4 map width, bits 7-6 tile size (01=16x16). */
    VERA.layer0.config = 0x42 | ((rows & 3) << 2) | ((cols & 3) << 4);
}

/* ------------------------------ Resource loading ------------------------------ */
static uint32_t asset_offset(const char *key) {
    for (uint8_t i = 0; i < ASSET_COUNT; i++) {
        if (str_eq(assetTable[i].key, key)) return assetTable[i].offset;
    }
    return 0;
}

static uint32_t asset_length(const char *key) {
    for (uint8_t i = 0; i < ASSET_COUNT; i++) {
        if (str_eq(assetTable[i].key, key)) return assetTable[i].length;
    }
    return 0;
}

static void load_resources(void) {
    disk_copy_to_vram(asset_offset("TILES"), TILES_ADDR, 11648, 0);
    disk_copy_to_vram(asset_offset("SPRITE0"), PLAYER_SPRITES_ADDR, 3328, 0);
    disk_copy_to_vram(asset_offset("SPRITE1"), CREATURE_SPRITES_ADDR, 8448, 0);
    disk_copy_to_vram(asset_offset("FONT"), NEW_CHAR_ADDR, 1760, 0);
    /* Palette lives in VRAM Bank 1 ($1FA00), 16-bit part $FA00. */
    disk_copy_to_vram(asset_offset("PAL"), PALETTE_ADDR, 512, 1);
    /* Music into VRAM Bank 1, 16-bit part $0000 (full addr $10000). */
    disk_copy_to_vram(asset_offset("MUSIC"), 0x0000, asset_length("MUSIC"), 1);

    /* Backup original graphics palettes 1..4 (128 bytes) so we can dim/restore light */
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR + 0x20);
    for (uint8_t i = 0; i < 128; i++) {
        graphicPalettes[i] = VERA.data0;
    }
}

static void load_level_map(void) {
    char key[8];
    key[0] = 'M'; key[1] = 'A'; key[2] = 'P';
    if (level >= 10) {
        key[3] = '1';
        key[4] = (char)('0' + (level - 10));
        key[5] = 0;
    } else {
        key[3] = (char)('0' + level);
        key[4] = 0;
    }
    uint32_t off = asset_offset(key);
    uint32_t len = 0;
    for (uint8_t i = 0; i < ASSET_COUNT; i++)
        if (str_eq(assetTable[i].key, key)) len = assetTable[i].length;
    disk_copy_to_vram(off, L0_MAP_ADDR, len, 0);
}

/* ------------------------------ Tilemap access ------------------------------ */
static uint16_t tilemap_index(uint16_t row, uint16_t col) {
    return ((uint16_t)row << levelPow2Width) + col;
}

static uint8_t tile_at(uint16_t row, uint16_t col) {
    uint16_t idx = tilemap_index(row, col);
    return vera_read(L0_MAP_ADDR + idx * 2);
}

static void set_tile(uint16_t row, uint16_t col, uint8_t tile, uint8_t palette) {
    uint16_t idx = tilemap_index(row, col);
    vera_set_addr(VERA_INC_BANK0, L0_MAP_ADDR + idx * 2);
    VERA.data0 = tile;
    VERA.data0 = palette;
}

static uint8_t tile_category(uint8_t tile) {
    return tile < 89 ? tileCategory[tile] : TILECAT_SPACE;
}

/* ------------------------------ Level setup ------------------------------ */
static void set_level_properties(void) {
    uint8_t rows = levelSize[level*2];
    uint8_t cols = levelSize[level*2 + 1];
    levelHeight = levelConv[rows];
    levelWidth = levelConv[cols];
    levelPow2Width = cols + 5;
    set_layer0_size(rows, cols);

    levelYMaxPos = levelHeight * TILEHEIGHT - SCREENHEIGHT/2;
    levelXMaxPos = levelWidth * TILEWIDTH - SCREENWIDTH/2;

    uint8_t d = levelDark[level];
    isDarkLevel = (d != 0);
    if (d == LEVEL_LIMITED_DARK) { lightColsLength = LEVEL_LIMITED_DARK_COLS; lightRowsLength = LEVEL_LIMITED_DARK_ROWS; }
    else if (d == LEVEL_MODERATE_DARK) { lightColsLength = LEVEL_MODERATE_DARK_DIST; lightRowsLength = LEVEL_MODERATE_DARK_DIST; }
    else if (d == LEVEL_REALLY_DARK) { lightColsLength = LEVEL_REALLY_DARK_DIST; lightRowsLength = LEVEL_REALLY_DARK_DIST; }
    else { lightColsLength = 0; lightRowsLength = 0; }
}

static void blackout_level(void) {
    if (!isDarkLevel) return;
    vera_set_addr(VERA_INC_2, L0_MAP_ADDR + 1);
    uint16_t total = levelHeight * levelWidth;
    for (uint16_t i = 0; i < total; i++) {
        VERA.data0 = (uint8_t)(BLACK_TILE_PALETTE_INDEX << 4);
    }
}

/* Light tiles around the player (dark levels only, e.g. levels 8 and 10). */
static void light_up_level(void) {
    if (!isDarkLevel) return;
    if (lightRowsLength == 0 || lightColsLength == 0) return;
    uint16_t prow = ypos / TILEHEIGHT;
    uint16_t pcol = xpos / TILEWIDTH;
    if (prow == lastLightRow && pcol == lastLightCol) return;
    lastLightRow = prow;
    lastLightCol = pcol;

    uint16_t rows = lightRowsLength, cols = lightColsLength;
    int16_t startR = (int16_t)prow - (int16_t)(rows >> 1);
    int16_t startC = (int16_t)pcol - (int16_t)(cols >> 1);
    for (uint16_t r = 0; r < rows; r++) {
        int16_t rr = startR + (int16_t)r;
        if (rr < 0 || rr >= (int16_t)levelHeight) continue;
        for (uint16_t c = 0; c < cols; c++) {
            int16_t cc = startC + (int16_t)c;
            if (cc < 0 || cc >= (int16_t)levelWidth) continue;
            uint16_t idx = tilemap_index((uint16_t)rr, (uint16_t)cc);
            vera_set_addr(VERA_INC_BANK0, L0_MAP_ADDR + idx * 2 + 1);
            VERA.data0 = (uint8_t)(TILE_PALETTE_INDEX << 4);
        }
    }
}

/* Turn off room light (halve RGB of palettes 1..4, preserving lava color 15). */
static void turn_off_light(void) {
    if (darkMode) return;
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR + 0x20);
    for (uint8_t y = 0; y < 127; y++) {
        uint8_t val = graphicPalettes[y];
        uint8_t hi = (val >> 4) >> 1;
        if (hi > 0) hi--;
        uint8_t lo = (val & 0x0F) >> 1;
        if (lo > 0) lo--;
        VERA.data0 = (uint8_t)((hi << 4) | lo);
    }
    darkMode = 1;
}

/* Restore normal room light (restore palettes 1..4 from RAM backup). */
static void turn_on_light(void) {
    if (!darkMode) return;
    vera_set_addr(VERA_INC_BANK1, PALETTE_ADDR + 0x20);
    for (uint8_t y = 0; y < 128; y++) {
        VERA.data0 = graphicPalettes[y];
    }
    darkMode = 0;
}

/* Wall lamp touched/broken: switch between light and darkness. */
static void swap_light(void) {
    if (darkMode) {
        turn_on_light();
    } else {
        turn_off_light();
    }
}

static void init_level(void) {
    turn_on_light();
    levelCompleted = 0;
    lastLightRow = 0xFFFF;
    lastLightCol = 0xFFFF;
    lastBarSec = 0xFF;
    lastBarLives = 0xFF;
    lastBarLevel = 0xFF;
    load_level_map();
    set_level_properties();
    blackout_level();
}

/* ------------------------------ Creatures ------------------------------ */
static uint8_t tile_at_hi(uint16_t row, uint16_t col);

static void hide_creatures(void) {
    /* Clear the visibility byte (attr0, offset +6) of every sprite slot so
     * leftover sprites from a previous scene are never shown. */
    for (uint16_t i = 0; i < MAX_SPRITE_COUNT; i++) {
        vera_set_addr(VERA_INC_BANK1, CREATURE_ADDR_L + (uint16_t)i * 8 + 6);
        VERA.data0 = 0;
        creatureVisible[i] = 0;
    }
}

static void init_creatures(void) {
    hide_creatures();
    creatureCount = 0;
    /* Scan tilemap for creature tiles and player start tile. */
    for (uint16_t r = 0; r < levelHeight; r++) {
        for (uint16_t c = 0; c < levelWidth; c++) {
            uint8_t tile = tile_at(r, c);
            if (tile == TILE_PLAYER1 || tile == TILE_PLAYER2) {
                if (tile == TILE_PLAYER1) { levelStartRow = r; levelStartCol = c; levelStartDirection = (tile_at_hi(r,c) & 0x04) ? 1 : 0; }
                set_tile(r, c, TILE_SPACE, TILE_PALETTE_INDEX << 4);
            } else if (tile >= TILE_FIRST_CREATURE && tile <= TILE_LAST_CREATURE) {
                if (creatureCount < MAX_SPRITE_COUNT) {
                    uint8_t i = creatureCount++;
                    uint8_t type = tile;
                    uint8_t hflip = (tile_at_hi(r, c) & 0x04) ? 1 : 0;
                    int16_t x = (int16_t)c * TILEWIDTH + 8;
                    int16_t y = (int16_t)r * TILEHEIGHT + 8;
                    /* Align creature to ceiling/wall/floor per its type. */
                    switch (type) {
                    case TYPE_SPIDER: y -= 1; break;                       /* rock ceiling */
                    case TYPE_CLAW:   x += hflip ? 2 : -2; break;          /* wall side */
                    case TYPE_PLANT:  y += 1; break;                       /* rock floor */
                    case TYPE_LAMP:   x += hflip ? 1 : -1; break;          /* wall side */
                    default: break;
                    }
                    creatureType[i] = type;
                    creatureLife[i] = CREATURE_ALIVE;
                    creatureDisarmed[i] = 0;
                    creatureLit[i] = (level == 0) ? 1 : 0;
                    creatureFlip[i] = hflip;
                    creatureX[i] = (uint16_t)x;
                    creatureY[i] = (uint16_t)y;
                    creatureFrame[i] = (uint8_t)(getRandom() & (FRAME_COUNT - 1));
                    if (type == TYPE_MINER) creatureFrame[i] = 0;   /* miner never animates; single frame */
                    creatureOffsetIndex[i] = (uint8_t)(getRandom() & (MOVEMENT_COUNT - 1));
                    set_tile(r, c, TILE_SPACE, TILE_PALETTE_INDEX << 4);
                }
            }
        }
    }
}

static uint8_t tile_at_hi(uint16_t row, uint16_t col) {
    uint16_t idx = tilemap_index(row, col);
    return vera_read(L0_MAP_ADDR + idx * 2 + 1);
}

/* ------------------------------ Player ------------------------------ */
static void init_player(void) {
    xpos = (uint16_t)levelStartCol * TILEWIDTH + 8;
    ypos = (uint16_t)levelStartRow * TILEHEIGHT + 16;
    lastXpos = xpos; lastYpos = ypos;
    isFlying = 1; isFalling = 0; isTakingOff = 0; isMoving = 0; isMovingLeft = levelStartDirection;
    flyingspeed = MIN_FLYINGSPEED; fallingspeed = MIN_FALLINGSPEED; walkingspeed = MIN_WALKINGSPEED;
    fallingspeedDelay = 0;
    flyingTime = FLYINGTIME; takeoffDelay = 0;
    laserpossible = 0; currentTileBelow = 0; lastTileBelow = 0xFF;
    isFlyingFlag = 0; playerFrame = 0; playerAnimDelay = 0;
    explosiveMode = EXPLOSIVE_NO; stubThreadTime = 0; explosiveFrame = 0;
}

static void update_view(void) {
    /* Scroll tilemap to center camera on player (clamped). */
    if (xpos < SCREENWIDTH/2) { camxpos = SCREENWIDTH/2; }
    else if (xpos > levelXMaxPos) { camxpos = levelXMaxPos; }
    else camxpos = xpos;
    if (ypos < SCREENHEIGHT/2) { camypos = SCREENHEIGHT/2; }
    else if (ypos > levelYMaxPos) { camypos = levelYMaxPos; }
    else camypos = ypos;

    VERA.layer0.hscroll = camxpos - SCREENWIDTH/2;
    VERA.layer0.vscroll = camypos - SCREENHEIGHT/2;
}

/* ------------------------------ Player sprite ------------------------------ */
static void hide_player(void) {
    vera_set_addr(VERA_INC_BANK1, SPR1 + 6);
    VERA.data0 = 0x00;
}

static void update_player_sprite(void) {
    uint8_t isDead = (gameStatus == ST_DEATH_CREATURE ||
                      gameStatus == ST_DEATH_EXPLOSION ||
                      gameStatus == ST_DEATH_LAVA);

    /* If invulnerable, blink the sprite (hide every other 4 frames) */
    if (!isDead && invulnerableTimer > 0 && (invulnerableTimer & 4)) {
        hide_player();
        return;
    }

    /* Animation frame (playersprites.asm UpdatePlayerSprite / ShowDeadPlayer). */
    if (isDead) {
        playerFrame = PLAYER_DEAD;   /* 12: face forward toward camera */
    } else if (isFlying) {
        if (!isFlyingFlag) playerFrame = PLAYER_FLYING_START;      /* just took off */
        else {
            playerFrame++;
            if (playerFrame > PLAYER_FLYING_STOP) playerFrame = PLAYER_FLYING_START;
        }
    } else {
        if (isFlyingFlag) { playerFrame = PLAYER_WALKING_START; playerAnimDelay = 0; }  /* just landed */
        else if (!isMoving) playerFrame = PLAYER_WALKING_START;
        else {
            playerAnimDelay++;
            uint8_t lim = (walkingspeed == MIN_WALKINGSPEED) ? MAX_ANIMATIONDELAY : MIN_ANIMATIONDELAY;
            if (playerAnimDelay >= lim) {
                playerAnimDelay = 0;
                playerFrame++;
                if (playerFrame > PLAYER_WALKING_STOP) playerFrame = PLAYER_WALKING_START;
            }
        }
    }
    isFlyingFlag = isFlying;

    /* Pattern register: base + frame*8 (each frame is 256 bytes = 8 reg units). */
    uint16_t patReg = (uint16_t)(SPRITE_REG(PLAYER_SPRITES_ADDR) + (uint16_t)playerFrame * 8);

    /* Faithful PositionSprite: screen pos = pos - cam + (center - width/2).
     * Player is 16x32, so x offset = 160-8, y offset = 120-16. */
    int16_t sx = (int16_t)xpos - (int16_t)camxpos + SCREENWIDTH/2 - PLAYERWIDTH/2;
    int16_t sy = (int16_t)ypos - (int16_t)camypos + SCREENHEIGHT/2 - PLAYERHEIGHT/2;
    /* attr0 = collision mask + flip (bit0 = face left). When dead, face forward (flip=0). */
    uint8_t flip = isDead ? 0 : (isMovingLeft ? 1 : 0);
    set_sprite(SPR1, patReg,
               (uint16_t)(sx < 0 ? 0 : sx), (uint16_t)(sy < 0 ? 0 : sy),
               (uint8_t)(PLAYER_COLLISION_MASK | flip), 0x90 | 1);
}

static const char HEXD[] = "0123456789ABCDEF";
static void print_hex(uint8_t row, uint8_t col, uint8_t v) {
    text_putc(row, col, HEXD[v >> 4], 0x20);
    text_putc(row, col + 1, HEXD[v & 15], 0x20);
}

static void show_player(void) { update_player_sprite(); }

struct Hitbox {
    int8_t x0, x1;
    int8_t y0, y1;
};

/* Pixel-accurate bounding boxes extracted from sprite pixel data for each
 * creature type and animation frame (handles expansion and contraction). */
static const struct Hitbox creatureHitboxes[8][4] = {
    /* TYPE_SPIDER (0): spider on ceiling, extends downward (frames 0..1 contracted) */
    { { 2, 13, 0, 10 }, { 2, 13, 0, 10 }, { 2, 13, 0, 12 }, { 2, 13, 0, 15 } },
    /* TYPE_CLAW (1): claw extends/retracts (frame 3 contracted) */
    { { 2, 13, 0, 15 }, { 2, 13, 0, 14 }, { 2, 13, 0, 11 }, { 2, 13, 0, 10 } },
    /* TYPE_ALIEN (2): alien */
    { { 0, 6, 5, 10 },  { 0, 8, 5, 10 },  { 0, 10, 5, 10 }, { 0, 12, 5, 11 } },
    /* TYPE_BAT_RIGHT (3): bat */
    { { 0, 13, 5, 12 }, { 0, 11, 5, 11 }, { 0, 9, 5, 10 },  { 0, 7, 5, 10 } },
    /* TYPE_BAT_DOWN (4): bat */
    { { 0, 13, 5, 12 }, { 0, 11, 5, 11 }, { 0, 9, 5, 10 },  { 0, 7, 5, 10 } },
    /* TYPE_PLANT (5): snake head / plant expands up, retracts down (frame 3: Y=[10..15] contracted!) */
    { { 1, 13, 5, 15 }, { 1, 14, 5, 15 }, { 0, 13, 7, 15 }, { 1, 14, 10, 15 } },
    /* TYPE_LAMP (6) */
    { { 0, 12, 0, 15 }, { 0, 12, 0, 15 }, { 0, 12, 0, 15 }, { 0, 12, 0, 15 } },
    /* TYPE_MINER (7) */
    { { 0, 14, 0, 15 }, { 0, 14, 0, 15 }, { 0, 14, 0, 15 }, { 0, 14, 0, 15 } }
};

/* ------------------------------ Collision ------------------------------ */
/* Returns 0 = no collision, 1 = player killed by a creature, 2 = rescued the
 * miner (level complete).  Matches original collision semantics:
 * - Miner rescue & wall lamp have generous interaction zones.
 * - Harmful creatures (snake head, spider, claw, etc.) use exact frame-dependent
 *   hitboxes so contracted creatures allow the player to pass safely! */
static uint8_t check_player_creature(void) {
    if (invulnerableTimer > 0) return 0;

    /* Player's active body bounds (width ~9px, height ~21px). */
    int16_t px0 = (int16_t)xpos - 4;
    int16_t px1 = (int16_t)xpos + 4;
    int16_t py0 = (int16_t)ypos - 10;
    int16_t py1 = (int16_t)ypos + 10;

    for (uint8_t i = 0; i < creatureCount; i++) {
        if (creatureLife[i] == CREATURE_ALIVE) {
            /* Quick coarse bounding box: creature cannot move more than 32px from anchor */
            int16_t rdx = (int16_t)xpos - (int16_t)creatureX[i];
            if (rdx < -48 || rdx > 48) continue;
            int16_t rdy = (int16_t)ypos - (int16_t)creatureY[i];
            if (rdy < -48 || rdy > 48) continue;

            uint8_t type = creatureType[i];
            int16_t cx = (int16_t)creatureX[i];
            int16_t cy = (int16_t)creatureY[i];
            uint8_t m = creatureOffsetIndex[i];

            if (type == TYPE_ALIEN) {
                cx = (int16_t)(cx + alienX[m]);
                cy = (int16_t)(cy + alienOffsetY(m));
            } else if (type == TYPE_BAT_RIGHT) {
                cx = (int16_t)(cx + batOff[m]);
            } else if (type == TYPE_BAT_DOWN) {
                cy = (int16_t)(cy + batOff[m]);
            }

            int16_t dx = (int16_t)xpos - cx;
            int16_t dy = (int16_t)ypos - cy;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;

            /* Generous interaction box for Miner and Wall Lamp */
            if (dx < 16 && dy < 24) {
                if (type == TYPE_MINER) return 2;
                if (type == TYPE_LAMP) {
                    if (!creatureDisarmed[i]) {
                        creatureDisarmed[i] = 1;
                        swap_light();
                    }
                    continue;   /* Stepping on a lamp does not harm or stop the player */
                }
            }

            /* For monsters, test exact frame-based hitbox (handles retraction/expansion) */
            if (type != TYPE_MINER && type != TYPE_LAMP) {
                uint8_t f = creatureFrame[i] & 3;
                struct Hitbox hb = creatureHitboxes[type][f];
                int16_t cx0, cx1;
                if (creatureFlip[i]) {
                    cx0 = cx - 8 + (15 - hb.x1);
                    cx1 = cx - 8 + (15 - hb.x0);
                } else {
                    cx0 = cx - 8 + hb.x0;
                    cx1 = cx - 8 + hb.x1;
                }
                int16_t cy0 = cy - 8 + hb.y0;
                int16_t cy1 = cy - 8 + hb.y1;

                if (px0 <= cx1 && px1 >= cx0 && py0 <= cy1 && py1 >= cy0) {
                    lastKillerCreature = i;
                    return 1;       /* Monster kills player */
                }
            }
        }
    }
    return 0;
}

static void move_player_back(void) {
    xpos = lastXpos;
    ypos = lastYpos;
    if (isFalling || isFlying) {
        isFalling = 0;
        isFlying = 1;
        flyingTime = 120;
    }
}

static uint8_t check_tile(uint16_t px, uint16_t py);

static uint8_t check_laser_creature(void) {
    if (laserpossible == 0) return 0;
    /* laserpossible == 1: 1 clear tile in front -> reach 26px (within confirmed clear tile).
     * laserpossible == 2: 2 clear tiles in front -> reach 46px (within confirmed clear corridor).
     * Since laserpossible already verified these tiles are free of walls, no raycast is needed. */
    int16_t maxReach = (laserpossible == 1) ? 26 : 46;

    for (uint8_t i = 0; i < creatureCount; i++) {
        if (creatureLife[i] == CREATURE_ALIVE && creatureType[i] != TYPE_MINER && creatureType[i] != TYPE_LAMP) {
            int16_t rdx = (int16_t)creatureX[i] - (int16_t)xpos;
            if (rdx < -64 || rdx > 64) continue;

            uint8_t type = creatureType[i];
            int16_t cx = (int16_t)creatureX[i];
            int16_t cy = (int16_t)creatureY[i];
            uint8_t m = creatureOffsetIndex[i];

            if (type == TYPE_ALIEN) {
                cx = (int16_t)(cx + alienX[m]);
                cy = (int16_t)(cy + alienOffsetY(m));
            } else if (type == TYPE_BAT_RIGHT) {
                cx = (int16_t)(cx + batOff[m]);
            } else if (type == TYPE_BAT_DOWN) {
                cy = (int16_t)(cy + batOff[m]);
            }

            /* Vertical distance: bat visual body center is cy + 9, other creatures cy + 8 */
            int16_t cyCenter = (type == TYPE_BAT_DOWN || type == TYPE_BAT_RIGHT) ? (cy + 9) : (cy + 8);
            int16_t dy = cyCenter - (int16_t)ypos;
            if (dy < 0) dy = -dy;
            if (dy > 14) continue;

            /* Horizontal distance in facing direction */
            int16_t dx;
            if (isMovingLeft) dx = (int16_t)xpos - cx;
            else             dx = cx - (int16_t)xpos;
            if (dx < 4 || dx > maxReach) continue;

            /* Hit! */
            creatureLife[i] = CREATURE_DYING_START;
            creatureLit[i] = 1;
            /* Freeze position at exact point of impact */
            creatureX[i] = (uint16_t)cx;
            creatureY[i] = (uint16_t)cy;
            audioPlaySfx(SFX_CREATUREDEAD);
            return 1;
        }
    }
    return 0;
}

static uint8_t check_tile(uint16_t px, uint16_t py) {
    uint16_t r = py / TILEHEIGHT, c = px / TILEWIDTH;
    if (r >= levelHeight || c >= levelWidth) return TILECAT_WALL;
    return tile_category(tile_at(r, c));
}

/* ------------------------------ Creatures ------------------------------ */
static void update_creatures(void) {
    static uint8_t animDelay = 0;
    animDelay = (uint8_t)(animDelay + 1);
    if (animDelay >= CREATURE_ANIMATION_DELAY) animDelay = 0;   /* CheckTimer */
    uint8_t step = (animDelay == 0);

    for (uint8_t i = 0; i < creatureCount; i++) {
        uint8_t life = creatureLife[i];
        if (life == CREATURE_DEAD) continue;
        uint8_t type = creatureType[i];
        uint8_t m = creatureOffsetIndex[i];

        /* Screen position: world anchor - cam + center offset. */
        int16_t sx = (int16_t)creatureX[i] - (int16_t)camxpos + CREATURE_SCREEN_X_OFFSET;
        int16_t sy = (int16_t)creatureY[i] - (int16_t)camypos + CREATURE_SCREEN_Y_OFFSET;

        if (life < CREATURE_DYING_START) {
            /* Movement offset added to screen position for living creatures only. */
            if (type == TYPE_ALIEN) {
                sx = (int16_t)(sx + alienX[m]);
                sy = (int16_t)(sy + alienOffsetY(m));
            } else if (type == TYPE_BAT_RIGHT) sx = (int16_t)(sx + batOff[m]);
            else if (type == TYPE_BAT_DOWN)  sy = (int16_t)(sy + batOff[m]);

            creatureOffsetIndex[i] = (uint8_t)(m + 1) & (MOVEMENT_COUNT - 1);
        }

        /* Offscreen culling: screen is 320x240, sprite is 16x16.
         * If far offscreen, hide once and skip VERA writes + pattern/lighting math. */
        if (sx < -16 || sx > 320 || sy < -16 || sy > 240) {
            if (creatureVisible[i]) {
                hide_sprite((uint16_t)(CREATURE_ADDR_L + (uint16_t)i * 8));
                creatureVisible[i] = 0;
            }
            continue;
        }
        creatureVisible[i] = 1;

        /* Sprite frame address + dying progression. */
        uint16_t patAddr;
        if (life >= CREATURE_DYING_START) {
            if (life == CREATURE_DYING_STOP) {          /* dying finished */
                creatureLife[i] = CREATURE_DEAD;
                hide_sprite((uint16_t)(CREATURE_ADDR_L + (uint16_t)i * 8));
                creatureVisible[i] = 0;
                continue;
            }
            creatureLife[i] = (uint8_t)(life + 1);
            patAddr = (uint16_t)(DIE_ADDR + (uint16_t)(life - CREATURE_DYING_START) * 4);
        } else {
            if (step && type != TYPE_MINER)             /* miner never animates */
                creatureFrame[i] = (uint8_t)(creatureFrame[i] + 1) & (FRAME_COUNT - 1);
            if (type == TYPE_LAMP) patAddr = darkMode ? DARK_LAMP_ADDR : LAMP_ADDR;
            else patAddr = (uint16_t)(creatureAddrTable[type] + (uint16_t)(creatureFrame[i] & (FRAME_COUNT - 1)) * 4);
        }

        /* Light up creature when near the player (sticky). */
        if (!creatureLit[i]) {
            int16_t dx = (int16_t)xpos - (int16_t)creatureX[i];
            int16_t dy = (int16_t)ypos - (int16_t)creatureY[i];
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (dx < LIGHT_CREATURE_COLS && dy < LIGHT_CREATURE_ROWS) creatureLit[i] = 1;
        }

        uint8_t palette = creatureLit[i] ? CREATURE_PALETTE_INDEX : BLACK_CREATURE_PALETTE_INDEX;
        /* attr0 = collision mask + h-flip (creatures.asm: ora #Z_DEPTH, ora spr_coll_mask). */
        set_sprite((uint16_t)(CREATURE_ADDR_L + (uint16_t)i * 8), patAddr,
                   (uint16_t)sx, (uint16_t)sy,
                   (uint8_t)(CREATURE_COLLISION_MASK | (creatureFlip[i] ? 1 : 0)),
                   0x50 | palette);   /* 16x16 */
    }
}

/* ------------------------------ Game tick ------------------------------ */
static uint8_t player_speed(void) {
    if (isFlying) return flyingspeed;
    if (isFalling) return (uint8_t)fallingspeed;
    return walkingspeed;
}

static void set_collision_box(void) {
    /* Offsets relative to the player's top-left position. */
    collQ1x = 5;  collQ1y = -13;
    collQ2x = -6; collQ2y = -13;
    collQ3x = -6; collQ3y = 9;
    collQ4x = 5;  collQ4y = 9;
}

/* Tile category at a world pixel position. */
static uint8_t cat_at(int16_t px, int16_t py) {
    if (px < 0 || py < 0) return TILECAT_WALL;
    uint16_t r = (uint16_t)py / TILEHEIGHT, c = (uint16_t)px / TILEWIDTH;
    if (r >= levelHeight || c >= levelWidth) return TILECAT_WALL;
    return tile_category(tile_at(r, c));
}

static void abort_explosion(void);   /* defined later (explosive section) */

static void kill_player_lava(void) {
    abort_explosion();
    laserEnabled = 0;
    audioStopSfx(SFX_LASER);
    audioStopSfx(SFX_ENGINE);
    audioPlayMusic(MUSIC_KILLED);
    gameStatus = ST_DEATH_LAVA;
    deathDelay = DEAD_DELAY;
}

static void move_up(void) {
    if (!isFlying) {
        if (isTakingOff) { takeoffDelay++; if (takeoffDelay <= TAKEOFF_DELAY) return; }
        else { isTakingOff = 1; takeoffDelay = 0; return; }
        isFlying = 1; isTakingOff = 0;
    }
    isFalling = 0;
    uint8_t sp = player_speed();
    int16_t ny = (int16_t)ypos - sp;
    int16_t q2x = (int16_t)xpos + collQ2x, q1x = (int16_t)xpos + collQ1x;
    uint8_t catLeft = cat_at(q2x, ny + collQ2y);
    uint8_t catRight = cat_at(q1x, ny + collQ1y);
    if (catLeft == TILECAT_DEATH || catRight == TILECAT_DEATH) { kill_player_lava(); return; }
    if (ny < 16) return;                     /* can't fly above level top */

    uint8_t leftClear = (catLeft == TILECAT_SPACE || catLeft == TILECAT_MINER);
    uint8_t rightClear = (catRight == TILECAT_SPACE || catRight == TILECAT_MINER);

    if (leftClear && rightClear) {
        ypos = (uint16_t)ny;
    } else if (!leftClear && rightClear) {
        /* Ceiling slide nudge: left corner blocked, right corner open -> slide right */
        xpos++;
    } else if (leftClear && !rightClear) {
        /* Ceiling slide nudge: right corner blocked, left corner open -> slide left */
        xpos--;
    }
}

static void move_down(void) {
    if (!isFlying) return;                   /* only while flying */
    uint8_t sp = player_speed();
    int16_t ny = (int16_t)ypos + sp;
    int16_t q3x = (int16_t)xpos + collQ3x, q4x = (int16_t)xpos + collQ4x;
    uint8_t catL = cat_at(q3x, ny + collQ3y);
    uint8_t catR = cat_at(q4x, ny + collQ4y);
    if (catL == TILECAT_DEATH || catR == TILECAT_DEATH) { kill_player_lava(); return; }
    if ((catL == TILECAT_SPACE || catL == TILECAT_MINER) &&
        (catR == TILECAT_SPACE || catR == TILECAT_MINER)) {
        ypos = (uint16_t)ny;
    }
}

static void move_left(void) {
    isMovingLeft = 1;
    uint8_t sp = player_speed();
    int16_t nx = (int16_t)xpos - sp;
    int16_t q2y = (int16_t)ypos + collQ2y;
    int16_t q3y = (int16_t)ypos + collQ3y;
    int16_t my  = (int16_t)ypos;
    uint8_t cTop = cat_at(nx + collQ2x, q2y);
    uint8_t cMid = cat_at(nx + collQ3x, my);
    uint8_t cBot = cat_at(nx + collQ3x, q3y);
    if (cTop == TILECAT_DEATH || cMid == TILECAT_DEATH || cBot == TILECAT_DEATH) { kill_player_lava(); return; }
    if ((cTop == TILECAT_SPACE || cTop == TILECAT_MINER) &&
        (cMid == TILECAT_SPACE || cMid == TILECAT_MINER) &&
        (cBot == TILECAT_SPACE || cBot == TILECAT_MINER)) {
        xpos = (uint16_t)nx;
        isMoving = 1;
    }
}

static void move_right(void) {
    isMovingLeft = 0;
    uint8_t sp = player_speed();
    int16_t nx = (int16_t)xpos + sp;
    int16_t q1y = (int16_t)ypos + collQ1y;
    int16_t q4y = (int16_t)ypos + collQ4y;
    int16_t my  = (int16_t)ypos;
    uint8_t cTop = cat_at(nx + collQ1x, q1y);
    uint8_t cMid = cat_at(nx + collQ4x, my);
    uint8_t cBot = cat_at(nx + collQ4x, q4y);
    if (cTop == TILECAT_DEATH || cMid == TILECAT_DEATH || cBot == TILECAT_DEATH) { kill_player_lava(); return; }
    if ((cTop == TILECAT_SPACE || cTop == TILECAT_MINER) &&
        (cMid == TILECAT_SPACE || cMid == TILECAT_MINER) &&
        (cBot == TILECAT_SPACE || cBot == TILECAT_MINER)) {
        xpos = (uint16_t)nx;
        isMoving = 1;
    }
}

/* player.asm KeepClearOfWalls: when falling, keep clear of walls left/right. */
static void keep_clear_of_walls(void) {
    int16_t q3y = (int16_t)ypos + collQ3y;
    int16_t q3x = (int16_t)xpos + collQ3x;
    int16_t q4y = (int16_t)ypos + collQ4y;
    int16_t q4x = (int16_t)xpos + collQ4x;
    if (cat_at(q3x, q3y) == TILECAT_BLOCK) { xpos = (uint16_t)((int16_t)xpos + 1); return; }
    if (cat_at(q3x, q3y) == TILECAT_DEATH) { kill_player_lava(); return; }
    if (cat_at(q4x, q4y) == TILECAT_BLOCK) { xpos = (uint16_t)((int16_t)xpos - 1); return; }
    if (cat_at(q4x, q4y) == TILECAT_DEATH) { kill_player_lava(); }
}

/* player.asm CheckLandingAndFalling. */
static void check_landing_and_falling(void) {
    uint8_t below = cat_at((int16_t)xpos, (int16_t)((int16_t)ypos + 16 - 4 - 1));
    currentTileBelow = below;

    if (below == TILECAT_DEATH) { kill_player_lava(); return; }

    /* Space or miner below: start falling if walking; keep clear of walls. */
    if (below == TILECAT_SPACE || below == TILECAT_MINER) {
        keep_clear_of_walls();
        if (gameStatus != ST_RUNNING) return;   /* killed by lava in keep_clear */
        if (!isFlying) isFalling = 1;
        return;
    }

    /* Block/wall below: land only if flying/falling and came from above. */
    if (!isFlying && !isFalling) return;
    if (lastTileBelow != TILECAT_SPACE) return;
    isFalling = 0; isFlying = 0; isMoving = 0;
    flyingspeed = MIN_FLYINGSPEED; fallingspeed = MIN_FALLINGSPEED; walkingspeed = MIN_WALKINGSPEED;
    fallingspeedDelay = 0;
    ypos = (uint16_t)((ypos & 0xFFF0) | TILE_GROUND_LEVEL);   /* faithful: (ypos&$F0)|TILE_GROUND_LEVEL */
}

/* ------------------------------ Explosive (miscsprites.asm) ------------------------------ */
static void save_background_colors(void) {
    vera_set_addr(VERA_INC_BANK1, TILES_PALETTES_ADDR + 2);
    originalBgColors[0] = (uint16_t)(VERA.data0 | ((uint16_t)VERA.data0 << 8));
    originalBgColors[1] = (uint16_t)(VERA.data0 | ((uint16_t)VERA.data0 << 8));
}

static void restore_background_colors(void) {
    vera_set_addr(VERA_INC_BANK1, TILES_PALETTES_ADDR + 2);
    VERA.data0 = (uint8_t)(originalBgColors[0] & 0xFF);
    VERA.data0 = (uint8_t)(originalBgColors[0] >> 8);
    VERA.data0 = (uint8_t)(originalBgColors[1] & 0xFF);
    VERA.data0 = (uint8_t)(originalBgColors[1] >> 8);
}

static void place_explosive(void) {
    explosiveFrame = 0;
    explX = xpos; explY = (uint16_t)((int16_t)ypos + EXPLOSIVE_YOFFSET);
    if (isMovingLeft) explX = (uint16_t)((int16_t)explX - EXPLOSIVE_XOFFSET);
    else              explX = (uint16_t)((int16_t)explX + EXPLOSIVE_XOFFSET);

    int16_t sx = (int16_t)explX - (int16_t)camxpos + SCREENWIDTH/2 - 8;   /* 16x16: -width/2 */
    int16_t sy = (int16_t)explY - (int16_t)camypos + SCREENHEIGHT/2 - 8;
    vera_set_addr(VERA_INC_BANK1, EXPLOSIVES);
    VERA.data0 = (uint8_t)(EXPLOSIVE_PAT_REG & 0xFF);
    VERA.data0 = (uint8_t)(EXPLOSIVE_PAT_REG >> 8);
    VERA.data0 = (uint8_t)(sx & 0xFF);
    VERA.data0 = (uint8_t)(sx >> 8);
    VERA.data0 = (uint8_t)(sy & 0xFF);
    VERA.data0 = (uint8_t)(sy >> 8);
    VERA.data0 = 0x08;                 /* z-depth 2, no collision mask */
    VERA.data0 = 0x50 | 2;             /* 16x16, palette 2 */
    explosiveMode = EXPLOSIVE_BURN;
    stubThreadTime = 0;
    explosiveFrameDelay = 0;
}

static void burn_explosive(void) {
    /* Animate frame. */
    explosiveFrameDelay++;
    if (explosiveFrameDelay >= EXPLOSIVE_FRAMEDELAY) {
        explosiveFrameDelay = 0;
        explosiveFrame++;
        if (explosiveFrame >= (EXPLOSIVE_STOP - EXPLOSIVE_START + 1)) explosiveFrame = 0;
    }
    uint16_t patReg = (uint16_t)(EXPLOSIVE_PAT_REG + (uint16_t)explosiveFrame * 4);
    int16_t sx = (int16_t)explX - (int16_t)camxpos + SCREENWIDTH/2 - 8;   /* 16x16: -width/2 */
    int16_t sy = (int16_t)explY - (int16_t)camypos + SCREENHEIGHT/2 - 8;
    vera_set_addr(VERA_INC_BANK1, EXPLOSIVES);
    VERA.data0 = (uint8_t)(patReg & 0xFF);
    VERA.data0 = (uint8_t)(patReg >> 8);
    VERA.data0 = (uint8_t)(sx & 0xFF);
    VERA.data0 = (uint8_t)(sx >> 8);
    VERA.data0 = (uint8_t)(sy & 0xFF);
    VERA.data0 = (uint8_t)(sy >> 8);

    /* Stub thread countdown; when done hide + detonate. */
    stubThreadTime++;
    if (stubThreadTime >= EXPLOSIVE_STUBTHREAD_TIME) {
        vera_set_addr(VERA_INC_BANK1, EXPLOSIVES + 6);
        VERA.data0 = 0x00;   /* hide sprite */
        explosiveMode = EXPLOSIVE_START_DETONATE;
    }
}

static uint8_t check_if_player_blasted(void) {
    int16_t dx = (int16_t)xpos - (int16_t)explX;
    int16_t dy = (int16_t)ypos - (int16_t)explY;
    if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
    return (dx < EXPLOSIVE_SAFE_DISTANCE && dy < EXPLOSIVE_SAFE_DISTANCE) ? 1 : 0;
}

static void remove_wall(void) {
    int16_t er = (int16_t)explY / TILEHEIGHT;
    int16_t ec = (int16_t)explX / TILEWIDTH;
    int16_t startR = er - 2, startC = ec - 1;   /* 4 rows x 3 cols */
    for (int16_t r = startR; r < startR + 4; r++) {
        for (int16_t c = startC; c < startC + 3; c++) {
            if (r < 0 || c < 0 || r >= (int16_t)levelHeight || c >= (int16_t)levelWidth) continue;
            if (tile_category(tile_at((uint16_t)r, (uint16_t)c)) == TILECAT_WALL)
                set_tile((uint16_t)r, (uint16_t)c, TILE_SPACE, TILE_PALETTE_INDEX << 4);
        }
    }
}

static void abort_explosion(void) {
    vera_set_addr(VERA_INC_BANK1, EXPLOSIVES + 6);
    VERA.data0 = 0x00;   /* hide sprite */
    restore_background_colors();
    stubThreadTime = 0;
    explosiveMode = EXPLOSIVE_NO;
}

static void update_explosive(void) {
    switch (explosiveMode) {
    case EXPLOSIVE_NO: return;
    case EXPLOSIVE_PLACE: place_explosive(); break;
    case EXPLOSIVE_BURN: burn_explosive(); break;
    case EXPLOSIVE_START_DETONATE:
        audioPlaySfx(SFX_EXPLOSION);
        playerBlasted = check_if_player_blasted();
        remove_wall();
        save_background_colors();
        explosionColorIndex = 0;
        explosiveMode = EXPLOSIVE_DETONATE;
        break;
    case EXPLOSIVE_DETONATE:
        if (explosionColorIndex < EXPLOSIONCOLORCOUNT) {
            backgroundColor = explosionColors[explosionColorIndex++];
            vera_set_addr(VERA_INC_BANK1, TILES_PALETTES_ADDR + 2);
            VERA.data0 = (uint8_t)(backgroundColor & 0xFF);
            VERA.data0 = (uint8_t)(backgroundColor >> 8);
            VERA.data0 = (uint8_t)(backgroundColor & 0xFF);
            VERA.data0 = (uint8_t)(backgroundColor >> 8);
        } else {
            restore_background_colors();
            explosiveMode = EXPLOSIVE_NO;
            if (playerBlasted) {
                hide_player();
                laserEnabled = 0;
                audioStopSfx(SFX_LASER);
                audioStopSfx(SFX_ENGINE);
                audioPlayMusic(MUSIC_KILLED);
                gameStatus = ST_DEATH_EXPLOSION;
                deathDelay = DEAD_DELAY;
            }
        }
        break;
    default: break;
    }
}

/* Render the laser beam (2 sprites) extending from the player in the facing
 * direction (miscsprites.asm FireLaser). Stationary beam, not a moving bullet! */
static void update_laser_sprites(void) {
    if (!laserEnabled || laserpossible == 0) {
        hide_sprite(LASER0);
        hide_sprite(LASER1);
        return;
    }
    uint16_t patReg = (uint16_t)(LASER_PAT_REG + (uint16_t)laserFrame * 4);
    uint8_t attr0 = (uint8_t)(LASER_COLLISION_MASK | (isMovingLeft ? 1 : 0));

    /* Offsets matching miscsprites.asm:
     * Facing right: sprite 0 at +14, sprite 1 at +30.
     * Facing left:  sprite 0 at -30, sprite 1 at -14. */
    int16_t off0 = isMovingLeft ? -30 : 14;
    int16_t off1 = isMovingLeft ? -14 : 30;
    int16_t sx0 = (int16_t)xpos + off0 - (int16_t)camxpos + SCREENWIDTH/2 - 8;
    int16_t sx1 = (int16_t)xpos + off1 - (int16_t)camxpos + SCREENWIDTH/2 - 8;
    int16_t sy  = (int16_t)ypos + 8 - (int16_t)camypos + SCREENHEIGHT/2 - 16;

    if (laserpossible == 1) {
        if (isMovingLeft) {
            hide_sprite(LASER0);
            set_sprite(LASER1, patReg, (uint16_t)(sx1 < 0 ? 0 : sx1), (uint16_t)(sy < 0 ? 0 : sy), attr0, 0x50 | 2);
        } else {
            set_sprite(LASER0, patReg, (uint16_t)(sx0 < 0 ? 0 : sx0), (uint16_t)(sy < 0 ? 0 : sy), attr0, 0x50 | 2);
            hide_sprite(LASER1);
        }
    } else { /* laserpossible == 2 */
        set_sprite(LASER0, patReg, (uint16_t)(sx0 < 0 ? 0 : sx0), (uint16_t)(sy < 0 ? 0 : sy), attr0, 0x50 | 2);
        set_sprite(LASER1, patReg, (uint16_t)(sx1 < 0 ? 0 : sx1), (uint16_t)(sy < 0 ? 0 : sy), attr0, 0x50 | 2);
    }
}

static void player_tick(void) {
    if (invulnerableTimer > 0) invulnerableTimer--;
    lastXpos = xpos; lastYpos = ypos;
    set_collision_box();

    uint8_t j = joy();

    /* Vertical: up = fly/boost, down = descend (flying only). */
    if ((j & JOY_UP) == 0) {
        move_up();
        if (isFlying && flyingspeed < MAX_FLYINGSPEED) flyingspeed++;
        flyingTime = FLYINGTIME;
    } else {
        isTakingOff = 0;
        if ((j & JOY_DOWN) == 0) {
            move_down();
            if (isFlying && flyingspeed < MAX_FLYINGSPEED) flyingspeed++;
        }
    }

    /* Horizontal. */
    if ((j & JOY_LEFT) == 0) { move_left(); if (isFlying && flyingspeed < MAX_FLYINGSPEED) flyingspeed++; }
    else if ((j & JOY_RIGHT) == 0) { move_right(); if (isFlying && flyingspeed < MAX_FLYINGSPEED) flyingspeed++; }
    else isMoving = 0;   /* no horizontal input (player.asm SetNotMoving) */

    /* Count down flying time (.CountDownFlyingTime). */
    if (isFlying) {
        if (flyingTime > 0) flyingTime--;
        else { isFlying = 0; isFalling = 1; fallingspeed = MIN_FALLINGSPEED; }
    }

    /* Fall down (.FallDown): increase speed every 8 frames, step pixel by pixel to prevent tunneling. */
    if (isFalling) {
        fallingspeedDelay++;
        if (fallingspeedDelay >= FALLINGSPEED_DELAY) {
            fallingspeedDelay = 0;
            if (fallingspeed < 4) fallingspeed++;
        }
        for (uint8_t s = 0; s < fallingspeed; s++) {
            ypos++;
            uint8_t below = cat_at((int16_t)xpos, (int16_t)((int16_t)ypos + 16 - 4 - 1));
            if (below == TILECAT_BLOCK || below == TILECAT_WALL) {
                ypos = (uint16_t)((ypos & 0xFFF0) | TILE_GROUND_LEVEL);
                isFalling = 0;
                isFlying = 0;
                isMoving = 0;
                flyingspeed = MIN_FLYINGSPEED;
                fallingspeed = MIN_FALLINGSPEED;
                walkingspeed = MIN_WALKINGSPEED;
                fallingspeedDelay = 0;
                currentTileBelow = below;
                break;
            } else if (below == TILECAT_DEATH) {
                kill_player_lava();
                break;
            }
        }
    }

    /* Check laser possible (player.asm CheckLaserPossible): tile(s) in front.
     * 0 = wall right in front (beam blocked), 1 = one clear tile, 2 = two. */
    if (laserEnabled || (j & JOY_BUTTON_A) == 0) {
        int16_t tileOffset = isMovingLeft ? -16 : 16;
        uint8_t t1 = cat_at((int16_t)xpos + tileOffset, (int16_t)ypos);
        if (t1 == TILECAT_BLOCK || t1 == TILECAT_WALL || t1 == TILECAT_DEATH) laserpossible = 0;
        else {
            uint8_t t2 = cat_at((int16_t)xpos + 2*tileOffset, (int16_t)ypos);
            if (t2 == TILECAT_BLOCK || t2 == TILECAT_WALL || t2 == TILECAT_DEATH) laserpossible = 1;
            else laserpossible = 2;
        }
    } else {
        laserpossible = 0;
    }

    /* Fire laser (button A). */
    if ((j & JOY_BUTTON_A) == 0) {
        if (!laserEnabled) {
            laserEnabled = 1;
            laserTime = 0;
            laserFrame = 0;
            audioPlaySfx(SFX_LASER);
        }
    } else {
        laserEnabled = 0;
        audioStopSfx(SFX_LASER);
    }

    /* Place dynamite (button B): only when grounded and no explosive active. */
    if ((j & JOY_BUTTON_B) == 0) {
        if (explosiveMode == EXPLOSIVE_NO && !isFlying && !isFalling) explosiveMode = EXPLOSIVE_PLACE;
    }

    /* Landing / falling detection. */
    check_landing_and_falling();
    if (gameStatus != ST_RUNNING) return;

    /* Engine sound: hum while flying/taking off, stop when landed. */
    if (isFlying || isTakingOff) audioStartSfxIfIdle(SFX_ENGINE);
    else audioStopSfx(SFX_ENGINE);

    lastTileBelow = currentTileBelow;
    update_explosive();
}

/* HUD timer: 60 jiffies/sec, 60 sec/min (timerlib.asm TimeTick). */
static void time_tick(void) {
    jiffies++;
    if (jiffies >= 60) {
        jiffies = 0;
        seconds++;
        if (seconds >= 60) { seconds = 0; minutes++; if (minutes >= 60) minutes = 0; }
    }
}

/* ------------------------------ UI: small helpers ------------------------------ */
static void text_rowstr(uint8_t row, uint8_t col, const uint8_t *codes, uint8_t color) {
    while (*codes) { text_putc_raw(row, col++, *codes++, color); }
}
static void text_shortnum(uint8_t row, uint8_t col, uint8_t val, uint8_t color) {
    if (val > 99) val = 99;
    text_putc(row, col, '0' + (val / 10), color);
    text_putc(row, col + 1, '0' + (val % 10), color);
}
static void text_time(uint8_t row, uint8_t col, uint8_t min, uint8_t sec, uint8_t color) {
    text_putc(row, col,     '0' + (min / 10), color);
    text_putc(row, col + 1, '0' + (min % 10), color);
    text_putc(row, col + 2, ':', color);
    text_putc(row, col + 3, '0' + (sec / 10), color);
    text_putc(row, col + 4, '0' + (sec % 10), color);
}

/* ------------------------------ Board (board.asm) ------------------------------ */
/* Status bar: row 28 (original view.asm / level04.png).
 * Layout:
 *   col 1..5:  "LEVEL"
 *   col 7..8:  level number (e.g. "04")
 *   col 17..21: "MM:SS" (e.g. "01:12")
 *   col 31..35: "LIVES"
 *   col 37..38: lives number (e.g. "05") */
static void draw_status_bar_static(void) {
    text_print(28, 1, "LEVEL", 1);
    text_putc(28, 6, ' ', 1);
    text_shortnum(28, 7, level, 1);
    text_time(28, 17, minutes, seconds, 1);
    text_print(28, 31, "LIVES", 1);
    text_putc(28, 36, ' ', 1);
    text_shortnum(28, 37, lives, 1);
    lastBarSec = seconds;
    lastBarLives = lives;
    lastBarLevel = level;
}
static void update_status_bar(void) {
    if (level != lastBarLevel || lives != lastBarLives) {
        draw_status_bar_static();
        return;
    }
    if (seconds != lastBarSec) {
        text_time(28, 17, minutes, seconds, 1);
        lastBarSec = seconds;
    }
}

/* Big 2-row titles (board.asm).  Top array = row R, bottom array = row R+1. */
static const uint8_t boardLevelFinishedTop[] = {
    112,113, 96,97, 116,117, 80,81, 132,133, 32, 136,137, 64,65, 148,149, 80,81, 76,77, 170, 0 };
static const uint8_t boardLevelFinishedBot[] = {
    114,115, 98,99, 118,119, 82,83, 134,135, 32, 138,139, 66,67, 150,151, 82,83, 78,79, 171, 0 };
static const uint8_t boardGameOverTop[] = {
    88,89, 64,65, 112,113, 80,81, 32, 120,121, 148,149, 80,81, 132,133, 170, 0 };
static const uint8_t boardGameOverBot[] = {
    90,91, 66,67, 114,115, 82,83, 32, 122,123, 150,151, 82,83, 134,135, 171, 0 };
static const uint8_t boardCompletedTop[] = {
    112,113, 96,97, 136,137, 136,137, 96,97, 120,121, 116,117, 32, 72,73, 120,121, 112,113, 124,125, 108,109, 80,81, 140,141, 80,81, 170, 0 };
static const uint8_t boardCompletedBot[] = {
    114,115, 98,99, 138,139, 138,139, 98,99, 122,123, 118,119, 32, 74,75, 122,123, 114,115, 126,127, 110,111, 82,83, 142,143, 82,83, 171, 0 };

static void print_level_finished(void) {
    text_rowstr(12, 9,  boardLevelFinishedTop, 7);
    text_rowstr(13, 9,  boardLevelFinishedBot, 7);
}
static void print_game_over(void) {
    text_rowstr(12, 11, boardGameOverTop, 7);
    text_rowstr(13, 11, boardGameOverBot, 7);
}
static void print_game_completed(void) {
    text_rowstr(9, 4, boardCompletedTop, 7);
    text_rowstr(10, 4, boardCompletedBot, 7);
    text_print(12, 7, "   you are a true hero!", 1);
    text_print(13, 7, " the international rescue", 1);
    text_print(14, 7, "service for trapped miners", 1);
    text_print(15, 7, "   will honor you with", 1);
    text_print(16, 7, "   a medal of bravery!", 1);
}

/* ------------------------------ Pause menu (board.asm) ------------------------------ */
#define PAUSEMENU_ITEMCOUNT 2
static uint8_t selecteditem;
static uint8_t pauseInputWait;
static const char * const pauseMenuItems[PAUSEMENU_ITEMCOUNT] = {
    " resume game ",
    " quit        ",
};
static void print_pause_menu(void) {
    /* Board shadow (width 13, height 4, startrow 12, startcol 13) from board.asm.
     * Color 0x0B (bg=transparent, fg=black shadow). */
    /* Right shadow at col 26 */
    text_putc_raw(12, 26, 29, 0x0B);  /* TOP_RIGHT_BORDER */
    for (uint8_t r = 13; r < 16; r++) {
        text_putc_raw(r, 26, 31, 0x0B); /* RIGHT_BORDER */
    }
    /* Bottom shadow at row 16 */
    text_putc_raw(16, 13, 28, 0x0B);  /* BOTTOM_LEFT_BORDER */
    for (uint8_t c = 14; c < 26; c++) {
        text_putc_raw(16, c, 36, 0x0B); /* BOTTOM_BORDER */
    }
    text_putc_raw(16, 26, 27, 0x0B);  /* BOTTOM_RIGHT_BORDER */

    /* Board box (width 13, height 4, startrow 12, startcol 13) */
    text_print(12, 13, "             ", 0x91);
    for (uint8_t i = 0; i < PAUSEMENU_ITEMCOUNT; i++) {
        uint8_t col = (i == selecteditem) ? 0x91 : 0x9B;
        text_print(13 + i, 13, pauseMenuItems[i], col);
    }
    text_print(15, 13, "             ", 0x91);
}
static void show_pause_menu(void) { selecteditem = 0; pauseInputWait = 12; print_pause_menu(); }
/* Returns 0 = resume, 1 = quit, 255 = nothing. */
static uint8_t update_pause_menu(void) {
    if (pauseInputWait > 0) { pauseInputWait--; return 255; }
    uint8_t j = joy();
    if (j == JOY_NOTHING_PRESSED) return 255;
    if ((j & JOY_BUTTON_A) == 0 || (j & JOY_START) == 0) return selecteditem;
    if ((j & JOY_UP) == 0) { selecteditem = (uint8_t)((selecteditem - 1) & (PAUSEMENU_ITEMCOUNT - 1)); print_pause_menu(); pauseInputWait = 14; return 255; }
    if ((j & JOY_DOWN) == 0) { selecteditem = (uint8_t)((selecteditem + 1) & (PAUSEMENU_ITEMCOUNT - 1)); print_pause_menu(); pauseInputWait = 14; return 255; }
    return 255;
}

/* ------------------------------ Menu (menu.asm) ------------------------------ */
static void print_leaderboard(void);
static void reset_leaderboard(void);
static void save_leaderboard(void);
static void load_leaderboard(void);
#define M_SHOW_MENU_SCREEN 0
#define M_SHOW_HIGHSCORE_SCREEN 1
#define M_ENTER_NEW_HIGH_SCORE 2
#define M_ENTER_NEW_HIGH_SCORE_2 3
#define M_SHOW_CREDIT_SCREEN 4
#define M_HANDLE_INPUT 5
#define MENU_COL 9
#define MENU_ITEMS_COUNT 4
#define MENU_ROW_COUNT 19
#define LEVEL_ROW 11
#define ARROW_POSITIONS 25
#define INACTIVITY_DELAY 3
#define MENU_WHITE 0x01
#define MENU_BLACK 0x0C
#define MENU_TITLE_COLOR 0x07
#define YES_POSITION 24
#define NO_POSITION 26

/* Big title "MINERESCUE" (menu.asm), 2 rows. */
static const uint8_t menuTitleTop[] = {
    112,113, 96,97, 116,117, 80,81, 168, 132,133, 80,81, 136,137, 72,73, 144,145, 80,81, 0 };
static const uint8_t menuTitleBot[] = {
    114,115, 98,99, 118,119, 82,83, 168, 134,135, 82,83, 138,139, 74,75, 146,147, 82,83, 0 };

static const char *menuItems[4] = {
    "start the game", "set start level (  )", "reset high scores   ", "quit game           " };
static const uint8_t menuItemRows[4] = { 9, 11, 13, 15 };
static uint8_t handrow;
static uint8_t inputwait;
static uint8_t answer;
static uint16_t inactivitytimer;
static uint8_t levelconfirmationflag, resetconfirmationflag, quitconfirmationflag;
static uint8_t highscorewait, creditwait;
static uint8_t menuNavDelay;

static void print_hand(uint8_t clear) {
    uint8_t row = menuItemRows[handrow];
    text_print(row, 6, clear ? "  " : " >", clear ? MENU_BLACK : MENU_WHITE);
}
static void update_main_menu(void) {
    /* Title. */
    text_rowstr(5, 9, menuTitleTop, MENU_TITLE_COLOR);
    text_rowstr(6, 9, menuTitleBot, MENU_TITLE_COLOR);
    /* Menu items. */
    for (uint8_t i = 0; i < MENU_ITEMS_COUNT; i++) {
        if (i == 3 && quitconfirmationflag) {
            text_print(menuItemRows[3], MENU_COL, "are you sure? (y/n)?", MENU_BLACK);
            text_putc(menuItemRows[3], 24, 'Y', answer ? MENU_WHITE : MENU_BLACK);
            text_putc(menuItemRows[3], 26, 'N', answer ? MENU_BLACK : MENU_WHITE);
        } else if (i == 2 && resetconfirmationflag) {
            text_print(menuItemRows[2], MENU_COL, "are you sure? (y/n)?", MENU_BLACK);
            text_putc(menuItemRows[2], 24, 'Y', answer ? MENU_WHITE : MENU_BLACK);
            text_putc(menuItemRows[2], 26, 'N', answer ? MENU_BLACK : MENU_WHITE);
        } else {
            text_print(menuItemRows[i], MENU_COL, menuItems[i], i == handrow ? MENU_WHITE : MENU_BLACK);
        }
    }

    /* Format two-digit start level inside ( 01 ) at row 11, col 26..27 */
    text_putc(11, 26, (uint8_t)('0' + (startLevel / 10)), MENU_WHITE);
    text_putc(11, 27, (uint8_t)('0' + (startLevel % 10)), MENU_WHITE);
    if (levelconfirmationflag) {
        text_putc(11, 25, '<', MENU_WHITE);
        text_putc(11, 28, '>', MENU_WHITE);
    } else {
        text_putc(11, 25, '(', handrow == 1 ? MENU_WHITE : MENU_BLACK);
        text_putc(11, 28, ')', handrow == 1 ? MENU_WHITE : MENU_BLACK);
    }

    /* Centered small font: <-SWITCH SCREEN-> (chars 212..219 at row 18, col 16) */
    for (uint8_t i = 0; i < 8; i++) {
        text_putc_raw(18, 16 + i, (uint8_t)(212 + i), MENU_BLACK);
    }

    print_hand(0);
}
static void set_menu_camera(uint16_t cx, uint16_t cy) {
    camxpos = cx; camypos = cy;
    VERA.layer0.hscroll = camxpos - SCREENWIDTH/2;
    VERA.layer0.vscroll = camypos - SCREENHEIGHT/2;
}
static void show_menu_screen(void) {
    levelconfirmationflag = 0;
    resetconfirmationflag = 0;
    quitconfirmationflag = 0;
    answer = 0;
    set_menu_camera(SCREENWIDTH/2, SCREENHEIGHT/2);   /* map top-left, hscroll=vscroll=0 */
    text_clear();
    handrow = 0;
    menuNavDelay = 10;
    update_main_menu();
}
static void show_high_score_screen(void) {
    set_menu_camera(480, SCREENHEIGHT/2);
    text_clear();
    print_leaderboard();
}
static void show_credit_screen(void) {
    set_menu_camera(800, SCREENHEIGHT/2);
    text_clear();
    text_rowstr(5, 9, menuTitleTop, 7);
    text_rowstr(6, 9, menuTitleBot, 7);
    text_print(9, 12, "by johan k;rlin", 1);
    text_print(11, 12, "music by gtr3qq", 1);
    text_print(13, 6, "using zsmkit by mooinglemur", 1);
    text_print(16, 9, "inspired by h.e.r.o.", 1);
    text_print(18, 7, "for atari and commodore 64", 1);
    text_print(20, 13, "version: 1.01", 1);
    text_print(23, 5, "apple ii vera port by anomixer", 1);
}
static void handle_updown(void) {
    if (menuNavDelay > 0) return;
    uint8_t j = joy();
    if ((j & JOY_UP) == 0) {
        print_hand(1);
        if (handrow == 0) handrow = MENU_ITEMS_COUNT - 1;
        else handrow--;
        resetconfirmationflag = 0; quitconfirmationflag = 0; levelconfirmationflag = 0;
        menuNavDelay = 14;
        update_main_menu();
    } else if ((j & JOY_DOWN) == 0) {
        print_hand(1);
        handrow = (uint8_t)((handrow + 1) % MENU_ITEMS_COUNT);
        resetconfirmationflag = 0; quitconfirmationflag = 0; levelconfirmationflag = 0;
        menuNavDelay = 14;
        update_main_menu();
    }
}
static void handle_leftright(void) {
    if (menuNavDelay > 0) return;
    uint8_t j = joy();
    if ((j & JOY_LEFT) == 0) {
        menumode = M_SHOW_CREDIT_SCREEN; show_credit_screen(); creditwait = 1; inactivitytimer = 0; menuNavDelay = 14;
    } else if ((j & JOY_RIGHT) == 0) {
        menumode = M_SHOW_HIGHSCORE_SCREEN; show_high_score_screen(); highscorewait = 1; inactivitytimer = 0; menuNavDelay = 14;
    }
}
static void handle_button(void) {
    uint8_t j = joy();
    if ((j & JOY_BUTTON_A) != 0 && (j & JOY_START) != 0) return;   /* button A / Start not pressed */
    switch (handrow) {
    case 0: /* start game */
        menumode = M_SHOW_MENU_SCREEN;
        gameStatus = ST_INITGAME;
        break;
    case 1: /* set start level */
        if (levelconfirmationflag == 0) { levelconfirmationflag = 1; update_main_menu(); }
        else { levelconfirmationflag = 0; update_main_menu(); menumode = M_HANDLE_INPUT; }
        menuNavDelay = 12;
        break;
    case 2: /* reset high scores */
        if (resetconfirmationflag == 0) { resetconfirmationflag = 1; answer = 0; update_main_menu(); }
        else {
            resetconfirmationflag = 0;
            if (answer) { reset_leaderboard(); save_leaderboard(); show_high_score_screen(); highscorewait = 1; menumode = M_SHOW_HIGHSCORE_SCREEN; }
            else { menumode = M_HANDLE_INPUT; update_main_menu(); }
        }
        menuNavDelay = 12;
        break;
    case 3: /* quit game */
        if (quitconfirmationflag == 0) { quitconfirmationflag = 1; answer = 0; update_main_menu(); }
        else {
            quitconfirmationflag = 0;
            if (answer) { gameStatus = ST_QUITGAME; }
            else { menumode = M_HANDLE_INPUT; update_main_menu(); }
        }
        menuNavDelay = 12;
        break;
    }
}
static void handle_user_input(void) {
    uint8_t j = joy();
    if (j == JOY_NOTHING_PRESSED) { inputwait = 0; return; }
    if (inputwait) return;
    if (menuNavDelay > 0) return;
    inputwait = 1;
    inactivitytimer = 0;
    /* Confirmation questions: left/right toggles answer. */
    if (levelconfirmationflag) {
        if (menuNavDelay > 0) return;
        uint8_t l = joy();
        if ((l & JOY_LEFT) == 0) { if (startLevel > 1) startLevel--; menuNavDelay = 10; update_main_menu(); return; }
        if ((l & JOY_RIGHT) == 0) { if (startLevel < leaderboard_start_high) startLevel++; menuNavDelay = 10; update_main_menu(); return; }
        if ((l & JOY_BUTTON_A) == 0 || (l & JOY_UP) == 0 || (l & JOY_DOWN) == 0 || (l & JOY_START) == 0) {
            levelconfirmationflag = 0;
            menuNavDelay = 12;
            update_main_menu();
            return;
        }
        return;
    }
    if (resetconfirmationflag || quitconfirmationflag) {
        if (menuNavDelay > 0) return;
        uint8_t l = joy();
        if ((l & JOY_LEFT) == 0) { answer = 1; menuNavDelay = 10; update_main_menu(); return; }
        else if ((l & JOY_RIGHT) == 0) { answer = 0; menuNavDelay = 10; update_main_menu(); return; }
        if ((l & JOY_BUTTON_A) == 0 || (l & JOY_START) == 0) {
            handle_button();
            menuNavDelay = 12;
            return;
        }
        return;
    }
    handle_updown();
    handle_leftright();
    handle_button();
}
static void menu_handler(void) {
    if (menuNavDelay > 0) menuNavDelay--;
    if (menumode == M_SHOW_MENU_SCREEN) {
        show_menu_screen();
        menumode = M_HANDLE_INPUT;
        inputwait = 1;
        inactivitytimer = 0;
    } else if (menumode == M_SHOW_HIGHSCORE_SCREEN) {
        uint8_t j = joy();
        if (highscorewait) {
            if (j == JOY_NOTHING_PRESSED) highscorewait = 0;
            return;   /* wait for key release before accepting input */
        }
        inactivitytimer++;
        if (inactivitytimer >= 720) {
            menumode = M_SHOW_CREDIT_SCREEN; show_credit_screen(); creditwait = 1; inactivitytimer = 0;
            return;
        }
        if (j != JOY_NOTHING_PRESSED) {
            if ((j & JOY_RIGHT) == 0) {
                menumode = M_SHOW_CREDIT_SCREEN; show_credit_screen(); creditwait = 1; inactivitytimer = 0;
            } else {
                menumode = M_SHOW_MENU_SCREEN; show_menu_screen();
            }
        }
        return;
    } else if (menumode == M_SHOW_CREDIT_SCREEN) {
        uint8_t j = joy();
        if (creditwait) {
            if (j == JOY_NOTHING_PRESSED) creditwait = 0;
            return;   /* wait for key release before accepting input */
        }
        inactivitytimer++;
        if (inactivitytimer >= 720) {
            menumode = M_SHOW_MENU_SCREEN; show_menu_screen();
            return;
        }
        if (j != JOY_NOTHING_PRESSED) {
            if ((j & JOY_LEFT) == 0) {
                menumode = M_SHOW_HIGHSCORE_SCREEN; show_high_score_screen(); highscorewait = 1; inactivitytimer = 0;
            } else {
                menumode = M_SHOW_MENU_SCREEN; show_menu_screen();
            }
        }
        return;
    } else if (menumode == M_HANDLE_INPUT) {
        inactivitytimer++;
        if (inactivitytimer >= 1800) {
            menumode = M_SHOW_HIGHSCORE_SCREEN; show_high_score_screen(); highscorewait = 1; inactivitytimer = 0;
            return;
        }
        handle_user_input();
        return;
    }
}

/* ------------------------------ Leaderboard (leaderboard.asm) ------------------------------ */
#define LB_ROW 2
#define LB_NAME_LENGTH 11
#define LB_HEADING_COUNT 6
#define LB_ENTRIES_COUNT 10
#define LB_NAME_COL 8
#define LB_MINERS_COL 22
#define LB_TIME_COL 27
#define LB_START_COL 34

static uint8_t  leaderboard_names[LB_ENTRIES_COUNT][LB_NAME_LENGTH + 1];
static uint8_t  leaderboard_saved[LB_ENTRIES_COUNT];
static uint8_t  leaderboard_times[LB_ENTRIES_COUNT * 2];
static uint8_t  leaderboard_start[LB_ENTRIES_COUNT];
static uint8_t  newrank, newminers, newtimeLo, newtimeHi, newstart;

static const char *lbDefaultNames[LB_ENTRIES_COUNT] = {
    "roderrick  ", "elvin a    ", "guybrush   ", "sandy pantz", "z mckracken",
    "bruce lee  ", "armakuni   ", "rockford   ", "giana      ", "monty mole " };

/* Big title "HIGH SCORES" (leaderboard.asm), 2 rows. */
static const uint8_t lbTitleTop[] = {
    92,93, 96,97, 88,89, 92,93, 32, 136,137, 72,73, 120,121, 132,133, 80,81, 136,137, 0 };
static const uint8_t lbTitleBot[] = {
    94,95, 98,99, 90,91, 94,95, 32, 138,139, 74,75, 122,123, 134,135, 82,83, 138,139, 0 };
static const uint8_t lbTableColors[LB_ENTRIES_COUNT] = { 2,7,14,5,15,8,4,9,13,10 };
static const char *lbRanks[LB_ENTRIES_COUNT] = {
    "    1st","    2nd","    3rd","    4th","    5th","    6th","    7th","    8th","    9th","   10th" };

static void reset_leaderboard(void) {
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) {
        uint8_t j = 0; const char *n = lbDefaultNames[i];
        while (*n && j < LB_NAME_LENGTH) leaderboard_names[i][j++] = *n++;
        leaderboard_names[i][j] = 0;
        leaderboard_saved[i] = 10 - i;
        leaderboard_times[i*2] = 20 - i*2; leaderboard_times[i*2+1] = 0;
        leaderboard_start[i] = i + 1;
    }
    leaderboard_start_high = 6;
}

#define LB_STORAGE_SIZE 163

static void save_leaderboard(void) {
    uint8_t buf[LB_STORAGE_SIZE];
    uint16_t p = 0;
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) {
        for (uint8_t j = 0; j <= LB_NAME_LENGTH; j++) {
            buf[p++] = leaderboard_names[i][j];
        }
    }
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) buf[p++] = leaderboard_saved[i];
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT * 2; i++) buf[p++] = leaderboard_times[i];
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) buf[p++] = leaderboard_start[i];
    buf[p++] = leaderboard_start_high;
    buf[p++] = 0x48; /* 'H' */
    buf[p++] = 0x53; /* 'S' */
    disk_write_hiscore(buf, sizeof(buf));
}

static void load_leaderboard(void) {
    uint8_t buf[LB_STORAGE_SIZE];
    disk_read_hiscore(buf, sizeof(buf));
    if (buf[161] == 0x48 && buf[162] == 0x53) {
        uint16_t p = 0;
        for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) {
            for (uint8_t j = 0; j <= LB_NAME_LENGTH; j++) {
                leaderboard_names[i][j] = buf[p++];
            }
            leaderboard_names[i][LB_NAME_LENGTH] = 0;
        }
        for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) leaderboard_saved[i] = buf[p++];
        for (uint8_t i = 0; i < LB_ENTRIES_COUNT * 2; i++) leaderboard_times[i] = buf[p++];
        for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) leaderboard_start[i] = buf[p++];
        leaderboard_start_high = buf[p++];
        if (leaderboard_start_high == 0 || leaderboard_start_high > 10) leaderboard_start_high = 6;
    } else {
        reset_leaderboard();
        save_leaderboard();
    }
}

static uint8_t get_saved_miners_count(void) {
    return (uint8_t)(level - startLevel + levelCompleted);
}
static void print_leaderboard(void) {
    /* Headings. */
    text_rowstr(LB_ROW,     9, lbTitleTop, 7);
    text_rowstr(LB_ROW + 1, 9, lbTitleBot, 7);
    text_print(LB_ROW + 3, 0, "                    saved       start", 1);
    text_print(LB_ROW + 4, 0, "   rank name        miners time level", 1);
    /* Entries: rank/name/saved/time/start. */
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) {
        uint8_t row = LB_ROW + LB_HEADING_COUNT + i * 2;
        uint8_t col = lbTableColors[i];
        text_print(row, 0, lbRanks[i], col);
        text_print(row, LB_NAME_COL, (const char *)leaderboard_names[i], col);
        text_shortnum(row, LB_MINERS_COL, leaderboard_saved[i], col);
        text_time(row, LB_TIME_COL, leaderboard_times[i*2], leaderboard_times[i*2+1], col);
        text_shortnum(row, LB_START_COL, leaderboard_start[i], col);
    }
}
/* Returns zero-indexed rank, or LB_ENTRIES_COUNT if not a new high score. */
static uint8_t get_high_score_rank(void) {
    uint8_t miners = get_saved_miners_count();
    uint8_t tm = minutes, ts = seconds;
    for (uint8_t i = 0; i < LB_ENTRIES_COUNT; i++) {
        if (leaderboard_saved[i] < miners) return i;
        if (leaderboard_saved[i] == miners) {
            if (tm < leaderboard_times[i*2] || (tm == leaderboard_times[i*2] && ts < leaderboard_times[i*2+1])) return i;
        }
    }
    return LB_ENTRIES_COUNT;
}
static void make_room(uint8_t rank) {
    for (uint8_t i = LB_ENTRIES_COUNT - 2; i >= rank && i != 255; i--) {
        for (uint8_t j = 0; j <= LB_NAME_LENGTH; j++) leaderboard_names[i+1][j] = leaderboard_names[i][j];
        leaderboard_saved[i+1] = leaderboard_saved[i];
        leaderboard_start[i+1] = leaderboard_start[i];
        leaderboard_times[(i+1)*2] = leaderboard_times[i*2];
        leaderboard_times[(i+1)*2+1] = leaderboard_times[i*2+1];
        if (i == 0) break;
    }
}
static void set_new_high_score(uint8_t rank) {
    make_room(rank);
    leaderboard_saved[rank] = newminers;
    leaderboard_start[rank] = newstart;
    leaderboard_times[rank*2] = newtimeLo;
    leaderboard_times[rank*2+1] = newtimeHi;
}

/* ------------------------------ High score entry (InputString) ------------------------------ */
static uint8_t  hsName[LB_NAME_LENGTH + 1];
static uint8_t  hsLen;
static uint8_t  hsCursor;
static uint8_t  hsBlink;
static void init_high_score_input(void) {
    newminers = get_saved_miners_count();
    newtimeLo = minutes; newtimeHi = seconds; newstart = startLevel;
    newrank = get_high_score_rank();
    set_new_high_score(newrank);
    hsLen = 0; hsCursor = 0; hsBlink = 0;
    for (uint8_t i = 0; i < LB_NAME_LENGTH; i++) hsName[i] = ' ';
    hsName[LB_NAME_LENGTH] = 0;
    text_clear();
    print_leaderboard();
}
/* Returns 1 when finished, 0 while still entering. */
static uint8_t high_score_input(void) {
    uint8_t row = LB_ROW + LB_HEADING_COUNT + newrank * 2;
    uint8_t col = lbTableColors[newrank];
    uint8_t k = inputPollKey();
    if (k) {
        if (k == 0x0D || k == '1') {   /* return/start finishes */
            for (uint8_t i = 0; i < LB_NAME_LENGTH; i++) leaderboard_names[newrank][i] = hsName[i];
            leaderboard_names[newrank][LB_NAME_LENGTH] = 0;
            if (level >= leaderboard_start_high) leaderboard_start_high = level;
            save_leaderboard();
            return 1;
        }
        if (k == 0x08 || k == 0x7F) {  /* backspace/delete */
            if (hsLen > 0) { hsLen--; hsName[hsLen] = ' '; }
        } else {
            uint8_t c = k;
            if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
                if (hsLen < LB_NAME_LENGTH) { hsName[hsLen] = c; hsLen++; }
            }
        }
        kbd_clear();
    }
    /* Render name and blinking underscore cursor with transparent background (row color). */
    hsBlink++;
    for (uint8_t i = 0; i < LB_NAME_LENGTH; i++) {
        uint8_t cpos = LB_NAME_COL + i;
        if (i == hsLen && (hsBlink & 16)) {
            text_putc(row, cpos, 0x5F, col);
        } else {
            text_putc(row, cpos, hsName[i], col);
        }
    }
    return 0;
}

/* ------------------------------ State machine ------------------------------ */
static void init_menu_background(void) {
    level = 0;
    init_level();
    set_layer0_tile_mode();
    init_creatures();
    init_player();
    /* Menu shows the level-0 demo scene anchored at the map's top-left
     * (hscroll=0,vscroll=0), NOT centered on the player like gameplay. */
    camxpos = SCREENWIDTH/2; camypos = SCREENHEIGHT/2;
    VERA.layer0.hscroll = camxpos - SCREENWIDTH/2;   /* = 0 */
    VERA.layer0.vscroll = camypos - SCREENHEIGHT/2;  /* = 0 */
    hide_player();
    VERA.display.video = 0x71;
}
static void restart_game(void) {
    hide_player();
    hide_creatures();
    uint8_t rank = get_high_score_rank();
    if (rank < LB_ENTRIES_COUNT) {
        audioPlayMusic(MUSIC_HIGHSCORE);
        init_high_score_input();
        gameStatus = ST_ENTERHIGHSCORE;
    } else {
        gameStatus = ST_INITMENU;
    }
}

static void level_tick(void) {
    /* Check pause. */
    static uint8_t startbtn;
    if ((joy() & JOY_START) == 0) { if (startbtn) { audioStopSfx(SFX_ENGINE); show_pause_menu(); gameStatus = ST_PAUSED; startbtn = 0; return; } }
    else startbtn = 1;

    update_view();
    player_tick();
    update_player_sprite();
    update_creatures();
    time_tick();
    light_up_level();
    update_status_bar();

    /* Laser update. Stop the laser sound once the beam expires. */
    if (laserEnabled) {
        laserTime++;
        laserFrame = (uint8_t)((laserFrame + 1) & 7);
        check_laser_creature();
        if (laserTime >= LASER_FIRE_TIME) {
            laserEnabled = 0;
            audioStopSfx(SFX_LASER);
        }
    }
    update_laser_sprites();

    /* Collision: player-miner completes level, player-creature dies. */
    uint8_t pc = check_player_creature();
    if (pc == 2) {
        levelCompleted = 1;
        gameStatus = ST_LEVELCOMPLETED;
    } else if (pc == 1) {
        abort_explosion();
        laserEnabled = 0;
        audioStopSfx(SFX_LASER); audioStopSfx(SFX_ENGINE);
        audioPlayMusic(MUSIC_KILLED);
        gameStatus = ST_DEATH_CREATURE;
        deathDelay = DEAD_DELAY;
    }

    /* Tile collision (lava). */
    if (invulnerableTimer == 0) {
        uint8_t cat = check_tile(xpos + 8, ypos + 8);
        if (cat == TILECAT_DEATH) {
            abort_explosion();
            laserEnabled = 0;
            audioStopSfx(SFX_LASER); audioStopSfx(SFX_ENGINE);
            audioPlayMusic(MUSIC_KILLED);
            gameStatus = ST_DEATH_LAVA;
            deathDelay = DEAD_DELAY;
        }
    }
}

static void game_tick(void) {
    audioServiceAudio();
    inputUpdate();

    /* Paused: freeze sound + only update pause menu. */
    if (gameStatus == ST_PAUSED) {
        uint8_t sel = update_pause_menu();
        if (sel == 0) { text_clear(); update_status_bar(); audioStopSfx(SFX_LASER); gameStatus = ST_RUNNING; }
        else if (sel == 1) { hide_player(); hide_creatures(); audioStopSfx(SFX_ENGINE); gameStatus = ST_INITMENU; }
        return;
    }

    switch (gameStatus) {
    case ST_INITSTARTSCREEN:
    case ST_INITMENU:
        init_menu_background();
        show_menu_screen();
        menumode = M_HANDLE_INPUT;
        audioPlayMusic(MUSIC_TITLE);
        gameStatus = ST_SHOWMENU;
        break;
    case ST_SHOWMENU:
        menu_handler();
        if (gameStatus != ST_SHOWMENU) break;   /* start game / quit changed it */
        update_creatures();
        break;
    case ST_INITGAME:
        audioStopMusic();
        lives = LIFE_COUNT;
        level = startLevel;
        levelCompleted = 0;
        minutes = seconds = jiffies = 0;
        lastLevelMinutes = lastLevelSeconds = 0;
        gameStatus = ST_INITLEVEL;
        break;
    case ST_INITLEVEL:
        invulnerableTimer = 0;
        lastKillerCreature = 0xFF;
        init_level();
        init_creatures();
        init_player();
        set_layer0_tile_mode();
        text_clear();
        update_view();
        update_status_bar();
        show_player();
        VERA.display.video = 0x71;
        gameStatus = ST_RUNNING;
        break;
    case ST_RESUMEGAME:
        show_player();
        update_view();
        gameStatus = ST_RUNNING;
        break;
    case ST_RUNNING:
        level_tick();
        break;
    case ST_DEATH_CREATURE:
    case ST_DEATH_EXPLOSION:
    case ST_DEATH_LAVA:
        update_player_sprite();
        if (deathDelay > 0) { deathDelay--; break; }
        if (gameStatus == ST_DEATH_CREATURE) {
            if (lastKillerCreature < creatureCount) {
                creatureLife[lastKillerCreature] = CREATURE_DEAD;
                hide_sprite((uint16_t)(CREATURE_ADDR_L + (uint16_t)lastKillerCreature * 8));
            }
        } else if (gameStatus == ST_DEATH_LAVA) {
            move_player_back();
        }
        lives--;
        if (lives == 0) { gameStatus = ST_GAMEOVER; }
        else { gameStatus = ST_RESTARTLEVEL; }
        break;
    case ST_RESTARTLEVEL:
        invulnerableTimer = 120;
        isFalling = 0;
        isFlying = 1;
        flyingspeed = MIN_FLYINGSPEED;
        fallingspeed = MIN_FALLINGSPEED;
        update_view();
        update_status_bar();
        show_player();
        gameStatus = ST_RUNNING;
        break;
    case ST_LEVELCOMPLETED:
        turn_on_light();
        audioStopSfx(SFX_ENGINE);
        audioStopSfx(SFX_LASER);
        lastLevelMinutes = minutes;
        lastLevelSeconds = seconds;
        if (level == LEVEL_COUNT) { print_game_completed(); audioPlayMusic(MUSIC_HIGHSCORE); gameStatus = ST_GAMECOMPLETED; gameCompletedDelay = 240; }
        else { print_level_finished(); audioPlayMusic(MUSIC_LEVELCOMPLETE); gameStatus = ST_LEVELCOMPLETED2; levelCompletedDelay = 180; }
        break;
    case ST_LEVELCOMPLETED2:
        if (levelCompletedDelay > 0) levelCompletedDelay--;
        else { level++; gameStatus = ST_INITLEVEL; }
        break;
    case ST_GAMECOMPLETED:
        if (gameCompletedDelay > 0) gameCompletedDelay--;
        else gameStatus = ST_GAMECOMPLETED2;
        break;
    case ST_GAMECOMPLETED2:
        if (joy() != JOY_NOTHING_PRESSED) restart_game();
        break;
    case ST_GAMEOVER:
        turn_on_light();
        audioStopSfx(SFX_ENGINE);
        audioPlayMusic(MUSIC_GAMEOVER);
        print_game_over();
        gameStatus = ST_GAMEOVER2;
        gameOverDelay = 100;
        break;
    case ST_GAMEOVER2:
        if (gameOverDelay > 0) gameOverDelay--;
        else gameStatus = ST_GAMEOVER3;
        break;
    case ST_GAMEOVER3:
        if (gameOverDelay > 0) gameOverDelay--;
        else {
            minutes = lastLevelMinutes; seconds = lastLevelSeconds;
            restart_game();
        }
        break;
    case ST_ENTERHIGHSCORE:
        if (high_score_input()) {
            init_menu_background();
            show_menu_screen();
            menumode = M_HANDLE_INPUT;
            audioPlayMusic(MUSIC_TITLE);
            gameStatus = ST_SHOWMENU;
        }
        break;
    case ST_QUITGAME:
        audioCleanup();
        VERA.display.video = 0x00;   /* disable VERA video output */
        /* Return to standard Apple II text mode page 1 */
        *(volatile uint8_t *)0xC051 = 0; /* TXTSET */
        *(volatile uint8_t *)0xC054 = 0; /* PAGE1 */
        *(volatile uint8_t *)0xC056 = 0; /* LORES */
        *(volatile uint8_t *)0xC058 = 0; /* CLRAN0 */
        *(volatile uint8_t *)0xC00C = 0; /* 80STORE off */
        *(volatile uint8_t *)0xC00E = 0; /* ALTCHAR off */
        mlib_quit();
        while (1) {}
        break;
    default:
        break;
    }
}

/* ------------------------------ Entry point ------------------------------ */
int main(void) {
    if (!detect_vera()) {
        const char *msg = "ERROR: NO VERA CARD DETECTED IN SLOT 2!";
        for (uint8_t i = 0; msg[i]; i++) ((volatile unsigned char *)0x0400)[i] = msg[i] | 0x80;
        while (1) {}
    }

    VERA.control = 0x80;
    VERA.control = 0x00;
    VERA.display.video = 0x71;   /* layer 0 + layer 1 + sprites on */
    VERA.display.hscale = 0x40;
    VERA.display.vscale = 0x40;

    disk_init();
    init_screen();
    load_resources();
    audioInit();
    inputInit();
    load_leaderboard();
    startLevel = 1;

    init_menu_background();
    show_menu_screen();
    menumode = M_HANDLE_INPUT;

    audioPlayMusic(MUSIC_TITLE);
    gameStatus = ST_SHOWMENU;

    /* Prime VSYNC flag. */
    VERA.irq_flags = VERA_IRQ_VSYNC;

    for (;;) {
        while ((VERA.irq_flags & VERA_IRQ_VSYNC) == 0) {}
        VERA.irq_flags = VERA_IRQ_VSYNC;
        frameCount++;
        game_tick();
    }
}
