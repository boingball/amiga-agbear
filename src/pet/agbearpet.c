/* AG Bear Pet - a virtual pet for AmigaOS 3.x (AGA or RTG, 256 colours)
 *
 * Grown from ag-bear-protov4.c (the first AG Bear Tamagotchi): look after AG Bear at home,
 * play in the garden, meet friends in the park and go shopping.  Everything is drawn through
 * the display list in ../common/agb_gfx.c, so the screen never flickers.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../common/agb_gfx.h"
#include "../common/agb_audio.h"
#include "../gen/pet_data.h"
#include "../gen/sounds_data.h"

static const char version[] __attribute__((used)) = "$VER: AGBearPet 2.0 (27.11.2026)";
static const char stack[] __attribute__((used)) = "$STACK:32768";

#define FPS 25
#define STAGE_H 200
#define PANEL_Y 200
#define BTN_Y 214
#define BTN_W 39
#define BTN_H 40
#define NUM_BTNS 8
#define BEAR_W 64
#define BEAR_H 64
#define NOWALK (-1000)

/* bear frames (sprite sheet order of bear32.ilbm) */
enum { F_FRONT = 0, F_TALK, F_WAVE, F_BACK, F_WR0, F_WR1, F_WL0, F_WL1 };

enum { BEAR_AG = 0, BEAR_ROSIE, BEAR_PIP, BEAR_HONEY, NUM_BEARS };
static const char *bearName[NUM_BEARS] = { "AG Bear", "Rosie", "Pip", "Honey" };

typedef enum { SC_HOME, SC_GARDEN, SC_PARK, SC_SHOP, NUM_SCENES } Scene;

static const WORD groundY[NUM_SCENES] = { 190, 188, 182, 192 };   /* where the feet go */

typedef enum {
    A_NONE, A_FEED, A_WASH, A_NAP, A_MEDI, A_GAME, A_DRESS, A_GO, A_MENU,
    A_FETCH, A_PICNIC, A_DANCE, A_HI, A_TOGETHER, A_GIFT, A_SWING, A_CATCH,
    A_BUY_HONEY, A_BUY_CAKE, A_BUY_MEDI, A_BUY_BALL, A_BUY_GIFT, A_BUY_SHADES, A_BUY_HAT,
    A_TO_HOME, A_TO_GARDEN, A_TO_PARK, A_TO_SHOP, A_BACK,
    A_WEAR_SHADES, A_WEAR_HAT, A_WEAR_CROWN, A_WEAR_NONE, A_VOICE, A_SAVE, A_QUIT
} Action;

typedef struct { Action act; UWORD icon; const char *tip; } Button;

typedef enum { P_MAIN, P_GO, P_DRESS, P_MENU } PanelMode;

/* ------------------------------------------------------------------ saved pet */
typedef struct {
    WORD food, fun, energy, clean, health;
    UWORD coins, honey, cake, medi, gifts;
    UBYTE ball, shades, hat, crown;        /* owned */
    UBYTE wearShades, wearHat;             /* wearHat: 0 none, 1 party hat, 2 crown */
    UBYTE friend[NUM_BEARS];               /* hearts 0..5 (index 0 unused) */
    UBYTE poop, sleeping, sick, voiceOn;
    UWORD bestCatch, bestMemory;
    ULONG born, lastSeen;
} Pet;

static Pet pet;

/* ------------------------------------------------------------------ actors */
typedef struct {
    BOOL on;
    UBYTE bear;
    WORD x, y;          /* top-left */
    WORD tx;            /* walk target, -1 = none */
    UBYTE dir;          /* 0 front, 1 right, 2 left, 3 back */
    UWORD anim;
    WORD hop;
    UWORD waveT, talkT, idleT;
    char say[2][28];
    UWORD sayT;
    UBYTE speed;
} Actor;

#define MAX_ACTORS 3
static Actor actors[MAX_ACTORS];   /* 0 = AG Bear, 1..2 = friends on stage */
#define ME (&actors[0])

typedef struct { BOOL on; UWORD img; LONG x, y, vx, vy; UWORD life; } Particle;
#define MAX_PARTS 28
static Particle parts[MAX_PARTS];

/* ------------------------------------------------------------------ globals */
static Img *imgs[PET_NUM_IMAGES];
static Img *bearImg[NUM_BEARS][8];
static Scene scene = SC_HOME;
static PanelMode panel = P_MAIN;
static AgbInput in;
static ULONG frame = 0;
static WORD hover = -1, joySel = 0;
static BOOL useJoySel = FALSE;
static BOOL quitting = FALSE;
static BOOL speechOK = FALSE;
static ULONG lastSec = 0, secCount = 0;
static UWORD idleChat = 200;
static UWORD visitT = 0;        /* a friend visiting at home */
static char saveName[64];

/* activities */
typedef enum { ACT_IDLE, ACT_EAT, ACT_WASH, ACT_SLEEP, ACT_SWING, ACT_FETCH, ACT_DANCE, ACT_TOGETHER,
               ACT_GREET, ACT_GIFT, ACT_TRAVEL, ACT_CATCH, ACT_MEMORY } Activity;
static Activity act = ACT_IDLE;
static UWORD actT = 0;
static WORD actStage = 0;
static Scene travelTo;
static UWORD eatImg;
static WORD ballX, ballY, ballVX, ballVY;
static BOOL ballHeld;
static WORD swingA = 0, swingV = 0;   /* swing angle (1/256 rad) and speed */
static UBYTE friendSlot = 1;

/* ------------------------------------------------------------------ helpers */
static WORD Clamp(WORD v, WORD lo, WORD hi) { return v < lo ? lo : v > hi ? hi : v; }
static void AddStat(WORD *s, WORD d) { *s = Clamp(*s + d, 0, 100); }
static WORD Rnd(WORD n) { return n > 0 ? (WORD)(rand() % n) : 0; }

static void Particle_Add(UWORD img, WORD x, WORD y, WORD vx, WORD vy, UWORD life)
{
    UWORD i;
    for (i = 0; i < MAX_PARTS; i++)
        if (!parts[i].on) {
            Particle *p = &parts[i];
            p->on = TRUE; p->img = img; p->x = (LONG)x << 4; p->y = (LONG)y << 4;
            p->vx = vx; p->vy = vy; p->life = life;
            return;
        }
}

static void Say(Actor *a, const char *text)
{
    /* split into two bubble lines of at most 26 characters */
    WORD len = (WORD)strlen(text), cut = len;
    memset(a->say, 0, sizeof(a->say));
    if (len > 26) {
        cut = 26;
        while (cut > 0 && text[cut] != ' ') cut--;
        if (cut == 0) cut = 26;
    }
    strncpy(a->say[0], text, cut > 27 ? 27 : cut);
    if (cut < len) strncpy(a->say[1], text + cut + (text[cut] == ' ' ? 1 : 0), 27);
    a->sayT = 70 + len * 2;
    a->talkT = 20 + len;
    if (pet.voiceOn && speechOK) {
        Speech_Say(text, a == ME ? VOICE_NORMAL : VOICE_FRIEND, a == ME ? 0 : (a->bear * 15));
    } else {
        Snd_Babble(a == ME ? 256 : 330 + a->bear * 20);
    }
}

static void SayMood(Actor *a, const char *text, SoundGroup g)
{
    Say(a, text);
    if (!(pet.voiceOn && speechOK)) Snd_PlayGroup(g, a == ME ? 256 : 320);
}

static Actor *Friend(void)
{
    UWORD i;
    for (i = 1; i < MAX_ACTORS; i++) if (actors[i].on) return &actors[i];
    return NULL;
}

static Actor *NearestFriend(void)
{
    Actor *best = NULL;
    UWORD i;
    WORD bd = 32767;
    for (i = 1; i < MAX_ACTORS; i++)
        if (actors[i].on) {
            WORD d = actors[i].x - ME->x;
            if (d < 0) d = -d;
            if (d < bd) { bd = d; best = &actors[i]; }
        }
    return best;
}

static void PlaceActor(Actor *a, UBYTE bear, WORD x)
{
    memset(a, 0, sizeof(*a));
    a->on = TRUE;
    a->bear = bear;
    a->x = x;
    a->y = groundY[scene] - BEAR_H;
    a->tx = NOWALK;
    a->speed = 2;
    a->idleT = 30 + Rnd(60);
}

static void WalkTo(Actor *a, WORD x) { a->tx = Clamp(x, -BEAR_W, SCR_W); }

/* ------------------------------------------------------------------ save / load */
static BOOL SaveTo(const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_NEWFILE);
    char buf[512];
    LONG n;
    if (!fh) return FALSE;
    n = sprintf(buf,
        "AGBEARPET 2\nfood %d\nfun %d\nenergy %d\nclean %d\nhealth %d\ncoins %u\nhoney %u\ncake %u\n"
        "medi %u\ngifts %u\nball %u\nshades %u\nhat %u\ncrown %u\nwearshades %u\nwearhat %u\n"
        "rosie %u\npip %u\nhoney_f %u\npoop %u\nsick %u\nvoice %u\nbestcatch %u\nbestmemory %u\n"
        "born %lu\nseen %lu\n",
        pet.food, pet.fun, pet.energy, pet.clean, pet.health, pet.coins, pet.honey, pet.cake,
        pet.medi, pet.gifts, pet.ball, pet.shades, pet.hat, pet.crown, pet.wearShades, pet.wearHat,
        pet.friend[1], pet.friend[2], pet.friend[3], pet.poop, pet.sick, pet.voiceOn,
        pet.bestCatch, pet.bestMemory, (unsigned long)pet.born, (unsigned long)Gfx_Seconds());
    n = Write(fh, buf, n) == n;
    Close(fh);
    return (BOOL)n;
}

static BOOL SavePet(void)
{
    if (SaveTo("PROGDIR:AGBear.save")) { strcpy(saveName, "PROGDIR:AGBear.save"); return TRUE; }
    if (SaveTo("ENVARC:AGBear.save")) {
        BPTR fh;
        strcpy(saveName, "ENVARC:AGBear.save");
        if ((fh = Open((STRPTR)"ENV:AGBear.save", MODE_NEWFILE))) Close(fh);
        return TRUE;
    }
    return FALSE;
}

static void NewPet(void)
{
    memset(&pet, 0, sizeof(pet));
    pet.food = 70; pet.fun = 80; pet.energy = 80; pet.clean = 100; pet.health = 100;
    pet.coins = 20;
    pet.honey = 1;
    pet.voiceOn = 1;
    pet.friend[BEAR_HONEY] = 1;
    pet.born = pet.lastSeen = Gfx_Seconds();
}

static BOOL LoadFrom(const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    char line[80];
    if (!fh) return FALSE;
    if (!FGets(fh, line, sizeof(line)) || strncmp(line, "AGBEARPET", 9) != 0) { Close(fh); return FALSE; }
    while (FGets(fh, line, sizeof(line))) {
        char key[24];
        unsigned long v = 0;
        if (sscanf(line, "%23s %lu", key, &v) != 2) continue;
#define K(k, f) if (!strcmp(key, k)) f = v;
        K("food", pet.food) K("fun", pet.fun) K("energy", pet.energy) K("clean", pet.clean)
        K("health", pet.health) K("coins", pet.coins) K("honey", pet.honey) K("cake", pet.cake)
        K("medi", pet.medi) K("gifts", pet.gifts) K("ball", pet.ball) K("shades", pet.shades)
        K("hat", pet.hat) K("crown", pet.crown) K("wearshades", pet.wearShades) K("wearhat", pet.wearHat)
        K("rosie", pet.friend[1]) K("pip", pet.friend[2]) K("honey_f", pet.friend[3]) K("poop", pet.poop)
        K("sick", pet.sick) K("voice", pet.voiceOn) K("bestcatch", pet.bestCatch)
        K("bestmemory", pet.bestMemory) K("born", pet.born) K("seen", pet.lastSeen)
#undef K
    }
    Close(fh);
    strcpy(saveName, name);
    return TRUE;
}

static void LoadPet(void)
{
    ULONG now = Gfx_Seconds(), away;
    NewPet();
    if (!LoadFrom("PROGDIR:AGBear.save") && !LoadFrom("ENVARC:AGBear.save")) {
        Say(ME, "Hi! I'm AG Bear! Will you look after me?");
        return;
    }
    pet.food = Clamp(pet.food, 0, 100); pet.fun = Clamp(pet.fun, 0, 100);
    pet.energy = Clamp(pet.energy, 0, 100); pet.clean = Clamp(pet.clean, 0, 100);
    pet.health = Clamp(pet.health, 0, 100);
    if (pet.poop > 3) pet.poop = 3;
    if (pet.wearHat > 2) pet.wearHat = 0;
    /* time away: a gentle slide, never below 10 */
    away = now > pet.lastSeen ? (now - pet.lastSeen) / 60 : 0;
    if (away > 0) {
        WORD d = (WORD)(away > 480 ? 40 : away / 12);
        if (pet.food > 10) pet.food = Clamp(pet.food - d, 10, 100);
        if (pet.fun > 10) pet.fun = Clamp(pet.fun - d, 10, 100);
        if (pet.clean > 10) pet.clean = Clamp(pet.clean - d / 2, 10, 100);
        pet.energy = Clamp(pet.energy + d, 0, 100);   /* slept while you were away */
    }
    if (now / 86400 != pet.lastSeen / 86400) {
        pet.coins += 5;
        Say(ME, "Hello again! Here are 5 pocket money coins!");
    } else if (away > 30) {
        Say(ME, "Hello! I missed you!");
    } else {
        Say(ME, "Hello again!");
    }
}

/* ------------------------------------------------------------------ buttons per scene */
static const Button bHome[] = {
    { A_FEED, PET_ICON_FEED, "Feed AG Bear" }, { A_WASH, PET_ICON_WASH, "Bath time" },
    { A_NAP, PET_ICON_NAP, "Nap on the sofa" }, { A_MEDI, PET_ICON_MEDI, "Medicine" },
    { A_GAME, PET_ICON_GAME, "Balloon memory game" }, { A_DRESS, PET_ICON_DRESS, "Dress up" },
    { A_GO, PET_ICON_GO, "Go out" }, { A_MENU, PET_ICON_MENU, "Stats and menu" } };
static const Button bGarden[] = {
    { A_FETCH, PET_ICON_FETCH, "Play fetch" }, { A_PICNIC, PET_ICON_PICNIC, "Picnic apple" },
    { A_DANCE, PET_ICON_DANCE, "Dance" }, { A_FEED, PET_ICON_FEED, "Feed AG Bear" },
    { A_DRESS, PET_ICON_DRESS, "Dress up" }, { A_NONE, 0, NULL },
    { A_GO, PET_ICON_GO, "Go somewhere" }, { A_MENU, PET_ICON_MENU, "Stats and menu" } };
static const Button bPark[] = {
    { A_HI, PET_ICON_HI, "Say hello" }, { A_TOGETHER, PET_ICON_TOGETHER, "Play together" },
    { A_GIFT, PET_ICON_ITEM_GIFT, "Give a present" }, { A_SWING, PET_ICON_SWING, "Swings" },
    { A_CATCH, PET_ICON_CATCH, "Catch the honey game" }, { A_DANCE, PET_ICON_DANCE, "Dance" },
    { A_GO, PET_ICON_GO, "Go somewhere" }, { A_MENU, PET_ICON_MENU, "Stats and menu" } };
static const Button bShop[] = {
    { A_BUY_HONEY, PET_ICON_ITEM_HONEY, "Honey pot 5 coins" }, { A_BUY_CAKE, PET_ICON_ITEM_CAKE, "Cake 8 coins" },
    { A_BUY_MEDI, PET_ICON_ITEM_MEDI, "Medicine 6 coins" }, { A_BUY_BALL, PET_ICON_ITEM_BALL, "Ball 15 coins" },
    { A_BUY_GIFT, PET_ICON_ITEM_GIFT, "Present 6 coins" }, { A_BUY_SHADES, PET_ICON_ITEM_SHADES, "Sunglasses 20 coins" },
    { A_BUY_HAT, PET_ICON_ITEM_PARTYHAT, "Party hat 20 coins" }, { A_GO, PET_ICON_GO, "Go somewhere" } };
static const Button bGo[] = {
    { A_TO_HOME, PET_ICON_HOME, "Go home" }, { A_TO_GARDEN, PET_ICON_GARDEN, "Go to the garden" },
    { A_TO_PARK, PET_ICON_PARK, "Go to the park" }, { A_TO_SHOP, PET_ICON_SHOP, "Go to the shop" },
    { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_BACK, PET_ICON_BACK, "Back" } };
static const Button bDress[] = {
    { A_WEAR_SHADES, PET_ICON_ITEM_SHADES, "Sunglasses" }, { A_WEAR_HAT, PET_ICON_ITEM_PARTYHAT, "Party hat" },
    { A_WEAR_CROWN, PET_ICON_ITEM_CROWN, "Golden crown" }, { A_WEAR_NONE, PET_ICON_TICKLE, "Take it all off" },
    { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_BACK, PET_ICON_BACK, "Back" } };
static const Button bMenu[] = {
    { A_VOICE, PET_ICON_VOICE, "Voice on or off" }, { A_SAVE, PET_ICON_SAVE, "Save now" },
    { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_NONE, 0, NULL }, { A_NONE, 0, NULL },
    { A_QUIT, PET_ICON_QUIT, "Save and quit" }, { A_BACK, PET_ICON_BACK, "Back" } };

static const Button *Buttons(void)
{
    switch (panel) {
    case P_GO: return bGo;
    case P_DRESS: return bDress;
    case P_MENU: return bMenu;
    default: break;
    }
    switch (scene) {
    case SC_GARDEN: return bGarden;
    case SC_PARK: return bPark;
    case SC_SHOP: return bShop;
    default: return bHome;
    }
}

/* ------------------------------------------------------------------ scenes */
static void EnterScene(Scene s)
{
    UWORD i;
    scene = s;
    for (i = 1; i < MAX_ACTORS; i++) actors[i].on = FALSE;
    memset(parts, 0, sizeof(parts));
    PlaceActor(ME, BEAR_AG, 20);
    WalkTo(ME, 90);
    visitT = 0;
    if (s == SC_PARK) {
        UBYTE first = (UBYTE)(BEAR_ROSIE + Rnd(2));
        PlaceActor(&actors[1], first, 200 + Rnd(60));
        if (Rnd(3) == 0) PlaceActor(&actors[2], first == BEAR_ROSIE ? BEAR_PIP : BEAR_ROSIE, 140);
        Say(&actors[1], first == BEAR_ROSIE ? "Hi AG Bear! It's me, Rosie!" : "Hey AG Bear! Pip here!");
    } else if (s == SC_SHOP) {
        PlaceActor(&actors[1], BEAR_HONEY, 246);
        actors[1].y = 98;
        actors[1].speed = 0;
        Say(&actors[1], "Welcome to Honey's shop!");
    } else if (s == SC_HOME) {
        Say(ME, "Home sweet home!");
    } else {
        Say(ME, "What a lovely day!");
    }
}

/* ------------------------------------------------------------------ actions */
static BOOL Busy(void) { return act != ACT_IDLE; }

static void Buy(UWORD price, UWORD *count, UBYTE *flag, const char *thanks)
{
    Actor *h = &actors[1];
    if (flag && *flag) { Say(h, "You already have one of those!"); return; }
    if (pet.coins < price) { Say(h, "Sorry, not enough coins. Play games to earn some!"); Snd_PlayGroup(SG_NO, 300); return; }
    pet.coins -= price;
    if (count) (*count)++;
    if (flag) *flag = 1;
    if (pet.friend[BEAR_HONEY] < 5 && Rnd(3) == 0) pet.friend[BEAR_HONEY]++;
    Say(h, thanks);
    Particle_Add(PET_PROP_COIN, h->x + 20, h->y + 20, -20, -24, 30);
}

static void StartTravel(Scene s)
{
    if (s == scene) { panel = P_MAIN; return; }
    travelTo = s;
    act = ACT_TRAVEL;
    actT = 0;
    panel = P_MAIN;
    WalkTo(ME, SCR_W + 10);
    ME->speed = 4;
}

static void WakeUp(void)
{
    if (!pet.sleeping) return;
    pet.sleeping = 0;
    act = ACT_IDLE;
    Gfx_LoadPalette(pet_palette, 256, 0);
    PlaceActor(ME, BEAR_AG, ME->x);
    SayMood(ME, "Yawn! Good morning!", SG_WAKEUP);
}

static void StartCatch(void);
static void StartMemory(void);

static void DoAction(Action a)
{
    Actor *f;
    if (a == A_NONE) return;
    if (pet.sleeping && a != A_MENU && a != A_QUIT && a != A_SAVE && a != A_VOICE && a != A_BACK) {
        WakeUp();
        if (a == A_NAP) return;
    }
    if (Busy() && a != A_MENU && a != A_BACK && a != A_QUIT && a != A_VOICE && a != A_SAVE) return;
    switch (a) {
    case A_FEED:
        if (pet.food >= 95) { SayMood(ME, "I'm full up! No more, thank you.", SG_NO); break; }
        if (pet.cake) { pet.cake--; eatImg = PET_PROP_CAKE; AddStat(&pet.food, 35); AddStat(&pet.fun, 15); AddStat(&pet.clean, -8); }
        else if (pet.honey) { pet.honey--; eatImg = PET_PROP_HONEY; AddStat(&pet.food, 30); AddStat(&pet.fun, 10); }
        else { eatImg = PET_PROP_APPLE; AddStat(&pet.food, 15); }
        act = ACT_EAT; actT = 0;
        ME->tx = NOWALK;
        break;
    case A_WASH:
        act = ACT_WASH; actT = 0; ME->tx = NOWALK;
        pet.clean = 100;
        pet.poop = 0;
        AddStat(&pet.fun, 5);
        SayMood(ME, "Splish splash! Bubbles!", SG_HAPPY);
        break;
    case A_NAP:
        if (pet.energy > 85) { SayMood(ME, "I'm not sleepy yet!", SG_NO); break; }
        pet.sleeping = 1;
        act = ACT_SLEEP; actT = 0;
        WalkTo(ME, 52);
        SayMood(ME, "Night night...", SG_WAKEUP);
        break;
    case A_MEDI:
        if (!pet.sick && pet.health > 70) { SayMood(ME, "I feel fine, no medicine please!", SG_NO); break; }
        if (!pet.medi) { SayMood(ME, "We need medicine from Honey's shop.", SG_NO); break; }
        pet.medi--;
        pet.sick = 0;
        AddStat(&pet.health, 50);
        SayMood(ME, "Yuck! ... Oh, I feel much better!", SG_YES);
        break;
    case A_GAME:
        if (pet.energy < 10) { SayMood(ME, "I'm too sleepy to play.", SG_NO); break; }
        StartMemory();
        break;
    case A_DRESS: panel = P_DRESS; break;
    case A_GO: panel = P_GO; break;
    case A_MENU: panel = P_MENU; break;
    case A_BACK: panel = P_MAIN; break;
    case A_FETCH:
        if (!pet.ball) { SayMood(ME, "I need a ball! Honey's shop has them.", SG_NO); break; }
        if (pet.energy < 10) { SayMood(ME, "Too tired for fetch...", SG_NO); break; }
        act = ACT_FETCH; actT = 0; actStage = 0;
        ballX = ME->x + 40; ballY = ME->y + 20; ballVX = 5 + Rnd(3); ballVY = -9; ballHeld = FALSE;
        ME->tx = NOWALK;
        SayMood(ME, "Throw it! Throw it!", SG_HAPPY);
        break;
    case A_PICNIC:
        if (pet.food >= 95) { SayMood(ME, "I'm too full for a picnic.", SG_NO); break; }
        eatImg = PET_PROP_APPLE;
        AddStat(&pet.food, 12); AddStat(&pet.fun, 8);
        act = ACT_EAT; actT = 0; ME->tx = NOWALK;
        break;
    case A_DANCE:
        if (pet.energy < 8) { SayMood(ME, "My feet are too tired.", SG_NO); break; }
        act = ACT_DANCE; actT = 0; ME->tx = NOWALK;
        AddStat(&pet.fun, 12); AddStat(&pet.energy, -5);
        if ((f = Friend()) && scene != SC_SHOP) f->tx = NOWALK;
        Say(ME, "Let's boogie!");
        break;
    case A_HI:
        if (!(f = NearestFriend())) { Say(ME, "Nobody here right now."); break; }
        act = ACT_GREET; actT = 0; actStage = 0;
        friendSlot = (UBYTE)(f - actors);
        WalkTo(ME, f->x > ME->x ? f->x - 52 : f->x + 52);
        break;
    case A_TOGETHER:
        if (!(f = NearestFriend())) { Say(ME, "Nobody to play with here."); break; }
        if (pet.energy < 10) { SayMood(ME, "I'm too tired to play.", SG_NO); break; }
        act = ACT_TOGETHER; actT = 0; actStage = 0;
        friendSlot = (UBYTE)(f - actors);
        WalkTo(ME, f->x > ME->x ? f->x - 50 : f->x + 50);
        break;
    case A_GIFT:
        if (!(f = NearestFriend())) { Say(ME, "Nobody here to give a present to."); break; }
        if (!pet.gifts) { SayMood(ME, "I have no presents. Honey's shop sells them!", SG_NO); break; }
        act = ACT_GIFT; actT = 0; actStage = 0;
        friendSlot = (UBYTE)(f - actors);
        WalkTo(ME, f->x > ME->x ? f->x - 50 : f->x + 50);
        break;
    case A_SWING:
        if (pet.energy < 5) { SayMood(ME, "Too sleepy for the swings.", SG_NO); break; }
        act = ACT_SWING; actT = 0; actStage = 0;
        WalkTo(ME, 22);
        break;
    case A_CATCH:
        if (pet.energy < 10) { SayMood(ME, "I'm too tired to play.", SG_NO); break; }
        StartCatch();
        break;
    case A_BUY_HONEY: Buy(5, &pet.honey, NULL, "One pot of honey. Yum!"); break;
    case A_BUY_CAKE: Buy(8, &pet.cake, NULL, "A lovely cake, enjoy!"); break;
    case A_BUY_MEDI: Buy(6, &pet.medi, NULL, "Medicine for poorly days."); break;
    case A_BUY_BALL: Buy(15, NULL, &pet.ball, "A bouncy ball! Try fetch in the garden."); break;
    case A_BUY_GIFT: Buy(6, &pet.gifts, NULL, "A present! Give it to a friend."); break;
    case A_BUY_SHADES: Buy(20, NULL, &pet.shades, "Cool shades! Dress up at home."); break;
    case A_BUY_HAT: Buy(20, NULL, &pet.hat, "Party hat! Time to celebrate!"); break;
    case A_TO_HOME: StartTravel(SC_HOME); break;
    case A_TO_GARDEN: StartTravel(SC_GARDEN); break;
    case A_TO_PARK: StartTravel(SC_PARK); break;
    case A_TO_SHOP: StartTravel(SC_SHOP); break;
    case A_WEAR_SHADES:
        if (!pet.shades) { Say(ME, "I need sunglasses from Honey's shop."); break; }
        pet.wearShades ^= 1;
        Say(ME, pet.wearShades ? "Looking cool!" : "Shades off.");
        break;
    case A_WEAR_HAT:
        if (!pet.hat) { Say(ME, "I need a party hat from Honey's shop."); break; }
        pet.wearHat = pet.wearHat == 1 ? 0 : 1;
        Say(ME, pet.wearHat ? "Party time!" : "Hat off.");
        break;
    case A_WEAR_CROWN:
        if (!pet.crown) { Say(ME, "Win a game to get the golden crown!"); break; }
        pet.wearHat = pet.wearHat == 2 ? 0 : 2;
        Say(ME, pet.wearHat ? "I am the king of the bears!" : "Crown off.");
        break;
    case A_WEAR_NONE:
        pet.wearShades = 0; pet.wearHat = 0;
        Say(ME, "Just plain old me.");
        break;
    case A_VOICE:
        pet.voiceOn ^= 1;
        if (!speechOK && pet.voiceOn) Say(ME, "No narrator.device - I'll use my bear noises!");
        else Say(ME, pet.voiceOn ? "Voice on!" : "Voice off. Bear noises only!");
        break;
    case A_SAVE:
        Say(ME, SavePet() ? "Saved! See you soon." : "Oh no, I couldn't save!");
        break;
    case A_QUIT:
        quitting = TRUE;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ mini game: catch */
typedef struct { BOOL on; UWORD img; WORD x, y, vy; } Faller;
#define MAX_FALL 8
static Faller fall[MAX_FALL];
static WORD score, dizzy, spawnT;
static WORD lastMX = -1;
static BOOL mouseCtl = FALSE;

static void StartCatch(void)
{
    act = ACT_CATCH; actT = 0;
    memset(fall, 0, sizeof(fall));
    score = 0; dizzy = 0; spawnT = 30;
    panel = P_MAIN;
    ME->tx = NOWALK;
    ME->x = 128;
    lastMX = in.mx;
    mouseCtl = FALSE;
    Say(ME, "Catch the honey! Watch out for bees!");
}

static void EndCatch(void)
{
    char msg[60];
    UWORD won = (UWORD)(score > 30 ? 30 : score < 0 ? 0 : score);
    act = ACT_IDLE;
    pet.coins += won;
    AddStat(&pet.fun, 20); AddStat(&pet.energy, -8); AddStat(&pet.food, -4);
    if ((UWORD)score > pet.bestCatch) pet.bestCatch = (UWORD)score;
    if (score >= 25 && !pet.crown) {
        pet.crown = 1;
        SayMood(ME, "WOW! I won the golden crown!", SG_HAPPY);
    } else {
        sprintf(msg, "I caught %d! That's %u coins!", score, won);
        SayMood(ME, msg, score > 10 ? SG_HAPPY : SG_YES);
    }
}

static void UpdateCatch(void)
{
    UWORD i;
    WORD speed = 5;
    actT++;
    if (actT >= 30 * FPS) { EndCatch(); return; }
    /* steering: mouse once it moves, otherwise keys / joystick */
    if (in.mx != lastMX) { mouseCtl = TRUE; lastMX = in.mx; }
    if (in.left || in.right) mouseCtl = FALSE;
    if (dizzy > 0) { dizzy--; speed = 1; }
    if (mouseCtl) {
        WORD target = Clamp(in.mx - BEAR_W / 2, 0, SCR_W - BEAR_W);
        WORD d = Clamp(target - ME->x, -speed - 1, speed + 1);
        ME->x += d;
        ME->dir = d > 0 ? 1 : d < 0 ? 2 : 0;
    } else {
        if (in.left) { ME->x -= speed; ME->dir = 2; }
        else if (in.right) { ME->x += speed; ME->dir = 1; }
        else ME->dir = 0;
    }
    ME->x = Clamp(ME->x, 0, SCR_W - BEAR_W);
    if (ME->dir) ME->anim++;
    if (--spawnT <= 0) {
        for (i = 0; i < MAX_FALL; i++)
            if (!fall[i].on) {
                static const UWORD kinds[] = { PET_PROP_HONEY, PET_PROP_HONEY, PET_PROP_APPLE, PET_PROP_APPLE,
                                               PET_PROP_CAKE, PET_PROP_BEE, PET_PROP_BEE };
                Faller *f = &fall[i];
                f->on = TRUE;
                f->img = kinds[Rnd(7)];
                f->x = 8 + Rnd(SCR_W - 24);
                f->y = -16;
                f->vy = 2 + Rnd(2) + actT / (10 * FPS);
                break;
            }
        spawnT = 16 + Rnd(18) - actT / (6 * FPS);
    }
    for (i = 0; i < MAX_FALL; i++) {
        Faller *f = &fall[i];
        if (!f->on) continue;
        if (f->img == PET_PROP_BEE) f->x += ((frame + i) & 8) ? 1 : -1;
        f->y += f->vy;
        if (f->y + 14 >= ME->y + 6 && f->y <= ME->y + 34 && f->x + 12 >= ME->x + 10 && f->x <= ME->x + 54) {
            f->on = FALSE;
            if (f->img == PET_PROP_BEE) {
                score = score > 2 ? score - 2 : 0;
                dizzy = 25;
                Snd_PlayGroup(SG_GROWL, 280);
                Particle_Add(PET_PROP_BEE, ME->x + 30, ME->y, 30, -30, 25);
            } else {
                score += f->img == PET_PROP_CAKE ? 2 : 1;
                Snd_Play(SND_NOM1 + (score & 1), 256 + (score & 7) * 8, 50);
                Particle_Add(PET_PROP_HEART, ME->x + 28, ME->y, 0, -24, 20);
            }
        } else if (f->y > STAGE_H) {
            f->on = FALSE;
        }
    }
}

static void DrawCatch(void)
{
    UWORD i;
    char buf[24];
    for (i = 0; i < MAX_FALL; i++)
        if (fall[i].on) Gfx_Image(imgs[fall[i].img], fall[i].x, fall[i].y);
    sprintf(buf, "Score %d", score);
    Gfx_TextOutlined(6, 4, PEN_YELLOW, PEN_BLACK, buf);
    sprintf(buf, "Time %d", 30 - actT / FPS);
    Gfx_TextOutlined(SCR_W - 70, 4, PEN_WHITE, PEN_BLACK, buf);
    if (actT < 50) Gfx_TextCentred(160, 60, PEN_WHITE, PEN_BLACK, "Move with mouse or joystick!");
}

/* ------------------------------------------------------------------ mini game: memory */
static UBYTE seq[16];
static UWORD seqLen, seqPos, litBalloon, litT;
static BOOL showing;
static const WORD balX[4] = { 96, 150, 96, 150 }, balY[4] = { 22, 22, 90, 90 };
static const UWORD balPitch[4] = { 200, 256, 320, 400 };

static void Balloon(UWORD b)
{
    litBalloon = b + 1;
    litT = 10;
    Snd_Play(SND_HI, balPitch[b], 60);
}

static void StartMemory(void)
{
    act = ACT_MEMORY; actT = 0;
    seqLen = 1; seqPos = 0;
    seq[0] = (UBYTE)Rnd(4);
    showing = TRUE;
    litBalloon = 0; litT = 0;
    ME->tx = NOWALK;
    ME->x = 226;
    ME->dir = 0;
    panel = P_MAIN;
    Say(ME, "Watch the balloons, then copy me!");
    actStage = -60;
}

static void EndMemory(void)
{
    char msg[60];
    UWORD level = seqLen - 1, won = level * 2;
    act = ACT_IDLE;
    pet.coins += won;
    AddStat(&pet.fun, 15); AddStat(&pet.energy, -4);
    if (level > pet.bestMemory) pet.bestMemory = level;
    if (level >= 7 && !pet.crown) {
        pet.crown = 1;
        SayMood(ME, "Amazing memory! I won the golden crown!", SG_HAPPY);
    } else {
        sprintf(msg, "Oops! You remembered %u. Here's %u coins!", level, won);
        SayMood(ME, msg, level > 3 ? SG_HAPPY : SG_NO);
    }
}

static void UpdateMemory(void)
{
    WORD b = -1;
    UWORD i;
    if (litT && --litT == 0) litBalloon = 0;
    if (actStage < 0) { actStage++; return; }
    if (showing) {
        if (++actStage >= 18) {
            actStage = 0;
            if (seqPos < seqLen) Balloon(seq[seqPos++]);
            else { showing = FALSE; seqPos = 0; }
        }
        return;
    }
    if (in.click)
        for (i = 0; i < 4; i++)
            if (in.mx >= balX[i] && in.mx < balX[i] + 36 && in.my >= balY[i] && in.my < balY[i] + 46) b = i;
    if (in.key >= '1' && in.key <= '4') b = in.key - '1';
    if (b < 0) return;
    Balloon((UWORD)b);
    if (b != seq[seqPos]) { EndMemory(); return; }
    if (++seqPos == seqLen) {
        if (seqLen == sizeof(seq)) { seqLen++; EndMemory(); return; }
        seq[seqLen++] = (UBYTE)Rnd(4);
        seqPos = 0;
        showing = TRUE;
        actStage = -30;
        Particle_Add(PET_PROP_HEART, ME->x + 28, ME->y, 0, -20, 25);
        Snd_PlayGroup(SG_HAPPY, 256);
    }
}

static void DrawMemory(void)
{
    UWORD i;
    char buf[24];
    for (i = 0; i < 4; i++)
        Gfx_Image(imgs[(litBalloon == i + 1 ? PET_BALLOON_RED_LIT : PET_BALLOON_RED) + i], balX[i], balY[i]);
    sprintf(buf, "Level %u", seqLen);
    Gfx_TextOutlined(6, 4, PEN_YELLOW, PEN_BLACK, buf);
    Gfx_TextCentred(130, 148, PEN_WHITE, PEN_BLACK, showing ? "Watch..." : "Your turn!");
}

/* ------------------------------------------------------------------ per-frame logic */
static void UpdateActor(Actor *a)
{
    if (!a->on) return;
    if (a->sayT) a->sayT--;
    if (a->talkT) a->talkT--;
    if (a->waveT) a->waveT--;
    if (a->tx != NOWALK && a->speed) {
        WORD d = a->tx - a->x;
        if (d > a->speed) d = a->speed;
        if (d < -a->speed) d = -a->speed;
        if (pet.sick && a == ME && (frame & 1)) d /= 2;
        a->x += d;
        a->dir = d > 0 ? 1 : d < 0 ? 2 : 0;
        a->anim++;
        if (a->x == a->tx) { a->tx = NOWALK; a->dir = 0; }
    } else {
        a->dir = 0;
    }
}

static void Wander(Actor *a, WORD lo, WORD hi)
{
    if (!a->on || a->tx != NOWALK || !a->speed) return;
    if (a->idleT) { a->idleT--; return; }
    a->idleT = 40 + Rnd(120);
    if (Rnd(3) == 0) a->waveT = 24;
    else WalkTo(a, lo + Rnd(hi - lo));
}

static void SecondTick(void)
{
    secCount++;
    if (pet.sleeping) {
        if (secCount % 2 == 0) AddStat(&pet.energy, 1);
        if (secCount % 60 == 0) AddStat(&pet.food, -1);
        if (pet.energy >= 100) WakeUp();
        return;
    }
    if (secCount % 36 == 0) AddStat(&pet.food, -1);
    if (secCount % 30 == 0) AddStat(&pet.fun, -1);
    if (secCount % 50 == 0) AddStat(&pet.clean, -1);
    if (pet.poop && secCount % 15 == 0) AddStat(&pet.clean, -1);
    if (secCount % 45 == 0) AddStat(&pet.energy, -1);
    if (secCount % 30 == 0) {
        if (pet.food < 20 || pet.clean < 15 || pet.fun < 10) AddStat(&pet.health, -2);
        else if (pet.health < 100) AddStat(&pet.health, 1);
    }
    if (!pet.sick && pet.health < 30) {
        pet.sick = 1;
        SayMood(ME, "I don't feel very well...", SG_GROWL);
    }
    if (pet.sick && pet.health > 60) pet.sick = 0;
    if (secCount % 120 == 0) SavePet();
}

static void IdleChatter(void)
{
    const char *line = NULL;
    SoundGroup g = SG_HAPPY;
    if (Busy() || pet.sleeping || ME->sayT) return;
    if (idleChat) { idleChat--; return; }
    idleChat = 500 + Rnd(400);
    if (pet.sick) { line = "I feel poorly. Medicine please?"; g = SG_GROWL; }
    else if (pet.food < 30) { line = "My tummy is rumbling! Food please!"; g = SG_NOM; }
    else if (pet.energy < 25) { line = "I'm getting sleepy..."; g = SG_WAKEUP; }
    else if (pet.clean < 30) { line = "I'm a bit smelly. Bath time?"; g = SG_NO; }
    else if (pet.fun < 30) { line = "I'm bored! Let's play!"; g = SG_GROWL; }
    else {
        static const char *happy[] = { "I love you!", "Let's go to the park!", "You're my best friend!",
                                       "Can we go shopping?", "I'm a happy bear!", "Hug please!",
                                       "Shall we play a game?", "I wonder what Rosie is doing." };
        line = happy[Rnd(8)];
    }
    SayMood(ME, line, g);
}

static void UpdateActivity(void)
{
    Actor *f = &actors[friendSlot];
    actT++;
    switch (act) {
    case ACT_EAT:
        if (actT == 1) Particle_Add(eatImg, ME->x + 24, ME->y - 22, 0, 18, 18);
        if (actT == 20) Snd_PlayGroup(SG_NOM, 256);
        if (actT > 20 && actT < 50) ME->talkT = 2;
        if (actT == 55) {
            SayMood(ME, eatImg == PET_PROP_CAKE ? "Cake! My favourite!" :
                        eatImg == PET_PROP_HONEY ? "Mmm, sweet honey!" : "Crunchy apple, yum!", SG_HAPPY);
        }
        if (actT >= 60) {
            act = ACT_IDLE;
            if (Rnd(3) == 0 && pet.poop < 3 && scene == SC_HOME) pet.poop++;
        }
        break;
    case ACT_WASH:
        if (actT % 3 == 0) Particle_Add(PET_PROP_BUBBLE, ME->x + 6 + Rnd(50), ME->y + 20 + Rnd(40), Rnd(9) - 4, -10 - Rnd(10), 30);
        ME->waveT = 2;
        if (actT >= 60) act = ACT_IDLE;
        break;
    case ACT_SLEEP:
        if (ME->tx == NOWALK && actStage == 0) {
            actStage = 1;
            Gfx_LoadPaletteTinted(pet_palette, 20, 110, 120, 190);
        }
        if (actStage && actT % 30 == 0) Particle_Add(PET_PROP_ZZZ, ME->x + 40, ME->y + 6, 8, -10, 40);
        break;
    case ACT_DANCE:
        ME->hop = (actT & 8) ? -4 : 0;
        ME->waveT = (actT & 16) ? 2 : 0;
        if (scene != SC_SHOP && Friend()) {
            Actor *ff = Friend();
            ff->hop = (actT & 8) ? 0 : -4;
            ff->waveT = (actT & 16) ? 0 : 2;
        }
        if (actT % 12 == 0) Particle_Add(PET_PROP_NOTE, ME->x + Rnd(60), ME->y, Rnd(9) - 4, -14, 30);
        if (actT % 25 == 1) Snd_PlayGroup(SG_HAPPY, 220 + (actT / 25 % 4) * 40);
        if (actT >= 125) {
            act = ACT_IDLE; ME->hop = 0;
            if (Friend()) Friend()->hop = 0;
            SayMood(ME, "That was fun!", SG_HAPPY);
        }
        break;
    case ACT_GREET:
        if (actStage == 0 && ME->tx == NOWALK) {
            char msg[48];
            actStage = 1; actT = 0;
            ME->waveT = 30; f->waveT = 30;
            sprintf(msg, "Hello %s!", bearName[f->bear]);
            Say(ME, msg);
        }
        if (actStage == 1 && actT == 45) {
            static const char *rep[] = { "Hi AG Bear! Nice to see you!", "Hello friend! Want to play?",
                                         "Hi! I like your fur today!", "Hello! Isn't the park lovely?" };
            Say(f, rep[Rnd(4)]);
            if (pet.friend[f->bear] < 5 && Rnd(2) == 0) pet.friend[f->bear]++;
            Particle_Add(PET_PROP_HEART, f->x + 28, f->y, 0, -20, 30);
        }
        if (actStage == 1 && actT > 90) act = ACT_IDLE;
        break;
    case ACT_TOGETHER:
        if (actStage == 0 && ME->tx == NOWALK) { actStage = 1; actT = 0; f->tx = NOWALK; Say(f, "Yay! Let's play tag!"); }
        if (actStage == 1) {
            /* tag: chase across the park and back */
            if (actT == 20) { WalkTo(f, f->x < 160 ? 250 : 10); f->speed = 3; }
            if (actT == 30) { WalkTo(ME, f->tx < 160 ? 60 : 200); ME->speed = 3; }
            if (actT == 90) { WalkTo(f, 150); WalkTo(ME, 90); }
            if (actT > 130 && actT % 10 == 0) Particle_Add(PET_PROP_HEART, ME->x + 50, ME->y + 4, Rnd(17) - 8, -16, 26);
            if (actT == 150) {
                ME->speed = 2; f->speed = 2; ME->waveT = 30; f->waveT = 30;
                if (pet.friend[f->bear] < 5) pet.friend[f->bear]++;
                AddStat(&pet.fun, 20); AddStat(&pet.energy, -10);
                if (Rnd(2) == 0) { pet.coins += 2; Say(f, "Tag, you're it! Here, have 2 coins!"); }
                else Say(f, "You're fast, AG Bear!");
                Snd_PlayGroup(SG_HAPPY, 256);
            }
            if (actT >= 190) act = ACT_IDLE;
        }
        break;
    case ACT_GIFT:
        if (actStage == 0 && ME->tx == NOWALK) {
            actStage = 1; actT = 0;
            pet.gifts--;
            Particle_Add(PET_PROP_GIFT, ME->x + 25, ME->y + 10, f->x > ME->x ? 24 : -24, -6, 22);
            Say(ME, "This is for you!");
        }
        if (actStage == 1 && actT == 40) {
            pet.friend[f->bear] = (UBYTE)(pet.friend[f->bear] + 2 > 5 ? 5 : pet.friend[f->bear] + 2);
            pet.coins += 3;
            f->waveT = 40;
            Say(f, "A present! Thank you! Here are 3 coins!");
            Snd_PlayGroup(SG_HAPPY, 320);
        }
        if (actStage == 1 && actT > 40 && actT < 80 && actT % 6 == 0)
            Particle_Add(PET_PROP_HEART, f->x + 10 + Rnd(40), f->y + 10, 0, -18, 24);
        if (actStage == 1 && actT > 100) act = ACT_IDLE;
        break;
    case ACT_SWING:
        if (actStage == 0 && ME->tx == NOWALK) { actStage = 1; actT = 0; swingA = 0; swingV = 26; Say(ME, "Wheee! Higher!"); }
        if (actStage == 1) {
            /* a simple pendulum, pushed for a while then left to slow down */
            swingV -= swingA / 14;
            if (actT < 100 && ((swingV > 0 && swingA > 0) || (swingV < 0 && swingA < 0))) swingV += swingV > 0 ? 1 : -1;
            if (actT >= 100) swingV = swingV * 63 / 64;
            swingA += swingV;
            if (actT % 30 == 0) Snd_PlayGroup(SG_HAPPY, 256 + Rnd(64));
            if (actT >= 175) {
                actStage = 0; act = ACT_IDLE; swingA = 0;
                AddStat(&pet.fun, 12); AddStat(&pet.energy, -4);
                PlaceActor(ME, BEAR_AG, 22);
                SayMood(ME, "I love the swings!", SG_HAPPY);
            }
        }
        break;
    case ACT_FETCH:
        if (actStage == 0) {            /* ball flies */
            ballX += ballVX; ballY += ballVY; ballVY += 1;
            if (ballY > groundY[scene] - 12) {
                ballY = groundY[scene] - 12;
                ballVY = -ballVY / 2;
                ballVX = ballVX * 2 / 3;
                if (ballVY > -4 || actT > 75) { actStage = 1; ballVY = 0; WalkTo(ME, ballX - 30); ME->speed = 3; }
            }
            if (ballX > SCR_W - 14) { ballX = SCR_W - 14; ballVX = -ballVX; }
            if (actT > 75 && actStage == 0) { actStage = 1; ballY = groundY[scene] - 12; WalkTo(ME, ballX - 30); ME->speed = 3; }
        } else if (actStage == 1 && ME->tx == NOWALK) {
            actStage = 2; ballHeld = TRUE; WalkTo(ME, 70);
            Snd_PlayGroup(SG_YES, 256);
        } else if (actStage == 2 && ME->tx == NOWALK) {
            actStage = 3; actT = 0; ballHeld = FALSE; ME->speed = 2; ME->waveT = 30;
            AddStat(&pet.fun, 15); AddStat(&pet.energy, -5);
            SayMood(ME, "Got it! Again, again!", SG_HAPPY);
        } else if (actStage == 3 && actT > 30) {
            act = ACT_IDLE;
        }
        break;
    case ACT_TRAVEL:
        if (actStage == 0 && (ME->x >= SCR_W || actT > 120)) { actStage = 1; actT = 0; }
        if (actStage == 1) {
            Gfx_LoadPalette(pet_palette, (UWORD)(256 - actT * 32 > 0 ? 256 - actT * 32 : 0), 0);
            if (actT >= 8) { EnterScene(travelTo); actStage = 2; actT = 0; }
        } else if (actStage == 2) {
            Gfx_LoadPalette(pet_palette, (UWORD)(actT * 32 > 256 ? 256 : actT * 32), 0);
            if (actT >= 8) { act = ACT_IDLE; actStage = 0; ME->speed = 2; }
        }
        break;
    case ACT_CATCH: UpdateCatch(); break;
    case ACT_MEMORY: UpdateMemory(); break;
    default: break;
    }
}

static void HomeVisits(void)
{
    UWORD best = 0, i;
    Actor *v = &actors[1];
    if (scene != SC_HOME || Busy() || pet.sleeping) return;
    if (visitT) {
        if (--visitT == 0) { Say(v, "Bye bye! See you in the park!"); WalkTo(v, SCR_W + 10); v->speed = 3; }
        if (visitT == 400) { v->waveT = 30; ME->waveT = 30; AddStat(&pet.fun, 10); Particle_Add(PET_PROP_HEART, ME->x + 40, ME->y, 0, -18, 30); }
        Wander(v, 120, 240);
        return;
    }
    if (v->on && v->x >= SCR_W) v->on = FALSE;
    for (i = 1; i < NUM_BEARS; i++) if (pet.friend[i] > best) best = pet.friend[i];
    if (best >= 3 && !v->on && Rnd(FPS * 90) == 0) {
        UBYTE who;
        do who = (UBYTE)(1 + Rnd(3)); while (pet.friend[who] < 3);
        PlaceActor(v, who, SCR_W);
        WalkTo(v, 200);
        visitT = 450;
        Say(v, "Knock knock! Can I come and play?");
    }
}

static void Update(void)
{
    ULONG s = Gfx_Seconds();
    UWORD i;
    if (s != lastSec) { if (lastSec) SecondTick(); lastSec = s; }
    for (i = 0; i < MAX_ACTORS; i++) UpdateActor(&actors[i]);
    for (i = 0; i < MAX_PARTS; i++) {
        Particle *p = &parts[i];
        if (!p->on) continue;
        p->x += p->vx; p->y += p->vy;
        if (p->img == PET_PROP_COIN || p->img == PET_PROP_GIFT) p->vy += 2;
        if (--p->life == 0) p->on = FALSE;
    }
    if (act != ACT_IDLE) UpdateActivity();
    else {
        if (!pet.sleeping) Wander(ME, 20, scene == SC_SHOP ? 150 : 240);
        IdleChatter();
    }
    if (scene == SC_PARK && act != ACT_TOGETHER && act != ACT_GREET && act != ACT_GIFT)
        for (i = 1; i < MAX_ACTORS; i++) Wander(&actors[i], 110, 250);
    HomeVisits();
}

/* ------------------------------------------------------------------ drawing */
static WORD bubTop[MAX_ACTORS], bubBot[MAX_ACTORS], bubL[MAX_ACTORS], bubR[MAX_ACTORS];

static void DrawBubble(const Actor *a, UWORD slot)
{
    WORD l0 = (WORD)strlen(a->say[0]), l1 = (WORD)strlen(a->say[1]);
    WORD w = (l0 > l1 ? l0 : l1) * 8 + 10, h = l1 ? 24 : 14;
    WORD cx = a->x + BEAR_W / 2, x = Clamp(cx - w / 2, 2, SCR_W - w - 2);
    WORD y = a->y + a->hop - h - 8, tail = Clamp(cx, x + 4, x + w - 8);
    UWORD i;
    bubTop[slot] = bubBot[slot] = 0;
    if (!a->on || !a->sayT || !l0) return;
    if (y < 2) y = 2;
    /* keep clear of the bubbles already drawn (AG Bear's comes first) */
    for (i = 0; i < slot; i++)
        if (bubBot[i] && x < bubR[i] && x + w > bubL[i] && y < bubBot[i] && y + h > bubTop[i])
            y = bubTop[i] - h - 4 >= 2 ? bubTop[i] - h - 4 : bubBot[i] + 4;
    bubTop[slot] = y; bubBot[slot] = y + h + 3; bubL[slot] = x; bubR[slot] = x + w;
    Gfx_Fill(PEN_OUTLINE, x, y, w, h);
    Gfx_Fill(PEN_WHITE, x + 1, y + 1, w - 2, h - 2);
    if (y + h + 4 < a->y + a->hop) {
        Gfx_Fill(PEN_OUTLINE, tail - 1, y + h, 5, 2);
        Gfx_Fill(PEN_WHITE, tail, y + h - 1, 3, 2);
        Gfx_Fill(PEN_OUTLINE, tail, y + h + 2, 2, 2);
    }
    Gfx_Text(x + 5, y + 3, PEN_BLACK, a->say[0]);
    if (l1) Gfx_Text(x + 5, y + 13, PEN_BLACK, a->say[1]);
}

static UBYTE ActorFrame(const Actor *a)
{
    UBYTE step = (UBYTE)((a->anim >> 2) & 1);
    if (a == ME && pet.sleeping && act == ACT_SLEEP && actStage) return F_BACK;
    if (a->dir == 1) return F_WR0 + step;
    if (a->dir == 2) return F_WL0 + step;
    if (a->dir == 3) return F_BACK;
    if (a->waveT) return (a->waveT & 4) ? F_WAVE : F_FRONT;
    if (a->talkT || (a == ME && Speech_Busy() && a->sayT)) return (a->talkT & 2) || (frame & 2) ? F_TALK : F_FRONT;
    return F_FRONT;
}

static void DrawActor(const Actor *a, WORD clipBottom)
{
    UBYTE f = ActorFrame(a);
    WORD x = a->x, y = a->y + a->hop;
    WORD h = BEAR_H;
    static const BYTE shX[8] = { 18, 18, 18, -1, 30, 30, 24, 24 }, shY[8] = { 12, 12, 12, 0, 10, 10, 10, 10 };
    static const BYTE hatCX[8] = { 34, 34, 34, 31, 32, 32, 29, 29 }, hatB[8] = { 5, 5, 5, 4, 4, 4, 4, 4 };
    if (!a->on) return;
    if (clipBottom && y + h > clipBottom) h = clipBottom - y;
    Gfx_ImagePart(bearImg[a->bear][f], 0, 0, BEAR_W, h, x, y);
    if (a != ME) return;
    if (pet.wearShades && shX[f] >= 0) {
        UWORD im = f <= F_WAVE ? PET_PET_SHADES : f <= F_WR1 ? PET_PET_SHADES_SIDE : PET_PET_SHADES_SIDEL;
        Gfx_Image(imgs[im], x + shX[f], y + shY[f]);
    }
    if (pet.wearHat == 1) Gfx_Image(imgs[PET_PET_PARTYHAT], x + hatCX[f] - 10, y + hatB[f] - 24);
    if (pet.wearHat == 2) Gfx_Image(imgs[PET_PET_CROWN], x + hatCX[f] - 12, y + hatB[f] + 2 - 16);
}

static void DrawSwing(void)
{
    /* pivot in the middle of the swing frame's top bar */
    static const WORD sinT[33] = { 0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256,
                                   255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98, 74, 50, 25, 0 };
    WORD a = Clamp(swingA / 16, -15, 15), s, c, sx, sy;
    s = a >= 0 ? sinT[a] : -sinT[-a];
    c = sinT[16 - (a < 0 ? -a : a)];
    sx = 53 + (WORD)((LONG)s * 62 / 256);
    sy = 67 + (WORD)((LONG)c * 62 / 256);
    Gfx_Line(PEN_OUTLINE, 45, 68, sx - 8, sy);
    Gfx_Line(PEN_OUTLINE, 61, 68, sx + 8, sy);
    if (act == ACT_SWING && actStage == 1) {
        Actor tmp = *ME;
        tmp.x = sx - 32; tmp.y = sy - 50; tmp.dir = 0; tmp.hop = 0; tmp.waveT = 0;
        DrawActor(&tmp, 0);
        ME->x = tmp.x;    /* keep the bubble with him */
    }
    Gfx_Fill(PEN_OUTLINE, sx - 11, sy, 23, 4);
    Gfx_Fill(PEN_ORANGE, sx - 10, sy + 1, 21, 2);
}

static void DrawMeter(WORD x, UWORD icon, WORD v)
{
    UBYTE pen = v > 60 ? PEN_GREEN : v > 30 ? PEN_YELLOW : PEN_RED;
    WORD w = v * 34 / 100;
    if (icon) Gfx_Image(imgs[icon], x, 201);
    else { Gfx_Fill(PEN_RED, x + 3, 202, 4, 10); Gfx_Fill(PEN_RED, x, 205, 10, 4); }
    Gfx_Fill(PEN_PANEL_DK, x + 13, 204, 36, 6);
    if (w) Gfx_Fill(pen, x + 14, 205, w, 4);
}

static void DrawPanel(void)
{
    const Button *b = Buttons();
    char buf[12];
    WORD i;
    Gfx_Fill(PEN_PANEL, 0, PANEL_Y, SCR_W, SCR_H - PANEL_Y);
    Gfx_Fill(PEN_OUTLINE, 0, PANEL_Y, SCR_W, 1);
    DrawMeter(2, PET_PROP_APPLE, pet.food);
    DrawMeter(53, PET_PROP_HEART, pet.fun);
    DrawMeter(104, PET_PROP_ZZZ, pet.energy);
    DrawMeter(155, PET_PROP_BUBBLE, pet.clean);
    DrawMeter(206, 0, pet.health);
    Gfx_Image(imgs[PET_PROP_COIN], 262, 202);
    sprintf(buf, "%u", pet.coins);
    Gfx_Text(274, 203, PEN_YELLOW, buf);
    for (i = 0; i < NUM_BTNS; i++) {
        WORD x = i * 40, y = BTN_Y;
        BOOL hot = (i == hover) || (useJoySel && i == joySel);
        if (b[i].act == A_NONE) continue;
        Gfx_Frame(x, y, BTN_W, BTN_H, hot ? PEN_WHITE : PEN_PANEL_LT, PEN_PANEL_DK, hot ? PEN_PANEL_LT : PEN_PANEL);
        Gfx_Image(imgs[b[i].icon], x + 7, y + 6);
        buf[0] = 0;
        switch (b[i].act) {
        case A_FEED: if (pet.honey + pet.cake) sprintf(buf, "%u", pet.honey + pet.cake); break;
        case A_MEDI: if (pet.medi) sprintf(buf, "%u", pet.medi); break;
        case A_GIFT: sprintf(buf, "%u", pet.gifts); break;
        case A_BUY_HONEY: strcpy(buf, "5"); break;
        case A_BUY_CAKE: strcpy(buf, "8"); break;
        case A_BUY_MEDI: strcpy(buf, "6"); break;
        case A_BUY_GIFT: strcpy(buf, "6"); break;
        case A_BUY_BALL: strcpy(buf, pet.ball ? "*" : "15"); break;
        case A_BUY_SHADES: strcpy(buf, pet.shades ? "*" : "20"); break;
        case A_BUY_HAT: strcpy(buf, pet.hat ? "*" : "20"); break;
        case A_WEAR_SHADES: if (!pet.shades) strcpy(buf, "?"); break;
        case A_WEAR_HAT: if (!pet.hat) strcpy(buf, "?"); break;
        case A_WEAR_CROWN: if (!pet.crown) strcpy(buf, "?"); break;
        case A_VOICE: strcpy(buf, pet.voiceOn ? "on" : "off"); break;
        default: break;
        }
        if (buf[0]) Gfx_TextOutlined(x + BTN_W - 2 - Gfx_TextWidth(buf), y + BTN_H - 10, PEN_WHITE, PEN_BLACK, buf);
    }
    /* tooltip for the button under the mouse / joystick */
    i = useJoySel ? joySel : hover;
    if (i >= 0 && b[i].act != A_NONE && b[i].tip && act != ACT_CATCH) {
        WORD w = Gfx_TextWidth(b[i].tip) + 8;
        WORD x = Clamp(i * 40 + 20 - w / 2, 1, SCR_W - w - 1);
        Gfx_Fill(PEN_OUTLINE, x, 187, w, 12);
        Gfx_Fill(PEN_CREAM, x + 1, 188, w - 2, 10);
        Gfx_Text(x + 4, 189, PEN_BLACK, b[i].tip);
    }
}

static void DrawStats(void)
{
    char buf[80];
    WORD x = 16, y = 24, i;
    ULONG days = (Gfx_Seconds() - pet.born) / 86400UL;
    Gfx_Fill(PEN_OUTLINE, x, y, 288, 150);
    Gfx_Fill(PEN_CREAM, x + 2, y + 2, 284, 146);
    sprintf(buf, "AG Bear - %lu day%s old", (unsigned long)days, days == 1 ? "" : "s");
    Gfx_Text(x + 10, y + 8, PEN_OUTLINE, buf);
    sprintf(buf, "Coins %u   Best catch %u", pet.coins, pet.bestCatch);
    Gfx_Text(x + 10, y + 22, PEN_BLACK, buf);
    sprintf(buf, "Best memory level %u", pet.bestMemory);
    Gfx_Text(x + 10, y + 32, PEN_BLACK, buf);
    Gfx_Text(x + 10, y + 48, PEN_OUTLINE, "Friends");
    for (i = 1; i < NUM_BEARS; i++) {
        WORD h;
        Gfx_Text(x + 20, y + 50 + i * 11, PEN_BLACK, bearName[i]);
        for (h = 0; h < 5; h++)
            if (h < pet.friend[i]) Gfx_Image(imgs[PET_PROP_HEART], x + 80 + h * 11, y + 50 + i * 11);
            else Gfx_Fill(PEN_GREY, x + 82 + h * 11, y + 52 + i * 11, 5, 4);
    }
    sprintf(buf, "Bag: %u honey, %u cake, %u medicine", pet.honey, pet.cake, pet.medi);
    Gfx_Text(x + 10, y + 96, PEN_BLACK, buf);
    sprintf(buf, "     %u present%s", pet.gifts, pet.gifts == 1 ? "" : "s");
    Gfx_Text(x + 10, y + 106, PEN_BLACK, buf);
    sprintf(buf, "Toys:%s%s%s%s", pet.ball ? " ball" : "", pet.shades ? " shades" : "",
            pet.hat ? " hat" : "", pet.crown ? " crown" : "");
    if (!pet.ball && !pet.shades && !pet.hat && !pet.crown) strcat(buf, " none yet");
    Gfx_Text(x + 10, y + 118, PEN_BLACK, buf);
    Gfx_Text(x + 10, y + 132, PEN_OUTLINE, speechOK ? "Voice: narrator.device" : "Voice: bear noises");
}

static void DrawAll(void)
{
    static const UWORD sceneImg[NUM_SCENES] = { PET_SCENE_HOME, PET_SCENE_GARDEN, PET_SCENE_PARK, PET_SCENE_SHOP };
    UWORD i;
    Gfx_Begin();
    Gfx_Image(imgs[sceneImg[scene]], 0, 0);
    if (scene == SC_HOME)
        for (i = 0; i < pet.poop; i++) Gfx_Image(imgs[PET_PROP_POOP], 150 + i * 40, 180 + (i & 1) * 4);
    if (act == ACT_MEMORY) DrawMemory();
    if (scene == SC_PARK) DrawSwing();
    if (act != ACT_CATCH && act != ACT_MEMORY)
        for (i = 1; i < MAX_ACTORS; i++) DrawActor(&actors[i], scene == SC_SHOP ? 146 : 0);
    if (!(act == ACT_SWING && actStage == 1)) DrawActor(ME, 0);
    if (act == ACT_FETCH && actStage < 3)
        Gfx_Image(imgs[PET_PROP_BALL], ballHeld ? ME->x + (ME->dir == 2 ? 2 : 48) : ballX,
                  ballHeld ? ME->y + 30 : ballY);
    if (act == ACT_CATCH) DrawCatch();
    for (i = 0; i < MAX_PARTS; i++)
        if (parts[i].on) Gfx_Image(imgs[parts[i].img], (WORD)(parts[i].x >> 4), (WORD)(parts[i].y >> 4));
    for (i = 0; i < MAX_ACTORS; i++)
        if (i == 0 || (act != ACT_CATCH && act != ACT_MEMORY)) DrawBubble(&actors[i], i);
    if (panel == P_MENU) DrawStats();
    DrawPanel();
    Gfx_End();
}

/* ------------------------------------------------------------------ input */
static void HandleInput(void)
{
    const Button *b = Buttons();
    WORD i;
    hover = -1;
    if (in.my >= BTN_Y && in.my < BTN_Y + BTN_H && in.mx >= 0 && in.mx < SCR_W && (in.mx % 40) < BTN_W)
        hover = in.mx / 40;
    if (in.mx != 0 || in.my != 0) { if (hover >= 0) useJoySel = FALSE; }
    if (act == ACT_CATCH || act == ACT_MEMORY || act == ACT_TRAVEL) {
        if (in.quit) quitting = TRUE;
        return;
    }
    if (in.quit) { quitting = TRUE; return; }
    /* joystick / cursor keys choose a button, fire presses it */
    {
        static BOOL lastL, lastR;
        if (in.left && !lastL) { useJoySel = TRUE; do joySel = (joySel + NUM_BTNS - 1) % NUM_BTNS; while (b[joySel].act == A_NONE); }
        if (in.right && !lastR) { useJoySel = TRUE; do joySel = (joySel + 1) % NUM_BTNS; while (b[joySel].act == A_NONE); }
        lastL = in.left; lastR = in.right;
        if (in.firePressed && useJoySel) { DoAction(b[joySel].act); if (Buttons() != b) joySel = 0; return; }
    }
    if (in.key >= '1' && in.key <= '8') { DoAction(b[in.key - '1'].act); return; }
    if (in.click) {
        if (hover >= 0) { DoAction(b[hover].act); if (Buttons() != b) joySel = 0; return; }
        /* clicking AG Bear: a tickle */
        if (in.my < STAGE_H && in.mx >= ME->x + 8 && in.mx < ME->x + 56 && in.my >= ME->y && in.my < ME->y + BEAR_H &&
            !Busy()) {
            static const char *tickle[] = { "Hee hee! That tickles!", "Ha ha ha!", "Stop it! Hee hee!", "I love hugs!" };
            if (pet.sleeping) { WakeUp(); return; }
            ME->waveT = 24; ME->tx = NOWALK;
            AddStat(&pet.fun, 3);
            SayMood(ME, tickle[Rnd(4)], SG_HAPPY);
            for (i = 0; i < 3; i++) Particle_Add(PET_PROP_HEART, ME->x + 16 + i * 14, ME->y + 4, (i - 1) * 6, -18, 24);
            return;
        }
        if (in.my < STAGE_H && panel != P_MAIN) panel = P_MAIN;
    }
    if (in.rclick && panel != P_MAIN) panel = P_MAIN;
}

/* ------------------------------------------------------------------ main */
static BOOL LoadImages(void)
{
    UWORD i, b;
    for (i = 0; i < PET_NUM_IMAGES; i++) {
        BOOL scenes = (i >= PET_SCENE_HOME && i <= PET_SCENE_SHOP);
        if (i < 8) continue;
        if (!(imgs[i] = Img_Unpack(&pet_images[i], pet_blob, 0, !scenes))) return FALSE;
    }
    for (b = 0; b < NUM_BEARS; b++)
        for (i = 0; i < 8; i++)
            if (!(bearImg[b][i] = Img_Unpack(&pet_images[PET_BEAR0 + i], pet_blob, (UBYTE)(b * PET_BEAR_COLS), TRUE)))
                return FALSE;
    return TRUE;
}

static void FreeImages(void)
{
    UWORD i, b;
    for (i = 0; i < PET_NUM_IMAGES; i++) { Img_Free(imgs[i]); imgs[i] = NULL; }
    for (b = 0; b < NUM_BEARS; b++)
        for (i = 0; i < 8; i++) { Img_Free(bearImg[b][i]); bearImg[b][i] = NULL; }
}

int main(void)
{
    int rc = 0;
    srand((unsigned)Gfx_Seconds() ^ (unsigned)(*(volatile UWORD *)0xDFF006));
    if (!Gfx_Open("AG Bear Pet", pet_palette)) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"AG Bear Pet",
            (UBYTE *)"AG Bear Pet needs an AGA Amiga (A1200/A4000)\nor a graphics card, and OS 3.0 or newer.\n(%s)",
            (UBYTE *)"OK" };
        printf("AGBearPet: %s\n", Gfx_Error);
        Gfx_Close();
        EasyRequestArgs(NULL, &es, NULL, (APTR)&Gfx_Error);
        return 20;
    }
    Gfx_LoadPalette(pet_palette, 0, 0);
    if (!LoadImages()) { rc = 20; goto out; }
    Snd_Init();
    speechOK = Speech_Init();
    if (!speechOK) Speech_Close();
    EnterScene(SC_HOME);
    LoadPet();
    {
        UWORD l;
        for (l = 0; l <= 256; l += 32) {   /* fade in */
            DrawAll();
            Gfx_LoadPalette(pet_palette, l, 0);
            WaitTOF();
        }
    }
    while (!quitting) {
        Gfx_Poll(&in);
        HandleInput();
        Update();
        Snd_Update();
        Speech_Update();
        DrawAll();
        Gfx_WaitFrame();
        frame++;
    }
    SavePet();
    {
        WORD l;
        for (l = 256; l >= 0; l -= 32) { Gfx_LoadPalette(pet_palette, (UWORD)l, 0); WaitTOF(); }
    }
out:
    Speech_Close();
    Snd_Close();
    FreeImages();
    Gfx_Close();
    return rc;
}
