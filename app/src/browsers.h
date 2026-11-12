/*
 * TunnelDPI - tarayıcıların kendi "Güvenli DNS" ayarı
 *
 * Chrome, Edge, Brave, Opera ve Firefox Güvenli DNS açıkken site adlarını
 * kendileri çözer; TunnelDPI'ın DNS ayarı o tarayıcıda geçerli olmaz.
 */
#pragma once
#include "app.h"

enum { BDNS_OFF = 0, BDNS_AUTO, BDNS_ON };

typedef struct {
    const wchar_t *name;
    int     state;          /* BDNS_* */
    wchar_t server[64];     /* açıksa kullanılan sağlayıcı (bilinebiliyorsa) */
} BrowserDns;

/* Kurulu tarayıcıları tarar (sonuç 30 sn önbellekte). Dönüş: bulunan sayı. */
int browsers_dns(const BrowserDns **out);
