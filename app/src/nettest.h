/*
 * TunnelDPI - bağlantı testi
 * Ayarlardaki test sitesine (yoksa aktif profilin ilk listesindeki siteye,
 * o da yoksa www.google.com'a) arka planda HTTPS HEAD isteği atar.
 */
#pragma once
#include "app.h"

enum { NT_NONE = 0, NT_RUNNING, NT_OK, NT_FAIL };

typedef struct {
    int     state;
    DWORD   ms;          /* yanıt süresi */
    DWORD   when;        /* GetTickCount() zamanı */
    DWORD   err;         /* başarısızsa WinHTTP hata kodu */
    wchar_t host[128];
} NetTest;

#define WM_NETTEST (WM_APP + 20)

void           nettest_start(void);          /* zaten çalışıyorsa bir şey yapmaz */
void           nettest_start_host(const wchar_t *host);   /* sabit hedefle */
BOOL           nettest_target(wchar_t *out, size_t cap);  /* FALSE: anlamlı hedef yok */
const NetTest *nettest_get(void);
void           nettest_age(wchar_t *out, size_t cap);   /* "12 sn önce" */
const wchar_t *nettest_error_text(void);
