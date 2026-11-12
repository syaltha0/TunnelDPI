/* Arayüz akışı; DEV_SHOT önizlemesi gerçek kurulum yapmaz. */
#define DEV_SHOT
#define wWinMain setup_entry
#include "../setup/setup.c"
#undef wWinMain

static int failures, phase, ticks;
static BOOL cancel_test;
static void CALLBACK tick(HWND h, UINT msg, UINT_PTR timer, DWORD time)
{
    (void)msg; (void)time;
    if (++ticks > 40) {
        failures++;
        KillTimer(h, timer);
        PostMessageW(h, WM_CLOSE, 0, 0);
        return;
    }
    if (!phase) {
        if (cancel_test) {
            KillTimer(h, timer);
            PropSheet_PressButton(h, PSBTN_CANCEL);
            return;
        }
        phase = 1;
        PropSheet_PressButton(h, PSBTN_NEXT);
    } else if (g_preview_page == PAGE_DIR) {
        if (phase == 1) {
            phase = 2;
            PropSheet_PressButton(h, PSBTN_BACK);
        } else {
            HWND page = PropSheet_GetCurrentPageHwnd(h);
            if (!g_prev[0]) SetDlgItemTextW(page, IDC_DIR, L"D:\\Programs\\TunnelDPI");
            PropSheet_PressButton(h, PSBTN_NEXT);
        }
    } else if (g_preview_page == PAGE_WELCOME) {
        PropSheet_PressButton(h, PSBTN_NEXT);
    } else if (g_preview_page == PAGE_OPTIONS) {
        HWND page = PropSheet_GetCurrentPageHwnd(h);
        CheckDlgButton(page, IDC_STARTUP, BST_UNCHECKED);
        PropSheet_PressButton(h, PSBTN_NEXT);
    } else if (g_preview_page == PAGE_FINISH) {
        if (g_ok != (g_preview == 1)) failures++;
        KillTimer(h, timer);
        PropSheet_PressButton(h, PSBTN_FINISH);
    }
}

static LRESULT CALLBACK hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_ACTIVATE)
    {
        wchar_t cls[32];
        GetClassNameW((HWND)wp, cls, 32);
        if (!wcscmp(cls, L"#32770") && GetDlgItem((HWND)wp, 0x3024))
            SetTimer((HWND)wp, 42, 150, tick);
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

int wmain(int argc, wchar_t **argv)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    HHOOK hh;
    int lang, mode, ret;
    InitCommonControlsEx(&icc);
    g_inst = GetModuleHandleW(NULL);
    g_icon = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 64, 64, 0);
    wcscpy(g_dir, L"C:\\Program Files\\TunnelDPI");
    if (argc > 1 && !wcscmp(argv[1], L"--show")) {
        g_tr = TRUE;
        g_preview = 1;
        return run_ui();
    }
    hh = SetWindowsHookExW(WH_CBT, hook, NULL, GetCurrentThreadId());
    if (!hh) return 2;
    for (lang = 0; lang < 2; lang++) {
        g_tr = lang;
        for (mode = 1; mode <= 4; mode++) {
            g_prev[0] = 0;
            if (mode == 3) wcscpy(g_prev, L"1.0");
            g_preview = mode == 2 ? 2 : 1;
            cancel_test = mode == 4;
            g_autostart = TRUE;
            phase = ticks = 0;
            g_ok = FALSE;
            ret = run_ui();
            if (ret != (mode == 4 ? 2 : mode == 2 ? 1 : 0)) failures++;
            if (mode != 4 && g_autostart) failures++;
        }
    }
    UnhookWindowsHookEx(hh);
    printf("setup: TR/EN install, update, failure, cancel, startup preference: %d failures\n", failures);
    return failures;
}
