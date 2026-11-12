/*
 * TunnelDPI - güncelleme denetimi ve kendini güncelleme
 *
 * Ayarlardaki depo (sahip/ad) için GitHub API'sinden en son sürümü okur.
 * Sürümde adında "Setup" geçen bir .exe varsa "Güncelle" onu indirip
 * --silent ile çalıştırır; kurulum çalışan örneği kapatır, dosyaları
 * değiştirir ve uygulamayı yeniden açar. Yeni sürüm bildirimi kararını ana
 * iş parçacığı verir (WM_UPDATE_DONE), ayarlara yalnızca oradan yazılır.
 *
 * Güvenlik: sürümdeki SHA256SUMS.txt yayıncının anahtarıyla imzalıdır
 * (SHA256SUMS.txt.sig). İmza tutmazsa ya da kurulum dosyasının özeti listede
 * yazanla aynı değilse hiçbir şey çalıştırılmaz. Dosya, yalnızca yöneticinin
 * yazabildiği uygulama klasörüne yazılır ve oradan başlatılır.
 */
#include "update.h"
#include "config.h"
#include "http.h"
#include "verify.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static UpdateInfo g_u;
static volatile LONG g_busy;

/* "v1.10.2" ile "1.9" gibi sürümleri sayısal karşılaştırır */
int version_cmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && !iswdigit(*a)) a++;
    while (*b && !iswdigit(*b)) b++;
    while (*a || *b) {
        long x = wcstol(a, (wchar_t **)&a, 10), y = wcstol(b, (wchar_t **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == L'.') a++; else if (*a) break;
        if (*b == L'.') b++; else if (*b) break;
    }
    return 0;
}

/* assets içindeki indirme adresleri: kurulum (...Setup....exe), özet listesi ve imzası */
static void find_assets(const char *body)
{
    const char *p = body, *name;
    char u[512];

    g_u.setup[0] = g_u.sums[0] = g_u.sig[0] = 0;
    while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
        if (json_str(p, "browser_download_url", u, sizeof(u))) {
            size_t n = strlen(u);
            name = strrchr(u, '/');
            name = name ? name + 1 : u;
            if (!g_u.setup[0] && n > 4 && !_stricmp(u + n - 4, ".exe") && (strstr(name, "Setup") || strstr(name, "setup"))) {
                MultiByteToWideChar(CP_UTF8, 0, u, -1, g_u.setup, 512);
                strncpy(g_u.setup_name, name, sizeof(g_u.setup_name) - 1);
            } else if (!_stricmp(name, "SHA256SUMS.txt")) {
                MultiByteToWideChar(CP_UTF8, 0, u, -1, g_u.sums, 512);
            } else if (!_stricmp(name, "SHA256SUMS.txt.sig")) {
                MultiByteToWideChar(CP_UTF8, 0, u, -1, g_u.sig, 512);
            }
        }
        p += 22;
    }
    g_u.signed_release = g_u.setup[0] && g_u.sums[0] && g_u.sig[0];
}

static DWORD WINAPI check_worker(LPVOID arg)
{
    wchar_t *repo = arg, api[256];
    char t[64], u[256], *body;
    DWORD st;
    BOOL ok = FALSE;

    _snwprintf(api, 256, L"https://api.github.com/repos/%s/releases/latest", repo);
    api[255] = 0;
    body = http_get(api, L"accept: application/vnd.github+json\r\n", 1024 * 1024, &st);
    if (body && st == 200 && json_str(body, "tag_name", t, sizeof(t))) {
        MultiByteToWideChar(CP_UTF8, 0, t, -1, g_u.tag, 64);
        if (json_str(body, "html_url", u, sizeof(u)))
            MultiByteToWideChar(CP_UTF8, 0, u, -1, g_u.url, 256);
        find_assets(body);
        g_u.newer = version_cmp(g_u.tag, APP_VERSION) > 0;
        ok = TRUE;
    }
    free(body);
    free(repo);
    g_u.when = GetTickCount();
    g_u.state = ok ? UPD_DONE : UPD_FAIL;
    InterlockedExchange(&g_busy, 0);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_UPDATE_DONE, 0, 0);
    return 0;
}

static BOOL spawn(LPTHREAD_START_ROUTINE fn, void *arg)
{
    HANDLE th = CreateThread(NULL, 64 * 1024, fn, arg, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);

    if (!th)
        return FALSE;
    CloseHandle(th);
    return TRUE;
}

void update_check(void)
{
    wchar_t *repo;

    if (!g_cfg.app_repo[0] || InterlockedExchange(&g_busy, 1))
        return;
    repo = _wcsdup(g_cfg.app_repo);
    g_u.state = UPD_RUNNING;
    if (!repo || !spawn(check_worker, repo)) {
        free(repo);
        g_u.state = UPD_FAIL;
        g_busy = 0;
    }
}

/* İmzalı özet listesinde bu dosyanın özeti ("<64 hex>  ad") */
static BOOL sums_lookup(const char *sums, const char *name, char hex[65])
{
    const char *p = sums;
    size_t nl = strlen(name);

    while (p && *p) {
        const char *e = strchr(p, '\n'), *f = p + 64;
        size_t ll = e ? (size_t)(e - p) : strlen(p);
        if (ll > 66 && (f[0] == ' ' || f[0] == '\t')) {
            while (*f == ' ' || *f == '\t' || *f == '*')
                f++;
            if ((size_t)(p + ll - f) >= nl && !strncmp(f, name, nl) &&
                (f[nl] == '\r' || f[nl] == '\n' || f[nl] == 0 || f + nl == p + ll)) {
                memcpy(hex, p, 64);
                hex[64] = 0;
                return TRUE;
            }
        }
        p = e ? e + 1 : NULL;
    }
    return FALSE;
}

/* 0 başarı, 1 indirilemedi, 2 doğrulanamadı */
static int download_verified(wchar_t *path, size_t cap)
{
    char *sums = NULL, *sig = NULL, *body = NULL, want[65], got[65];
    BYTE h[32];
    DWORD st, slen = 0, glen = 0, len = 0, wr;
    HANDLE f;
    int rc = 1;

    sums = http_get_n(g_u.sums, NULL, 64 * 1024, &st, &slen);
    if (!sums || st != 200)
        goto out;
    sig = http_get_n(g_u.sig, NULL, 1024, &st, &glen);
    if (!sig || st != 200)
        goto out;
    rc = 2;
    if (!signature_ok(sums, slen, (const BYTE *)sig, glen)) {
        app_log(L"Güncelleme: özet listesinin imzası doğrulanamadı; güncelleme yapılmadı.");
        goto out;
    }
    if (!sums_lookup(sums, g_u.setup_name, want)) {
        app_log(L"Güncelleme: kurulum dosyası imzalı listede yok; güncelleme yapılmadı.");
        goto out;
    }
    rc = 1;
    body = http_get_n(g_u.setup, NULL, 64 * 1024 * 1024, &st, &len);
    if (!body || st != 200 || len < 4096)
        goto out;
    rc = 2;
    if (!sha256_buf(body, len, h))
        goto out;
    hex_lower(h, 32, got);
    if (_stricmp(got, want)) {
        app_log(L"Güncelleme: indirilen dosyanın özeti imzalı listedekiyle aynı değil; güncelleme yapılmadı.");
        goto out;
    }
    /* uygulama klasörü yalnızca yöneticiye yazılabilir: doğrulanan dosya çalıştırılana dek değiştirilemez */
    app_path(path, cap, L"update");
    CreateDirectoryW(path, NULL);
    app_path(path, cap, L"update\\" APP_ID L"-Setup.exe");
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        rc = 1;
        goto out;
    }
    rc = WriteFile(f, body, len, &wr, NULL) && wr == len ? 0 : 1;
    CloseHandle(f);
    if (rc == 0)
        app_log(L"Güncelleme %s indirildi ve doğrulandı.", g_u.tag);
out:
    free(sums);
    free(sig);
    free(body);
    return rc;
}

static DWORD WINAPI install_worker(LPVOID arg)
{
    wchar_t path[MAX_PATH];
    int rc;
    (void)arg;

    rc = download_verified(path, MAX_PATH);
    if (rc == 0) {
        /* kurulum bu süreci kapatıp yenisini başlatır */
        if ((INT_PTR)ShellExecuteW(NULL, L"open", path, L"--silent --update", NULL, SW_SHOWNORMAL) <= 32)
            rc = 1;
    }
    g_u.state = UPD_DONE;
    InterlockedExchange(&g_busy, 0);
    if (rc && g_hwnd)
        PostMessageW(g_hwnd, WM_UPDATE_DONE, (WPARAM)rc, 0);
    return 0;
}

void update_install(void)
{
    if (!g_u.newer || !g_u.signed_release || InterlockedExchange(&g_busy, 1))
        return;
    g_u.state = UPD_DOWNLOADING;
    if (!spawn(install_worker, NULL)) {
        g_u.state = UPD_DONE;
        g_busy = 0;
    }
}

const UpdateInfo *update_get(void)
{
    return &g_u;
}
