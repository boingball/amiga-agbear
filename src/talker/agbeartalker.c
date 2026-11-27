/* AG Bear Talker 2.0 - type something and AG Bear says it.
 *
 * Grown from AGBear-Talker/ag-bear2.c: the same photo and mouth pictures, now on its own
 * 256-colour screen (no more wrong colours or flicker on the Workbench), with dress-up
 * (hats, sunglasses, bow ties, a moustache), fur colours, backdrops, voices, jokes, a song,
 * a surprise button and photos saved to RAM:.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../common/agb_gfx.h"
#include "../common/agb_audio.h"
#include "../gen/talk_data.h"
#include "../gen/sounds_data.h"

static const char version[] __attribute__((used)) = "$VER: AGBearTalker 2.0 (27.11.2026)";
static const char stack[] __attribute__((used)) = "$STACK:32768";

#define FPS 25
#define PHOTO_X 24
#define PHOTO_Y 50
#define CAPTION_Y 217
#define LIMIT_H 237
#define COL_X 229
#define GAD_Y 239
#define BTN_Y0 156


typedef struct { const char *name; WORD img; } Choice;

static const Choice whos[] = { { "AG Bear", 0 }, { "Party", 1 }, { "Prof", 2 }, { "Ted", 3 }, { "Sid Seal", 4 },
    { "Starfish", 5 }, { "Fudgie", 6 },
    { "Grandmaw", 7 }, { "HuliHoop", 8 } };
static const char *whoHello[] = { "Hello! I'm AG Bear!", "Party Bear here! Let's party!",
    "Good day. I'm Professor Bear.", "Hiya! I'm Ted! Look at my tongue!", "Arf arf! I'm Sid the Seal!",
    "Hello! I'm Starfish! I love my starry jumper!",
    "Hi! I'm Fudgie Arms! Come here for a big fudgy hug!",
    "Hello dearie! I'm Original Grandmaw. Do you like my hat?",
    "Hula hoop! I'm HuliHoop! Let's have some fun!" };
#define CH(i) (talk_chars[sel[R_WHO]][i])
enum { C_PHOTO, C_OPEN, C_CLOSED, C_MX, C_MY, C_PY, C_HATX, C_HATB, C_GLX, C_GLY, C_BOWX, C_BOWY, C_TASHX, C_TASHY };

static const Choice hats[] = { { "None", -1 }, { "Party", TALK_TALK_HAT_PARTY }, { "Top hat", TALK_TALK_HAT_TOP },
    { "Crown", TALK_TALK_HAT_CROWN }, { "Cap", TALK_TALK_HAT_CAP }, { "Wizard", TALK_TALK_HAT_WIZARD },
    { "Flower", TALK_TALK_HAT_FLOWER } };
static const Choice specs[] = { { "None", -1 }, { "Cool", TALK_TALK_GLASSES_COOL }, { "Hearts", TALK_TALK_GLASSES_HEARTS },
    { "Stars", TALK_TALK_GLASSES_STAR }, { "Specs", TALK_TALK_GLASSES_SPECS } };
static const Choice bows[] = { { "None", -1 }, { "Red", TALK_TALK_BOW_RED }, { "Blue", TALK_TALK_BOW_BLUE },
    { "Spotty", TALK_TALK_BOW_SPOTS } };
static const Choice tashes[] = { { "None", -1 }, { "Yes!", TALK_TALK_MOUSTACHE } };
static const Choice furs[] = { { "Normal", 0 }, { "Pink", 1 }, { "Blue", 2 }, { "Gold", 3 }, { "Green", 4 },
    { "Purple", 5 }, { "Silver", 6 }, { "Rainbow", 7 } };
enum { BK_SKY, BK_HOME, BK_PARK, BK_HOUSE, BK_SHOP, BK_SPACE, BK_DISCO, BK_PINK, NUM_BACKS };
static const Choice backs[] = { { "Sky", -1 }, { "Home", TALK_BACK_HOME }, { "Park", TALK_BACK_PARK },
    { "House", TALK_BACK_HOUSE }, { "Shop", TALK_BACK_SHOP }, { "Space", TALK_BACK_SPACE }, { "Disco", -1 },
    { "Pink", -1 } };
enum { V_NOISES, V_NORMAL, V_AGBEAR, V_CHIPMUNK, V_ROBOT, V_GIANT, NUM_VOICES };
static const Choice voices[] = { { "Growly", 0 }, { "Normal", 0 }, { "AG Bear", 0 }, { "Squeaky", 0 },
    { "Robot", 0 }, { "Giant", 0 } };

#define N(a) ((WORD)(sizeof(a) / sizeof(a[0])))
enum { R_WHO, R_HAT, R_SPECS, R_BOW, R_TASH, R_FUR, R_BACK, R_VOICE, NUM_ROWS };
static const char *rowName[NUM_ROWS] = { "Who", "Hat", "Glasses", "Bow tie", "Moustache", "Fur", "Backdrop", "Voice" };
static const Choice *rowChoices[NUM_ROWS] = { whos, hats, specs, bows, tashes, furs, backs, voices };
static const WORD rowCount[NUM_ROWS] = { N(whos), N(hats), N(specs), N(bows), N(tashes), N(furs), N(backs), N(voices) };
static WORD sel[NUM_ROWS] = { 0, 0, 0, 0, 0, 0, BK_SKY, V_NOISES };
#define ROW_H 19

enum { B_JOKE, B_SONG, B_MIX, B_GROWL, B_PHOTO, B_BYE, NUM_BTNS };
static const UWORD btnIcon[NUM_BTNS] = { TALK_ICON_HI, TALK_ICON_DANCE, TALK_ICON_GAME, TALK_ICON_VOICE,
                                          TALK_ICON_SAVE, TALK_ICON_QUIT };
static const char *btnTip[NUM_BTNS] = { "Tell me a joke!", "Sing a song!", "Surprise me!", "Bear noises!",
                                        "Take a photo (saved in RAM:)", "Bye bye!" };

static const char *jokes[] = {
    "Why do bears have hairy coats? Fur protection!",
    "What do you call a bear with no teeth? A gummy bear!",
    "What do you call a bear with no ears? B!",
    "Why don't bears wear shoes? They have bear feet!",
    "What is a bear's favourite drink? Koka-koala!",
    "How do you hug a bear? Very carefully!",
    "What do bears eat for pudding? Honey crumble!",
    "Why was the teddy bear never hungry? It was always stuffed!",
    "What do you call a cold bear? A brrr-bear!",
    "Knock knock! Who's there? Bear. Bear who? Bear with me!",
    "What goes ha ha bonk? A bear laughing its head off!",
    "Where do bears keep their money? In a honey box!",
};

static const char *hellos[] = { "Hello! I'm AG Bear. Type something for me to say!",
    "Click my hat, glasses and fur buttons to dress me up!", "I love honey and cuddles!",
    "Do you want to hear a joke? Press the speech button!", "Ooh, I like this outfit!" };

/* ------------------------------------------------------------------ state */
static Img *imgs[TALK_NUM_IMAGES];
static AgbInput in;
static ULONG frame = 0;
static BOOL quitting = FALSE, speechOK = FALSE, demo = FALSE;
static WORD hoverRow = -1, hoverBtn = -1;
static char caption[2][40];
static UWORD captionT = 0;
static UWORD talkT = 0;           /* mouth moves while > 0 */
static UBYTE mouthOpen = 0;
static UWORD babbleWords = 0, babblePitch = 256, babbleGap = 0;
static UWORD idleT = 0;
static UWORD photoNo = 1;
static WORD bob = 0;

/* song: Twinkle Twinkle Little Star (semitones above C) */
static const BYTE songNotes[] = { 0, 0, 7, 7, 9, 9, 7, -1, 5, 5, 4, 4, 2, 2, 0, -1,
                                  7, 7, 5, 5, 4, 4, 2, -1, 7, 7, 5, 5, 4, 4, 2, -1,
                                  0, 0, 7, 7, 9, 9, 7, -1, 5, 5, 4, 4, 2, 2, 0, -2 };
static const UWORD semitone[13] = { 256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483, 512 };
static WORD songPos = -1;
static UWORD songT = 0;

static struct Gadget *gadList = NULL, *strGad = NULL, *sayGad = NULL;
static APTR vi = NULL;

/* ------------------------------------------------------------------ helpers */
static WORD Rnd(WORD n) { return n > 0 ? (WORD)(rand() % n) : 0; }

static void SetCaption(const char *text)
{
    WORD len = (WORD)strlen(text), cut = len;
    memset(caption, 0, sizeof(caption));
    if (len > 38) {
        cut = 38;
        while (cut > 0 && text[cut] != ' ') cut--;
        if (cut == 0) cut = 38;
    }
    strncpy(caption[0], text, cut > 38 ? 38 : cut);
    if (cut < len) strncpy(caption[1], text + cut + (text[cut] == ' ' ? 1 : 0), 38);
    captionT = 100 + len * 3;
}

static UWORD VoicePitch(void)
{
    switch (sel[R_VOICE]) {
    case V_CHIPMUNK: return 420;
    case V_GIANT: return 170;
    case V_ROBOT: return 300;
    default: return 256;
    }
}

static void SayText(const char *text)
{
    WORD words = 0;
    const char *p;
    if (!text || !*text) return;
    SetCaption(text);
    songPos = -1;
    Snd_Stop();
    if (speechOK && sel[R_VOICE] != V_NOISES) {
        static const VoiceKind map[NUM_VOICES] = { VOICE_BEAR, VOICE_NORMAL, VOICE_BEAR, VOICE_CHIPMUNK,
                                                   VOICE_ROBOT, VOICE_GIANT };
        Speech_Say(text, map[sel[R_VOICE]], 0);
        babbleWords = 0;
    } else {
        Speech_Stop();
        for (p = text; *p; p++)
            if (*p != ' ' && (p == text || p[-1] == ' ')) words++;
        babbleWords = (UWORD)(words > 24 ? 24 : words);
        babblePitch = VoicePitch();
        babbleGap = 0;
    }
    talkT = 30;
    idleT = 0;
}

static void ApplyFur(void)
{
    static ULONG pal[256];
    WORD f = sel[R_FUR];
    CopyMem((APTR)talk_palette, pal, sizeof(pal));
    if (f == 7) f = TALK_RAINBOW_FIRST + (WORD)((frame / 3) % (TALK_NUM_FURS - TALK_RAINBOW_FIRST));
    CopyMem((APTR)talk_furs[sel[R_WHO]][f], pal + TALK_PHOTO_BASE, TALK_PHOTO_COLS * sizeof(ULONG));
    /* disco backdrop pens */
    {
        static const ULONG disco[8] = { 0xFF4060, 0xFFA030, 0xFFE040, 0x60E060, 0x40C0FF, 0x6060FF, 0xC060FF, 0xFF60C0 };
        UWORD i;
        for (i = 0; i < 16; i++) pal[TALK_CYCLE_BASE + i] = disco[((i / 2) + frame / 4) & 7];
    }
    Gfx_LoadPalette(pal, 256, 0);
}

static void Surprise(void)
{
    static const char *ta[] = { "Ta-da! How do I look?", "New look! Do you like it?", "I feel fabulous!",
                                "Wow, look at me!", "Surprise!" };
    if (demo) sel[R_WHO] = (sel[R_WHO] + 1) % rowCount[R_WHO];
    else if (Rnd(3) == 0) sel[R_WHO] = Rnd(rowCount[R_WHO]);
    sel[R_HAT] = Rnd(rowCount[R_HAT]);
    sel[R_SPECS] = Rnd(rowCount[R_SPECS]);
    sel[R_BOW] = Rnd(rowCount[R_BOW]);
    sel[R_TASH] = Rnd(4) == 0;
    sel[R_FUR] = Rnd(rowCount[R_FUR]);
    sel[R_BACK] = Rnd(rowCount[R_BACK]);
    ApplyFur();
    Snd_PlayGroup(SG_HAPPY, 256);
    SayText(ta[Rnd(5)]);
}

static void StartSong(void)
{
    Speech_Stop();
    babbleWords = 0;
    songPos = 0;
    songT = 0;
    SetCaption("Twinkle twinkle little star... la la la!");
    captionT = 48 * 7;
}

static void SongStep(void)
{
    BYTE n;
    if (songPos < 0) return;
    if (songT++ % 7) return;
    n = songNotes[songPos++];
    if (n == -2 || songPos >= (WORD)sizeof(songNotes)) { songPos = -1; bob = 0; SetCaption("Thank you, thank you!"); Snd_PlayGroup(SG_HAPPY, 256); return; }
    bob = (songPos & 1) ? -2 : 0;
    if (n < 0) return;
    Snd_PlayPart(SND_HI, 0, 2400, semitone[n], 60);
    talkT = 5;
}

/* ------------------------------------------------------------------ photo (IFF ILBM) */
static BOOL WriteLong(BPTR fh, ULONG v) { return Write(fh, &v, 4) == 4; }

static BOOL SavePhoto(char *name)
{
    struct RastPort tmp;
    struct BitMap *tbm;
    UBYTE *line, *body;
    BPTR fh;
    const WORD W = TALK_STAGE, H = TALK_STAGE_H, BPR = ((TALK_STAGE + 15) / 16) * 2;
    ULONG bodyLen = 0, pos;
    WORD y, p, x;
    BOOL ok = FALSE;
    ULONG cols[256 * 3 + 2];

    sprintf(name, "RAM:AGBear-Photo-%u.iff", photoNo);
    tbm = AllocBitMap(W, 1, 8, 0, g_scr->RastPort.BitMap);
    line = AllocVec(((W + 15) & ~15) + 16, MEMF_ANY);
    body = AllocVec((ULONG)H * 8 * (BPR + BPR / 128 + 2), MEMF_ANY);
    if (!tbm || !line || !body) goto out;
    tmp = g_workRP;
    tmp.Layer = NULL;
    tmp.BitMap = tbm;
    for (y = 0; y < H; y++) {
        ReadPixelLine8(&g_workRP, 0, y, W, line, &tmp);
        for (p = 0; p < 8; p++) {
            UBYTE row[TALK_STAGE / 8 + 2];
            memset(row, 0, sizeof(row));
            for (x = 0; x < W; x++)
                if (line[x] & (1 << p)) row[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
            /* ByteRun1 */
            pos = 0;
            while (pos < (ULONG)BPR) {
                ULONG run = 1;
                while (pos + run < (ULONG)BPR && run < 128 && row[pos + run] == row[pos]) run++;
                if (run >= 3) { body[bodyLen++] = (UBYTE)(257 - run); body[bodyLen++] = row[pos]; pos += run; }
                else {
                    ULONG start = pos, n = 0;
                    while (pos < (ULONG)BPR && n < 128 &&
                           !(pos + 2 < (ULONG)BPR && row[pos] == row[pos + 1] && row[pos] == row[pos + 2])) { pos++; n++; }
                    body[bodyLen++] = (UBYTE)(n - 1);
                    CopyMem(row + start, body + bodyLen, n);
                    bodyLen += n;
                }
            }
        }
    }
    if (bodyLen & 1) body[bodyLen++] = 0;
    GetRGB32(g_scr->ViewPort.ColorMap, 0, 256, cols);
    if (!(fh = Open((STRPTR)name, MODE_NEWFILE))) goto out;
    {
        UBYTE bmhd[20] = { 0 };
        UBYTE cmap[768];
        UWORD i;
        bmhd[0] = W >> 8; bmhd[1] = W & 255; bmhd[2] = H >> 8; bmhd[3] = H & 255;
        bmhd[8] = 8;        /* planes */
        bmhd[10] = 1;       /* ByteRun1 */
        bmhd[14] = 10; bmhd[15] = 11;  /* aspect (PAL lores) */
        bmhd[16] = 320 >> 8; bmhd[17] = 320 & 255; bmhd[18] = 256 >> 8; bmhd[19] = 0;
        for (i = 0; i < 768; i++) cmap[i] = (UBYTE)(cols[i] >> 24);
        ok = Write(fh, "FORM", 4) == 4 && WriteLong(fh, 4 + 8 + 20 + 8 + 768 + 8 + bodyLen) &&
             Write(fh, "ILBMBMHD", 8) == 8 && WriteLong(fh, 20) && Write(fh, bmhd, 20) == 20 &&
             Write(fh, "CMAP", 4) == 4 && WriteLong(fh, 768) && Write(fh, cmap, 768) == 768 &&
             Write(fh, "BODY", 4) == 4 && WriteLong(fh, bodyLen) && Write(fh, body, bodyLen) == (LONG)bodyLen;
    }
    Close(fh);
    if (ok) photoNo++;
out:
    if (tbm) FreeBitMap(tbm);
    if (line) FreeVec(line);
    if (body) FreeVec(body);
    return ok;
}

/* ------------------------------------------------------------------ actions */
static void ChangeRow(WORD r, WORD d)
{
    static const char *hatLine[] = { "No hat today.", "Party time!", "Very posh!", "I am the king of the bears!",
                                     "Sporty bear!", "Abracadabra!", "Flower power!" };
    sel[r] = (sel[r] + d + rowCount[r]) % rowCount[r];
    idleT = 0;
    switch (r) {
    case R_WHO: ApplyFur(); SayText(whoHello[sel[r]]); break;
    case R_HAT: SayText(hatLine[sel[r]]); break;
    case R_SPECS: SayText(sel[r] ? (sel[r] == 4 ? "Now I can read my books!" : "Looking cool!") : "Glasses off."); break;
    case R_BOW: SayText(sel[r] ? "Very smart!" : "No bow tie."); break;
    case R_TASH: SayText(sel[r] ? "What a splendid moustache!" : "Shaved it off!"); break;
    case R_FUR: ApplyFur(); SayText(sel[r] == 7 ? "I'm a rainbow bear!" : "Do you like my new fur?"); break;
    case R_BACK: Snd_PlayGroup(SG_WAVE, 256); break;
    case R_VOICE:
        if (!speechOK && sel[r] != V_NOISES && sel[r] != V_CHIPMUNK && sel[r] != V_GIANT && sel[r] != V_ROBOT)
            SayText("No narrator.device here, so I talk in bear!");
        else SayText("Hello! This is my new voice!");
        break;
    }
}

static void Button(WORD b)
{
    char name[48], msg[80];
    idleT = 0;
    switch (b) {
    case B_JOKE: SayText(jokes[Rnd(N(jokes))]); break;
    case B_SONG: StartSong(); break;
    case B_MIX: Surprise(); break;
    case B_GROWL:
        Speech_Stop();
        babbleWords = 0;
        Snd_PlayGroup((SoundGroup)Rnd(SG_ANY), VoicePitch());
        talkT = 30;
        SetCaption("Grrr! Rarrr! Hee hee!");
        break;
    case B_PHOTO:
        if (SavePhoto(name)) { sprintf(msg, "Cheese! Saved %s", name); SetCaption(msg); Snd_PlayGroup(SG_HAPPY, 300); }
        else SetCaption("Oh no, the photo could not be saved.");
        break;
    case B_BYE: quitting = TRUE; break;
    }
}

/* ------------------------------------------------------------------ drawing */
static void DrawAccessory(const Choice *c, WORD sel1, WORD cx, WORD cy, BOOL bottomAnchor)
{
    const Img *im;
    WORD x, y, py = CH(C_PY);
    if (c[sel1].img < 0) return;
    im = imgs[c[sel1].img];
    x = PHOTO_X + cx - im->w / 2;
    y = bottomAnchor ? py + bob + cy - im->h : py + bob + cy - im->h / 2;
    if (!bottomAnchor && y < 0) y = 0;          /* hats may poke off the top of the screen */
    if (y + im->h > TALK_STAGE_H) y = TALK_STAGE_H - im->h;
    Gfx_Image(im, x, y);
}

static void DrawStage(void)
{
    WORD b = sel[R_BACK], i, py = CH(C_PY) + bob;
    const Img *ph = imgs[CH(C_PHOTO)];
    WORD visH = TALK_STAGE_H - py;
    if (backs[b].img >= 0) Gfx_Image(imgs[backs[b].img], 0, 0);
    else if (b == BK_DISCO)
        for (i = 0; i < 16; i++) Gfx_Fill((UBYTE)(TALK_CYCLE_BASE + i), i * 15, 0, i == 15 ? TALK_STAGE - 225 : 15, TALK_STAGE_H);
    else Gfx_Fill(b == BK_PINK ? PEN_PINK : PEN_SKY, 0, 0, TALK_STAGE, TALK_STAGE_H);
    Gfx_ImagePart(ph, 0, 0, ph->w, visH > ph->h ? ph->h : visH, PHOTO_X, py);
    {
        const Img *m = imgs[mouthOpen ? CH(C_OPEN) : CH(C_CLOSED)];
        WORD my = py + CH(C_MY), mh = m->h;
        if (my + mh > TALK_STAGE_H) mh = TALK_STAGE_H - my;
        Gfx_ImagePart(m, 0, 0, m->w, mh, PHOTO_X + CH(C_MX), my);
    }
    DrawAccessory(tashes, sel[R_TASH], CH(C_TASHX), CH(C_TASHY), FALSE);
    DrawAccessory(bows, sel[R_BOW], CH(C_BOWX), CH(C_BOWY), FALSE);
    DrawAccessory(specs, sel[R_SPECS], CH(C_GLX), CH(C_GLY), FALSE);
    DrawAccessory(hats, sel[R_HAT], CH(C_HATX), CH(C_HATB), TRUE);
    if (songPos >= 0 && (frame & 8))
        Gfx_TextOutlined(PHOTO_X + 150, 20 + ((frame >> 3) & 3) * 3, PEN_YELLOW, PEN_BLACK, "La!");
}

static void DrawColumn(void)
{
    WORD r, y, b;
    Gfx_Fill(PEN_PANEL, TALK_STAGE, 0, SCR_W - TALK_STAGE, TALK_STAGE_H);
    Gfx_Fill(PEN_OUTLINE, TALK_STAGE, 0, 1, TALK_STAGE_H);
    for (r = 0; r < NUM_ROWS; r++) {
        BOOL hot = r == hoverRow;
        y = 2 + r * ROW_H;
        Gfx_Frame(COL_X + 1, y, SCR_W - COL_X - 2, ROW_H - 1, hot ? PEN_WHITE : PEN_PANEL_LT, PEN_PANEL_DK, hot ? PEN_PANEL_LT : PEN_PANEL);
        Gfx_Text(COL_X + 4, y + 1, PEN_CREAM, rowName[r]);
        Gfx_Text(COL_X + 3, y + 9, PEN_YELLOW, "<");
        Gfx_Text(SCR_W - 10, y + 9, PEN_YELLOW, ">");
        Gfx_TextCentred(COL_X + (SCR_W - COL_X) / 2, y + 9, PEN_WHITE, PEN_WHITE, rowChoices[r][sel[r]].name);
    }
    for (b = 0; b < NUM_BTNS; b++) {
        WORD x = COL_X + 1 + (b % 3) * 30, yy = BTN_Y0 + (b / 3) * 29;
        BOOL hot = b == hoverBtn;
        Gfx_Frame(x, yy, 29, 28, hot ? PEN_WHITE : PEN_PANEL_LT, PEN_PANEL_DK, hot ? PEN_PANEL_LT : PEN_PANEL);
        Gfx_Image(imgs[btnIcon[b]], x + 3, yy + 2);
    }
}

static void DrawCaption(void)
{
    const char *tip = NULL;
    Gfx_Fill(PEN_OUTLINE, 0, TALK_STAGE_H, TALK_STAGE, 1);
    Gfx_Fill(PEN_CREAM, 0, TALK_STAGE_H + 1, TALK_STAGE, LIMIT_H - TALK_STAGE_H - 1);
    if (hoverBtn >= 0) tip = btnTip[hoverBtn];
    Gfx_Fill(PEN_OUTLINE, TALK_STAGE, TALK_STAGE_H, SCR_W - TALK_STAGE, 1);
    Gfx_Fill(PEN_CREAM, TALK_STAGE, TALK_STAGE_H + 1, SCR_W - TALK_STAGE, LIMIT_H - TALK_STAGE_H - 1);
    if (captionT && caption[0][0]) {
        Gfx_Text(4, CAPTION_Y + 2, PEN_BLACK, caption[0]);
        if (caption[1][0]) Gfx_Text(4, CAPTION_Y + 11, PEN_BLACK, caption[1]);
    } else {
        Gfx_Text(4, CAPTION_Y + 6, PEN_OUTLINE, tip ? tip : "Type below, then press Return!");
    }
}

static void DrawAll(void)
{
    Gfx_Begin();
    DrawStage();
    DrawColumn();
    DrawCaption();
    Gfx_End();
}

/* ------------------------------------------------------------------ input & update */
static void HandleInput(void)
{
    WORD r, b;
    hoverRow = hoverBtn = -1;
    if (in.mx >= COL_X && in.my < BTN_Y0 - 2) {
        r = (in.my - 2) / ROW_H;
        if (r >= 0 && r < NUM_ROWS) hoverRow = r;
    } else if (in.mx >= COL_X && in.my >= BTN_Y0 && in.my < BTN_Y0 + 58) {
        b = (in.mx - COL_X - 1) / 30 + ((in.my - BTN_Y0) / 29) * 3;
        if (b >= 0 && b < NUM_BTNS && (in.mx - COL_X - 1) / 30 < 3) hoverBtn = b;
    }
    if (in.quit) quitting = TRUE;
    if (in.gadgetUp) {
        STRPTR s = NULL;
        GT_GetGadgetAttrs(strGad, g_win, NULL, GTST_String, (ULONG)&s, TAG_END);
        if (s && *s) {
            SayText((const char *)s);
            GT_SetGadgetAttrs(strGad, g_win, NULL, GTST_String, (ULONG)"", TAG_END);
        }
        ActivateGadget(strGad, g_win, NULL);
        return;
    }
    if (in.click || in.rclick) {
        WORD d = in.rclick ? -1 : 1;
        if (hoverRow >= 0) {
            if (in.mx < COL_X + 20) d = -1;
            ChangeRow(hoverRow, d);
        } else if (hoverBtn >= 0 && in.click) {
            Button(hoverBtn);
        } else if (in.my < TALK_STAGE_H && in.mx < TALK_STAGE && in.click) {
            static const char *poke[] = { "Hee hee, that tickles!", "Hello!", "Ooh!", "I love you!", "Boop!" };
            SayText(poke[Rnd(5)]);
        }
        if (!quitting) ActivateGadget(strGad, g_win, NULL);
    }
}

static void Update(void)
{
    BOOL talking;
    Snd_Update();
    Speech_Update();
    SongStep();
    if (babbleWords && !Snd_Busy()) {
        if (babbleGap) babbleGap--;
        else {
            Snd_Babble(babblePitch + Rnd(40));
            babbleWords--;
            babbleGap = 2;
            talkT = 8;
        }
    }
    talking = Speech_Busy() || babbleWords || talkT || (songPos >= 0 && Snd_Busy());
    if (talkT) talkT--;
    if (talking) {
        if (frame % 3 == 0) mouthOpen = (UBYTE)(Rnd(3) != 0 ? !mouthOpen : mouthOpen);
    } else mouthOpen = 0;
    if (captionT) captionT--;
    if (sel[R_FUR] == 7 || sel[R_BACK] == BK_DISCO) { if (frame % 3 == 0) ApplyFur(); }
    /* nobody playing? the bear does something on his own */
    if (++idleT > (demo ? 4 * FPS : 45 * FPS) && !talking && songPos < 0) {
        idleT = 0;
        switch (demo ? 0 : Rnd(4)) {
        case 0: Surprise(); break;
        case 1: SayText(jokes[Rnd(N(jokes))]); break;
        case 2: SayText(hellos[Rnd(N(hellos))]); break;
        default: if (demo) Surprise(); else SayText("Are you still there? Let's play dress-up!"); break;
        }
    }
}

/* ------------------------------------------------------------------ main */
static BOOL MakeGadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    static struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, FPF_ROMFONT };
    if (!(vi = GetVisualInfoA(g_scr, NULL))) return FALSE;
    g = CreateContext(&gadList);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &ta;
    ng.ng_LeftEdge = 4; ng.ng_TopEdge = GAD_Y; ng.ng_Width = 250; ng.ng_Height = 14;
    ng.ng_GadgetID = 1;
    g = strGad = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 200, GTST_String, (ULONG)"", TAG_END);
    ng.ng_LeftEdge = 260; ng.ng_Width = 56; ng.ng_GadgetID = 2;
    ng.ng_GadgetText = (UBYTE *)"Say";
    g = sayGad = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    if (!g) return FALSE;
    SetAPen(g_win->RPort, PEN_PANEL);
    RectFill(g_win->RPort, 0, LIMIT_H, SCR_W - 1, SCR_H - 1);
    AddGList(g_win, gadList, (UWORD)~0, (UWORD)~0, NULL);
    RefreshGList(gadList, g_win, NULL, (UWORD)~0);
    GT_RefreshWindow(g_win, NULL);
    ActivateGadget(strGad, g_win, NULL);
    return TRUE;
}

int main(int argc, char **argv)
{
    UWORD i;
    int rc = 0;
    if (argc > 1 && (!strcmp(argv[1], "DEMO") || !strcmp(argv[1], "demo"))) demo = TRUE;
    srand((unsigned)Gfx_Seconds() ^ (unsigned)(*(volatile UWORD *)0xDFF006));
    if (!Gfx_Open("AG Bear Talker", talk_palette)) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"AG Bear Talker",
            (UBYTE *)"AG Bear Talker needs an AGA Amiga (A1200/A4000)\nor a graphics card, and OS 3.0 or newer.\n(%s)",
            (UBYTE *)"OK" };
        Gfx_Close();
        EasyRequestArgs(NULL, &es, NULL, (APTR)&Gfx_Error);
        return 20;
    }
    Gfx_LoadPalette(talk_palette, 0, 0);
    Gfx_SetLimit(LIMIT_H);
    for (i = 0; i < TALK_NUM_IMAGES; i++) {
        BOOL opaque = (i >= TALK_BACK_HOME && i <= TALK_BACK_SPACE) || (i < TALK_NUM_CHARS * 3 && i % 3 != 0);
        if (!(imgs[i] = Img_Unpack(&talk_images[i], talk_blob, 0, !opaque))) { rc = 20; goto out; }
    }
    if (!MakeGadgets()) { rc = 20; goto out; }
    Snd_Init();
    speechOK = Speech_Init();
    if (!speechOK) Speech_Close();
    if (speechOK) sel[R_VOICE] = V_AGBEAR;
    SetCaption(hellos[0]);
    for (i = 0; i <= 8; i++) {      /* fade in */
        DrawAll();
        Gfx_LoadPalette(talk_palette, i * 32, 0);
        WaitTOF();
    }
    ApplyFur();
    Snd_PlayGroup(SG_GREETING, 256);
    talkT = 25;
    while (!quitting) {
        Gfx_Poll(&in);
        HandleInput();
        Update();
        DrawAll();
        Gfx_WaitFrame();
        frame++;
    }
    for (i = 8; i-- > 0;) { Gfx_LoadPalette(talk_palette, i * 32, 0); WaitTOF(); }
out:
    Speech_Close();
    Snd_Close();
    if (g_win && gadList) RemoveGList(g_win, gadList, -1);
    if (gadList) FreeGadgets(gadList);
    if (vi) FreeVisualInfo(vi);
    for (i = 0; i < TALK_NUM_IMAGES; i++) Img_Free(imgs[i]);
    Gfx_Close();
    return rc;
}
