/* ag-bear-tamagotchi.c — AG Bear with Hardware BOBs and Tamagotchi features for AmigaOS 3.2
 * Hardware accelerated version using proper BOBs and blitter operations
 * Now with full virtual pet functionality!
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <devices/narrator.h>
#include <libraries/translator.h>
#include <devices/audio.h>
#include <stdlib.h>  
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/gels.h>
#include <graphics/gfx.h>
#include <graphics/sprite.h>
#include <libraries/gadtools.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <utility/tagitem.h>
#include <gadgets/button.h>
#include <images/label.h>
#include <intuition/imageclass.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/datatypes.h>
#include <proto/utility.h>
#include <proto/timer.h>
#include <proto/translator.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

extern void BeginIO(struct IORequest *);
static const char __attribute__((used)) min_stack[] = "$STACK:102400";
/* ------------ Config ------------ */
#define WIN_W  360
#define WIN_H  355

#define UI_MARGIN_X 10
#define UI_MARGIN_Y 14
#define UI_STR_H    16
#define UI_BTN_W    65    /* Smaller buttons to fit more */
#define UI_BTN_H    16
#define UI_GAP_X    4
#define UI_GAP_Y    4
#define UI_ROWS     3 
#define UI_BAR_H   (UI_MARGIN_Y + UI_STR_H + UI_GAP_Y + (UI_BTN_H + UI_GAP_Y) * UI_ROWS + UI_MARGIN_Y)

#define BG_PATH  "PROGDIR:bg.ilbm"
#define SPR_PATH "PROGDIR:bear32.ilbm"

/* Tamagotchi icon paths */
#define ICON_PLAY     "PROGDIR:icons/play.ilbm"
#define ICON_CLEAN    "PROGDIR:icons/clean.ilbm" 
#define ICON_SLEEP    "PROGDIR:icons/sleep.ilbm"
#define ICON_MEDICINE "PROGDIR:icons/medicine.ilbm"
#define ICON_STATS    "PROGDIR:icons/stats.ilbm"
#define ICON_POOP     "PROGDIR:icons/poop.ilbm"

#define MAX_SOUNDS 16
#define MAX_SOUND_SIZE 32768

/* sprite sheet: 48x48 tiles, 4x2 grid at (0,0) of a 320x200 picture */
#define SPR_W 48
#define SPR_H 48
#define SPR_COLS 4
#define SPR_ROWS 2
#define SPR_ORIGIN_X 0
#define SPR_ORIGIN_Y 0

#define TICK_MS 40  /* ~12.5 FPS */

/* BOB Configuration */
#define MAX_BOBS 8
#define BEAR_DEPTH 8  /* Assume 16 color sprites */
#define BEAR_W (doubleSize ? (SPR_W*2) : SPR_W)
#define BEAR_H (doubleSize ? (SPR_H*2) : SPR_H)

/* ------------ Tamagotchi Stats and State ------------ */
typedef struct {
    UWORD hunger;       /* 0-100, 0=starving, 100=full */
    UWORD happiness;    /* 0-100, 0=miserable, 100=ecstatic */
    UWORD health;       /* 0-100, 0=sick, 100=perfect health */
    UWORD energy;       /* 0-100, 0=exhausted, 100=energetic */
    UWORD cleanliness;  /* 0-100, 0=filthy, 100=spotless */
    UWORD age;          /* Bear's age in hours */
    UWORD weight;       /* 50-150, normal around 100 */
    ULONG lastFeed;     /* Timer tick of last feeding */
    ULONG lastPlay;     /* Timer tick of last play session */
    ULONG lastClean;    /* Timer tick of last cleaning */
    ULONG lastSleep;    /* Timer tick of last sleep */
    BOOL sleeping;      /* Is bear currently sleeping? */
    BOOL sick;          /* Is bear sick? */
    BOOL pooped;        /* Has bear made a mess? */
} BearStats;

static BearStats bearStats = {0};
static ULONG gameTicks = 0;     /* Global game timer */

/* Tamagotchi constants */
#define STAT_DECAY_RATE    1        /* How fast stats decay per minute */
#define HUNGER_CRITICAL    20       /* Bear gets sick if hunger below this */
#define HAPPINESS_CRITICAL 15       /* Bear gets depressed below this */
#define HEALTH_CRITICAL    25       /* Bear might die below this */
#define ENERGY_SLEEP_POINT 30       /* Bear falls asleep below this */
#define WEIGHT_MIN         50       /* Minimum healthy weight */
#define WEIGHT_MAX         150      /* Maximum before health issues */
#define POOP_CHANCE        400      /* 1 in N chance per tick when fed */
#define SICK_CHANCE        500      /* 1 in N chance when stats low */

/* Visual indicators */
typedef enum {
    MOOD_HAPPY = 0,     /* S_FRONT_C */
    MOOD_CONTENT,       /* S_FRONT_C */
    MOOD_NEUTRAL,       /* S_FRONT_C */
    MOOD_SAD,           /* S_FRONT_O with slower blink */
    MOOD_ANGRY,         /* Custom growling animation */
    MOOD_SICK,          /* S_BACK with slow movement */
    MOOD_SLEEPING,      /* S_BACK with ZZZ effect */
    MOOD_DEAD           /* S_BACK, no movement */
} BearMood;

/* ------------ Globals ------------ */
static struct Window *win=NULL;
static struct RastPort *rp=NULL;
static APTR vi=NULL;
extern struct Library *GadToolsBase;
struct Library *DataTypesBase=NULL;
struct Library *TranslatorBase=NULL;
static void BlitSpriteFrameWithColorKey(UWORD frame, WORD dx, WORD dy);
static BOOL ValidateBobData(void);

/* BOB System */
static WORD bearSaveBuffer[4608];
static struct GelsInfo *gelsInfo = NULL;
static struct VSprite *headVSprite = NULL, *tailVSprite = NULL;
static struct Bob *bearBob = NULL;
static struct VSprite *bearVS = NULL;
static struct BitMap *bobBitMaps[SPR_COLS * SPR_ROWS] = {NULL};
static WORD *bobImageData[SPR_COLS * SPR_ROWS] = {NULL};
static WORD *bobMaskData[SPR_COLS * SPR_ROWS] = {NULL};
static BOOL bobSystemReady = FALSE;

static BOOL doubleSize = FALSE;
static struct Gadget *sizeBtn = NULL;
static struct MsgPort *tmPort=NULL;
static struct timerequest *tmReq=NULL;

/* Audio system */
static struct MsgPort *narrPort=NULL;
static struct narrator_rb *voiceIO=NULL;
static struct Gadget *gadList=NULL,*txtGad=NULL,*sayBtn=NULL,*feedBtn=NULL,*stopBtn=NULL;
static struct Gadget *playBtn=NULL,*cleanBtn=NULL,*sleepBtn=NULL,*medicineBtn=NULL,*statsBtn=NULL;
static const UBYTE kAudChanMasks[] = {3,12};

/* Global variables for tracking gadgets */
static struct Gadget *originalPlayBtn = NULL, *originalCleanBtn = NULL;
static struct Gadget *originalSleepBtn = NULL, *originalMedicineBtn = NULL, *originalStatsBtn = NULL;

/* Store original gadget pointers before replacement */
static void StoreOriginalGadgets(void) {
    originalPlayBtn = playBtn;
    originalCleanBtn = cleanBtn; 
    originalSleepBtn = sleepBtn;
    originalMedicineBtn = medicineBtn;
    originalStatsBtn = statsBtn;
}

/* Tamagotchi UI icons */
static Object *poopIcon = NULL;
static struct BitMapHeader *poopBMHD = NULL;
static struct BitMap *poopBM = NULL;

struct BearSound {
    UBYTE *data;
    ULONG length;
    UWORD period;
    char name[32];
};

static struct BearSound bearSounds[MAX_SOUNDS];
static UWORD numSounds = 0;
static struct MsgPort *audioPort = NULL;
static struct IOAudio *audioReq = NULL;
static UBYTE audioChannels[] = {1, 2, 4, 8};
static BOOL audioPlaying = FALSE;

static const char *soundFiles[] = {
    "PROGDIR:sounds/greeting1.iff", "PROGDIR:sounds/greeting2.iff", 
    "PROGDIR:sounds/growl1.iff", "PROGDIR:sounds/growl2.iff",
    "PROGDIR:sounds/happy1.iff", "PROGDIR:sounds/happy2.iff", "PROGDIR:sounds/happy3.iff",
    "PROGDIR:sounds/hi.iff", "PROGDIR:sounds/no.iff", "PROGDIR:sounds/no2.iff",
    "PROGDIR:sounds/nom1.iff", "PROGDIR:sounds/nom2.iff", "PROGDIR:sounds/wakeup.iff",
    "PROGDIR:sounds/wave.iff", "PROGDIR:sounds/yes.iff", NULL
};

typedef enum { SOUND_GROWL, SOUND_HAPPY, SOUND_NOM, SOUND_GREETING, SOUND_WAVE, 
               SOUND_AGREEMENT, SOUND_DISAGREE, SOUND_WAKEUP, SOUND_RANDOM } SoundType;

static struct { UWORD soundIndex; SoundType type; } soundMap[] = {
    {0, SOUND_GREETING}, {1, SOUND_GREETING}, {2, SOUND_GROWL}, {3, SOUND_GROWL},
    {4, SOUND_HAPPY}, {5, SOUND_HAPPY}, {6, SOUND_HAPPY}, {7, SOUND_GREETING},
    {8, SOUND_DISAGREE}, {9, SOUND_DISAGREE}, {10, SOUND_NOM}, {11, SOUND_NOM},
    {12, SOUND_WAKEUP}, {13, SOUND_WAVE}, {14, SOUND_AGREEMENT}, {0, SOUND_RANDOM}
};

/* Background */
static Object *bgObj=NULL;
static struct BitMapHeader *bgBMHD=NULL;
static struct BitMap *bgBM=NULL;
static UBYTE *bgMask=NULL;
static WORD bgX=0,bgY=0,bgW=300,bgH=240;

/* Sprite sheet */
static Object *sprObj=NULL;
static struct BitMapHeader *sprBMHD=NULL;
static struct BitMap *sprBM=NULL;

/* Bear state */
typedef enum { ST_IDLE, ST_WALK_R, ST_WALK_L, ST_WAVE, ST_FEED, ST_SLEEP, ST_SICK } BearState;
static BearState state=ST_IDLE;
static WORD bearX, bearY, bearVX;
static UWORD animFrame=0, animTick=0;
static BOOL speaking=FALSE;

static UWORD talkTick=0, talkPhase=0;
static UWORD waveTick=0;
static UWORD feedTicks=0;
static UWORD sleepTick=0;
static BearState resumeState=ST_IDLE;

static WORD lastPoopX = -1, lastPoopY = -1;  /* Track poop position for cleanup */
static WORD lastBearDrawX = -1, lastBearDrawY = -1, lastBearDrawW = 0, lastBearDrawH = 0;

/* Frame indices */
enum { S_FRONT_C=0, S_FRONT_O=1, S_WAVE=2, S_BACK=3,
       S_WR0=4, S_WR1=5, S_WL0=6, S_WL1=7 };
static const UWORD walkRSeq[4]={S_WR0,S_WR1,S_WR0,S_WR1};
static const UWORD walkLSeq[4]={S_WL0,S_WL1,S_WL0,S_WL1};
static const UWORD talkSeq[2] ={S_FRONT_O,S_FRONT_C};

/* ------------ Function Prototypes ------------ */
static void PlayBearSound(SoundType type);
static void UpdateTamagotchiStats(void);
static void InitTamagotchiStats(void);
static BearMood GetBearMood(void);
static void FeedBear(void);
static void PlayWithBear(void);
static void CleanBear(void);
static void ToggleBearSleep(void);
static void MedicineBear(void);
static void UpdateStatsDisplay(void);
static BOOL SaveBearStats(const char *filename);
static BOOL LoadBearStats(const char *filename);
static void ShowStatsRequester(void);
static BOOL LoadPoopIcon(void);
static struct Image *LoadButtonIcon(const char *path, Object **iconObj, struct BitMap **iconBM);
static void LoadButtonIcons(void);
static void FreeButtonIcons(void);
static void RestoreBGRect(WORD dx, WORD dy, WORD w, WORD h);
/* Icon structures for buttons */
static Object *playIcon = NULL, *cleanIcon = NULL, *sleepIcon = NULL;
static Object *medicineIcon = NULL, *statsIcon = NULL;
static struct BitMap *playBM = NULL, *cleanBM = NULL, *sleepBM = NULL;
static struct BitMap *medicineBM = NULL, *statsBM = NULL;
static struct Image *playImage = NULL, *cleanImage = NULL, *sleepImage = NULL;
static struct Image *medicineImage = NULL, *statsImage = NULL;

/* Proper Intuition Image Gadgets using GFLG_GADGIMAGE */

/* Replace your entire button creation approach with this classic Intuition method */

/* Create a proper Intuition gadget with image using GFLG_GADGIMAGE */
static struct Gadget *CreateImageGadget(UWORD id, WORD x, WORD y, struct Image *img, struct Gadget *prevGadget) {
    if (!img) return NULL;
    
    /* Allocate gadget structure */
    struct Gadget *gad = AllocVec(sizeof(struct Gadget), MEMF_ANY | MEMF_CLEAR);
    if (!gad) return NULL;
    
    /* Set up the gadget structure */
    gad->NextGadget = NULL;
    gad->LeftEdge = x;
    gad->TopEdge = y;
    gad->Width = UI_BTN_W;
    gad->Height = UI_BTN_H;
    gad->Flags = GFLG_GADGHCOMP | GFLG_GADGIMAGE;  /* CRITICAL: Set GFLG_GADGIMAGE for bitmap */
    gad->Activation = GACT_RELVERIFY;
    gad->GadgetType = GTYP_BOOLGADGET;
    gad->GadgetRender = (APTR)img;  /* Point to our Image structure */
    gad->SelectRender = NULL;       /* No select imagery for now */
    gad->GadgetText = NULL;         /* No text */
    gad->MutualExclude = 0;
    gad->SpecialInfo = NULL;
    gad->GadgetID = id;
    gad->UserData = NULL;
    
  
    printf("Created Intuition image gadget ID %d\n", id);
    return gad;
}

/* Fix 1: Update the image gadget creation to work with new layout */

static void CreateIntuitionImageGadgets(void) {
    printf("Creating Intuition image gadgets...\n");
    
    /* Remove old gadgets we want to replace */
    if (playBtn) RemoveGList(win, playBtn, 1);
    if (cleanBtn) RemoveGList(win, cleanBtn, 1);
    if (sleepBtn) RemoveGList(win, sleepBtn, 1);
    if (medicineBtn) RemoveGList(win, medicineBtn, 1);
    if (statsBtn) RemoveGList(win, statsBtn, 1);
    
    /* Calculate positions for new compact layout */
    WORD btnPerRow = 4;
    WORD totalBtnWidth = btnPerRow * UI_BTN_W + (btnPerRow-1) * UI_GAP_X;
    WORD startX = (win->Width - totalBtnWidth) / 2;
    
    /* Row 1 positions */
    WORD y1 = UI_MARGIN_Y + UI_STR_H + UI_GAP_Y;
    WORD playX = startX + UI_BTN_W + UI_GAP_X;           /* 2nd position */
    WORD cleanX = startX + 2 * (UI_BTN_W + UI_GAP_X);    /* 3rd position */
    WORD sleepX = startX + 3 * (UI_BTN_W + UI_GAP_X);    /* 4th position */
    
    /* Row 2 positions */
    WORD y2 = y1 + UI_BTN_H + UI_GAP_Y;
    WORD medX = startX;                                   /* 1st position row 2 */
    WORD statsX = startX + UI_BTN_W + UI_GAP_X;          /* 2nd position row 2 */
    
    /* Create new image gadgets with correct positions */
    if (playImage) {
        playBtn = CreateImageGadget(6, playX, y1, playImage, feedBtn);
        if (playBtn) AddGList(win, playBtn, (UWORD)-1, 1, NULL);
    }
    
    if (cleanImage) {
        cleanBtn = CreateImageGadget(7, cleanX, y1, cleanImage, playBtn ? playBtn : feedBtn);
        if (cleanBtn) AddGList(win, cleanBtn, (UWORD)-1, 1, NULL);
    }
    
    if (sleepImage) {
        sleepBtn = CreateImageGadget(8, sleepX, y1, sleepImage, cleanBtn ? cleanBtn : feedBtn);
        if (sleepBtn) AddGList(win, sleepBtn, (UWORD)-1, 1, NULL);
    }
    
    if (medicineImage) {
        medicineBtn = CreateImageGadget(9, medX, y2, medicineImage, sleepBtn ? sleepBtn : feedBtn);
        if (medicineBtn) AddGList(win, medicineBtn, (UWORD)-1, 1, NULL);
    }
    
    if (statsImage) {
        statsBtn = CreateImageGadget(10, statsX, y2, statsImage, medicineBtn ? medicineBtn : feedBtn);
        if (statsBtn) AddGList(win, statsBtn, (UWORD)-1, 1, NULL);
    }
    
    RefreshGList(gadList, win, NULL, (UWORD)-1);
    GT_RefreshWindow(win, NULL);
    printf("Intuition image gadgets created\n");
}
/* ------------ Tamagotchi Functions ------------ */

/* Initialize bear with baby stats */
static void InitTamagotchiStats(void) {
    bearStats.hunger = 70;      /* Hungry baby */
    bearStats.happiness = 80;   /* Happy to be alive */
    bearStats.health = 100;     /* Perfect baby health */
    bearStats.energy = 60;      /* Babies get tired */
    bearStats.cleanliness = 100; /* Clean newborn */
    bearStats.age = 0;
    bearStats.weight = 80;      /* Small baby bear */
    bearStats.lastFeed = gameTicks;
    bearStats.lastPlay = gameTicks;
    bearStats.lastClean = gameTicks;
    bearStats.lastSleep = gameTicks;
    bearStats.sleeping = FALSE;
    bearStats.sick = FALSE;
    bearStats.pooped = FALSE;
    
    printf("Tamagotchi stats initialized - baby bear born!\n");
}

/* Calculate current mood based on stats */
static BearMood GetBearMood(void) {
    if (bearStats.health <= 0) return MOOD_DEAD;
    if (bearStats.sleeping) return MOOD_SLEEPING;
    if (bearStats.sick) return MOOD_SICK;
    
    /* Weighted mood calculation */
    WORD moodScore = (bearStats.happiness * 2 + bearStats.health + bearStats.energy) / 4;
    
    if (bearStats.hunger < HUNGER_CRITICAL) moodScore -= 30;
    if (bearStats.cleanliness < 30) moodScore -= 20;
    
    if (moodScore >= 80) return MOOD_HAPPY;
    if (moodScore >= 60) return MOOD_CONTENT;
    if (moodScore >= 40) return MOOD_NEUTRAL;
    if (moodScore >= 20) return MOOD_SAD;
    return MOOD_ANGRY;
}

/* Update stats over time - call this every game tick */
static void UpdateTamagotchiStats(void) {
    gameTicks++;
    
    /* Age the bear every 3600 ticks (roughly 5 minutes real time) */
    if ((gameTicks % 3600) == 0) {
        bearStats.age++;
        printf("Bear is now %d hours old\n", bearStats.age);
    }
    
    /* Gradual stat decay every 450 ticks (~36 seconds) */
    if ((gameTicks % 450) == 0) {
        /* Hunger increases (gets hungrier) */
        if (bearStats.hunger > 0) bearStats.hunger--;
        
        /* Energy decreases if awake */
        if (!bearStats.sleeping && bearStats.energy > 0) {
            bearStats.energy--;
        }
        
        /* Cleanliness decreases */
        if (bearStats.cleanliness > 0) bearStats.cleanliness--;
        
        /* Happiness decreases if other needs aren't met */
        if ((bearStats.hunger < 50 || bearStats.energy < 30 || bearStats.cleanliness < 40) 
            && bearStats.happiness > 0) {
            bearStats.happiness--;
        }
    }
    
    /* Health management */
    if ((gameTicks % 600) == 0) {  /* Every 48 seconds */
        BOOL healthRisk = FALSE;
        
        /* Health problems from neglect */
        if (bearStats.hunger < HUNGER_CRITICAL) healthRisk = TRUE;
        if (bearStats.cleanliness < 20) healthRisk = TRUE;
        if (bearStats.happiness < HAPPINESS_CRITICAL) healthRisk = TRUE;
        if (bearStats.weight < WEIGHT_MIN || bearStats.weight > WEIGHT_MAX) healthRisk = TRUE;
        
        if (healthRisk && bearStats.health > 0) {
            bearStats.health--;
            if ((rand() % SICK_CHANCE) == 0) {
                bearStats.sick = TRUE;
                PlayBearSound(SOUND_GROWL);  /* Sick sound */
                printf("Bear got sick! Health: %d\n", bearStats.health);
            }
        } else if (!healthRisk && bearStats.health < 100 && !bearStats.sick) {
            bearStats.health++;  /* Gradual recovery when well cared for */
        }
    }
    
    /* Automatic sleep when exhausted */
    if (bearStats.energy <= ENERGY_SLEEP_POINT && !bearStats.sleeping) {
        bearStats.sleeping = TRUE;
        state = ST_SLEEP;
        PlayBearSound(SOUND_WAKEUP);  /* Yawn sound */
        printf("Bear fell asleep from exhaustion\n");
    }
    
    /* Wake up when rested */
    if (bearStats.sleeping && bearStats.energy >= 80) {
        bearStats.sleeping = FALSE;
        state = ST_IDLE;
        PlayBearSound(SOUND_GREETING);
        printf("Bear woke up refreshed\n");
    }
    
    /* Energy recovery during sleep */
    if (bearStats.sleeping && (gameTicks % 300) == 0) {  /* Every 24 seconds */
        if (bearStats.energy < 100) bearStats.energy += 2;
    }
    
    /* Pooping chance after feeding */
    if (bearStats.hunger > 70 && !bearStats.pooped && (rand() % POOP_CHANCE) == 0) {
        bearStats.pooped = TRUE;
        bearStats.cleanliness -= 20;
        if (bearStats.cleanliness < 0) bearStats.cleanliness = 0;
        printf("Bear made a mess!\n");
    }
    
    /* Death from neglect */
    if (bearStats.health <= 0 && !bearStats.sick) {
        printf("Bear died from neglect! Game over.\n");
        state = ST_SICK;  /* Use sick animation for death */
    }
}

/* Feed the bear - enhanced */
static void FeedBear(void) {
    if (bearStats.hunger >= 100) {
        PlayBearSound(SOUND_DISAGREE);  /* "I'm full!" */
        return;
    }
    
    /* Original feeding animation */
    feedTicks = 12;
    if (state != ST_FEED) {
        resumeState = (bearVX > 0) ? ST_WALK_R : ST_WALK_L;
        state = ST_FEED;
    }
    
    /* Update stats */
    bearStats.hunger += 15;
    if (bearStats.hunger > 100) bearStats.hunger = 100;
    
    bearStats.happiness += 5;
    if (bearStats.happiness > 100) bearStats.happiness = 100;
    
    bearStats.weight += 1;
    bearStats.lastFeed = gameTicks;
    
    /* Wake up if sleeping and really hungry */
    if (bearStats.sleeping && bearStats.hunger < 30) {
        bearStats.sleeping = FALSE;
        state = ST_IDLE;
        PlayBearSound(SOUND_WAKEUP);
    } else {
        PlayBearSound(SOUND_NOM);
    }
    
    printf("Fed bear: Hunger %d, Happiness %d, Weight %d\n", 
           bearStats.hunger, bearStats.happiness, bearStats.weight);
}

/* Play with the bear */
static void PlayWithBear(void) {
    if (bearStats.sleeping) {
        PlayBearSound(SOUND_WAKEUP);  /* Wake up for play */
        bearStats.sleeping = FALSE;
        state = ST_IDLE;
    }
    
    if (bearStats.energy < 20) {
        PlayBearSound(SOUND_DISAGREE);  /* Too tired to play */
        return;
    }
    
    /* Force wave animation */
    state = ST_WAVE;
    waveTick = 0;
    
    /* Update stats */
    bearStats.happiness += 20;
    if (bearStats.happiness > 100) bearStats.happiness = 100;
    
    /* Playing is tiring */
    bearStats.energy = (bearStats.energy > 10) ? (bearStats.energy - 10) : 0;
    
    bearStats.weight -= 1;  /* Exercise helps weight */
    if (bearStats.weight < WEIGHT_MIN) bearStats.weight = WEIGHT_MIN;
    
    bearStats.lastPlay = gameTicks;
    
    PlayBearSound(SOUND_HAPPY);
    printf("Played with bear: Happiness %d, Energy %d\n", 
           bearStats.happiness, bearStats.energy);
}

/* Clean the bear */
static void CleanBear(void) {
    if (bearStats.cleanliness >= 100 && !bearStats.pooped) {
        PlayBearSound(SOUND_DISAGREE);  /* "I'm already clean!" */
        return;
    }
    
    /* CLEANUP POOP VISUAL when cleaning */
    if (bearStats.pooped && lastPoopX >= 0 && lastPoopY >= 0) {
        if (poopBMHD) {
            RestoreBGRect(lastPoopX, lastPoopY, poopBMHD->bmh_Width, poopBMHD->bmh_Height);
        }
        lastPoopX = lastPoopY = -1;  /* Mark as cleaned */
    }
    
    bearStats.cleanliness = 100;
    bearStats.pooped = FALSE;
    bearStats.happiness += 10;
    if (bearStats.happiness > 100) bearStats.happiness = 100;
    
    /* Cure sickness if it was caused by dirt */
    if (bearStats.sick && bearStats.health > 50) {
        bearStats.sick = FALSE;
        state = ST_IDLE;
        printf("Cleaning cured the bear's sickness!\n");
    }
    
    bearStats.lastClean = gameTicks;
    
    PlayBearSound(SOUND_HAPPY);
    printf("Cleaned bear: Cleanliness %d, Happiness %d\n", 
           bearStats.cleanliness, bearStats.happiness);
}

/* Put bear to sleep or wake up */
static void ToggleBearSleep(void) {
    if (bearStats.sleeping) {
        bearStats.sleeping = FALSE;
        state = ST_IDLE;
        PlayBearSound(SOUND_GREETING);
        printf("Woke up bear\n");
    } else {
        bearStats.sleeping = TRUE;
        state = ST_SLEEP;
        PlayBearSound(SOUND_WAKEUP);  /* Yawn */
        printf("Put bear to sleep\n");
    }
}

/* Give medicine to sick bear */
static void MedicineBear(void) {
    if (!bearStats.sick) {
        PlayBearSound(SOUND_DISAGREE);
        return;
    }
    
    bearStats.sick = FALSE;
    state = ST_IDLE;
    bearStats.health += 30;
    if (bearStats.health > 100) bearStats.health = 100;
    
    bearStats.happiness += 15;  /* Feeling better makes bear happy */
    if (bearStats.happiness > 100) bearStats.happiness = 100;
    
    PlayBearSound(SOUND_AGREEMENT);
    printf("Gave medicine: Health %d, no longer sick\n", bearStats.health);
}

/* Display stats in window title */
static void UpdateStatsDisplay(void) {
    static char titleBuf[256];
    BearMood mood = GetBearMood();
    
    const char *moodStr[] = {
        "Happy", "Content", "Neutral", "Sad", "Angry", "Sick", "Sleeping", "Dead"
    };
    
    snprintf(titleBuf, sizeof(titleBuf), 
             "AG Bear [%s] - H:%d E:%d C:%d Hp:%d Age:%dh Wt:%d", 
             moodStr[mood], bearStats.hunger, bearStats.energy, 
             bearStats.cleanliness, bearStats.happiness, bearStats.age, bearStats.weight);
    
    if (win) {
        SetWindowTitles(win, (UBYTE*)titleBuf, (UBYTE*)-1);
    }
}

/* Simple save/load system using a text file */
static BOOL SaveBearStats(const char *filename) {
    BPTR file = Open((STRPTR)filename, MODE_NEWFILE);
    if (!file) return FALSE;
    
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "BEAR_SAVE_V1\n%d %d %d %d %d %d %d %lu %lu %lu %lu %d %d %d\n",
             bearStats.hunger, bearStats.happiness, bearStats.health,
             bearStats.energy, bearStats.cleanliness, bearStats.age, bearStats.weight,
             bearStats.lastFeed, bearStats.lastPlay, bearStats.lastClean, bearStats.lastSleep,
             bearStats.sleeping ? 1 : 0, bearStats.sick ? 1 : 0, bearStats.pooped ? 1 : 0);
    
    LONG written = Write(file, buffer, strlen(buffer));
    Close(file);
    
    printf("Saved bear stats to %s\n", filename);
    return (written > 0);
}

static BOOL LoadBearStats(const char *filename) {
    BPTR file = Open((STRPTR)filename, MODE_OLDFILE);
    if (!file) return FALSE;
    
    char buffer[512];
    LONG bytesRead = Read(file, buffer, sizeof(buffer) - 1);
    Close(file);
    
    if (bytesRead <= 0) return FALSE;
    buffer[bytesRead] = 0;
    
    /* Simple parsing */
    char *line = strstr(buffer, "BEAR_SAVE_V1\n");
    if (!line) return FALSE;
    
    line += 13;  /* Skip header */
    
    // Safer Load System
    unsigned long lf, lp, lc, ls;
    unsigned int h, ha, he, en, cl, age, wt, sl, si, po;

    int parsed = sscanf(line,
    "%u %u %u %u %u %u %u %lu %lu %lu %lu %u %u %u",
    &h,&ha,&he,&en,&cl,&age,&wt,&lf,&lp,&lc,&ls,&sl,&si,&po);

    if (parsed != 14) return FALSE;

    bearStats.hunger=h; bearStats.happiness=ha; bearStats.health=he;
    bearStats.energy=en; bearStats.cleanliness=cl; bearStats.age=age;
    bearStats.weight=wt; bearStats.lastFeed=lf; bearStats.lastPlay=lp;
    bearStats.lastClean=lc; bearStats.lastSleep=ls;
    bearStats.sleeping=sl?TRUE:FALSE; bearStats.sick=si?TRUE:FALSE; bearStats.pooped=po?TRUE:FALSE;
    
    printf("Loaded bear stats from %s\n", filename);
    return TRUE;
}

/* Show detailed stats in a requester */
static void ShowStatsRequester(void) {
    static char statsBuf[512];
    BearMood mood = GetBearMood();
    const char *moodStr[] = {
        "Happy", "Content", "Neutral", "Sad", "Angry", "Sick", "Sleeping", "Dead"
    };
    
    snprintf(statsBuf, sizeof(statsBuf),
             "Bear Statistics:\n\n"
             "Mood: %s\n"
             "Hunger: %d/100\n"
             "Happiness: %d/100\n"
             "Health: %d/100\n"
             "Energy: %d/100\n"
             "Cleanliness: %d/100\n"
             "Age: %d hours old\n"
             "Weight: %d kg\n\n"
             "Status: %s%s%s",
             moodStr[mood], bearStats.hunger, bearStats.happiness, bearStats.health,
             bearStats.energy, bearStats.cleanliness, bearStats.age, bearStats.weight,
             bearStats.sleeping ? "Sleeping " : "",
             bearStats.sick ? "Sick " : "",
             bearStats.pooped ? "Messy" : "Clean");
    
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, "Bear Statistics", statsBuf, "OK"
    };
    EasyRequestArgs(win, &es, NULL, NULL);
}

/* ------------ BOB System Functions (unchanged) ------------ */
/* [Previous BOB system code remains the same] */

/* Turn a BitMap (from DataTypes) into a classic struct Image */
static struct Image *CreateImageFromBitMap(struct BitMap *bm, UWORD w, UWORD h)
{
    if (!bm || !w || !h || !bm->Planes[0]) return NULL;  /* Added null plane check */
    
    UWORD wordsPerLine = (w + 15) >> 4;
    UBYTE depth = bm->Depth;
    
    /* Validate depth */
    if (depth == 0 || depth > 8) return NULL;

    ULONG totalWords = (ULONG)wordsPerLine * h * depth;
    WORD *raw = AllocVec(totalWords * sizeof(WORD), MEMF_CHIP | MEMF_CLEAR);
    if (!raw) return NULL;

    WORD *dst = raw;
    for (int p = 0; p < depth; p++) {
        /* Check if plane exists */
        if (!bm->Planes[p]) {
            FreeVec(raw);
            return NULL;
        }
        
        for (int y = 0; y < h; y++) {
            UBYTE *src = (UBYTE*)bm->Planes[p] + y * bm->BytesPerRow;
            for (int widx = 0; widx < wordsPerLine; widx++) {
                UWORD v = 0;
                if (widx * 2 < bm->BytesPerRow) {
                    v = ((UWORD)src[widx*2] << 8);
                    if (widx*2 + 1 < bm->BytesPerRow) {
                        v |= src[widx*2 + 1];
                    }
                }
                *dst++ = v;
            }
        }
    }

    struct Image *img = AllocVec(sizeof(struct Image), MEMF_ANY | MEMF_CLEAR);
    if (!img) { 
        FreeVec(raw); 
        return NULL; 
    }

    img->LeftEdge   = 0;
    img->TopEdge    = 0;
    img->Width      = w;
    img->Height     = h;
    img->Depth      = depth;
    img->ImageData  = (UWORD*)raw;
    img->PlanePick  = (1 << depth) - 1;
    img->PlaneOnOff = 0;
    img->NextImage  = NULL;
    
    return img;
}

/* Load an ILBM as DT picture, return classic struct Image* */
static struct Image *LoadButtonIcon(const char *path, Object **iconObj, struct BitMap **iconBM)
{
    struct BitMapHeader *bmhd = NULL;
    *iconObj = NewDTObject((STRPTR)path,
                           DTA_SourceType, DTST_FILE,
                           DTA_GroupID,   GID_PICTURE,
                           PDTA_Screen,   (ULONG)win->WScreen,
                           TAG_END);
    if (!*iconObj) return NULL;

    DoDTMethod(*iconObj, 0L, 0L, DTM_PROCLAYOUT, 0L, 1L);
    GetDTAttrs(*iconObj,
               PDTA_BitMapHeader, (ULONG)&bmhd,
               PDTA_BitMap,       (ULONG)iconBM,
               TAG_END);
    if (!bmhd || !*iconBM) { DisposeDTObject(*iconObj); *iconObj=NULL; return NULL; }

    return CreateImageFromBitMap(*iconBM, bmhd->bmh_Width, bmhd->bmh_Height);
}

/* Build all five images */
static void LoadButtonIcons(void) {
    printf("Loading button icons...\n");
    
    playImage     = LoadButtonIcon(ICON_PLAY,     &playIcon,     &playBM);
    if (playImage) printf("Loaded play icon\n"); else printf("Failed to load play icon\n");
    
    cleanImage    = LoadButtonIcon(ICON_CLEAN,    &cleanIcon,    &cleanBM);
    if (cleanImage) printf("Loaded clean icon\n"); else printf("Failed to load clean icon\n");
    
    sleepImage    = LoadButtonIcon(ICON_SLEEP,    &sleepIcon,    &sleepBM);
    if (sleepImage) printf("Loaded sleep icon\n"); else printf("Failed to load sleep icon\n");
    
    medicineImage = LoadButtonIcon(ICON_MEDICINE, &medicineIcon, &medicineBM);
    if (medicineImage) printf("Loaded medicine icon\n"); else printf("Failed to load medicine icon\n");
    
    statsImage    = LoadButtonIcon(ICON_STATS,    &statsIcon,    &statsBM);
    if (statsImage) printf("Loaded stats icon\n"); else printf("Failed to load stats icon\n");
}

static void EnsureFunctionalButtons(void) {
    /* If image replacement failed, make sure we still have working buttons */
    if (!playBtn) playBtn = originalPlayBtn;
    if (!cleanBtn) cleanBtn = originalCleanBtn;
    if (!sleepBtn) sleepBtn = originalSleepBtn;
    if (!medicineBtn) medicineBtn = originalMedicineBtn;
    if (!statsBtn) statsBtn = originalStatsBtn;
    
    /* Re-add any missing buttons to the window */
    if (playBtn && !playBtn->NextGadget && playBtn != gadList) {
        AddGList(win, playBtn, (UWORD)-1, 1, NULL);
    }
    /* Repeat for other buttons as needed */
}

/* Properly free the classic images + DT objects */
static void FreeButtonIcons(void)
{
    struct Image *imgs[] = { playImage, cleanImage, sleepImage, medicineImage, statsImage };
    for (int i=0;i<5;i++){
        if (imgs[i]) {
            if (imgs[i]->ImageData) FreeVec(imgs[i]->ImageData);
            FreeVec(imgs[i]);
        }
    }
    playImage = cleanImage = sleepImage = medicineImage = statsImage = NULL;

    if (playIcon)     { DisposeDTObject(playIcon);     playIcon=NULL; }
    if (cleanIcon)    { DisposeDTObject(cleanIcon);    cleanIcon=NULL; }
    if (sleepIcon)    { DisposeDTObject(sleepIcon);    sleepIcon=NULL; }
    if (medicineIcon) { DisposeDTObject(medicineIcon); medicineIcon=NULL; }
    if (statsIcon)    { DisposeDTObject(statsIcon);    statsIcon=NULL; }
}

/* Create a proper Amiga BOB bitmap from sprite data */
static struct BitMap *CreateBobBitMap(UWORD frame) {
    if (!sprBM || !sprBMHD) return NULL;

    UWORD col = frame % SPR_COLS;
    UWORD row = frame / SPR_COLS;
    WORD sx = SPR_ORIGIN_X + (WORD)(col * SPR_W);
    WORD sy = SPR_ORIGIN_Y + (WORD)(row * SPR_H);

    UWORD width  = doubleSize ? (SPR_W * 2) : SPR_W;
    UWORD height = doubleSize ? (SPR_H * 2) : SPR_H;

    struct BitMap *bobBM = AllocBitMap(width, height, BEAR_DEPTH, BMF_CLEAR, NULL);
    if (!bobBM) return NULL;

    /* dst RP for writing into the new BOB bitmap */
    struct RastPort dstRP;
    InitRastPort(&dstRP);
    dstRP.BitMap = bobBM;

    /* src RP bound to the sprite sheet so ReadPixel works */
    struct RastPort srcRP;
    InitRastPort(&srcRP);
    srcRP.BitMap = sprBM;

    if (doubleSize) {
        /* 2× scale by sampling source pixels */
        for (WORD y = 0; y < SPR_H; y++) {
            for (WORD x = 0; x < SPR_W; x++) {
                UBYTE c = (UBYTE)ReadPixel(&srcRP, sx + x, sy + y);  /* returns ULONG; cast to UBYTE */
                SetAPen(&dstRP, c & ((1u << BEAR_DEPTH) - 1));
                WORD dx = x * 2, dy = y * 2;
                RectFill(&dstRP, dx, dy, dx + 1, dy + 1);
            }
        }
    } else {
        /* 1× direct blit */
        BltBitMapRastPort(sprBM, sx, sy, &dstRP, 0, 0, SPR_W, SPR_H, 0xC0);
    }

    return bobBM;
}

/* Create RAW image data with NO headers (as per Amiga docs) */
static WORD *CreateBobImageData(struct BitMap *bm, UWORD width, UWORD height) {
    if (!bm || width == 0 || height == 0) return NULL;
    
    UWORD wordsPerLine = (width + 15) >> 4;
    UBYTE depth = bm->Depth;

    /* Ensure word alignment for larger BOBs */
    if (wordsPerLine > 3) {  /* For double-size or larger */
        wordsPerLine = (wordsPerLine + 1) & ~1;  /* Force even word count */
    }

      printf("BOB size: %dx%d pixels = %d words per line\n", width, height, wordsPerLine);


    /* Allocate CHIP memory for RAW planar data (NO HEADERS) */
    ULONG totalWords = (ULONG)wordsPerLine * height * depth;
    WORD *img = AllocVec(totalWords * sizeof(WORD), MEMF_CHIP | MEMF_CLEAR | MEMF_REVERSE);
    if (!img) return NULL;
    
    /* Copy data plane by plane, line by line - RAW format */
    WORD *dst = img;
    for (int p = 0; p < depth; p++) {
        for (int y = 0; y < height; y++) {
            UBYTE *srcLine = (UBYTE*)bm->Planes[p] + y * bm->BytesPerRow;
            
            for (int w = 0; w < wordsPerLine; w++) {
                UWORD v = 0;
                if (w * 2 < bm->BytesPerRow) {
                    v = ((UWORD)srcLine[w * 2] << 8);
                    if (w * 2 + 1 < bm->BytesPerRow) {
                        v |= srcLine[w * 2 + 1];
                    }
                }
                *dst++ = v;  /* Store directly, no header */
            }
        }
    }
    return img;
}

/* Create proper single-plane transparency mask for Amiga BOBs */
static WORD *CreateBobMask(struct BitMap *bm, UWORD width, UWORD height) {
    if (!bm || width == 0 || height == 0) return NULL;
    
    UWORD wordsPerLine = (width + 15) >> 4;
    
    /* Create a single-plane bitmap for the mask */
    struct BitMap *maskBM = AllocBitMap(width, height, 1, BMF_CLEAR, NULL);
    if (!maskBM) return NULL;
    
    /* Set up RastPort for the mask bitmap */
    struct RastPort maskRP;
    InitRastPort(&maskRP);
    maskRP.BitMap = maskBM;
    
    /* Set up RastPort for reading from source */
    struct RastPort srcRP;
    InitRastPort(&srcRP);
    srcRP.BitMap = bm;
    
    /* CRITICAL FIX: Get transparent color from pixel (1,1) instead of assuming color 0 */
    UBYTE transparentColor = (UBYTE)ReadPixel(&srcRP, 1, 1);
    
    printf("Using transparent color: %d\n", transparentColor);  /* Debug info */
    
    /* Build mask by checking each pixel against the transparent color */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            /* Read pixel color from source */
            UBYTE colorIdx = (UBYTE)ReadPixel(&srcRP, x, y);
            
            /* Set mask pixel: 1 for non-transparent (colorIdx != transparentColor) */
            if (colorIdx != transparentColor) {
                SetAPen(&maskRP, 1);
                WritePixel(&maskRP, x, y);
            }
            /* If colorIdx == transparentColor, leave mask pixel as 0 (transparent) */
        }
    }
    
    /* Now extract the raw mask data from the single plane */
    ULONG totalWords = (ULONG)wordsPerLine * height;
    WORD *mask = AllocVec(totalWords * sizeof(WORD), MEMF_CHIP | MEMF_CLEAR);
    if (!mask) {
        FreeBitMap(maskBM);
        return NULL;
    }
    
    /* Copy the mask plane data */
    UBYTE *srcPlane = (UBYTE*)maskBM->Planes[0];
    WORD *dst = mask;
    
    for (int y = 0; y < height; y++) {
        UBYTE *srcLine = srcPlane + y * maskBM->BytesPerRow;
        for (int w = 0; w < wordsPerLine; w++) {
            UWORD maskWord = 0;
            if (w * 2 < maskBM->BytesPerRow) {
                maskWord = ((UWORD)srcLine[w * 2] << 8);
                if (w * 2 + 1 < maskBM->BytesPerRow) {
                    maskWord |= srcLine[w * 2 + 1];
                }
            }
            *dst++ = maskWord;
        }
    }
    
    FreeBitMap(maskBM);
    return mask;
}

/* Initialize the BOB system */
static BOOL InitBobSystem(void) {
    if (!win || !rp) return FALSE;
    
    gelsInfo = (struct GelsInfo *)AllocVec(sizeof(struct GelsInfo), MEMF_CLEAR);
    if (!gelsInfo) return FALSE;
    
    /* CRITICAL: Set up proper collision area */
    gelsInfo->leftmost = 0;
    gelsInfo->rightmost = WIN_W - 1;
    gelsInfo->topmost = UI_BAR_H;
    gelsInfo->bottommost = WIN_H - 1;
    
    gelsInfo->collHandler = NULL;  /* No custom collision handler */
    
    /* Create and link VSprites */
    headVSprite = (struct VSprite *)AllocVec(sizeof(struct VSprite), MEMF_CLEAR);
    tailVSprite = (struct VSprite *)AllocVec(sizeof(struct VSprite), MEMF_CLEAR);
    if (!headVSprite || !tailVSprite) return FALSE;
    
    headVSprite->Flags = VSPRITE;
    tailVSprite->Flags = VSPRITE;
    
    /* Install BEFORE creating any BOBs */
    rp->GelsInfo = gelsInfo;
    InitGels(headVSprite, tailVSprite, gelsInfo);
    
    bobSystemReady = TRUE;
    return TRUE;
}

/* Simple fallback sprite drawing function */
static void DrawSpriteDirectly(UWORD frame, WORD x, WORD y) {
    if (!sprBM || !sprBMHD) return;
    BlitSpriteFrameWithColorKey(frame, x, y);
}

/* Create BOB data for all sprite frames */
static BOOL CreateBobData(void) {
    if (!bobSystemReady || !sprBM) return FALSE;
    
    for (int frame = 0; frame < (SPR_COLS * SPR_ROWS); frame++) {
        bobBitMaps[frame] = CreateBobBitMap(frame);
        if (!bobBitMaps[frame]) continue;
        
        UWORD width = doubleSize ? (SPR_W * 2) : SPR_W;
        UWORD height = doubleSize ? (SPR_H * 2) : SPR_H;
        
        /* Create RAW image data and proper transparency mask */
        bobImageData[frame] = CreateBobImageData(bobBitMaps[frame], width, height);
        bobMaskData[frame] = CreateBobMask(bobBitMaps[frame], width, height);
        
        if (!bobImageData[frame] || !bobMaskData[frame]) {
            if (bobImageData[frame]) FreeVec(bobImageData[frame]);
            if (bobMaskData[frame]) FreeVec(bobMaskData[frame]);
            if (bobBitMaps[frame]) FreeBitMap(bobBitMaps[frame]);
            bobImageData[frame] = NULL;
            bobMaskData[frame] = NULL;
            bobBitMaps[frame] = NULL;
        }
    }
    return TRUE;
}

/* Fixed BOB creation with proper error checking */
static BOOL CreateBearBob(void) {
    if (!bobSystemReady || !sprBM || !rp) return FALSE;

    UWORD wpx = doubleSize ? (SPR_W * 2) : SPR_W;
    UWORD hpx = doubleSize ? (SPR_H * 2) : SPR_H;
    UWORD wwords = (wpx + 15) >> 4;
    UBYTE bobDepth = sprBM->Depth;

    /* Allocate structures */
    bearBob = (struct Bob *)AllocVec(sizeof(struct Bob), MEMF_CLEAR);
    bearVS = (struct VSprite *)AllocVec(sizeof(struct VSprite), MEMF_CLEAR);
    if (!bearBob || !bearVS) return FALSE;

    /* Validate we have data */
    if (!bobImageData[S_FRONT_C] || !bobMaskData[S_FRONT_C]) {
        FreeVec(bearVS);
        FreeVec(bearBob);
        bearVS = NULL;
        bearBob = NULL;
        return FALSE;
    }

    /* === VSPRITE SETUP === */
    bearVS->Height = hpx;
    bearVS->Width = wwords;
    bearVS->Depth = bobDepth;
    bearVS->PlanePick = (1 << bobDepth) - 1;  /* All planes */
    bearVS->ImageData = bobImageData[S_FRONT_C];
    bearVS->X = bearX;
    bearVS->Y = bearY;
    bearVS->CollMask = NULL;  /* No collision detection */
    bearVS->BorderLine = NULL;
    bearVS->SprColors = NULL;
    bearVS->PlaneOnOff = 0;
    bearVS->Flags = SAVEBACK | OVERLAY;

    /* === BOB SETUP === */
    bearBob->BobVSprite = bearVS;
    bearBob->ImageShadow = bobMaskData[S_FRONT_C];
    bearBob->SaveBuffer = bearSaveBuffer;  /* Static buffer */
    bearBob->Flags = SAVEBACK | OVERLAY;
    
    /* Bidirectional linking */
    bearVS->VSBob = bearBob;

    /* Clear other pointers */
    bearBob->Before = NULL;
    bearBob->After = NULL;
    bearBob->BobComp = NULL;
    bearBob->DBuffer = NULL;

    /* Add BOB to system */
    AddBob(bearBob, rp);
    SortGList(rp);

    return TRUE;
}

static void SetBobFrame(UWORD frame) {
    if (!bearVS || !bearBob || frame >= (SPR_COLS * SPR_ROWS)) return;
    
    if (bobImageData[frame] && bobMaskData[frame]) {
        bearVS->ImageData = bobImageData[frame];
        bearBob->ImageShadow = bobMaskData[frame];
    }
}

static void UpdateBobPosition(WORD x, WORD y) {
    if (!bearVS) return;
    bearVS->X = x;
    bearVS->Y = y;
}

static void CleanupBobData(void) {
    for (int i = 0; i < SPR_COLS * SPR_ROWS; i++) {
        if (bobImageData[i]) { 
            FreeVec(bobImageData[i]); 
            bobImageData[i] = NULL; 
        }
        if (bobMaskData[i]) { 
            FreeVec(bobMaskData[i]); 
            bobMaskData[i] = NULL; 
        }
        if (bobBitMaps[i]) { 
            FreeBitMap(bobBitMaps[i]); 
            bobBitMaps[i] = NULL; 
        }
    }
}

static void CloseBobSystem(void) {
    if (bearBob) {
        RemBob(bearBob);
        FreeVec(bearBob);
        bearBob = NULL;
    }
    if (bearVS) { 
        FreeVec(bearVS); 
        bearVS = NULL; 
    }

    CleanupBobData();

    if (rp) rp->GelsInfo = NULL;
    if (headVSprite) { FreeVec(headVSprite); headVSprite = NULL; }
    if (tailVSprite) { FreeVec(tailVSprite); tailVSprite = NULL; }
    if (gelsInfo) { FreeVec(gelsInfo); gelsInfo = NULL; }

    bobSystemReady = FALSE;
}

static void DrawBobs(void) {
    if (!bobSystemReady || !rp || !bearBob || !bearVS) return;
    DrawGList(rp, &win->WScreen->ViewPort);
    WaitTOF();
}

/* ------------ Utils ------------ */

/* Load a single IFF 8SVX sound file */
static BOOL LoadSound(const char *filename, UWORD index) {
    if (index >= MAX_SOUNDS) return FALSE;
    
    BPTR file = Open((STRPTR)filename, MODE_OLDFILE);
    if (!file) return FALSE;
    
    /* Get file size first */
    Seek(file, 0, OFFSET_END);
    LONG fileSize = Seek(file, 0, OFFSET_BEGINNING);
    if (fileSize <= 12 || fileSize > MAX_SOUND_SIZE) {
        Close(file);
        return FALSE;
    }
    
    /* Allocate buffer for entire file */
    UBYTE *fileData = (UBYTE*)AllocVec(fileSize, MEMF_ANY);
    if (!fileData) {
        Close(file);
        return FALSE;
    }
    
    /* Read entire file */
    LONG bytesRead = Read(file, fileData, fileSize);
    Close(file);
    
    if (bytesRead != fileSize) {
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Safely check FORM header */
    if (fileSize < 12 ||
        fileData[0] != 'F' || fileData[1] != 'O' || 
        fileData[2] != 'R' || fileData[3] != 'M') {
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Check 8SVX type */
    if (fileData[8] != '8' || fileData[9] != 'S' || 
        fileData[10] != 'V' || fileData[11] != 'X') {
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Parse chunks safely - force 8287 Hz */
    ULONG pos = 12;  /* Skip FORM header */
    ULONG sampleRate = 8287;  /* Force PAL middle C */
    UBYTE *sampleData = NULL;
    ULONG sampleLength = 0;
    
    while (pos + 8 <= fileSize) {
        /* Read chunk ID and size safely */
        ULONG chunkID = ((ULONG)fileData[pos] << 24) | 
                        ((ULONG)fileData[pos+1] << 16) | 
                        ((ULONG)fileData[pos+2] << 8) | 
                        (ULONG)fileData[pos+3];
        
        ULONG chunkSize = ((ULONG)fileData[pos+4] << 24) | 
                          ((ULONG)fileData[pos+5] << 16) |
                          ((ULONG)fileData[pos+6] << 8) | 
                          (ULONG)fileData[pos+7];
        
        /* Bounds check for chunk data */
        if (pos + 8 + chunkSize > fileSize) break;
        
        if (chunkID == 0x424F4459) {  /* 'BODY' - Sample Data */
            if (chunkSize > 0 && chunkSize <= MAX_SOUND_SIZE) {
                sampleData = &fileData[pos + 8];
                sampleLength = chunkSize;
                break;  /* Found what we need */
            }
        }
        
        /* Move to next chunk (with proper alignment) */
        pos += 8 + chunkSize;
        if (chunkSize & 1) pos++;  /* IFF chunks are word-aligned */
    }
    
    if (!sampleData || sampleLength == 0) {
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Allocate CHIP memory for sample */
    bearSounds[index].data = (UBYTE*)AllocVec(sampleLength, MEMF_CHIP);
    if (!bearSounds[index].data) {
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Copy sample data */
    CopyMem(sampleData, bearSounds[index].data, sampleLength);
    bearSounds[index].length = sampleLength;
    bearSounds[index].period = 3579545 / sampleRate;
    
    /* Store filename */
    strncpy(bearSounds[index].name, filename, 31);
    bearSounds[index].name[31] = 0;
    
    FreeVec(fileData);
    return TRUE;
}

/* Initialize audio system and load all sounds */
static BOOL InitBearSounds(void) {
    /* Initialize sound array */
    for (int i = 0; i < MAX_SOUNDS; i++) {
        bearSounds[i].data = NULL;
        bearSounds[i].length = 0;
        bearSounds[i].period = 447;
        bearSounds[i].name[0] = 0;
    }
    numSounds = 0;
    audioPlaying = FALSE;
    
    /* Try to open audio device */
    audioPort = CreateMsgPort();
    if (!audioPort) return FALSE;
    
    audioReq = (struct IOAudio*)CreateIORequest(audioPort, sizeof(struct IOAudio));
    if (!audioReq) {
        DeleteMsgPort(audioPort);
        audioPort = NULL;
        return FALSE;
    }
    
    audioReq->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audioReq->ioa_Data = audioChannels;
    audioReq->ioa_Length = sizeof(audioChannels);
    
    if (OpenDevice("audio.device", 0, (struct IORequest*)audioReq, 0)) {
        DeleteIORequest((struct IORequest*)audioReq);
        DeleteMsgPort(audioPort);
        audioReq = NULL;
        audioPort = NULL;
        return FALSE;
    }
    
    /* Load sound files one by one */
    for (int i = 0; soundFiles[i] && numSounds < MAX_SOUNDS; i++) {
        if (LoadSound(soundFiles[i], numSounds)) {
            numSounds++;
        }
    }
    
    return TRUE;
}

/* Clean up audio resources */
static void CloseBearSounds(void) {
    /* Stop any playing sound */
    if (audioReq && audioPlaying) {
        if (!CheckIO((struct IORequest*)audioReq)) {
            AbortIO((struct IORequest*)audioReq);
        }
        WaitIO((struct IORequest*)audioReq);
        audioPlaying = FALSE;
    }
    
    /* Free all sound data */
    for (int i = 0; i < numSounds; i++) {
        if (bearSounds[i].data) {
            FreeVec(bearSounds[i].data);
            bearSounds[i].data = NULL;
        }
    }
    numSounds = 0;
    
    /* Close audio device */
    if (audioReq) {
        CloseDevice((struct IORequest*)audioReq);
        DeleteIORequest((struct IORequest*)audioReq);
        audioReq = NULL;
    }
    
    if (audioPort) {
        DeleteMsgPort(audioPort);
        audioPort = NULL;
    }
}

/* Play a random sound of a specific type */
static void PlayBearSound(SoundType type) {
    if (!audioReq || numSounds == 0) return;
    
    /* Stop any currently playing sound */
    if (audioPlaying) {
        if (!CheckIO((struct IORequest*)audioReq)) {
            AbortIO((struct IORequest*)audioReq);
        }
        WaitIO((struct IORequest*)audioReq);
        audioPlaying = FALSE;
    }
    
    /* Find sounds of the requested type */
    UWORD candidates[MAX_SOUNDS];
    UWORD numCandidates = 0;
    
    for (int i = 0; soundMap[i].soundIndex < numSounds; i++) {
        if (soundMap[i].type == type || type == SOUND_RANDOM) {
            if (soundMap[i].soundIndex < numSounds) {
                candidates[numCandidates++] = soundMap[i].soundIndex;
            }
        }
    }
    
    if (numCandidates == 0) return;
    
    /* Pick a random sound */
    UWORD chosen = candidates[rand() % numCandidates];
    struct BearSound *sound = &bearSounds[chosen];
    
    /* Set up audio request */
    audioReq->ioa_Request.io_Command = CMD_WRITE;
    audioReq->ioa_Request.io_Flags = ADIOF_PERVOL;
    audioReq->ioa_Data = sound->data;
    audioReq->ioa_Length = sound->length;
    audioReq->ioa_Period = sound->period;
    audioReq->ioa_Volume = 64;  /* Full volume */
    audioReq->ioa_Cycles = 1;   /* Play once */
    
    /* Start playback */
    BeginIO((struct IORequest*)audioReq);
    audioPlaying = TRUE;
}

/* Random chatter with more variety */
static void RandomBearChatter(void) {
    static UWORD chatterCounter = 0;
    chatterCounter++;
    
    /* Different behaviors based on bear's mood and activity */
    if ((chatterCounter & 127) == 0) {  /* Every ~10 seconds at 12.5fps */
        /* Occasional random mumbling */
        if ((rand() % 5) == 0) {  /* 20% chance */
            SoundType chatterType;
            switch (rand() % 4) {
                case 0: chatterType = SOUND_GROWL; break;
                case 1: chatterType = SOUND_HAPPY; break; 
                case 2: chatterType = SOUND_GREETING; break;
                default: chatterType = SOUND_RANDOM; break;
            }
            PlayBearSound(chatterType);
        }
    }
    
    /* Respond to being ignored for too long */
    if ((chatterCounter & 511) == 0 && !speaking) {  /* Every ~40 seconds */
        if ((rand() % 3) == 0) {  /* 33% chance */
            PlayBearSound(SOUND_WAKEUP);  /* "Hey, pay attention to me!" */
        }
    }
}

/* Smart sound responses based on what the user types */
static void RespondToUserText(const char *text) {
    if (!text || !*text) return;
    
    /* Convert to lowercase for easier matching */
    char lowerText[256];
    strncpy(lowerText, text, 255);
    lowerText[255] = 0;
    for (int i = 0; lowerText[i]; i++) {
        if (lowerText[i] >= 'A' && lowerText[i] <= 'Z') {
            lowerText[i] += ('a' - 'A');
        }
    }
    
    /* Look for keywords and respond appropriately */
    if (strstr(lowerText, "yes") || strstr(lowerText, "good") || strstr(lowerText, "nice")) {
        PlayBearSound(SOUND_AGREEMENT);
    } else if (strstr(lowerText, "no") || strstr(lowerText, "bad") || strstr(lowerText, "stop")) {
        PlayBearSound(SOUND_DISAGREE);
    } else if (strstr(lowerText, "hello") || strstr(lowerText, "hi") || strstr(lowerText, "hey")) {
        PlayBearSound(SOUND_GREETING);
    } else if (strstr(lowerText, "happy") || strstr(lowerText, "love") || strstr(lowerText, "cute")) {
        PlayBearSound(SOUND_HAPPY);
    } else if (strstr(lowerText, "food") || strstr(lowerText, "hungry") || strstr(lowerText, "eat")) {
        PlayBearSound(SOUND_NOM);
    } else {
        /* Default response - random greeting or happy sound */
        PlayBearSound((rand() & 1) ? SOUND_GREETING : SOUND_HAPPY);
    }
}

/* ------------ Loaders ------------ */
static BOOL LoadBG(const char *path){
    bgObj=NewDTObject((STRPTR)path, DTA_SourceType,DTST_FILE, DTA_GroupID,GID_PICTURE,
                      PDTA_Screen,(ULONG)win->WScreen, TAG_END);
    if(!bgObj) return FALSE;
    DoDTMethod(bgObj,0L,0L,DTM_PROCLAYOUT,0L,1L);
    GetDTAttrs(bgObj, PDTA_BitMapHeader,(ULONG)&bgBMHD, PDTA_BitMap,(ULONG)&bgBM,
               PDTA_MaskPlane,(ULONG)&bgMask, TAG_END);
    if(!bgBMHD||!bgBM){ DisposeDTObject(bgObj); bgObj=NULL; return FALSE; }
    bgW=bgBMHD->bmh_Width; bgH=bgBMHD->bmh_Height;
    bgX=(WIN_W-bgW)/2; bgY=UI_BAR_H+((WIN_H-UI_BAR_H)-bgH)/2;
    if(bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
    else       BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
    return TRUE;
}

static void FreeBG(void){
    if(bgObj){ DisposeDTObject(bgObj); bgObj=NULL; }
    bgBMHD=NULL; bgBM=NULL; bgMask=NULL;
}

static BOOL LoadSpriteSheet(const char *path){
    sprObj=NewDTObject((STRPTR)path, 
                       DTA_SourceType, DTST_FILE, 
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Screen, (ULONG)win->WScreen,
                       TAG_END);
    if(!sprObj) return FALSE;
    
    DoDTMethod(sprObj,0L,0L,DTM_PROCLAYOUT,0L,1L);
    GetDTAttrs(sprObj,
        PDTA_BitMapHeader,(ULONG)&sprBMHD,
        PDTA_BitMap,      (ULONG)&sprBM,
        TAG_END);
    if(!sprBMHD||!sprBM){ DisposeDTObject(sprObj); sprObj=NULL; return FALSE; }
    
    return TRUE;
}

/* Load poop icon */
static BOOL LoadPoopIcon(void) {
    poopIcon = NewDTObject((STRPTR)ICON_POOP, DTA_SourceType, DTST_FILE, 
                          DTA_GroupID, GID_PICTURE, PDTA_Screen, (ULONG)win->WScreen, TAG_END);
    if (!poopIcon) return FALSE;
    
    DoDTMethod(poopIcon, 0L, 0L, DTM_PROCLAYOUT, 0L, 1L);
    GetDTAttrs(poopIcon, PDTA_BitMapHeader, (ULONG)&poopBMHD, 
               PDTA_BitMap, (ULONG)&poopBM, TAG_END);
    
    if (!poopBMHD || !poopBM) {
        DisposeDTObject(poopIcon);
        poopIcon = NULL;
        return FALSE;
    }
    return TRUE;
}

/* Use a pixel-by-pixel approach that checks for a specific "transparent" color */
static void BlitSpriteFrameWithColorKey(UWORD frame, WORD dx, WORD dy){
    if(!sprBM||!sprBMHD) return;
    
    UWORD col = frame % SPR_COLS;
    UWORD row = frame / SPR_COLS;
    WORD sx = SPR_ORIGIN_X + (WORD)(col * SPR_W);
    WORD sy = SPR_ORIGIN_Y + (WORD)(row * SPR_H);
    
    /* Cache these values to avoid repeated lookups */
    UBYTE depth = sprBM->Depth;
    ULONG bytesPerRow = sprBM->BytesPerRow;
    UBYTE **planes = (UBYTE**)sprBM->Planes;
    
    /* UPDATED: Get transparent color from pixel (1,1) of the current sprite frame */
    struct RastPort srcRP;
    InitRastPort(&srcRP);
    srcRP.BitMap = sprBM;
    UBYTE bgColor = (UBYTE)ReadPixel(&srcRP, sx + 1, sy + 1);
    
    if (doubleSize) {
        /* 2x scaled blitting - each source pixel becomes a 2x2 block */
        for(WORD y = 0; y < SPR_H; y++) {
            WORD srcY = sy + y;
            if(srcY < 0 || srcY >= sprBMHD->bmh_Height) continue;
            
            for(WORD x = 0; x < SPR_W; x++) {
                WORD srcX = sx + x;
                if(srcX < 0 || srcX >= sprBMHD->bmh_Width) continue;
                
                /* Read pixel color */
                UBYTE byteOffset = srcX >> 3;
                UBYTE bitMask = 0x80 >> (srcX & 7);
                UBYTE colorIdx = 0;
                
                for(int p = 0; p < depth; p++){
                    UBYTE *planeByte = planes[p] + bytesPerRow * srcY + byteOffset;
                    if((*planeByte) & bitMask) {
                        colorIdx |= (1U << p);
                    }
                }
                
                /* Draw 2x2 pixel block if not transparent */
                if(colorIdx != bgColor) {
                    SetAPen(rp, colorIdx);
                    WORD destX = dx + (x * 2);
                    WORD destY = dy + (y * 2);
                    
                    /* Draw 2x2 block */
                    WritePixel(rp, destX,     destY);
                    WritePixel(rp, destX + 1, destY);
                    WritePixel(rp, destX,     destY + 1);
                    WritePixel(rp, destX + 1, destY + 1);
                }
            }
        }
    } else {
        /* Normal 1x1 blitting */
        for(WORD y = 0; y < SPR_H; y++) {
            WORD srcY = sy + y;
            WORD destY = dy + y;
            
            if(srcY < 0 || srcY >= sprBMHD->bmh_Height) continue;
            
            for(WORD x = 0; x < SPR_W; x++) {
                WORD srcX = sx + x;
                WORD destX = dx + x;
                
                if(srcX < 0 || srcX >= sprBMHD->bmh_Width) continue;
                
                UBYTE byteOffset = srcX >> 3;
                UBYTE bitMask = 0x80 >> (srcX & 7);
                UBYTE colorIdx = 0;
                
                for(int p = 0; p < depth; p++){
                    UBYTE *planeByte = planes[p] + bytesPerRow * srcY + byteOffset;
                    if((*planeByte) & bitMask) {
                        colorIdx |= (1U << p);
                    }
                }
                
                if(colorIdx != bgColor) {
                    SetAPen(rp, colorIdx);
                    WritePixel(rp, destX, destY);
                }
            }
        }
    }
}

static void FreeSpriteSheet(void){
    if (sprObj){ DisposeDTObject(sprObj); sprObj=NULL; }
    sprBMHD=NULL; sprBM=NULL;
}

/* ------------ Timer ------------ */
static BOOL ArmTimerMS(ULONG ms){
    if(!tmReq) return FALSE;
    tmReq->tr_node.io_Command=TR_ADDREQUEST;
    tmReq->tr_time.tv_secs=ms/1000;
    tmReq->tr_time.tv_micro=(ms%1000)*1000;
    SendIO((struct IORequest*)tmReq);
    return TRUE;
}

static BOOL InitTimer(void){
    tmPort=CreateMsgPort(); if(!tmPort) return FALSE;
    tmReq=(struct timerequest*)CreateIORequest(tmPort,sizeof(struct timerequest)); if(!tmReq) return FALSE;
    if(OpenDevice("timer.device",UNIT_MICROHZ,(struct IORequest*)tmReq,0)) return FALSE;
    return ArmTimerMS(TICK_MS);
}

static void CloseTimer(void){
    if(tmReq){
        if(!CheckIO((struct IORequest*)tmReq)) AbortIO((struct IORequest*)tmReq);
        WaitIO((struct IORequest*)tmReq);
        CloseDevice((struct IORequest*)tmReq);
        DeleteIORequest((struct IORequest*)tmReq); tmReq=NULL;
    }
    if(tmPort){ DeleteMsgPort(tmPort); tmPort=NULL; }
}

/* ------------ Speech (safe stop) ------------ */
static void StopSpeech(void){
    if(!voiceIO) return;
    if(speaking){
        if(!CheckIO((struct IORequest*)voiceIO)) AbortIO((struct IORequest*)voiceIO);
        WaitIO((struct IORequest*)voiceIO);
        speaking=FALSE;
    }
    if(voiceIO->message.io_Data){
        FreeVec((APTR)voiceIO->message.io_Data);
        voiceIO->message.io_Data=NULL;
    }
}

static BOOL InitSpeech(void){
    if(!(TranslatorBase=OpenLibrary("translator.library",0))) return FALSE;
    narrPort=CreateMsgPort(); if(!narrPort) return FALSE;
    voiceIO=(struct narrator_rb*)CreateIORequest(narrPort,sizeof(struct narrator_rb)); if(!voiceIO) return FALSE;
#ifdef NDF_NEWIORB
    voiceIO->flags|=NDF_NEWIORB;
#endif
    voiceIO->mouths=0;
    voiceIO->ch_masks=(UBYTE*)kAudChanMasks;
    voiceIO->nm_masks=sizeof(kAudChanMasks);
    if(OpenDevice("narrator.device",0,(struct IORequest*)voiceIO,0)) return FALSE;
    return TRUE;
}

static void CloseSpeech(void){
    StopSpeech();
    if(voiceIO){ CloseDevice((struct IORequest*)voiceIO); DeleteIORequest((struct IORequest*)voiceIO); voiceIO=NULL; }
    if(narrPort){ DeleteMsgPort(narrPort); narrPort=NULL; }
    if(TranslatorBase){ CloseLibrary(TranslatorBase); TranslatorBase=NULL; }
}

static UBYTE* MakePhonemes(const char* text, ULONG* outLen){
    if(!text||!*text) return NULL;
    LONG inPos=0,inLen=(LONG)strlen(text); ULONG cap=512,used=0;
    UBYTE* buf=(UBYTE*)AllocVec(cap,MEMF_ANY); if(!buf) return NULL;
    while(inPos<inLen){
        UBYTE tmp[256]; tmp[0]=0;
        LONG rc=Translate((STRPTR)(text+inPos),inLen-inPos,(STRPTR)tmp,sizeof(tmp)-1);
        tmp[sizeof(tmp)-1]=0;
        LONG consumed=(rc==0)?(inLen-inPos):(rc>0?rc:-rc);
        ULONG tlen=(ULONG)strlen((char*)tmp);
        if(tlen){
            if(used+tlen+1>cap){
                cap=(used+tlen+1)*2;
                UBYTE* nb=(UBYTE*)AllocVec(cap,MEMF_ANY);
                if(!nb){ FreeVec(buf); return NULL; }
                memcpy(nb,buf,used); FreeVec(buf); buf=nb;
            }
            memcpy(buf+used,tmp,tlen); used+=tlen; buf[used]=0;
        }
        inPos+=consumed; if(rc==0) break;
    }
    if(used==0){ FreeVec(buf); return NULL; }
    if(outLen) *outLen=used; return buf;
}

static void Speak(const char* s){
    if(!voiceIO||!TranslatorBase||!s) return;
    StopSpeech(); /* ensure clean */
    ULONG plen=0; UBYTE* p=MakePhonemes(s,&plen); if(!p) return;
    voiceIO->rate=140; voiceIO->pitch=110; voiceIO->volume=64;
    voiceIO->message.io_Command=CMD_WRITE;
    voiceIO->message.io_Flags=0;
    voiceIO->message.io_Data=p;
    voiceIO->message.io_Length=plen;
    BeginIO((struct IORequest*)voiceIO);
    speaking=TRUE;
}

/* ------------ Blits ------------ */
/* REPLACE your RestoreBGRect function with this corrected version */

static void RestoreBGRect(WORD dx, WORD dy, WORD w, WORD h)
{
    if (w <= 0 || h <= 0) return;

    /* R = sprite rect in window coords */
    WORD rx1 = dx,         ry1 = dy;
    WORD rx2 = dx + w - 1, ry2 = dy + h - 1;

    if (!bgBM) {
        /* No background image - erase entire rect */
        EraseRect(rp, rx1, ry1, rx2, ry2);
        return;
    }

    /* B = background picture rect in window coords */
    WORD bx1 = bgX,            by1 = bgY;
    WORD bx2 = bgX + bgW - 1,  by2 = bgY + bgH - 1;

    /* Intersection I = R ∩ B */
    WORD ix1 = (rx1 > bx1) ? rx1 : bx1;
    WORD iy1 = (ry1 > by1) ? ry1 : by1;
    WORD ix2 = (rx2 < bx2) ? rx2 : bx2;
    WORD iy2 = (ry2 < by2) ? ry2 : by2;

    /* 1) Restore the part that sits over the BG image */
    if (ix1 <= ix2 && iy1 <= iy2) {
        WORD sw = ix2 - ix1 + 1;
        WORD sh = iy2 - iy1 + 1;
        WORD sx = ix1 - bgX;   /* source coords inside BG bitmap */
        WORD sy = iy1 - bgY;

        if (bgMask) BltMaskBitMapRastPort(bgBM, sx, sy, rp, ix1, iy1, sw, sh, 0xC0, bgMask);
        else        BltBitMapRastPort   (bgBM, sx, sy, rp, ix1, iy1, sw, sh, 0xC0);
    }

    /* 2) Erase any remainder of R that falls outside B using EraseRect */
    
    /* Top strip - above background */
    if (ry1 < by1) {
        WORD stripBottom = (ry2 < by1-1) ? ry2 : by1-1;
        EraseRect(rp, rx1, ry1, rx2, stripBottom);
    }

    /* Bottom strip - below background */
    if (ry2 > by2) {
        WORD stripTop = (ry1 > by2+1) ? ry1 : by2+1;
        EraseRect(rp, rx1, stripTop, rx2, ry2);
    }

    /* Left strip - to the left of background, within vertical overlap */
    if (rx1 < bx1) {
        WORD ly1 = (ry1 > by1) ? ry1 : by1;
        WORD ly2 = (ry2 < by2) ? ry2 : by2;
        if (ly1 <= ly2) {
            WORD stripRight = (rx2 < bx1-1) ? rx2 : bx1-1;
            EraseRect(rp, rx1, ly1, stripRight, ly2);
        }
    }

    /* Right strip - to the right of background, within vertical overlap */
    if (rx2 > bx2) {
        WORD ry1i = (ry1 > by1) ? ry1 : by1;
        WORD ry2i = (ry2 < by2) ? ry2 : by2;
        if (ry1i <= ry2i) {
            WORD stripLeft = (rx1 > bx2+1) ? rx1 : bx2+1;
            EraseRect(rp, stripLeft, ry1i, rx2, ry2i);
        }
    }
}

/* ------------ Game ------------ */
static void InitBear(void) {
    bearX = bgX + 50;  /* Start well within the background */
    
    /* Position bear on the floor instead of floating */
    /* Assuming floor starts around 75% down the background */
    bearY = bgY + (bgH * 9/10) - (doubleSize ? SPR_H * 2 : SPR_H);
    
    /* Ensure position is within collision bounds */
    if (bearY < UI_BAR_H + 10) bearY = UI_BAR_H + 10;
    
    bearVX = 2;
    animFrame = animTick = 0;
    state = ST_IDLE;
    talkTick = talkPhase = waveTick = feedTicks = sleepTick = 0;
    
    /* CRITICAL: Initialize tracking variables */
    lastPoopX = lastPoopY = -1;
    lastBearDrawX = lastBearDrawY = -1;
    lastBearDrawW = lastBearDrawH = 0;
    
    /* Update BOB position if it exists */
    if (bearVS) {
        bearVS->X = bearX;
        bearVS->Y = bearY;
        bearVS->OldX = bearX;
        bearVS->OldY = bearY;
    }
}
/* Safe size change with complete BOB recreation */
static BOOL ChangeBearSize(BOOL newDoubleSize) {
    if (doubleSize == newDoubleSize) return TRUE;
    
    /* Store old position and size */
    WORD oldX = bearX, oldY = bearY;
    WORD oldW = doubleSize ? (SPR_W * 2) : SPR_W;
    WORD oldH = doubleSize ? (SPR_H * 2) : SPR_H;
    
    /* CRITICAL: Erase old sprite position BEFORE changing size */
    if (bobSystemReady) {
        /* BOB system will handle cleanup automatically */
    } else {
        /* Manual cleanup for direct drawing */
        RestoreBGRect(oldX, oldY, oldW, oldH);
    }
    
    /* Clean up current BOB system completely */
    if (bobSystemReady) {
        CloseBobSystem();
    }
    
    /* Update size flag */
    doubleSize = newDoubleSize;
    
    /* Reinitialize everything */
    if (!InitBobSystem()) return FALSE;
    if (!CreateBobData()) {
        CloseBobSystem();
        return FALSE;
    }
    if (!CreateBearBob()) {
        CloseBobSystem();
        return FALSE;
    }
    
    /* Restore position with bounds checking */
    UWORD width = doubleSize ? (SPR_W * 2) : SPR_W;
    UWORD height = doubleSize ? (SPR_H * 2) : SPR_H;

    /* X position bounds checking */
    if (oldX > bgX + bgW - width) {
        bearX = bgX + bgW - width;
    } else {
        bearX = oldX;
    }

    /* Y position bounds checking - keep bear on floor but within room */
    WORD floorY = bgY + (bgH * 9/10) - height;  /* Floor position for current size */
    WORD maxY = bgY + bgH - height;             /* Bottom boundary of room */

    /* Use floor position, but don't let bear go below room bottom */
    bearY = (floorY < maxY) ? floorY : maxY;

    /* If bear was sleeping on sofa, keep him there */
    if (state == ST_SLEEP) {
        bearY = bgY + (bgH * 6/10) - height;
    }
    
    /* Reset tracking variables */
    lastBearDrawX = lastBearDrawY = -1;
    lastBearDrawW = lastBearDrawH = 0;
    
    /* Update BOB position */
    UpdateBobPosition(bearX, bearY);
    SetBobFrame(S_FRONT_C);
    
    return TRUE;
}

/* Enhanced Tick function with mood-based behavior */
static void Tick(void) {
    BearMood mood = GetBearMood();
    
    /* Calculate bounce offset for feeding */
    WORD offY = 0;
    if (state == ST_FEED && feedTicks > 0) {
        if (feedTicks > 6) offY = -6;
        else if (feedTicks > 3) offY = -4;
        else offY = -2;
        feedTicks--;
        if (feedTicks == 0) state = resumeState;
    }

        /* Store previous bear position for cleanup */
    WORD prevBearX = bearX, prevBearY = bearY;
    WORD currentDrawW = doubleSize ? (SPR_W * 2) : SPR_W;
    WORD currentDrawH = doubleSize ? (SPR_H * 2) : SPR_H;

        /* SPECIAL POSITIONING: Move bear to sofa when sleeping */
    WORD actualBearY = bearY;  /* Default floor position */
    if (state == ST_SLEEP) {
        /* Position bear on the sofa - approximately 60% down the background */
        actualBearY = bgY + (bgH * 7/10) - (doubleSize ? SPR_H * 2 : SPR_H);
        
        /* Center horizontally on sofa area (left side of room) */
        bearX = bgX + (bgW * 2/10);  /* About 20% from left edge */
    }

    
    /* Movement and behavior - modified by mood */
    switch(state) {
        case ST_IDLE:
            if (mood == MOOD_SLEEPING) {
                state = ST_SLEEP;
                /* Jump to sofa when falling asleep */
                bearX = bgX + (bgW * 2/10);
            } else if ((animTick++ & 31) == 0) {
                state = (bearVX > 0) ? ST_WALK_R : ST_WALK_L;
            }
            break;
            
        case ST_WALK_R:
            /* Only move if not sleeping */
            if (state != ST_SLEEP) {
                /* Slower movement when sick or sad */
                if (mood == MOOD_SICK || mood == MOOD_SAD) {
                    if ((animTick & 1) == 0) bearX += 1;
                } else {
                    bearX += 2;
                }
                
                if (bearX >= bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W)) {
                    bearX = bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W);
                    bearVX = -2;
                    state = ST_WALK_L;
                }
                
                /* Happy bears wave more often */
                UWORD waveChance = (mood == MOOD_HAPPY) ? 31 : 63;
                if ((animTick++ & waveChance) == 0) {
                    state = ST_WAVE;
                    waveTick = 0;
                }
            }
            break;
            
        case ST_WALK_L:
            /* Only move if not sleeping */
            if (state != ST_SLEEP) {
                /* Slower movement when sick or sad */
                if (mood == MOOD_SICK || mood == MOOD_SAD) {
                    if ((animTick & 1) == 0) bearX -= 1;
                } else {
                    bearX -= 2;
                }
                
                if (bearX <= bgX) {
                    bearX = bgX;
                    bearVX = +2;
                    state = ST_WALK_R;
                }
                
                /* Happy bears wave more often */
                UWORD waveChance2 = (mood == MOOD_HAPPY) ? 31 : 63;
                if ((animTick++ & waveChance2) == 0) {
                    state = ST_WAVE;
                    waveTick = 0;
                }
            }
            break;
            
        case ST_WAVE:
            if (++waveTick > 12) {
                waveTick = 0;
                state = (bearVX > 0) ? ST_WALK_R : ST_WALK_L;
            }
            break;
            
        case ST_SLEEP:
            /* Breathing animation during sleep */
            sleepTick++;
            if ((sleepTick & 31) == 0) offY = -1;  /* Gentle breathing */
            
            /* Wake up if not actually sleeping */
            if (!bearStats.sleeping) {
                state = ST_IDLE;
                sleepTick = 0;
                /* Return to normal floor position when waking up */
                actualBearY = bgY + (bgH * 9/10) - (doubleSize ? SPR_H * 2 : SPR_H);
            }
            break;
            
        case ST_SICK:
            /* Sick animation - barely move, stay on floor */
            if ((animTick++ & 63) == 0) {
                bearX += (rand() & 1) ? 1 : -1;  /* Random tiny movement */
                /* Keep within bounds */
                if (bearX < bgX) bearX = bgX;
                if (bearX > bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W)) 
                    bearX = bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W);
            }
            
            /* Recovery check */
            if (!bearStats.sick && bearStats.health > 30) {
                state = ST_IDLE;
            }
            break;
            
        case ST_FEED:
            /* Position handled above */
            break;
    }

    /* CLEANUP PREVIOUS POOP POSITION */
    if (lastPoopX >= 0 && lastPoopY >= 0) {
        /* Restore background where poop was drawn */
        if (poopBMHD) {
            RestoreBGRect(lastPoopX, lastPoopY, poopBMHD->bmh_Width, poopBMHD->bmh_Height);
        }
        lastPoopX = lastPoopY = -1;  /* Mark as cleaned */
    }
    
    /* CLEANUP PREVIOUS BEAR POSITION if not using BOBs */
    if (!bobSystemReady && lastBearDrawX >= 0 && lastBearDrawY >= 0) {
        RestoreBGRect(lastBearDrawX, lastBearDrawY, lastBearDrawW, lastBearDrawH);
    }
    
    /* Pick frame based on state and mood */
    UWORD frame = S_FRONT_C;
    
    if (speaking) {
        if (++talkTick >= 3) { talkTick = 0; talkPhase ^= 1; }
        frame = talkSeq[talkPhase];
    } else if (state == ST_SLEEP) {
        frame = S_BACK;  /* Sleeping position */
    } else if (state == ST_SICK) {
        frame = S_BACK;  /* Sick/weak position */
    } else if (state == ST_WALK_R) {
        /* Slower animation when sick */
        UWORD animSpeed = (mood == MOOD_SICK) ? 1 : 3;
        if ((animFrame++ & animSpeed) == 0) {
            static UWORD walkPhase = 0;
            walkPhase = (walkPhase + 1) & 3;
            frame = walkRSeq[walkPhase];
        } else {
            frame = walkRSeq[animFrame & 3];
        }
    } else if (state == ST_WALK_L) {
        /* Slower animation when sick */
        UWORD animSpeed = (mood == MOOD_SICK) ? 1 : 3;
        if ((animFrame++ & animSpeed) == 0) {
            static UWORD walkPhase = 0;
            walkPhase = (walkPhase + 1) & 3;
            frame = walkLSeq[walkPhase];
        } else {
            frame = walkLSeq[animFrame & 3];
        }
    } else if (state == ST_WAVE) {
        frame = (waveTick & 1) ? S_WAVE : S_FRONT_C;
    } else if (mood == MOOD_SAD) {
        frame = S_FRONT_O;  /* Sad expression */
    }
    
    /* Update BOB with the calculated position (sofa or floor) */
    if (bobSystemReady && bearBob && bearVS) {
        SetBobFrame(frame);
        UpdateBobPosition(bearX, actualBearY + offY);  /* Use actualBearY instead of bearY */
        DrawBobs();
    } else {
        /* Fallback to direct sprite drawing */
        DrawSpriteDirectly(frame, bearX, actualBearY + offY);
        lastBearDrawX = bearX;
        lastBearDrawY = actualBearY + offY;
        lastBearDrawW = currentDrawW;
        lastBearDrawH = currentDrawH;
    }
    
    /* Draw poop indicator if messy - WITH PROPER POSITION TRACKING */
    if (bearStats.pooped && poopBM && poopBMHD) {
/* Position poop on the floor behind the bear, not relative to bear position */
    static WORD staticPoopX = -1, staticPoopY = -1;
    
    /* Set poop position only once when it first appears */
    if (staticPoopX < 0 || staticPoopY < 0) {
        staticPoopX = bearX + 20;  /* Slightly behind bear */
        staticPoopY = bgY + (bgH * 3/4) + 10;  /* On the floor */
        
        /* Keep poop within background bounds */
        if (staticPoopX > bgX + bgW - poopBMHD->bmh_Width) {
            staticPoopX = bgX + bgW - poopBMHD->bmh_Width;
        }
        if (staticPoopX < bgX) {
            staticPoopX = bgX;
        }
    }
    
    /* Only draw if position changed or first time */
    if (staticPoopX != lastPoopX || staticPoopY != lastPoopY) {
        BltBitMapRastPort(poopBM, 0, 0, rp, staticPoopX, staticPoopY, 
                          poopBMHD->bmh_Width, poopBMHD->bmh_Height, 0xC0);
        lastPoopX = staticPoopX;
        lastPoopY = staticPoopY;
    }
    
    /* Reset static position when cleaned */
    if (!bearStats.pooped) {
        staticPoopX = staticPoopY = -1;
    }
}
    
    /* Add random chatter */
    RandomBearChatter();
}

/* ------------ Main ------------ */
int main(void){
    struct Screen *scr = LockPubScreen(NULL); if(!scr) return 20;
    GadToolsBase = OpenLibrary("gadtools.library",37);
    DataTypesBase = OpenLibrary("datatypes.library",39);
    if(!GadToolsBase||!DataTypesBase){ 
        if(GadToolsBase) CloseLibrary(GadToolsBase); 
        if(DataTypesBase) CloseLibrary(DataTypesBase); 
        UnlockPubScreen(NULL,scr); 
        return 20; 
    }

    vi = GetVisualInfoA(scr,NULL);
    struct NewWindow nw = {
        .LeftEdge=40,.TopEdge=32,.Width=WIN_W,.Height=WIN_H,
        .DetailPen=0,.BlockPen=1,
        .IDCMPFlags=IDCMP_GADGETUP|IDCMP_REFRESHWINDOW|IDCMP_CLOSEWINDOW|IDCMP_IDCMPUPDATE,
        .Flags=WFLG_SMART_REFRESH|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_ACTIVATE,
        .FirstGadget=NULL,.CheckMark=NULL,.Title=(UBYTE*)"AG Bear Tamagotchi (Hardware BOBs)",
        .Screen=NULL,.BitMap=NULL,.MinWidth=WIN_W,.MinHeight=WIN_H,.MaxWidth=WIN_W,.MaxHeight=WIN_H,
        .Type=WBENCHSCREEN
    };
    win = OpenWindow(&nw); 
    if(!win){ 
        FreeVisualInfo(vi); 
        CloseLibrary(DataTypesBase); 
        CloseLibrary(GadToolsBase); 
        UnlockPubScreen(NULL,scr); 
        return 10; 
    }
    rp = win->RPort;

/* Enhanced UI with compact Tamagotchi controls */
CreateContext(&gadList);
struct NewGadget ng; 
memset(&ng,0,sizeof(ng));
ng.ng_TextAttr=scr->Font; 
ng.ng_VisualInfo=vi;

/* Text input at top */
WORD strW = win->Width - (UI_MARGIN_X*2);
ng.ng_LeftEdge=UI_MARGIN_X; 
ng.ng_TopEdge=UI_MARGIN_Y; 
ng.ng_Width=strW; 
ng.ng_Height=UI_STR_H; 
ng.ng_GadgetID=1;
txtGad=CreateGadget(STRING_KIND,gadList,&ng, GA_ID,1, GTST_MaxChars,200, GTST_String,(ULONG)"hello friend!", TAG_END);

/* Calculate button layout */
WORD btnPerRow = 4;  /* 4 buttons per row fits nicely */
WORD totalBtnWidth = btnPerRow * UI_BTN_W + (btnPerRow-1) * UI_GAP_X;
WORD startX = (win->Width - totalBtnWidth) / 2;  /* Center the button grid */

/* Row 1: Primary care functions */
WORD y1 = UI_MARGIN_Y + UI_STR_H + UI_GAP_Y;
WORD x = startX;

ng.ng_LeftEdge=x; ng.ng_TopEdge=y1; ng.ng_Width=UI_BTN_W; ng.ng_Height=UI_BTN_H; ng.ng_GadgetID=3; ng.ng_GadgetText=(STRPTR)"_Feed";
feedBtn=CreateGadget(BUTTON_KIND,txtGad,&ng, GA_ID,3, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=6; ng.ng_GadgetText=(STRPTR)"_Play";
playBtn=CreateGadget(BUTTON_KIND,feedBtn,&ng, GA_ID,6, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=7; ng.ng_GadgetText=(STRPTR)"_Clean";
cleanBtn=CreateGadget(BUTTON_KIND,playBtn,&ng, GA_ID,7, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=8; ng.ng_GadgetText=(STRPTR)"Sl_eep";
sleepBtn=CreateGadget(BUTTON_KIND,cleanBtn,&ng, GA_ID,8, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

/* Row 2: Health and interface */
WORD y2 = y1 + UI_BTN_H + UI_GAP_Y;
x = startX;

ng.ng_LeftEdge=x; ng.ng_TopEdge=y2; ng.ng_GadgetID=9; ng.ng_GadgetText=(STRPTR)"_Med";
medicineBtn=CreateGadget(BUTTON_KIND,sleepBtn,&ng, GA_ID,9, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=10; ng.ng_GadgetText=(STRPTR)"S_tats";
statsBtn=CreateGadget(BUTTON_KIND,medicineBtn,&ng, GA_ID,10, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=5; ng.ng_GadgetText=(STRPTR)"Si_ze";
sizeBtn=CreateGadget(BUTTON_KIND,statsBtn,&ng, GA_ID,5, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

x += UI_BTN_W + UI_GAP_X;
ng.ng_LeftEdge=x; ng.ng_GadgetID=4; ng.ng_GadgetText=(STRPTR)"_Stop";
stopBtn=CreateGadget(BUTTON_KIND,sizeBtn,&ng, GA_ID,4, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

/* Row 3: Speech controls (centered, only 2 buttons) */
WORD y3 = y2 + UI_BTN_H + UI_GAP_Y;
WORD speechStartX = (win->Width - (2 * UI_BTN_W + UI_GAP_X)) / 2;

ng.ng_LeftEdge=speechStartX; ng.ng_TopEdge=y3; ng.ng_GadgetID=2; ng.ng_GadgetText=(STRPTR)"_Speak";
sayBtn=CreateGadget(BUTTON_KIND,stopBtn,&ng, GA_ID,2, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

ng.ng_LeftEdge=speechStartX + UI_BTN_W + UI_GAP_X; ng.ng_GadgetID=11; ng.ng_GadgetText=(STRPTR)"_Quit";
/* Add a quit button for convenience */
struct Gadget *quitBtn=CreateGadget(BUTTON_KIND,sayBtn,&ng, GA_ID,11, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);


    AddGList(win,gadList,(UWORD)-1,(UWORD)-1,NULL);
    RefreshGList(gadList,win,NULL,(UWORD)-1);
    GT_RefreshWindow(win,NULL);
    /* Load images and replace five buttons with image buttons */
    /* Load button icons */
    LoadButtonIcons();
    CreateIntuitionImageGadgets();
    
    /* Initialize subsystems */
    if(!InitTimer()) SetWindowTitles(win,(UBYTE*)"Timer init failed",(UBYTE*)-1);
    (void)InitSpeech();
    (void)InitBearSounds();
    if (numSounds > 0) {
        PlayBearSound(SOUND_WAKEUP);  /* Bear says hello when program starts */
    }
    
    /* Initialize Tamagotchi system */
    /* Try to load existing save first */
    if (!LoadBearStats("PROGDIR:bear.save")) {
        InitTamagotchiStats();  /* New bear if no save file */
    }
    
    /* Load graphics */
    (void)LoadBG(BG_PATH);
    (void)LoadSpriteSheet(SPR_PATH);
    (void)LoadPoopIcon();
    
    /* Initialize BOB system */
    if (!InitBobSystem()) {
        bobSystemReady = FALSE;
    } else {
        if (!CreateBobData()) {
            CloseBobSystem();
            bobSystemReady = FALSE;
        } else if (!CreateBearBob()) {
            CloseBobSystem(); 
            bobSystemReady = FALSE;
        }
    }
    
    InitBear();
    
    /* Draw background */
    if(bgBM){
        if (bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
        else        BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
    }
    
    /* Initial BOB draw */
    if (bobSystemReady) {
        DrawBobs();
    }
    
    /* Simple random seed using system timer */
    if (tmReq) {
        srand((ULONG)tmReq->tr_time.tv_micro);
    } else {
        srand(1);  /* Fallback seed */
    }
    
    BOOL running=TRUE;
    ULONG winSig = 1L<<win->UserPort->mp_SigBit;
    ULONG timSig = tmPort ? (1L<<tmPort->mp_SigBit) : 0;

    while(running){
        ULONG sigs = Wait(winSig | timSig);

        if ((sigs & timSig) && tmPort){
            struct Message *m; 
            while((m=GetMsg(tmPort))){ 
                UpdateTamagotchiStats();  /* Update stats first */
                Tick();                   /* Then do visual updates */
                UpdateStatsDisplay();     /* Update window title */
                
                /* Auto-save every 5 minutes */
                if ((gameTicks % 3600) == 0) {
                    SaveBearStats("PROGDIR:bear.save");
                }
            }
            ArmTimerMS(TICK_MS);
        }

        if (sigs & winSig){
            struct IntuiMessage *imsg;
            while((imsg=GT_GetIMsg(win->UserPort))){
                ULONG cls=imsg->Class;
                UWORD gid = imsg->IAddress ? ((struct Gadget*)imsg->IAddress)->GadgetID : 0;

                if (cls==IDCMP_CLOSEWINDOW){
                    running=FALSE;
                } else if (cls==IDCMP_GADGETUP){
                    if (gid==2){  /* Speak button */
                        STRPTR s=NULL; 
                        GT_GetGadgetAttrs(txtGad,win,NULL,GTST_String,(ULONG)&s,TAG_END);
                        if (s) {
                            Speak((const char*)s);
                            RespondToUserText((const char*)s);
                        }
                        state=ST_IDLE;
                    } else if (gid==3){  /* Feed button */
                        FeedBear();
                    } else if (gid==4) {  /* Stop button */
                        StopSpeech();
                        /* Stop audio too */
                        if (audioReq && audioPlaying) {
                            AbortIO((struct IORequest*)audioReq);
                            WaitIO((struct IORequest*)audioReq);
                            audioPlaying = FALSE;
                        }
                        PlayBearSound(SOUND_DISAGREE);
                        speaking = FALSE;
                        talkTick = talkPhase = 0;
                        
                        /* Force bear to idle frame */
                        if (bobSystemReady && bearBob) {
                            SetBobFrame(S_FRONT_C);
                            DrawBobs();
                        }
                    } else if (gid==5) {  /* Size button */
                        BOOL newDoubleSize = !doubleSize;
                        
                        /* Use safe size change function */
                        if (ChangeBearSize(newDoubleSize)) {
                            /* Update button text */
                            GT_SetGadgetAttrs(sizeBtn, win, NULL, 
                                            GA_Text, doubleSize ? (ULONG)"1x" : (ULONG)"2x", 
                                            TAG_END);
                            
                            /* Redraw background and BOBs */
                            if(bgBM){
                                if (bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
                                else        BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
                            }
                            if (bobSystemReady) {
                                DrawBobs();
                            }
                        }
                    } else if (gid==6) {  /* Play button */
                        PlayWithBear();
                    } else if (gid==7) {  /* Clean button */
                        CleanBear();
                    } else if (gid==8) {  /* Sleep button */
                        ToggleBearSleep();
                    } else if (gid==9) {  /* Medicine button */
                        MedicineBear();
                    } else if (gid==10) {  /* Stats button */
                        ShowStatsRequester();
                    }  else if (gid==11) {  /* Quit button */
                        running=FALSE;
                    }
                } else if (cls==IDCMP_REFRESHWINDOW){
                    GT_BeginRefresh(win);
                    
                    /* Redraw background */
                    if(bgBM){
                        if (bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
                        else        BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
                    }
                    
                    /* Redraw BOBs */
                    if (bobSystemReady) {
                        DrawBobs();
                    } else {
                        /* Fallback for non-BOB systems */
                        Tick();
                    }
                    
                    GT_EndRefresh(win,TRUE);
                }
                GT_ReplyIMsg(imsg);
            }
        }

        /* Check for completed audio */
        if (audioReq && audioPlaying && CheckIO((struct IORequest*)audioReq)){
            WaitIO((struct IORequest*)audioReq);
            audioPlaying = FALSE; 
        }

        /* Check for completed speech */
        if (voiceIO && speaking && CheckIO((struct IORequest*)voiceIO)){
            WaitIO((struct IORequest*)voiceIO);
            if(voiceIO->message.io_Data){ 
                FreeVec((APTR)voiceIO->message.io_Data); 
                voiceIO->message.io_Data=NULL; 
            }
            speaking=FALSE;
        }
    }

    /* SHUTDOWN SEQUENCE */
printf("Shutting down Tamagotchi...\n");

/* Final save */
SaveBearStats("PROGDIR:bear.save");

/* Stop all active operations FIRST */
StopSpeech();

if (audioReq && audioPlaying) {
    AbortIO((struct IORequest*)audioReq);
    WaitIO((struct IORequest*)audioReq);
    audioPlaying = FALSE;
}

if (tmReq && !CheckIO((struct IORequest*)tmReq)) {
    AbortIO((struct IORequest*)tmReq);
    WaitIO((struct IORequest*)tmReq);
}

/* CRITICAL FIX: Clean up gadgets BEFORE closing window */
if (win) {
    /* Remove all gadgets from window first */
    if (gadList) RemoveGList(win, gadList, -1);
    
    /* Free ONLY the custom Intuition gadgets we created */
    if (playBtn && (playBtn->Flags & GFLG_GADGIMAGE)) {
        FreeVec(playBtn);
        playBtn = NULL;
    }
    if (cleanBtn && (cleanBtn->Flags & GFLG_GADGIMAGE)) {
        FreeVec(cleanBtn);
        cleanBtn = NULL;
    }
    if (sleepBtn && (sleepBtn->Flags & GFLG_GADGIMAGE)) {
        FreeVec(sleepBtn);
        sleepBtn = NULL;
    }
    if (medicineBtn && (medicineBtn->Flags & GFLG_GADGIMAGE)) {
        FreeVec(medicineBtn);
        medicineBtn = NULL;
    }
    if (statsBtn && (statsBtn->Flags & GFLG_GADGIMAGE)) {
        FreeVec(statsBtn);
        statsBtn = NULL;
    }
    
    /* Free GadTools gadgets */
    if (gadList) {
        FreeGadgets(gadList);
        gadList = NULL;
    }
    
    /* NOW close the window */
    CloseWindow(win);
    win = NULL;
}
    
    /* Clean up subsystems */
    CloseBobSystem();
    CloseTimer();
    CloseBearSounds();
    CloseSpeech();
    FreeSpriteSheet();
    FreeBG();
    
    /* Clean up icons */
    FreeButtonIcons();
    if (poopIcon) {
        DisposeDTObject(poopIcon);
        poopIcon = NULL;
    }
    
    /* Clean up libraries */
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
    if (GadToolsBase) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    
    UnlockPubScreen(NULL, scr);
    
    printf("Tamagotchi shutdown complete\n");
    return 0;
}