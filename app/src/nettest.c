/*
 * TunnelDPI - bağlantı testi
 *
 * WinHTTP yalnızca test sırasında yüklenir ve sonra bırakılır; boşta bellek
 * maliyeti yoktur. Sonuç ana pencereye WM_NETTEST ile bildirilir.
 */
#include "nettest.h"
#include "i18n.h"
#include "config.h"
#include "http.h"
#include <winhttp.h>
#include <stdlib.h>
#include <stdio.h>

static NetTest g_nt;
static volatile LONG g_busy;

/* Hedef: ayarlardaki test sitesi; yoksa aktif profilin ilk listesindeki ilk
   alan adı; o da yoksa genel bir HTTPS sitesi. */
static BOOL pick_host(wchar_t *out, size_t cap)
{
    Profile *p = config_active(&g_cfg);
    const wchar_t *s = p->blacklist;
    wchar_t tok[MAX_PATH], path[MAX_PATH], line[128], *t, *q;
    BOOL found = FALSE;

    if (g_cfg.test_host[0]) {
        /* "https://site/yol" yazılmışsa yalnızca alan adını al */
        q = wcsstr(g_cfg.test_host, L"://");
        wcsncpy(out, q ? q + 3 : g_cfg.test_host, cap - 1);
        out[cap - 1] = 0;
        out[wcscspn(out, L"/: ")] = 0;
        if (out[0])
            return TRUE;
    }
    wcsncpy(out, L"www.google.com", cap - 1);
    out[cap - 1] = 0;
    if (!next_token(&s, tok, MAX_PATH, L";\r\n"))
        return FALSE;
    app_path(path, MAX_PATH, tok);
    t = read_text_file(path);
    if (!t)
        return FALSE;
    s = t;
    if (next_token(&s, line, 128, L"\r\n") && wcschr(line, L'.')) {
        wcsncpy(out, line, cap - 1);
        out[cap - 1] = 0;
        found = TRUE;
    }
    free(t);
    return found;
}

/* FALSE: ne ayarlı test sitesi ne liste var; yedek google.com engelsiz olduğundan
   profil karşılaştırması için anlamsızdır. */
BOOL nettest_target(wchar_t *out, size_t cap)
{
    return pick_host(out, cap);
}

static DWORD WINAPI worker(LPVOID arg)
{
    wchar_t *host = arg;
    DWORD ms, err;
    BOOL ok = http_probe(host, &ms, &err);

    g_nt.ms = ms;
    g_nt.err = err;
    g_nt.when = GetTickCount();
    g_nt.state = ok ? NT_OK : NT_FAIL;
    free(host);
    InterlockedExchange(&g_busy, 0);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_NETTEST, 0, 0);
    return 0;
}

void nettest_start_host(const wchar_t *fixed)
{
    wchar_t *host;
    HANDLE th;

    if (InterlockedExchange(&g_busy, 1))
        return;
    host = malloc(128 * sizeof(wchar_t));
    if (!host) {
        g_busy = 0;
        return;
    }
    if (fixed && fixed[0]) {
        wcsncpy(host, fixed, 127);
        host[127] = 0;
    } else {
        pick_host(host, 128);
    }
    wcscpy(g_nt.host, host);
    g_nt.state = NT_RUNNING;
    th = CreateThread(NULL, 64 * 1024, worker, host, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        free(host);
        g_nt.state = NT_NONE;
        g_busy = 0;
    }
}

void nettest_start(void)
{
    nettest_start_host(NULL);
}

const NetTest *nettest_get(void)
{
    return &g_nt;
}

void nettest_age(wchar_t *out, size_t cap)
{
    DWORD s = (GetTickCount() - g_nt.when) / 1000;

    if (g_nt.state == NT_RUNNING)
        _snwprintf(out, cap, tr(L"test ediliyor"));
    else if (g_nt.state == NT_NONE)
        _snwprintf(out, cap, tr(L"henüz test edilmedi"));
    else if (s < 5)
        _snwprintf(out, cap, tr(L"az önce"));
    else if (s < 60)
        _snwprintf(out, cap, tr(L"%lu sn önce"), s);
    else if (s < 3600)
        _snwprintf(out, cap, tr(L"%lu dk önce"), s / 60);
    else
        _snwprintf(out, cap, tr(L"%lu sa önce"), s / 3600);
    out[cap - 1] = 0;
}

const wchar_t *nettest_error_text(void)
{
    switch (g_nt.err) {
    case ERROR_WINHTTP_TIMEOUT:             return tr(L"Zaman aşımı");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:   return tr(L"Alan adı çözülemedi");
    case ERROR_WINHTTP_CANNOT_CONNECT:      return tr(L"Bağlanılamadı");
    case ERROR_WINHTTP_CONNECTION_ERROR:    return tr(L"Bağlantı sıfırlandı");
    case ERROR_WINHTTP_SECURE_FAILURE:      return tr(L"TLS hatası");
    default:                                return tr(L"Ulaşılamadı");
    }
}
