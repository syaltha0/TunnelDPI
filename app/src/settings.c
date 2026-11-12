/*
 * TunnelDPI - ayar penceresi
 *
 * Tasarım ilkeleri (taste-skill "app screens"):
 *  - renk veriye/duruma gider; arayüz tek mürekkep rengiyle çizilir
 *  - her ekranın bir ad başlığı (24px) ve altında gerçek veriyle bir satırı var
 *  - bir bölümün satırları tek panelde, 1px çizgilerle ayrılır
 *  - yüzeyler ya kenarlık ya ton farkıyla ayrılır; gölge yok
 *  - yıkıcı işlemler en sonda, sonucu yazılarak
 *
 * Solda kenar çubuğu, sağda kaydırılabilir içerik. İçerik her sayfa için
 * yeniden kurulan bir satır listesidir. Kontroller elle çizilir; yalnızca
 * metin kutuları yerel EDIT kontrolüdür. Değişiklikler anında g_cfg'ye yazılır,
 * kaydetme ve motorun yeniden başlatılması gecikmeli yapılır.
 */
#include "settings_int.h"
#include "i18n.h"
#include "config.h"
#include "dns.h"
#include "engine.h"
#include "block.h"
#include "subs.h"
#include "icons.h"
#include "nettest.h"
#include "popup.h"
#include "diag.h"
#include "subs.h"
#include "update.h"
#include "netid.h"
#include "system.h"
#include "ui.h"
#include "presets.h"
#include "../res/resource.h"
#include <commdlg.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

static const struct { const wchar_t *name; wchar_t icon; } g_pages[SETTINGS_PAGE_COUNT] = {
    { L"Durum",          IC_HOME },
    { L"Profiller",      IC_PROFILE },
    { L"DNS",            IC_GLOBE },
    { L"Site listeleri", IC_LIST },
    { L"Engelleme",      IC_SHIELD },
    { L"Uygulamalar",    IC_APPS },
    { L"Genel",          IC_SETTINGS },
    { L"Günlük",         IC_LOG },
};

HWND   g_main, g_content;
int    g_page = SETTINGS_PAGE_HOME;
Row   *g_rows;
int    g_nrows;
int    g_scroll, g_total_h;
int    g_hover = -1, g_hover_btn = -1, g_press = -1, g_press_btn = -1;
int    g_nav_hover = -1;
BOOL   g_drag_thumb, g_tracking;
int    g_drag_y0, g_drag_scroll0;
int    g_ep;
int    g_kfr = -1, g_kfb = -1;     /* klavye odağı: satır, buton (-1 satır kontrolü) */
BOOL   g_adv_open, g_blk_open;
wchar_t g_list_name[MAX_PATH], g_list_rename[MAX_PATH];
wchar_t *g_list_buf;
wchar_t *g_blk_own, *g_blk_allow;    /* Engelleme sayfasındaki düzenlenebilir listeler */
BOOL     g_ep_rename, g_list_renaming;  /* ad kutuları yalnızca "Yeniden adlandır" ile görünür */
wchar_t g_sub_url[1024];          /* seçili listenin abonelik adresi */
HBRUSH g_input_brush;
static HBRUSH g_input_brush_f;   /* odaktaki kutunun dolgusu */
BOOL   g_pending_save, g_pending_restart;

/* animasyon durumu: hepsi 0..1 arası, hedefe üstel yaklaşır */
float  g_tog[MAX_ROWS];            /* toggle konumu */
float  g_hov[MAX_ROWS];            /* satır kontrolü üzerine gelme */
float  g_hovb[MAX_ROWS][MAX_BTN];  /* buton üzerine gelme */
float  g_navh[SETTINGS_PAGE_COUNT];
float  g_scroll_f;                 /* yumuşak kaydırma */
int    g_scroll_target;
float  g_fade = 1.0f;              /* sayfa geçişi */
float  g_nav_y = -1.0f;            /* kenar çubuğu seçim vurgusunun konumu (animasyonlu) */
BOOL   g_animating;



/* ileri bildirimler */
static BOOL toggle_value(const Row *r);
static RECT focus_rect(int row, int b);
static void scroll_to(int s);

static void log_to_edit(HWND e)
{
    size_t n = log_get(NULL, 0);
    wchar_t *t = malloc((n + 1) * sizeof(wchar_t));

    if (!t)
        return;
    log_get(t, n + 1);
    SetWindowTextW(e, t);
    SendMessageW(e, EM_SETSEL, n, n);
    SendMessageW(e, EM_SCROLLCARET, 0, 0);
    free(t);
}

static void create_edits(void)
{
    int i;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        DWORD st;

        if (r->type != T_EDIT && r->type != T_AREA)
            continue;
        st = WS_CHILD | WS_TABSTOP | (r->type == T_EDIT ? ES_AUTOHSCROLL : 0) | r->estyle;
        r->edit = CreateWindowExW(0, L"EDIT", L"", st, 0, 0, 10, 10, g_content,
                                  (HMENU)(INT_PTR)(5000 + i), g_inst, NULL);
        SendMessageW(r->edit, WM_SETFONT, (WPARAM)ui_font(r->mono ? FONT_MONO : FONT_BODY), FALSE);
        SendMessageW(r->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
        ui_style_control(r->edit);
        if (r->cue)
            SendMessageW(r->edit, EM_SETCUEBANNER, TRUE, (LPARAM)r->cue);
        if (r->ebuf) {
            SendMessageW(r->edit, EM_SETLIMITTEXT, r->ecap - 1, 0);
            SetWindowTextW(r->edit, r->ebuf);
        }
        if (r->id == A_LOG_TEXT) {
            SendMessageW(r->edit, EM_SETLIMITTEXT, 0, 0);
            log_to_edit(r->edit);
        }
        EnableWindow(r->edit, r->enabled);
    }
}

static void destroy_edits(void)
{
    int i;

    for (i = 0; i < g_nrows; i++)
        if (g_rows[i].edit) {
            DestroyWindow(g_rows[i].edit);
            g_rows[i].edit = NULL;
        }
}

/* ---------------- yerleşim ---------------- */

/* Boş durum bloğu: simge dairesi, başlık, açıklama, butonlar; satırda dikey ortalı */
#define EMPTY_H 180
static int empty_top(const Row *r)
{
    int h = US(r->nbtn ? EMPTY_H : EMPTY_H - CTL_H - 20);
    return (r->rc.top + r->rc.bottom) / 2 - h / 2;
}

static int row_height(HDC dc, Row *r, int w)
{
    int h, reserve = 0, k, textw;
    r->stacked = FALSE;
    r->label_h = 0;
    if (IS_PANEL_ROW(r->type) && r->type != T_AREA) {
        for (k = 0; k < r->nbtn; k++)
            reserve += IS_ICON_LABEL(r->btn[k].label) ? US(CTL_H + 8) : ui_text_width(dc, r->btn[k].label, FONT_BODY) + US(36);
        if (r->type == T_TOGGLE) reserve += US(64);
        if (r->type == T_DROP) reserve += min(US(320), max(US(180), ui_text_width(dc, r->value, FONT_BODY) + US(48))) + US(16);
        if (r->type == T_EXPANDER) reserve += US(40);
        if (r->type == T_EDIT && r->ctl_w) reserve += US(r->ctl_w + 16);
        textw = w - US(2 * ROW_PAD) - reserve - (r->info ? US(24) : 0);
        r->stacked = (reserve && textw < US(200)) || (r->type == T_EDIT && !r->ctl_w);
        if (r->stacked) textw = w - US(2 * ROW_PAD) - (r->info ? US(24) : 0);
        textw = max(US(80), textw);
        if (r->title[0]) r->label_h = ui_text_height(dc, r->title, FONT_MEDIUM, textw);
        if (r->desc[0]) r->label_h += US(4) + ui_text_height(dc, r->desc, FONT_DESC, textw);
        return max(US(56), r->label_h + US(24) + (r->stacked ? US(CTL_H + 16) : 0));
    }

    switch (r->type) {
    case T_TITLE:   return US(60);
    case T_SECTION:
        h = US(28 + 24);
        if (r->desc[0])
            h += ui_text_height(dc, r->desc, FONT_DESC, w) + US(2);
        return h + US(10);
    case T_STATUS:  return US(128);
    case T_KPIS:    return US(92);
    case T_INFO:    return ui_text_height(dc, r->desc, FONT_DESC, w) + US(8);
    case T_NOTICE:  return ui_text_height(dc, r->desc, FONT_DESC, w - US(2 * 16)) + US(2 * 12);
    case T_CODE:
        h = ui_text_height(dc, r->value, FONT_MONO, w - US(2 * ROW_PAD) - (r->nbtn ? US(112) : 0));
        return max(US(56), h + US(2 * 16));
    case T_AREA:    return US(r->min_h > 0 ? r->min_h : 240);
    case T_EMPTY:   return US(r->min_h > 0 ? r->min_h : 240);
    case T_EDIT:
        if (!r->ctl_w)
            return US(r->desc[0] ? ROW_H2 : ROW_H1) + US(CTL_H + 4);
        return US(r->desc[0] ? ROW_H2 : ROW_H1);
    case T_BUTTONS:
        if (!r->title[0] && !r->desc[0])
            return US(56);
        /* fall through */
    default:
        return US(r->desc[0] ? ROW_H2 : ROW_H1);
    }
}

/* Yerel metin kutularını kaydırmaya göre yerleştir; sayfa geçişi sürerken gizli tut */
static void position_edits(void)
{
    HDWP dw = BeginDeferWindowPos(8);
    UINT vis = g_fade >= 1.0f ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
    int i;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        RECT f = r->field;
        int eh = US(20);

        if (!r->edit)
            continue;
        if (r->type == T_AREA) {
            InflateRect(&f, -US(12), -US(10));
            dw = DeferWindowPos(dw, r->edit, NULL, f.left, f.top - g_scroll, f.right - f.left, f.bottom - f.top,
                                SWP_NOZORDER | SWP_NOACTIVATE | vis);
        } else {
            dw = DeferWindowPos(dw, r->edit, NULL, f.left + US(12), (f.top + f.bottom) / 2 - eh / 2 - g_scroll,
                                f.right - f.left - US(24), eh, SWP_NOZORDER | SWP_NOACTIVATE | vis);
        }
    }
    if (dw)
        EndDeferWindowPos(dw);
}

void relayout(void)
{
    RECT cr;
    HDC dc;
    int i, k, y, x0, x1, w, view_h, fill = -1, extra;

    if (!g_content || !g_rows)
        return;
    GetClientRect(g_content, &cr);
    view_h = cr.bottom;
    x0 = US(PAD);
    x1 = cr.right - US(PAD);
    if (x1 - x0 > US(MAX_W)) {          /* geniş pencerede içerik ortada durur */
        x0 = (cr.right - US(MAX_W)) / 2;
        x1 = x0 + US(MAX_W);
    }
    w = x1 - x0;
    dc = GetDC(g_content);

    y = US(28);
    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        int h = row_height(dc, r, w);
        BOOL pr = IS_PANEL_ROW(r->type);

        r->first = pr && (i == 0 || !IS_PANEL_ROW(g_rows[i - 1].type));
        r->last = pr && (i == g_nrows - 1 || !IS_PANEL_ROW(g_rows[i + 1].type));
        if ((r->type == T_AREA || r->type == T_EMPTY) && r->min_h == -1)
            fill = i;
        SetRect(&r->rc, x0, y, x1, y + h);
        y += h;
        /* bloklar arası boşluk (panel içi satırlar bitişik) */
        if (!pr || r->last)
            y += r->type == T_SECTION ? 0 : r->type == T_TITLE ? US(8) : US(16);
    }
    y += US(16);
    extra = view_h - y;
    if (fill >= 0 && extra > 0) {
        g_rows[fill].rc.bottom += extra;
        for (i = fill + 1; i < g_nrows; i++)
            OffsetRect(&g_rows[i].rc, 0, extra);
        y += extra;
    }
    g_total_h = y;

    /* panel sınırları */
    for (i = 0; i < g_nrows; i++) {
        if (!g_rows[i].first)
            continue;
        for (k = i; k < g_nrows - 1 && !g_rows[k].last; k++)
            ;
        SetRect(&g_rows[i].panel, x0, g_rows[i].rc.top, x1, g_rows[k].rc.bottom);
    }

    /* sağ kontroller */
    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        RECT *rc = &r->rc;
        int mid = (rc->top + rc->bottom) / 2, right = rc->right - (r->type == T_TITLE ? 0 : US(ROW_PAD));
        int ch = US(CTL_H);
        if (r->stacked) mid = rc->bottom - US(16) - ch / 2;

        SetRectEmpty(&r->ctl);
        SetRectEmpty(&r->field);
        for (k = r->nbtn - 1; k >= 0; k--) {
            Btn *b = &r->btn[k];
            int bw = IS_ICON_LABEL(b->label) ? ch : ui_text_width(dc, b->label, FONT_BODY) + US(28);
            int top = mid - ch / 2;

            if (r->type == T_CODE)
                top = rc->top + US(12);
            else if (r->type == T_EDIT && !r->ctl_w)
                top = rc->bottom - US(16) - ch;
            else if (r->type == T_TITLE)
                top = rc->top + US(1);
            else if (r->type == T_EMPTY)
                top = empty_top(r) + US(EMPTY_H - CTL_H);
            SetRect(&b->rc, right - bw, top, right, top + ch);
            right -= bw + US(8);
        }
        /* boş durum: butonlar ortada */
        if (r->type == T_EMPTY && r->nbtn) {
            int dx = ((rc->left + rc->right) - (r->btn[0].rc.left + r->btn[r->nbtn - 1].rc.right)) / 2;
            for (k = 0; k < r->nbtn; k++)
                OffsetRect(&r->btn[k].rc, dx, 0);
        }
        /* başlıksız buton satırı: butonlar soldan dizilir */
        if (r->type == T_BUTTONS && !r->title[0] && !r->desc[0] && r->nbtn) {
            int dx = r->btn[0].rc.left - (rc->left + US(ROW_PAD));
            for (k = 0; k < r->nbtn; k++)
                OffsetRect(&r->btn[k].rc, -dx, 0);
        }
        switch (r->type) {
        case T_TOGGLE:
            SetRect(&r->ctl, rc->right - US(ROW_PAD + 40), mid - US(10), rc->right - US(ROW_PAD), mid + US(10));
            break;
        case T_DROP: {
            int bw = ui_text_width(dc, r->value, FONT_BODY) + US(48);
            bw = max(US(180), min(bw, US(320)));
            bw = min(bw, w - US(2 * ROW_PAD));
            SetRect(&r->ctl, right - bw, mid - ch / 2, right, mid + ch / 2);
            break;
        }
        case T_EXPANDER:
            SetRect(&r->ctl, rc->right - US(ROW_PAD + 24), mid - US(12), rc->right - US(ROW_PAD), mid + US(12));
            break;
        case T_EDIT:
            if (r->ctl_w)
                SetRect(&r->field, right - US(r->ctl_w), mid - ch / 2, right, mid + ch / 2);
            else
                SetRect(&r->field, rc->left + US(ROW_PAD), rc->bottom - US(16) - ch, right, rc->bottom - US(16));
            break;
        case T_AREA:
            r->field = *rc;
            break;
        }
    }
    /* aynı paneldeki açılır listeler aynı genişlikte olsun; sol kenarları hizalı dursun */
    for (i = 0; i < g_nrows; i++) {
        int wmax = 0;

        if (!g_rows[i].first)
            continue;
        for (k = i; k < g_nrows; k++) {
            if (g_rows[k].type == T_DROP)
                wmax = max(wmax, g_rows[k].ctl.right - g_rows[k].ctl.left);
            if (g_rows[k].last)
                break;
        }
        for (k = i; k < g_nrows; k++) {
            if (g_rows[k].type == T_DROP)
                g_rows[k].ctl.left = g_rows[k].ctl.right - wmax;
            if (g_rows[k].last)
                break;
        }
    }
    ReleaseDC(g_content, dc);

    if (g_scroll_target > g_total_h - view_h)
        g_scroll_target = g_total_h - view_h;
    if (g_scroll_target < 0)
        g_scroll_target = 0;
    if (g_scroll > g_total_h - view_h)
        g_scroll = g_total_h - view_h;
    if (g_scroll < 0)
        g_scroll = 0;
    position_edits();
    InvalidateRect(g_content, NULL, FALSE);
}

void rebuild(BOOL keep_scroll)
{
    HWND focus = GetFocus();
    int i, fid = -1, farg = 0;
    DWORD s0 = 0, s1 = 0;

    int old_n = g_nrows;

    if (!g_rows)
        return;
    for (i = 0; i < g_nrows; i++)
        if (g_rows[i].edit && g_rows[i].edit == focus) {
            /* yazarken arka planda yenilenirse odak ve imleç kaybolmasın */
            fid = g_rows[i].id;
            farg = g_rows[i].arg;
            SendMessageW(focus, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
            SetFocus(g_content);
        }
    destroy_edits();
    g_nrows = 0;
    g_hover = g_press = -1;
    if (!keep_scroll) {
        g_scroll = g_scroll_target = 0;
        g_scroll_f = 0;
        g_kfr = -1;
    }
    KillTimer(g_content, T_TICK);
    switch (g_page) {
    case SETTINGS_PAGE_HOME:
        page_home();
        SetTimer(g_content, T_TICK, 1000, NULL);   /* çalışma süresi / test yaşı */
        break;
    case SETTINGS_PAGE_PROFILES: page_profiles(); update_preview(); break;
    case SETTINGS_PAGE_DNS:      page_dns(); break;
    case SETTINGS_PAGE_LISTS:    page_lists(); break;
    case SETTINGS_PAGE_BLOCK:    page_block(); break;
    case SETTINGS_PAGE_APPS:     page_apps(); break;
    case SETTINGS_PAGE_APP:      page_app(); break;
    case SETTINGS_PAGE_LOG:      page_log(); break;
    }
    /* animasyon değerleri yeni satırların mevcut durumundan başlar */
    for (i = 0; i < g_nrows; i++) {
        if (!keep_scroll || i >= old_n) {
            g_tog[i] = toggle_value(&g_rows[i]) ? 1.0f : 0.0f;
            g_hov[i] = 0;
            memset(g_hovb[i], 0, sizeof(g_hovb[i]));
        }
    }
    if (g_kfr >= g_nrows)
        g_kfr = -1;
    create_edits();
    relayout();
    if (fid >= 0)
        for (i = 0; i < g_nrows; i++)
            if (g_rows[i].edit && g_rows[i].id == fid && g_rows[i].arg == farg) {
                SetFocus(g_rows[i].edit);
                SendMessageW(g_rows[i].edit, EM_SETSEL, s0, s1);
                break;
            }
    anim_kick();
}

/* ---------------- çizim ---------------- */

static BOOL toggle_value(const Row *r)
{
    if (r->id == A_AUTOSTART)
        return app_autostart();
    if (r->id == A_EP_LIST)
        return bl_has(ep(), r->value);
    if (r->id == A_EP_UDP)
        return udp_mode(ep()) == r->arg;
    if (r->id == A_BLK_SRC)
        return block_has(&g_cfg, g_block_src[r->arg].file);
    return r->pint ? *r->pint != 0 : FALSE;
}

static void rrect(Paint *p, const RECT *r, int rad, COLORREF fill, COLORREF stroke, BOOL has_stroke)
{
    ui_round(p, (float)r->left, (float)r->top, (float)(r->right - r->left), (float)(r->bottom - r->top),
             (float)US(rad), fill, stroke, has_stroke);
}

static void draw_button(Paint *p, const Btn *b, float hover, BOOL press, int pass)
{
    COLORREF fill, txt, stroke;
    BOOL has_stroke = TRUE;

    if (b->kind == B_PRIMARY && b->enabled) {
        fill = ui_mix(g_th.primary, g_th.primary_hover, (int)(hover * 100));
        stroke = fill;
        txt = g_th.on_primary;
        has_stroke = FALSE;
    } else {
        fill = ui_mix(g_th.raised, g_th.raised_hover, b->enabled ? (int)(hover * 100) : 0);
        stroke = g_th.control;
        txt = !b->enabled ? g_th.faint : b->kind == B_DANGER ? g_th.negative : g_th.text;
    }
    if (press && b->enabled)
        fill = ui_mix(fill, g_th.ground, 30);
    if (pass == 0)
        rrect(p, &b->rc, R_CTL, fill, stroke, has_stroke);
    else if (IS_ICON_LABEL(b->label))
        ui_icon(p, b->label[0], &b->rc, FONT_ICON_SM, txt);
    else
        ui_text(p, b->label, &b->rc, FONT_BODY, txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* başlık + açıklama (panel satırının sol bloğu) */
static void draw_label(Paint *p, Row *r, int right, int top, int h)
{
    RECT t;
    COLORREF tc = r->enabled ? g_th.text : g_th.faint, dc2 = r->enabled ? g_th.muted : g_th.faint;
    int th, total;

    t.left = r->rc.left + US(ROW_PAD);
    t.right = right - US(16);
    if (r->stacked) t.right = r->rc.right - US(ROW_PAD);
    if (r->info) t.right -= US(24);
    th = r->title[0] ? ui_text_height(p->dc, r->title, FONT_MEDIUM, max(1, t.right - t.left)) : 0;
    total = th + (r->desc[0] ? US(4) + ui_text_height(p->dc, r->desc, FONT_DESC, max(1, t.right - t.left)) : 0);
    t.top = top + (h - total) / 2;
    t.bottom = t.top + th;
    SetRectEmpty(&r->inf);
    if (r->info && r->title[0]) {
        /* ⓘ başlığın hemen sağında; başlık uzunsa ona yer açılır */
        int tw = ui_text_width(p->dc, r->title, FONT_MEDIUM), ix, iy, idx = (int)(r - g_rows);
        BOOL hv = g_hover == idx && g_hover_btn == BTN_INFO;

        ix = min(t.left + tw + US(4), t.right + US(2));
        iy = t.top;
        SetRect(&r->inf, ix, iy, ix + US(20), iy + US(20));
        if (hv)
            ui_circle_alpha(p, (float)(ix + US(10)), (float)(iy + US(10)), (float)US(10), g_th.primary, 36);
        ui_icon(p, IC_INFO, &r->inf, FONT_ICON_SM, hv ? g_th.primary : g_th.faint);
    }
    if (r->desc[0] && r->title[0]) {
        ui_text(p, r->title, &t, FONT_MEDIUM, tc, DT_LEFT | DT_TOP | DT_WORDBREAK);
        t.right += r->info ? US(24) : 0;   /* açıklama tüm genişliği kullanır */
        t.top = t.bottom + US(4);
        t.bottom = t.top + ui_text_height(p->dc, r->desc, FONT_DESC, max(1, t.right - t.left));
        ui_text(p, r->desc, &t, FONT_DESC, dc2, DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (r->title[0]) {
        ui_text(p, r->title, &t, FONT_MEDIUM, tc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

static COLORREF status_color(int st)
{
    return st == ICON_ON ? g_th.positive : st == ICON_ERROR ? g_th.negative : g_th.faint;
}

/* Koruma açıkken güç düğmesinin halesi yavaşça nefes alır (0..1); sistem animasyonları kapalıysa sabit */
static float status_pulse(void)
{
    BOOL anim = TRUE;

    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    if (!anim)
        return 0.5f;
    return 0.5f + 0.5f * sinf((float)(GetTickCount() % 2600) / 2600.0f * 6.2831853f);
}

/* Ekranın sorusu: "Şu an çalışıyor mu?" Solda büyük güç düğmesi, sağında cevap. */
static void draw_status(Paint *p, Row *r, int idx, int pass)
{
    int st = app_state();
    BOOL on = bypass_running();
    RECT rc = r->rc, t;
    int rad = US(34), mid = (rc.top + rc.bottom) / 2, cx = rc.left + US(28) + rad;
    const wchar_t *word = st == ICON_ON ? tr(L"Açık") : st == ICON_ERROR ? tr(L"Hata") : st == ICON_BUSY ? tr(L"Başlıyor")
                        : app_captive() ? tr(L"Beklemede") : tr(L"Kapalı");
    wchar_t line[300];
    float hv = g_hovb[idx][0];

    r->nbtn = 1;
    r->btn[0].id = A_BYPASS;
    r->btn[0].kind = B_PRIMARY;
    r->btn[0].enabled = TRUE;
    wcscpy(r->btn[0].label, on ? tr(L"Durdur") : tr(L"Başlat"));
    SetRect(&r->btn[0].rc, cx - rad, mid - rad, cx + rad, mid + rad);

    {
        COLORREF fill, edge, icon;
        RECT ir = r->btn[0].rc;
        BOOL lit = on || st == ICON_BUSY;

        if (lit) {
            fill = ui_mix(g_th.primary, g_th.primary_hover, (int)(hv * 100));
            edge = fill;
            icon = g_th.on_primary;
        } else {
            fill = ui_mix(g_th.raised, g_th.raised_hover, (int)(hv * 100));
            edge = st == ICON_ERROR ? g_th.negative : ui_mix(g_th.control, g_th.primary, (int)(hv * 100));
            icon = st == ICON_ERROR ? g_th.negative : ui_mix(g_th.muted, g_th.primary, (int)(hv * 100));
        }
        if (pass == 1) {
            ui_icon(p, IC_POWER, &ir, FONT_ICON_LG, icon);
        } else {
            rrect(p, &rc, R_CARD, g_th.surface, g_th.border, g_th.card_border);
            if (st == ICON_BUSY) {
                float a = (float)(GetTickCount() % 1200) / 1200.0f * 360.0f;
                ui_arc(p, (float)cx, (float)mid, (float)rad + US(6), a, 90.0f, g_th.primary, (float)US(3));
            } else if (on) {
                float s = status_pulse();
                ui_circle_alpha(p, (float)cx, (float)mid, (float)rad + US(4) + s * US(6), g_th.primary, (int)(60 - s * 40));
            }
            if (g_press == idx && g_press_btn == 0)
                fill = ui_mix(fill, g_th.ground, 30);
            ui_circle(p, (float)cx, (float)mid, (float)rad, fill);
            if (!lit)
                ui_ring(p, (float)cx, (float)mid, (float)rad - US(1), edge, 255, (float)US(2));
            return;
        }
    }
    t = rc;
    t.left = cx + rad + US(24);
    t.right = rc.right - US(24);
    t.top = mid - US(32);
    t.bottom = t.top + US(40);
    ui_text(p, word, &t, FONT_STATE, st == ICON_ERROR ? g_th.negative : g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    t.top = t.bottom + US(4);
    t.bottom = t.top + US(20);
    if (st == ICON_ERROR && app_last_error()[0]) {
        int i;
        wcsncpy(line, app_last_error(), 299);
        line[299] = 0;
        for (i = 0; line[i]; i++)
            if (line[i] == L'\n' || line[i] == L'\r') line[i] = L' ';
        ui_text(p, line, &t, FONT_DESC, g_th.negative, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        int ti, tn;
        if (app_captive())
            wcscpy(line, tr(L"Bu ağ giriş istiyor; giriş yapınca koruma kendiliğinden açılır."));
        else if (app_tune_state(&ti, &tn))
            _snwprintf(line, 300, tr(L"En uygun profil aranıyor: %d/%d, %s"), ti + 1 > tn ? tn : ti + 1, tn, app_tune_name());
        else
            _snwprintf(line, 300, on ? tr(L"Engeller \"%s\" profiliyle aşılıyor.")
                                     : tr(L"Koruma kapalı; engelli siteler açılmayabilir."), g_cfg.active);
        ui_text(p, line, &t, FONT_DESC, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

/* Bölünmüş veri paneli: Profil | DNS | Çalışma süresi | Bağlantı.
   İlk iki kutucuk tıklanınca kendi açılır listesini açar (btn 0/1). */
static void draw_kpis(Paint *p, Row *r, int idx, int pass)
{
    RECT rc = r->rc, c;
    int i, cw = (rc.right - rc.left) / 4;
    const NetTest *nt = nettest_get();
    static const wchar_t *lab[4] = { L"Profil", L"DNS", L"Çalışma süresi", L"Bağlantı" };
    wchar_t val[4][96], meta[4][128], age[48];
    COLORREF vcol[4];
    DWORD up = bypass_running() ? engine_uptime_ms() : 0;

    r->nbtn = 2;
    for (i = 0; i < 2; i++) {
        r->btn[i].id = i == 0 ? A_HOME_PROFILE : A_HOME_DNS;
        r->btn[i].kind = B_SECONDARY;
        r->btn[i].enabled = TRUE;
        wcscpy(r->btn[i].label, tr(lab[i]));
        SetRect(&r->btn[i].rc, rc.left + i * cw + US(6), rc.top + US(6), rc.left + (i + 1) * cw - US(6), rc.bottom - US(6));
    }
    if (pass == 0) {
        rrect(p, &rc, R_CARD, g_th.surface, g_th.border, g_th.card_border);
        for (i = 0; i < 2; i++) {
            float hv = g_hovb[idx][i];
            if (hv > 0.01f || (g_press == idx && g_press_btn == i))
                rrect(p, &r->btn[i].rc, R_CTL, ui_mix(g_th.surface, g_th.hover,
                      g_press == idx && g_press_btn == i ? 100 : (int)(hv * 100)), 0, FALSE);
        }
        for (i = 1; i < 4; i++) {
            RECT d = { rc.left + i * cw, rc.top + US(16), rc.left + i * cw + 1, rc.bottom - US(16) };
            ui_fill_rect(p, &d, g_th.rule);
        }
        return;
    }
    for (i = 0; i < 4; i++)
        vcol[i] = g_th.text;
    wcsncpy(val[0], g_cfg.active, 95);
    val[0][95] = 0;
    {
        /* "Yöntem 5 · Türkiye için önerilen" → kutucukta yalnız açıklaması */
        const wchar_t *ps = tr(g_preset_short[config_active(&g_cfg)->preset]), *dot = wcsstr(ps, L" · ");
        wcsncpy(meta[0], dot ? dot + 3 : ps, 127);
        meta[0][127] = 0;
    }
    if (g_cfg.dns_mode == DNS_OFF)
        wcscpy(val[1], tr(L"Varsayılan"));
    else
        dns_value(val[1], 96);
    wcscpy(meta[1], g_cfg.dns_mode == DNS_OFF ? tr(L"Windows ayarı") : g_cfg.dns_mode == DNS_REDIRECT ? tr(L"uygulama üzerinden") : tr(L"Windows ayarı"));
    if (up) {
        SYSTEMTIME now;
        FILETIME ft;
        ULARGE_INTEGER u;

        fmt_duration(up, val[2], 96);
        GetLocalTime(&now);
        SystemTimeToFileTime(&now, &ft);
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        u.QuadPart -= (ULONGLONG)up * 10000;
        ft.dwLowDateTime = u.LowPart;
        ft.dwHighDateTime = u.HighPart;
        FileTimeToSystemTime(&ft, &now);
        _snwprintf(meta[2], 128, tr(L"%02d:%02d'de başladı"), now.wHour, now.wMinute);
    } else {
        wcscpy(val[2], L"—");
        vcol[2] = g_th.faint;
        wcscpy(meta[2], tr(L"çalışmıyor"));
    }
    if (nt->state == NT_OK) {
        _snwprintf(val[3], 96, L"%lu ms", nt->ms);
    } else if (nt->state == NT_FAIL) {
        wcscpy(val[3], nettest_error_text());
        vcol[3] = g_th.negative;
    } else {
        wcscpy(val[3], nt->state == NT_RUNNING ? tr(L"Ölçülüyor") : L"—");
        vcol[3] = g_th.faint;
    }
    nettest_age(age, 48);
    if (isp_name()[0])
        _snwprintf(meta[3], 128, L"%s · %s", isp_name(), age);
    else if (nt->host[0])
        _snwprintf(meta[3], 128, L"%s · %s", nt->host, age);
    else
        wcscpy(meta[3], age);

    for (i = 0; i < 4; i++) {
        c.left = rc.left + i * cw + US(20);
        c.right = rc.left + (i + 1) * cw - US(12);
        c.top = rc.top + US(16);
        c.bottom = c.top + US(18);
        ui_text(p, tr(lab[i]), &c, FONT_CAPTION, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        c.top = c.bottom + US(4);
        c.bottom = c.top + US(26);
        if (i < 2) {
            RECT ch = c;
            ch.left = ch.right - US(16);
            c.right -= US(20);
            ui_icon(p, IC_CHEVDOWN, &ch, FONT_ICON_SM, ui_mix(g_th.faint, g_th.text, (int)(g_hovb[idx][i] * 100)));
        }
        ui_text(p, val[i], &c, FONT_VALUE, vcol[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        c.right = rc.left + (i + 1) * cw - US(12);
        c.top = c.bottom + US(2);
        c.bottom = c.top + US(18);
        ui_text(p, meta[i], &c, FONT_CAPTION, g_th.faint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

/* Fluent tarzı giriş kutusu: hafif dolgu, ince kenar ve altta bir çizgi;
   odaktayken alt çizgi vurgu rengine ve iki piksele çıkar. */
static void draw_input(Paint *p, const RECT *r, BOOL focus, BOOL enabled, float hover)
{
    RECT u = *r;
    /* dolgu sabit: içteki EDIT denetimi aynı rengi (input_fill) boyar */
    COLORREF edge = ui_mix(g_th.border, g_th.control, (int)(hover * 100));

    rrect(p, r, R_CTL, enabled ? input_fill(focus) : g_th.surface, edge, TRUE);
    u.left += US(R_CTL) / 2;
    u.right -= US(R_CTL) / 2;
    u.top = u.bottom - (focus ? US(2) : 1);
    ui_fill_rect(p, &u, focus ? g_th.accent : ui_mix(g_th.control, g_th.muted, 45));
}

static void draw_row(Paint *p, int idx, int pass)
{
    Row *r = &g_rows[idx];
    RECT rc = r->rc, t;
    int k, rgt;

    switch (r->type) {
    case T_TITLE:
        for (k = 0; k < r->nbtn; k++)
            draw_button(p, &r->btn[k], g_hovb[idx][k], g_press == idx && g_press_btn == k, pass);
        if (pass == 1) {
            wchar_t sub[256];
            t = rc;
            if (r->nbtn)
                t.right = r->btn[0].rc.left - US(16);
            t.bottom = t.top + US(34);
            ui_text(p, r->title, &t, FONT_TITLE, g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (g_page == SETTINGS_PAGE_HOME) {
                DWORD up = bypass_running() ? engine_uptime_ms() : 0;
                if (up) {
                    wchar_t d[48];
                    fmt_duration(up, d, 48);
                    _snwprintf(sub, 256, tr(L"Çalışıyor · %s"), d);
                } else {
                    wcscpy(sub, app_state() == ICON_ERROR ? tr(L"Motor hata verdi") : tr(L"Durduruldu"));
                }
            } else {
                wcsncpy(sub, r->desc, 255);
                sub[255] = 0;
            }
            if (sub[0]) {
                t.top = t.bottom + US(2);
                t.bottom = t.top + US(20);
                ui_text(p, sub, &t, FONT_BODY, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        return;
    case T_SECTION:
        if (pass == 1) {
            t = rc;
            t.top += US(28);
            t.bottom = t.top + US(24);
            ui_text(p, r->title, &t, FONT_SECTION, g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (r->desc[0]) {
                t.top = t.bottom + US(2);
                t.bottom = rc.bottom;
                ui_text(p, r->desc, &t, FONT_DESC, g_th.muted, DT_LEFT | DT_TOP | DT_WORDBREAK);
            }
        }
        return;
    case T_STATUS:
        draw_status(p, r, idx, pass);
        return;
    case T_KPIS:
        draw_kpis(p, r, idx, pass);
        return;
    case T_INFO:
        if (pass == 1)
            ui_text(p, r->desc, &rc, FONT_DESC, g_th.muted, DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    case T_NOTICE:
        if (pass == 0) {
            rrect(p, &rc, R_CARD, g_th.notice_bg, g_th.notice_border, TRUE);
        } else {
            t = rc;
            InflateRect(&t, -US(16), -US(12));
            ui_text(p, r->desc, &t, FONT_DESC, g_th.notice_text, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        return;
    case T_CODE:
        if (pass == 0) {
            rrect(p, &rc, R_CARD, g_th.surface, g_th.border, g_th.card_border);
        } else {
            t = rc;
            t.left += US(ROW_PAD);
            t.right = (r->nbtn ? r->btn[0].rc.left : rc.right) - US(16);
            t.top += US(16);
            ui_text(p, r->value, &t, FONT_MONO, g_th.text, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
        }
        for (k = 0; k < r->nbtn; k++)
            draw_button(p, &r->btn[k], g_hovb[idx][k], g_press == idx && g_press_btn == k, pass);
        return;
    case T_AREA:
        if (pass == 0)
            draw_input(p, &rc, r->edit && GetFocus() == r->edit, TRUE, g_hov[idx]);
        return;
    case T_EMPTY: {
        int top = empty_top(r), cx = (rc.left + rc.right) / 2;
        RECT ir = { cx - US(32), top, cx + US(32), top + US(64) };

        if (pass == 0) {
            ui_circle(p, (float)cx, (float)(top + US(32)), (float)US(32), g_th.surface);
        } else {
            ui_icon(p, (wchar_t)r->arg, &ir, FONT_ICON_LG, g_th.muted);
            t = rc;
            t.left += US(24);
            t.right -= US(24);
            t.top = top + US(64 + 16);
            t.bottom = t.top + US(24);
            ui_text(p, r->title, &t, FONT_SECTION, g_th.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            t.top = t.bottom + US(4);
            t.bottom = t.top + US(20);
            ui_text(p, r->desc, &t, FONT_DESC, g_th.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        for (k = 0; k < r->nbtn; k++)
            draw_button(p, &r->btn[k], g_hovb[idx][k], g_press == idx && g_press_btn == k, pass);
        return;
    }
    default:
        break;
    }

    /* --- panel satırları --- */
    if (pass == 0) {
        if (r->first)
            rrect(p, &r->panel, R_CARD, g_th.surface, g_th.border, g_th.card_border);
        else {
            RECT d = { rc.left + US(ROW_PAD), rc.top, rc.right - US(ROW_PAD), rc.top + 1 };
            ui_fill_rect(p, &d, g_th.rule);
        }
        if (r->type == T_EXPANDER && g_hov[idx] > 0.01f) {
            RECT hr = rc;
            InflateRect(&hr, -US(4), -US(4));
            rrect(p, &hr, R_CTL, ui_mix(g_th.surface, g_th.hover, (int)(g_hov[idx] * 100)), 0, FALSE);
        }
        switch (r->type) {
        case T_TOGGLE:
            ui_toggle(p, r->ctl.left, r->ctl.top, g_tog[idx], g_hov[idx], r->enabled);
            break;
        case T_DROP:
            rrect(p, &r->ctl, R_CTL, ui_mix(g_th.raised, g_th.raised_hover, (int)(g_hov[idx] * 100)), g_th.control, TRUE);
            break;
        case T_EDIT:
            draw_input(p, &r->field, r->edit && GetFocus() == r->edit, r->enabled, g_hov[idx]);
            break;
        }
        for (k = 0; k < r->nbtn; k++)
            draw_button(p, &r->btn[k], g_hovb[idx][k], g_press == idx && g_press_btn == k, 0);
        return;
    }

    rgt = (r->type == T_EDIT && r->ctl_w) ? r->field.left :
          r->nbtn ? r->btn[0].rc.left : !IsRectEmpty(&r->ctl) ? r->ctl.left : rc.right;
    draw_label(p, r, r->stacked ? rc.right - US(ROW_PAD) : rgt, rc.top,
               r->stacked ? r->label_h + US(24) : rc.bottom - rc.top);

    switch (r->type) {
    case T_TOGGLE:
        t = r->ctl;
        t.right = t.left - US(12);
        t.left -= US(64);
        ui_text(p, toggle_value(r) ? tr(L"Açık") : tr(L"Kapalı"), &t, FONT_BODY, r->enabled ? g_th.muted : g_th.faint,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    case T_DROP: {
        RECT vr = r->ctl, cr2 = r->ctl;
        vr.left += US(12);
        vr.right -= US(32);
        ui_text(p, r->value, &vr, FONT_BODY, g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        cr2.left = cr2.right - US(32);
        ui_icon(p, IC_CHEVDOWN, &cr2, FONT_ICON_SM, g_th.muted);
        break;
    }
    case T_EXPANDER:
        ui_icon(p, r->pint && *r->pint ? IC_CHEVUP : IC_CHEVDOWN, &r->ctl, FONT_ICON_SM, g_th.muted);
        break;
    }
    for (k = 0; k < r->nbtn; k++)
        draw_button(p, &r->btn[k], g_hovb[idx][k], g_press == idx && g_press_btn == k, 1);
}

static void shift_rows(int dy)
{
    int i, k;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        OffsetRect(&r->rc, 0, dy);
        OffsetRect(&r->ctl, 0, dy);
        OffsetRect(&r->field, 0, dy);
        OffsetRect(&r->panel, 0, dy);
        OffsetRect(&r->inf, 0, dy);
        for (k = 0; k < r->nbtn; k++)
            OffsetRect(&r->btn[k].rc, 0, dy);
    }
}

static void shift_row(Row *r, int dy)
{
    int k;

    OffsetRect(&r->rc, 0, dy);
    OffsetRect(&r->ctl, 0, dy);
    OffsetRect(&r->field, 0, dy);
    OffsetRect(&r->panel, 0, dy);
    OffsetRect(&r->inf, 0, dy);
    for (k = 0; k < r->nbtn; k++)
        OffsetRect(&r->btn[k].rc, 0, dy);
}

/* Sayfa açılırken satırlar sırayla, hafif gecikmeyle yukarı kayarak gelir */
static int row_entry_offset(int i)
{
    float t = g_fade * 1.5f - (float)min(i, 10) * 0.05f, e;

    if (t >= 1.0f)
        return 0;
    if (t < 0.0f)
        t = 0.0f;
    e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    return (int)((1.0f - e) * US(18));
}

static void paint_content(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mem;
    RECT cr;
    HBITMAP bmp, oldb;
    Paint p;
    int i, pass;

    GetClientRect(h, &cr);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    oldb = SelectObject(mem, bmp);
    {
        Paint bgp = { mem, NULL };
        ui_fill_rect(&bgp, &cr, g_th.ground);
    }
    if (g_rows) {
        /* T_STATUS butonunun yerleşimi çizimde hesaplanır; isabet testi için kaydırmasız tut */
        float e = 1.0f - (1.0f - g_fade) * (1.0f - g_fade) * (1.0f - g_fade);   /* ease-out */

        shift_rows(-g_scroll);
        for (pass = 0; pass < 2; pass++) {
            ui_begin(&p, mem);
            if (pass == 1)
                ui_end(&p);   /* metin geçişi saf GDI */
            for (i = 0; i < g_nrows; i++) {
                const RECT *b = g_rows[i].first ? &g_rows[i].panel : &g_rows[i].rc;
                int dy;
                if (b->bottom < 0 || g_rows[i].rc.top > cr.bottom)
                    continue;
                /* panel sırayla gelir: ilk satırının gecikmesi tüm panele uygulanır */
                {
                    int head = i;
                    while (head > 0 && !g_rows[head].first && IS_PANEL_ROW(g_rows[head].type))
                        head--;
                    dy = row_entry_offset(head);
                }
                shift_row(&g_rows[i], dy);
                draw_row(&p, i, pass);
                shift_row(&g_rows[i], -dy);
            }
            /* klavye odak halkası */
            if (pass == 0 && g_kfr >= 0 && g_kfr < g_nrows && GetFocus() == g_content) {
                RECT fr = focus_rect(g_kfr, g_kfb);
                float fw = (float)(fr.right - fr.left);
                ui_stroke(&p, (float)fr.left, (float)fr.top, fw, (float)(fr.bottom - fr.top),
                          g_rows[g_kfr].type == T_STATUS ? fw / 2 : (float)US(8),
                          g_th.dark ? RGB(100,180,255) : RGB(0,80,160), (float)US(2));
            }
            if (pass == 0)
                ui_end(&p);
        }
        shift_rows(g_scroll);
        if (g_fade < 1.0f) {
            ui_begin(&p, mem);
            ui_fill_alpha(&p, &cr, g_th.ground, (int)((1.0f - e) * 255));
            ui_end(&p);
        }
    }
    if (g_total_h > cr.bottom) {
        int th = max(US(40), cr.bottom * cr.bottom / g_total_h);
        int ty = (cr.bottom - th) * g_scroll / (g_total_h - cr.bottom);
        ui_begin(&p, mem);
        ui_round(&p, (float)(cr.right - US(7)), (float)ty + US(2), (float)US(3), (float)th - US(4),
                 (float)US(2), g_th.control, 0, FALSE);
        ui_end(&p);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* Kenar çubuğu: zeminle aynı renk, içerikten 1px çizgiyle ayrılır; seçili satır nötr */
static RECT nav_rect(int i)
{
    RECT r = { US(8), US(NAV_TOP) + i * US(NAV_PITCH), US(SIDEBAR_W - 8), US(NAV_TOP) + i * US(NAV_PITCH) + US(NAV_H) };
    return r;
}

static void paint_main(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mem;
    RECT cr, t;
    HBITMAP bmp, oldb;
    Paint p;
    HICON ico;
    int i, st = app_state();
    wchar_t s[160];

    GetClientRect(h, &cr);
    cr.right = US(SIDEBAR_W);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    oldb = SelectObject(mem, bmp);
    ui_begin(&p, mem);
    ui_fill_rect(&p, &cr, g_th.ground);
    t = cr;
    t.left = t.right - 1;
    ui_fill_rect(&p, &t, g_th.border);
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        RECT r = nav_rect(i);
        if (i != g_page && g_navh[i] > 0.01f)
            rrect(&p, &r, R_CTL, ui_mix(g_th.ground, g_th.hover, (int)(g_navh[i] * 100)), 0, FALSE);
    }
    {
        /* seçili satır vurgusu yeni sayfaya kayar; soldaki çubuk yol boyunca uzayıp kısalır */
        RECT r = nav_rect(g_page), bar;
        int h = r.bottom - r.top, top, travel;

        if (g_nav_y < 0)
            g_nav_y = (float)r.top;
        top = (int)lroundf(g_nav_y);
        OffsetRect(&r, 0, top - r.top);
        rrect(&p, &r, R_CTL, g_th.active, 0, FALSE);
        travel = abs(nav_rect(g_page).top - top);
        bar.left = r.left;
        bar.right = r.left + US(3);
        bar.top = r.top + h / 2 - US(8) - min(travel / 3, h / 2 - US(4));
        bar.bottom = r.top + h / 2 + US(8) + min(travel / 3, h / 2 - US(4));
        ui_round(&p, (float)bar.left, (float)bar.top, (float)(bar.right - bar.left), (float)(bar.bottom - bar.top),
                 (float)US(2), g_th.primary, 0, FALSE);
    }
    /* alt durum satırı Durum sayfasındakinin tekrarı; yalnız diğer sayfalarda */
    if (g_page != SETTINGS_PAGE_HOME)
        ui_circle(&p, (float)US(24), (float)(cr.bottom - US(28)), (float)US(4), status_color(st));
    ui_end(&p);

    ico = LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, US(20), US(20), 0);
    if (ico) {
        DrawIconEx(mem, US(18), US(24), ico, US(20), US(20), 0, NULL, DI_NORMAL);
        DestroyIcon(ico);
    }
    SetRect(&t, US(48), US(20), cr.right - US(8), US(48));
    ui_text(&p, APP_NAME, &t, FONT_SECTION, g_th.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        RECT r = nav_rect(i), ir = r, lr = r;
        ir.left += US(10);
        ir.right = ir.left + US(20);
        ui_icon(&p, g_pages[i].icon, &ir, FONT_ICON, i == g_page ? g_th.text : g_th.muted);
        lr.left += US(40);
        ui_text(&p, tr(g_pages[i].name), &lr, i == g_page ? FONT_MEDIUM : FONT_BODY, g_th.text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_page != SETTINGS_PAGE_HOME) {
        _snwprintf(s, 160, L"%s · %s", app_state_text(), g_cfg.active);
        SetRect(&t, US(36), cr.bottom - US(40), cr.right - US(12), cr.bottom - US(16));
        ui_text(&p, s, &t, FONT_CAPTION, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* ---------------- açılır menüler ---------------- */

static int hit(int x, int y, int *bi)
{
    int i, k;
    POINT pt = { x, y + g_scroll };

    *bi = -1;
    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];

        if (!PtInRect(&r->rc, pt))
            continue;
        if (r->info && PtInRect(&r->inf, pt)) {
            *bi = BTN_INFO;
            return i;
        }
        for (k = 0; k < r->nbtn; k++)
            if (PtInRect(&r->btn[k].rc, pt)) {
                *bi = k;
                return i;
            }
        return i;
    }
    return -1;
}

static BOOL row_clickable(const Row *r, int bi)
{
    if (bi == BTN_INFO)
        return TRUE;
    if (bi >= 0)
        return r->btn[bi].enabled;
    return r->enabled && (r->type == T_TOGGLE || r->type == T_DROP || r->type == T_EXPANDER);
}

/* ---------------- klavye ----------------
 * Odaklanabilir öğeler satır sırasıyla: metin kutusu (-2), satır kontrolü
 * (-1: toggle / açılır menü / genişletici) ve etkin butonlar (0..). */



static int focus_items(FItem *out, int max)
{
    int i, k, n = 0;

    for (i = 0; i < g_nrows && n < max; i++) {
        Row *r = &g_rows[i];
        if (r->edit && r->enabled)
            out[n].row = i, out[n++].btn = -2;
        if (n < max && r->enabled && (r->type == T_TOGGLE || r->type == T_DROP || r->type == T_EXPANDER))
            out[n].row = i, out[n++].btn = -1;
        for (k = 0; k < r->nbtn && n < max; k++)
            if (r->btn[k].enabled)
                out[n].row = i, out[n++].btn = k;
        if (r->info && n < max)
            out[n].row = i, out[n++].btn = BTN_INFO;
    }
    return n;
}

static RECT focus_rect(int row, int b)
{
    Row *r = &g_rows[row];
    RECT rc;

    if (b == BTN_INFO)
        rc = r->inf;
    else if (b >= 0)
        rc = r->btn[b].rc;
    else if (r->type == T_EXPANDER)
        rc = r->rc;
    else
        rc = r->ctl;
    InflateRect(&rc, US(3), US(3));
    return rc;
}

static void ensure_visible(const RECT *rc)
{
    RECT cr;

    GetClientRect(g_content, &cr);
    if (rc->top < g_scroll_target + US(16))
        scroll_to(rc->top - US(24));
    else if (rc->bottom > g_scroll_target + cr.bottom - US(16))
        scroll_to(rc->bottom - cr.bottom + US(24));
}

/* TRUE: tuş işlendi */
static BOOL focus_key(MSG *m)
{
    FItem it[MAX_ROWS * (MAX_BTN + 3)];
    int n, cur = -1, i;
    HWND f = GetFocus();
    BOOL back = GetKeyState(VK_SHIFT) < 0;

    if (m->wParam == VK_TAB && GetKeyState(VK_CONTROL) >= 0) {
        n = focus_items(it, MAX_ROWS * (MAX_BTN + 3));
        if (!n)
            return TRUE;
        for (i = 0; i < n; i++) {
            if (it[i].btn == -2 ? g_rows[it[i].row].edit == f
                                : (f == g_content && it[i].row == g_kfr && it[i].btn == g_kfb))
                cur = i;
        }
        cur = cur < 0 ? (back ? n - 1 : 0) : (cur + (back ? n - 1 : 1)) % n;
        if (it[cur].btn == -2) {
            g_kfr = -1;
            SetFocus(g_rows[it[cur].row].edit);
            SendMessageW(g_rows[it[cur].row].edit, EM_SETSEL, 0, -1);
            ensure_visible(&g_rows[it[cur].row].field);
        } else {
            RECT rc;
            g_kfr = it[cur].row;
            g_kfb = it[cur].btn;
            SetFocus(g_content);
            rc = focus_rect(g_kfr, g_kfb);
            ensure_visible(&rc);
        }
        InvalidateRect(g_content, NULL, FALSE);
        return TRUE;
    }
    if ((m->wParam == VK_SPACE || m->wParam == VK_RETURN) && f == g_content && g_kfr >= 0 && g_kfr < g_nrows) {
        Row *r = &g_rows[g_kfr];
        if (g_kfb == BTN_INFO) {
            RECT a = r->inf;
            OffsetRect(&a, 0, -g_scroll);
            MapWindowPoints(g_content, NULL, (POINT *)&a, 2);
            ui_bubble(g_main, &a, r->title, r->info);
        } else if (g_kfb >= 0 && g_kfb < r->nbtn && r->btn[g_kfb].enabled)
            on_action(r, r->btn[g_kfb].id);
        else if (r->type == T_DROP)
            do_dropdown(r);
        else
            on_action(r, r->id);
        return TRUE;
    }
    return FALSE;
}

/* ---------------- animasyon ---------------- */

/* Hedefe üstel yaklaşma; yeterince yakınsa oturt */
static float approach(float v, float target, float k, BOOL *busy)
{
    float d = target - v;

    if (fabsf(d) < 0.005f)
        return target;
    *busy = TRUE;
    return v + d * k;
}

void anim_kick(void)
{
    if (!g_animating && g_content) {
        g_animating = TRUE;
        SetTimer(g_content, T_ANIM, 15, NULL);
    }
}

static void anim_step(void)
{
    BOOL busy = FALSE, nav = FALSE;
    int i, k;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        if (r->type == T_TOGGLE)
            g_tog[i] = approach(g_tog[i], toggle_value(r) ? 1.0f : 0.0f, 0.32f, &busy);
        g_hov[i] = approach(g_hov[i], i == g_hover && g_hover_btn == -1 ? 1.0f : 0.0f, 0.3f, &busy);
        for (k = 0; k < r->nbtn; k++)
            g_hovb[i][k] = approach(g_hovb[i][k], i == g_hover && g_hover_btn == k ? 1.0f : 0.0f, 0.3f, &busy);
    }
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++)
        g_navh[i] = approach(g_navh[i], i == g_nav_hover ? 1.0f : 0.0f, 0.3f, &nav);
    if (g_nav_y >= 0) {
        float target = (float)nav_rect(g_page).top;
        if (fabsf(target - g_nav_y) < 0.6f)
            g_nav_y = target;
        else {
            g_nav_y += (target - g_nav_y) * 0.22f;
            nav = TRUE;
        }
    }
    if (g_scroll != g_scroll_target) {
        g_scroll_f += (g_scroll_target - g_scroll_f) * 0.28f;
        if (fabsf(g_scroll_target - g_scroll_f) < 0.6f)
            g_scroll_f = (float)g_scroll_target;
        g_scroll = (int)lroundf(g_scroll_f);
        position_edits();
        busy = TRUE;
    }
    if (g_fade < 1.0f) {
        g_fade += 0.06f;
        if (g_fade >= 1.0f) {
            g_fade = 1.0f;
            position_edits();   /* metin kutularını göster */
        }
        busy = TRUE;
    }
    InvalidateRect(g_content, NULL, FALSE);
    if (nav)
        InvalidateRect(g_main, NULL, FALSE);
    if (!busy && !nav) {
        KillTimer(g_content, T_ANIM);
        g_animating = FALSE;
    }
}

static void scroll_to(int s)
{
    RECT cr;

    GetClientRect(g_content, &cr);
    if (s > g_total_h - cr.bottom)
        s = g_total_h - cr.bottom;
    if (s < 0)
        s = 0;
    if (s != g_scroll_target) {
        g_scroll_target = s;
        g_scroll_f = (float)g_scroll;
        anim_kick();
    }
}

/* sürükleme gibi doğrudan kaydırma (animasyonsuz) */
static void scroll_now(int s)
{
    RECT cr;

    GetClientRect(g_content, &cr);
    if (s > g_total_h - cr.bottom)
        s = g_total_h - cr.bottom;
    if (s < 0)
        s = 0;
    g_scroll = g_scroll_target = s;
    g_scroll_f = (float)s;
    position_edits();
    InvalidateRect(g_content, NULL, FALSE);
}

static LRESULT CALLBACK content_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int bi, i;

    switch (msg) {
    case WM_PAINT:
        paint_content(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SIZE:
        relayout();
        return 0;
    case WM_MOUSEWHEEL:
        scroll_to(g_scroll_target - GET_WHEEL_DELTA_WPARAM(wp) * US(110) / WHEEL_DELTA);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g_drag_thumb) {
            RECT cr;
            GetClientRect(h, &cr);
            if (cr.bottom > 0)
                scroll_now(g_drag_scroll0 + (y - g_drag_y0) * g_total_h / cr.bottom);
            return 0;
        }
        i = hit(x, y, &bi);
        if (i >= 0 && !row_clickable(&g_rows[i], bi)) {
            i = -1;
            bi = -1;
        }
        if (i != g_hover || bi != g_hover_btn) {
            g_hover = i;
            g_hover_btn = bi;
            anim_kick();
        }
        if (!g_tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            g_tracking = TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover >= 0) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSELEAVE:
        g_tracking = FALSE;
        if (g_hover >= 0) {
            g_hover = g_hover_btn = -1;
            anim_kick();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        RECT cr;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        g_kfr = -1;
        SetFocus(h);
        GetClientRect(h, &cr);
        if (g_total_h > cr.bottom && x >= cr.right - US(12)) {
            g_drag_thumb = TRUE;
            g_drag_y0 = y;
            g_drag_scroll0 = g_scroll;
            SetCapture(h);
            return 0;
        }
        g_press = hit(x, y, &g_press_btn);
        if (g_press >= 0 && !row_clickable(&g_rows[g_press], g_press_btn))
            g_press = -1;
        if (g_press >= 0)
            SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int p = g_press, pb = g_press_btn;
        ReleaseCapture();
        if (g_drag_thumb) {
            g_drag_thumb = FALSE;
            return 0;
        }
        g_press = g_press_btn = -1;
        i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &bi);
        if (p >= 0 && i == p && bi == pb) {
            Row *r = &g_rows[p];
            if (pb == BTN_INFO) {
                RECT a = r->inf;
                OffsetRect(&a, 0, -g_scroll);
                MapWindowPoints(h, NULL, (POINT *)&a, 2);
                ui_bubble(g_main, &a, r->title, r->info);
            } else if (pb >= 0)
                on_action(r, r->btn[pb].id);
            else if (r->type == T_DROP)
                do_dropdown(r);
            else
                on_action(r, r->id);
        }
        if (g_content)
            InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) >= 5000 && LOWORD(wp) < 5000 + MAX_ROWS)
            on_edit(LOWORD(wp) - 5000, HIWORD(wp));
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        BOOL en = IsWindowEnabled((HWND)lp), foc = GetFocus() == (HWND)lp;
        SetTextColor(dc, en ? g_th.text : g_th.faint);
        SetBkColor(dc, input_fill(foc));
        return (LRESULT)(foc ? g_input_brush_f : g_input_brush);
    }
    case WM_TIMER:
        if (wp == T_ANIM) {
            anim_step();
            return 0;
        }
        if (wp == T_TICK) {
            if (g_page == SETTINGS_PAGE_HOME) {
                /* güç düğmesi nefes alırken / dönerken sık, aksi halde saniyede bir */
                BOOL anim = TRUE, live;
                SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
                live = anim && IsWindowVisible(g_main) && !IsIconic(g_main) &&
                       (bypass_running() || app_state() == ICON_BUSY);
                SetTimer(h, T_TICK, live ? 40 : 1000, NULL);
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        KillTimer(h, wp);
        if (wp == T_SAVE && g_pending_save) {
            config_save(&g_cfg);
            g_pending_save = FALSE;
        } else if (wp == T_RESTART && g_pending_restart) {
            g_pending_restart = FALSE;
            bypass_restart_if_running();
        } else if (wp == T_LISTSAVE) {
            save_list();
        } else if (wp == T_BLKSAVE) {
            blk_save(TRUE);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void make_brushes(void)
{
    if (g_input_brush)
        DeleteObject(g_input_brush);
    if (g_input_brush_f)
        DeleteObject(g_input_brush_f);
    g_input_brush = CreateSolidBrush(input_fill(FALSE));
    g_input_brush_f = CreateSolidBrush(input_fill(TRUE));
}

void switch_page(int i)
{
    if (i < 0 || i >= SETTINGS_PAGE_COUNT || !g_main)
        return;
    if (g_page == SETTINGS_PAGE_LISTS && i != g_page)
        save_list();
    if (g_page == SETTINGS_PAGE_BLOCK && i != g_page && g_content && KillTimer(g_content, T_BLKSAVE))
        blk_save(TRUE);
    g_page = i;
    g_fade = 0.0f;
    InvalidateRect(g_main, NULL, FALSE);
    rebuild(FALSE);
    if (i == SETTINGS_PAGE_HOME && nettest_get()->state == NT_NONE)
        nettest_start();
}

static LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;

    switch (msg) {
    case WM_PAINT:
        paint_main(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (g_content)
            SetWindowPos(g_content, NULL, US(SIDEBAR_W), 0, LOWORD(lp) - US(SIDEBAR_W), HIWORD(lp),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        mm->ptMinTrackSize.x = min(US(820), mi.rcWork.right - mi.rcWork.left);
        mm->ptMinTrackSize.y = min(US(540), mi.rcWork.bottom - mi.rcWork.top);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT *suggested = (RECT *)lp;
        ui_set_dpi(HIWORD(wp));
        for (i = 0; i < g_nrows; i++)
            if (g_rows[i].edit)
                SendMessageW(g_rows[i].edit, WM_SETFONT, (WPARAM)ui_font(g_rows[i].mono ? FONT_MONO : FONT_BODY), TRUE);
        SetWindowPos(h, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        relayout();
        RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hv = -1;
        for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
            RECT r = nav_rect(i);
            if (PtInRect(&r, pt))
                hv = i;
        }
        if (hv != g_nav_hover) {
            g_nav_hover = hv;
            anim_kick();
        }
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_nav_hover >= 0) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSELEAVE:
        if (g_nav_hover >= 0) {
            g_nav_hover = -1;
            anim_kick();
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_nav_hover >= 0 && g_nav_hover != g_page)
            switch_page(g_nav_hover);
        return 0;
    case WM_MOUSEWHEEL:
        return SendMessageW(g_content, msg, wp, lp);
    case WM_KEYDOWN:
        /* Ctrl+Tab / Ctrl+Shift+Tab: sayfalar arasında geçiş */
        if (wp == VK_TAB && GetKeyState(VK_CONTROL) < 0)
            switch_page((g_page + (GetKeyState(VK_SHIFT) < 0 ? SETTINGS_PAGE_COUNT - 1 : 1)) % SETTINGS_PAGE_COUNT);
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && !wcscmp((const wchar_t *)lp, L"ImmersiveColorSet")) {
            ui_reload_theme(g_cfg.theme);
            ui_style_window(h);
            make_brushes();
            for (i = 0; i < g_nrows; i++)
                if (g_rows[i].edit)
                    ui_style_control(g_rows[i].edit);
            RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL)
            settings_close();
        return 0;
    case WM_CLOSE:
        settings_close();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------------- dış arayüz ---------------- */

void settings_show(int page)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    RECT r = { 0, 0, 0, 0 }, wa;
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    HICON ico;

    if (g_main) {
        if (page >= 0 && page != g_page)
            switch_page(page);
        ShowWindow(g_main, IsIconic(g_main) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_main);
        return;
    }
    if (page >= 0)
        g_page = page;
    g_ep = config_find(&g_cfg, g_cfg.active);
    if (g_ep < 0)
        g_ep = 0;
    g_rows = calloc(MAX_ROWS, sizeof(Row));
    if (!g_rows)
        return;
    g_nrows = 0;
    ui_init(g_cfg.theme);
    make_brushes();

    wc.lpfnWndProc = main_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = MAIN_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = content_proc;
    wc.lpszClassName = CONTENT_CLASS;
    RegisterClassExW(&wc);

    r.right = US(1040);
    r.bottom = US(720);
    AdjustWindowRectEx(&r, style, FALSE, 0);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (r.bottom - r.top > wa.bottom - wa.top)
        r.bottom = r.top + (wa.bottom - wa.top);
    if (r.right - r.left > wa.right - wa.left)
        r.right = r.left + (wa.right - wa.left);
    g_main = CreateWindowExW(WS_EX_CONTROLPARENT, MAIN_CLASS, APP_NAME, style,
                             wa.left + ((wa.right - wa.left) - (r.right - r.left)) / 2,
                             wa.top + ((wa.bottom - wa.top) - (r.bottom - r.top)) / 2,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, g_inst, NULL);
    if (!g_main)
        return;
    ui_style_window(g_main);
    ico = LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                     GetSystemMetrics(SM_CYICON), 0);
    SendMessageW(g_main, WM_SETICON, ICON_BIG, (LPARAM)ico);
    ico = LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                     GetSystemMetrics(SM_CYSMICON), 0);
    SendMessageW(g_main, WM_SETICON, ICON_SMALL, (LPARAM)ico);

    GetClientRect(g_main, &r);
    g_content = CreateWindowExW(WS_EX_CONTROLPARENT, CONTENT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                US(SIDEBAR_W), 0, r.right - US(SIDEBAR_W), r.bottom, g_main, NULL, g_inst, NULL);
    rebuild(FALSE);
    ShowWindow(g_main, SW_SHOWNORMAL);
    SetForegroundWindow(g_main);
    if (g_page == SETTINGS_PAGE_HOME && nettest_get()->state == NT_NONE)
        nettest_start();
}

void settings_close(void)
{
    HICON big, small;

    if (!g_main)
        return;
    if (g_page == SETTINGS_PAGE_LISTS)
        save_list();
    KillTimer(g_content, T_SAVE);
    KillTimer(g_content, T_RESTART);
    KillTimer(g_content, T_TICK);
    flush_pending();
    destroy_edits();
    g_nrows = 0;
    big = (HICON)SendMessageW(g_main, WM_GETICON, ICON_BIG, 0);
    small = (HICON)SendMessageW(g_main, WM_GETICON, ICON_SMALL, 0);
    DestroyWindow(g_main);
    if (big) DestroyIcon(big);
    if (small) DestroyIcon(small);
    g_main = g_content = NULL;
    g_nav_y = -1.0f;
    g_site_clip = FALSE;                 /* bir sonraki açılışta pano yeniden okunur */
    free(g_rows);
    g_rows = NULL;
    free(g_list_buf);
    g_list_buf = NULL;
    g_list_name[0] = 0;
    blk_save(FALSE);                     /* bekleyen düzenleme kaybolmasın */
    free(g_blk_own);
    free(g_blk_allow);
    g_blk_own = g_blk_allow = NULL;
    if (g_input_brush) DeleteObject(g_input_brush);
    if (g_input_brush_f) DeleteObject(g_input_brush_f);
    g_input_brush = g_input_brush_f = NULL;
    ui_shutdown();
    UnregisterClassW(MAIN_CLASS, g_inst);
    UnregisterClassW(CONTENT_CLASS, g_inst);
    trim_memory();
}

BOOL settings_dialog_msg(MSG *m)
{
    if (!g_main)
        return FALSE;
    if (m->message == WM_KEYDOWN && (m->hwnd == g_main || IsChild(g_main, m->hwnd)) && focus_key(m))
        return TRUE;
    if (m->message == WM_KEYDOWN && m->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0 &&
        (m->hwnd == g_main || IsChild(g_main, m->hwnd))) {
        SendMessageW(g_main, WM_KEYDOWN, m->wParam, m->lParam);
        return TRUE;
    }
    return IsDialogMessageW(g_main, m);
}

void settings_on_log(void)
{
    int i;

    if (!g_main || g_page != SETTINGS_PAGE_LOG)
        return;
    for (i = 0; i < g_nrows; i++)
        if (g_rows[i].id == A_LOG_TEXT && g_rows[i].edit)
            log_to_edit(g_rows[i].edit);
}

void settings_on_state(void)
{
    if (!g_main)
        return;
    InvalidateRect(g_main, NULL, FALSE);
    InvalidateRect(g_content, NULL, FALSE);
}

#ifdef DEV_SHOT
void settings_dev_adv(void)
{
    g_adv_open = TRUE;
    rebuild(TRUE);
}

/* Durum sayfasındaki ilk ⓘ baloncuğunu aç (modal) */
void settings_dev_bubble(void)
{
    int i;

    for (i = 0; i < g_nrows; i++)
        if (g_rows[i].info) {
            RECT a = { 600, 400, 620, 420 };
            ui_bubble(g_main, &a, g_rows[i].title, g_rows[i].info);
            return;
        }
}

/* Durum sayfasındaki DNS açılır listesini aç (modal) */
void settings_dev_dropdown(void)
{
    int i;

    for (i = 0; i < g_nrows; i++)
        if (g_rows[i].id == A_HOME_DNS) {
            do_dropdown(&g_rows[i]);
            return;
        }
}
#endif

void settings_on_config(void)
{
    if (!g_main)
        return;
    if (g_ep >= g_cfg.nprofiles)
        g_ep = 0;
    rebuild(TRUE);
    InvalidateRect(g_main, NULL, FALSE);
}

/* Bağlantı testi bitince (main.c'den) */
void settings_on_nettest(void)
{
    if (g_main && (g_page == SETTINGS_PAGE_HOME || g_page == SETTINGS_PAGE_DNS))
        rebuild(TRUE);
}

/* Abonelik indirmesi listeleri değiştirdi (main.c'den) */
void settings_on_lists(void)
{
    if (!g_main)
        return;
    if (g_page == SETTINGS_PAGE_LISTS && g_list_name[0])
        list_load();
    rebuild(TRUE);
}

#ifdef DEV_SHOT
/* ---------------- arayüz testi (yalnızca geliştirme sürümü) ----------------
 * Her sayfadaki her anahtara, açılır listeye, düğmeye ve menü öğesine basar;
 * metin kutularına yazar. Değişmesi gereken değer değişmezse "HATA" yazar. */
#include <limits.h>
#include <stdio.h>
extern int     g_dev_pick;
extern PopItem g_dev_items[64];
extern int     g_dev_nitems;

static void dev_pump(int ms)
{
    DWORD t = GetTickCount();
    MSG m;

    while (GetTickCount() - t < (DWORD)ms) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                PostQuitMessage((int)m.wParam);
                return;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    }
}

int settings_dev_qualitytest(FILE *f, int *fails)
{
    const int dpis[] = {96, 120, 144, 192};
    int lang, d, page, i, k, cases = 0;
    settings_show(0);
    ShowWindow(g_main, SW_SHOW);
    for (lang = LANG_TR; lang <= LANG_EN; lang++) {
        i18n_set(lang);
        for (d = 0; d < 4; d++) {
            RECT wr;
            GetWindowRect(g_main, &wr);
            SendMessageW(g_main, WM_DPICHANGED, MAKELONG(dpis[d], dpis[d]), (LPARAM)&wr);
            for (page = 0; page < SETTINGS_PAGE_COUNT; page++) {
                FItem focus[MAX_ROWS * (MAX_BTN + 3)];
                int count, has_power = 0, has_info = 0;
                switch_page(page);
                SetWindowPos(g_content, NULL, US(SIDEBAR_W), 0, US(520), US(600), SWP_NOZORDER | SWP_NOACTIVATE);
                g_fade = 1;
                position_edits();
                RedrawWindow(g_content, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                count = focus_items(focus, MAX_ROWS * (MAX_BTN + 3));
                for (i = 0; i < count; i++) {
                    Row *r = &g_rows[focus[i].row];
                    if (r->type == T_STATUS) has_power++;
                    if (focus[i].btn == BTN_INFO) has_info++;
                    for (k = i + 1; k < count; k++)
                        if (focus[i].row == focus[k].row && focus[i].btn == focus[k].btn) (*fails)++;
                }
                if (!count || (page == SETTINGS_PAGE_HOME && !has_power) ||
                    (page == SETTINGS_PAGE_PROFILES && !has_info)) {
                    fwprintf(f, L"FAIL focus lang=%d dpi=%d page=%d count=%d power=%d info=%d\n", lang, dpis[d], page, count, has_power, has_info);
                    (*fails)++;
                }
                for (i = 0; i < g_nrows; i++) {
                    Row *r = &g_rows[i];
                    if (r->rc.bottom <= r->rc.top) (*fails)++;
                    if (r->stacked && r->label_h + US(24 + CTL_H) > r->rc.bottom - r->rc.top) (*fails)++;
                    for (k = 0; k < r->nbtn; k++) {
                        RECT b = r->btn[k].rc;
                        if (b.left < r->rc.left || b.right > r->rc.right || b.bottom > r->rc.bottom || b.top < r->rc.top) {
                            fwprintf(f, L"FAIL bounds lang=%d dpi=%d page=%d row=%d button=%d\n", lang, dpis[d], page, i, k);
                            (*fails)++;
                        }
                    }
                }
                cases++;
            }
        }
    }
    i18n_set(LANG_TR);
    switch_page(SETTINGS_PAGE_HOME);
    g_fade = 1;
    RedrawWindow(g_content, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    SetFocus(g_content);
    g_kfr = -1;
    {
        MSG key = { 0 };
        key.message = WM_KEYDOWN; key.wParam = VK_TAB; key.hwnd = g_content;
        focus_key(&key);
        if (g_kfr < 0 || g_rows[g_kfr].type != T_STATUS) {
            fwprintf(f, L"FAIL first Tab row=%d\n", g_kfr);
            (*fails)++;
        }
    }
    fwprintf(f, L"DPI/layout/keyboard: %d cases, %d failures\n", cases, *fails);
    settings_close();
    return cases;
}

typedef struct { int type, id, arg, btn, bid; wchar_t title[96]; } DevItem;

static int dev_find(const DevItem *it)
{
    int i, any = -1;

    for (i = 0; g_rows && i < g_nrows; i++)
        if (g_rows[i].type == it->type && g_rows[i].id == it->id && g_rows[i].arg == it->arg) {
            if (it->btn >= 0) {
                int k, ok = 0;
                for (k = 0; k < g_rows[i].nbtn; k++)
                    if (g_rows[i].btn[k].id == it->bid)
                        ok = 1;
                if (!ok)
                    continue;
            }
            if (!wcscmp(g_rows[i].title, it->title))
                return i;
            if (any < 0)
                any = i;
        }
    return any;
}

static void dev_state(const Row *r, wchar_t *out, size_t cap)
{
    switch (r->type) {
    case T_TOGGLE:   _snwprintf(out, cap, L"%s", toggle_value(r) ? L"açık" : L"kapalı"); break;
    case T_DROP:     _snwprintf(out, cap, L"%s", r->value); break;
    case T_EXPANDER: _snwprintf(out, cap, L"%s", r->pint && *r->pint ? L"açık" : L"kapalı"); break;
    default:         _snwprintf(out, cap, L"%s", r->desc); break;
    }
    out[cap - 1] = 0;
}

static void dev_act(const DevItem *it, int ri)
{
    Row *r = &g_rows[ri];
    int k;

    if (it->btn < 0) {
        if (r->type == T_DROP)
            do_dropdown(r);
        else
            on_action(r, r->id);
        return;
    }
    for (k = 0; k < r->nbtn; k++)
        if (r->btn[k].id == it->bid) {
            on_action(r, it->bid);
            return;
        }
}

/* dışarıyı etkileyen ya da testi bitiren eylemler; ana koruma düğmesi ayrıca denenir */
static BOOL dev_skip(int id)
{
    static const int skip[] = {
        A_AUTOSTART, A_APP_SERVICE, A_UPD_OPEN, A_UPD_INSTALL, A_APP_FOLDER, A_LIST_FOLDER, A_APP_QUIT,
        A_CLIP_COPY, A_LOG_COPY, A_BYPASS,
    };
    size_t i;

    for (i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (skip[i] == id)
            return TRUE;
    return FALSE;
}

static BOOL dev_is_menu(int id)
{
    return id == A_EP_MENU || id == A_LIST_MENU || id == A_APPS_RUNNING;
}

static int dev_selectables(void)
{
    int i, n = 0;

    for (i = 0; i < g_dev_nitems; i++)
        if (!(g_dev_items[i].flags & (PI_SEP | PI_DISABLED)))
            n++;
    return n;
}

/* k. seçilebilir öğe (1'den) */
static const PopItem *dev_item(int k)
{
    int i, s = 0;

    for (i = 0; i < g_dev_nitems; i++)
        if (!(g_dev_items[i].flags & (PI_SEP | PI_DISABLED)) && ++s == k)
            return &g_dev_items[i];
    return NULL;
}

static void dev_edits(FILE *f, int *fails, int *n)
{
    int i;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        wchar_t old[1024];
        const wchar_t *probe;
        int id, arg;
        BOOL ok;

        if (r->type != T_EDIT || !r->edit || !r->enabled || !r->ebuf || r->id == A_EP_NAME || r->id == A_LIST_NAME ||
            r->id == A_SUB_URL)
            continue;
        probe = r->estyle & ES_NUMBER ? L"7" : L"deneme.com";
        id = r->id;
        arg = r->arg;
        GetWindowTextW(r->edit, old, 1024);
        SetWindowTextW(r->edit, probe);
        dev_pump(60);
        if (i >= g_nrows)
            break;
        r = &g_rows[i];
        if (r->id != id || r->arg != arg || !r->ebuf)
            continue;
        ok = !wcscmp(r->ebuf, probe);
        fwprintf(f, L"  [kutu] %s: %s\n", r->title[0] ? r->title : L"(metin alanı)", ok ? L"yazılıyor" : L"HATA: tampona geçmedi");
        if (!ok)
            (*fails)++;
        if (r->edit)
            SetWindowTextW(r->edit, old);
        dev_pump(60);
        (*n)++;
    }
}

static int dev_items(DevItem *items)
{
    int i, k, ni = 0;

    for (i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        DevItem *it;

        if (r->enabled && (r->type == T_TOGGLE || r->type == T_DROP || r->type == T_EXPANDER)) {
            it = &items[ni++];
            it->type = r->type; it->id = r->id; it->arg = r->arg; it->btn = -1; it->bid = 0;
            wcscpy(it->title, r->title);
        }
        for (k = 0; k < r->nbtn; k++)
            if (r->btn[k].enabled) {
                it = &items[ni++];
                it->type = r->type; it->id = r->id; it->arg = r->arg; it->btn = k; it->bid = r->btn[k].id;
                wcscpy(it->title, r->title[0] ? r->title : r->btn[k].label);
            }
    }
    return ni;
}

static void dev_dropdown(FILE *f, int *fails, const DevItem *it, int ri, int page)
{
    wchar_t before[1024], after[1024];
    int cnt, kk, orig = 0, pick;

    g_dev_pick = INT_MIN;                      /* önce öğeleri öğren */
    dev_act(it, ri);
    dev_pump(100);
    cnt = dev_selectables();
    for (kk = 1; kk <= cnt; kk++)
        if (dev_item(kk)->flags & PI_CHECK)
            orig = kk;
    if (cnt < 2) {
        fwprintf(f, L"  [liste] %s: tek seçenek\n", it->title);
        g_dev_pick = 0;
        return;
    }
    pick = orig % cnt + 1;                     /* seçili olanın bir sonrakini seç */
    dev_state(&g_rows[ri], before, 1024);
    g_dev_pick = -pick;
    dev_act(it, ri);
    dev_pump(200);
    ri = dev_find(it);
    if (g_page != page || ri < 0) {
        fwprintf(f, L"  [liste] %s: seçim sayfayı değiştirdi\n", it->title);
        g_dev_pick = 0;
        return;
    }
    dev_state(&g_rows[ri], after, 1024);
    fwprintf(f, L"  [liste] %s: \"%s\" → \"%s\" %s\n", it->title, before, after,
             wcscmp(before, after) ? L"" : L"HATA: değişmedi");
    if (!wcscmp(before, after))
        (*fails)++;
    if (orig) {                                /* eski seçime dön */
        g_dev_pick = -orig;
        dev_act(it, ri);
        dev_pump(200);
        ri = dev_find(it);
        if (ri >= 0) {
            dev_state(&g_rows[ri], after, 1024);
            if (wcscmp(before, after)) {
                fwprintf(f, L"  [liste] %s: HATA: eski değere dönülemedi (\"%s\")\n", it->title, after);
                (*fails)++;
            }
        }
    }
    g_dev_pick = 0;
}

static void dev_menu(FILE *f, const DevItem *it, int ri, int page)
{
    PopItem saved[64];
    int cnt, kk, sn;

    g_dev_pick = INT_MIN;
    dev_act(it, ri);
    dev_pump(100);
    cnt = dev_selectables();
    sn = g_dev_nitems;
    memcpy(saved, g_dev_items, sizeof(saved));
    for (kk = 1; kk <= cnt; kk++) {
        const PopItem *pi;

        g_dev_nitems = sn;
        memcpy(g_dev_items, saved, sizeof(saved));
        pi = dev_item(kk);
        if (!pi || (it->bid != A_APPS_RUNNING && dev_skip(pi->id))) {
            fwprintf(f, L"  [menü] %s → %s: atlandı\n", it->title, pi ? pi->text : L"?");
            continue;
        }
        if (g_page != page) {
            switch_page(page);
            dev_pump(150);
        }
        ri = dev_find(it);
        if (ri < 0)
            break;
        g_dev_pick = pi->id;
        dev_act(it, ri);
        dev_pump(250);
        g_dev_pick = 0;
        fwprintf(f, L"  [menü] %s → %s: çalıştı (sayfa %d, %d profil)\n", it->title, saved[0].text[0] ? pi->text : L"",
                 g_page, g_cfg.nprofiles);
        /* yeniden adlandırma kutusu açıldıysa kapat */
        if (g_ep_rename || g_list_renaming) {
            SetFocus(g_content);
            dev_pump(100);
        }
    }
    g_dev_pick = 0;
}

int settings_dev_uitest(FILE *f, int *fails)
{
    static DevItem items[MAX_ROWS * (MAX_BTN + 1)];
    int page, ni, i, n = 0;
    wchar_t before[1024], after[1024];

    settings_show(0);
    dev_pump(300);
    for (page = 0; page < SETTINGS_PAGE_COUNT; page++) {
        switch_page(page);
        dev_pump(300);
        fwprintf(f, L"\n=== Sayfa %d (%d satır)\n", page, g_nrows);
        dev_edits(f, fails, &n);
        ni = dev_items(items);
        for (i = 0; i < ni; i++) {
            DevItem *it = &items[i];
            int ri, id = it->btn < 0 ? it->id : it->bid;

            if (g_page != page) {
                switch_page(page);
                dev_pump(150);
            }
            if (dev_skip(id)) {
                fwprintf(f, L"  [atla] %s\n", it->title);
                continue;
            }
            ri = dev_find(it);
            if (ri < 0) {
                fwprintf(f, L"  [yok]  %s: satır artık yok (önceki eylem sayfayı değiştirdi)\n", it->title);
                continue;
            }
            n++;
            if (it->btn < 0 && it->type == T_DROP) {
                dev_dropdown(f, fails, it, ri, page);
                continue;
            }
            if (it->btn >= 0 && dev_is_menu(id)) {
                dev_menu(f, it, ri, page);
                continue;
            }
            dev_state(&g_rows[ri], before, 1024);
            g_dev_pick = -1;                   /* düğme bir menü açarsa ilkini seç */
            dev_act(it, ri);
            dev_pump(250);
            g_dev_pick = 0;
            if (app_tune_state(NULL, NULL)) {
                fwprintf(f, L"  [profil bulucu] başladı (%s), durduruluyor\n", it->title);
                app_tune_cancel();
                dev_pump(200);
            }
            if (bypass_running()) {
                fwprintf(f, L"  [koruma] açıldı: %s, kapatılıyor\n", app_state_text());
                app_toggle();
                dev_pump(200);
            }
            ri = g_page == page ? dev_find(it) : -1;
            if (it->btn < 0 && ri >= 0) {
                dev_state(&g_rows[ri], after, 1024);
                fwprintf(f, L"  [anahtar] %s: %s → %s %s\n", it->title, before, after,
                         wcscmp(before, after) ? L"" : L"HATA: değişmedi");
                if (!wcscmp(before, after))
                    (*fails)++;
                dev_act(it, ri);               /* eski haline */
                dev_pump(200);
            } else {
                fwprintf(f, L"  [düğme] %s: çalıştı%s\n", it->title, g_page != page ? L" (başka sayfaya geçti)" : L"");
            }
        }
    }
    return n;
}
#endif
