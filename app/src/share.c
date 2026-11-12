/*
 * TunnelDPI - erişim noktasındaki cihazlarla paylaşım (küçük HTTP CONNECT vekili)
 */
#include "share.h"
#include "config.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define HOTSPOT_NET  0xC0A88900u   /* 192.168.137.0/24: Windows Mobil erişim noktası */
#define MAX_CLIENTS  64
#define HDR_CAP      8192
#define IDLE_MS      (5 * 60 * 1000)
#define FW_RULE      L"TunnelDPI paylaşım"

/* ws2_32 yalnızca açıkken yüklendiği için bayt sırası çevirisi burada */
#define BSWAP32(v) ((((v) & 0xFFu) << 24) | (((v) & 0xFF00u) << 8) | (((v) >> 8) & 0xFF00u) | (((v) >> 24) & 0xFFu))
#define BSWAP16(v) ((USHORT)((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF)))

typedef int    (WSAAPI *WSAStartup_t)(WORD, LPWSADATA);
typedef SOCKET (WSAAPI *socket_t)(int, int, int);
typedef int    (WSAAPI *bind_t)(SOCKET, const struct sockaddr *, int);
typedef int    (WSAAPI *listen_t)(SOCKET, int);
typedef SOCKET (WSAAPI *accept_t)(SOCKET, struct sockaddr *, int *);
typedef int    (WSAAPI *connect_t)(SOCKET, const struct sockaddr *, int);
typedef int    (WSAAPI *recv_t)(SOCKET, char *, int, int);
typedef int    (WSAAPI *send_t)(SOCKET, const char *, int, int);
typedef int    (WSAAPI *select_t)(int, fd_set *, fd_set *, fd_set *, const struct timeval *);
typedef int    (WSAAPI *closesocket_t)(SOCKET);
typedef int    (WSAAPI *shutdown_t)(SOCKET, int);
typedef int    (WSAAPI *setsockopt_t)(SOCKET, int, int, const char *, int);
typedef int    (WSAAPI *getaddrinfo_t)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
typedef void   (WSAAPI *freeaddrinfo_t)(PADDRINFOA);
typedef int    (WSAAPI *wsafdisset_t)(SOCKET, fd_set *);

static struct {
    HMODULE        m;
    socket_t       socket;
    bind_t         bind;
    listen_t       listen;
    accept_t       accept;
    connect_t      connect;
    recv_t         recv;
    send_t         send;
    select_t       select;
    closesocket_t  close;
    shutdown_t     shutdown;
    setsockopt_t   setsockopt;
    getaddrinfo_t  getaddrinfo;
    freeaddrinfo_t freeaddrinfo;
    wsafdisset_t   isset;
} W;

static SOCKET        g_ls = INVALID_SOCKET;
static DWORD         g_ip;            /* dinlenen adres (ağ sırası), 0 = kapalı */
static volatile LONG g_clients;
static BOOL          g_waiting;
static BOOL          g_error;

static BOOL ws_load(void)
{
    WSADATA wd;
    WSAStartup_t start;

    if (W.m)
        return TRUE;
    W.m = LoadLibraryExW(L"ws2_32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!W.m)
        return FALSE;
#define L_(f, n) W.f = (__typeof__(W.f))(void *)GetProcAddress(W.m, n)
    L_(socket, "socket");
    L_(bind, "bind");
    L_(listen, "listen");
    L_(accept, "accept");
    L_(connect, "connect");
    L_(recv, "recv");
    L_(send, "send");
    L_(select, "select");
    L_(close, "closesocket");
    L_(shutdown, "shutdown");
    L_(setsockopt, "setsockopt");
    L_(getaddrinfo, "getaddrinfo");
    L_(freeaddrinfo, "freeaddrinfo");
    L_(isset, "__WSAFDIsSet");
#undef L_
    start = (WSAStartup_t)(void *)GetProcAddress(W.m, "WSAStartup");
    if (start && W.socket && W.bind && W.listen && W.accept && W.connect && W.recv && W.send && W.select &&
        W.close && W.shutdown && W.setsockopt && W.getaddrinfo && W.freeaddrinfo && W.isset && !start(MAKEWORD(2, 2), &wd))
        return TRUE;
    FreeLibrary(W.m);
    memset(&W, 0, sizeof(W));
    return FALSE;
}

/* Erişim noktasının bu bilgisayardaki adresi (ağ sırası), yoksa 0 */
static DWORD hotspot_ip(void)
{
    ULONG sz = 16 * 1024;
    IP_ADAPTER_ADDRESSES *aa = malloc(sz), *a;
    DWORD ip = 0;
    ULONG result;

    if (!aa)
        return 0;
    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 NULL, aa, &sz);
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(aa);
        aa = malloc(sz);
        if (!aa)
            return 0;
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                     NULL, aa, &sz);
    }
    if (result != NO_ERROR) {
        free(aa);
        return 0;
    }
    for (a = aa; a && !ip; a = a->Next) {
        IP_ADAPTER_UNICAST_ADDRESS *u;
        if (a->OperStatus != IfOperStatusUp)
            continue;
        for (u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.iSockaddrLength < (int)sizeof(struct sockaddr_in) ||
                u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            DWORD v = BSWAP32(((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr);
            if ((v & 0xFFFFFF00u) == HOTSPOT_NET) {
                ip = BSWAP32(v);
                break;
            }
        }
    }
    free(aa);
    return ip;
}

/* ---------------- istemci ---------------- */

static BOOL send_all(SOCKET s, const char *b, int n)
{
    while (n > 0) {
        int k = W.send(s, b, n, 0);
        if (k <= 0)
            return FALSE;
        b += k;
        n -= k;
    }
    return TRUE;
}

static SOCKET dial(const char *host, const char *port)
{
    ADDRINFOA hints = { 0 }, *res = NULL, *r;
    SOCKET s = INVALID_SOCKET;

    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (W.getaddrinfo(host, port, &hints, &res) != 0)
        return INVALID_SOCKET;
    for (r = res; r; r = r->ai_next) {
        s = W.socket(r->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            continue;
        if (W.connect(s, r->ai_addr, (int)r->ai_addrlen) == 0)
            break;
        W.close(s);
        s = INVALID_SOCKET;
    }
    W.freeaddrinfo(res);
    return s;
}

static void relay(SOCKET a, SOCKET b)
{
    char buf[16384];
    DWORD last = GetTickCount();

    for (;;) {
        fd_set rd;
        struct timeval tv = { 5, 0 };
        int n;

        FD_ZERO(&rd);
        FD_SET(a, &rd);
        FD_SET(b, &rd);
        n = W.select(0, &rd, NULL, NULL, &tv);
        if (n < 0)
            break;
        if (n == 0) {
            if (GetTickCount() - last > IDLE_MS || !g_ip)
                break;
            continue;
        }
        last = GetTickCount();
        if (W.isset(a, &rd)) {
            n = W.recv(a, buf, sizeof(buf), 0);
            if (n <= 0 || !send_all(b, buf, n))
                break;
        }
        if (W.isset(b, &rd)) {
            n = W.recv(b, buf, sizeof(buf), 0);
            if (n <= 0 || !send_all(a, buf, n))
                break;
        }
    }
}

/* "host:port" ya da "[v6]:port" → host, port */
static BOOL split_hostport(char *hp, const char *defport, char *host, size_t hcap, char *port, size_t pcap)
{
    char *c;

    if (*hp == '[') {
        char *e = strchr(hp, ']');
        if (!e)
            return FALSE;
        *e = 0;
        snprintf(host, hcap, "%s", hp + 1);
        c = e[1] == ':' ? e + 2 : NULL;
    } else {
        c = strrchr(hp, ':');
        if (c)
            *c++ = 0;
        snprintf(host, hcap, "%s", hp);
    }
    snprintf(port, pcap, "%s", c && *c ? c : defport);
    return host[0] != 0;
}

static void send_str(SOCKET s, const char *t)
{
    send_all(s, t, (int)strlen(t));
}

static const char *header_end(const char *b, int n)
{
    int i;

    for (i = 0; i + 3 < n; i++)
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n')
            return b + i + 4;
    return NULL;
}

static DWORD WINAPI client(LPVOID arg)
{
    SOCKET c = (SOCKET)(UINT_PTR)arg, s = INVALID_SOCKET;
    char *h = malloc(HDR_CAP + 1), method[16], target[1024], host[512], port[8];
    const char *end = NULL;
    int n = 0, k;

    if (!h)
        goto out;
    while (n < HDR_CAP && !(end = header_end(h, n))) {
        k = W.recv(c, h + n, HDR_CAP - n, 0);
        if (k <= 0)
            goto out;
        n += k;
    }
    h[n] = 0;
    if (!end || sscanf(h, "%15s %1023s", method, target) != 2)
        goto out;
    if (!_stricmp(method, "CONNECT")) {
        static const char ok[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        if (!split_hostport(target, "443", host, sizeof(host), port, sizeof(port)) ||
            (s = dial(host, port)) == INVALID_SOCKET) {
            send_str(c, "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n");
            goto out;
        }
        if (!send_all(c, ok, sizeof(ok) - 1))
            goto out;
        if (end < h + n && !send_all(s, end, (int)(h + n - end)))   /* CONNECT'in ardından gelen veri */
            goto out;
    } else if (!_strnicmp(target, "http://", 7)) {
        /* düz HTTP: "GET http://site/yol" → siteye "GET /yol"; bağlantı tek istekte kapanır */
        char *path = strchr(target + 7, '/'), line[1200];
        const char *rest = strstr(h, "\r\n") + 2, *p;
        char hp[512];
        int ll;

        snprintf(hp, sizeof(hp), "%.*s", path ? (int)(path - target - 7) : (int)strlen(target + 7), target + 7);
        if (!split_hostport(hp, "80", host, sizeof(host), port, sizeof(port)) ||
            (s = dial(host, port)) == INVALID_SOCKET) {
            send_str(c, "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n");
            goto out;
        }
        p = strstr(h, " HTTP/");
        {
            const char *e = p ? strstr(p, "\r\n") : NULL;
            ll = snprintf(line, sizeof(line), "%s %s%.*s\r\nConnection: close\r\n", method, path ? path : "/",
                          p && e ? (int)(e - p) : 9, p && e ? p : " HTTP/1.1");
        }
        if (ll <= 0 || ll >= (int)sizeof(line))
            goto out;
        if (!send_all(s, line, ll))
            goto out;
        /* özgün başlıklar: Connection / Proxy-Connection atlanır */
        while (rest < end) {
            const char *e = strstr(rest, "\r\n");
            if (!e || e >= end)
                break;
            if (e > rest && _strnicmp(rest, "Connection:", 11) && _strnicmp(rest, "Proxy-Connection:", 17) &&
                !send_all(s, rest, (int)(e - rest + 2)))
                goto out;
            if (e == rest) {                     /* boş satır: başlıklar bitti */
                if (!send_all(s, "\r\n", 2))
                    goto out;
                break;
            }
            rest = e + 2;
        }
        if (end < h + n && !send_all(s, end, (int)(h + n - end)))
            goto out;
    } else {
        send_str(c, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        goto out;
    }
    relay(c, s);
out:
    if (s != INVALID_SOCKET)
        W.close(s);
    W.shutdown(c, SD_BOTH);
    W.close(c);
    free(h);
    InterlockedDecrement(&g_clients);
    return 0;
}

static DWORD WINAPI listener(LPVOID arg)
{
    SOCKET ls = (SOCKET)(UINT_PTR)arg;
    DWORD net = BSWAP32(g_ip) & 0xFFFFFF00u;

    for (;;) {
        struct sockaddr_in peer;
        int pl = sizeof(peer);
        SOCKET c = W.accept(ls, (struct sockaddr *)&peer, &pl);
        HANDLE th;

        if (c == INVALID_SOCKET)
            break;                               /* share_stop dinleyiciyi kapattı */
        /* yalnızca erişim noktasına bağlı cihazlar */
        if ((BSWAP32(peer.sin_addr.s_addr) & 0xFFFFFF00u) != net || g_clients >= MAX_CLIENTS) {
            W.close(c);
            continue;
        }
        InterlockedIncrement(&g_clients);
        th = CreateThread(NULL, 64 * 1024, client, (LPVOID)(UINT_PTR)c, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (th) {
            CloseHandle(th);
        } else {
            InterlockedDecrement(&g_clients);
            W.close(c);
        }
    }
    return 0;
}

/* Güvenlik duvarı kuralı: yalnızca erişim noktası ağından bu porta */
static DWORD WINAPI firewall(LPVOID arg)
{
    wchar_t cmd[400];
    BOOL add = (BOOL)(UINT_PTR)arg;

    run_hidden(L"netsh.exe advfirewall firewall delete rule name=\"" FW_RULE L"\"", 10000);
    if (add) {
        _snwprintf(cmd, 400, L"netsh.exe advfirewall firewall add rule name=\"" FW_RULE L"\" dir=in action=allow "
                             L"protocol=TCP localport=%d remoteip=192.168.137.0/24", SHARE_PORT);
        cmd[399] = 0;
        run_hidden(cmd, 10000);
    }
    return 0;
}

static void firewall_async(BOOL add)
{
    HANDLE th = CreateThread(NULL, 64 * 1024, firewall, (LPVOID)(UINT_PTR)add, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
}

/* ---------------- dışarıya ---------------- */

void share_stop(void)
{
    if (g_ls != INVALID_SOCKET) {
        W.close(g_ls);                           /* accept() hata döner, dinleyici biter */
        g_ls = INVALID_SOCKET;
        app_log(L"Paylaşım kapatıldı.");
    }
    g_ip = 0;
}

void share_apply(void)
{
    static BOOL fw_added;
    DWORD ip;
    struct sockaddr_in sa = { 0 };
    SOCKET ls;
    HANDLE th;
    int one = 1;

    if (!g_cfg.share_proxy) {
        g_error = FALSE;
        share_stop();
        g_waiting = FALSE;
        if (fw_added) {
            firewall_async(FALSE);
            fw_added = FALSE;
        }
        return;
    }
    ip = hotspot_ip();
    g_waiting = !ip;
    if (ip == g_ip && g_ls != INVALID_SOCKET)
        return;
    share_stop();
    g_error = FALSE;
    if (!ip)
        return;
    if (!ws_load()) { g_error = TRUE; return; }
    if (!fw_added) {
        firewall_async(TRUE);
        fw_added = TRUE;
    }
    ls = W.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        g_error = TRUE;
        return;
    }
    W.setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof(one));
    sa.sin_family = AF_INET;
    sa.sin_port = BSWAP16(SHARE_PORT);
    sa.sin_addr.s_addr = ip;
    if (W.bind(ls, (struct sockaddr *)&sa, sizeof(sa)) || W.listen(ls, 16)) {
        app_log(L"Paylaşım açılamadı: %d portu kullanımda olabilir.", SHARE_PORT);
        g_error = TRUE;
        W.close(ls);
        return;
    }
    g_ls = ls;
    g_ip = ip;
    th = CreateThread(NULL, 64 * 1024, listener, (LPVOID)(UINT_PTR)ls, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th) {
        CloseHandle(th);
        app_log(L"Paylaşım açık: erişim noktasındaki cihazlar %d.%d.%d.%d:%d vekilini kullanabilir.",
                ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24, SHARE_PORT);
    } else {
        share_stop();
        g_error = TRUE;
    }
}

int share_status(wchar_t *ip, size_t cap)
{
    if (ip && cap)
        ip[0] = 0;
    if (!g_cfg.share_proxy)
        return SHARE_OFF;
    if (g_error)
        return SHARE_ERROR;
    if (!g_ip)
        return SHARE_WAITING;
    if (ip)
        _snwprintf(ip, cap, L"%lu.%lu.%lu.%lu", g_ip & 0xFF, (g_ip >> 8) & 0xFF, (g_ip >> 16) & 0xFF, g_ip >> 24);
    return SHARE_ON;
}

#ifdef DEV_SHOT
/* Geliştirme: vekili verilen adreste aç (erişim noktası olmadan denemek için) */
void share_dev_start(DWORD ip, DWORD ms)
{
    struct sockaddr_in sa = { 0 };
    SOCKET ls;
    HANDLE th;

    if (!ws_load())
        return;
    ls = W.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sa.sin_family = AF_INET;
    sa.sin_port = BSWAP16(SHARE_PORT);
    sa.sin_addr.s_addr = ip;
    if (W.bind(ls, (struct sockaddr *)&sa, sizeof(sa)) || W.listen(ls, 16))
        return;
    g_ls = ls;
    g_ip = ip;
    th = CreateThread(NULL, 64 * 1024, listener, (LPVOID)(UINT_PTR)ls, 0, NULL);
    if (th)
        CloseHandle(th);
    Sleep(ms);
    share_stop();
}
#endif
