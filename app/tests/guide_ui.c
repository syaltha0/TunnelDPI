/* Derleme ve çalıştırma: tests/README.md */
#include "../src/guide.c"
#include <stdio.h>

static int step, failures, opened;
static BOOL cancel_test;
static const int pages[] = { 0, 1, 2, 1, 2, 3 };
static const int clicks[] = { GUIDE_NEXT, GUIDE_NEXT, GUIDE_BACK, GUIDE_NEXT, GUIDE_NEXT, GUIDE_DONE };

void settings_show(int page) { if (page != 0) failures++; opened++; }

static void CALLBACK tick(HWND h, UINT msg, UINT_PTR timer, DWORD time)
{
    int page = (int)(INT_PTR)GetPropW(h, L"TunnelDPIGuidePage");
    (void)msg; (void)time;
    if (cancel_test) {
        KillTimer(h, timer);
        SendMessageW(h, TDM_CLICK_BUTTON, IDCANCEL, 0);
        return;
    }
    if (step >= 6 || page != pages[step]) {
        printf("guide mismatch step=%d page=%d\n", step, page);
        failures++;
        KillTimer(h, timer);
        SendMessageW(h, TDM_CLICK_BUTTON, IDCANCEL, 0);
        return;
    }
    if (step == 5)
        KillTimer(h, timer);
    SendMessageW(h, TDM_CLICK_BUTTON, clicks[step++], 0);
}

static LRESULT CALLBACK hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_ACTIVATE)
        SetTimer((HWND)wp, 42, 150, tick);
    return CallNextHookEx(NULL, code, wp, lp);
}

int wmain(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    HHOOK hh;
    int lang;
    InitCommonControlsEx(&icc);
    hh = SetWindowsHookExW(WH_CBT, hook, NULL, GetCurrentThreadId());
    if (!hh) return 2;
    for (lang = LANG_TR; lang <= LANG_EN; lang++) {
        i18n_set(lang);
        step = 0;
        cancel_test = FALSE;
        guide_show(NULL);
        if (step != 6 || opened != lang) failures++;
        cancel_test = TRUE;
        guide_show(NULL);
        if (opened != lang) failures++;
    }
    UnhookWindowsHookEx(hh);
    printf("guide: TR/EN next, back, finish, cancel: %d failures\n", failures);
    return failures;
}
