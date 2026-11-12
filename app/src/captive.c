/*
 * TunnelDPI - giriş sayfası (captive portal) denetimi
 */
#include "captive.h"
#include "http.h"
#include <stdlib.h>
#include <string.h>

static volatile LONG g_busy;

static DWORD WINAPI worker(LPVOID arg)
{
    DWORD status = 0;
    char *body;
    int r = CAPTIVE_OFFLINE;

    (void)arg;
    body = http_get_raw(L"http://www.msftconnecttest.com/connecttest.txt", 16384, &status);
    if (body && status == 200 && strstr(body, "Microsoft Connect Test"))
        r = CAPTIVE_ONLINE;
    else if (status)          /* yanıt geldi ama beklenen değil: yönlendirme ya da giriş sayfası */
        r = CAPTIVE_PORTAL;
    free(body);
    InterlockedExchange(&g_busy, 0);
    PostMessageW(g_hwnd, WM_CAPTIVE_DONE, (WPARAM)r, 0);
    return 0;
}

void captive_check(void)
{
    HANDLE th;

    if (InterlockedCompareExchange(&g_busy, 1, 0))
        return;
    th = CreateThread(NULL, 64 * 1024, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&g_busy, 0);
}
