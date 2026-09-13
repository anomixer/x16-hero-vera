// sfx_table.h — PSG sound-effect register tables ported from x16-hero
// view/soundfx.asm.  Each step: freq_lo, freq_hi, pan_vol, wave_pw, delay.
// Voice numbers and lengths mirror the assembly constants.
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t freq_lo;
    uint8_t freq_hi;
    uint8_t pan_vol;
    uint8_t wave_pw;
    uint8_t delay;
} SfxStep;

// Voice assignment (MASTER_VOICE = 0)
#define ENGINE_VOICE        0
#define LASER_VOICE         1
#define CREATUREDEAD_VOICE  2
#define EXPLOSION_VOICE     3
#define PLAYERDEAD_VOICE    4
#define FINISHED_VOICE1     4
#define FINISHED_VOICE2     5

#define ENGINE_LENGTH       4
#define LASER_LENGTH        8
#define CREATUREDEAD_LENGTH 5
#define EXPLOSION_LENGTH    12
#define PLAYERDEAD_LENGTH   4
#define FINISHED_LENGTH     4

// PULSE=0, SAW=64, TRIANGLE=128, NOISE=192
#define SFX_PULSE   0
#define SFX_SAW     64
#define SFX_TRIANGLE 128
#define SFX_NOISE   192
// RIGHT_PAN=64, LEFT_PAN=128, BOTH_PAN=192
#define SFX_RIGHT_PAN 64
#define SFX_LEFT_PAN  128
#define SFX_BOTH_PAN  192

static const SfxStep engineFx[ENGINE_LENGTH] = {
    { 74, 0, SFX_BOTH_PAN+48, SFX_PULSE+10, 4 },
    { 74, 0, SFX_BOTH_PAN+48, SFX_PULSE+20, 4 },
    { 74, 0, SFX_BOTH_PAN+48, SFX_PULSE+10, 4 },
    { 74, 0, SFX_BOTH_PAN+48, SFX_PULSE+20, 4 },
};

static const SfxStep laserFx[LASER_LENGTH] = {
    {200, 5, SFX_BOTH_PAN+50, SFX_PULSE+15,    2 },
    {180, 5, SFX_BOTH_PAN+50, SFX_SAW+25,      2 },
    {160, 5, SFX_BOTH_PAN+50, SFX_PULSE+35,    2 },
    {140, 5, SFX_BOTH_PAN+50, SFX_TRIANGLE+45, 2 },
    {120, 5, SFX_BOTH_PAN+50, SFX_PULSE+35,    2 },
    {100, 5, SFX_BOTH_PAN+50, SFX_SAW+25,      2 },
    { 80, 5, SFX_BOTH_PAN+50, SFX_PULSE+15,    2 },
    { 60, 5, SFX_BOTH_PAN+50, SFX_TRIANGLE+25, 2 },
};

static const SfxStep creatureDeadFx[CREATUREDEAD_LENGTH] = {
    {249,12, SFX_BOTH_PAN+63, SFX_NOISE+10, 4 },
    {249,12, SFX_BOTH_PAN+48, SFX_NOISE+20, 4 },
    {249,12, SFX_BOTH_PAN+32, SFX_NOISE+30, 4 },
    {249,12, SFX_BOTH_PAN+32, SFX_NOISE+20, 4 },
    {249,12, SFX_BOTH_PAN+24, SFX_NOISE+10, 4 },
};

static const SfxStep explosionFx[EXPLOSION_LENGTH] = {
    {249,10, SFX_BOTH_PAN+63, SFX_NOISE+10, 8 },
    {249,10, SFX_BOTH_PAN+63, SFX_NOISE+20, 8 },
    {249,10, SFX_BOTH_PAN+48, SFX_NOISE+30, 8 },
    {249,10, SFX_BOTH_PAN+48, SFX_NOISE+20, 8 },
    {249,10, SFX_BOTH_PAN+32, SFX_NOISE+10, 8 },
    {249,10, SFX_BOTH_PAN+32, SFX_NOISE+20, 8 },
    {249,10, SFX_BOTH_PAN+24, SFX_NOISE+30, 8 },
    {249,10, SFX_BOTH_PAN+24, SFX_NOISE+20, 8 },
    {249,10, SFX_BOTH_PAN+16, SFX_NOISE+10, 8 },
    {249,10, SFX_BOTH_PAN+16, SFX_NOISE+20, 8 },
    {249,10, SFX_BOTH_PAN+ 8, SFX_NOISE+30, 8 },
    {249,10, SFX_BOTH_PAN+ 8, SFX_NOISE+20, 8 },
};

static const SfxStep playerDeadFx[PLAYERDEAD_LENGTH] = {
    { 14,2, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 24 },
    {178,1, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 12 },
    { 14,2, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 12 },
    { 95,1, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 48 },
};

static const SfxStep finished1Fx[FINISHED_LENGTH] = {
    { 28,4, SFX_LEFT_PAN+63, SFX_TRIANGLE+52, 24 },
    {117,3, SFX_LEFT_PAN+63, SFX_TRIANGLE+52, 12 },
    { 28,4, SFX_LEFT_PAN+63, SFX_TRIANGLE+52, 12 },
    {125,5, SFX_LEFT_PAN+63, SFX_TRIANGLE+52, 48 },
};

static const SfxStep finished2Fx[FINISHED_LENGTH] = {
    { 14,2, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 24 },
    {178,1, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 12 },
    { 14,2, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 12 },
    {190,2, SFX_RIGHT_PAN+63, SFX_TRIANGLE+52, 48 },
};
