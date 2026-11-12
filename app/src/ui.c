/*
 * TunnelDPI - Fluent tarzı çizim katmanı
 *
 * Şekiller kenar yumuşatmalı olarak GDI+ (flat API) ile, metinler ClearType'lı
 * GDI ile çizilir. GDI+ yalnızca ayar penceresi açıkken başlatılır.
 */
#include "ui.h"
#include <dwmapi.h>
#include <uxtheme.h>

Theme g_th;

/* ---- GDI+ flat API (MinGW'de C başlığı yok) ---- */
typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GdipStartupInput;

/* gdiplus.dll yalnızca pencere/panel açıkken yüklenir (boşta bellek harcamasın) */
static int (WINAPI *pGdiplusStartup)(ULONG_PTR *, const GdipStartupInput *, void *);
static void (WINAPI *pGdiplusShutdown)(ULONG_PTR);
static int (WINAPI *pGdipCreateFromHDC)(HDC, void **);
static int (WINAPI *pGdipDeleteGraphics)(void *);
static int (WINAPI *pGdipSetSmoothingMode)(void *, int);
static int (WINAPI *pGdipSetPixelOffsetMode)(void *, int);
static int (WINAPI *pGdipCreateSolidFill)(DWORD, void **);
static int (WINAPI *pGdipDeleteBrush)(void *);
static int (WINAPI *pGdipCreatePen1)(DWORD, float, int, void **);
static int (WINAPI *pGdipDeletePen)(void *);
static int (WINAPI *pGdipCreatePath)(int, void **);
static int (WINAPI *pGdipDeletePath)(void *);
static int (WINAPI *pGdipAddPathArc)(void *, float, float, float, float, float, float);
static int (WINAPI *pGdipClosePathFigure)(void *);
static int (WINAPI *pGdipFillPath)(void *, void *, void *);
static int (WINAPI *pGdipDrawPath)(void *, void *, void *);
static int (WINAPI *pGdipFillEllipse)(void *, void *, float, float, float, float);
static int (WINAPI *pGdipDrawEllipse)(void *, void *, float, float, float, float);
static int (WINAPI *pGdipDrawArc)(void *, void *, float, float, float, float, float, float);
static int (WINAPI *pGdipSetPenStartCap)(void *, int);
static int (WINAPI *pGdipSetPenEndCap)(void *, int);
static HMODULE g_gdip_dll;

static ULONG_PTR g_gdip;
static HFONT     g_fonts[FONT_COUNT];
static int       g_dpi = 96;

#define ARGB(c) (0xFF000000u | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c))

int ui_dpi(void)
{
    return g_dpi;
}

HFONT ui_font(int f)
{
    return g_fonts[f];
}

COLORREF ui_mix(COLORREF a, COLORREF b, int p)
{
    return RGB((GetRValue(a) * (100 - p) + GetRValue(b) * p) / 100,
               (GetGValue(a) * (100 - p) + GetGValue(b) * p) / 100,
               (GetBValue(a) * (100 - p) + GetBValue(b) * p) / 100);
}

/* ---------------- tema ---------------- */

BOOL ui_system_dark(void)
{
    DWORD v = 1, sz = sizeof(v);

    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz);
    return v == 0;
}

static COLORREF accent_color(BOOL dark)
{
    BYTE pal[32];
    DWORD sz = sizeof(pal);

    /* AccentPalette: 8 renk (RGBA). Koyu temada açık ton, açık temada koyu ton kullanılır. */
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, NULL, pal, &sz) == ERROR_SUCCESS && sz >= 32) {
        int i = dark ? 1 : 4;
        return RGB(pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2]);
    }
    return dark ? RGB(0x60, 0xCD, 0xFF) : RGB(0x00, 0x5F, 0xB8);
}

static void set_theme(int pref)
{
    BOOL dark = pref == 2 || (pref == 0 && ui_system_dark());
    Theme *t = &g_th;

    t->dark = dark;
    t->accent = accent_color(dark);
    if (dark) {
        t->card_border   = FALSE;
        /* Windows 11 koyu tonları: saf siyah değil, yumuşak gri */
        t->ground        = RGB(0x20, 0x20, 0x20);
        t->surface       = RGB(0x2B, 0x2B, 0x2B);
        t->raised        = RGB(0x37, 0x37, 0x37);
        t->raised_hover  = RGB(0x3E, 0x3E, 0x3E);
        t->active        = RGB(0x2D, 0x2D, 0x2D);
        t->hover         = RGB(0x29, 0x29, 0x29);
        t->border        = RGB(0x30, 0x30, 0x30);
        t->rule          = RGB(0x38, 0x38, 0x38);
        t->control       = RGB(0x4A, 0x4A, 0x4A);
        t->text          = RGB(0xFF, 0xFF, 0xFF);
        t->muted         = RGB(0xC5, 0xC5, 0xC5);
        t->faint         = RGB(0x9D, 0x9D, 0x9D);
        t->primary       = t->accent;
        t->primary_hover = ui_mix(t->accent, RGB(0xFF, 0xFF, 0xFF), 12);
        t->on_primary    = RGB(0x00, 0x00, 0x00);
        t->positive      = RGB(0x3F, 0xC0, 0x7A);
        t->negative      = RGB(0xE5, 0x56, 0x6F);
        t->pos_bg        = RGB(0x17, 0x2A, 0x1D);
        t->neg_bg        = RGB(0x2C, 0x1A, 0x20);
        t->notice_bg     = RGB(0x24, 0x1F, 0x10);
        t->notice_border = RGB(0x45, 0x3A, 0x16);
        t->notice_text   = RGB(0xF0, 0xD0, 0x80);
    } else {
        t->card_border   = TRUE;
        t->ground        = RGB(0xF3, 0xF3, 0xF3);
        t->surface       = RGB(0xFF, 0xFF, 0xFF);
        t->raised        = RGB(0xFF, 0xFF, 0xFF);
        t->raised_hover  = RGB(0xF5, 0xF5, 0xF5);
        t->active        = RGB(0xEE, 0xEE, 0xEE);
        t->hover         = RGB(0xF3, 0xF4, 0xF6);
        t->border        = RGB(0xE8, 0xE8, 0xE8);
        t->rule          = RGB(0xF0, 0xF0, 0xF0);
        t->control       = RGB(0xD1, 0xD5, 0xDB);
        t->text          = RGB(0x11, 0x11, 0x11);
        t->muted         = RGB(0x5F, 0x63, 0x68);
        t->faint         = RGB(0x6B, 0x6B, 0x6B);
        t->primary       = t->accent;
        t->primary_hover = ui_mix(t->accent, RGB(0x00, 0x00, 0x00), 12);
        t->on_primary    = RGB(0xFF, 0xFF, 0xFF);
        t->positive      = RGB(0x24, 0x7B, 0x50);
        t->negative      = RGB(0xC4, 0x24, 0x2A);
        t->pos_bg        = RGB(0xDC, 0xFA, 0xE5);
        t->neg_bg        = RGB(0xFF, 0xED, 0xEA);
        t->notice_bg     = RGB(0xFF, 0xFB, 0xEB);
        t->notice_border = RGB(0xFD, 0xE6, 0x8A);
        t->notice_text   = RGB(0x92, 0x40, 0x0E);
    }
}

/* ---------------- yazı tipleri ---------------- */

static int CALLBACK font_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(BOOL *)lp = TRUE;
    return 0;
}

static BOOL font_exists(const wchar_t *face)
{
    LOGFONTW lf = {0};
    BOOL found = FALSE;
    HDC dc = GetDC(NULL);

    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    EnumFontFamiliesExW(dc, &lf, font_cb, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

static HFONT mkfont(const wchar_t *face, int px, int weight)
{
    LOGFONTW lf = {0};

    lf.lfHeight = -MulDiv(px, g_dpi, 96);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    return CreateFontIndirectW(&lf);
}

static void make_fonts(void)
{
    const wchar_t *txt = font_exists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t *disp = font_exists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    const wchar_t *ico = font_exists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    const wchar_t *mono = font_exists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    /* tek aile; büyük boyutlarda aynı ailenin Display kesimi */
    g_fonts[FONT_BODY]    = mkfont(txt, 14, FW_NORMAL);
    g_fonts[FONT_MEDIUM]  = mkfont(txt, 14, FW_MEDIUM);
    g_fonts[FONT_DESC]    = mkfont(txt, 13, FW_NORMAL);
    g_fonts[FONT_CAPTION] = mkfont(txt, 12, FW_NORMAL);
    g_fonts[FONT_SECTION] = mkfont(txt, 16, FW_SEMIBOLD);
    g_fonts[FONT_VALUE]   = mkfont(txt, 18, FW_SEMIBOLD);
    g_fonts[FONT_TITLE]   = mkfont(disp, 24, FW_SEMIBOLD);
    g_fonts[FONT_STATE]   = mkfont(disp, 32, FW_SEMIBOLD);
    g_fonts[FONT_ICON]    = mkfont(ico, 16, FW_NORMAL);
    g_fonts[FONT_ICON_SM] = mkfont(ico, 12, FW_NORMAL);
    g_fonts[FONT_ICON_LG] = mkfont(ico, 34, FW_NORMAL);
    g_fonts[FONT_MONO]    = mkfont(mono, 13, FW_NORMAL);
}

static int g_refs;   /* ayar penceresi ve hızlı panel aynı anda açık olabilir */

void ui_set_dpi(int dpi)
{
    int i;
    if (dpi < 96 || dpi > 480 || dpi == g_dpi) return;
    g_dpi = dpi;
    if (!g_refs) return;
    for (i = 0; i < FONT_COUNT; i++) {
        if (g_fonts[i]) DeleteObject(g_fonts[i]);
        g_fonts[i] = NULL;
    }
    make_fonts();
}

void ui_init(int theme_pref)
{
    GdipStartupInput in = { 1, NULL, FALSE, TRUE };

    set_theme(theme_pref);
    if (g_refs++)
        return;
    {
        HDC dc = GetDC(NULL);
        g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }
    g_gdip_dll = LoadLibraryExW(L"gdiplus.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_gdip_dll) {
#define LOADFN(n) p##n = (__typeof__(p##n))(void *)GetProcAddress(g_gdip_dll, #n)
        LOADFN(GdiplusStartup);
        LOADFN(GdiplusShutdown);
        LOADFN(GdipCreateFromHDC);
        LOADFN(GdipDeleteGraphics);
        LOADFN(GdipSetSmoothingMode);
        LOADFN(GdipSetPixelOffsetMode);
        LOADFN(GdipCreateSolidFill);
        LOADFN(GdipDeleteBrush);
        LOADFN(GdipCreatePen1);
        LOADFN(GdipDeletePen);
        LOADFN(GdipCreatePath);
        LOADFN(GdipDeletePath);
        LOADFN(GdipAddPathArc);
        LOADFN(GdipClosePathFigure);
        LOADFN(GdipFillPath);
        LOADFN(GdipDrawPath);
        LOADFN(GdipFillEllipse);
        LOADFN(GdipDrawEllipse);
        LOADFN(GdipDrawArc);
        LOADFN(GdipSetPenStartCap);
        LOADFN(GdipSetPenEndCap);
#undef LOADFN
        if (pGdiplusStartup && pGdipCreateFromHDC && pGdipFillPath && pGdipFillEllipse)
            pGdiplusStartup(&g_gdip, &in, NULL);
    }
    make_fonts();
}

void ui_reload_theme(int theme_pref)
{
    set_theme(theme_pref);
}

void ui_shutdown(void)
{
    int i;

    if (g_refs <= 0 || --g_refs)
        return;

    for (i = 0; i < FONT_COUNT; i++) {
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
        g_fonts[i] = NULL;
    }
    if (g_gdip) {
        pGdiplusShutdown(g_gdip);
        g_gdip = 0;
    }
    if (g_gdip_dll) {
        FreeLibrary(g_gdip_dll);
        g_gdip_dll = NULL;
    }
}

/* ---------------- koyu mod (pencere/menü) ---------------- */

typedef int  (WINAPI *SetPreferredAppMode_t)(int);
typedef void (WINAPI *FlushMenuThemes_t)(void);

void ui_enable_dark_menus(void)
{
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetPreferredAppMode_t spam;
    FlushMenuThemes_t fmt;

    if (!ux)
        return;
    /* Belgelenmemiş ama Explorer/Notepad'in de kullandığı sıra numaraları */
    spam = (SetPreferredAppMode_t)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    fmt = (FlushMenuThemes_t)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (spam)
        spam(1); /* AllowDark: sistem temasını izle */
    if (fmt)
        fmt();
}

void ui_style_window(HWND h)
{
    BOOL dark = g_th.dark;
    COLORREF cap = g_th.ground, txt = g_th.text;
    int corner = 2; /* DWMWCP_ROUND */

    DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(h, 35 /* DWMWA_CAPTION_COLOR */, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, 36 /* DWMWA_TEXT_COLOR */, &txt, sizeof(txt));
    DwmSetWindowAttribute(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
}

void ui_style_control(HWND h)
{
    SetWindowTheme(h, g_th.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}

/* ---------------- çizim ---------------- */

void ui_begin(Paint *p, HDC dc)
{
    p->dc = dc;
    p->g = NULL;
    if (g_gdip && pGdipCreateFromHDC(dc, &p->g) == 0) {
        pGdipSetSmoothingMode(p->g, 4);   /* AntiAlias */
        pGdipSetPixelOffsetMode(p->g, 4); /* Half */
    }
    SetBkMode(dc, TRANSPARENT);
}

void ui_end(Paint *p)
{
    if (p->g)
        pGdipDeleteGraphics(p->g);
    p->g = NULL;
}

void ui_fill_rect(Paint *p, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);

    FillRect(p->dc, r, b);
    DeleteObject(b);
}

static void *round_path(float x, float y, float w, float h, float r)
{
    void *path = NULL;
    float d = r * 2;

    if (d > w) d = w;
    if (d > h) d = h;
    pGdipCreatePath(0, &path);
    if (!path)
        return NULL;
    if (d <= 0) {
        pGdipAddPathArc(path, x, y, 0.01f, 0.01f, 180, 90);
        pGdipAddPathArc(path, x + w, y, 0.01f, 0.01f, 270, 90);
        pGdipAddPathArc(path, x + w, y + h, 0.01f, 0.01f, 0, 90);
        pGdipAddPathArc(path, x, y + h, 0.01f, 0.01f, 90, 90);
    } else {
        pGdipAddPathArc(path, x, y, d, d, 180, 90);
        pGdipAddPathArc(path, x + w - d, y, d, d, 270, 90);
        pGdipAddPathArc(path, x + w - d, y + h - d, d, d, 0, 90);
        pGdipAddPathArc(path, x, y + h - d, d, d, 90, 90);
    }
    pGdipClosePathFigure(path);
    return path;
}

void ui_round(Paint *p, float x, float y, float w, float h, float rad,
              COLORREF fill, COLORREF stroke, BOOL has_stroke)
{
    void *path, *br = NULL, *pen = NULL;

    if (!p->g) {
        /* GDI+ yoksa düz dikdörtgen */
        RECT r = { (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) };
        ui_fill_rect(p, &r, fill);
        return;
    }
    if (has_stroke) {
        /* 1px çizgi piksel ortasına otursun */
        x += 0.5f; y += 0.5f; w -= 1; h -= 1;
    }
    path = round_path(x, y, w, h, rad);
    if (!path)
        return;
    pGdipCreateSolidFill(ARGB(fill), &br);
    if (br) {
        pGdipFillPath(p->g, br, path);
        pGdipDeleteBrush(br);
    }
    if (has_stroke) {
        pGdipCreatePen1(ARGB(stroke), 1.0f, 2 /* UnitPixel */, &pen);
        if (pen) {
            pGdipDrawPath(p->g, pen, path);
            pGdipDeletePen(pen);
        }
    }
    pGdipDeletePath(path);
}

void ui_circle(Paint *p, float cx, float cy, float r, COLORREF fill)
{
    void *br = NULL;

    if (!p->g)
        return;
    pGdipCreateSolidFill(ARGB(fill), &br);
    if (br) {
        pGdipFillEllipse(p->g, br, cx - r, cy - r, r * 2, r * 2);
        pGdipDeleteBrush(br);
    }
}

void ui_text(Paint *p, const wchar_t *s, const RECT *r, int font, COLORREF c, UINT fmt)
{
    RECT rr = *r;
    HGDIOBJ old = SelectObject(p->dc, g_fonts[font]);

    SetTextColor(p->dc, c);
    DrawTextW(p->dc, s, -1, &rr, fmt | DT_NOPREFIX);
    SelectObject(p->dc, old);
}

int ui_text_width(HDC dc, const wchar_t *s, int font)
{
    RECT r = { 0, 0, 0, 0 };
    HGDIOBJ old = SelectObject(dc, g_fonts[font]);

    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.right;
}

int ui_text_height(HDC dc, const wchar_t *s, int font, int width)
{
    RECT r = { 0, 0, width, 0 };
    HGDIOBJ old = SelectObject(dc, g_fonts[font]);

    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    SelectObject(dc, old);
    return r.bottom;
}

void ui_icon(Paint *p, wchar_t glyph, const RECT *r, int font, COLORREF c)
{
    wchar_t s[2] = { glyph, 0 };

    ui_text(p, s, r, font, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* Anahtar 40x20. pos: 0 kapalı .. 1 açık (animasyon ara değerleri), hover: 0..1 */
void ui_toggle(Paint *p, int x, int y, float pos, float hover, BOOL enabled)
{
    float w = (float)US(40), h = (float)US(20), r = h / 2;
    float kr = (float)US(6) + (enabled ? hover * (float)US(1) : 0);
    int pc = (int)(pos * 100 + 0.5f);
    COLORREF on_fill = !enabled ? g_th.control : ui_mix(g_th.primary, g_th.primary_hover, (int)(hover * 100));
    COLORREF off_fill = ui_mix(g_th.surface, g_th.hover, enabled ? (int)(hover * 100) : 0);
    COLORREF off_line = enabled ? g_th.muted : g_th.control;
    COLORREF fill = ui_mix(off_fill, on_fill, pc);
    COLORREF knob = ui_mix(off_line, enabled ? g_th.on_primary : g_th.surface, pc);

    ui_round(p, (float)x, (float)y, w, h, r, fill, ui_mix(off_line, on_fill, pc), TRUE);
    ui_circle(p, x + r + (w - 2 * r) * pos, y + r, kr, knob);
}

/* Yalnızca çerçeve (odak halkası) */
void ui_stroke(Paint *p, float x, float y, float w, float h, float rad, COLORREF c, float width)
{
    void *path, *pen = NULL;

    if (!p->g)
        return;
    x += width / 2; y += width / 2; w -= width; h -= width;
    path = round_path(x, y, w, h, rad);
    if (!path)
        return;
    pGdipCreatePen1(ARGB(c), width, 2 /* UnitPixel */, &pen);
    if (pen) {
        pGdipDrawPath(p->g, pen, path);
        pGdipDeletePen(pen);
    }
    pGdipDeletePath(path);
}

static DWORD argb_a(COLORREF c, int alpha)
{
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    return ((DWORD)alpha << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
}

/* Yarı saydam daire (dolgu) */
void ui_circle_alpha(Paint *p, float cx, float cy, float r, COLORREF fill, int alpha)
{
    void *br = NULL;

    if (!p->g || alpha <= 0)
        return;
    pGdipCreateSolidFill(argb_a(fill, alpha), &br);
    if (br) {
        pGdipFillEllipse(p->g, br, cx - r, cy - r, r * 2, r * 2);
        pGdipDeleteBrush(br);
    }
}

/* Halka; alpha 0-255 */
void ui_ring(Paint *p, float cx, float cy, float r, COLORREF c, int alpha, float width)
{
    void *pen = NULL;

    if (!p->g || !pGdipDrawEllipse || alpha <= 0)
        return;
    pGdipCreatePen1(argb_a(c, alpha), width, 2 /* UnitPixel */, &pen);
    if (pen) {
        pGdipDrawEllipse(p->g, pen, cx - r, cy - r, r * 2, r * 2);
        pGdipDeletePen(pen);
    }
}

/* Yuvarlak uçlu yay (dönen yükleniyor göstergesi); açılar derece */
void ui_arc(Paint *p, float cx, float cy, float r, float start, float sweep, COLORREF c, float width)
{
    void *pen = NULL;

    if (!p->g || !pGdipDrawArc)
        return;
    pGdipCreatePen1(ARGB(c), width, 2 /* UnitPixel */, &pen);
    if (pen) {
        if (pGdipSetPenStartCap && pGdipSetPenEndCap) {
            pGdipSetPenStartCap(pen, 2 /* LineCapRound */);
            pGdipSetPenEndCap(pen, 2);
        }
        pGdipDrawArc(p->g, pen, cx - r, cy - r, r * 2, r * 2, start, sweep);
        pGdipDeletePen(pen);
    }
}

/* Yarı saydam dolgu (sayfa geçişi örtüsü) */
void ui_fill_alpha(Paint *p, const RECT *r, COLORREF c, int alpha)
{
    void *br = NULL, *path;
    DWORD argb = ((DWORD)(alpha & 0xFF) << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);

    if (!p->g || alpha <= 0)
        return;
    path = round_path((float)r->left, (float)r->top, (float)(r->right - r->left), (float)(r->bottom - r->top), 0);
    if (!path)
        return;
    pGdipCreateSolidFill(argb, &br);
    if (br) {
        pGdipFillPath(p->g, br, path);
        pGdipDeleteBrush(br);
    }
    pGdipDeletePath(path);
}
