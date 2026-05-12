/* ag-bear-bob.c — AG Bear Tamagotchi (BOB, OS3) — stable feed/stop/close
 * Sprite sheet: PROGDIR:bear32.ilbm (48x48 tiles; 4x2 at top-left)
 * Layout:
 *   Row0: FRONT_C, FRONT_O, WAVE, BACK
 *   Row1: WALK_R0, WALK_R1, WALK_L0, WALK_L1
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
#include <graphics/gfx.h>
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
#define MAX_SOUND_SIZE 32768  /* 32KB max per sound */

/* sprite sheet: 48x48 tiles, 4x2 grid at (0,0) of a 320x200 picture */
#define SPR_W 48
#define SPR_H 48
#define SPR_COLS 4
#define SPR_ROWS 2
#define SPR_ORIGIN_X 0
#define SPR_ORIGIN_Y 0

#define TICK_MS 80  /* ~12.5 FPS */

/* ------------ Globals ------------ */
static struct Window *win=NULL;
static struct RastPort *rp=NULL;
static APTR vi=NULL;

extern struct Library *GadToolsBase;
struct Library *DataTypesBase=NULL;
struct Library *TranslatorBase=NULL;
static BOOL doubleSize = FALSE;  /* Toggle for 2x sprite size */
#define BEAR_W (doubleSize ? (SPR_W * 2) : SPR_W)
#define BEAR_H (doubleSize ? (SPR_H * 2) : SPR_H)
static struct Gadget *sizeBtn = NULL;  /* New button gadget */
static struct MsgPort *tmPort=NULL;
static struct timerequest *tmReq=NULL;

static struct MsgPort *narrPort=NULL;
static struct narrator_rb *voiceIO=NULL;

static struct Gadget *gadList=NULL,*txtGad=NULL,*sayBtn=NULL,*feedBtn=NULL,*stopBtn=NULL;

static const UBYTE kAudChanMasks[] = {3,12};

struct BearSound {
    UBYTE *data;        /* Sample data */
    ULONG length;       /* Length in bytes */
    UWORD period;       /* Playback period (frequency) */
    char name[32];      /* For debugging */
};

static struct BearSound bearSounds[MAX_SOUNDS];
static UWORD numSounds = 0;
static struct MsgPort *audioPort = NULL;
static struct IOAudio *audioReq = NULL;
static UBYTE audioChannels[] = {1, 2, 4, 8}; /* Left, Right, Left rear, Right rear */
static BOOL audioPlaying = FALSE;

/* Sound file paths - your actual AG Bear sound files */
static const char *soundFiles[] = {
    "PROGDIR:sounds/greeting1.iff",
    "PROGDIR:sounds/greeting2.iff", 
    "PROGDIR:sounds/growl1.iff",
    "PROGDIR:sounds/growl2.iff",
    "PROGDIR:sounds/happy1.iff",
    "PROGDIR:sounds/happy2.iff",
    "PROGDIR:sounds/happy3.iff",
    "PROGDIR:sounds/hi.iff",
    "PROGDIR:sounds/no.iff",
    "PROGDIR:sounds/no2.iff",
    "PROGDIR:sounds/nom1.iff",
    "PROGDIR:sounds/nom2.iff",
    "PROGDIR:sounds/wakeup.iff",
    "PROGDIR:sounds/wave.iff",
    "PROGDIR:sounds/yes.iff",
    NULL  /* Terminator */
};

/* Sound categories for different situations */
typedef enum {
    SOUND_GROWL,     /* Idle/walking sounds */
    SOUND_HAPPY,     /* When fed or content */
    SOUND_NOM,       /* Eating sounds */
    SOUND_GREETING,  /* When spoken to or greeting */
    SOUND_WAVE,      /* When waving */
    SOUND_AGREEMENT, /* Yes/positive responses */
    SOUND_DISAGREE,  /* No/negative responses */
    SOUND_WAKEUP,    /* When starting up or becoming active */
    SOUND_RANDOM     /* Any sound for ambient chatter */
} SoundType;

/* Map sound indices to categories */
static struct {
    UWORD soundIndex;
    SoundType type;
} soundMap[] = {
    {0, SOUND_GREETING},  /* greeting1 */
    {1, SOUND_GREETING},  /* greeting2 */
    {2, SOUND_GROWL},     /* growl1 */
    {3, SOUND_GROWL},     /* growl2 */
    {4, SOUND_HAPPY},     /* happy1 */
    {5, SOUND_HAPPY},     /* happy2 */
    {6, SOUND_HAPPY},     /* happy3 */
    {7, SOUND_GREETING},  /* hi */
    {8, SOUND_DISAGREE},  /* no */
    {9, SOUND_DISAGREE},  /* no2 */
    {10, SOUND_NOM},      /* nom1 */
    {11, SOUND_NOM},      /* nom2 */
    {12, SOUND_WAKEUP},   /* wakeup */
    {13, SOUND_WAVE},     /* wave */
    {14, SOUND_AGREEMENT}, /* yes */
    {0, SOUND_RANDOM}     /* Terminator */
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
static UBYTE *sprMask=NULL,*sprMaskOwned=NULL;

static UBYTE *tileMask[SPR_COLS*SPR_ROWS] = {0};
static UWORD  tileMaskBpr = ((SPR_W + 15) >> 4) << 1;  /* = 6 for 48px */

/* Bear state */
typedef enum { ST_IDLE, ST_WALK_R, ST_WALK_L, ST_WAVE, ST_FEED } BearState;
static BearState state=ST_IDLE;
static WORD bearX, bearY, bearVX;
static UWORD animFrame=0, animTick=0;
static BOOL speaking=FALSE;

/* drawn position for correct restore (so feed-bounce doesn’t smear) */
static WORD prevDrawX=-1, prevDrawY=-1;

/* talk/wave/feed anim helpers */
static UWORD talkTick=0, talkPhase=0;
static UWORD waveTick=0;
static UWORD feedTicks=0;    /* >0 while bouncing */
static BearState resumeState=ST_IDLE;

/* Frame indices */
enum { S_FRONT_C=0, S_FRONT_O=1, S_WAVE=2, S_BACK=3,
       S_WR0=4, S_WR1=5, S_WL0=6, S_WL1=7 };
static const UWORD walkRSeq[4]={S_WR0,S_WR1,S_WR0,S_WR1};
static const UWORD walkLSeq[4]={S_WL0,S_WL1,S_WL0,S_WL1};
static const UWORD talkSeq[2] ={S_FRONT_O,S_FRONT_C};

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

/* Random chatter - call this occasionally during idle time */
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
    // Go back to using screen mapping for correct colors
    sprObj=NewDTObject((STRPTR)path, 
                       DTA_SourceType, DTST_FILE, 
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Screen, (ULONG)win->WScreen,  // Back to screen mapping
                       TAG_END);
    if(!sprObj) return FALSE;
    
    DoDTMethod(sprObj,0L,0L,DTM_PROCLAYOUT,0L,1L);
    GetDTAttrs(sprObj, 
               PDTA_BitMapHeader, (ULONG)&sprBMHD, 
               PDTA_BitMap, (ULONG)&sprBM,
               PDTA_MaskPlane, (ULONG)&sprMask, 
               TAG_END);
    if(!sprBMHD||!sprBM){ DisposeDTObject(sprObj); sprObj=NULL; return FALSE; }
    
    // Don't build our own mask - we'll handle transparency differently
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

/* Update bear position constraints for double size mode */
static void UpdateBearConstraints(void) {
    /* Make sure bear doesn't walk outside visible area in double size mode */
    if (doubleSize) {
        if (bearX > bgX + bgW - BEAR_W) bearX = bgX + bgW - BEAR_W;
        if (bearX < bgX) bearX = bgX;
        
        /* Also adjust Y position to keep bear fully visible */
        if (bearY > bgY + bgH - BEAR_H) bearY = bgY + bgH - BEAR_H;
    }
}

static void FreeSpriteSheet(void){
    if(sprObj){ DisposeDTObject(sprObj); sprObj=NULL; }
    if(sprMaskOwned){ FreeVec(sprMaskOwned); sprMaskOwned=NULL; }
    sprBMHD=NULL; sprBM=NULL; sprMask=NULL;
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
    /* We need to erase ALL parts of R that don't overlap with B */
    
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
static UBYTE *CreateSpriteMask(struct BitMap *bm, WORD sx, WORD sy, WORD sw, WORD sh) {
    if(!bm || sw <= 0 || sh <= 0) return NULL;
    
    // IMPORTANT: Amiga masks need to match the source bitmap's bytes-per-row alignment
    // For 48 pixels: (48 + 15) / 16 * 2 = 6 bytes per row
    UWORD bprMask = ((sw + 15) >> 4) << 1;
    UBYTE *mask = (UBYTE*)AllocVec((ULONG)bprMask * sh, MEMF_CLEAR);
    if(!mask) return NULL;
    
    ULONG transparentCount = 0, opaqueCount = 0;
    
    for(WORD y = 0; y < sh; y++) {
        UBYTE *mrow = mask + (ULONG)bprMask * y;
        for(WORD x = 0; x < sw; x++) {
            // Read from source bitmap at sx+x, sy+y
            WORD srcX = sx + x;
            WORD srcY = sy + y;
            
            if(srcX >= 0 && srcY >= 0 && srcX < 320 && srcY < 200) {
                UBYTE planeBit = (UBYTE)(0x80 >> (srcX & 7)), idx = 0;
                
                for(int p = 0; p < bm->Depth; p++) {
                    UBYTE *pp = (UBYTE*)bm->Planes[p] + (ULONG)bm->BytesPerRow * srcY + (srcX >> 3);
                    if((*pp) & planeBit) idx |= (1U << p);
                }
                
                if(idx != 0) {
                    mrow[x >> 3] |= (0x80 >> (x & 7));
                    opaqueCount++;
                } else {
                    transparentCount++;
                }
            } else {
                transparentCount++; // out of bounds = transparent
            }
        }
    }
    
  return mask;
}

static void BlitSpriteFrame(UWORD frame, WORD dx, WORD dy){
    BlitSpriteFrameWithColorKey(frame, dx, dy);
}
/* ------------ Game ------------ */
static void InitBear(void){
    bearX = bgX + 10;
    bearY = bgY + (bgH - SPR_H) - 6;
    bearVX = 2;
    animFrame=animTick=0;
    state=ST_IDLE;
    talkTick=talkPhase=waveTick=feedTicks=0;
    prevDrawX=prevDrawY=-1;
}

static void Tick(void){
    /* compute bounce (do NOT mutate bearY to avoid trails) */
    WORD offY = 0;
    if (state==ST_FEED){
        /* simple bounce: 12 ticks total, higher first half */
        if (feedTicks > 0){
            if (feedTicks > 6) offY = -6;
            else if (feedTicks > 3) offY = -4;
            else offY = -2;
            feedTicks--;
            if (feedTicks == 0) state = resumeState;
        }
    }

    /* restore last drawn rect */
    if (prevDrawX>=0) RestoreBGRect(prevDrawX, prevDrawY, BEAR_W, BEAR_H);
    if (!speaking && !audioPlaying) {
        RandomBearChatter();
    }
    /* movement / behaviour (don’t touch bearY during feed bounce) */
    switch(state){
        case ST_IDLE:
            if ((animTick++ & 31)==0) state = (bearVX>0)?ST_WALK_R:ST_WALK_L;
            break;
        case ST_WALK_R:
                    bearX += 2;
                    if (bearX >= bgX + bgW - BEAR_W) { 
                        bearX = bgX + bgW - BEAR_W; 
                        bearVX = -2; 
                        state = ST_WALK_L; 
                        if ((rand() % 8) == 0) PlayBearSound(SOUND_GROWL);
                    }
                    if ((animTick++ & 63)==0) { 
                        state = ST_WAVE; 
                        waveTick=0; 
                        PlayBearSound(SOUND_WAVE);
                    }
                    break;
        case ST_WALK_L:
                        bearX -= 2;
                        if (bearX <= bgX){ 
                            bearX = bgX; 
                            bearVX = +2; 
                            state = ST_WALK_R; 
                            if ((rand() % 8) == 0) PlayBearSound(SOUND_GROWL);
                        }
                        if ((animTick++ & 63)==0) { 
                            state = ST_WAVE; 
                            waveTick=0; 
                            PlayBearSound(SOUND_WAVE);
                        }
                        break; 
        case ST_WAVE:
                    if (++waveTick > 12) { 
                        waveTick=0; 
                        state = (bearVX>0)?ST_WALK_R:ST_WALK_L; 
                        /* Sometimes make a happy sound after waving */
                        if ((rand() % 4) == 0) PlayBearSound(SOUND_HAPPY);
                    }
                    break;
        case ST_FEED:
            /* position handled above; resume when feedTicks==0 */
            break;
    }

    /* pick frame */
    UWORD frame = S_FRONT_C;
    if (speaking){
        if (++talkTick >= 3){ talkTick=0; talkPhase ^= 1; }
        frame = talkSeq[talkPhase];
    } else if (state==ST_WALK_R){
        frame = walkRSeq[(animFrame++ & 3)];
    } else if (state==ST_WALK_L){
        frame = walkLSeq[(animFrame++ & 3)];
    } else if (state==ST_WAVE){
        frame = (waveTick & 1) ? S_WAVE : S_FRONT_C;
    } else {
        frame = S_FRONT_C;
    }

    /* draw at current draw coords (with bounce offset) */
    WORD drawX = bearX;
    WORD drawY = bearY + offY;
    BlitSpriteFrame(frame, drawX, drawY);

    prevDrawX = drawX;
    prevDrawY = drawY;
}

/* ------------ Main ------------ */
int main(void){
    struct Screen *scr = LockPubScreen(NULL); if(!scr) return 20;
    GadToolsBase = OpenLibrary("gadtools.library",37);
    DataTypesBase = OpenLibrary("datatypes.library",39);
    if(!GadToolsBase||!DataTypesBase){ if(GadToolsBase) CloseLibrary(GadToolsBase); if(DataTypesBase) CloseLibrary(DataTypesBase); UnlockPubScreen(NULL,scr); return 20; }

    vi = GetVisualInfoA(scr,NULL);
    struct NewWindow nw = {
        .LeftEdge=40,.TopEdge=32,.Width=WIN_W,.Height=WIN_H,
        .DetailPen=0,.BlockPen=1,
        .IDCMPFlags=IDCMP_GADGETUP|IDCMP_REFRESHWINDOW|IDCMP_CLOSEWINDOW|IDCMP_IDCMPUPDATE,
        .Flags=WFLG_SMART_REFRESH|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_ACTIVATE,
        .FirstGadget=NULL,.CheckMark=NULL,.Title=(UBYTE*)"AG Bear Tamagotchi (BOB)",
        .Screen=NULL,.BitMap=NULL,.MinWidth=WIN_W,.MinHeight=WIN_H,.MaxWidth=WIN_W,.MaxHeight=WIN_H,
        .Type=WBENCHSCREEN
    };
    win = OpenWindow(&nw); if(!win){ FreeVisualInfo(vi); CloseLibrary(DataTypesBase); CloseLibrary(GadToolsBase); UnlockPubScreen(NULL,scr); return 10; }
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
    InitBear();
    if(bgBM){
        if (bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
        else        BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
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
            struct Message *m; while((m=GetMsg(tmPort))){ Tick(); }
            ArmTimerMS(TICK_MS);
        }

        if (sigs & winSig){
            struct IntuiMessage *imsg;
            while((imsg=GT_GetIMsg(win->UserPort))){
                ULONG cls=imsg->Class;
                UWORD gid = imsg->IAddress ? ((struct Gadget*)imsg->IAddress)->GadgetID : 0;

                if (cls==IDCMP_CLOSEWINDOW){
                    running=FALSE; /* break after processing */
                } else if (cls==IDCMP_GADGETUP){
                    if (gid==2){
                                STRPTR s=NULL; GT_GetGadgetAttrs(txtGad,win,NULL,GTST_String,(ULONG)&s,TAG_END);
                                    Speak((const char*)s);
                                    RespondToUserText((const char*)s);  /* Smart response based on text */
                                    state=ST_IDLE;
                                } else if (gid==3){  /* Feed button */
                                    feedTicks = 12;
                                    if (state!=ST_FEED){ resumeState = (bearVX>0)?ST_WALK_R:ST_WALK_L; state=ST_FEED; }
                                    //Speak("nom nom");
                                    PlayBearSound(SOUND_NOM);  /* Happy eating sound */
                                    /* Sometimes add a happy sound after eating */
                                    if ((rand() % 3) == 0) {
                                        /* We'll play this after the nom sound finishes */
                                        /* For now, just the nom sound */
                                    }
                                } else if (gid==4) {  /* Stop button */
                                    StopSpeech();
                                    /* Stop audio too */
                                    if (audioReq && audioPlaying) {
                                        AbortIO((struct IORequest*)audioReq);
                                        WaitIO((struct IORequest*)audioReq);
                                        audioPlaying = FALSE;
                                    }
                                    PlayBearSound(SOUND_DISAGREE);  /* Bear doesn't like being stopped! */
                                    speaking = FALSE;
                                    talkTick = talkPhase = 0;
                                    if (prevDrawX >= 0) RestoreBGRect(prevDrawX, prevDrawY, BEAR_W, BEAR_H);
                                    BlitSpriteFrame(S_FRONT_C, bearX, bearY);
                                    prevDrawX = bearX; prevDrawY = bearY;
                                } else if (gid==5) {  /* Size button */
                                            doubleSize = !doubleSize;  /* Toggle size mode */
                                            
                                            /* Force immediate redraw at new size */
                                            if (prevDrawX >= 0) RestoreBGRect(prevDrawX, prevDrawY, 
                                                                            doubleSize ? SPR_W : BEAR_W, 
                                                                            doubleSize ? SPR_H : BEAR_H);
                                            
                                            /* Update bear position constraints */
                                            UpdateBearConstraints();
                                            
                                            /* Redraw bear at current position with new size */
                                            WORD drawX = bearX;
                                            WORD drawY = bearY + (state == ST_FEED && feedTicks > 0 ? 
                                                                (feedTicks > 6 ? -6 : feedTicks > 3 ? -4 : -2) : 0);
                                            BlitSpriteFrame(speaking ? (talkPhase ? S_FRONT_O : S_FRONT_C) : S_FRONT_C, 
                                                        drawX, drawY);
                                            prevDrawX = drawX; 
                                            prevDrawY = drawY;
                                            
                                            /* Update button text to show current mode */
                                            GT_SetGadgetAttrs(sizeBtn, win, NULL, 
                                                            GA_Text, doubleSize ? (ULONG)"1x" : (ULONG)"2x", 
                                                            TAG_END);
                                            
                                            printf("Switched to %s mode\n", doubleSize ? "double size" : "normal size");  } 
                } else if (cls==IDCMP_REFRESHWINDOW){
                    GT_BeginRefresh(win);
                    if(bgBM){
                        if (bgMask) BltMaskBitMapRastPort(bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0,bgMask);
                        else        BltBitMapRastPort   (bgBM,0,0,rp,bgX,bgY,bgW,bgH,0xC0);
                    }
                    prevDrawX=prevDrawY=-1; Tick();
                    GT_EndRefresh(win,TRUE);
                }
                GT_ReplyIMsg(imsg);
            }
        }

        if (audioReq && audioPlaying && CheckIO((struct IORequest*)audioReq)){
            WaitIO((struct IORequest*)audioReq);
            audioPlaying = FALSE; }
        if (state == ST_FEED && feedTicks > 0 && (rand() % 2) == 0) {
        PlayBearSound(SOUND_HAPPY);  /* Content after eating */
        }

        if (voiceIO && speaking && CheckIO((struct IORequest*)voiceIO)){
            WaitIO((struct IORequest*)voiceIO);
            if(voiceIO->message.io_Data){ FreeVec((APTR)voiceIO->message.io_Data); voiceIO->message.io_Data=NULL; }
            speaking=FALSE;
        }

        
    }

    /* shutdown — stop timers & speech before closing libraries */
    StopSpeech();
    CloseTimer();

    if (gadList) RemoveGList(win,gadList,-1);
    if (gadList) FreeGadgets(gadList);
    CloseWindow(win);
    CloseBearSounds();
    CloseSpeech();
    FreeSpriteSheet();
    FreeBG();
    if (vi) FreeVisualInfo(vi);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    UnlockPubScreen(NULL,scr);
    return 0;
}
