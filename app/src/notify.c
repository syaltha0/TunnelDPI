/* Ön planda tema uyumlu kart; arka planda Windows'un sessiz saat kuralları. */
#include "notify.h"
#include "config.h"
#include "ui.h"
#include "i18n.h"
#include "settings.h"
#include <windowsx.h>
#include <dwmapi.h>

#define NOTICE_CLASS L"TunnelDPINotice"
static struct { unsigned hash; ULONGLONG tick; BOOL used; } recent[8];
static unsigned next_recent;
static struct {
    HWND window;
    wchar_t title[128], body[1024];
    BOOL error, ui, hover, hover_close;
    DWORD remaining, duration, last, shown;
    int final_y;
    RECT close, action;
} N;

#define NOTICE_W     360
#define SLIDE_MS     220
#define PAD          16
#define TEXT_X       64

BOOL notify_allow(const wchar_t *title, const wchar_t *body, BOOL error, ULONGLONG now)
{
    unsigned hash = error ? 1u : 0u, i;
    const wchar_t *s;
    for (s = title; *s; s++) hash = (hash ^ (unsigned)*s) * 16777619u;
    hash = (hash ^ 0xFFFFu) * 16777619u;
    for (s = body; *s; s++) hash = (hash ^ (unsigned)*s) * 16777619u;
    for (i = 0; i < 8; i++)
        if (recent[i].used && recent[i].hash == hash && now - recent[i].tick < (error ? 60000u : 15000u)) return FALSE;
    i = next_recent++ % 8;
    recent[i].hash = hash; recent[i].tick = now; recent[i].used = TRUE;
    return TRUE;
}

void notify_close(void)
{
    if (N.window) DestroyWindow(N.window);
}

/* Yerleşim: üstte uygulama adı ve kapat; altında renkli simge, başlık, metin, eylem bağlantısı */
static int body_height(HDC dc, int width)
{
    return ui_text_height(dc, N.body, FONT_DESC, width - US(TEXT_X + PAD));
}

static void paint(HWND h, HDC dc)
{
    RECT r, t;
    Paint p;
    COLORREF accent = N.error ? g_th.negative : g_th.primary;
    COLORREF bg = N.hover ? ui_mix(g_th.surface, g_th.raised, 50) : g_th.surface;
    HICON ico;
    int th, bh;

    GetClientRect(h, &r);
    ui_begin(&p, dc);
    ui_fill_rect(&p, &r, bg);
    /* simge: yumuşak renkli daire */
    ui_circle_alpha(&p, (float)US(PAD + 20), (float)US(44 + 20), (float)US(20), accent, 38);
    if (N.hover_close) {
        RECT c = N.close;
        ui_round(&p, (float)c.left, (float)c.top, (float)(c.right - c.left), (float)(c.bottom - c.top),
                 (float)US(4), g_th.hover, 0, FALSE);
    }
    /* kalan süre: altta ince çizgi */
    t = r;
    t.top = r.bottom - US(2);
    t.right = MulDiv(r.right, (int)N.remaining, (int)N.duration);
    ui_fill_rect(&p, &t, ui_mix(bg, accent, 55));
    ui_end(&p);

    ico = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, US(16), US(16), 0);
    if (ico) {
        DrawIconEx(dc, US(PAD), US(14), ico, US(16), US(16), 0, NULL, DI_NORMAL);
        DestroyIcon(ico);
    }
    SetRect(&t, US(PAD + 24), US(10), r.right - US(48), US(34));
    ui_text(&p, APP_NAME, &t, FONT_CAPTION, g_th.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetRect(&N.close, r.right - US(40), US(8), r.right - US(10), US(36));
    ui_icon(&p, 0xE8BB, &N.close, FONT_ICON_SM, N.hover_close ? g_th.text : g_th.muted);

    SetRect(&t, US(PAD), US(44), US(PAD + 40), US(84));
    ui_icon(&p, N.error ? IC_ERROR : IC_SHIELD, &t, FONT_ICON, accent);

    th = ui_text_height(dc, N.title, FONT_MEDIUM, r.right - US(TEXT_X + PAD));
    SetRect(&t, US(TEXT_X), US(44), r.right - US(PAD), US(44) + th);
    ui_text(&p, N.title, &t, FONT_MEDIUM, g_th.text, DT_LEFT | DT_TOP | DT_WORDBREAK);
    bh = body_height(dc, r.right);
    SetRect(&t, US(TEXT_X), t.bottom + US(4), r.right - US(PAD), t.bottom + US(4) + bh);
    if (t.bottom > r.bottom - US(44))
        t.bottom = r.bottom - US(44);
    ui_text(&p, N.body, &t, FONT_DESC, g_th.muted, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    SetRect(&N.action, US(TEXT_X), r.bottom - US(40), r.right - US(PAD), r.bottom - US(14));
    ui_text(&p, N.error ? tr(L"Ayrıntıları aç →") : tr(L"Durum sayfasını aç →"), &N.action,
            FONT_MEDIUM, accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK notice_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        /* çift tampon: süre çizgisi her turda yenilenirken titremesin */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps), mem = CreateCompatibleDC(dc);
        RECT r;
        HBITMAP bmp, old;
        GetClientRect(h, &r);
        bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        old = SelectObject(mem, bmp);
        paint(h, mem);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER: {
        POINT pt;
        RECT r;
        DWORD now = GetTickCount(), elapsed = now - N.last, since = now - N.shown;
        N.last = now;
        GetCursorPos(&pt);
        GetWindowRect(h, &r);
        if (since < SLIDE_MS) {
            /* yukarı kayarak belirir */
            float k = (float)since / SLIDE_MS, e = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
            SetLayeredWindowAttributes(h, 0, (BYTE)(e * 255), LWA_ALPHA);
            SetWindowPos(h, NULL, r.left, N.final_y + (int)((1.0f - e) * US(24)), 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (since - elapsed < SLIDE_MS) {
            SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
            SetWindowPos(h, NULL, r.left, N.final_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetTimer(h, 1, 80, NULL);
        }
        if (!PtInRect(&r, pt)) {
            if (N.hover || N.hover_close) {
                N.hover = N.hover_close = FALSE;
            }
            if (elapsed >= N.remaining) { notify_close(); return 0; }
            N.remaining -= elapsed;
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        BOOL hc = PtInRect(&N.close, pt);
        if (!N.hover || hc != N.hover_close) {
            N.hover = TRUE;
            N.hover_close = hc;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        BOOL details = !PtInRect(&N.close, pt);
        BOOL error = N.error;
        notify_close();
        if (details) settings_show(error ? SETTINGS_PAGE_LOG : SETTINGS_PAGE_HOME);
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE;
    case WM_DPICHANGED:
        notify_close(); return 0;
    case WM_DESTROY:
        N.window = NULL;
        if (N.ui) { N.ui = FALSE; ui_shutdown(); }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void show_card(const wchar_t *title, const wchar_t *body, BOOL error, HWND foreground);

void notify_show(const wchar_t *title, const wchar_t *body, BOOL error)
{
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (!notify_allow(title, body, error, GetTickCount64())) return;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (pid != GetCurrentProcessId()) {
        NOTIFYICONDATAW nid = {sizeof(nid)};
        nid.hWnd = g_hwnd; nid.uID = 1; nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = (error ? NIIF_ERROR : NIIF_USER) | NIIF_RESPECT_QUIET_TIME | NIIF_NOSOUND | NIIF_LARGE_ICON;
        nid.hBalloonIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
        wcsncpy(nid.szInfoTitle, title, 63); nid.szInfoTitle[63] = 0;
        wcsncpy(nid.szInfo, body, 255); nid.szInfo[255] = 0;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        return;
    }
    show_card(title, body, error, foreground);
}

static void show_card(const wchar_t *title, const wchar_t *body, BOOL error, HWND foreground)
{
    MONITORINFO mi = {sizeof(mi)};
    WNDCLASSEXW wc = {sizeof(wc)};
    int width, height, corner = 2;

    if (N.window && N.error && !error) return;
    notify_close();
    ui_init(g_cfg.theme); N.ui = TRUE;
    wc.lpfnWndProc = notice_proc; wc.hInstance = g_inst; wc.lpszClassName = NOTICE_CLASS;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassExW(&wc);
    N.error = error;
    wcsncpy(N.title, title, 127); N.title[127] = 0;
    wcsncpy(N.body, body, 1023); N.body[1023] = 0;
    N.duration = N.remaining = error ? 12000 : 6500;
    N.last = N.shown = GetTickCount();
    N.hover = N.hover_close = FALSE;
    GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &mi);
    width = min(US(NOTICE_W), mi.rcWork.right - mi.rcWork.left - US(24));
    {
        HDC dc = GetDC(NULL);
        int th = ui_text_height(dc, N.title, FONT_MEDIUM, width - US(TEXT_X + PAD));
        height = US(44) + th + US(4) + body_height(dc, width) + US(52);
        height = min(US(300), max(US(140), height));
        ReleaseDC(NULL, dc);
    }
    N.final_y = mi.rcWork.bottom - height - US(16);
    N.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED, NOTICE_CLASS, APP_NAME,
        WS_POPUP, mi.rcWork.right - width - US(16), N.final_y + US(24),
        width, height, foreground, NULL, g_inst, NULL);
    if (!N.window) { N.ui = FALSE; ui_shutdown(); return; }
    SetLayeredWindowAttributes(N.window, 0, 0, LWA_ALPHA);
    {
        COLORREF edge = g_th.dark ? RGB(0x40, 0x40, 0x40) : RGB(0xD9, 0xD9, 0xD9);
        DwmSetWindowAttribute(N.window, 33, &corner, sizeof(corner));   /* yuvarlak köşe */
        DwmSetWindowAttribute(N.window, 34, &edge, sizeof(edge));       /* ince kenar */
    }
    ui_style_window(N.window);
    ShowWindow(N.window, SW_SHOWNOACTIVATE);
    SetTimer(N.window, 1, 15, NULL);
}

#ifdef DEV_SHOT
/* Geliştirme: kartı ön plan denetimi olmadan göster, çizilince pencereyi döndür */
HWND notify_dev_card(const wchar_t *title, const wchar_t *body, BOOL error)
{
    show_card(title, body, error, NULL);
    return N.window;
}
#endif
