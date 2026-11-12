/*
 * TunnelDPI - reklam ve izleyici engelleme
 *
 * Klasik motor (--block-list) listedeki sitelere giden bağlantıları TLS
 * SNI ya da HTTP Host'tan tanıyıp keser. Bu dosya hazır liste kataloğunu,
 * liste dosyalarını ve motor argümanlarını yönetir. Tarayıcılar QUIC'te
 * (HTTP/3) SNI'yi motorun okumadığı biçimde gönderdiğinden engelleme açıkken
 * QUIC kapatılır (-q); tarayıcılar TCP'ye döner.
 */
#include "block.h"
#include "subs.h"
#include <stdio.h>
#include <stdlib.h>

const BlockSource g_block_src[] = {
    { L"hagezi-light.txt", L"Reklam ve izleyiciler",
      L"Önerilen · dengeli, siteleri bozmaz (HaGeZi Light)",
      L"https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/light-onlydomains.txt" },
    { L"hagezi-pro.txt", L"Reklam ve izleyiciler (geniş)",
      L"Daha çok reklam yakalar, daha çok bellek kullanır (HaGeZi Pro)",
      L"https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/pro-onlydomains.txt" },
    { L"adguard-dns.txt", L"AdGuard DNS filtresi",
      L"AdGuard'ın DNS düzeyindeki reklam ve izleyici listesi",
      L"https://adguardteam.github.io/AdGuardSDNSFilter/Filters/filter.txt" },
    { L"stevenblack.txt", L"StevenBlack hosts",
      L"Birçok kaynaktan birleştirilmiş klasik hosts listesi",
      L"https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts" },
    { L"hagezi-tif.txt", L"Zararlı siteler",
      L"Dolandırıcılık ve virüs yayan siteler (HaGeZi TIF)",
      L"https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/tif.mini-onlydomains.txt" },
};
const int g_nblock_src = sizeof(g_block_src) / sizeof(g_block_src[0]);

void block_path(const wchar_t *file, wchar_t *out)
{
    wchar_t rel[MAX_PATH];

    _snwprintf(rel, MAX_PATH, L"%s\\%s", BLOCK_DIR, file);
    rel[MAX_PATH - 1] = 0;
    app_path(out, MAX_PATH, rel);
}

void block_subkey(const wchar_t *file, wchar_t *out)
{
    _snwprintf(out, MAX_PATH, L"engel\\%s", file);
    out[MAX_PATH - 1] = 0;
}

BOOL block_has(const Config *c, const wchar_t *file)
{
    const wchar_t *s = c->block_lists;
    wchar_t tok[MAX_PATH];

    while (next_token(&s, tok, MAX_PATH, L";"))
        if (!_wcsicmp(tok, file))
            return TRUE;
    return FALSE;
}

void block_set(Config *c, int src, BOOL on)
{
    const BlockSource *b = &g_block_src[src];
    wchar_t key[MAX_PATH], path[MAX_PATH], out[BIG_CAP], tok[MAX_PATH];
    const wchar_t *s = c->block_lists;
    size_t len = 0;

    out[0] = 0;
    while (next_token(&s, tok, MAX_PATH, L";"))
        if (_wcsicmp(tok, b->file))
            len += _snwprintf(out + len, BIG_CAP - len, L"%s%s", len ? L";" : L"", tok);
    if (on && len + wcslen(b->file) + 2 < BIG_CAP)
        _snwprintf(out + len, BIG_CAP - len, L"%s%s", len ? L";" : L"", b->file);
    out[BIG_CAP - 1] = 0;
    wcscpy(c->block_lists, out);

    block_subkey(b->file, key);
    block_path(b->file, path);
    if (on) {
        wchar_t dir[MAX_PATH];
        app_path(dir, MAX_PATH, BLOCK_DIR);
        CreateDirectoryW(dir, NULL);
        subs_set_url(key, b->url);
        if (!file_exists(path))
            subs_update(key, TRUE);
    } else {
        subs_set_url(key, L"");          /* güncellemeyi bırak, yer kaplamasın */
        DeleteFileW(path);
    }
}

/* Büyük listeleri her yeniden çizimde saymamak için dosya zamanına göre önbellek */
static struct { wchar_t file[64]; FILETIME t; int n; } g_cache[16];

int block_hosts(const wchar_t *file)
{
    wchar_t path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    char *buf = NULL, *p;
    HANDLE f;
    DWORD sz, rd;
    int i, slot = -1, n = 0;

    block_path(file, path);
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa))
        return -1;
    for (i = 0; i < 16; i++) {
        if (!_wcsicmp(g_cache[i].file, file)) {
            if (!CompareFileTime(&g_cache[i].t, &fa.ftLastWriteTime))
                return g_cache[i].n;
            slot = i;
            break;
        }
        if (slot < 0 && !g_cache[i].file[0])
            slot = i;
    }
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    sz = GetFileSize(f, NULL);
    if (sz != INVALID_FILE_SIZE && sz < 64 * 1024 * 1024)
        buf = malloc(sz + 1);
    if (buf && ReadFile(f, buf, sz, &rd, NULL)) {
        buf[rd] = 0;
        /* satır başına bir alan adı; yorum ve @@ istisnaları sayılmaz */
        for (p = buf; *p; ) {
            while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;
            if (*p != '#' && *p != '!' && !(p[0] == '@' && p[1] == '@'))
                n++;
            p += strcspn(p, "\n");
        }
    }
    free(buf);
    CloseHandle(f);
    if (slot < 0)
        slot = 0;
    wcsncpy(g_cache[slot].file, file, 63);
    g_cache[slot].file[63] = 0;
    g_cache[slot].t = fa.ftLastWriteTime;
    g_cache[slot].n = n;
    return n;
}

BOOL block_active(const Config *c)
{
    const wchar_t *s = c->block_lists;
    wchar_t tok[MAX_PATH];

    if (!c->block_on)
        return FALSE;
    while (next_token(&s, tok, MAX_PATH, L";"))
        if (block_hosts(tok) > 0)
            return TRUE;
    return block_hosts(BLOCK_OWN) > 0;
}

static void add2(wchar_t *out, size_t cap, const wchar_t *flag, const wchar_t *path)
{
    size_t len = wcslen(out);

    _snwprintf(out + len, cap - len, wcschr(path, L' ') ? L"%s%s \"%s\"" : L"%s%s %s", len ? L" " : L"", flag, path);
    out[cap - 1] = 0;
}

void block_args(const Config *c, const Profile *p, wchar_t *out, size_t cap)
{
    const wchar_t *s = c->block_lists;
    wchar_t tok[MAX_PATH], path[MAX_PATH];
    BOOL any = FALSE;

    if (!c->block_on || p->engine != ENGINE_CLASSIC)
        return;
    while (next_token(&s, tok, MAX_PATH, L";")) {
        if (block_hosts(tok) > 0) {
            block_path(tok, path);
            add2(out, cap, L"--block-list", path);
            any = TRUE;
        }
    }
    if (block_hosts(BLOCK_OWN) > 0) {
        block_path(BLOCK_OWN, path);
        add2(out, cap, L"--block-list", path);
        any = TRUE;
    }
    if (!any)
        return;
    if (block_hosts(BLOCK_ALLOW) > 0) {
        block_path(BLOCK_ALLOW, path);
        add2(out, cap, L"--block-allow", path);
    }
    if (!p->q && p->preset != 9) {           /* -9 zaten QUIC'i engeller */
        size_t len = wcslen(out);
        _snwprintf(out + len, cap - len, L"%s-q", len ? L" " : L"");
        out[cap - 1] = 0;
    }
}
