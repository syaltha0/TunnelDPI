/*
 * TunnelDPI - DNS
 *
 * İki yöntem:
 *  - GoodbyeDPI yönlendirmesi: --dns-addr/--dns-port ile DNS paketleri başka
 *    sunucu/porta yönlendirilir (ISS'nin 53. port müdahalesini aşmak için
 *    standart dışı port gerekir: Yandex 1253, Quad9 9953, AdGuard/OpenDNS 5353).
 *  - Sistem DNS: aktif ağ adaptörlerinin DNS sunucuları değiştirilir, isteğe
 *    bağlı DoH (Windows 11) etkinleştirilir; eski değerler dns-restore.ini'ye
 *    kaydedilir ve kapatınca/çökme sonrası açılışta geri yüklenir.
 */
#include "dns.h"
#include "i18n.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdlib.h>
#include <stdio.h>

const DnsPreset g_dns_presets[] = {
    { L"Yandex",     L"77.88.8.8,77.88.8.1",       L"2a02:6b8::feed:0ff,2a02:6b8:0:1::feed:0ff", L"1253", L"https://common.dot.dns.yandex.net/dns-query" },
    { L"Cloudflare", L"1.1.1.1,1.0.0.1",           L"2606:4700:4700::1111,2606:4700:4700::1001", L"53",   L"https://cloudflare-dns.com/dns-query" },
    { L"Google",     L"8.8.8.8,8.8.4.4",           L"2001:4860:4860::8888,2001:4860:4860::8844", L"53",   L"https://dns.google/dns-query" },
    { L"Quad9",      L"9.9.9.9,149.112.112.112",   L"2620:fe::fe,2620:fe::9",                    L"9953", L"https://dns.quad9.net/dns-query" },
    { L"AdGuard",    L"94.140.14.14,94.140.15.15", L"2a10:50c0::ad1:ff,2a10:50c0::ad2:ff",       L"5353", L"https://dns.adguard-dns.com/dns-query" },
    { L"OpenDNS",    L"208.67.222.222,208.67.220.220", L"2620:119:35::35,2620:119:53::53",       L"5353", L"https://doh.opendns.com/dns-query" },
    { L"Özel",       NULL, NULL, NULL, NULL },
};
const int g_ndns_presets = sizeof(g_dns_presets) / sizeof(g_dns_presets[0]);

void dns_current(const Config *c, const wchar_t **v4, const wchar_t **v6,
                 const wchar_t **port, const wchar_t **doh)
{
    int i = c->dns_preset;

    if (i < 0 || i >= g_ndns_presets)
        i = 0;
    if (i == DNS_PRESET_CUSTOM) {
        *v4 = c->dns_v4;
        *v6 = c->dns_v6;
        *port = c->dns_port[0] ? c->dns_port : L"53";
        *doh = c->dns_doh_tpl;
    } else {
        *v4 = g_dns_presets[i].v4;
        *v6 = g_dns_presets[i].v6;
        *port = g_dns_presets[i].engine_port;
        *doh = g_dns_presets[i].doh;
    }
}

void dns_engine_args(const Config *c, wchar_t *out, size_t cap)
{
    const wchar_t *v4, *v6, *port, *doh, *s;
    wchar_t a4[64] = L"", a6[64] = L"";
    size_t len;

    dns_current(c, &v4, &v6, &port, &doh);
    s = v4;
    next_token(&s, a4, 64, L", ;");
    s = v6;
    next_token(&s, a6, 64, L", ;");
    len = wcslen(out);
    if (a4[0])
        len += _snwprintf(out + len, cap - len, L"%s--dns-addr %s --dns-port %s",
                          len ? L" " : L"", a4, port);
    if (a6[0] && len < cap)
        _snwprintf(out + len, cap - len, L"%s--dnsv6-addr %s --dnsv6-port %s",
                   len ? L" " : L"", a6, port);
    out[cap - 1] = 0;
}

/* ---------------- Sistem DNS ---------------- */

/* SetInterfaceDnsSettings (Win10 2004+) - MinGW başlıklarında yok */
typedef struct {
    ULONG   Version;
    ULONG64 Flags;
    PWSTR   Domain;
    PWSTR   NameServer;
    PWSTR   SearchList;
    ULONG   RegistrationEnabled;
    ULONG   RegisterAdapterName;
    ULONG   EnableLLMNR;
    ULONG   QueryAdapterName;
    PWSTR   ProfileNameServer;
} XDNS_INTERFACE_SETTINGS;
#define XDNS_INTERFACE_SETTINGS_VERSION1 1
#define XDNS_SETTING_IPV6       0x0001
#define XDNS_SETTING_NAMESERVER 0x0002
typedef DWORD (WINAPI *SetIfDns_t)(GUID, const XDNS_INTERFACE_SETTINGS *);
typedef BOOL  (WINAPI *DnsFlush_t)(void);

static SetIfDns_t p_SetIfDns;

static BOOL load_api(void)
{
    if (!p_SetIfDns) {
        HMODULE h = GetModuleHandleW(L"iphlpapi.dll");
        if (!h)
            h = LoadLibraryW(L"iphlpapi.dll");
        if (h)
            p_SetIfDns = (SetIfDns_t)(void *)GetProcAddress(h, "SetInterfaceDnsSettings");
    }
    return p_SetIfDns != NULL;
}

static void flush_cache(void)
{
    HMODULE h = LoadLibraryW(L"dnsapi.dll");

    if (h) {
        DnsFlush_t f = (DnsFlush_t)(void *)GetProcAddress(h, "DnsFlushResolverCache");
        if (f)
            f();
        FreeLibrary(h);
    }
}

static BOOL parse_guid(const wchar_t *s, GUID *g)
{
    unsigned long d1;
    unsigned int d2, d3, b[8];
    int i;

    if (swscanf(s, L"{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",
                &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11)
        return FALSE;
    g->Data1 = d1;
    g->Data2 = (USHORT)d2;
    g->Data3 = (USHORT)d3;
    for (i = 0; i < 8; i++)
        g->Data4[i] = (BYTE)b[i];
    return TRUE;
}

static void restore_path(wchar_t *out)
{
    app_path(out, MAX_PATH, L"dns-restore.ini");
}

BOOL dns_restore_pending(void)
{
    wchar_t p[MAX_PATH];

    restore_path(p);
    return file_exists(p);
}

static void read_static_ns(BOOL v6, const wchar_t *guid, wchar_t *out, DWORD cap)
{
    wchar_t key[256];
    DWORD sz = cap * sizeof(wchar_t);

    _snwprintf(key, 256, L"SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters\\Interfaces\\%s",
               v6 ? L"Tcpip6" : L"Tcpip", guid);
    out[0] = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"NameServer", RRF_RT_REG_SZ, NULL, out, &sz) != ERROR_SUCCESS)
        out[0] = 0;
}

static DWORD set_ns(const wchar_t *guid, BOOL v6, const wchar_t *servers)
{
    XDNS_INTERFACE_SETTINGS s = {0};
    wchar_t buf[256];
    GUID g;

    if (!parse_guid(guid, &g))
        return ERROR_INVALID_PARAMETER;
    wcsncpy(buf, servers, 255);
    buf[255] = 0;
    s.Version = XDNS_INTERFACE_SETTINGS_VERSION1;
    s.Flags = XDNS_SETTING_NAMESERVER | (v6 ? XDNS_SETTING_IPV6 : 0);
    s.NameServer = buf;
    return p_SetIfDns(g, &s);
}

/* "a, b ;c" -> "a,b,c" */
static void normalize_list(const wchar_t *in, wchar_t *out, size_t cap)
{
    wchar_t tok[64];
    size_t len = 0;

    out[0] = 0;
    while (in && next_token(&in, tok, 64, L", ;") && len < cap)
        len += _snwprintf(out + len, cap - len, L"%s%s", len ? L"," : L"", tok);
    out[cap - 1] = 0;
}

static void register_doh(const wchar_t *servers, const wchar_t *tpl)
{
    wchar_t tok[64], cmd[512];
    const wchar_t *s = servers;

    while (next_token(&s, tok, 64, L",")) {
        _snwprintf(cmd, 512, L"netsh.exe dns add encryption server=%s dohtemplate=%s autoupgrade=yes udpfallback=no",
                   tok, tpl);
        if (run_hidden(cmd, 10000) != 0) {
            _snwprintf(cmd, 512, L"netsh.exe dns set encryption server=%s dohtemplate=%s autoupgrade=yes udpfallback=no",
                       tok, tpl);
            run_hidden(cmd, 10000);
        }
    }
}

BOOL dns_system_apply(const Config *c, wchar_t *err, size_t errcap)
{
    const wchar_t *v4, *v6, *port, *doh;
    wchar_t n4[256], n6[256], rpath[MAX_PATH], wguid[64], old4[256], old6[256], val[520], chk[8];
    IP_ADAPTER_ADDRESSES *aa = NULL, *a;
    ULONG len = 16 * 1024, r;
    int applied = 0;

    err[0] = 0;
    if (!load_api()) {
        _snwprintf(err, errcap, tr(L"Bu Windows sürümü DNS değiştirmeyi desteklemiyor; \"Uygulama üzerinden\" yöntemini kullan."));
        return FALSE;
    }
    dns_current(c, &v4, &v6, &port, &doh);
    normalize_list(v4, n4, 256);
    normalize_list(v6, n6, 256);
    if (!n4[0] && !n6[0]) {
        _snwprintf(err, errcap, tr(L"DNS sunucusu tanımlı değil."));
        return FALSE;
    }

    for (;;) {
        aa = malloc(len);
        if (!aa)
            return FALSE;
        r = GetAdaptersAddresses(AF_UNSPEC,
                                 GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST |
                                 GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME,
                                 NULL, aa, &len);
        if (r != ERROR_BUFFER_OVERFLOW)
            break;
        free(aa);
    }
    if (r != NO_ERROR) {
        free(aa);
        _snwprintf(err, errcap, tr(L"Ağ bağlantıları okunamadı (%lu)."), r);
        return FALSE;
    }

    restore_path(rpath);
    for (a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress ||
            a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL)
            continue;
        MultiByteToWideChar(CP_ACP, 0, a->AdapterName, -1, wguid, 64);
        /* Zaten uygulanmış mı? */
        if (GetPrivateProfileStringW(L"Restore", wguid, L"", chk, 8, rpath) > 0 || chk[0])
            continue;

        read_static_ns(FALSE, wguid, old4, 256);
        read_static_ns(TRUE, wguid, old6, 256);
        /* Önce eskiyi kaydet, sonra değiştir: çökme olursa geri dönebilelim */
        _snwprintf(val, 520, L"%s|%s", old4, old6);
        if (!WritePrivateProfileStringW(L"Restore", wguid, val, rpath)) {
            _snwprintf(err, errcap, L"dns-restore.ini yazılamadı.");
            break;
        }
        if (n4[0])
            set_ns(wguid, FALSE, n4);
        if (n6[0] && a->Ipv6IfIndex)
            set_ns(wguid, TRUE, n6);
        applied++;
    }
    free(aa);

    if (applied) {
        if (c->dns_doh && doh && doh[0]) {
            if (n4[0])
                register_doh(n4, doh);
            if (n6[0])
                register_doh(n6, doh);
        }
        flush_cache();
        app_log(L"Sistem DNS %d adaptöre uygulandı: %s %s%s", applied, n4, n6,
                c->dns_doh ? L" (DoH)" : L"");
    }
    return err[0] == 0;
}

void dns_system_restore(void)
{
    wchar_t rpath[MAX_PATH], keys[4096], val[520], *k, *bar;
    int n = 0;

    restore_path(rpath);
    if (!file_exists(rpath))
        return;
    if (load_api()) {
        keys[0] = keys[1] = 0;
        GetPrivateProfileStringW(L"Restore", NULL, L"", keys, 4096, rpath);
        for (k = keys; *k; k += wcslen(k) + 1) {
            GetPrivateProfileStringW(L"Restore", k, L"", val, 520, rpath);
            bar = wcschr(val, L'|');
            if (bar)
                *bar++ = 0;
            set_ns(k, FALSE, val);          /* boş = DHCP */
            set_ns(k, TRUE, bar ? bar : L"");
            n++;
        }
        flush_cache();
    }
    DeleteFileW(rpath);
    if (n)
        app_log(L"Sistem DNS %d adaptörde eski haline döndürüldü.", n);
}

/* ---------------- Ağ değişikliği takibi ---------------- */

static HANDLE g_notify;

static VOID NETIOAPI_API_ on_if_change(PVOID ctx, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
    (void)ctx; (void)row;
    if (type != MibInitialNotification)
        PostMessageW(g_hwnd, WM_NET_CHANGED, 0, 0);
}

void dns_watch(BOOL on)
{
    if (on && !g_notify) {
        NotifyIpInterfaceChange(AF_UNSPEC, on_if_change, NULL, FALSE, &g_notify);
    } else if (!on && g_notify) {
        CancelMibChangeNotify2(g_notify);
        g_notify = NULL;
    }
}
