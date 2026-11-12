/*
 * TunnelDPI - uzaktan site listesi aboneliği
 *
 * Desteklenen biçimler: düz alan adı listesi, hosts dosyası
 * ("0.0.0.0 site.com"), adblock ("||site.com^"). Yorumlar (#, !) ve geçersiz
 * satırlar atlanır. İndirme arka planda yapılır; bitince WM_SUBS_DONE gider.
 */
#include "subs.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#define MAX_JOBS 32

static volatile LONG g_busy;

/* Türkçe dosya adları için INI'yi UTF-16 olarak oluştur (Windows API'si ANSI açar) */
static void ini(wchar_t *out)
{
    app_path(out, MAX_PATH, L"lists\\subscriptions.ini");
    if (!file_exists(out)) {
        HANDLE f = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            static const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD wr;
            WriteFile(f, bom, 2, &wr, NULL);
            CloseHandle(f);
        }
    }
}

void subs_get_url(const wchar_t *file, wchar_t *url, size_t cap)
{
    wchar_t p[MAX_PATH];

    ini(p);
    GetPrivateProfileStringW(L"Subscriptions", file, L"", url, (DWORD)cap, p);
}

void subs_set_url(const wchar_t *file, const wchar_t *url)
{
    wchar_t p[MAX_PATH], t[1024];

    ini(p);
    wcsncpy(t, url, 1023);
    t[1023] = 0;
    trim_ws(t);
    WritePrivateProfileStringW(L"Subscriptions", file, t[0] ? t : NULL, p);
    if (!t[0])
        WritePrivateProfileStringW(L"Updated", file, NULL, p);
}

void subs_rename(const wchar_t *from, const wchar_t *to)
{
    wchar_t url[1024], p[MAX_PATH], upd[32];

    subs_get_url(from, url, 1024);
    if (!url[0])
        return;
    ini(p);
    GetPrivateProfileStringW(L"Updated", from, L"", upd, 32, p);
    WritePrivateProfileStringW(L"Subscriptions", from, NULL, p);
    WritePrivateProfileStringW(L"Updated", from, NULL, p);
    WritePrivateProfileStringW(L"Subscriptions", to, url, p);
    if (upd[0])
        WritePrivateProfileStringW(L"Updated", to, upd, p);
}

LONG64 subs_updated(const wchar_t *file)
{
    wchar_t p[MAX_PATH], v[32];

    ini(p);
    GetPrivateProfileStringW(L"Updated", file, L"0", v, 32, p);
    return _wtoi64(v);
}

BOOL subs_running(void)
{
    return g_busy != 0;
}

/* Bir satırdan alan adı çıkar: 1 = alan adı, 2 = istisna ("@@||site^" ->
   "@@site" olarak yazılır), 0 = geçersiz. Adblock kurallarından yalnızca tüm
   alan adını kapsayanlar alınır; yol, joker ve $third-party gibi seçenekli
   kurallar alınmaz, yoksa sitenin kendisi de engellenirdi. */
static int parse_line(char *line, char *out, size_t cap)
{
    char *p = line, *e;
    size_t n;
    int allow = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p || *p == '#' || *p == '!' || *p == '[')
        return 0;
    e = p + strcspn(p, "#\r\n");
    *e = 0;
    if (p[0] == '@' && p[1] == '@') {
        allow = 1;
        p += 2;
    }
    if (p[0] == '|' && p[1] == '|') {
        /* adblock: "||site^" ya da "||site^$important" */
        p += 2;
        n = strcspn(p, "^/$*| \t");
        if (p[n] == '^') {
            const char *rest = p + n + 1;
            if (*rest == '|')
                rest++;
            if (*rest && *rest != ' ' && *rest != '\t' && strncmp(rest, "$important", 10))
                return 0;
        } else if (p[n] && p[n] != ' ' && p[n] != '\t') {
            return 0;
        }
    } else {
        /* hosts biçimi: "0.0.0.0 site" / "127.0.0.1 site" / ":: site" */
        if (!strncmp(p, "0.0.0.0", 7) || !strncmp(p, "127.0.0.1", 9) || !strncmp(p, "::", 2)) {
            p += strcspn(p, " \t");
            while (*p == ' ' || *p == '\t')
                p++;
        }
        n = strcspn(p, " \t");
    }
    if (!n || n >= cap)
        return 0;
    memcpy(out, p, n);
    out[n] = 0;
    while (n && out[n - 1] == '.')
        out[--n] = 0;
    for (e = out; *e; e++) {
        *e = (char)tolower((unsigned char)*e);
        if (!(isalnum((unsigned char)*e) || *e == '.' || *e == '-' || *e == '_'))
            return 0;
    }
    if (!n || !strchr(out, '.') || !strcmp(out, "localhost") || out[0] == '.' || strspn(out, "0123456789.") == n)
        return 0;
    return allow ? 2 : 1;
}

typedef struct {
    int     n;
    wchar_t file[MAX_JOBS][MAX_PATH];
    wchar_t url[MAX_JOBS][1024];
} Jobs;

static DWORD WINAPI worker(LPVOID arg)
{
    Jobs *j = arg;
    int i, ok = 0, fail = 0;

    for (i = 0; i < j->n; i++) {
        DWORD st = 0;
        char *body = http_get(j->url[i], NULL, 16 * 1024 * 1024, &st), *line, *next;
        size_t cap, len = 0;
        char *out, host[256];
        wchar_t rel[MAX_PATH + 8], path[MAX_PATH], *w;
        int hosts = 0, wn;

        if (!body || st != 200) {
            app_log(L"Liste güncellenemedi (%s): HTTP %lu", j->file[i], st);
            free(body);
            fail++;
            continue;
        }
        cap = strlen(body) + 16;
        out = malloc(cap * 2);
        if (!out) {
            free(body);
            fail++;
            continue;
        }
        for (line = body; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            int kind = parse_line(line, host, sizeof(host));
            if (kind && len + strlen(host) + 5 < cap * 2) {
                len += (size_t)sprintf(out + len, kind == 2 ? "@@%s\r\n" : "%s\r\n", host);
                hosts += kind == 1;
            }
        }
        out[len] = 0;
        free(body);
        if (!hosts) {
            app_log(L"Liste güncellenemedi (%s): geçerli alan adı yok", j->file[i]);
            free(out);
            fail++;
            continue;
        }
        wn = MultiByteToWideChar(CP_UTF8, 0, out, -1, NULL, 0);
        w = malloc((size_t)wn * sizeof(wchar_t));
        if (w) {
            MultiByteToWideChar(CP_UTF8, 0, out, -1, w, wn);
            _snwprintf(rel, MAX_PATH + 8, L"lists\\%s", j->file[i]);
            app_path(path, MAX_PATH, rel);
            if (write_text_file(path, w)) {
                wchar_t p[MAX_PATH], v[32];
                ini(p);
                _snwprintf(v, 32, L"%lld", (long long)time(NULL));
                WritePrivateProfileStringW(L"Updated", j->file[i], v, p);
                app_log(L"Liste güncellendi: %s (%d alan adı)", j->file[i], hosts);
                ok++;
            } else {
                fail++;
            }
            free(w);
        }
        free(out);
    }
    free(j);
    InterlockedExchange(&g_busy, 0);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_SUBS_DONE, (WPARAM)ok, (LPARAM)fail);
    return 0;
}

void subs_update(const wchar_t *file, BOOL force)
{
    wchar_t p[MAX_PATH], *keys;
    const wchar_t *k;
    Jobs *j;
    HANDLE th;
    LONG64 now = (LONG64)time(NULL);

    if (InterlockedExchange(&g_busy, 1))
        return;
    j = calloc(1, sizeof(Jobs));
    keys = malloc(8192 * sizeof(wchar_t));
    if (!j || !keys) {
        free(j);
        free(keys);
        g_busy = 0;
        return;
    }
    ini(p);
    keys[0] = keys[1] = 0;
    GetPrivateProfileStringW(L"Subscriptions", NULL, L"", keys, 8192, p);
    for (k = keys; *k && j->n < MAX_JOBS; k += wcslen(k) + 1) {
        if (file && _wcsicmp(k, file))
            continue;
        if (!force && now - subs_updated(k) < 24 * 3600)
            continue;
        wcsncpy(j->file[j->n], k, MAX_PATH - 1);
        subs_get_url(k, j->url[j->n], 1024);
        if (j->url[j->n][0])
            j->n++;
    }
    free(keys);
    if (!j->n) {
        free(j);
        g_busy = 0;
        return;
    }
    th = CreateThread(NULL, 64 * 1024, worker, j, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    else {
        free(j);
        g_busy = 0;
    }
}
