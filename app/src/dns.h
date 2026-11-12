/*
 * TunnelDPI - DNS
 */
#pragma once
#include "app.h"

typedef struct {
    const wchar_t *name;
    const wchar_t *v4;       /* virgülle ayrılmış: birincil,ikincil */
    const wchar_t *v6;
    const wchar_t *engine_port; /* GoodbyeDPI yönlendirmesi için port */
    const wchar_t *doh;      /* DoH şablonu */
} DnsPreset;

extern const DnsPreset g_dns_presets[];
extern const int       g_ndns_presets;   /* son eleman "Özel" */

#define DNS_PRESET_CUSTOM (g_ndns_presets - 1)

#include "config.h"

/* Etkin preset bilgisi (özel ise kullanıcı alanlarından). */
void dns_current(const Config *c, const wchar_t **v4, const wchar_t **v6,
                 const wchar_t **port, const wchar_t **doh);
void dns_engine_args(const Config *c, wchar_t *out, size_t cap);

/* Sistem DNS: aktif adaptörlere uygula (yalnızca henüz uygulanmamış olanlara). */
BOOL dns_system_apply(const Config *c, wchar_t *err, size_t errcap);
/* Kayıtlı eski ayarları geri yükle. Kayıt yoksa bir şey yapmaz. */
void dns_system_restore(void);
BOOL dns_restore_pending(void);
/* Ağ arayüzü değişince WM_NET_CHANGED gönder. */
void dns_watch(BOOL on);
