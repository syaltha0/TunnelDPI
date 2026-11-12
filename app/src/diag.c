/*
 * TunnelDPI - engel teşhisi
 *
 * 1) Sistem çözücüsü (GetAddrInfoW) ve DoH (Cloudflare, olmazsa Google)
 *    yanıtları karşılaştırılır: boş/yerel/0.0.0.0 yanıt ya da hiç ortak adres
 *    olmaması DNS müdahalesine işaret eder (CDN'ler farklı adres verebildiği
 *    için ortak adres olmaması tek başına "olası" sayılır).
 * 2) HTTPS bağlantısı denenir; DNS temizse ve bağlantı kesiliyorsa engel
 *    DPI'dır.
 * ws2_32 ve winhttp yalnızca teşhis sırasında yüklenir.
 */
#include "diag.h"
#include "i18n.h"
#include "http.h"
#include "nettest.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#define WM_DIAG_DONE_ (WM_DIAG_DONE)

static DiagResult g_d;
static volatile LONG g_busy;

typedef int  (WSAAPI *WSAStartup_t)(WORD, LPWSADATA);
typedef int  (WSAAPI *GetAddrInfoW_t)(PCWSTR, PCWSTR, const ADDRINFOW *, PADDRINFOW *);
typedef void (WSAAPI *FreeAddrInfoW_t)(PADDRINFOW);

static BOOL is_bogus(const wchar_t *ip)
{
    int a = 0, b = 0;

    swscanf(ip, L"%d.%d", &a, &b);
    return a == 0 || a == 127 || a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) ||
           (a == 169 && b == 254);
}

/* Sistem çözücüsüyle IPv4 adresleri: "a, b, c" */
static BOOL resolve_system(const wchar_t *host, wchar_t *out, size_t cap)
{
    HMODULE ws = LoadLibraryExW(L"ws2_32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WSAStartup_t pStartup;
    GetAddrInfoW_t pGai;
    FreeAddrInfoW_t pFree;
    ADDRINFOW hints = { 0 }, *res = NULL, *ai;
    WSADATA wd;
    size_t len = 0;
    int n = 0;

    out[0] = 0;
    if (!ws)
        return FALSE;
    pStartup = (WSAStartup_t)(void *)GetProcAddress(ws, "WSAStartup");
    pGai = (GetAddrInfoW_t)(void *)GetProcAddress(ws, "GetAddrInfoW");
    pFree = (FreeAddrInfoW_t)(void *)GetProcAddress(ws, "FreeAddrInfoW");
    if (pStartup && pGai && pFree && pStartup(MAKEWORD(2, 2), &wd) == 0) {
        hints.ai_family = AF_INET;
        if (pGai(host, NULL, &hints, &res) == 0) {
            for (ai = res; ai && n < 4; ai = ai->ai_next) {
                const BYTE *b = (const BYTE *)&((struct sockaddr_in *)ai->ai_addr)->sin_addr;
                len += _snwprintf(out + len, cap - len, L"%s%u.%u.%u.%u", n ? L", " : L"", b[0], b[1], b[2], b[3]);
                n++;
            }
            pFree(res);
        }
    }
    FreeLibrary(ws);
    out[cap - 1] = 0;
    return n > 0;
}

/* DoH JSON yanıtındaki A kayıtları ("type":1 ... "data":"x.x.x.x") */
static BOOL resolve_doh(const wchar_t *host, wchar_t *out, size_t cap)
{
    static const wchar_t *urls[] = {
        L"https://cloudflare-dns.com/dns-query?name=%s&type=A",
        L"https://dns.google/resolve?name=%s&type=A",
    };
    wchar_t url[400];
    size_t k, len = 0;
    int n = 0;

    out[0] = 0;
    for (k = 0; k < 2 && !n; k++) {
        char *body, *p;
        DWORD st;

        _snwprintf(url, 400, urls[k], host);
        url[399] = 0;
        body = http_get(url, L"accept: application/dns-json\r\n", 65536, &st);
        if (!body)
            continue;
        for (p = body; n < 4 && (p = strstr(p, "\"data\"")) != NULL; p += 6) {
            char ip[64];
            int a, b, c, d;
            if (json_str(p, "data", ip, sizeof(ip)) && sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                len += _snwprintf(out + len, cap - len, L"%s%d.%d.%d.%d", n ? L", " : L"", a, b, c, d);
                n++;
            }
        }
        free(body);
    }
    out[cap - 1] = 0;
    return n > 0;
}

/* HTTPS (SVCB, type 65) kaydında "ech=" var mı: varsa tarayıcı Güvenli DNS
   ile SNI'yi şifreleyebilir ve DPI hangi siteye gidildiğini göremez */
static BOOL check_ech(const wchar_t *host)
{
    wchar_t url[400];
    char *body;
    DWORD st;
    BOOL ech;

    _snwprintf(url, 400, L"https://cloudflare-dns.com/dns-query?name=%s&type=HTTPS", host);
    url[399] = 0;
    body = http_get(url, L"accept: application/dns-json\r\n", 65536, &st);
    if (!body)
        return FALSE;
    ech = strstr(body, "ech=") != NULL;
    free(body);
    return ech;
}

static BOOL overlap(const wchar_t *a, const wchar_t *b)
{
    wchar_t t[32];
    const wchar_t *s = a;

    while (next_token(&s, t, 32, L", "))
        if (wcsstr(b, t))
            return TRUE;
    return FALSE;
}

static DWORD WINAPI worker(LPVOID arg)
{
    DiagResult *d = &g_d;
    BOOL sys_ok, doh_ok;
    (void)arg;

    sys_ok = resolve_system(d->host, d->sys, 160);
    doh_ok = resolve_doh(d->host, d->doh, 160);
    if (!doh_ok)
        d->dns = DNS_DOHFAIL;
    else if (!sys_ok)
        d->dns = DNS_SYSFAIL;
    else if (is_bogus(d->sys))
        d->dns = DNS_BOGUS;
    else if (!overlap(d->sys, d->doh))
        d->dns = DNS_DIFF;
    else
        d->dns = DNS_SAME;
    d->ech = doh_ok && check_ech(d->host);
    d->https_ok = http_probe(d->host, &d->ms, &d->err);

    if (d->https_ok) {
        _snwprintf(d->verdict, 160, tr(L"Erişim var: %s %lu ms'de yanıt verdi"), d->host, d->ms);
        wcscpy(d->advice, d->dns == DNS_DIFF ? tr(L"DNS yanıtları farklı (normal olabilir); sorun yaşarsan DNS'i değiştir.")
                                             : tr(L"Bu site için ek bir ayar gerekmiyor."));
    } else if (d->dns == DNS_SYSFAIL || d->dns == DNS_BOGUS) {
        wcscpy(d->verdict, tr(L"DNS engeli: site adresi bulunamıyor ya da yanlış yere yönleniyor"));
        wcscpy(d->advice, tr(L"DNS sayfasından bir sunucu seç ve şifreli DNS'i aç."));
    } else if (d->dns == DNS_DIFF) {
        wcscpy(d->verdict, tr(L"Olası DNS engeli: internet sağlayıcının DNS'i farklı adres veriyor"));
        wcscpy(d->advice, tr(L"Önce DNS'i değiştirmeyi dene; düzelmezse \"En uygun profili bul\"u kullan."));
    } else if (d->bypass_on) {
        wcscpy(d->verdict, tr(L"Engel aşılamıyor: bağlantı kesiliyor"));
        wcscpy(d->advice, tr(L"Durum sayfasından \"En uygun profili bul\" ya da \"Derin tarama\"yı dene."));
    } else {
        wcscpy(d->verdict, tr(L"Erişim engeli: bağlantı kesiliyor"));
        wcscpy(d->advice, tr(L"Korumayı aç."));
    }
    if (d->ech) {
        size_t al = wcslen(d->advice);
        _snwprintf(d->advice + al, 400 - al, L" %s",
                   tr(L"Bu site ECH destekliyor: tarayıcında Güvenli DNS'i açarsan site adı gizlenir ve engel çoğu "
                      L"zaman kendiliğinden kalkar."));
        d->advice[399] = 0;
    }
    d->state = DIAG_DONE;
    InterlockedExchange(&g_busy, 0);
    app_log(L"Teşhis (%s): sistem DNS [%s], DoH [%s], HTTPS %s. %s", d->host, d->sys[0] ? d->sys : L"-",
            d->doh[0] ? d->doh : L"-", d->https_ok ? L"başarılı" : L"başarısız", d->verdict);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_DIAG_DONE_, 0, 0);
    return 0;
}

void diag_start(void)
{
    diag_start_host(NULL);
}

void diag_start_host(const wchar_t *host)
{
    HANDLE th;

    if (InterlockedExchange(&g_busy, 1))
        return;
    memset(&g_d, 0, sizeof(g_d));
    if (host && host[0])
        wcsncpy(g_d.host, host, 127);
    else
        nettest_target(g_d.host, 128);
    g_d.bypass_on = bypass_running();
    g_d.state = DIAG_RUNNING;
    th = CreateThread(NULL, 64 * 1024, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    else {
        g_d.state = DIAG_NONE;
        g_busy = 0;
    }
}

const DiagResult *diag_get(void)
{
    return &g_d;
}
