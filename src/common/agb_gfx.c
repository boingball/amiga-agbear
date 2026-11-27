/* agb_gfx.c - see agb_gfx.h */
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/text.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>

#include "agb_gfx.h"

struct Screen *g_scr = NULL;
struct Window *g_win = NULL;
struct RastPort g_workRP;
const char *Gfx_Error = "";

extern struct GfxBase *GfxBase;
struct Library *GadToolsBase = NULL;

static struct BitMap *workBM = NULL;
static struct TextFont *font = NULL;
static WORD limitH = SCR_H;
static BOOL firstFrame = TRUE;

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, FPF_ROMFONT };
/* DETAIL BLOCK TEXT SHINE SHADOW FILL FILLTEXT BACKGROUND HIGHLIGHTTEXT BARDETAIL BARBLOCK BARTRIM */
static UWORD scrPens[] = { PEN_DARK, PEN_BLACK, PEN_BLACK, PEN_WHITE, PEN_BLACK, PEN_BLUE, PEN_WHITE,
                           PEN_CREAM, PEN_WHITE, PEN_BLACK, PEN_WHITE, PEN_BLACK, (UWORD)~0 };

/* ------------------------------------------------------------------ screen */
BOOL Gfx_Open(const char *title, const ULONG *palette)
{
    ULONG modeID = INVALID_ID;
    struct DimensionInfo dims;

    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    Gfx_Error = "gadtools.library 39 / graphics 39";
    if (!GadToolsBase || GfxBase->LibNode.lib_Version < 39) return FALSE;

    /* lores on the native chipset when it is AGA (PAL first), otherwise the best 8-bit mode (RTG) */
    {
        static char why[80];
        ULONG cand[4];
        UWORD n = 0, i;
        /* a boot without SetPatch can leave an AGA machine in ECS mode */
        if (!(GfxBase->ChipRevBits0 & GFXF_AA_ALICE)) SetChipRev(SETCHIPREV_BEST);
        if (GfxBase->ChipRevBits0 & GFXF_AA_ALICE) {
            cand[n++] = PAL_MONITOR_ID | LORES_KEY;
            cand[n++] = LORES_KEY;               /* default monitor */
            cand[n++] = NTSC_MONITOR_ID | LORES_KEY;
        }
        cand[n++] = BestModeID(BIDTAG_NominalWidth, SCR_W, BIDTAG_NominalHeight, SCR_H,
                               BIDTAG_Depth, 8, TAG_END);
        sprintf(why, "no 8-bit screen mode (chip %02x, n %u, best %08lx)", (unsigned)GfxBase->ChipRevBits0,
                (unsigned)n, (unsigned long)cand[n - 1]);
        for (i = 0; i < n && modeID == INVALID_ID; i++) {
            ULONG na;
            if (cand[i] == INVALID_ID) continue;
            na = ModeNotAvailable(cand[i]);
            if (na) { sprintf(why, "mode %08lx not available (%08lx, chip %02x)", (unsigned long)cand[i],
                              (unsigned long)na, (unsigned)GfxBase->ChipRevBits0); continue; }
            if (GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof(dims), DTAG_DIMS, cand[i]) &&
                dims.MaxDepth < 8) {
                sprintf(why, "mode %08lx has only %u planes (chip %02x)", (unsigned long)cand[i],
                        (unsigned)dims.MaxDepth, (unsigned)GfxBase->ChipRevBits0);
                continue;
            }
            modeID = cand[i];
        }
        Gfx_Error = why;
        if (modeID == INVALID_ID) return FALSE;
    }

    g_scr = OpenScreenTags(NULL,
        SA_DisplayID, modeID, SA_Width, SCR_W, SA_Height, SCR_H, SA_Depth, 8,
        SA_Title, (ULONG)title, SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN,
        SA_Font, (ULONG)&topaz8, SA_Pens, (ULONG)scrPens, SA_Exclusive, FALSE,
        SA_Interleaved, FALSE, SA_Draggable, FALSE, SA_Overscan, OSCAN_TEXT,
        TAG_END);
    Gfx_Error = "OpenScreen failed";
    if (!g_scr) return FALSE;
    Gfx_LoadPalette(palette, 256, 0);

    g_win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)g_scr, WA_Left, 0, WA_Top, 0, WA_Width, SCR_W, WA_Height, SCR_H,
        WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
        WA_ReportMouse, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
        WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_VANILLAKEY |
                  IDCMP_GADGETUP | IDCMP_ACTIVEWINDOW,
        TAG_END);
    Gfx_Error = "OpenWindow failed";
    if (!g_win) return FALSE;

    workBM = AllocBitMap(SCR_W, SCR_H, 8, BMF_CLEAR, g_scr->RastPort.BitMap);
    Gfx_Error = "out of chip memory";
    if (!workBM) return FALSE;
    InitRastPort(&g_workRP);
    g_workRP.BitMap = workBM;
    font = OpenFont(&topaz8);
    if (font) { SetFont(&g_workRP, font); SetFont(g_win->RPort, font); }
    SetDrMd(&g_workRP, JAM1);
    firstFrame = TRUE;
    return TRUE;
}

void Gfx_Close(void)
{
    if (g_win) { CloseWindow(g_win); g_win = NULL; }
    if (workBM) { WaitBlit(); FreeBitMap(workBM); workBM = NULL; }
    if (g_scr) { CloseScreen(g_scr); g_scr = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (GadToolsBase) { CloseLibrary(GadToolsBase); GadToolsBase = NULL; }
}

static ULONG palTable[256 * 3 + 2];

static void LoadTable(UWORD count)
{
    palTable[0] = ((ULONG)count << 16) | 0;
    palTable[count * 3 + 1] = 0;
    if (g_scr) LoadRGB32(&g_scr->ViewPort, palTable);
}

static inline ULONG Expand(ULONG v) { v &= 255; return v * 0x01010101UL; }

void Gfx_LoadPaletteTinted(const ULONG *pal, UWORD from, UWORD rMul, UWORD gMul, UWORD bMul)
{
    UWORD i;
    for (i = 0; i < 256; i++) {
        ULONG r = (pal[i] >> 16) & 255, g = (pal[i] >> 8) & 255, b = pal[i] & 255;
        if (i >= from) { r = r * rMul >> 8; g = g * gMul >> 8; b = b * bMul >> 8; }
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        palTable[1 + i * 3] = Expand(r);
        palTable[2 + i * 3] = Expand(g);
        palTable[3 + i * 3] = Expand(b);
    }
    LoadTable(256);
}

void Gfx_LoadPalette(const ULONG *pal, UWORD level, UWORD from)
{
    Gfx_LoadPaletteTinted(pal, from, level, level, level);
}

void Gfx_SetColour(UWORD pen, ULONG rgb)
{
    if (g_scr) SetRGB32(&g_scr->ViewPort, pen, Expand(rgb >> 16), Expand(rgb >> 8), Expand(rgb));
}

/* ------------------------------------------------------------------ images */
static void Unpack(const UBYTE *src, UBYTE *dst, ULONG n)
{
    ULONG o = 0;
    while (o < n) {
        BYTE c = (BYTE)*src++;
        if (c >= 0) {
            UWORD k = (UWORD)c + 1;
            if (o + k > n) k = (UWORD)(n - o);
            CopyMem((APTR)src, dst + o, k);
            src += (UWORD)c + 1;
            o += k;
        } else if (c != -128) {
            UWORD k = (UWORD)(1 - c);
            UBYTE v = *src++;
            while (k-- && o < n) dst[o++] = v;
        }
    }
}

Img *Img_Unpack(const PackedImage *pi, const UBYTE *blob, UBYTE penAdd, BOOL masked)
{
    Img *img;
    UBYTE *chunky;
    struct RastPort rp;
    ULONG n = (ULONG)pi->w * pi->h, i;

    img = AllocVec(sizeof(Img), MEMF_CLEAR);
    if (!img) return NULL;
    img->w = pi->w;
    img->h = pi->h;
    chunky = AllocVec(n + 16, MEMF_ANY);
    img->bm = AllocBitMap(pi->w, pi->h, 8, BMF_CLEAR, NULL);
    if (!chunky || !img->bm) goto fail;
    Unpack(blob + pi->off, chunky, n);
    if (penAdd)
        for (i = 0; i < n; i++)
            if (chunky[i]) chunky[i] += penAdd;
    InitRastPort(&rp);
    rp.BitMap = img->bm;
    WriteChunkyPixels(&rp, 0, 0, pi->w - 1, pi->h - 1, chunky, pi->w);
    if (masked) {
        UWORD bpr = img->bm->BytesPerRow, x, y;
        img->mask = AllocVec((ULONG)bpr * pi->h, MEMF_CHIP | MEMF_CLEAR);
        if (!img->mask) goto fail;
        for (y = 0; y < pi->h; y++) {
            const UBYTE *row = chunky + (ULONG)y * pi->w;
            UBYTE *m = img->mask + (ULONG)y * bpr;
            for (x = 0; x < pi->w; x++)
                if (row[x]) m[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
        }
    }
    FreeVec(chunky);
    return img;
fail:
    if (chunky) FreeVec(chunky);
    Img_Free(img);
    return NULL;
}

void Img_Free(Img *img)
{
    if (!img) return;
    WaitBlit();
    if (img->bm) FreeBitMap(img->bm);
    if (img->mask) FreeVec(img->mask);
    FreeVec(img);
}

/* ------------------------------------------------------------------ display list */
enum { DI_IMG = 1, DI_FILL, DI_TEXT, DI_LINE };
#define TEXT_MAX 40
#define MAX_ITEMS 200

typedef struct {
    UBYTE kind, pen, pen2, pad;
    WORD x, y, w, h;          /* bounds */
    const Img *img;
    WORD sx, sy, x2, y2;
    char text[TEXT_MAX];
} DItem;

typedef struct { WORD x0, y0, x1, y1; } Rect;   /* inclusive */

static DItem listA[MAX_ITEMS], listB[MAX_ITEMS];
static DItem *cur = listA, *prev = listB;
static UWORD nCur = 0, nPrev = 0;

void Gfx_SetLimit(WORD h) { limitH = h; }
void Gfx_Invalidate(void) { firstFrame = TRUE; }

void Gfx_Begin(void) { nCur = 0; }

static DItem *NewItem(UBYTE kind)
{
    DItem *d;
    if (nCur >= MAX_ITEMS) return NULL;
    d = &cur[nCur++];
    memset(d, 0, sizeof(*d));
    d->kind = kind;
    return d;
}

void Gfx_ImagePart(const Img *img, WORD sx, WORD sy, WORD w, WORD h, WORD x, WORD y)
{
    DItem *d;
    if (!img || w <= 0 || h <= 0) return;
    if (x >= SCR_W || y >= SCR_H || x + w <= 0 || y + h <= 0) return;
    if (!(d = NewItem(DI_IMG))) return;
    d->img = img; d->sx = sx; d->sy = sy;
    d->x = x; d->y = y; d->w = w; d->h = h;
}

void Gfx_Image(const Img *img, WORD x, WORD y)
{
    if (img) Gfx_ImagePart(img, 0, 0, img->w, img->h, x, y);
}

void Gfx_Fill(UBYTE pen, WORD x, WORD y, WORD w, WORD h)
{
    DItem *d;
    if (w <= 0 || h <= 0) return;
    if (!(d = NewItem(DI_FILL))) return;
    d->pen = pen; d->x = x; d->y = y; d->w = w; d->h = h;
}

void Gfx_Frame(WORD x, WORD y, WORD w, WORD h, UBYTE light, UBYTE dark, UBYTE fill)
{
    Gfx_Fill(fill, x + 1, y + 1, w - 2, h - 2);
    Gfx_Fill(light, x, y, w - 1, 1);
    Gfx_Fill(light, x, y, 1, h - 1);
    Gfx_Fill(dark, x + 1, y + h - 1, w - 1, 1);
    Gfx_Fill(dark, x + w - 1, y + 1, 1, h - 1);
}

void Gfx_Line(UBYTE pen, WORD x0, WORD y0, WORD x1, WORD y1)
{
    DItem *d;
    if (!(d = NewItem(DI_LINE))) return;
    d->pen = pen;
    d->sx = x0; d->sy = y0; d->x2 = x1; d->y2 = y1;
    d->x = x0 < x1 ? x0 : x1;
    d->y = y0 < y1 ? y0 : y1;
    d->w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
    d->h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
}

WORD Gfx_TextWidth(const char *s) { return (WORD)(strlen(s) * 8); }

static void AddText(WORD x, WORD y, UBYTE pen, UBYTE outline, BOOL outlined, const char *s)
{
    DItem *d;
    WORD len = (WORD)strlen(s);
    if (len > TEXT_MAX - 1) len = TEXT_MAX - 1;
    if (len == 0) return;
    if (!(d = NewItem(DI_TEXT))) return;
    d->pen = pen; d->pen2 = outline; d->pad = outlined;
    memcpy(d->text, s, len);
    d->x = x - (outlined ? 1 : 0);
    d->y = y - (outlined ? 1 : 0);
    d->w = len * 8 + (outlined ? 2 : 0);
    d->h = 8 + (outlined ? 2 : 0);
    d->sx = x; d->sy = y;
}

void Gfx_Text(WORD x, WORD y, UBYTE pen, const char *s) { AddText(x, y, pen, 0, FALSE, s); }
void Gfx_TextOutlined(WORD x, WORD y, UBYTE pen, UBYTE outline, const char *s) { AddText(x, y, pen, outline, TRUE, s); }
void Gfx_TextCentred(WORD cx, WORD y, UBYTE pen, UBYTE outline, const char *s)
{
    WORD w = Gfx_TextWidth(s);
    AddText(cx - w / 2, y, pen, outline, outline != pen, s);
}

static BOOL Clip(const Rect *r, WORD *x, WORD *y, WORD *w, WORD *h, WORD *sx, WORD *sy)
{
    if (*x < r->x0) { WORD d = r->x0 - *x; *x += d; *w -= d; if (sx) *sx += d; }
    if (*y < r->y0) { WORD d = r->y0 - *y; *y += d; *h -= d; if (sy) *sy += d; }
    if (*x + *w - 1 > r->x1) *w = r->x1 - *x + 1;
    if (*y + *h - 1 > r->y1) *h = r->y1 - *y + 1;
    return *w > 0 && *h > 0;
}

static BOOL Intersects(const DItem *d, const Rect *r)
{
    return d->x <= r->x1 && d->y <= r->y1 && d->x + d->w - 1 >= r->x0 && d->y + d->h - 1 >= r->y0;
}

static void DrawText(const DItem *d, const Rect *r)
{
    WORD len = (WORD)strlen(d->text);
    WORD x = d->sx, y = d->sy;
    /* text is drawn whole, so keep it inside the bitmap */
    if (x < 1 || y < 1 || x + len * 8 + 1 > SCR_W || y + 9 > limitH) return;
    (void)r;
    if (d->pad) {
        static const BYTE off[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
        UWORD i;
        SetAPen(&g_workRP, d->pen2);
        for (i = 0; i < 8; i++) {
            Move(&g_workRP, x + off[i][0], y + 6 + off[i][1]);
            Text(&g_workRP, (STRPTR)d->text, len);
        }
    }
    SetAPen(&g_workRP, d->pen);
    Move(&g_workRP, x, y + 6);
    Text(&g_workRP, (STRPTR)d->text, len);
}

static void DrawLine(const DItem *d)
{
    WORD x0 = d->sx, y0 = d->sy, x1 = d->x2, y1 = d->y2;
    if (x0 < 0 || x1 < 0 || y0 < 0 || y1 < 0 || x0 >= SCR_W || x1 >= SCR_W || y0 >= limitH || y1 >= limitH)
        return;
    SetAPen(&g_workRP, d->pen);
    Move(&g_workRP, x0, y0);
    Draw(&g_workRP, x1, y1);
}

static void Compose(const Rect *r)
{
    UWORD i;
    for (i = 0; i < nCur; i++) {
        const DItem *d = &cur[i];
        WORD x = d->x, y = d->y, w = d->w, h = d->h, sx = d->sx, sy = d->sy;
        if (!Intersects(d, r)) continue;
        switch (d->kind) {
        case DI_IMG:
            if (!Clip(r, &x, &y, &w, &h, &sx, &sy)) break;
            if (d->img->mask)
                BltMaskBitMapRastPort(d->img->bm, sx, sy, &g_workRP, x, y, w, h,
                                      (ABC | ABNC | ANBC), d->img->mask);
            else
                BltBitMap(d->img->bm, sx, sy, workBM, x, y, w, h, 0xC0, 0xFF, NULL);
            break;
        case DI_FILL:
            if (!Clip(r, &x, &y, &w, &h, NULL, NULL)) break;
            SetAPen(&g_workRP, d->pen);
            RectFill(&g_workRP, x, y, x + w - 1, y + h - 1);
            break;
        case DI_TEXT:
            DrawText(d, r);
            break;
        case DI_LINE:
            DrawLine(d);
            break;
        }
    }
}

#define MAX_RECTS 48
static Rect rects[MAX_RECTS];
static UWORD nRects;

static void AddRect(WORD x, WORD y, WORD w, WORD h)
{
    Rect r;
    r.x0 = x < 0 ? 0 : x;
    r.y0 = y < 0 ? 0 : y;
    r.x1 = x + w - 1 >= SCR_W ? SCR_W - 1 : x + w - 1;
    r.y1 = y + h - 1 >= limitH ? limitH - 1 : y + h - 1;
    if (r.x0 > r.x1 || r.y0 > r.y1) return;
    if (nRects == MAX_RECTS) {          /* too many: merge into the last one */
        Rect *l = &rects[MAX_RECTS - 1];
        if (r.x0 < l->x0) l->x0 = r.x0;
        if (r.y0 < l->y0) l->y0 = r.y0;
        if (r.x1 > l->x1) l->x1 = r.x1;
        if (r.y1 > l->y1) l->y1 = r.y1;
        return;
    }
    rects[nRects++] = r;
}

static void MergeRects(void)
{
    BOOL again = TRUE;
    while (again) {
        UWORD i, j;
        again = FALSE;
        for (i = 0; i < nRects && !again; i++)
            for (j = i + 1; j < nRects; j++) {
                Rect *a = &rects[i], *b = &rects[j];
                if (a->x0 <= b->x1 + 4 && b->x0 <= a->x1 + 4 && a->y0 <= b->y1 + 4 && b->y0 <= a->y1 + 4) {
                    if (b->x0 < a->x0) a->x0 = b->x0;
                    if (b->y0 < a->y0) a->y0 = b->y0;
                    if (b->x1 > a->x1) a->x1 = b->x1;
                    if (b->y1 > a->y1) a->y1 = b->y1;
                    rects[j] = rects[--nRects];
                    again = TRUE;
                    break;
                }
            }
    }
}

void Gfx_End(void)
{
    UWORD i;
    DItem *t;

    nRects = 0;
    if (firstFrame) {
        AddRect(0, 0, SCR_W, limitH);
        firstFrame = FALSE;
    } else {
        UWORD n = nCur > nPrev ? nCur : nPrev;
        for (i = 0; i < n; i++) {
            const DItem *a = i < nCur ? &cur[i] : NULL;
            const DItem *b = i < nPrev ? &prev[i] : NULL;
            if (a && b && memcmp(a, b, sizeof(DItem)) == 0) continue;
            if (a) AddRect(a->x, a->y, a->w, a->h);
            if (b) AddRect(b->x, b->y, b->w, b->h);
        }
    }
    MergeRects();
    for (i = 0; i < nRects; i++) Compose(&rects[i]);
    if (nRects) WaitBlit();
    /* copy to the screen right after the vertical blank, so the beam never shows a half-copied
       area (some bitplanes new, some old: a one-frame shimmer of wrong colours) */
    WaitTOF();
    for (i = 0; i < nRects; i++) {
        const Rect *r = &rects[i];
        BltBitMapRastPort(workBM, r->x0, r->y0, g_win->RPort, r->x0, r->y0,
                          r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, 0xC0);
    }
    t = prev; prev = cur; cur = t;
    nPrev = nCur;
    nCur = 0;
}

/* ------------------------------------------------------------------ input */
static BOOL keyL, keyR, keyU, keyD, keyFire;
static BOOL lastFire;

#define HWCUSTOM ((volatile struct Custom *)0xDFF000)
#define CIAA ((volatile struct CIA *)0xBFE001)

BOOL Gfx_Poll(AgbInput *in)
{
    struct IntuiMessage *m;
    BOOL ok = TRUE;
    BOOL fire;

    in->click = in->rclick = FALSE;
    in->key = 0;
    in->quit = FALSE;
    in->gadgetUp = FALSE;
    in->firePressed = FALSE;
    while ((m = GT_GetIMsg(g_win->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        WORD mx = m->MouseX, my = m->MouseY;
        APTR addr = m->IAddress;
        GT_ReplyIMsg(m);
        switch (cls) {
        case IDCMP_MOUSEBUTTONS:
            in->mx = mx; in->my = my;
            if (code == SELECTDOWN) { in->click = TRUE; in->lmb = TRUE; }
            else if (code == SELECTUP) in->lmb = FALSE;
            else if (code == MENUDOWN) in->rclick = TRUE;
            break;
        case IDCMP_MOUSEMOVE:
            in->mx = mx; in->my = my;
            break;
        case IDCMP_VANILLAKEY:
            if (code == 27) in->quit = TRUE;
            else in->key = (UBYTE)code;
            if (code == ' ' || code == 13) in->firePressed = TRUE;
            break;
        case IDCMP_RAWKEY: {
            BOOL down = !(code & IECODE_UP_PREFIX);
            switch (code & 0x7F) {
            case 0x4F: keyL = down; break;
            case 0x4E: keyR = down; break;
            case 0x4C: keyU = down; break;
            case 0x4D: keyD = down; break;
            case 0x40: keyFire = down; break;
            case 0x45: if (down) in->quit = TRUE; break;
            }
            break;
        }
        case IDCMP_GADGETUP:
            in->gadgetUp = TRUE;
            in->gadgetID = ((struct Gadget *)addr)->GadgetID;
            break;
        }
    }
    /* joystick in port 2 (only when our screen is in front) */
    in->left = keyL; in->right = keyR; in->up = keyU; in->down = keyD;
    fire = keyFire;
    if (((struct IntuitionBase *)IntuitionBase)->FirstScreen == g_scr) {
        UWORD j = HWCUSTOM->joy1dat;
        BOOL r = (j >> 1) & 1, l = (j >> 9) & 1;
        BOOL d = ((j >> 1) ^ j) & 1, u = ((j >> 9) ^ (j >> 8)) & 1;
        if (r) in->right = TRUE;
        if (l) in->left = TRUE;
        if (d) in->down = TRUE;
        if (u) in->up = TRUE;
        if (!(CIAA->ciapra & CIAF_GAMEPORT1)) fire = TRUE;
    }
    in->fire = fire;
    if (fire && !lastFire) in->firePressed = TRUE;
    lastFire = fire;
    return ok;
}

void Gfx_WaitFrame(void)
{
    WaitTOF();     /* Gfx_End waited for the other one: 25 frames per second */
}

ULONG Gfx_Seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}
