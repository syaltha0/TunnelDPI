/*
 * TunnelDPI - site denetimi ve DNS ölçümü
 *
 * İzlenen siteler: ayarlardaki test sitesi, etkin profilin ve site
 * kurallarının listelerindeki ilk siteler; hiçbiri yoksa discord.com.
 * Denetim tek bir arka plan iş parçacığında sırayla yapılır, ws2_32 ve
 * WinHTTP yalnızca o sırada yüklenir.
 */
#include "health.h"
#include "config.h"
#include "dns.h"
#include "http.h"
#include "i18n.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#define CONTROL_HOST L"www.microsoft.com"   /* engelsiz; internet var mı */

static Health   g_h, g_work;
static DnsBench g_b;
static volatile LONG g_busy, g_bbusy;
static wchar_t  g_dns_ip[64];
static int      g_dns_port;

/* ---------------- DNS sorgusu (UDP) ---------------- */

typedef int    (WSAAPI *WSAStartup_t)(WORD, LPWSADATA);
typedef SOCKET (WSAAPI *socket_t)(int, int, int);
typedef int    (WSAAPI *sendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int    (WSAAPI *recvfrom_t)(SOCKET, char *, int, int, struct sockaddr *, int *);
typedef int    (WSAAPI *closesocket_t)(SOCKET);
typedef int    (WSAAPI *setsockopt_t)(SOCKET, int, int, const char *, int);

BOOL dns_probe(const wchar_t *ip, int port, DWORD timeout_ms, DWORD *ms)
{
    static const char qname[] = "\3www\6google\3com";
    HMODULE ws = LoadLibraryExW(L"ws2_32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WSAStartup_t pStartup;
    socket_t pSocket;
    sendto_t pSendto;
    recvfrom_t pRecvfrom;
    closesocket_t pClose;
    setsockopt_t pSetopt;
    struct sockaddr_in sa = { 0 };
    unsigned char q[64], r[512];
    int a, b, c, d, qn = 0, got;
    USHORT id = (USHORT)(GetTickCount() ^ GetCurrentThreadId());
    DWORD t0;
    WSADATA wd;
    SOCKET s;
    BOOL ok = FALSE;

    if (ms)
        *ms = 0;
    if (!ws)
        return FALSE;
    if (swscanf(ip, L"%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        goto out;
    pStartup = (WSAStartup_t)(void *)GetProcAddress(ws, "WSAStartup");
    pSocket = (socket_t)(void *)GetProcAddress(ws, "socket");
    pSendto = (sendto_t)(void *)GetProcAddress(ws, "sendto");
    pRecvfrom = (recvfrom_t)(void *)GetProcAddress(ws, "recvfrom");
    pClose = (closesocket_t)(void *)GetProcAddress(ws, "closesocket");
    pSetopt = (setsockopt_t)(void *)GetProcAddress(ws, "setsockopt");
    if (!pStartup || !pSocket || !pSendto || !pRecvfrom || !pClose || !pSetopt || pStartup(MAKEWORD(2, 2), &wd))
        goto out;

    /* başlık: kimlik, RD, 1 soru; ardından www.google.com A IN */
    q[qn++] = (unsigned char)(id >> 8);
    q[qn++] = (unsigned char)id;
    q[qn++] = 0x01; q[qn++] = 0x00;
    q[qn++] = 0; q[qn++] = 1;
    q[qn++] = 0; q[qn++] = 0; q[qn++] = 0; q[qn++] = 0; q[qn++] = 0; q[qn++] = 0;
    memcpy(q + qn, qname, sizeof(qname));
    qn += (int)sizeof(qname);
    q[qn++] = 0; q[qn++] = 1;
    q[qn++] = 0; q[qn++] = 1;

    sa.sin_family = AF_INET;
    sa.sin_port = (USHORT)(((port ? port : 53) >> 8) | (((port ? port : 53) & 0xFF) << 8));
    sa.sin_addr.S_un.S_un_b.s_b1 = (UCHAR)a;
    sa.sin_addr.S_un.S_un_b.s_b2 = (UCHAR)b;
    sa.sin_addr.S_un.S_un_b.s_b3 = (UCHAR)c;
    sa.sin_addr.S_un.S_un_b.s_b4 = (UCHAR)d;

    s = pSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        goto out;
    pSetopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    t0 = GetTickCount();
    if (pSendto(s, (const char *)q, qn, 0, (struct sockaddr *)&sa, sizeof(sa)) == qn) {
        while ((got = pRecvfrom(s, (char *)r, sizeof(r), 0, NULL, NULL)) > 0) {
            if (got < 12 || r[0] != q[0] || r[1] != q[1] || !(r[2] & 0x80))
                continue;               /* başka bir yanıt */
            ok = (r[3] & 0x0F) == 0 && (r[6] || r[7]);   /* hata yok, en az bir cevap */
            break;
        }
        if (ms)
            *ms = GetTickCount() - t0;
    }
    pClose(s);
out:
    FreeLibrary(ws);
    return ok;
}

/* ---------------- izlenen siteler ---------------- */

static void add_host(Health *h, const wchar_t *host)
{
    int i;

    if (h->n >= HEALTH_MAX || !host[0] || !wcschr(host, L'.'))
        return;
    for (i = 0; i < h->n; i++)
        if (!_wcsicmp(h->s[i].host, host))
            return;
    wcsncpy(h->s[h->n].host, host, 95);
    h->s[h->n].host[95] = 0;
    h->s[h->n].ok = SITE_UNKNOWN;
    h->n++;
}

/* Liste dosyasındaki ilk alan adı */
static void add_first_of(Health *h, const wchar_t *rel)
{
    wchar_t path[MAX_PATH], line[128], *t;
    const wchar_t *s;

    app_path(path, MAX_PATH, rel);
    t = read_text_file(path);
    if (!t)
        return;
    s = t;
    while (next_token(&s, line, 128, L"\r\n")) {
        trim_ws(line);
        if (line[0] && line[0] != L'#') {
            add_host(h, line[0] == L'.' ? line + 1 : line);
            break;
        }
    }
    free(t);
}

static void collect(Health *h)
{
    const Profile *p = config_active(&g_cfg);
    const wchar_t *s = p->blacklist;
    wchar_t tok[MAX_PATH], host[128], *q;
    int i;

    h->n = 0;
    if (g_cfg.test_host[0]) {
        q = wcsstr(g_cfg.test_host, L"://");
        wcsncpy(host, q ? q + 3 : g_cfg.test_host, 127);
        host[127] = 0;
        host[wcscspn(host, L"/: ")] = 0;
        add_host(h, host);
    }
    while (next_token(&s, tok, MAX_PATH, L";\r\n"))
        add_first_of(h, tok);
    for (i = 0; i < g_cfg.nrules; i++)
        add_first_of(h, g_cfg.rules[i].list);
    if (!h->n)
        add_host(h, L"discord.com");
}

static DWORD WINAPI worker(LPVOID arg)
{
    Health *h = &g_work;
    DWORD ms, err;
    int i;
    (void)arg;

    for (i = 0; i < h->n; i++) {
        h->s[i].ok = http_probe(h->s[i].host, &ms, &err) ? SITE_OK : SITE_FAIL;
        h->s[i].ms = ms;
    }
    h->control_ok = http_probe(CONTROL_HOST, &ms, &err);
    h->dns = SITE_UNKNOWN;
    if (g_dns_ip[0]) {
        /* tek kayıp paket yüzünden yedeğe geçilmesin */
        BOOL ok = dns_probe(g_dns_ip, g_dns_port, 1500, &ms) || dns_probe(g_dns_ip, g_dns_port, 2500, &ms);
        h->dns = ok ? SITE_OK : SITE_FAIL;
    }
    h->when = GetTickCount();
    h->running = FALSE;
    g_h = *h;
    InterlockedExchange(&g_busy, 0);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_HEALTH_DONE, 0, 0);
    return 0;
}

void health_start(void)
{
    const wchar_t *v4, *v6, *port, *doh, *s;
    HANDLE th;

    if (InterlockedExchange(&g_busy, 1))
        return;
    memset(&g_work, 0, sizeof(g_work));
    collect(&g_work);
    g_dns_ip[0] = 0;
    g_dns_port = 53;
    if (g_cfg.dns_mode != DNS_OFF) {
        dns_current(&g_cfg, &v4, &v6, &port, &doh);
        s = v4 ? v4 : L"";
        next_token(&s, g_dns_ip, 64, L", ;");
        if (g_cfg.dns_mode == DNS_REDIRECT && port)
            g_dns_port = _wtoi(port);
    }
    g_work.running = TRUE;
    g_h.running = TRUE;
    th = CreateThread(NULL, 64 * 1024, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        g_h.running = FALSE;
        g_busy = 0;
    }
}

const Health *health_get(void)
{
    return &g_h;
}

int health_ok_count(void)
{
    int i, n = 0;

    for (i = 0; i < g_h.n; i++)
        if (g_h.s[i].ok == SITE_OK)
            n++;
    return n;
}

void health_summary(wchar_t *out, size_t cap)
{
    wchar_t ok[200] = L"", bad[200] = L"";
    int i;

    out[0] = 0;
    if (!g_h.when)
        return;
    for (i = 0; i < g_h.n; i++) {
        wchar_t *dst = g_h.s[i].ok == SITE_OK ? ok : bad;
        size_t l = wcslen(dst);
        _snwprintf(dst + l, 200 - l, L"%s%s", l ? L", " : L"", g_h.s[i].host);
        dst[199] = 0;
    }
    if (bad[0] && ok[0])
        _snwprintf(out, cap, tr(L"%s açılmıyor · %s açılıyor"), bad, ok);
    else if (bad[0])
        _snwprintf(out, cap, tr(L"%s açılmıyor"), bad);
    else
        _snwprintf(out, cap, tr(L"%s açılıyor"), ok);
    out[cap - 1] = 0;
}

/* ---------------- DNS hız ölçümü ---------------- */

static DWORD WINAPI bench_worker(LPVOID arg)
{
    int i, k, best = -1;
    (void)arg;

    for (i = 0; i < g_b.n; i++) {
        const wchar_t *s = g_dns_presets[i].v4;
        wchar_t ip[64];
        DWORD ms, mn = (DWORD)-1;

        g_b.ms[i] = -1;
        if (!s || !next_token(&s, ip, 64, L", ;"))
            continue;
        for (k = 0; k < 3; k++)
            if (dns_probe(ip, 53, 1200, &ms) && ms < mn)
                mn = ms;
        if (mn != (DWORD)-1) {
            g_b.ms[i] = (int)mn;
            if (best < 0 || g_b.ms[i] < g_b.ms[best])
                best = i;
        }
        if (g_hwnd)
            PostMessageW(g_hwnd, WM_DNSBENCH_DONE, 0, 0);   /* ara sonuç */
    }
    g_b.best = best;
    g_b.running = FALSE;
    InterlockedExchange(&g_bbusy, 0);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_DNSBENCH_DONE, 1, 0);
    return 0;
}

void dnsbench_start(void)
{
    HANDLE th;
    int i;

    if (InterlockedExchange(&g_bbusy, 1))
        return;
    g_b.n = DNS_PRESET_CUSTOM < 16 ? DNS_PRESET_CUSTOM : 16;   /* "Özel" hariç */
    for (i = 0; i < 16; i++)
        g_b.ms[i] = -2;   /* henüz ölçülmedi */
    g_b.best = -1;
    g_b.running = TRUE;
    th = CreateThread(NULL, 64 * 1024, bench_worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        g_b.running = FALSE;
        g_bbusy = 0;
    }
}

const DnsBench *dnsbench_get(void)
{
    return &g_b;
}
