/*
 * TunnelDPI - ayar penceresinin iç tanımları
 * settings.c (pencere, yerleşim, çizim, giriş, animasyon) ile
 * settings_pages.c (sayfa içerikleri ve eylemler) arasında paylaşılır.
 */
#pragma once
#include "settings.h"
#include "config.h"
#include "ui.h"
#include "popup.h"

#define MAIN_CLASS    L"TunnelDPIWindow"
#define CONTENT_CLASS L"TunnelDPIContent"

/* ---------------- ölçüler (mantıksal px, 4px taban) ---------------- */

#define SIDEBAR_W   232
#define NAV_TOP     72
#define NAV_H       32
#define NAV_PITCH   34
#define PAD         32      /* içerik kenar boşluğu */
#define MAX_W       760     /* içerik en fazla genişlik */
#define ROW_PAD     20      /* panel içi yatay boşluk */
#define ROW_H2      64      /* başlık + açıklama */
#define ROW_H1      48      /* yalnız başlık */
#define CTL_H       32
#define R_CARD      8
#define R_CTL       6

/* ---------------- satırlar ---------------- */

enum {
    /* panel dışı */
    T_TITLE, T_SECTION, T_STATUS, T_KPIS, T_INFO, T_NOTICE, T_CODE, T_AREA, T_EMPTY,
    /* panel satırları (art arda gelenler tek panelde toplanır) */
    T_TOGGLE, T_DROP, T_BUTTONS, T_EDIT, T_EXPANDER,
};
#define IS_PANEL_ROW(t) ((t) >= T_TOGGLE)

enum {
    A_NONE = 0,
    A_BYPASS, A_HOME_PROFILE, A_HOME_DNS, A_AUTOSTART, A_TUNE, A_APP_HOTKEY,
    A_EP_SELECT, A_EP_NEW, A_EP_COPY, A_EP_DELETE, A_EP_NAME, A_EP_PRESET,
    A_EP_MENU, A_EP_RENAME, A_EP_TTL, A_LIST_MENU, A_LIST_RENAME,
    A_EP_FIELD, A_EP_FIELD_VALUE, A_EP_LIST, A_EP_ADV, A_EP_COPYCMD, A_EP_MAKELIST, A_EP_UDP,
    A_EP_ENGINE, A_EP_ZAPRET, A_TUNE_DEEP,
    A_BLK_ON, A_BLK_SRC, A_BLK_OWN, A_BLK_ALLOW, A_BLK_CUSTOM, A_AUTO_SHOW, A_AUTO_CLEAR,
    A_DNS_SERVER, A_DNS_METHOD, A_DNS_DOH, A_DNS_CUSTOM,
    A_LIST_SELECT, A_LIST_NEW, A_LIST_DELETE, A_LIST_FOLDER, A_LIST_NAME, A_LIST_TEXT,
    A_APP_BOOL, A_APP_THEME, A_APP_SERVICE, A_APP_FOLDER, A_APP_TESTHOST,
    A_APPS_MODE, A_APPS_REMOVE, A_APPS_RUNNING, A_APPS_BROWSE,
    A_DIAG, A_NET_REMEMBER, A_NET_FORGET, A_EXPORT, A_IMPORT, A_CLIP_COPY, A_CLIP_PASTE,
    A_SUB_URL, A_SUB_UPDATE, A_UPD_CHECK, A_UPD_OPEN, A_UPD_INSTALL, A_APP_LANG,
    A_LOG_COPY, A_LOG_CLEAR, A_LOG_TEXT,
    A_SITE_HOST, A_SITE_FIX, A_HEALTH_CHECK, A_LIST_RULE, A_AUTO_BOOL, A_TRIG_APPS,
    A_SCHED_FROM, A_SCHED_TO, A_SCHED_DAYS, A_DNS_BENCH, A_DNS_BEST, A_APP_QUIT, A_BACKUP, A_RESTORE, A_ISP_USE, A_LIST_TR, A_SHARE, A_GUIDE,
    A_RETRY, A_OPEN_LOG, A_SHARE_SETTINGS, A_SHARE_RETRY, A_SUPPORT, A_RELEASE,
    A_AUTO_CANDIDATES, A_AUTO_LEGACY,
};

#define BTN_INFO (-3)   /* hit(): satır başlığının yanındaki ⓘ */
/* Etiketi tek bir simge (Segoe Fluent) olan kare buton, ör. "⋯" */
#define IS_ICON_LABEL(s) ((s)[0] >= 0xE700 && (s)[0] <= 0xF8FF && !(s)[1])

enum { B_SECONDARY = 0, B_PRIMARY, B_DANGER };

#define MAX_ROWS 96
#define MAX_BTN  4

typedef struct {
    wchar_t label[32];
    int     id;
    int     kind;
    BOOL    enabled;
    RECT    rc;
} Btn;

typedef struct {
    int     type;
    int     id;
    int     arg;
    wchar_t title[96];
    wchar_t desc[256];
    wchar_t value[1024];   /* açılır menü değeri / kod metni */
    int    *pint;
    BOOL    enabled;
    BOOL    mono;
    BOOL    stacked;       /* dar alanda kontrol metnin altına iner */
    int     label_h;
    int     ctl_w;         /* metin kutusu genişliği (mantıksal), 0 = tam genişlik */
    int     min_h;         /* T_AREA / T_EMPTY: -1 = kalan alanı doldur */
    wchar_t *ebuf;
    int     ecap;
    DWORD   estyle;
    HWND    edit;
    int     nbtn;
    Btn     btn[MAX_BTN];
    const wchar_t *info;   /* ⓘ açıklaması (tr() tablosundan, kalıcı) */
    const wchar_t *cue;    /* boş metin kutusunda gri örnek yazı */
    RECT    inf;           /* ⓘ konumu */
    /* yerleşim */
    RECT    rc, ctl, field, panel;
    BOOL    first, last;   /* panel içindeki konumu */
} Row;

enum { T_SAVE = 1, T_RESTART, T_LISTSAVE, T_TICK, T_ANIM, T_BLKSAVE };

extern HWND g_main, g_content;
extern int g_page;
extern Row *g_rows;
extern int g_nrows;
extern int g_scroll, g_total_h;
extern int g_hover, g_hover_btn, g_press, g_press_btn;
extern int g_nav_hover;
extern BOOL g_drag_thumb, g_tracking;
extern int g_drag_y0, g_drag_scroll0;
extern int g_ep;
extern int g_kfr, g_kfb;
extern BOOL g_adv_open, g_blk_open;
extern wchar_t g_list_name[MAX_PATH], g_list_rename[MAX_PATH];
extern wchar_t *g_list_buf;
extern wchar_t *g_blk_own, *g_blk_allow;
extern BOOL g_ep_rename, g_list_renaming;
extern wchar_t g_sub_url[1024];
extern HBRUSH g_input_brush;
extern BOOL g_pending_save, g_pending_restart;
extern float g_tog[MAX_ROWS];
extern float g_hov[MAX_ROWS];
extern float g_hovb[MAX_ROWS][MAX_BTN];
extern float g_navh[SETTINGS_PAGE_COUNT];
extern float g_scroll_f;
extern int g_scroll_target;
extern float g_fade;
extern float g_nav_y;
extern BOOL g_animating;
extern wchar_t g_site_input[256];
extern BOOL g_site_clip;
extern PopItem g_pi[48];
extern int g_npi;

typedef struct { int row, btn; } FItem;   /* klavye odağı: satır, buton */
extern const wchar_t *g_preset_short[10];

/* settings.c */
void relayout(void);
void rebuild(BOOL keep_scroll);
void anim_kick(void);
void switch_page(int i);

/* settings_pages.c */
COLORREF input_fill(BOOL focus);
int udp_mode(const Profile *p);
Profile *ep(void);
void dns_value(wchar_t *out, size_t cap);
void fmt_duration(DWORD ms, wchar_t *out, size_t cap);
void flush_pending(void);
BOOL bl_has(const Profile *p, const wchar_t *rel);
Row *title(const wchar_t *t, const wchar_t *sub);
Row *toggle(int id, const wchar_t *t, const wchar_t *d, int *p);
void btn(Row *r, int id, const wchar_t *label, int kind, BOOL enabled);
Row *edit(int id, const wchar_t *t, const wchar_t *d, wchar_t *buf, int cap, int ctl_w, DWORD style);
void page_home(void);
void page_profiles(void);
void page_dns(void);
void list_load(void);
void page_lists(void);
void page_app(void);
void blk_save(BOOL apply);
void page_block(void);
void page_apps(void);
void page_log(void);
void update_preview(void);
int popup(Row *r);
void do_dropdown(Row *r);
void save_list(void);
void on_action(Row *r, int id);
void on_edit(int idx, int code);

