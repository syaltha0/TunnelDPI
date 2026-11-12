/*
 * TunnelDPI - bağlı ağın kimliği
 * Anahtar: varsayılan ağ geçidinin MAC adresi (yönlendiriciye özgü).
 * Görünen ad: Wi-Fi'de SSID, kabloluda adaptör adı.
 */
#pragma once
#include "app.h"

BOOL netid_current(wchar_t *key, size_t kcap, wchar_t *name, size_t ncap);
