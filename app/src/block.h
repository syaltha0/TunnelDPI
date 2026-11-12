/*
 * TunnelDPI - reklam ve izleyici engelleme
 * Listeler lists\engel\ altında tutulur; hazır listeler abonelik olarak
 * (subs.c, anahtar "engel\dosya.txt") günde bir güncellenir.
 */
#pragma once
#include "config.h"

#define BLOCK_DIR    L"lists\\engel"
#define BLOCK_OWN    L"kendi.txt"     /* kullanıcının kendi engel listesi */
#define BLOCK_ALLOW  L"izinli.txt"    /* hiç engellenmeyecekler */

typedef struct {
    const wchar_t *file, *name, *desc, *url;
} BlockSource;

extern const BlockSource g_block_src[];
extern const int         g_nblock_src;

void block_path(const wchar_t *file, wchar_t *out);            /* MAX_PATH */
void block_subkey(const wchar_t *file, wchar_t *out);          /* abonelik anahtarı, MAX_PATH */
BOOL block_has(const Config *c, const wchar_t *file);
void block_set(Config *c, int src, BOOL on);                   /* hazır listeyi aç/kapat (indirir) */
int  block_hosts(const wchar_t *file);                         /* dosyadaki alan adı sayısı (önbellekli) */
BOOL block_active(const Config *c);                            /* açık ve en az bir liste hazır */
void block_args(const Config *c, const Profile *p, wchar_t *out, size_t cap);
