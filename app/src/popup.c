/*
 * TunnelDPI - açılır liste
 *
 * Etkinleşmeyen (WS_EX_NOACTIVATE) bir pencere + fare yakalama + yerel mesaj
 * döngüsü: TrackPopupMenu gibi modal çalışır, ama uygulamanın renkleriyle
 * çizilir. Dışarı tıklama, Esc ya da yakalamanın kaybı listeyi kapatır.
 */
#include "popup.h"
#include "config.h"
#include "ui.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <math.h>

#define POP_CLASS L"TunnelDPIPopup"
#define ITEM_H    34
#define SEP_H     9
#define PADY      4
#define MAX_H     380
#define MAX_ITEMS 64
#define T_ANIM    1

#ifdef DEV_SHOT
/* Arayüz testi: >0 bu id'yi seç, <0 (-k) k. seçilebilir öğeyi seç,
   DEV_PICK_CANCEL vazgeç. Son açılan menünün öğeleri saklanır. */
#include <limits.h>
int     g_dev_pick;
PopItem g_dev_items[64];
int     g_dev_nitems;
#endif

static struct {
    HWND           w;
    const PopItem *it;
    int            n;
    int            hover;      /* fare ya da klavye ile vurgulanan öğe */
    int            result;
    BOOL           done;
    int            scroll, total_h, view_h;
    float          hv[MAX_ITEMS];
    float          open;
    int            final_y, from_y;
} P;

static int item_h(int i)
{
    return US(P.it[i].flags & PI_SEP ? SEP_H : ITEM_H);
}

static int item_top(int i)
{
    int y = US(PADY), k;

    for (k = 0; k < i; k++)
        y += item_h(k);
    return y - P.scroll;
}

static BOOL selectable(int i)
{
    return i >= 0 && i < P.n && !(P.it[i].flags & (PI_SEP | PI_DISABLED));
}

static int hit(int x, int y)
{
    RECT cr;
    int i, top;

    GetClientRect(P.w, &cr);
    if (x < 0 || x >= cr.right || y < 0 || y >= cr.bottom)
        return -1;
    for (i = 0; i < P.n; i++) {
        top = item_top(i);
        if (y >= top && y < top + item_h(i))
            return selectable(i) ? i : -1;
    }
    return -1;
}

static COLORREF pop_bg(void)
{
    return g_th.dark ? ui_mix(g_th.surface, g_th.raised, 45) : g_th.surface;
}

static void paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mem;
    RECT cr;
    HBITMAP bmp, oldb;
    Paint p;
    int i, pass;
    COLORREF bg = pop_bg();

    GetClientRect(h, &cr);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    oldb = SelectObject(mem, bmp);
    for (pass = 0; pass < 2; pass++) {
        ui_begin(&p, mem);
        if (pass == 1)
            ui_end(&p);
        else
            ui_fill_rect(&p, &cr, bg);
        for (i = 0; i < P.n; i++) {
            const PopItem *it = &P.it[i];
            int top = item_top(i), hh = item_h(i);
            RECT r = { US(4), top, cr.right - US(4), top + hh };

            if (r.bottom < 0 || r.top > cr.bottom)
                continue;
            if (it->flags & PI_SEP) {
                if (pass == 0) {
                    RECT d = { US(12), top + hh / 2, cr.right - US(12), top + hh / 2 + 1 };
                    ui_fill_rect(&p, &d, g_th.rule);
                }
                continue;
            }
            if (pass == 0) {
                COLORREF sel = g_th.dark ? g_th.raised : g_th.active;
                COLORREF hv = g_th.dark ? g_th.raised_hover : g_th.hover;
                COLORREF f = ui_mix(it->flags & PI_CHECK ? sel : bg, hv, (int)(P.hv[i] * 100));
                if (it->flags & PI_CHECK || P.hv[i] > 0.01f)
                    ui_round(&p, (float)r.left, (float)r.top + 1, (float)(r.right - r.left), (float)(hh - 2),
                             (float)US(4), f, f, FALSE);
                if (it->flags & PI_CHECK)
                    ui_round(&p, (float)r.left, (float)(top + hh / 2 - US(8)), (float)US(3), (float)US(16),
                             (float)US(1), g_th.accent, g_th.accent, FALSE);
            } else {
                COLORREF tc = it->flags & PI_DISABLED ? g_th.faint : it->flags & PI_DANGER ? g_th.negative : g_th.text;
                r.left += US(14);
                r.right -= US(10);
                ui_text(&p, it->text, &r, it->flags & PI_CHECK ? FONT_MEDIUM : FONT_BODY, tc,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        if (pass == 0) {
            if (P.total_h > P.view_h) {
                int th = max(US(24), cr.bottom * P.view_h / P.total_h);
                int ty = (cr.bottom - th) * P.scroll / (P.total_h - P.view_h);
                ui_round(&p, (float)(cr.right - US(5)), (float)ty + US(3), (float)US(2), (float)th - US(6),
                         (float)US(1), g_th.control, g_th.control, FALSE);
            }
            ui_end(&p);
        }
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* İlk sonuç geçerlidir: kapanırken fare yakalamasının bırakılması
   WM_CAPTURECHANGED üretir ve seçilen öğeyi "vazgeçildi" ile ezmemeli. */
static void finish(int result)
{
    if (P.done)
        return;
    P.result = result;
    P.done = TRUE;
}

static void scroll_into_view(int i)
{
    int top = item_top(i) + P.scroll, bottom = top + item_h(i);

    if (top - US(PADY) < P.scroll)
        P.scroll = top - US(PADY);
    else if (bottom + US(PADY) > P.scroll + P.view_h)
        P.scroll = bottom + US(PADY) - P.view_h;
    if (P.scroll < 0)
        P.scroll = 0;
}

static void move_hover(int dir)
{
    int i = P.hover, k;

    if (i < 0) {
        for (k = 0; k < P.n; k++)
            if (P.it[k].flags & PI_CHECK)
                i = k;
        if (i < 0)
            i = dir > 0 ? -1 : P.n;
        else
            i -= dir;
    }
    for (k = 0; k < P.n; k++) {
        i += dir;
        if (i < 0)
            i = P.n - 1;
        if (i >= P.n)
            i = 0;
        if (selectable(i))
            break;
    }
    if (selectable(i)) {
        P.hover = i;
        scroll_into_view(i);
    }
}

static LRESULT CALLBACK pop_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int k;

    switch (msg) {
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        k = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k >= 0)
            P.hover = k;
        else if (GET_X_LPARAM(lp) < 0 || GET_Y_LPARAM(lp) < 0)
            P.hover = -1;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        RECT cr;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        GetClientRect(h, &cr);
        if (!PtInRect(&cr, pt))
            finish(0);   /* dışarı tıklandı */
        return 0;
    }
    case WM_LBUTTONUP:
        k = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k >= 0)
            finish(P.it[k].id);
        return 0;
    case WM_MOUSEWHEEL:
        if (P.total_h > P.view_h) {
            P.scroll -= GET_WHEEL_DELTA_WPARAM(wp) * US(ITEM_H) * 3 / WHEEL_DELTA;
            if (P.scroll > P.total_h - P.view_h)
                P.scroll = P.total_h - P.view_h;
            if (P.scroll < 0)
                P.scroll = 0;
        }
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h)
            finish(0);
        return 0;
    case WM_TIMER:
        return 0;   /* animasyon yerel döngüde işlenir */
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void anim(void)
{
    int i;

    for (i = 0; i < P.n && i < MAX_ITEMS; i++) {
        float t = i == P.hover ? 1.0f : 0.0f, d = t - P.hv[i];
        P.hv[i] = fabsf(d) < 0.01f ? t : P.hv[i] + d * 0.35f;
    }
    if (P.open < 1.0f) {
        RECT wr;
        float e;
        P.open += 0.14f;
        if (P.open > 1.0f)
            P.open = 1.0f;
        e = 1.0f - (1.0f - P.open) * (1.0f - P.open) * (1.0f - P.open);
        GetWindowRect(P.w, &wr);
        SetWindowPos(P.w, NULL, wr.left, P.from_y + (int)((P.final_y - P.from_y) * e), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(P.w, NULL, FALSE);
}

int ui_popup(HWND owner, const RECT *anchor, const PopItem *items, int n, BOOL align_right)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    MONITORINFO mi = { sizeof(mi) };
    HDC dc;
    int i, w, h, x, y, tw = 0;
    BOOL above;
    DWORD none = 0xFFFFFFFE;
    int corner = 2;
    MSG m;

    if (n <= 0)
        return 0;
    if (n > MAX_ITEMS)
        n = MAX_ITEMS;
    ui_init(g_cfg.theme);
    memset(&P, 0, sizeof(P));
    P.it = items;
    P.n = n;
    P.hover = -1;

    dc = GetDC(NULL);
    for (i = 0; i < n; i++)
        if (!(items[i].flags & PI_SEP)) {
            int t = ui_text_width(dc, items[i].text, FONT_MEDIUM);
            if (t > tw)
                tw = t;
        }
    ReleaseDC(NULL, dc);
    for (i = 0; i < n; i++)
        P.total_h += item_h(i);
    P.total_h += 2 * US(PADY);

    w = max((int)(anchor->right - anchor->left), tw + US(14 + 10 + 16));
    w = max(US(160), min(w, US(420)));
    h = min(P.total_h, US(MAX_H));
    P.view_h = h;

    GetMonitorInfoW(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    x = align_right ? anchor->right - w : anchor->left;
    if (x + w > mi.rcWork.right - US(4))
        x = mi.rcWork.right - US(4) - w;
    if (x < mi.rcWork.left + US(4))
        x = mi.rcWork.left + US(4);
    above = anchor->bottom + US(4) + h > mi.rcWork.bottom && anchor->top - US(4) - h >= mi.rcWork.top;
    y = above ? anchor->top - US(4) - h : anchor->bottom + US(4);
    P.final_y = y;
    P.from_y = y + (above ? US(8) : -US(8));

    /* seçili öğe görünür olsun */
    for (i = 0; i < n; i++)
        if (items[i].flags & PI_CHECK)
            scroll_into_view(i);

    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = pop_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = POP_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassExW(&wc);
    P.w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, POP_CLASS, L"", WS_POPUP,
                          x, P.from_y, w, h, owner, NULL, g_inst, NULL);
    if (!P.w) {
        ui_shutdown();
        return 0;
    }
    ui_style_window(P.w);
    DwmSetWindowAttribute(P.w, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    DwmSetWindowAttribute(P.w, 34 /* DWMWA_BORDER_COLOR */, &none, sizeof(none));
    ShowWindow(P.w, SW_SHOWNOACTIVATE);
    SetCapture(P.w);
    SetTimer(P.w, T_ANIM, 15, NULL);
#ifdef DEV_SHOT
    int dev_ticks = 0;
    g_dev_nitems = n;
    memcpy(g_dev_items, items, (size_t)n * sizeof(PopItem));
#endif

    /* yerel modal döngü: klavye burada işlenir, diğer mesajlar dağıtılır */
    while (!P.done) {
        if (!GetMessageW(&m, NULL, 0, 0)) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (GetCapture() != P.w) {
            finish(0);
            break;
        }
        if (m.message == WM_TIMER && m.hwnd == P.w) {
            anim();
#ifdef DEV_SHOT
            if (g_dev_pick && ++dev_ticks > 3) {
                int k, s = 0, pick = 0;
                if (g_dev_pick > 0) {
                    pick = g_dev_pick;
                } else if (g_dev_pick != INT_MIN) {
                    for (k = 0; k < P.n; k++)
                        if (selectable(k) && ++s == -g_dev_pick)
                            pick = P.it[k].id;
                }
                finish(pick);
            }
#endif
            continue;
        }
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            switch (m.wParam) {
            case VK_ESCAPE:
            case VK_TAB:
            case VK_MENU:
                finish(0);
                break;
            case VK_UP:
                move_hover(-1);
                break;
            case VK_DOWN:
                move_hover(1);
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (selectable(P.hover))
                    finish(P.it[P.hover].id);
                break;
            }
            continue;
        }
        if (m.message == WM_CHAR || m.message == WM_KEYUP || m.message == WM_SYSKEYUP)
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    KillTimer(P.w, T_ANIM);
    if (GetCapture() == P.w)
        ReleaseCapture();
    DestroyWindow(P.w);
    P.w = NULL;
    UnregisterClassW(POP_CLASS, g_inst);
    ui_shutdown();
    return P.result;
}

/* ---------------- açıklama baloncuğu ---------------- */

#define BUB_CLASS L"TunnelDPIBubble"
#define BUB_W     340
#define BUB_PAD   16

static struct {
    HWND           w;
    const wchar_t *title, *text;
    BOOL           done;
    float          open;
    int            final_y, from_y, title_h;
} B;

static void bub_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps), mem;
    RECT cr, t;
    HBITMAP bmp, oldb;
    Paint p;

    GetClientRect(h, &cr);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    oldb = SelectObject(mem, bmp);
    ui_begin(&p, mem);
    ui_fill_rect(&p, &cr, pop_bg());
    t.left = US(BUB_PAD) + US(26);
    t.right = cr.right - US(BUB_PAD);
    t.top = US(BUB_PAD) - US(1);
    t.bottom = t.top + B.title_h;
    {
        RECT ic = { US(BUB_PAD), t.top, US(BUB_PAD) + US(20), t.top + US(20) };
        ui_icon(&p, IC_INFO, &ic, FONT_ICON_SM, g_th.primary);
    }
    ui_end(&p);
    ui_text(&p, B.title, &t, FONT_MEDIUM, g_th.text, DT_LEFT | DT_TOP | DT_WORDBREAK);
    t.left = US(BUB_PAD);
    t.top = t.bottom + US(8);
    t.bottom = cr.bottom - US(BUB_PAD);
    ui_text(&p, B.text, &t, FONT_DESC, g_th.muted, DT_LEFT | DT_TOP | DT_WORDBREAK);
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK bub_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        bub_paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        B.done = TRUE;
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)lp != h)
            B.done = TRUE;
        return 0;
    case WM_TIMER:
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void ui_bubble(HWND owner, const RECT *anchor, const wchar_t *title, const wchar_t *text)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    MONITORINFO mi = { sizeof(mi) };
    DWORD none = 0xFFFFFFFE;
    int corner = 2, w, h, x, y, th;
    BOOL above;
    HDC dc;
    MSG m;

    if (!text || !text[0])
        return;
    ui_init(g_cfg.theme);
    memset(&B, 0, sizeof(B));
    B.title = title && title[0] ? title : APP_NAME;
    B.text = text;
    w = US(BUB_W);
    dc = GetDC(NULL);
    B.title_h = max(US(20), ui_text_height(dc, B.title, FONT_MEDIUM, w - 2 * US(BUB_PAD) - US(26)));
    th = ui_text_height(dc, text, FONT_DESC, w - 2 * US(BUB_PAD));
    ReleaseDC(NULL, dc);
    h = US(BUB_PAD) * 2 + B.title_h + US(8) + th;

    GetMonitorInfoW(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    x = anchor->left - US(BUB_PAD);
    if (x + w > mi.rcWork.right - US(8))
        x = mi.rcWork.right - US(8) - w;
    if (x < mi.rcWork.left + US(8))
        x = mi.rcWork.left + US(8);
    above = anchor->bottom + US(6) + h > mi.rcWork.bottom;
    y = above ? anchor->top - US(6) - h : anchor->bottom + US(6);
    B.final_y = y;
    B.from_y = y + (above ? US(6) : -US(6));

    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = bub_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = BUB_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassExW(&wc);
    B.w = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED, BUB_CLASS, L"",
                          WS_POPUP, x, B.from_y, w, h, owner, NULL, g_inst, NULL);
    if (!B.w) {
        ui_shutdown();
        return;
    }
    SetLayeredWindowAttributes(B.w, 0, 0, LWA_ALPHA);
    ui_style_window(B.w);
    DwmSetWindowAttribute(B.w, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    DwmSetWindowAttribute(B.w, 34 /* DWMWA_BORDER_COLOR */, &none, sizeof(none));
    ShowWindow(B.w, SW_SHOWNOACTIVATE);
    SetCapture(B.w);
    SetTimer(B.w, T_ANIM, 15, NULL);

    while (!B.done) {
        if (!GetMessageW(&m, NULL, 0, 0)) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (GetCapture() != B.w)
            break;
        if (m.message == WM_TIMER && m.hwnd == B.w) {
#ifdef DEV_SHOT
            if (g_dev_pick && B.open >= 1.0f)
                B.done = TRUE;
#endif
            if (B.open < 1.0f) {
                float e;
                B.open = min(1.0f, B.open + 0.12f);
                e = 1.0f - (1.0f - B.open) * (1.0f - B.open) * (1.0f - B.open);
                SetLayeredWindowAttributes(B.w, 0, (BYTE)(255 * e), LWA_ALPHA);
                SetWindowPos(B.w, NULL, x, B.from_y + (int)((B.final_y - B.from_y) * e), 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            continue;
        }
        if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) {
            B.done = TRUE;
            continue;
        }
        if (m.message == WM_CHAR || m.message == WM_KEYUP || m.message == WM_SYSKEYUP)
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    KillTimer(B.w, T_ANIM);
    if (GetCapture() == B.w)
        ReleaseCapture();
    DestroyWindow(B.w);
    B.w = NULL;
    UnregisterClassW(BUB_CLASS, g_inst);
    ui_shutdown();
}
