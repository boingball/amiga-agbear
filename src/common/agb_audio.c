/* agb_audio.c - see agb_audio.h */
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/audio.h>
#include <devices/narrator.h>
#include <libraries/translator.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/translator.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "agb_audio.h"
#include "../gen/sounds_data.h"

extern struct GfxBase *GfxBase;
extern void BeginIO(struct IORequest *);

/* ------------------------------------------------------------------ samples */
static struct MsgPort *audPort = NULL;
static struct IOAudio *audReq = NULL;
static BOOL audOpen = FALSE, audBusy = FALSE;
static BYTE *chipSamples = NULL;
static ULONG sampleOff[SND_COUNT];
static UBYTE chanMasks[] = { 8, 4, 2, 1 };   /* leave 0+1 / 2+3 pairs to narrator.device */

static const UBYTE groupOf[SND_COUNT] = {
    SG_GREETING, SG_GREETING, SG_GROWL, SG_GROWL, SG_HAPPY, SG_HAPPY, SG_HAPPY, SG_GREETING,
    SG_NO, SG_NO, SG_NOM, SG_NOM, SG_WAKEUP, SG_WAVE, SG_YES
};

BOOL Snd_Init(void)
{
    ULONG total = 0, i;
    for (i = 0; i < SND_COUNT; i++) total += bear_samples[i].length;
    chipSamples = AllocVec(total, MEMF_CHIP);
    if (!chipSamples) return FALSE;
    total = 0;
    for (i = 0; i < SND_COUNT; i++) {
        sampleOff[i] = total;
        CopyMem((APTR)bear_samples[i].data, chipSamples + total, bear_samples[i].length);
        total += bear_samples[i].length;
    }
    if (!(audPort = CreateMsgPort())) return FALSE;
    if (!(audReq = (struct IOAudio *)CreateIORequest(audPort, sizeof(struct IOAudio)))) return FALSE;
    audReq->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audReq->ioa_Data = chanMasks;
    audReq->ioa_Length = sizeof(chanMasks);
    if (OpenDevice((STRPTR)"audio.device", 0, (struct IORequest *)audReq, 0)) return FALSE;
    audOpen = TRUE;
    return TRUE;
}

void Snd_Stop(void)
{
    if (audOpen && audBusy) {
        AbortIO((struct IORequest *)audReq);
        WaitIO((struct IORequest *)audReq);
        audBusy = FALSE;
    }
}

void Snd_Close(void)
{
    Snd_Stop();
    if (audOpen) { CloseDevice((struct IORequest *)audReq); audOpen = FALSE; }
    if (audReq) { DeleteIORequest((struct IORequest *)audReq); audReq = NULL; }
    if (audPort) { DeleteMsgPort(audPort); audPort = NULL; }
    if (chipSamples) { FreeVec(chipSamples); chipSamples = NULL; }
}

static void PlayRaw(const BYTE *data, ULONG len, UWORD rate, UWORD pitch, UWORD volume)
{
    ULONG clock = (GfxBase->DisplayFlags & PAL) ? 3546895UL : 3579545UL;
    ULONG period;
    if (!audOpen || len < 2) return;
    Snd_Stop();
    if (pitch < 64) pitch = 64;
    period = (clock * 256UL) / ((ULONG)rate * pitch);
    if (period < 124) period = 124;
    audReq->ioa_Request.io_Command = CMD_WRITE;
    audReq->ioa_Request.io_Flags = ADIOF_PERVOL;
    audReq->ioa_Data = (UBYTE *)data;
    audReq->ioa_Length = len & ~1UL;
    audReq->ioa_Period = (UWORD)period;
    audReq->ioa_Volume = volume > 64 ? 64 : volume;
    audReq->ioa_Cycles = 1;
    BeginIO((struct IORequest *)audReq);
    audBusy = TRUE;
}

void Snd_Play(UWORD s, UWORD pitch, UWORD volume)
{
    if (s >= SND_COUNT || !chipSamples) return;
    PlayRaw(chipSamples + sampleOff[s], bear_samples[s].length, bear_samples[s].rate, pitch, volume);
}

void Snd_PlayPart(UWORD s, ULONG off, ULONG len, UWORD pitch, UWORD volume)
{
    if (s >= SND_COUNT || !chipSamples || off >= bear_samples[s].length) return;
    if (off + len > bear_samples[s].length) len = bear_samples[s].length - off;
    PlayRaw(chipSamples + sampleOff[s] + (off & ~1UL), len, bear_samples[s].rate, pitch, volume);
}

void Snd_PlayGroup(SoundGroup g, UWORD pitch)
{
    UWORD pick[SND_COUNT], n = 0, i;
    for (i = 0; i < SND_COUNT; i++)
        if (g == SG_ANY || groupOf[i] == g) pick[n++] = i;
    if (n) Snd_Play(pick[rand() % n], pitch, 64);
}

void Snd_Babble(UWORD pitch)
{
    static const UWORD talky[] = { SND_HAPPY1, SND_HAPPY2, SND_HAPPY3, SND_HI, SND_GREETING1,
                                   SND_GREETING2, SND_YES, SND_WAVE };
    UWORD s = talky[rand() % (sizeof(talky) / sizeof(talky[0]))];
    ULONG len = bear_samples[s].length, n, off;
    if (!chipSamples) return;
    n = 1400 + (rand() % 1400);
    if (n > len) n = len;
    off = len > n ? (ULONG)(rand() % (len - n)) & ~1UL : 0;
    PlayRaw(chipSamples + sampleOff[s] + off, n, bear_samples[s].rate, pitch, 60);
}

BOOL Snd_Busy(void) { return audBusy; }

void Snd_Update(void)
{
    if (audBusy && CheckIO((struct IORequest *)audReq)) {
        WaitIO((struct IORequest *)audReq);
        audBusy = FALSE;
    }
}

/* ------------------------------------------------------------------ speech */
struct Library *TranslatorBase = NULL;
static struct MsgPort *narrPort = NULL;
static struct narrator_rb *narr = NULL;
static BOOL narrOpen = FALSE, speaking = FALSE;
static UBYTE *phonemes = NULL;
static const UBYTE narrMasks[] = { 3, 12 };

BOOL Speech_Init(void)
{
    if (!(TranslatorBase = OpenLibrary((STRPTR)"translator.library", 0))) return FALSE;
    if (!(narrPort = CreateMsgPort())) return FALSE;
    if (!(narr = (struct narrator_rb *)CreateIORequest(narrPort, sizeof(struct narrator_rb)))) return FALSE;
    narr->flags = NDF_NEWIORB;
    narr->ch_masks = (UBYTE *)narrMasks;
    narr->nm_masks = sizeof(narrMasks);
    if (OpenDevice((STRPTR)"narrator.device", 0, (struct IORequest *)narr, 0)) return FALSE;
    narrOpen = TRUE;
    return TRUE;
}

BOOL Speech_Available(void) { return narrOpen; }

static void FreePhonemes(void)
{
    if (phonemes) { FreeVec(phonemes); phonemes = NULL; }
}

void Speech_Stop(void)
{
    if (narrOpen && speaking) {
        AbortIO((struct IORequest *)narr);
        WaitIO((struct IORequest *)narr);
        speaking = FALSE;
    }
    FreePhonemes();
}

void Speech_Close(void)
{
    Speech_Stop();
    if (narrOpen) { CloseDevice((struct IORequest *)narr); narrOpen = FALSE; }
    if (narr) { DeleteIORequest((struct IORequest *)narr); narr = NULL; }
    if (narrPort) { DeleteMsgPort(narrPort); narrPort = NULL; }
    if (TranslatorBase) { CloseLibrary(TranslatorBase); TranslatorBase = NULL; }
}

static UBYTE *MakePhonemes(const char *text, ULONG *outLen)
{
    LONG inPos = 0, inLen = (LONG)strlen(text);
    ULONG cap = 512, used = 0;
    UBYTE *buf = AllocVec(cap, MEMF_ANY);
    if (!buf) return NULL;
    while (inPos < inLen) {
        UBYTE tmp[256];
        LONG rc, consumed;
        ULONG tlen;
        tmp[0] = 0;
        rc = Translate((STRPTR)(text + inPos), inLen - inPos, (STRPTR)tmp, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        consumed = (rc == 0) ? (inLen - inPos) : (rc > 0 ? rc : -rc);
        tlen = strlen((char *)tmp);
        if (tlen) {
            if (used + tlen + 2 > cap) {
                UBYTE *nb;
                cap = (used + tlen + 2) * 2;
                if (!(nb = AllocVec(cap, MEMF_ANY))) { FreeVec(buf); return NULL; }
                CopyMem(buf, nb, used);
                FreeVec(buf);
                buf = nb;
            }
            CopyMem(tmp, buf + used, tlen);
            used += tlen;
            buf[used] = 0;
        }
        if (rc == 0 || consumed <= 0) break;
        inPos += consumed;
    }
    if (!used) { FreeVec(buf); return NULL; }
    *outLen = used;
    return buf;
}

void Speech_Say(const char *text, VoiceKind voice, UWORD pitchShift)
{
    char *bear = NULL;
    ULONG len = 0;
    if (!narrOpen || !text || !*text) return;
    Speech_Stop();
    if (voice == VOICE_BEAR) bear = Speech_Bearify(text);
    phonemes = MakePhonemes(bear ? bear : text, &len);
    if (bear) FreeVec(bear);
    if (!phonemes) return;

    narr->mode = NATURALF0;
    narr->sex = MALE;
    narr->volume = 64;
    narr->sampfreq = DEFFREQ;
    narr->mouths = 0;
    switch (voice) {
    case VOICE_BEAR:     narr->rate = 95;  narr->pitch = 70;  break;
    case VOICE_CHIPMUNK: narr->rate = 180; narr->pitch = 250; narr->sex = FEMALE; break;
    case VOICE_ROBOT:    narr->rate = 140; narr->pitch = 90;  narr->mode = ROBOTICF0; break;
    case VOICE_GIANT:    narr->rate = 80;  narr->pitch = 65;  narr->sampfreq = 18000; break;
    case VOICE_FRIEND:   narr->rate = 160; narr->pitch = 190; narr->sex = FEMALE; break;
    default:             narr->rate = 150; narr->pitch = 110; break;
    }
    narr->pitch += pitchShift;
    if (narr->pitch > MAXPITCH) narr->pitch = MAXPITCH;
    narr->message.io_Command = CMD_WRITE;
    narr->message.io_Flags = 0;
    narr->message.io_Offset = 0;
    narr->message.io_Data = phonemes;
    narr->message.io_Length = len;
    BeginIO((struct IORequest *)narr);
    speaking = TRUE;
}

BOOL Speech_Busy(void) { return speaking; }

void Speech_Update(void)
{
    if (speaking && CheckIO((struct IORequest *)narr)) {
        WaitIO((struct IORequest *)narr);
        speaking = FALSE;
        FreePhonemes();
    }
}

/* AG syllables (from the original AG Bear Talker) */
static int IsVowel(char c) { c = (char)tolower((unsigned char)c); return c=='a'||c=='e'||c=='i'||c=='o'||c=='u'||c=='y'; }
static int HasChar(const char *s, char ch) { for (; *s; ++s) if ((char)tolower((unsigned char)*s) == ch) return 1; return 0; }

static int Syllables(const char *w)
{
    int syl = 0, in = 0;
    for (; *w; ++w) {
        if (IsVowel(*w)) { if (!in) { ++syl; in = 1; } }
        else in = 0;
    }
    if (syl < 1) syl = 1;
    if (syl > 4) syl = 4;
    return syl;
}

static ULONG AgSyllable(const char *word, char *out, ULONG cap)
{
    int stretch = Syllables(word) + 1, i;
    ULONG o = 0;
    char vowel = (HasChar(word, 'o') || HasChar(word, 'u')) ? 'o' : HasChar(word, 'a') ? 'a' : 'e';
    if (stretch > 5) stretch = 5;
    if (HasChar(word, 'r') || HasChar(word, 'l')) {
        const char *g = "rar";
        while (*g && o < cap) out[o++] = *g++;
        for (i = 0; i < stretch && o < cap; i++) out[o++] = 'r';
    } else {
        if (o < cap) out[o++] = 'w';
        for (i = 0; i < stretch && o < cap; i++) out[o++] = vowel;
        if (o < cap) out[o++] = 'w';
    }
    if (o < cap) out[o++] = ' ';
    return o;
}

char *Speech_Bearify(const char *in)
{
    ULONG n = strlen(in), cap = n * 6 + 32, o = 0, i = 0;
    char *out = AllocVec(cap, MEMF_ANY);
    if (!out) return NULL;
    while (i < n) {
        char word[64];
        ULONG wl = 0;
        while (i < n && (in[i] == ' ' || in[i] == '\t')) i++;
        while (i < n && in[i] != ' ' && in[i] != '\t') {
            if (wl < 63) word[wl++] = in[i];
            i++;
        }
        word[wl] = 0;
        if (wl && o + 16 < cap) o += AgSyllable(word, out + o, cap - o - 1);
    }
    out[o] = 0;
    if (!o) { FreeVec(out); return NULL; }
    return out;
}
