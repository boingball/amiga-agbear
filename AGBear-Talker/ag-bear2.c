/* ag-bear.c — AG Bear Talker with photo background + mouth overlays (OS3)
 * - Background: PROGDIR:agbear.ilbm   (no mouth drawn in)
 * - Overlays  : PROGDIR:mouth_closed.ilbm, PROGDIR:mouth_open.ilbm
 * - narrator.device + translator.library (async)
 * - GadTools UI (STRING, BUTTON, CYCLE) + fixed close gadget + spacing
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/narrator.h>
#include <devices/timer.h>
#include <libraries/translator.h>

#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>
#include <utility/tagitem.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/dos.h>
#include <proto/translator.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/datatypes.h>

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Some NDKs don’t prototype this */
extern void BeginIO(struct IORequest *);

/* ---- UI constants ---- */
#define MARGIN_X   10
#define MARGIN_Y   14
#define STR_H      16
#define BTN_W      90
#define BTN_H      16
#define CYC_W     160
#define GAP_X      10
#define GAP_Y       8
#define UI_BAR_H   (MARGIN_Y + STR_H + GAP_Y + BTN_H + MARGIN_Y)
#define TICK_MS    60

/* ---- Picture + mouth placement ---- */
#define PIC_PATH            "PROGDIR:agbear.ilbm"
#define MOUTH_CLOSED_PATH   "PROGDIR:mouth_closed.ilbm"
#define MOUTH_OPEN_PATH     "PROGDIR:mouth_open.ilbm"

/* Mouth anchor (top-left) relative to picture’s top-left. Tweak for your art. */
#define MOUTH_OFF_X 72
#define MOUTH_OFF_Y 162

/* Fallback mouth rect size if overlays are missing */
#define FB_MOUTH_W 64
#define FB_MOUTH_H 34

/* Palette from the mouth ILBMs (provided by picture.datatype) */
static struct ColorRegister *mouthClosedCRegs = NULL, *mouthOpenCRegs = NULL;
static ULONG mouthClosedNumCols = 0, mouthOpenNumCols = 0;

/* Masks we synthesize and own (so we can free them) */
static UBYTE *mouthClosedMaskOwned = NULL;
static UBYTE *mouthOpenMaskOwned   = NULL;

/* Bounding box for restore (covers both frames) */
static WORD mouthBBoxW = 0, mouthBBoxH = 0;

/* ---------- Globals ---------- */
static struct Window *win;
static struct RastPort *rp;
static APTR vi = NULL;

extern struct Library *TranslatorBase;
extern struct Library *GadToolsBase;
struct Library *DataTypesBase = NULL;

static struct MsgPort     *narrPort  = NULL;
static struct narrator_rb *voiceIO   = NULL;

static struct MsgPort     *tmPort    = NULL;
static struct timerequest *tmReq     = NULL;

static struct Gadget *gadList=NULL;
static struct Gadget *txtGad=NULL, *sayBtn=NULL, *modeCyc=NULL, *stopBtn=NULL;

typedef enum { MODE_NORMAL=0, MODE_BEAR=1, MODE_CHIP=2 } VoiceMode;
static VoiceMode mode = MODE_BEAR;

static BOOL  speaking   = FALSE;
static UWORD mouthFrame = 0;

/* Prefer stereo pair 0+1, fallback 2+3 */
static const UBYTE kAudChanMasks[] = { 3, 12 };

/* ---------- Background picture via datatypes ---------- */
static Object *picObj = NULL;
static struct BitMapHeader *picBMHD = NULL;
static struct BitMap *picBM = NULL;
static UBYTE *picMask = NULL;
static WORD picX = 0, picY = 0;
static BOOL picDrawnOnce = FALSE;

/* ---------- Mouth overlays (closed/open) ---------- */
static Object *mouthClosedObj = NULL, *mouthOpenObj = NULL;
static struct BitMapHeader *mouthClosedBMHD = NULL, *mouthOpenBMHD = NULL;
static struct BitMap *mouthClosedBM = NULL, *mouthOpenBM = NULL;
static UBYTE *mouthClosedMask = NULL, *mouthOpenMask = NULL;

/* ---------- AG syllable helpers ---------- */
static int is_vowel(char c){ c=(char)tolower((unsigned char)c); return (c=='a'||c=='e'||c=='i'||c=='o'||c=='u'||c=='y'); }
static int has_char(const char* s,char ch){ ch=(char)tolower((unsigned char)ch); for(;*s;++s) if ((char)tolower((unsigned char)*s)==ch) return 1; return 0; }
static int syllables_of(const char* w){ int syl=0,in=0; for(;*w;++w){ if(is_vowel(*w)){ if(!in){++syl;in=1;} } else in=0; } if(syl<1)syl=1; if(syl>4)syl=4; return syl; }
static void build_ag_syllable(const char* word,char* out,size_t cap){
    int syl=syllables_of(word), stretch=syl+1; if(stretch<2)stretch=2; if(stretch>5)stretch=5;
    int growly=has_char(word,'r')||has_char(word,'l'); int has_o=has_char(word,'o')||has_char(word,'u'); int has_a=has_char(word,'a');
    char vowel=has_o?'o':has_a?'a':'e'; size_t o=0;
    if(growly){ if(o<cap)out[o++]='r'; if(o<cap)out[o++]='a'; if(o<cap)out[o++]='r'; for(int i=0;i<stretch && o<cap;i++) out[o++]='r'; if(o<cap)out[o++]=' '; if(o<cap)out[o]=0; return; }
    if(o<cap)out[o++]='w'; for(int i=0;i<stretch && o<cap;i++) out[o++]=vowel; if(o<cap)out[o++]='w'; if(o<cap)out[o++]=' '; if(o<cap)out[o]=0;
}
static char* Bearify(const char* in){
    if(!in||!*in) return NULL; size_t n=strlen(in), cap=n*6+32; char* out=(char*)AllocVec(cap, MEMF_ANY); if(!out) return NULL; size_t o=0;
    for(size_t i=0;i<=n;){
        while(i<n && (in[i]==' '||in[i]=='\t'||in[i]=='\n')){ if(o+1<cap) out[o++]=' '; i++; }
        size_t s=i; while(i<n && !(in[i]==' '||in[i]=='\t'||in[i]=='\n')) i++; size_t e=i;
        if(e>s){ char word[64]; size_t wl=e-s; if(wl>63) wl=63; memcpy(word,in+s,wl); word[wl]=0; char syl[32]; syl[0]=0; build_ag_syllable(word,syl,sizeof(syl));
                 size_t sl=strlen(syl); if(o+sl+1>=cap) break; memcpy(out+o,syl,sl); o+=sl; }
        if(i>=n) break;
    }
    if(o==0){ FreeVec(out); return NULL; } out[o]=0; return out;
}

/* ---------- Picture loading/drawing ---------- */
static BOOL LoadPicture(const char *path){
    if(!DataTypesBase || !win) return FALSE;
    picObj = NewDTObject((STRPTR)path,
                         DTA_SourceType, DTST_FILE,
                         DTA_GroupID,   GID_PICTURE,
                         PDTA_Screen,   (ULONG)win->WScreen,
                         TAG_END);
    if(!picObj) return FALSE;

    DoDTMethod(picObj, 0L, 0L, DTM_PROCLAYOUT, 0L, 1L);

    GetDTAttrs(picObj,
               PDTA_BitMapHeader, (ULONG)&picBMHD,
               PDTA_BitMap,       (ULONG)&picBM,
               PDTA_MaskPlane,    (ULONG)&picMask,
               TAG_END);
    if(!picBMHD || !picBM){
        DisposeDTObject(picObj); picObj=NULL; picBMHD=NULL; picBM=NULL; picMask=NULL; return FALSE;
    }

    /* Center below UI bar */
    picX = (win->Width  - (WORD)picBMHD->bmh_Width)/2;
    picY = UI_BAR_H + 10;

    /* First full draw */
    if(picMask){
        BltMaskBitMapRastPort(picBM, 0, 0, rp, picX, picY,
                              picBMHD->bmh_Width, picBMHD->bmh_Height, 0xC0, picMask);
    } else {
        BltBitMapRastPort(picBM, 0, 0, rp, picX, picY,
                          picBMHD->bmh_Width, picBMHD->bmh_Height, 0xC0);
    }
    picDrawnOnce = TRUE;
    return TRUE;
}
static void FreePicture(void){
    if(picObj){ DisposeDTObject(picObj); picObj=NULL; }
    picBMHD=NULL; picBM=NULL; picMask=NULL; picDrawnOnce=FALSE;
}

/* Pick the palette index that is "most white" (highest R+G+B) */
static WORD FindWhiteIndex(struct ColorRegister *regs, ULONG n)
{
    if (!regs || n == 0) return -1;
    LONG bestScore = -1; WORD best = -1;
    for (ULONG i = 0; i < n; i++) {
        LONG s = regs[i].red + regs[i].green + regs[i].blue; /* 0..255 each */
        if (s > bestScore) { bestScore = s; best = (WORD)i; }
    }
    return best;
}

/* Build a 1-bit mask where palette index != whiteIndex is opaque */
static UBYTE *BuildMaskByIndex(struct BitMap *bm, WORD w, WORD h, WORD whiteIndex)
{
    if (!bm || w <= 0 || h <= 0 || whiteIndex < 0) return NULL;
    UWORD maskBpr = ((w + 15) >> 4) << 1; /* word-aligned bytes/row */
    UBYTE *mask = (UBYTE*)AllocVec((ULONG)maskBpr * h, MEMF_CLEAR);
    if (!mask) return NULL;

    for (WORD y = 0; y < h; y++) {
        UBYTE *row = mask + (ULONG)maskBpr * y;
        for (WORD x = 0; x < w; x++) {
            UBYTE planeBit = (UBYTE)(0x80 >> (x & 7));
            UBYTE idx = 0;
            for (int p = 0; p < bm->Depth; p++) {
                UBYTE *pp = (UBYTE*)bm->Planes[p] + (ULONG)bm->BytesPerRow * y + (x >> 3);
                if ((*pp) & planeBit) idx |= (1U << p);
            }
            if (idx != (UBYTE)whiteIndex) row[x >> 3] |= (0x80 >> (x & 7)); /* opaque */
        }
    }
    return mask;
}

static void CacheMouthBBox(void)
{
    WORD cw = mouthClosedBMHD ? mouthClosedBMHD->bmh_Width  : 0;
    WORD ch = mouthClosedBMHD ? mouthClosedBMHD->bmh_Height : 0;
    WORD ow = mouthOpenBMHD   ? mouthOpenBMHD->bmh_Width    : 0;
    WORD oh = mouthOpenBMHD   ? mouthOpenBMHD->bmh_Height   : 0;
    mouthBBoxW = (cw > ow ? cw : ow); if (!mouthBBoxW) mouthBBoxW = FB_MOUTH_W;
    mouthBBoxH = (ch > oh ? ch : oh); if (!mouthBBoxH) mouthBBoxH = FB_MOUTH_H;
}

/* If the ILBMs didn’t include a mask plane, synthesize one treating WHITE as transparent */
static void EnsureMouthMasksFromWhite(void)
{
    if (!mouthClosedMask && mouthClosedBMHD && mouthClosedBM) {
        WORD widx = FindWhiteIndex(mouthClosedCRegs, mouthClosedNumCols);
        mouthClosedMaskOwned = BuildMaskByIndex(mouthClosedBM,
                                               mouthClosedBMHD->bmh_Width,
                                               mouthClosedBMHD->bmh_Height,
                                               widx);
        if (mouthClosedMaskOwned) mouthClosedMask = mouthClosedMaskOwned;
    }
    if (!mouthOpenMask && mouthOpenBMHD && mouthOpenBM) {
        WORD widx = FindWhiteIndex(mouthOpenCRegs, mouthOpenNumCols);
        mouthOpenMaskOwned = BuildMaskByIndex(mouthOpenBM,
                                             mouthOpenBMHD->bmh_Width,
                                             mouthOpenBMHD->bmh_Height,
                                             widx);
        if (mouthOpenMaskOwned) mouthOpenMask = mouthOpenMaskOwned;
    }
}


/* ---------- Mouth overlays ---------- */
static BOOL LoadMouthBitmaps(void){
    if(!DataTypesBase || !win) return FALSE;

    mouthClosedObj = NewDTObject((STRPTR)MOUTH_CLOSED_PATH,
                                 DTA_SourceType, DTST_FILE,
                                 DTA_GroupID,   GID_PICTURE,
                                 PDTA_Screen,   (ULONG)win->WScreen,
                                 TAG_END);
    mouthOpenObj = NewDTObject((STRPTR)MOUTH_OPEN_PATH,
                               DTA_SourceType, DTST_FILE,
                               DTA_GroupID,   GID_PICTURE,
                               PDTA_Screen,   (ULONG)win->WScreen,
                               TAG_END);

    if (mouthClosedObj) DoDTMethod(mouthClosedObj, 0L, 0L, DTM_PROCLAYOUT, 0L, 1L);
    if (mouthOpenObj)   DoDTMethod(mouthOpenObj,   0L, 0L, DTM_PROCLAYOUT, 0L, 1L);

    if (mouthClosedObj) GetDTAttrs(mouthClosedObj,
        PDTA_BitMapHeader, (ULONG)&mouthClosedBMHD,
        PDTA_BitMap,       (ULONG)&mouthClosedBM,
        PDTA_MaskPlane,    (ULONG)&mouthClosedMask,
        PDTA_CRegs,        (ULONG)&mouthClosedCRegs,   /* NEW */
        PDTA_NumColors,    (ULONG)&mouthClosedNumCols, /* NEW */
        TAG_END);

    if (mouthOpenObj) GetDTAttrs(mouthOpenObj,
        PDTA_BitMapHeader, (ULONG)&mouthOpenBMHD,
        PDTA_BitMap,       (ULONG)&mouthOpenBM,
        PDTA_MaskPlane,    (ULONG)&mouthOpenMask,
        PDTA_CRegs,        (ULONG)&mouthOpenCRegs,     /* NEW */
        PDTA_NumColors,    (ULONG)&mouthOpenNumCols,   /* NEW */
        TAG_END);

    return (mouthClosedBM && mouthOpenBM);
}
static void FreeMouthBitmaps(void){
    if(mouthClosedObj){ DisposeDTObject(mouthClosedObj); mouthClosedObj=NULL; }
    if(mouthOpenObj){   DisposeDTObject(mouthOpenObj);   mouthOpenObj=NULL; }
    if (mouthClosedMaskOwned) { FreeVec(mouthClosedMaskOwned); mouthClosedMaskOwned = NULL; }
    if (mouthOpenMaskOwned)   { FreeVec(mouthOpenMaskOwned);   mouthOpenMaskOwned   = NULL; }   
    mouthClosedBMHD = mouthOpenBMHD = NULL;
    mouthClosedBM   = mouthOpenBM   = NULL;
    mouthClosedMask = mouthOpenMask = NULL;
}

/* Restore background under mouth, then blit CLOSED or OPEN overlay */
static void DrawMouthOverlay(UWORD isOpen){
    if(!picBMHD || !picBM) return;

    WORD dx = picX + MOUTH_OFF_X;
    WORD dy = picY + MOUTH_OFF_Y;

    /* Choose overlay and size */
    struct BitMapHeader *bmhd = isOpen ? mouthOpenBMHD   : mouthClosedBMHD;
    struct BitMap       *bm   = isOpen ? mouthOpenBM     : mouthClosedBM;
    UBYTE               *mask = isOpen ? mouthOpenMask    : mouthClosedMask;

    WORD w = bmhd ? bmhd->bmh_Width  : FB_MOUTH_W;
    WORD h = bmhd ? bmhd->bmh_Height : FB_MOUTH_H;

    /* 1) Restore mouth area from background picture */
    if (picMask) BltMaskBitMapRastPort(picBM, MOUTH_OFF_X, MOUTH_OFF_Y, rp, dx, dy, w, h, 0xC0, picMask);
    else         BltBitMapRastPort   (picBM, MOUTH_OFF_X, MOUTH_OFF_Y, rp, dx, dy, w, h, 0xC0);

    /* 2) Draw overlay (or a black rect fallback) */
    if (bm && bmhd){
        if (mask) BltMaskBitMapRastPort(bm, 0, 0, rp, dx, dy, w, h, 0xC0, mask);
        else      BltBitMapRastPort   (bm, 0, 0, rp, dx, dy, w, h, 0xC0);
    } else {
        SetAPen(rp,1); RectFill(rp, dx, dy, dx+w-1, dy+h-1);
    }
}

/* Vector fallback if no picture at all */
static void DrawVectorBear(UWORD mouthOpen /*0..2*/){
    SetAPen(rp, 0);
    RectFill(rp, 0, UI_BAR_H, win->Width-1, win->Height-1);

    int cy = UI_BAR_H + 20;
    SetAPen(rp, 3);
    DrawEllipse(rp, 160, cy+90, 90, 70);
    DrawEllipse(rp,  90, cy+30, 25, 20);
    DrawEllipse(rp, 230, cy+30, 25, 20);

    SetAPen(rp, 1);
    RectFill(rp, 130, cy+65, 140, cy+80);
    RectFill(rp, 180, cy+65, 190, cy+80);

    SetAPen(rp, 2);
    DrawEllipse(rp, 160, cy+115, 35, 22);

    SetAPen(rp, 1);
    if (mouthOpen==0) RectFill(rp, 150, cy+125, 170, cy+130);
    if (mouthOpen==1) RectFill(rp, 148, cy+122, 172, cy+133);
    if (mouthOpen==2) RectFill(rp, 145, cy+118, 175, cy+136);
}

/* Draw one frame */
static void DrawScene(UWORD frame){
    if (picBM){
        if (!picDrawnOnce){
            if(picMask) BltMaskBitMapRastPort(picBM, 0, 0, rp, picX, picY, picBMHD->bmh_Width, picBMHD->bmh_Height, 0xC0, picMask);
            else        BltBitMapRastPort   (picBM, 0, 0, rp, picX, picY, picBMHD->bmh_Width, picBMHD->bmh_Height, 0xC0);
            picDrawnOnce = TRUE;
        }
        /* map 0=closed, 1=open, 2=open */
        DrawMouthOverlay(frame ? 1 : 0);
    } else {
        DrawVectorBear(frame);
    }
}

/* ---------- Timer ---------- */
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
    if(OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest*)tmReq, 0)) return FALSE;
    ArmTimerMS(TICK_MS); return TRUE;
}
static void CloseTimer(void){
    if(tmReq){ if(!CheckIO((struct IORequest*)tmReq)) AbortIO((struct IORequest*)tmReq);
        WaitIO((struct IORequest*)tmReq); CloseDevice((struct IORequest*)tmReq);
        DeleteIORequest((struct IORequest*)tmReq); tmReq=NULL; }
    if(tmPort){ DeleteMsgPort(tmPort); tmPort=NULL; }
}

/* ---------- Speech ---------- */
static BOOL InitSpeech(void){
    if(!(TranslatorBase=OpenLibrary("translator.library",0))){
        SetWindowTitles(win,(UBYTE*)"Missing translator.library",(UBYTE*)-1); return FALSE; }
    narrPort=CreateMsgPort(); if(!narrPort) return FALSE;
    voiceIO=(struct narrator_rb*)CreateIORequest(narrPort,sizeof(struct narrator_rb)); if(!voiceIO) return FALSE;
#ifdef NDF_NEWIORB
    voiceIO->flags |= NDF_NEWIORB;
#endif
    voiceIO->mouths   = 0;
    voiceIO->ch_masks = (UBYTE*)kAudChanMasks;
    voiceIO->nm_masks = sizeof(kAudChanMasks);
    if(OpenDevice("narrator.device",0,(struct IORequest*)voiceIO,0)){
        SetWindowTitles(win,(UBYTE*)"Can't open narrator.device",(UBYTE*)-1); return FALSE; }
    return TRUE;
}
static void CloseSpeech(void){
    if(speaking && voiceIO){ AbortIO((struct IORequest*)voiceIO); WaitIO((struct IORequest*)voiceIO); speaking=FALSE; }
    if(voiceIO){ CloseDevice((struct IORequest*)voiceIO); DeleteIORequest((struct IORequest*)voiceIO); voiceIO=NULL; }
    if(narrPort){ DeleteMsgPort(narrPort); narrPort=NULL; }
    if(TranslatorBase){ CloseLibrary(TranslatorBase); TranslatorBase=NULL; }
}

static UBYTE* MakePhonemes(const char* text, ULONG* outLen){
    if(!text||!*text) return NULL;
    LONG inPos=0, inLen=(LONG)strlen(text); ULONG cap=512, used=0; UBYTE* buf=(UBYTE*)AllocVec(cap, MEMF_ANY); if(!buf) return NULL;
    while(inPos<inLen){
        UBYTE tmp[256]; tmp[0]=0;
        LONG rc=Translate((STRPTR)(text+inPos), inLen-inPos, (STRPTR)tmp, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1]=0;
        LONG consumed=(rc==0)?(inLen-inPos):(rc>0?rc:-rc);
        ULONG tlen=(ULONG)strlen((char*)tmp);
        if(tlen){
            if(used+tlen+1>cap){ cap=(used+tlen+1)*2; UBYTE* nb=(UBYTE*)AllocVec(cap, MEMF_ANY); if(!nb){ FreeVec(buf); return NULL; }
                memcpy(nb,buf,used); FreeVec(buf); buf=nb; }
            memcpy(buf+used,tmp,tlen); used+=tlen; buf[used]=0;
        }
        inPos+=consumed; if(rc==0) break;
    }
    if(used==0){ FreeVec(buf); return NULL; }
    if(outLen) *outLen=used; return buf;
}

static void StopSpeech(void){
    if(speaking && voiceIO){ AbortIO((struct IORequest*)voiceIO); WaitIO((struct IORequest*)voiceIO); speaking=FALSE; }
    if(voiceIO && voiceIO->message.io_Data){ FreeVec((APTR)voiceIO->message.io_Data); voiceIO->message.io_Data=NULL; }
    DrawScene(0);
}

static void ApplyVoiceParams(void){
    switch(mode){
        case MODE_BEAR:   voiceIO->rate=95;  voiceIO->pitch=70;  voiceIO->volume=64; break;
        case MODE_CHIP:   voiceIO->rate=175; voiceIO->pitch=185; voiceIO->volume=64; break;
        default:          voiceIO->rate=150; voiceIO->pitch=110; voiceIO->volume=64; break;
    }
}

static void SpeakText(const char* input){
    if(!TranslatorBase || !voiceIO) return;
    StopSpeech();

    char* ag=NULL; const char* useText=input;
    if(mode==MODE_BEAR){ ag=Bearify(input); if(ag && *ag) useText=ag; }

    ULONG plen=0; UBYTE* p=MakePhonemes(useText,&plen);
    if(ag) FreeVec(ag);
    if(!p || plen==0){ if(p) FreeVec(p); return; }

    ApplyVoiceParams();

    voiceIO->message.io_Command=CMD_WRITE;
    voiceIO->message.io_Flags=0;
    voiceIO->message.io_Offset=0;
    voiceIO->message.io_Data=p;      /* freed on completion */
    voiceIO->message.io_Length=plen; /* payload only */

    BeginIO((struct IORequest*)voiceIO);
    speaking=TRUE; mouthFrame=0;
}

/* ---------- Main ---------- */
int main(void){
    struct Screen* scr=LockPubScreen(NULL); if(!scr) return 20;

    GadToolsBase=OpenLibrary("gadtools.library",37);
    if(!GadToolsBase){ UnlockPubScreen(NULL,scr); return 20; }

    DataTypesBase=OpenLibrary("datatypes.library",39); /* optional but needed for images */
    vi=GetVisualInfoA(scr,NULL);
    if(!vi){ if(DataTypesBase) CloseLibrary(DataTypesBase); CloseLibrary(GadToolsBase); UnlockPubScreen(NULL,scr); return 20; }

    struct NewWindow nw={
        .LeftEdge=40,.TopEdge=30,.Width=500,.Height=340,
        .DetailPen=0,.BlockPen=1,
        .IDCMPFlags = IDCMP_GADGETUP|IDCMP_IDCMPUPDATE|IDCMP_REFRESHWINDOW|IDCMP_CLOSEWINDOW,
        .Flags = WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE,
        .FirstGadget=NULL,.CheckMark=NULL,.Title=(UBYTE*)"AG Bear Talker",
        .Screen=NULL,.BitMap=NULL,.MinWidth=500,.MinHeight=340,.MaxWidth=500,.MaxHeight=340,
        .Type=WBENCHSCREEN
    };

    win=OpenWindow(&nw);
    if(!win){ FreeVisualInfo(vi); if(DataTypesBase) CloseLibrary(DataTypesBase); CloseLibrary(GadToolsBase); UnlockPubScreen(NULL,scr); return 10; }
    rp=win->RPort;

    /* ---- GadTools ---- */
    CreateContext(&gadList);
    struct NewGadget ng; memset(&ng,0,sizeof(ng));
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;

    WORD strW = win->Width - (MARGIN_X*2 + 30); if (strW < 160) strW = 160;
    ng.ng_LeftEdge=MARGIN_X; ng.ng_TopEdge=MARGIN_Y; ng.ng_Width=strW; ng.ng_Height=STR_H; ng.ng_GadgetID=1;
    txtGad=CreateGadget(STRING_KIND, gadList, &ng,
                        GA_ID, (ULONG)1,
                        GTST_MaxChars, 220,
                        GTST_String, (ULONG)"I love you, friend!",
                        TAG_END);

    WORD btnY = MARGIN_Y + STR_H + GAP_Y;
    WORD x = MARGIN_X;
    ng.ng_LeftEdge=x; ng.ng_TopEdge=btnY; ng.ng_Width=BTN_W; ng.ng_Height=BTN_H; ng.ng_GadgetID=2;
    ng.ng_GadgetText=(STRPTR)"_Say";
    sayBtn=CreateGadget(BUTTON_KIND, txtGad, &ng,
                        GA_ID, (ULONG)2,
                        GT_Underscore, '_',
                        GA_RelVerify, TRUE,
                        TAG_END);

    static STRPTR modeLabels[] = { "Mode: Normal", "Mode: Bear", "Mode: Chip", NULL };
    x += BTN_W + GAP_X;
    ng.ng_LeftEdge=x; ng.ng_Width=CYC_W; ng.ng_GadgetID=3; ng.ng_GadgetText=NULL;
    modeCyc=CreateGadget(CYCLE_KIND, sayBtn, &ng,
                         GA_ID, (ULONG)3,
                         GTCY_Labels, (ULONG)modeLabels,
                         GTCY_Active, (ULONG)mode,
                         GA_RelVerify, TRUE,
                         TAG_END);

    x += CYC_W + GAP_X;
    ng.ng_LeftEdge=x; ng.ng_Width=BTN_W; ng.ng_GadgetID=4;
    ng.ng_GadgetText=(STRPTR)"_Stop";
    stopBtn=CreateGadget(BUTTON_KIND, modeCyc, &ng,
                         GA_ID, (ULONG)4,
                         GT_Underscore, '_',
                         GA_RelVerify, TRUE,
                         TAG_END);

    AddGList(win, gadList, (UWORD)-1, (UWORD)-1, NULL);
    RefreshGList(gadList, win, NULL, (UWORD)-1);
    GT_RefreshWindow(win, NULL);

    if(!InitSpeech()) SetWindowTitles(win,(UBYTE*)"Speech init failed",(UBYTE*)-1);
    if(!InitTimer())  SetWindowTitles(win,(UBYTE*)"Timer init failed",(UBYTE*)-1);

    /* Images */
    if (DataTypesBase) {
        (void)LoadPicture(PIC_PATH);
        (void)LoadMouthBitmaps();
        EnsureMouthMasksFromWhite();
        CacheMouthBBox();
    }

    DrawScene(0);

    BOOL running=TRUE;
    ULONG winSig = 1L<<win->UserPort->mp_SigBit;
    ULONG timSig = tmPort ? (1L<<tmPort->mp_SigBit) : 0;

    while(running){
        ULONG sigs=Wait(winSig | timSig);

        if ((sigs & timSig) && tmPort){
            struct Message* m;
            while((m=GetMsg(tmPort))){
                if(speaking){ mouthFrame=(mouthFrame+1)%3; DrawScene(mouthFrame); }
            }
            ArmTimerMS(TICK_MS);
        }

        if (sigs & winSig){
            struct IntuiMessage *imsg;
            while((imsg = GT_GetIMsg(win->UserPort))){
                ULONG cls = imsg->Class;
                UWORD gid = imsg->IAddress ? ((struct Gadget*)imsg->IAddress)->GadgetID : 0;

                if (cls == IDCMP_CLOSEWINDOW) {
                    running = FALSE;
                }
                else if (cls == IDCMP_IDCMPUPDATE && imsg->IAddress) {
                    struct TagItem *tags = (struct TagItem*)imsg->IAddress;
                    UWORD aid = (UWORD)GetTagData(GA_ID, 0xffff, tags);
                    if (aid == 3) {
                        ULONG active = GetTagData(GTCY_Active, 0, tags);
                        if (active > 2) active = 0;
                        mode = (VoiceMode)active;
                    }
                }
                else if (cls == IDCMP_GADGETUP) {
                    if (gid==2) { /* Say */
                        STRPTR s=NULL;
                        GT_GetGadgetAttrs(txtGad, win, NULL, GTST_String, (ULONG)&s, TAG_END);
                        SpeakText((const char*)s);
                    } else if (gid==4) { /* Stop */
                        StopSpeech();
                    } else if (gid==3) { /* some stacks also send GADGETUP for cycle */
                        ULONG active=0;
                        GT_GetGadgetAttrs(modeCyc, win, NULL, GTCY_Active, (ULONG)&active, TAG_END);
                        if (active > 2) active = 0;
                        mode = (VoiceMode)active;
                    }
                }
                else if (cls == IDCMP_REFRESHWINDOW) {
                    picDrawnOnce = FALSE; /* force picture redraw */
                    GT_BeginRefresh(win);
                    DrawScene(speaking ? mouthFrame : 0);
                    GT_EndRefresh(win, TRUE);
                }

                GT_ReplyIMsg(imsg);
            }
        }

        if (speaking && CheckIO((struct IORequest*)voiceIO)){
            WaitIO((struct IORequest*)voiceIO);
            speaking=FALSE;
            if(voiceIO->message.io_Data){ FreeVec((APTR)voiceIO->message.io_Data); voiceIO->message.io_Data=NULL; }
            DrawScene(0);
        }
    }

    if (gadList) RemoveGList(win, gadList, -1);
    if (gadList) FreeGadgets(gadList);
    CloseWindow(win);

    CloseTimer();
    CloseSpeech();
    FreeMouthBitmaps();
    FreePicture();

    if (vi) FreeVisualInfo(vi);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    UnlockPubScreen(NULL, scr);
    return 0;
}
