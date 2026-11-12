/*
 * TunnelDPI - site denetimi ve DNS ölçümü
 *
 * Koruma açıkken birkaç dakikada bir izlenen sitelere bağlanılır; sonuç
 * panelde ve Durum sayfasında gösterilir, hepsi açılmaz olunca uygulama
 * çalışan profili kendisi arar. Aynı tur seçili DNS sunucusunu da yoklar.
 */
#pragma once
#include "app.h"

#define WM_HEALTH_DONE   (WM_APP + 24)
#define WM_DNSBENCH_DONE (WM_APP + 25)

#define HEALTH_MAX 5

enum { SITE_UNKNOWN = -1, SITE_FAIL = 0, SITE_OK = 1 };

typedef struct {
    wchar_t host[96];
    int     ok;          /* SITE_* */
    DWORD   ms;
} SiteState;

typedef struct {
    BOOL      running;
    int       n;
    SiteState s[HEALTH_MAX];
    DWORD     when;        /* GetTickCount(), 0 = hiç */
    BOOL      control_ok;  /* engelsiz bir siteye ulaşılabildi mi (internet var mı) */
    int       dns;         /* SITE_*; DNS varsayılansa SITE_UNKNOWN */
} Health;

void          health_start(void);              /* zaten çalışıyorsa bir şey yapmaz */
const Health *health_get(void);
int           health_ok_count(void);
void          health_summary(wchar_t *out, size_t cap);   /* "discord.com, x.com açılıyor" */

/* DNS hız ölçümü: her hazır sunucuya UDP sorgusu (3 deneme, en iyisi) */
typedef struct {
    BOOL running;
    int  n;
    int  ms[16];        /* -1 = yanıt yok */
    int  best;          /* en hızlı hazır sunucu, -1 = hiçbiri */
} DnsBench;

void            dnsbench_start(void);
const DnsBench *dnsbench_get(void);

/* Tek sunucu: TRUE = yanıt verdi. ip "1.2.3.4", port 0 = 53. Engelleyicidir. */
BOOL dns_probe(const wchar_t *ip, int port, DWORD timeout_ms, DWORD *ms);
