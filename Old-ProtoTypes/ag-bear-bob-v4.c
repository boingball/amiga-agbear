/* ag-bear-bob-hw.c — AG Bear with Hardware BOBs for AmigaOS 3.2
 * Hardware accelerated version using proper BOBs and blitter operations
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

/* ------------ Config ------------ */
#define WIN_W  500
#define WIN_H  340

#define UI_MARGIN_X 10
#define UI_MARGIN_Y 14
#define UI_STR_H    16
#define UI_BTN_W    90
#define UI_BTN_H    16
#define UI_GAP_X    10
#define UI_GAP_Y     8
#define UI_BAR_H   (UI_MARGIN_Y + UI_STR_H + UI_GAP_Y + UI_BTN_H + UI_MARGIN_Y)

#define BG_PATH  "PROGDIR:bg.ilbm"
#define SPR_PATH "PROGDIR:bear32.ilbm"

#define MAX_SOUNDS 16
#define MAX_SOUND_SIZE 32768

/* sprite sheet: 48x48 tiles, 4x2 grid at (0,0) of a 320x200 picture */
#define SPR_W 48
#define SPR_H 48
#define SPR_COLS 4
#define SPR_ROWS 2
#define SPR_ORIGIN_X 0
#define SPR_ORIGIN_Y 0

#define TICK_MS 80  /* ~12.5 FPS */

/* BOB Configuration */
#define MAX_BOBS 8
#define BEAR_DEPTH 8  /* Assume 16 color sprites */
#define BEAR_W (doubleSize ? (SPR_W*2) : SPR_W)
#define BEAR_H (doubleSize ? (SPR_H*2) : SPR_H)

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

/* Audio system - same as before */
static struct MsgPort *narrPort=NULL;
static struct narrator_rb *voiceIO=NULL;
static struct Gadget *gadList=NULL,*txtGad=NULL,*sayBtn=NULL,*feedBtn=NULL,*stopBtn=NULL;
static const UBYTE kAudChanMasks[] = {3,12};

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
static WORD bgX=0,bgY=0,bgW=300,bgH=200;

/* Sprite sheet */
static Object *sprObj=NULL;
static struct BitMapHeader *sprBMHD=NULL;
static struct BitMap *sprBM=NULL;

/* Bear state */
typedef enum { ST_IDLE, ST_WALK_R, ST_WALK_L, ST_WAVE, ST_FEED } BearState;
static BearState state=ST_IDLE;
static WORD bearX, bearY, bearVX;
static UWORD animFrame=0, animTick=0;
static BOOL speaking=FALSE;

static UWORD talkTick=0, talkPhase=0;
static UWORD waveTick=0;
static UWORD feedTicks=0;
static BearState resumeState=ST_IDLE;

/* Frame indices */
enum { S_FRONT_C=0, S_FRONT_O=1, S_WAVE=2, S_BACK=3,
       S_WR0=4, S_WR1=5, S_WL0=6, S_WL1=7 };
static const UWORD walkRSeq[4]={S_WR0,S_WR1,S_WR0,S_WR1};
static const UWORD walkLSeq[4]={S_WL0,S_WL1,S_WL0,S_WL1};
static const UWORD talkSeq[2] ={S_FRONT_O,S_FRONT_C};

/* ------------ BOB System Functions ------------ */

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
    
    /* Allocate CHIP memory for RAW planar data (NO HEADERS) */
    ULONG totalWords = (ULONG)wordsPerLine * height * depth;
    WORD *img = AllocVec(totalWords * sizeof(WORD), MEMF_CHIP | MEMF_CLEAR);
    if (!img) return NULL;
    
    printf("Allocated %lu words for image data (%d planes)\n", totalWords, depth);
    
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
    UBYTE depth = bm->Depth;
    
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
    
    printf("Creating single-plane mask: %dx%d pixels\n", width, height);
    
    /* Build mask by checking each pixel */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            /* Read pixel color from source */
            UBYTE colorIdx = (UBYTE)ReadPixel(&srcRP, x, y);
            
            /* Set mask pixel: 1 for non-transparent (colorIdx != 0) */
            if (colorIdx != 0) {
                SetAPen(&maskRP, 1);
                WritePixel(&maskRP, x, y);
            }
            /* Pixel remains 0 for transparent pixels (color 0) */
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
    
    /* Debug: Check mask data */
    BOOL hasData = FALSE;
    for (int i = 0; i < totalWords; i++) {
        if (mask[i] != 0) {
            hasData = TRUE;
            break;
        }
    }
    printf("Single-plane mask created: %s\n", hasData ? "has data" : "all zeros");
    
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
    
    printf("Drawing sprite frame %d at (%d,%d)\n", frame, x, y);
    
    /* Use the existing BlitSpriteFrameWithColorKey function */
    BlitSpriteFrameWithColorKey(frame, x, y);
}

/* Create BOB data for all sprite frames */
static BOOL CreateBobData(void) {
    if (!bobSystemReady || !sprBM) return FALSE;
    
    printf("Creating BOB data for %d frames\n", SPR_COLS * SPR_ROWS);
    
    for (int frame = 0; frame < (SPR_COLS * SPR_ROWS); frame++) {
        bobBitMaps[frame] = CreateBobBitMap(frame);
        if (!bobBitMaps[frame]) {
            printf("Failed to create bitmap for frame %d\n", frame);
            continue;
        }
        
        UWORD width = doubleSize ? (SPR_W * 2) : SPR_W;
        UWORD height = doubleSize ? (SPR_H * 2) : SPR_H;
        
        /* Create RAW image data and proper transparency mask */
        bobImageData[frame] = CreateBobImageData(bobBitMaps[frame], width, height);
        bobMaskData[frame] = CreateBobMask(bobBitMaps[frame], width, height);
        
        if (!bobImageData[frame] || !bobMaskData[frame]) {
            printf("Failed to create BOB data for frame %d\n", frame);
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

static void DebugBobMask(UWORD frame) {
    if (!bobMaskData[frame]) {
        printf("No mask data for frame %d\n", frame);
        return;
    }
    
    UWORD width = doubleSize ? (SPR_W * 2) : SPR_W;
    UWORD height = doubleSize ? (SPR_H * 2) : SPR_H;
    UWORD wordsPerLine = (width + 15) >> 4;
    
    printf("=== MASK DEBUG for frame %d ===\n", frame);
    printf("Size: %dx%d pixels (%d words per line)\n", width, height, wordsPerLine);
    
    WORD *mask = bobMaskData[frame];
    ULONG totalBits = 0, setBits = 0;
    
    /* Count set bits in first few lines */
    for (int y = 0; y < (height > 8 ? 8 : height); y++) {
        WORD *line = mask + y * wordsPerLine;
        printf("Line %d: ", y);
        
        for (int w = 0; w < wordsPerLine; w++) {
            UWORD word = line[w];
            printf("%04X ", word);
            
            /* Count bits */
            for (int b = 0; b < 16; b++) {
                totalBits++;
                if (word & (1 << b)) setBits++;
            }
        }
        printf("\n");
    }
    
    printf("Bits set: %lu/%lu (%.1f%%)\n", setBits, totalBits, 
           totalBits ? (setBits * 100 / totalBits) : 0);
    printf("=========================\n");
}

/* Fixed BOB creation with proper error checking */
static BOOL CreateBearBob(void) {
    if (!bobSystemReady || !sprBM || !rp) {
        printf("ERROR: BOB prerequisites not met\n");
        return FALSE;
    }

    UWORD wpx = doubleSize ? (SPR_W * 2) : SPR_W;
    UWORD hpx = doubleSize ? (SPR_H * 2) : SPR_H;
    UWORD wwords = (wpx + 15) >> 4;
    UBYTE bobDepth = sprBM->Depth;

    printf("Creating BOB: %dx%d pixels (%d words), depth=%d\n", 
           wpx, hpx, wwords, bobDepth);

    /* Allocate structures */
    bearBob = (struct Bob *)AllocVec(sizeof(struct Bob), MEMF_CLEAR);
    bearVS = (struct VSprite *)AllocVec(sizeof(struct VSprite), MEMF_CLEAR);
    if (!bearBob || !bearVS) {
        printf("ERROR: Failed to allocate BOB structures\n");
        return FALSE;
    }

    /* Validate we have data */
    if (!bobImageData[S_FRONT_C] || !bobMaskData[S_FRONT_C]) {
        printf("ERROR: No BOB data for initial frame\n");
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
    
    /* For BOB transparency, we DON'T use CollMask - that's for collision */
    bearVS->CollMask = NULL;  /* No collision detection */
    bearVS->BorderLine = NULL;
    bearVS->SprColors = NULL;
    bearVS->PlaneOnOff = 0;
    bearVS->Flags = SAVEBACK | OVERLAY;

    /* === BOB SETUP === */
    bearBob->BobVSprite = bearVS;
    /* ImageShadow is the transparency mask for blitter operations */
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
    printf("Adding BOB to gel list\n");
    AddBob(bearBob, rp);

    /* Sort once during setup */
    printf("Sorting gel list once during setup\n");
    SortGList(rp);

    printf("BOB creation successful\n");
    return TRUE;
}

/* Safe cleanup of BOB data arrays */
static void SetBobFrame(UWORD frame) {
    if (!bearVS || !bearBob || frame >= (SPR_COLS * SPR_ROWS)) return;
    
    if (bobImageData[frame] && bobMaskData[frame]) {
        bearVS->ImageData = bobImageData[frame];
        bearBob->ImageShadow = bobMaskData[frame];
        
        /* Debug the mask on first frame change */
        static BOOL debugged = FALSE;
        if (!debugged && frame == S_FRONT_C) {
            DebugBobMask(frame);
            debugged = TRUE;
        }
    }
}


/* Update BOB position */
static void UpdateBobPosition(WORD x, WORD y) {
    if (!bearVS) return;
    bearVS->X = x;
    bearVS->Y = y;
}

/* Safe cleanup of BOB data arrays */
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

/* Clean up BOB system */
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

/* Safer BOB rendering with error checking */
static void DrawBobs(void) {
    if (!bobSystemReady || !rp || !bearBob || !bearVS) {
        return;
    }
    
    /* Only call DrawGList - NO SortGList on every frame */
    DrawGList(rp, &win->WScreen->ViewPort);
    WaitTOF();
}

/* ------------ Utils ------------ */
static UBYTE *BuildMaskFromColor0(struct BitMap *bm, WORD w, WORD h){
    if(!bm||w<=0||h<=0) return NULL;
    UWORD bprMask=((w+15)>>4)<<1;
    UBYTE *mask=(UBYTE*)AllocVec((ULONG)bprMask*h,MEMF_CLEAR);
    if(!mask) return NULL;
    
    ULONG transparentCount = 0, opaqueCount = 0;
    
    for(WORD y=0;y<h;y++){
        UBYTE *mrow=mask+(ULONG)bprMask*y;
        for(WORD x=0;x<w;x++){
            UBYTE planeBit=(UBYTE)(0x80>>(x&7)), idx=0;
            
            // Read pixel color index from all bitplanes
            for(int p=0;p<bm->Depth;p++){
                UBYTE *pp=(UBYTE*)bm->Planes[p]+(ULONG)bm->BytesPerRow*y+(x>>3);
                if((*pp)&planeBit) idx|=(1U<<p);
            }
            
            // Set mask bit to 1 for NON-transparent pixels (idx != 0)
            if(idx != 0) {
                mrow[x>>3] |= (0x80>>(x&7)); // opaque pixel - draw it
                opaqueCount++;
            } else {
                transparentCount++;
            }
        }
    }
    
   return mask;
}

/* Load a single IFF 8SVX sound file */
static BOOL LoadSound(const char *filename, UWORD index) {
    if (index >= MAX_SOUNDS) return FALSE;
    
    BPTR file = Open((STRPTR)filename, MODE_OLDFILE);
    if (!file) {
        printf("Could not open sound file: %s\n", filename);
        return FALSE;
    }
    
    /* Get file size first */
    Seek(file, 0, OFFSET_END);
    LONG fileSize = Seek(file, 0, OFFSET_BEGINNING);
    if (fileSize <= 12 || fileSize > MAX_SOUND_SIZE) {
        printf("Invalid file size: %ld bytes\n", fileSize);
        Close(file);
        return FALSE;
    }
    
    /* Allocate buffer for entire file */
    UBYTE *fileData = (UBYTE*)AllocVec(fileSize, MEMF_ANY);
    if (!fileData) {
        printf("Could not allocate %ld bytes for file\n", fileSize);
        Close(file);
        return FALSE;
    }
    
    /* Read entire file */
    LONG bytesRead = Read(file, fileData, fileSize);
    Close(file);
    
    if (bytesRead != fileSize) {
        printf("Read error: got %ld bytes, expected %ld\n", bytesRead, fileSize);
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Safely check FORM header */
    if (fileSize < 12 ||
        fileData[0] != 'F' || fileData[1] != 'O' || 
        fileData[2] != 'R' || fileData[3] != 'M') {
        printf("Not a valid IFF FORM file: %s\n", filename);
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Check 8SVX type */
    if (fileData[8] != '8' || fileData[9] != 'S' || 
        fileData[10] != 'V' || fileData[11] != 'X') {
        printf("Not an 8SVX audio file: %s\n", filename);
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Parse chunks safely - but ignore sample rate, force 8287 Hz */
    ULONG pos = 12;  /* Skip FORM header */
    ULONG sampleRate = 8287;  /* Force PAL middle C - ignore file data */
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
        if (pos + 8 + chunkSize > fileSize) {
            printf("Chunk extends beyond file: pos=%lu, size=%lu, fileSize=%ld\n", 
                   pos, chunkSize, fileSize);
            break;
        }
        
        if (chunkID == 0x424F4459) {  /* 'BODY' - Sample Data */
            if (chunkSize > 0 && chunkSize <= MAX_SOUND_SIZE) {
                sampleData = &fileData[pos + 8];
                sampleLength = chunkSize;
                printf("Found BODY: %lu bytes of sample data\n", sampleLength);
                break;  /* Found what we need */
            } else {
                printf("Invalid BODY chunk size: %lu\n", chunkSize);
            }
        }
        
        /* Move to next chunk (with proper alignment) */
        pos += 8 + chunkSize;
        if (chunkSize & 1) pos++;  /* IFF chunks are word-aligned */
    }
    
    if (!sampleData || sampleLength == 0) {
        printf("Could not find valid sample data in: %s\n", filename);
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Allocate CHIP memory for sample */
    bearSounds[index].data = (UBYTE*)AllocVec(sampleLength, MEMF_CHIP);
    if (!bearSounds[index].data) {
        printf("Could not allocate %lu bytes of CHIP RAM\n", sampleLength);
        FreeVec(fileData);
        return FALSE;
    }
    
    /* Copy sample data */
    CopyMem(sampleData, bearSounds[index].data, sampleLength);
    bearSounds[index].length = sampleLength;
    
    /* Calculate period from sample rate */
    if (sampleRate > 0 && sampleRate < 65536) {
        bearSounds[index].period = 3579545 / sampleRate;
    } else {
        bearSounds[index].period = 432;  /* Force PAL middle C */
    }
    
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
    if (!audioPort) {
        return FALSE;
    }
    
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
        } else {
            printf("Skipping failed sound: %s\n", soundFiles[i]);
        }
    }
    
    printf("Audio initialization complete: %d sounds loaded\n", numSounds);
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

/* Random chatter with more variety - bear mumbles and responds */
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

// Use a pixel-by-pixel approach that checks for a specific "transparent" color
static void BlitSpriteFrameWithColorKey(UWORD frame, WORD dx, WORD dy){
    if(!sprBM||!sprBMHD) return;
    
    UWORD col = frame % SPR_COLS;
    UWORD row = frame / SPR_COLS;
    WORD sx = SPR_ORIGIN_X + (WORD)(col * SPR_W);
    WORD sy = SPR_ORIGIN_Y + (WORD)(row * SPR_H);
    
    // Cache these values to avoid repeated lookups
    UBYTE depth = sprBM->Depth;
    ULONG bytesPerRow = sprBM->BytesPerRow;
    UBYTE **planes = (UBYTE**)sprBM->Planes;
    
    // Find background color once
    UBYTE bgColor = 0;
    for(int p = 0; p < depth; p++){
        if(planes[p][0] & 0x80) bgColor |= (1U << p);
    }
    
    if (doubleSize) {
        /* 2x scaled blitting - each source pixel becomes a 2x2 block */
        for(WORD y = 0; y < SPR_H; y++) {
            WORD srcY = sy + y;
            if(srcY < 0 || srcY >= sprBMHD->bmh_Height) continue;
            
            for(WORD x = 0; x < SPR_W; x++) {
                WORD srcX = sx + x;
                if(srcX < 0 || srcX >= sprBMHD->bmh_Width) continue;
                
                // Read pixel color
                UBYTE byteOffset = srcX >> 3;
                UBYTE bitMask = 0x80 >> (srcX & 7);
                UBYTE colorIdx = 0;
                
                for(int p = 0; p < depth; p++){
                    UBYTE *planeByte = planes[p] + bytesPerRow * srcY + byteOffset;
                    if((*planeByte) & bitMask) {
                        colorIdx |= (1U << p);
                    }
                }
                
                // Draw 2x2 pixel block if not transparent
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
static void RestoreBGRect(WORD dx, WORD dy, WORD w, WORD h)
{
    if (w <= 0 || h <= 0) return;

    /* R = sprite rect in window coords */
    WORD rx1 = dx,         ry1 = dy;
    WORD rx2 = dx + w - 1, ry2 = dy + h - 1;

    /* B = background picture rect in window coords (if we have one) */
    WORD bx1 = bgX,            by1 = bgY;
    WORD bx2 = bgX + bgW - 1,  by2 = bgY + bgH - 1;

    if (bgBM) {
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
    }

    /* 2) Erase any remainder of R that falls outside B using the window bg */
    
    /* Top strip - above background or above sprite rect */
    if (bgBM && ry1 < by1) {
        WORD stripBottom = (ry2 < by1-1) ? ry2 : by1-1;
        EraseRect(rp, rx1, ry1, rx2, stripBottom);
    } else if (!bgBM) {
        /* No background image - erase entire rect */
        EraseRect(rp, rx1, ry1, rx2, ry2);
        return;
    }

    /* Bottom strip - below background */
    if (bgBM && ry2 > by2) {
        WORD stripTop = (ry1 > by2+1) ? ry1 : by2+1;
        EraseRect(rp, rx1, stripTop, rx2, ry2);
    }

    /* Left strip - to the left of background, within vertical overlap */
    if (bgBM && rx1 < bx1) {
        WORD ly1 = (ry1 > by1) ? ry1 : by1;
        WORD ly2 = (ry2 < by2) ? ry2 : by2;
        if (ly1 <= ly2) {
            WORD stripRight = (rx2 < bx1-1) ? rx2 : bx1-1;
            EraseRect(rp, rx1, ly1, stripRight, ly2);
        }
    }

    /* Right strip - to the right of background, within vertical overlap */
    if (bgBM && rx2 > bx2) {
        WORD ry1i = (ry1 > by1) ? ry1 : by1;
        WORD ry2i = (ry2 < by2) ? ry2 : by2;
        if (ry1i <= ry2i) {
            WORD stripLeft = (rx1 > bx2+1) ? rx1 : bx2+1;
            EraseRect(rp, stripLeft, ry1i, rx2, ry2i);
        }
    }
}

static void BlitSpriteFrame(UWORD frame, WORD dx, WORD dy){
    BlitSpriteFrameWithColorKey(frame, dx, dy);
}

/* ------------ Game ------------ */
static void InitBear(void) {
    bearX = bgX + 50;  /* Start well within the background */
    bearY = bgY + 50;  /* Make sure it's below UI_BAR_H */
    
    /* Ensure position is within collision bounds */
    if (bearY < UI_BAR_H + 10) bearY = UI_BAR_H + 10;
    
    bearVX = 2;
    animFrame = animTick = 0;
    state = ST_IDLE;
    talkTick = talkPhase = waveTick = feedTicks = 0;
    
    printf("Bear initialized at position (%d, %d)\n", bearX, bearY);
    
    /* Update BOB position if it exists */
    if (bearVS) {
        bearVS->X = bearX;
        bearVS->Y = bearY;
        bearVS->OldX = bearX;
        bearVS->OldY = bearY;
        printf("Updated BOB position to (%d, %d)\n", bearVS->X, bearVS->Y);
    }
}

/* Validate BOB data before drawing */
static BOOL ValidateBobData(void) {
    if (!bearVS || !bearBob) {
        printf("ERROR: BOB structures not allocated\n");
        return FALSE;
    }
    
    if (!bearVS->ImageData) {
        printf("ERROR: No ImageData in VSprite\n");
        return FALSE;
    }
    
    if (!bearBob->SaveBuffer) {
        printf("ERROR: No SaveBuffer in BOB\n");
        return FALSE;
    }
    
    if (bearVS->Width == 0 || bearVS->Height == 0) {
        printf("ERROR: Invalid BOB dimensions: %dx%d\n", bearVS->Width, bearVS->Height);
        return FALSE;
    }
    
    /* Check if position is within valid bounds */
    if (bearVS->X < 0 || bearVS->Y < UI_BAR_H || 
        bearVS->X >= WIN_W || bearVS->Y >= WIN_H) {
        printf("WARNING: BOB position (%d,%d) may be outside valid area\n", 
               bearVS->X, bearVS->Y);
        
        /* Clamp to valid area */
        if (bearVS->X < 10) bearVS->X = 10;
        if (bearVS->Y < UI_BAR_H + 10) bearVS->Y = UI_BAR_H + 10;
        if (bearVS->X >= WIN_W - 50) bearVS->X = WIN_W - 50;
        if (bearVS->Y >= WIN_H - 50) bearVS->Y = WIN_H - 50;
        
        printf("Clamped BOB position to (%d,%d)\n", bearVS->X, bearVS->Y);
    }
    
    printf("BOB validation passed: %dx%d at (%d,%d), ImageData=%p\n", 
           bearVS->Width, bearVS->Height, bearVS->X, bearVS->Y, bearVS->ImageData);
    
    return TRUE;
}

/* Safe size change with complete BOB recreation */
static BOOL ChangeBearSize(BOOL newDoubleSize) {
    if (doubleSize == newDoubleSize) return TRUE;
    
    printf("Changing bear size from %s to %s\n", 
           doubleSize ? "double" : "normal", 
           newDoubleSize ? "double" : "normal");
    
    /* Store old position */
    WORD oldX = bearX, oldY = bearY;
    
    /* Clean up current BOB system completely */
    if (bobSystemReady) {
        CloseBobSystem();
    }
    
    /* Update size flag */
    doubleSize = newDoubleSize;
    
    /* Reinitialize everything */
    if (!InitBobSystem()) {
        printf("BOB system re-init failed\n");
        return FALSE;
    }
    
    if (!CreateBobData()) {
        printf("BOB data re-creation failed\n");
        CloseBobSystem();
        return FALSE;
    }
    
    if (!CreateBearBob()) {
        printf("Bear BOB re-creation failed\n");
        CloseBobSystem();
        return FALSE;
    }
    
    /* Restore position with bounds checking */
    UWORD width = doubleSize ? (SPR_W * 2) : SPR_W;
    if (oldX > bgX + bgW - width) {
        bearX = bgX + bgW - width;
    } else {
        bearX = oldX;
    }
    bearY = oldY;
    
    /* Update BOB position */
    UpdateBobPosition(bearX, bearY);
    SetBobFrame(S_FRONT_C);
    
    printf("Size change completed successfully\n");
    return TRUE;
}

static void Tick(void) {
    /* Calculate bounce offset for feeding */
    WORD offY = 0;
    if (state == ST_FEED && feedTicks > 0) {
        if (feedTicks > 6) offY = -6;
        else if (feedTicks > 3) offY = -4;
        else offY = -2;
        feedTicks--;
        if (feedTicks == 0) state = resumeState;
    }
    
    /* Movement and behavior */
    switch(state) {
        case ST_IDLE:
            if ((animTick++ & 31) == 0) state = (bearVX > 0) ? ST_WALK_R : ST_WALK_L;
            break;
            
        case ST_WALK_R:
            bearX += 2;
            if (bearX >= bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W)) {
                bearX = bgX + bgW - (doubleSize ? SPR_W * 2 : SPR_W);
                bearVX = -2;
                state = ST_WALK_L;
            }
            if ((animTick++ & 63) == 0) {
                state = ST_WAVE;
                waveTick = 0;
            }
            break;
            
        case ST_WALK_L:
            bearX -= 2;
            if (bearX <= bgX) {
                bearX = bgX;
                bearVX = +2;
                state = ST_WALK_R;
            }
            if ((animTick++ & 63) == 0) {
                state = ST_WAVE;
                waveTick = 0;
            }
            break;
            
        case ST_WAVE:
            if (++waveTick > 12) {
                waveTick = 0;
                state = (bearVX > 0) ? ST_WALK_R : ST_WALK_L;
            }
            break;
            
        case ST_FEED:
            /* Position handled above */
            break;
    }
    
    /* Pick frame */
    UWORD frame = S_FRONT_C;
    if (speaking) {
        if (++talkTick >= 3) { talkTick = 0; talkPhase ^= 1; }
        frame = talkSeq[talkPhase];
    } else if (state == ST_WALK_R) {
        frame = walkRSeq[(animFrame++ & 3)];
    } else if (state == ST_WALK_L) {
        frame = walkLSeq[(animFrame++ & 3)];
    } else if (state == ST_WAVE) {
        frame = (waveTick & 1) ? S_WAVE : S_FRONT_C;
    }
    
   /* Update BOB */
    if (bobSystemReady && bearBob && bearVS) {
        SetBobFrame(frame);
        UpdateBobPosition(bearX, bearY + offY);
        DrawBobs();
    } else {
        /* Fallback to direct sprite drawing */
        DrawSpriteDirectly(frame, bearX, bearY + offY);
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
        .FirstGadget=NULL,.CheckMark=NULL,.Title=(UBYTE*)"AG Bear Tamagotchi (BOB Hardware - Fixed)",
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

    /* UI */
    CreateContext(&gadList);
    struct NewGadget ng; memset(&ng,0,sizeof(ng));
    ng.ng_TextAttr=scr->Font; ng.ng_VisualInfo=vi;

    WORD strW = win->Width - (UI_MARGIN_X*2 + 30); if(strW<160) strW=160;
    ng.ng_LeftEdge=UI_MARGIN_X; ng.ng_TopEdge=UI_MARGIN_Y; ng.ng_Width=strW; ng.ng_Height=UI_STR_H; ng.ng_GadgetID=1;
    txtGad=CreateGadget(STRING_KIND,gadList,&ng, GA_ID,1, GTST_MaxChars,200, GTST_String,(ULONG)"hello friend!", TAG_END);

    WORD y = UI_MARGIN_Y + UI_STR_H + UI_GAP_Y;
    WORD x = UI_MARGIN_X;
    ng.ng_LeftEdge=x; ng.ng_TopEdge=y; ng.ng_Width=UI_BTN_W; ng.ng_Height=UI_BTN_H; ng.ng_GadgetID=2; ng.ng_GadgetText=(STRPTR)"_Speak";
    sayBtn=CreateGadget(BUTTON_KIND,txtGad,&ng, GA_ID,2, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

    x += UI_BTN_W + UI_GAP_X;
    ng.ng_LeftEdge=x; ng.ng_GadgetID=3; ng.ng_GadgetText=(STRPTR)"_Feed";
    feedBtn=CreateGadget(BUTTON_KIND,sayBtn,&ng, GA_ID,3, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

    x += UI_BTN_W + UI_GAP_X;
    ng.ng_LeftEdge=x; ng.ng_GadgetID=4; ng.ng_GadgetText=(STRPTR)"_Stop";
    stopBtn=CreateGadget(BUTTON_KIND,feedBtn,&ng, GA_ID,4, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

     x += UI_BTN_W + UI_GAP_X;
    ng.ng_LeftEdge=x; ng.ng_GadgetID=5; ng.ng_GadgetText=(STRPTR)"Si_ze";
    sizeBtn=CreateGadget(BUTTON_KIND,stopBtn,&ng, GA_ID,5, GA_RelVerify,TRUE, GT_Underscore,'_', TAG_END);

    AddGList(win,gadList,(UWORD)-1,(UWORD)-1,NULL);
    RefreshGList(gadList,win,NULL,(UWORD)-1);
    GT_RefreshWindow(win,NULL);

    /* subsystems */
    if(!InitTimer()) SetWindowTitles(win,(UBYTE*)"Timer init failed",(UBYTE*)-1);
    (void)InitSpeech();
    (void)InitBearSounds();
    if (numSounds > 0) {
        PlayBearSound(SOUND_WAKEUP);  /* Bear says hello when program starts */
    }
    
    /* gfx */
    (void)LoadBG(BG_PATH);
    (void)LoadSpriteSheet(SPR_PATH);
    
    /* Initialize BOB system */
    if (!InitBobSystem()) {
        printf("BOB system init failed - using fallback\n");
        bobSystemReady = FALSE;
    } else {
        if (!CreateBobData()) {
            printf("BOB data creation failed - using fallback\n");
            CloseBobSystem();
            bobSystemReady = FALSE;
        } else if (!CreateBearBob()) {
            printf("Bear BOB creation failed - using fallback\n");
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
            static int tickCount = 0;
            while((m=GetMsg(tmPort))){ 
                tickCount++;
                Tick(); 
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
                        feedTicks = 12;
                        if (state!=ST_FEED){ 
                            resumeState = (bearVX>0)?ST_WALK_R:ST_WALK_L; 
                            state=ST_FEED; 
                        }
                        PlayBearSound(SOUND_NOM);
                        if ((rand() % 3) == 0) {
                            /* Sometimes make happy sound after eating */
                        }
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
                    } else if (gid==5) {  /* Size button - FIXED */
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
                            
                            printf("Size changed to %s mode\n", doubleSize ? "double" : "normal");
                        } else {
                            printf("Size change failed - continuing with current size\n");
                        }
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
        
        /* Happy sounds after eating */
        if (state == ST_FEED && feedTicks > 0 && (rand() % 200) == 0) {
            PlayBearSound(SOUND_HAPPY);
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

    /* FIXED SHUTDOWN SEQUENCE - stop everything first, then cleanup */
    printf("Shutting down...\n");
    
    /* Stop all active operations */
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
    
    /* Clean up UI first */
    if (gadList && win) {
        RemoveGList(win, gadList, -1);
        FreeGadgets(gadList);
        gadList = NULL;
    }
    
    /* Clean up window */
    if (win) {
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
    
    printf("Shutdown complete\n");
    return 0;
}