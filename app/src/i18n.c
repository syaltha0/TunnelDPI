/*
 * TunnelDPI - arayüz dili
 *
 * Çeviriler tray/i18n/en.txt'den üretilen i18n_table.h'dadır. Arama, ilk
 * kullanımda kurulan açık adresli bir hash tablosuyla yapılır (çizim sırasında
 * sık çağrıldığı için).
 */
#include "app.h"
#include "i18n.h"

static const struct { const wchar_t *tr, *en; } T[] = {
#include "i18n_table.h"
};
#define NT    ((int)(sizeof(T) / sizeof(T[0])))
#define SLOTS 1024

static int   g_en;
static short g_slot[SLOTS];
static BOOL  g_built;

static unsigned hash(const wchar_t *s)
{
    unsigned h = 2166136261u;

    while (*s)
        h = (h ^ (unsigned)*s++) * 16777619u;
    return h;
}

static void build(void)
{
    int i;

    for (i = 0; i < SLOTS; i++)
        g_slot[i] = -1;
    for (i = 0; i < NT; i++) {
        unsigned k = hash(T[i].tr) % SLOTS;
        while (g_slot[k] != -1)
            k = (k + 1) % SLOTS;
        g_slot[k] = (short)i;
    }
    g_built = TRUE;
}

void i18n_set(int lang)
{
    if (lang == LANG_EN)
        g_en = 1;
    else if (lang == LANG_TR)
        g_en = 0;
    else
        g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_TURKISH;
}

int i18n_is_en(void)
{
    return g_en;
}

const wchar_t *tr(const wchar_t *s)
{
    unsigned k;

    if (!g_en || !s || !*s)
        return s;
    if (!g_built)
        build();
    for (k = hash(s) % SLOTS; g_slot[k] != -1; k = (k + 1) % SLOTS)
        if (!wcscmp(T[g_slot[k]].tr, s))
            return T[g_slot[k]].en;
    return s;
}
