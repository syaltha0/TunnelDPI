/*
 * TunnelDPI - tarayıcıların kendi "Güvenli DNS" ayarı
 *
 * Chromium tabanlılar ayarı "Local State" dosyasında tutar:
 *   "dns_over_https":{"mode":"secure","templates":"https://..."}
 * Anahtar yoksa varsayılan "automatic"tir (sistem DNS sunucusu destekliyorsa
 * şifreli bağlanır). Firefox'ta prefs.js içindeki network.trr.mode:
 * 2 = önce şifreli, 3 = yalnız şifreli; yoksa kapalı.
 */
#include "browsers.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_B 16

static BrowserDns g_b[MAX_B];
static int   g_n;
static DWORD g_when;

/* "https://chrome.cloudflare-dns.com/dns-query" → "Cloudflare" */
static void provider(const wchar_t *url, wchar_t *out, size_t cap)
{
    static const struct { const wchar_t *key, *name; } known[] = {
        { L"cloudflare", L"Cloudflare" }, { L"google", L"Google" }, { L"quad9", L"Quad9" },
        { L"adguard", L"AdGuard" }, { L"nextdns", L"NextDNS" }, { L"opendns", L"OpenDNS" },
        { L"umbrella", L"OpenDNS" }, { L"cleanbrowsing", L"CleanBrowsing" }, { L"yandex", L"Yandex" },
    };
    const wchar_t *h = wcsstr(url, L"://");
    size_t i, n = 0;

    out[0] = 0;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (wcsstr(url, known[i].key)) {
            wcsncpy(out, known[i].name, cap - 1);
            out[cap - 1] = 0;
            return;
        }
    h = h ? h + 3 : url;
    while (h[n] && h[n] != L'/' && h[n] != L'"' && h[n] != L' ' && n + 1 < cap) {
        out[n] = h[n];
        n++;
    }
    out[n] = 0;
}

/* JSON'da "key":"değer" (ilk eşleşme, başlangıç s'den) */
static BOOL jstr(const wchar_t *s, const wchar_t *key, wchar_t *out, size_t cap)
{
    wchar_t pat[64];
    const wchar_t *p, *e;
    size_t n;

    _snwprintf(pat, 64, L"\"%s\":\"", key);
    p = wcsstr(s, pat);
    if (!p)
        return FALSE;
    p += wcslen(pat);
    e = wcschr(p, L'"');
    if (!e)
        return FALSE;
    n = (size_t)(e - p);
    if (n >= cap)
        n = cap - 1;
    wmemcpy(out, p, n);
    out[n] = 0;
    return TRUE;
}

static void chromium(const wchar_t *name, const wchar_t *env, const wchar_t *rel)
{
    wchar_t base[MAX_PATH], path[MAX_PATH], mode[32], tpl[512], *t;
    const wchar_t *d;
    BrowserDns *b;

    if (g_n >= MAX_B || !GetEnvironmentVariableW(env, base, MAX_PATH))
        return;
    _snwprintf(path, MAX_PATH, L"%s\\%s\\Local State", base, rel);
    path[MAX_PATH - 1] = 0;
    t = read_text_file(path);
    if (!t)
        return;
    b = &g_b[g_n++];
    memset(b, 0, sizeof(*b));
    b->name = name;
    b->state = BDNS_AUTO;
    d = wcsstr(t, L"\"dns_over_https\":{");
    if (d && jstr(d, L"mode", mode, 32)) {
        if (!wcscmp(mode, L"secure"))
            b->state = BDNS_ON;
        else if (!wcscmp(mode, L"off"))
            b->state = BDNS_OFF;
        if (b->state == BDNS_ON && jstr(d, L"templates", tpl, 512))
            provider(tpl, b->server, 64);
    }
    free(t);
}

/* Firefox ve türevleri (Zen, LibreWolf…): rel = APPDATA altındaki Profiles klasörü */
static void firefox(const wchar_t *name, const wchar_t *rel)
{
    wchar_t base[MAX_PATH], pat[MAX_PATH], path[MAX_PATH], best[MAX_PATH] = L"", *t, *p;
    WIN32_FIND_DATAW fd;
    FILETIME newest = { 0 };
    HANDLE h;
    BrowserDns *b;
    int mode = 0;

    if (g_n >= MAX_B || !GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH))
        return;
    _snwprintf(pat, MAX_PATH, L"%s\\%s\\*", base, rel);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
            continue;
        _snwprintf(path, MAX_PATH, L"%s\\%s\\%s\\prefs.js", base, rel, fd.cFileName);
        /* en son kullanılan profil */
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &newest) > 0) {
            newest = a.ftLastWriteTime;
            wcscpy(best, path);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0] || !(t = read_text_file(best)))
        return;
    b = &g_b[g_n++];
    memset(b, 0, sizeof(*b));
    b->name = name;
    p = wcsstr(t, L"\"network.trr.mode\",");
    if (p)
        mode = _wtoi(p + 19 + wcsspn(p + 19, L" "));
    b->state = mode == 3 ? BDNS_ON : mode == 2 ? BDNS_AUTO : BDNS_OFF;
    if (b->state != BDNS_OFF) {
        p = wcsstr(t, L"\"network.trr.uri\", \"");
        provider(p ? p + 20 : L"cloudflare", b->server, 64);
    }
    free(t);
}

int browsers_dns(const BrowserDns **out)
{
    if (!g_when || GetTickCount() - g_when > 30000) {
        g_n = 0;
        chromium(L"Chrome", L"LOCALAPPDATA", L"Google\\Chrome\\User Data");
        chromium(L"Edge", L"LOCALAPPDATA", L"Microsoft\\Edge\\User Data");
        chromium(L"Brave", L"LOCALAPPDATA", L"BraveSoftware\\Brave-Browser\\User Data");
        chromium(L"Opera", L"APPDATA", L"Opera Software\\Opera Stable");
        chromium(L"Opera GX", L"APPDATA", L"Opera Software\\Opera GX Stable");
        chromium(L"Chromium", L"LOCALAPPDATA", L"Chromium\\User Data");
        chromium(L"Vivaldi", L"LOCALAPPDATA", L"Vivaldi\\User Data");
        chromium(L"Yandex", L"LOCALAPPDATA", L"Yandex\\YandexBrowser\\User Data");
        chromium(L"Thorium", L"LOCALAPPDATA", L"Thorium\\User Data");
        chromium(L"Arc", L"LOCALAPPDATA", L"Packages\\TheBrowserCompany.Arc_ttt1ap7aakyb4\\LocalCache\\Local\\Arc\\User Data");
        firefox(L"Firefox", L"Mozilla\\Firefox\\Profiles");
        firefox(L"Zen", L"zen\\Profiles");
        firefox(L"LibreWolf", L"librewolf\\Profiles");
        firefox(L"Floorp", L"Floorp\\Profiles");
        firefox(L"Waterfox", L"Waterfox\\Profiles");
        g_when = GetTickCount();
    }
    *out = g_b;
    return g_n;
}
