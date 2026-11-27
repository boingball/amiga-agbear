/* agb_audio.h - AG Bear samples (audio.device) and speech (narrator.device, optional) */
#ifndef AGB_AUDIO_H
#define AGB_AUDIO_H

#include <exec/types.h>

/* sample groups, picked at random inside a group */
typedef enum { SG_GREETING, SG_GROWL, SG_HAPPY, SG_NOM, SG_NO, SG_YES, SG_WAKEUP, SG_WAVE, SG_ANY } SoundGroup;

BOOL Snd_Init(void);
void Snd_Close(void);
/* pitch: 256 = normal, larger = higher */
void Snd_Play(UWORD sample, UWORD pitch, UWORD volume);
void Snd_PlayGroup(SoundGroup g, UWORD pitch);
void Snd_PlayPart(UWORD sample, ULONG offset, ULONG length, UWORD pitch, UWORD volume);
/* a short random snippet of a bear sample - "bear babble" for one word */
void Snd_Babble(UWORD pitch);
BOOL Snd_Busy(void);
void Snd_Stop(void);
void Snd_Update(void);   /* call every frame */

/* narrator voices */
typedef enum { VOICE_BEAR, VOICE_NORMAL, VOICE_CHIPMUNK, VOICE_ROBOT, VOICE_GIANT, VOICE_FRIEND,
               VOICE_COUNT } VoiceKind;

BOOL Speech_Init(void);         /* FALSE when narrator.device / translator.library are missing */
BOOL Speech_Available(void);
void Speech_Say(const char *text, VoiceKind voice, UWORD pitchShift);
BOOL Speech_Busy(void);
void Speech_Stop(void);
void Speech_Update(void);
void Speech_Close(void);
/* "Bearify": turns a sentence into AG Bear sounds ("waaw rarr") - caller frees with FreeVec */
char *Speech_Bearify(const char *in);

#endif
