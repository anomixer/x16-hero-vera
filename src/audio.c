//-----------------------------------------------------------------------------
// audio.c — VERA 16-channel PSG engine for x16-hero (Apple II VERA port).
//
// Two subsystems, both driven by audioServiceAudio() at 60 Hz:
//   1. Sound effects — step-tables from sfx_table.h (ported from soundfx.asm).
//   2. Background music — ZSM converted to a compact PSG register stream
//      (gen_music.mjs), stored in VRAM Bank 1 and decoded here each frame.
//      Zero disk access during play (all tracks preloaded into VRAM at boot).
//-----------------------------------------------------------------------------
#include <stdint.h>
#include "apple2e.h"
#include "audio.h"
#include "sfx_table.h"
#include "music_table.h"

#define PSG_BASE 0xF9C0   /* in VRAM Bank 1: $1F9C0 */

/* Music blob lives in VRAM Bank 1.  The blob starts at full address $10000,
 * whose 16-bit part is $0000; gen_music.mjs packs tracks with musicTracks[]. */
#define MUSIC_VRAM_ADDR 0x0000
#define MUSIC_VRAM_BANK 1

/* --- PSG low-level helpers --- */
static void psg_write(uint8_t ch, uint16_t freq, uint8_t pan_vol, uint8_t wave_pw) {
    VERA.control = 0;
    uint16_t addr = PSG_BASE + (uint16_t)ch * 4;
    vera_set_addr(VERA_INC_BANK1, addr);
    VERA.data0 = (uint8_t)(freq & 0xFF);
    VERA.data0 = (uint8_t)((freq >> 8) & 0x3F);
    VERA.data0 = pan_vol;
    VERA.data0 = wave_pw;
}

static void psg_silence(uint8_t ch) {
    VERA.control = 0;
    uint16_t addr = PSG_BASE + (uint16_t)ch * 4;
    vera_set_addr(VERA_INC_BANK1, addr);
    VERA.data0 = 0x00;  /* freq_lo = 0 */
    VERA.data0 = 0x00;  /* freq_hi = 0 */
    VERA.data0 = 0x00;  /* pan = 0, vol = 0 */
    VERA.data0 = 0x00;  /* wave = 0, pw = 0 */
}

static void psg_silence_all(void) {
    VERA.control = 0;
    vera_set_addr(VERA_INC_BANK1, PSG_BASE);
    for (uint8_t i = 0; i < 64; i++) VERA.data0 = 0x00;
}

static uint8_t vram_peek(uint16_t addr) {
    vera_set_addr(VERA_INC_BANK0, addr);
    return VERA.data0;
}

static uint8_t vram_peek_bank(uint8_t bank, uint16_t addr) {
    vera_set_addr(bank ? VERA_INC_BANK1 : VERA_INC_BANK0, addr);
    return VERA.data0;
}

/* ============================ Sound effects ============================ */
/* Playing table: one byte per sfx (0=off, 1=on). */
#define SFX_ENGINE       0
#define SFX_LASER        1
#define SFX_CREATUREDEAD 2
#define SFX_EXPLOSION    3
#define SFX_PLAYERDEAD   4
#define SFX_FINISHED1    5
#define SFX_FINISHED2    6
#define SFX_COUNT        7

static uint8_t  sfxPlaying[SFX_COUNT] = {0};
static uint8_t  sfxIndex[SFX_COUNT]   = {0};
static uint8_t  sfxDelay[SFX_COUNT]   = {0};
static uint8_t  engineSpeed           = 0;  /* added to engine freq_lo */

void audioSetEngineSpeed(uint8_t speed) { engineSpeed = speed; }

/* Read one step of a sfx table and write the 4 PSG registers for its voice. */
static void sfxStep(uint8_t sfxId, const SfxStep *steps, uint8_t len, uint8_t voice,
                    uint8_t repeat) {
    uint8_t idx = sfxIndex[sfxId];
    if (idx >= len) {
        if (repeat) { sfxIndex[sfxId] = 0; idx = 0; }
        else { sfxPlaying[sfxId] = 0; psg_silence(voice); return; }
    }
    const SfxStep *s = &steps[idx];
    uint16_t freq = (uint16_t)s->freq_lo | ((uint16_t)s->freq_hi << 8);
    if (sfxId == SFX_ENGINE) freq += (uint16_t)engineSpeed * 2;
    psg_write(voice, freq, s->pan_vol, s->wave_pw);
    sfxDelay[sfxId] = s->delay;
    sfxIndex[sfxId] = idx + 1;
}

void audioPlaySfx(uint8_t sfxId) { sfxPlaying[sfxId] = 1; sfxIndex[sfxId] = 0; sfxDelay[sfxId] = 0; }
void audioStopSfx(uint8_t sfxId) {
    if (sfxId >= SFX_COUNT || !sfxPlaying[sfxId]) return;
    sfxPlaying[sfxId] = 0;
    switch (sfxId) {
    case SFX_ENGINE:       psg_silence(ENGINE_VOICE); break;
    case SFX_LASER:        psg_silence(LASER_VOICE); break;
    case SFX_CREATUREDEAD: psg_silence(CREATUREDEAD_VOICE); break;
    case SFX_EXPLOSION:    psg_silence(EXPLOSION_VOICE); break;
    case SFX_PLAYERDEAD:   psg_silence(PLAYERDEAD_VOICE); break;
    case SFX_FINISHED1:    psg_silence(FINISHED_VOICE1); break;
    case SFX_FINISHED2:    psg_silence(FINISHED_VOICE2); break;
    }
}
/* Start a looping sfx only if it is not already playing (original PlayEngineSound
 * is idempotent: calling it every frame must not restart the engine hum). */
void audioStartSfxIfIdle(uint8_t sfxId) {
    if (!sfxPlaying[sfxId]) { sfxPlaying[sfxId] = 1; sfxIndex[sfxId] = 0; sfxDelay[sfxId] = 0; }
}

static void sfxTick(void) {
    for (uint8_t i = 0; i < SFX_COUNT; i++) {
        if (!sfxPlaying[i]) continue;
        if (sfxDelay[i]) { sfxDelay[i]--; continue; }
        switch (i) {
        case SFX_ENGINE:       sfxStep(i, engineFx, ENGINE_LENGTH, ENGINE_VOICE, 1); break;
        case SFX_LASER:        sfxStep(i, laserFx, LASER_LENGTH, LASER_VOICE, 1); break;
        case SFX_CREATUREDEAD: sfxStep(i, creatureDeadFx, CREATUREDEAD_LENGTH, CREATUREDEAD_VOICE, 0); break;
        case SFX_EXPLOSION:    sfxStep(i, explosionFx, EXPLOSION_LENGTH, EXPLOSION_VOICE, 0); break;
        case SFX_PLAYERDEAD:   sfxStep(i, playerDeadFx, PLAYERDEAD_LENGTH, PLAYERDEAD_VOICE, 0); break;
        case SFX_FINISHED1:    sfxStep(i, finished1Fx, FINISHED_LENGTH, FINISHED_VOICE1, 0); break;
        case SFX_FINISHED2:    sfxStep(i, finished2Fx, FINISHED_LENGTH, FINISHED_VOICE2, 0); break;
        }
    }
}

/* ============================ Background music ============================ */
static uint8_t  musicActive  = 0;
static uint16_t musicOffset  = 0;   /* offset within the blob */
static uint16_t musicEnd     = 0;   /* end offset of current track */
static uint16_t musicBase    = 0;   /* track start offset (for looping) */
static uint16_t musicLoopOffset = 0;/* seamless loop target offset */
static uint8_t  musicLoop    = 0;
static uint8_t  musicDelay   = 0;

void audioPlayMusic(uint8_t trackIdx) {
    if (trackIdx >= MUSIC_TRACK_COUNT) return;
    const MusicTrack *t = &musicTracks[trackIdx];
    musicBase       = t->offset;
    musicOffset     = t->offset;
    musicEnd        = t->offset + t->length;
    musicLoopOffset = t->offset + t->loop_offset;
    musicDelay      = 0;
    musicActive     = 1;
    /* Menu/title and highscore music loop; jingles play once. */
    musicLoop   = (trackIdx == MUSIC_TITLE || trackIdx == MUSIC_HIGHSCORE) ? 1 : 0;
    /* Silence all 16 channels and registers completely to avoid residual pops/noise. */
    psg_silence_all();
}

void audioStopMusic(void) {
    musicActive = 0;
    musicLoop = 0;
    psg_silence_all();
}

/* Decode the register stream: { 0x80|voice*4+reg, value } pairs + delay bytes.
 * Uses VERA Port 0 (auto-incrementing data0) to stream music bytes and Port 1
 * (data1) to write PSG registers, eliminating hundreds of address resets per frame. */
static void musicTick(void) {
    if (!musicActive) return;
    if (musicDelay) { musicDelay--; return; }

    /* Set Port 0 to stream music data sequentially with 1-byte auto-increment. */
    VERA.control = 0;
    vera_set_addr(VERA_INC_BANK1, MUSIC_VRAM_ADDR + musicOffset);

    /* Switch to Port 1 for writing to PSG registers in Bank 1. */
    VERA.control = 1;
    VERA.address_hi = VERA_INC_BANK1;

    while (musicDelay == 0) {
        if (musicOffset >= musicEnd) {
            if (musicLoop) {
                musicOffset = musicLoopOffset;
                musicDelay = 0;
                break;
            }
            musicActive = 0;
            psg_silence_all();
            break;
        }

        uint8_t b = VERA.data0;   /* reads from Port 0 and auto-increments */
        musicOffset++;

        if (b == 0xFF) {   /* end of track */
            if (musicLoop) {
                musicOffset = musicLoopOffset;
                musicDelay = 0;
                break;
            }
            musicActive = 0;
            psg_silence_all();
            break;
        }

        if (b >= 0x80) {
            uint8_t cmd = b & 0x3F;
            uint8_t value = VERA.data0;   /* reads value from Port 0 and auto-increments */
            musicOffset++;
            VERA.address = PSG_BASE + cmd; /* updates Port 1 address */
            VERA.data1 = value;           /* writes to PSG register */
        } else {
            musicDelay = b;   /* wait this many frames */
            break;
        }
    }

    /* Restore Port 0 selection for the rest of the engine. */
    VERA.control = 0;
}

void audioServiceAudio(void) {
    sfxTick();
    musicTick();
}

void audioInit(void) {
    for (uint8_t i = 0; i < SFX_COUNT; i++) { sfxPlaying[i] = 0; sfxIndex[i] = 0; sfxDelay[i] = 0; }
    musicActive = 0;
    engineSpeed = 0;
    psg_silence_all();
}

void audioCleanup(void) { audioInit(); }
