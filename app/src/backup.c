/*
 * TunnelDPI - ayarları yedekleme ve geri yükleme
 *
 * Biçim (UTF-8 metin):
 *   TunnelDPI yedeği 1
 *   @@@ dosya: lists\discord-sni.txt
 *   ...dosyanın içeriği...
 *   @@@ dosya16: tunneldpi.ini        (UTF-16 olarak geri yazılır)
 * Geri yüklemede yalnızca bilinen yerlere (uygulama klasörü ve lists\)
 * düz dosya adları kabul edilir; ".." ya da sürücü harfi içeren yollar atlanır.
 */
#include "backup.h"
#include "config.h"
#include "block.h"
#include <stdio.h>
#include <stdlib.h>

#define MAGIC   L"TunnelDPI yedeği 1"
#define MARK    L"\n@@@ dosya: "
#define MARK16  L"\n@@@ dosya16: "

typedef struct { wchar_t *buf; size_t len, cap; } Buf;

static BOOL put(Buf *b, const wchar_t *s)
{
    size_t n = wcslen(s);

    if (b->len + n + 1 > b->cap) {
        size_t nc = (b->len + n + 1) * 2;
        wchar_t *nb = realloc(b->buf, nc * sizeof(wchar_t));
        if (!nb)
            return FALSE;
        b->buf = nb;
        b->cap = nc;
    }
    wmemcpy(b->buf + b->len, s, n + 1);
    b->len += n;
    return TRUE;
}

/* UTF-16 (BOM'lu) ya da UTF-8 dosyayı okur; utf16 hangisi olduğunu söyler */
static wchar_t *read_any(const wchar_t *path, BOOL *utf16)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, rd;
    BYTE *raw;
    wchar_t *w = NULL;

    *utf16 = FALSE;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 8 * 1024 * 1024 || !(raw = malloc(sz + 2))) {
        CloseHandle(f);
        return NULL;
    }
    if (ReadFile(f, raw, sz, &rd, NULL) && rd == sz) {
        if (sz >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
            *utf16 = TRUE;
            w = malloc(sz);
            if (w) {
                memcpy(w, raw + 2, sz - 2);
                w[(sz - 2) / 2] = 0;
            }
        }
    }
    CloseHandle(f);
    free(raw);
    return *utf16 ? w : read_text_file(path);
}

static BOOL write_utf16(const wchar_t *path, const wchar_t *text)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD wr;
    BOOL ok;
    WCHAR bom = 0xFEFF;

    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(f, &bom, 2, &wr, NULL) && WriteFile(f, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(f);
    return ok;
}

static int add_file(Buf *b, const wchar_t *rel)
{
    wchar_t path[MAX_PATH], *t;
    BOOL u16;

    app_path(path, MAX_PATH, rel);
    t = read_any(path, &u16);
    if (!t)
        return 0;
    put(b, u16 ? MARK16 : MARK);
    put(b, rel);
    put(b, L"\n");
    put(b, t);
    free(t);
    return 1;
}

int backup_save(const wchar_t *dest)
{
    wchar_t pat[MAX_PATH], rel[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    Buf b = { 0 };
    int n = 0;

    config_save(&g_cfg);                       /* bekleyen değişiklikler de girsin */
    put(&b, MAGIC);
    n += add_file(&b, APP_INI);
    n += add_file(&b, APPS_FILE);
    n += add_file(&b, L"lists\\subscriptions.ini");
    _snwprintf(rel, MAX_PATH, L"%s\\%s", BLOCK_DIR, BLOCK_OWN);
    n += add_file(&b, rel);
    _snwprintf(rel, MAX_PATH, L"%s\\%s", BLOCK_DIR, BLOCK_ALLOW);
    n += add_file(&b, rel);
    app_path(pat, MAX_PATH, L"lists\\*.txt");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            _snwprintf(rel, MAX_PATH, L"lists\\%s", fd.cFileName);
            n += add_file(&b, rel);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!b.buf || !write_text_file(dest, b.buf))
        n = -1;
    free(b.buf);
    return n;
}

/* Yalnızca uygulama klasöründeki ya da lists\ altındaki düz dosya adları */
static BOOL safe_rel(const wchar_t *rel)
{
    const wchar_t *name;

    if (!rel[0] || wcsstr(rel, L"..") || wcschr(rel, L':') || wcschr(rel, L'/') || rel[0] == L'\\')
        return FALSE;
    if (!_wcsnicmp(rel, L"lists\\engel\\", 12))
        name = rel + 12;
    else if (!_wcsnicmp(rel, L"lists\\", 6))
        name = rel + 6;
    else
        name = rel;
    return !wcschr(name, L'\\') && wcslen(name) < 120;
}

int backup_restore(const wchar_t *src)
{
    wchar_t *t = read_text_file(src), *p, *next, *nl, path[MAX_PATH], dir[MAX_PATH];
    int n = 0;

    if (!t)
        return -1;
    if (wcsncmp(t, MAGIC, wcslen(MAGIC))) {
        free(t);
        return -1;
    }
    app_path(dir, MAX_PATH, L"lists");
    CreateDirectoryW(dir, NULL);
    app_path(dir, MAX_PATH, BLOCK_DIR);
    CreateDirectoryW(dir, NULL);
    p = wcsstr(t, L"\n@@@ dosya");
    while (p) {
        BOOL u16 = !wcsncmp(p, MARK16, wcslen(MARK16));
        wchar_t *rel = p + wcslen(u16 ? MARK16 : MARK), *body;

        nl = wcschr(rel, L'\n');
        if (!nl)
            break;
        *nl = 0;
        if (nl > rel && nl[-1] == L'\r')
            nl[-1] = 0;
        body = nl + 1;
        next = wcsstr(body, L"\n@@@ dosya");
        if (next)
            *next = 0;
        if (safe_rel(rel)) {
            app_path(path, MAX_PATH, rel);
            if (u16 ? write_utf16(path, body) : write_text_file(path, body))
                n++;
        }
        if (!next)
            break;
        *next = L'\n';
        p = next;
    }
    free(t);
    return n;
}
