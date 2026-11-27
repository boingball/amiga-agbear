/* agb_gfx.h - AG Bear shared graphics: a 320x256 256-colour screen, packed images and a
 * "display list" renderer.
 *
 * Every frame the program describes the whole picture again (Gfx_Begin, Gfx_Image, Gfx_Text,
 * ..., Gfx_End).  Gfx_End compares the list with the previous frame's, rebuilds only the
 * rectangles that changed in an off-screen bitmap and copies them to the screen, so nothing
 * flickers and unchanged areas cost nothing.
 */
#ifndef AGB_GFX_H
#define AGB_GFX_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>

#define SCR_W 320
#define SCR_H 256

/* fixed user-interface pens 0..15 (tools/mkassets.py UI[]) */
enum {
    PEN_DARK = 0, PEN_BLACK, PEN_WHITE, PEN_OUTLINE, PEN_PANEL, PEN_PANEL_LT, PEN_PANEL_DK,
    PEN_YELLOW, PEN_RED, PEN_GREEN, PEN_ORANGE, PEN_BLUE, PEN_CREAM, PEN_GREY, PEN_PINK, PEN_SKY
};

typedef struct { UWORD w, h; ULONG off, len; } PackedImage;

typedef struct {
    struct BitMap *bm;
    PLANEPTR mask;       /* NULL = opaque */
    UWORD w, h;
} Img;

typedef struct {
    WORD mx, my;         /* mouse position (screen pixels) */
    BOOL click;          /* left button went down this frame */
    BOOL rclick;         /* right button went down this frame */
    BOOL lmb;            /* left button held */
    BOOL left, right, up, down, fire;   /* cursor keys / joystick in port 2 (held) */
    BOOL firePressed;    /* fire / space / return went down this frame */
    UBYTE key;           /* last character typed this frame (0 = none) */
    BOOL quit;           /* Esc */
    BOOL gadgetUp;       /* a GadTools gadget was released */
    UWORD gadgetID;
} AgbInput;

extern struct Screen *g_scr;
extern struct Window *g_win;
extern struct RastPort g_workRP;   /* off-screen rastport (only Gfx_End and helpers draw here) */

extern const char *Gfx_Error;   /* why Gfx_Open failed */
BOOL Gfx_Open(const char *title, const ULONG *palette);
void Gfx_Close(void);
/* level 0..256 (256 = full colour); pens below 'from' are left at full colour */
void Gfx_LoadPalette(const ULONG *palette, UWORD level, UWORD from);
void Gfx_LoadPaletteTinted(const ULONG *palette, UWORD from, UWORD rMul, UWORD gMul, UWORD bMul);
void Gfx_SetColour(UWORD pen, ULONG rgb);

Img *Img_Unpack(const PackedImage *pi, const UBYTE *blob, UBYTE penAdd, BOOL masked);
void Img_Free(Img *img);

/* display list (call between Gfx_Begin and Gfx_End) */
void Gfx_Begin(void);
void Gfx_Image(const Img *img, WORD x, WORD y);
void Gfx_ImagePart(const Img *img, WORD sx, WORD sy, WORD w, WORD h, WORD x, WORD y);
void Gfx_Fill(UBYTE pen, WORD x, WORD y, WORD w, WORD h);
void Gfx_Frame(WORD x, WORD y, WORD w, WORD h, UBYTE light, UBYTE dark, UBYTE fill);
void Gfx_Line(UBYTE pen, WORD x0, WORD y0, WORD x1, WORD y1);
void Gfx_Text(WORD x, WORD y, UBYTE pen, const char *s);           /* y = top of the text */
void Gfx_TextOutlined(WORD x, WORD y, UBYTE pen, UBYTE outline, const char *s);
void Gfx_TextCentred(WORD cx, WORD y, UBYTE pen, UBYTE outline, const char *s);
WORD Gfx_TextWidth(const char *s);
void Gfx_End(void);          /* rebuilds what changed and shows it */
void Gfx_Invalidate(void);   /* next Gfx_End redraws everything */
/* only rows 0..h-1 belong to the display list (the rest is left to GadTools gadgets) */
void Gfx_SetLimit(WORD h);

/* input: call once per frame; returns FALSE when the window wants to close */
BOOL Gfx_Poll(AgbInput *in);
void Gfx_WaitFrame(void);    /* paces the program to 25 frames per second */
ULONG Gfx_Seconds(void);     /* wall-clock seconds (for slow pet timers) */

#endif
