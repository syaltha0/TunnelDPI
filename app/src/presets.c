/*
 * TunnelDPI - internet sağlayıcısına göre hazır ayarlar
 *
 * hazir-ayarlar.ini:
 *   [Hazir]       surum=2026-11-07
 *   [ISP.9121]    ad=Türk Telekom
 *                 profiller=Standart;Agresif (-9)     (en iyisi önce)
 *   [Profile.*]   isteğe bağlı profil tanımları (config.c ile aynı biçim)
 * Yanında hazir-ayarlar.ini.sig (openssl dgst -sha256 -sign, DER) durur.
 */
#include "presets.h"
#include "config.h"
#include "http.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ISP 32
#define PRESETS_URL L"https://raw.githubusercontent.com/%s/main/app/hazir-ayarlar.ini"

typedef struct {
    DWORD   asn;
    wchar_t name[48];
    wchar_t profiles[256];
} IspEntry;

static IspEntry g_isp[MAX_ISP];
static int      g_nisp;
static wchar_t  g_version[32];
static DWORD    g_asn;              /* bulunan sağlayıcının AS numarası, 0 = bilinmiyor */
static wchar_t  g_name[64];
static volatile LONG g_busy_upd, g_busy_isp;

/* ---------------- dosya ---------------- */

static char *read_bytes(const wchar_t *path, DWORD *len)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, rd = 0;
    char *b;

    *len = 0;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    size = GetFileSize(f, NULL);
    b = size < 1024 * 1024 ? malloc(size + 1) : NULL;
    if (b && ReadFile(f, b, size, &rd, NULL) && rd == size) {
        b[size] = 0;
        *len = size;
    } else {
        free(b);
        b = NULL;
    }
    CloseHandle(f);
    return b;
}

static BOOL write_bytes(const wchar_t *path, const char *b, DWORD len)
{
    wchar_t tmp[MAX_PATH + 8];
    HANDLE f;
    DWORD wr = 0;
    BOOL ok;

    _snwprintf(tmp, MAX_PATH + 8, L"%s.new", path);
    tmp[MAX_PATH + 7] = 0;
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(f, b, len, &wr, NULL) && wr == len;
    CloseHandle(f);
    if (ok)
        ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        DeleteFileW(tmp);
    return ok;
}

static BOOL signed_ok(const char *body, DWORD len, const char *sig, DWORD slen)
{
    return body && sig && len && slen && signature_ok(body, len, (const BYTE *)sig, slen);
}

/* ---------------- ayrıştırma ---------------- */

static void trim(wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *p = s;

    while (n && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r'))
        s[--n] = 0;
    while (*p == L' ' || *p == L'\t')
        p++;
    if (p != s)
        memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
}

static void parse(const wchar_t *text)
{
    const wchar_t *p = text;
    wchar_t line[512], *eq;
    IspEntry *cur = NULL;
    BOOL head = FALSE;

    g_nisp = 0;
    g_version[0] = 0;
    while (*p) {
        size_t n = wcscspn(p, L"\n");
        if (n > 511)
            n = 511;
        wmemcpy(line, p, n);
        line[n] = 0;
        p += wcscspn(p, L"\n");
        if (*p)
            p++;
        trim(line);
        if (!line[0] || line[0] == L';' || line[0] == L'#')
            continue;
        if (line[0] == L'[') {
            cur = NULL;
            head = !_wcsicmp(line, L"[Hazir]");
            if (!_wcsnicmp(line, L"[ISP.", 5) && g_nisp < MAX_ISP) {
                cur = &g_isp[g_nisp++];
                memset(cur, 0, sizeof(*cur));
                cur->asn = (DWORD)wcstoul(line + 5, NULL, 10);
            }
            continue;
        }
        eq = wcschr(line, L'=');
        if (!eq)
            continue;
        *eq++ = 0;
        trim(line);
        trim(eq);
        if (head && !_wcsicmp(line, L"surum")) {
            wcsncpy(g_version, eq, 31);
            g_version[31] = 0;
        } else if (cur && !_wcsicmp(line, L"ad")) {
            wcsncpy(cur->name, eq, 47);
        } else if (cur && !_wcsicmp(line, L"profiller")) {
            wcsncpy(cur->profiles, eq, 255);
        }
    }
}

static wchar_t *utf8_to_wide(const char *s, DWORD len)
{
    int off = len >= 3 && (BYTE)s[0] == 0xEF && (BYTE)s[1] == 0xBB && (BYTE)s[2] == 0xBF ? 3 : 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, s + off, (int)len - off, NULL, 0);
    wchar_t *w = malloc((n + 1) * sizeof(wchar_t));

    if (w) {
        MultiByteToWideChar(CP_UTF8, 0, s + off, (int)len - off, w, n);
        w[n] = 0;
    }
    return w;
}

BOOL presets_load(void)
{
    wchar_t path[MAX_PATH], spath[MAX_PATH + 8], *w;
    char *body, *sig;
    DWORD len, slen;
    BOOL ok = FALSE;

    app_path(path, MAX_PATH, PRESETS_FILE);
    _snwprintf(spath, MAX_PATH + 8, L"%s.sig", path);
    spath[MAX_PATH + 7] = 0;
    body = read_bytes(path, &len);
    sig = read_bytes(spath, &slen);
    if (!body) {
        /* dosya yok: kurulum eski ya da taşınabilir kopya */
    } else if (!signed_ok(body, len, sig, slen)) {
        app_log(L"Hazır ayar dosyasının imzası tutmuyor; kullanılmadı.");
    } else if ((w = utf8_to_wide(body, len)) != NULL) {
        parse(w);
        if (profiles_merge_presets(&g_cfg, w) > 0)
            config_save(&g_cfg);
        free(w);
        ok = TRUE;
    }
    free(body);
    free(sig);
    return ok;
}

const wchar_t *presets_version(void)
{
    return g_version;
}

/* ---------------- güncelleme ---------------- */

static DWORD WINAPI update_worker(LPVOID arg)
{
    wchar_t url[256], surl[264], path[MAX_PATH], spath[MAX_PATH + 8], *w;
    char *body, *sig;
    DWORD st = 0, sst = 0, len = 0, slen = 0;
    int changed = 0;

    (void)arg;
    _snwprintf(url, 256, PRESETS_URL, g_cfg.app_repo);
    url[255] = 0;
    _snwprintf(surl, 264, L"%s.sig", url);
    surl[263] = 0;
    body = http_get_n(url, NULL, 512 * 1024, &st, &len);
    sig = st == 200 ? http_get_n(surl, NULL, 4096, &sst, &slen) : NULL;
    if (body && sig && sst == 200 && signed_ok(body, len, sig, slen)) {
        w = utf8_to_wide(body, len);
        if (w) {
            const wchar_t *v = wcsstr(w, L"surum=");
            wchar_t ver[32] = L"";
            if (v) {
                v += 6;
                wcsncpy(ver, v, 31);
                ver[31] = 0;
                ver[wcscspn(ver, L"\r\n")] = 0;
            }
            if (ver[0] && wcscmp(ver, g_version)) {
                app_path(path, MAX_PATH, PRESETS_FILE);
                _snwprintf(spath, MAX_PATH + 8, L"%s.sig", path);
                spath[MAX_PATH + 7] = 0;
                /* önce imza, sonra dosya: yarıda kalırsa eşleşmeyen çift kullanılmaz */
                if (write_bytes(spath, sig, slen) && write_bytes(path, body, len))
                    changed = 1;
            }
            free(w);
        }
    } else if (body && st == 200) {
        app_log(L"İndirilen hazır ayar dosyasının imzası tutmuyor; kullanılmadı.");
    }
    free(body);
    free(sig);
    InterlockedExchange(&g_busy_upd, 0);
    if (changed)
        PostMessageW(g_hwnd, WM_PRESETS_DONE, 1, 0);
    return 0;
}

void presets_update(void)
{
    HANDLE th;

    if (!g_cfg.app_repo[0] || InterlockedCompareExchange(&g_busy_upd, 1, 0))
        return;
    th = CreateThread(NULL, 64 * 1024, update_worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&g_busy_upd, 0);
}

/* ---------------- sağlayıcı ---------------- */

static const IspEntry *entry(DWORD asn)
{
    int i;

    for (i = 0; i < g_nisp; i++)
        if (g_isp[i].asn == asn)
            return &g_isp[i];
    return NULL;
}

static DWORD WINAPI isp_worker(LPVOID arg)
{
    char *body, ip[64] = "", holder[128] = "";
    wchar_t url[256], wip[64], name[64] = L"";
    DWORD st = 0, asn = 0;
    const char *p;

    (void)arg;
    /* RIPE NCC: genel IP adresi → AS numarası → kayıtlı ad */
    body = http_get(L"https://stat.ripe.net/data/whats-my-ip/data.json", NULL, 64 * 1024, &st);
    if (body && st == 200)
        json_str(body, "ip", ip, sizeof(ip));
    free(body);
    if (ip[0]) {
        MultiByteToWideChar(CP_UTF8, 0, ip, -1, wip, 64);
        _snwprintf(url, 256, L"https://stat.ripe.net/data/network-info/data.json?resource=%s", wip);
        url[255] = 0;
        body = http_get(url, NULL, 64 * 1024, &st);
        if (body && st == 200 && (p = strstr(body, "\"asns\"")) != NULL) {
            p += strcspn(p, "0123456789]");
            if (*p >= '0' && *p <= '9')
                asn = strtoul(p, NULL, 10);
        }
        free(body);
    }
    if (asn && !entry(asn)) {
        _snwprintf(url, 256, L"https://stat.ripe.net/data/as-overview/data.json?resource=AS%lu", asn);
        url[255] = 0;
        body = http_get(url, NULL, 64 * 1024, &st);
        if (body && st == 200 && json_str(body, "holder", holder, sizeof(holder))) {
            /* "TTNET - TTNET A.S." / "TURKCELL-AS TURKCELL ILETISIM..." → kısa ad */
            char *d = strchr(holder, ' ');
            if (d)
                *d = 0;
            d = strstr(holder, "-AS");
            if (d && d[3] == 0)
                *d = 0;
            MultiByteToWideChar(CP_UTF8, 0, holder, -1, name, 64);
            name[63] = 0;
        }
        free(body);
    }
    if (asn) {
        const IspEntry *e = entry(asn);
        g_asn = asn;
        wcscpy(g_name, e && e->name[0] ? e->name : name[0] ? name : L"");
        if (!g_name[0])
            _snwprintf(g_name, 64, L"AS%lu", asn);
        app_log(L"İnternet sağlayıcı: %s (AS%lu)", g_name, asn);
    }
    InterlockedExchange(&g_busy_isp, 0);
    PostMessageW(g_hwnd, WM_PRESETS_DONE, 2, 0);
    return 0;
}

void isp_detect(void)
{
    HANDLE th;

    if (InterlockedCompareExchange(&g_busy_isp, 1, 0))
        return;
    th = CreateThread(NULL, 64 * 1024, isp_worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&g_busy_isp, 0);
}

const wchar_t *isp_name(void)
{
    return g_name;
}

int isp_profiles(const wchar_t **names, int max)
{
    static wchar_t buf[256];
    const IspEntry *e = entry(g_asn);
    wchar_t *ctx = NULL, *t;
    int n = 0;

    if (!e || !g_asn)
        return 0;
    wcscpy(buf, e->profiles);
    for (t = wcstok(buf, L";", &ctx); t && n < max; t = wcstok(NULL, L";", &ctx)) {
        trim(t);
        if (t[0] && config_find(&g_cfg, t) >= 0)
            names[n++] = t;
    }
    return n;
}
