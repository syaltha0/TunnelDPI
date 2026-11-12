/*
 * TunnelDPI - hızlı panel (tepsi simgesine sağ tık)
 *
 * Bilerek sade: büyük bir aç/kapat düğmesi, durum ve bağlantı satırı. Diğer
 * her şey ayar penceresinde ya da ⋯ menüsünde. Animasyonlar:
 *  - panel aşağıdan kayıp belirerek açılır
 *  - düğme rengi açık/kapalı arasında yumuşakça geçer, basınca hafifçe küçülür
 *  - açılıp kapanınca düğmeden dışa doğru bir dalga yayılır
 *  - koruma başlarken düğmenin çevresinde dönen bir yay görünür
 *  - açıkken düğmenin çevresinde çok hafif bir "nefes" halkası vardır
 * Panel odağı kaybedince kapanır ve tüm kaynaklarını bırakır.
 */
#include "flyout.h"
#include "block.h"
#include "i18n.h"
#include "config.h"
#include "icons.h"
#include "nettest.h"
#include "health.h"
#include "popup.h"
#include "settings.h"
#include "ui.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <stdio.h>
#include <math.h>

#define FLY_CLASS L"TunnelDPIFlyout"
#define FLY_W     280
#define PADX      16
#define BTN_R     52            /* aç/kapat düğmesi yarıçapı */
#define T_ANIM    1

enum { H_NONE = -1, H_POWER = 0, H_GEAR, H_MORE, H_COUNT };

static HWND  g_fly;
static int   g_hover = H_NONE, g_press = H_NONE;
static BOOL  g_in_menu;
static RECT  g_r[H_COUNT];
static int   g_cx, g_cy, g_text_y, g_height;
static float g_hv[H_COUNT];      /* üzerine gelme 0..1 */
static float g_on;               /* açık rengine geçiş 0..1 */
static float g_pressv;           /* basılı küçülme 0..1 */
static float g_ripple = 1.0f;    /* dalga ilerlemesi; 1 = yok */
static float g_spin;             /* yükleniyor yayının açısı */
static float g_breath;           /* nefes halkası evresi */
static float g_open;             /* açılış 0..1 */
static int   g_final_y;
static BOOL  g_animating;

/* ---------------- yerleşim ---------------- */

static void layout(void)
{
    int x1 = US(FLY_W - PADX), y = US(12);

    SetRect(&g_r[H_MORE], x1 - US(28), y, x1, y + US(28));
    SetRect(&g_r[H_GEAR], x1 - US(60), y, x1 - US(32), y + US(28));
    y += US(28) + US(22);
    g_cx = US(FLY_W) / 2;
    g_cy = y + US(BTN_R);
    SetRect(&g_r[H_POWER], g_cx - US(BTN_R), g_cy - US(BTN_R), g_cx + US(BTN_R), g_cy + US(BTN_R));
    y += US(BTN_R * 2) + US(18);
    g_text_y = y;
    y += US(40 + 22 + 18) + US(16);
    g_height = y;
}

/* ---------------- çizim ---------------- */

static float ease_out(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

static void paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mem;
    RECT cr, t;
    HBITMAP bmp, oldb;
    Paint p;
    int st = app_state(), i;
    const NetTest *nt = nettest_get();
    COLORREF on_c = st == ICON_ERROR ? g_th.negative : g_th.primary;
    COLORREF fill, icon, ring;
    float r = US(BTN_R) * (1.0f - 0.05f * g_pressv);
    wchar_t buf[256], age[48];

    GetClientRect(h, &cr);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    oldb = SelectObject(mem, bmp);

    fill = ui_mix(g_th.raised, on_c, (int)(g_on * 100));
    fill = ui_mix(fill, g_on > 0.5f ? g_th.primary_hover : g_th.raised_hover, (int)(g_hv[H_POWER] * 60));
    icon = ui_mix(g_th.muted, g_th.on_primary, (int)(g_on * 100));
    ring = ui_mix(g_th.control, on_c, (int)(g_on * 100));

    /* --- şekiller --- */
    ui_begin(&p, mem);
    ui_fill_rect(&p, &cr, g_th.surface);
    for (i = H_GEAR; i < H_COUNT; i++)
        if (g_hv[i] > 0.01f)
            ui_round(&p, (float)g_r[i].left, (float)g_r[i].top, (float)(g_r[i].right - g_r[i].left),
                     (float)(g_r[i].bottom - g_r[i].top), (float)US(6),
                     ui_mix(g_th.surface, g_th.hover, (int)(g_hv[i] * 100)), 0, FALSE);

    /* açıkken nefes halkası */
    if (g_on > 0.01f && st == ICON_ON) {
        float b = 0.5f - 0.5f * cosf(g_breath);
        ui_circle_alpha(&p, (float)g_cx, (float)g_cy, r + US(6) + US(4) * b, on_c, (int)(28 * g_on * (0.6f + 0.4f * b)));
    }
    /* aç/kapat dalgası */
    if (g_ripple < 1.0f) {
        float e = ease_out(g_ripple);
        ui_ring(&p, (float)g_cx, (float)g_cy, r + US(4) + US(46) * e, on_c, (int)(150 * (1.0f - e)), (float)US(3) * (1.0f - e) + 1);
    }
    ui_circle(&p, (float)g_cx, (float)g_cy, r, fill);
    if (g_on < 0.99f)
        ui_ring(&p, (float)g_cx, (float)g_cy, r - 0.5f, ring, (int)(255 * (1.0f - g_on)), 1.0f);
    /* başlarken dönen yay */
    if (st == ICON_BUSY)
        ui_arc(&p, (float)g_cx, (float)g_cy, r + US(7), g_spin, 90.0f, g_th.primary, (float)US(3));
    ui_end(&p);

    /* --- metinler --- */
    t = g_r[H_GEAR];
    t.left = US(PADX);
    t.right = g_r[H_GEAR].left - US(8);
    ui_text(&p, APP_NAME, &t, FONT_MEDIUM, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui_icon(&p, IC_SETTINGS, &g_r[H_GEAR], FONT_ICON, g_hv[H_GEAR] > 0.5f ? g_th.text : g_th.muted);
    ui_icon(&p, IC_MORE, &g_r[H_MORE], FONT_ICON, g_hv[H_MORE] > 0.5f ? g_th.text : g_th.muted);
    ui_icon(&p, IC_POWER, &g_r[H_POWER], FONT_ICON_LG, icon);

    SetRect(&t, US(PADX), g_text_y, US(FLY_W - PADX), g_text_y + US(40));
    ui_text(&p, st == ICON_ON ? tr(L"Açık") : st == ICON_ERROR ? tr(L"Hata") : st == ICON_BUSY ? tr(L"Başlıyor")
                                                                : tr(L"Kapalı"),
            &t, FONT_STATE, g_th.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    t.top = t.bottom;
    t.bottom = t.top + US(22);
    if (st == ICON_ERROR && app_last_error()[0]) {
        wcsncpy(buf, app_last_error(), 255);
        buf[255] = 0;
        for (i = 0; buf[i]; i++)
            if (buf[i] == L'\n' || buf[i] == L'\r') buf[i] = L' ';
        ui_text(&p, buf, &t, FONT_DESC, g_th.negative, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        int ti, tn;
        if (app_tune_state(&ti, &tn))
            _snwprintf(buf, 256, tr(L"Profil aranıyor %d/%d · %s"), ti + 1 > tn ? tn : ti + 1, tn, app_tune_name());
        else if (app_paused())
            _snwprintf(buf, 256, tr(L"Duraklatıldı · %d dk sonra açılır"), app_paused());
        else if (bypass_running())
            _snwprintf(buf, 256, tr(L"Profil: %s"), g_cfg.active);
        else
            wcscpy(buf, tr(L"Açmak için düğmeye bas"));
        ui_text(&p, buf, &t, FONT_DESC, g_th.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    t.top = t.bottom;
    t.bottom = t.top + US(18);
    {
        /* koruma açıkken izlenen sitelerin son durumu, yoksa son bağlantı testi */
        const Health *hl = health_get();
        COLORREF c = g_th.faint;

        buf[0] = 0;
        if (bypass_running() && hl->when && hl->n) {
            health_summary(buf, 256);
            if (health_ok_count() < hl->n)
                c = g_th.negative;
        } else {
            nettest_age(age, 48);
            if (nt->state == NT_OK)
                _snwprintf(buf, 256, tr(L"%s · %lu ms · %s"), nt->host, nt->ms, age);
            else if (nt->state == NT_FAIL)
                _snwprintf(buf, 256, L"%s · %s", nt->host, nettest_error_text());
            else if (nt->state == NT_RUNNING)
                _snwprintf(buf, 256, tr(L"%s test ediliyor…"), nt->host);
            if (nt->state == NT_FAIL)
                c = g_th.negative;
        }
        ui_text(&p, buf, &t, FONT_CAPTION, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* ---------------- animasyon ---------------- */

static float approach(float v, float target, float k, BOOL *busy)
{
    float d = target - v;

    if (fabsf(d) < 0.005f)
        return target;
    *busy = TRUE;
    return v + d * k;
}

static void anim_kick(void)
{
    if (g_fly && !g_animating) {
        g_animating = TRUE;
        SetTimer(g_fly, T_ANIM, 15, NULL);
    }
}

static void anim_step(void)
{
    BOOL busy = FALSE;
    int i, st = app_state();

    for (i = 0; i < H_COUNT; i++)
        g_hv[i] = approach(g_hv[i], i == g_hover ? 1.0f : 0.0f, 0.3f, &busy);
    g_on = approach(g_on, st == ICON_ON || st == ICON_ERROR ? 1.0f : 0.0f, 0.18f, &busy);
    g_pressv = approach(g_pressv, g_press == H_POWER && g_hover == H_POWER ? 1.0f : 0.0f, 0.4f, &busy);
    if (g_ripple < 1.0f) {
        g_ripple += 0.035f;
        busy = TRUE;
    }
    if (st == ICON_BUSY) {
        g_spin = fmodf(g_spin + 9.0f, 360.0f);
        busy = TRUE;
    }
    if (st == ICON_ON) {
        g_breath = fmodf(g_breath + 0.045f, 6.2832f);
        busy = TRUE;                        /* panel açıkken sürer; kapanınca zamanlayıcı da gider */
    }
    if (g_open < 1.0f) {
        RECT wr;
        float e;
        g_open += 0.08f;
        if (g_open > 1.0f)
            g_open = 1.0f;
        e = ease_out(g_open);
        GetWindowRect(g_fly, &wr);
        SetWindowPos(g_fly, NULL, wr.left, g_final_y + (int)((1.0f - e) * US(16)), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(g_fly, 0, (BYTE)(255 * e), LWA_ALPHA);
        busy = TRUE;
    }
    InvalidateRect(g_fly, NULL, FALSE);
    if (!busy) {
        KillTimer(g_fly, T_ANIM);
        g_animating = FALSE;
    }
}

/* ---------------- etkileşim ---------------- */

static int hit(int x, int y)
{
    POINT pt = { x, y };
    int i, dx = x - g_cx, dy = y - g_cy;

    if (dx * dx + dy * dy <= US(BTN_R) * US(BTN_R))
        return H_POWER;
    for (i = H_GEAR; i < H_COUNT; i++)
        if (PtInRect(&g_r[i], pt))
            return i;
    return H_NONE;
}

static PopItem g_pi[16];
static int     g_npi;

static void pi_add(int id, const wchar_t *t, int flags)
{
    if (g_npi >= 16)
        return;
    g_pi[g_npi].id = id;
    g_pi[g_npi].flags = flags;
    wcsncpy(g_pi[g_npi].text, t ? t : L"", 95);
    g_pi[g_npi].text[95] = 0;
    g_npi++;
}

static int menu_at(const RECT *r)
{
    RECT a = *r;
    POINT tl = { a.left, a.top }, br = { a.right, a.bottom };
    int cmd;

    ClientToScreen(g_fly, &tl);
    ClientToScreen(g_fly, &br);
    SetRect(&a, tl.x, tl.y, br.x, br.y);
    g_in_menu = TRUE;
    cmd = ui_popup(g_fly, &a, g_pi, g_npi, TRUE);
    g_in_menu = FALSE;
    return cmd;
}

enum { M_TEST = 1, M_PAUSE, M_RESUME, M_ADS, M_SETTINGS, M_QUIT, M_SITE, M_RECENT = 20 };

static void more_menu(void)
{
    int cmd;

    g_npi = 0;
    /* son kullanılan profiller: tek tıkla geçiş */
    {
        int i, n = 0;
        for (i = 0; i < MAX_RECENT; i++)
            if (g_cfg.recent[i][0] && config_find(&g_cfg, g_cfg.recent[i]) >= 0) {
                pi_add(M_RECENT + i, g_cfg.recent[i], !_wcsicmp(g_cfg.recent[i], g_cfg.active) ? PI_CHECK : 0);
                n++;
            }
        if (n > 1)
            pi_add(0, NULL, PI_SEP);
        else
            g_npi = 0;   /* tek profil varsa göstermeye değmez */
    }
    pi_add(M_SITE, tr(L"Bir site açılmıyor…"), 0);
    pi_add(M_TEST, tr(L"Bağlantıyı test et"), 0);
    if (bypass_running())
        pi_add(M_PAUSE, tr(L"30 dakika duraklat"), 0);
    else if (app_paused())
        pi_add(M_RESUME, tr(L"Duraklatmayı bitir"), 0);
    pi_add(M_ADS, tr(L"Reklamları engelle"), g_cfg.block_on ? PI_CHECK : 0);
    pi_add(0, NULL, PI_SEP);
    pi_add(M_SETTINGS, tr(L"Ayarlar"), 0);
    pi_add(M_QUIT, tr(L"Çıkış"), PI_DANGER);
    cmd = menu_at(&g_r[H_MORE]);
    switch (cmd) {
    case M_TEST:
        nettest_start();
        break;
    case M_PAUSE:
        app_pause(30);
        break;
    case M_RESUME:
        app_toggle();
        g_ripple = 0.0f;
        break;
    case M_ADS:
        g_cfg.block_on = !g_cfg.block_on;
        if (g_cfg.block_on && !g_cfg.block_lists[0])
            block_set(&g_cfg, 0, TRUE);
        config_save(&g_cfg);
        bypass_restart_if_running();
        settings_on_config();
        break;
    case M_SETTINGS:
    case M_SITE:
        flyout_close();
        settings_show(SETTINGS_PAGE_HOME);
        return;
    default:
        if (cmd >= M_RECENT && cmd < M_RECENT + MAX_RECENT)
            app_use_profile(g_cfg.recent[cmd - M_RECENT]);
        break;
    case M_QUIT:
        flyout_close();
        app_quit();
        return;
    }
    flyout_refresh();
}

static void activate(int what)
{
    switch (what) {
    case H_POWER:
        if (app_state() != ICON_BUSY) {
            app_toggle();
            g_ripple = 0.0f;
        }
        break;
    case H_GEAR:
        flyout_close();
        settings_show(SETTINGS_PAGE_HOME);
        return;
    case H_MORE:
        more_menu();
        return;
    }
    flyout_refresh();
}

static LRESULT CALLBACK fly_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int k;

    switch (msg) {
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == T_ANIM)
            anim_step();
        return 0;
    case WM_ACTIVATE:
#ifndef DEV_SHOT
        if (LOWORD(wp) == WA_INACTIVE && !g_in_menu)
            PostMessageW(h, WM_CLOSE, 0, 0);
#endif
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            PostMessageW(h, WM_CLOSE, 0, 0);
        else if (wp == VK_SPACE || wp == VK_RETURN)
            activate(H_POWER);
        return 0;
    case WM_MOUSEMOVE:
        k = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k != g_hover) {
            g_hover = k;
            anim_kick();
        }
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover != H_NONE) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSELEAVE:
        g_hover = H_NONE;
        anim_kick();
        return 0;
    case WM_LBUTTONDOWN:
        g_press = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        anim_kick();
        return 0;
    case WM_LBUTTONUP:
        k = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k != H_NONE && k == g_press)
            activate(k);
        g_press = H_NONE;
        anim_kick();
        return 0;
    case WM_CLOSE:
        flyout_close();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------------- dış arayüz ---------------- */

void flyout_show(int ax, int ay)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    MONITORINFO mi = { sizeof(mi) };
    POINT pt = { ax, ay };
    int w, x, st;
    BOOL bottom;
    DWORD none = 0xFFFFFFFE; /* DWMWA_COLOR_NONE: gölge var, kenarlık yok */

    if (g_fly) {
        flyout_close();
        return;
    }
    ui_init(g_cfg.theme);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = fly_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = FLY_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassExW(&wc);

    layout();
    w = US(FLY_W);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    /* görev çubuğu hangi kenardaysa ona yapışık, imleç hizasında */
    x = ax - w / 2;
    if (x + w > mi.rcWork.right - US(12))
        x = mi.rcWork.right - US(12) - w;
    if (x < mi.rcWork.left + US(12))
        x = mi.rcWork.left + US(12);
    bottom = ay > (mi.rcWork.top + mi.rcWork.bottom) / 2;
    g_final_y = bottom ? mi.rcWork.bottom - US(12) - g_height : mi.rcWork.top + US(12);

    st = app_state();
    g_hover = g_press = H_NONE;
    memset(g_hv, 0, sizeof(g_hv));
    g_on = st == ICON_ON || st == ICON_ERROR ? 1.0f : 0.0f;
    g_pressv = 0.0f;
    g_ripple = 1.0f;
    g_open = 0.0f;
    g_fly = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, FLY_CLASS, APP_NAME, WS_POPUP,
                            x, g_final_y + US(16), w, g_height, g_hwnd, NULL, g_inst, NULL);
    if (!g_fly) {
        ui_shutdown();
        return;
    }
    SetLayeredWindowAttributes(g_fly, 0, 0, LWA_ALPHA);
    ui_style_window(g_fly);
    DwmSetWindowAttribute(g_fly, 34 /* DWMWA_BORDER_COLOR */, &none, sizeof(none));
    ShowWindow(g_fly, SW_SHOW);
    SetForegroundWindow(g_fly);
    SetFocus(g_fly);
    anim_kick();

    /* sonuç eskiyse yeniden ölç: koruma açıkken siteler, değilse bağlantı testi */
    {
        const NetTest *nt = nettest_get();
        const Health *hl = health_get();
        if (bypass_running() && (!hl->when || GetTickCount() - hl->when > 120000))
            health_start();
        else if (nt->state == NT_NONE || (nt->state != NT_RUNNING && GetTickCount() - nt->when > 60000))
            nettest_start();
    }
}

void flyout_close(void)
{
    HWND h = g_fly;

    if (!h)
        return;
    g_fly = NULL;
    g_animating = FALSE;
    DestroyWindow(h);
    UnregisterClassW(FLY_CLASS, g_inst);
    ui_shutdown();
    trim_memory();
}

void flyout_refresh(void)
{
    if (g_fly) {
        InvalidateRect(g_fly, NULL, FALSE);
        anim_kick();
    }
}

#ifdef DEV_SHOT
/* Arayüz testi: aç/kapat düğmesi, dişli ve ⋯ menüsünün her öğesi (Çıkış hariç) */
#include <limits.h>
extern int     g_dev_pick;
extern PopItem g_dev_items[64];
extern int     g_dev_nitems;

static void fdev_pump(int ms)
{
    DWORD t = GetTickCount();
    MSG m;

    while (GetTickCount() - t < (DWORD)ms) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    }
}

static void fdev_open(void)
{
    if (!g_fly)
        flyout_show(GetSystemMetrics(SM_CXSCREEN) - 200, GetSystemMetrics(SM_CYSCREEN));
    fdev_pump(300);
}

int flyout_dev_uitest(FILE *f, int *fails)
{
    PopItem saved[64];
    int i, sn, n = 0;

    fwprintf(f, L"\n=== Hızlı panel\n");
    fdev_open();
    if (!g_fly) {
        fwprintf(f, L"  HATA: panel açılmadı\n");
        (*fails)++;
        return 0;
    }
    /* aç/kapat: yönetici izni olmadan motor başlayamaz, durum Hata olmalı; sonra kapanmalı */
    activate(H_POWER);
    fdev_pump(600);
    fwprintf(f, L"  [düğme] aç: durum \"%s\" (koruma isteği: %s)\n", app_state_text(), bypass_running() ? L"açık" : L"kapalı");
    if (!bypass_running()) {
        fwprintf(f, L"  HATA: aç düğmesi korumayı açmadı\n");
        (*fails)++;
    }
    fdev_open();
    activate(H_POWER);
    fdev_pump(400);
    fwprintf(f, L"  [düğme] kapat: durum \"%s\"\n", app_state_text());
    if (bypass_running()) {
        fwprintf(f, L"  HATA: kapat düğmesi korumayı kapatmadı\n");
        (*fails)++;
    }
    n += 2;

    /* ⋯ menüsü: önce öğeleri öğren */
    fdev_open();
    g_dev_pick = INT_MIN;
    activate(H_MORE);
    fdev_pump(100);
    sn = g_dev_nitems;
    memcpy(saved, g_dev_items, sizeof(saved));
    for (i = 0; i < sn; i++) {
        const PopItem *pi = &saved[i];
        BOOL ads = g_cfg.block_on;

        if (pi->flags & (PI_SEP | PI_DISABLED))
            continue;
        if (pi->id == M_QUIT) {
            fwprintf(f, L"  [menü] %s: en sonda denenecek\n", pi->text);
            continue;
        }
        fdev_open();
        g_dev_pick = pi->id;
        activate(H_MORE);
        fdev_pump(400);
        g_dev_pick = 0;
        n++;
        if (pi->id == M_ADS) {
            fwprintf(f, L"  [menü] %s: reklam engelleme %s → %s %s\n", pi->text, ads ? L"açık" : L"kapalı",
                     g_cfg.block_on ? L"açık" : L"kapalı", ads != g_cfg.block_on ? L"" : L"HATA: değişmedi");
            if (ads == g_cfg.block_on)
                (*fails)++;
            /* eski haline */
            fdev_open();
            g_dev_pick = M_ADS;
            activate(H_MORE);
            fdev_pump(300);
            g_dev_pick = 0;
        } else if (pi->id == M_SETTINGS || pi->id == M_SITE) {
            BOOL ok = FindWindowW(L"TunnelDPIWindow", NULL) != NULL;
            fwprintf(f, L"  [menü] %s: ayar penceresi %s\n", pi->text, ok ? L"açıldı" : L"HATA: açılmadı");
            if (!ok)
                (*fails)++;
            settings_close();
        } else if (pi->id >= M_RECENT) {
            BOOL ok = !_wcsicmp(g_cfg.active, pi->text);
            fwprintf(f, L"  [menü] son profil %s: %s\n", pi->text, ok ? L"seçildi" : L"HATA: seçilmedi");
            if (!ok)
                (*fails)++;
        } else {
            fwprintf(f, L"  [menü] %s: çalıştı (durum \"%s\")\n", pi->text, app_state_text());
        }
        if (bypass_running())
            app_toggle();
    }

    /* dişli: ayarları açar */
    fdev_open();
    activate(H_GEAR);
    fdev_pump(300);
    {
        BOOL ok = FindWindowW(L"TunnelDPIWindow", NULL) != NULL;
        fwprintf(f, L"  [düğme] dişli: ayar penceresi %s\n", ok ? L"açıldı" : L"HATA: açılmadı");
        if (!ok)
            (*fails)++;
        settings_close();
    }
    n++;
    return n;
}

/* Çıkış: ⋯ → Çıkış (testin son adımı) */
void flyout_dev_quit(void)
{
    fdev_open();
    g_dev_pick = M_QUIT;
    activate(H_MORE);
    g_dev_pick = 0;
}
#endif
