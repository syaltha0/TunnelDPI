/*
 * TunnelDPI - Fluent tarzı çizim katmanı (GDI + GDI+ flat API)
 */
#pragma once
#include "app.h"

/*
 * Renk token'ları (taste-skill "app screens" varsayılanlarından):
 * renk veriye/duruma gider, arayüz tek mürekkep rengiyle çizilir.
 * Yüzeyler kenarlıkla (açık tema) ya da bir ton farkıyla (koyu tema) ayrılır.
 */
typedef struct {
    BOOL     dark;
    BOOL     card_border;   /* açık: 1px kenarlık; koyu: yalnızca ton farkı */
    COLORREF ground;        /* pencere zemini */
    COLORREF surface;       /* panel */
    COLORREF raised;        /* buton / açılır menü dolgusu */
    COLORREF raised_hover;
    COLORREF active;        /* seçili gezinme satırı */
    COLORREF hover;         /* hafif üzerine gelme */
    COLORREF border;        /* panel kenarı */
    COLORREF rule;          /* satır ayırıcı */
    COLORREF control;       /* kontrol kenarlığı */
    COLORREF text, muted, faint;
    COLORREF primary, primary_hover, on_primary;
    COLORREF accent;        /* yalnızca klavye/odak göstergesi */
    COLORREF positive, negative, pos_bg, neg_bg;
    COLORREF notice_bg, notice_border, notice_text;
} Theme;

extern Theme g_th;

enum { FONT_BODY, FONT_MEDIUM, FONT_DESC, FONT_CAPTION, FONT_SECTION, FONT_VALUE, FONT_TITLE, FONT_STATE,
       FONT_ICON, FONT_ICON_SM, FONT_ICON_LG, FONT_MONO, FONT_COUNT };

void   ui_init(int theme_pref);         /* 0 sistem, 1 açık, 2 koyu */
void   ui_shutdown(void);
void   ui_reload_theme(int theme_pref);
HFONT  ui_font(int f);
int    ui_dpi(void);
void   ui_set_dpi(int dpi);
#define US(v) MulDiv((v), ui_dpi(), 96)

/* Uygulama genelinde koyu menüler (tray menüsü dahil) */
void   ui_enable_dark_menus(void);
void   ui_style_window(HWND h);         /* başlık çubuğu rengi/koyu mod */
void   ui_style_control(HWND h);        /* edit kaydırma çubukları vb. */
BOOL   ui_system_dark(void);

/* Çizim: Paint bir bellek DC'si üzerinde, GDI+ şekiller + GDI metin */
typedef struct {
    HDC   dc;
    void *g;     /* GpGraphics */
} Paint;

void ui_begin(Paint *p, HDC dc);
void ui_end(Paint *p);
void ui_fill_rect(Paint *p, const RECT *r, COLORREF c);
void ui_round(Paint *p, float x, float y, float w, float h, float rad, COLORREF fill, COLORREF stroke, BOOL has_stroke);
void ui_circle(Paint *p, float cx, float cy, float r, COLORREF fill);
void ui_text(Paint *p, const wchar_t *s, const RECT *r, int font, COLORREF c, UINT fmt);
int  ui_text_width(HDC dc, const wchar_t *s, int font);
int  ui_text_height(HDC dc, const wchar_t *s, int font, int width);
void ui_icon(Paint *p, wchar_t glyph, const RECT *r, int font, COLORREF c);
void ui_toggle(Paint *p, int x, int y, float pos, float hover, BOOL enabled);
void ui_fill_alpha(Paint *p, const RECT *r, COLORREF c, int alpha);
void ui_stroke(Paint *p, float x, float y, float w, float h, float rad, COLORREF c, float width);
COLORREF ui_mix(COLORREF a, COLORREF b, int pct_b);
void ui_circle_alpha(Paint *p, float cx, float cy, float r, COLORREF fill, int alpha);
void ui_ring(Paint *p, float cx, float cy, float r, COLORREF c, int alpha, float width);
void ui_arc(Paint *p, float cx, float cy, float r, float start, float sweep, COLORREF c, float width);

/* Fluent ikon kodları (Segoe Fluent Icons / Segoe MDL2 Assets) */
#define IC_HOME      0xE80F
#define IC_PROFILE   0xE9E9
#define IC_GLOBE     0xE774
#define IC_LIST      0xE8FD
#define IC_SETTINGS  0xE713
#define IC_LOG       0xE9D9
#define IC_SHIELD    0xEA18
#define IC_POWER     0xE7E8
#define IC_CHEVDOWN  0xE70D
#define IC_CHEVRIGHT 0xE76C
#define IC_CHEVUP    0xE70E
#define IC_ADD       0xE710
#define IC_COPY      0xE8C8
#define IC_DELETE    0xE74D
#define IC_FOLDER    0xE838
#define IC_CHECK     0xE73E
#define IC_ERROR     0xEA39
#define IC_INFO      0xE946
#define IC_WARN      0xE7BA
#define IC_REFRESH   0xE72C
#define IC_CODE      0xE943
#define IC_MORE      0xE712
#define IC_APPS      0xE80A
