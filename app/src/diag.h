/*
 * TunnelDPI - engel teşhisi
 * Test sitesini sistem DNS'i ve DoH ile ayrı ayrı çözer, sonra HTTPS ile
 * bağlanmayı dener; engelin DNS mi DPI mı olduğunu yorumlar.
 */
#pragma once
#include "app.h"

#define WM_DIAG_DONE (WM_APP + 21)

enum { DIAG_NONE = 0, DIAG_RUNNING, DIAG_DONE };
enum { DNS_SAME = 0, DNS_DIFF, DNS_BOGUS, DNS_SYSFAIL, DNS_DOHFAIL };

typedef struct {
    int     state;
    wchar_t host[128];
    wchar_t sys[160], doh[160];   /* çözülen IPv4 adresleri */
    int     dns;
    BOOL    https_ok;
    DWORD   ms, err;
    BOOL    ech;                  /* site HTTPS kaydında ECH yayınlıyor */
    BOOL    bypass_on;            /* test sırasında koruma açık mıydı */
    wchar_t verdict[160];
    wchar_t advice[400];
} DiagResult;

void              diag_start(void);
void              diag_start_host(const wchar_t *host);   /* NULL: test sitesi */
const DiagResult *diag_get(void);
