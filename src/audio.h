#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>

void audioInit(void);
void audioCleanup(void);
void audioServiceAudio(void);
void audioSetEngineSpeed(uint8_t speed);

/* Music tracks (indices into musicTracks[]). */
#define MUSIC_TITLE    0
#define MUSIC_HIGHSCORE 1
#define MUSIC_GAMEOVER 2
#define MUSIC_KILLED   3
#define MUSIC_LEVELCOMPLETE 4
void audioPlayMusic(uint8_t trackIdx);
void audioStopMusic(void);

/* Sound effects (indices into sfxPlaying). */
#define SFX_ENGINE       0
#define SFX_LASER        1
#define SFX_CREATUREDEAD 2
#define SFX_EXPLOSION    3
#define SFX_PLAYERDEAD   4
#define SFX_FINISHED1    5
#define SFX_FINISHED2    6
void audioPlaySfx(uint8_t sfxId);
void audioStopSfx(uint8_t sfxId);
void audioStartSfxIfIdle(uint8_t sfxId);

#endif
