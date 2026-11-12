/*
 * TunnelDPI - küçük HTTPS istemcisi (WinHTTP, isteğe bağlı yüklenir)
 * Engelleyici çağrılardır; yalnızca arka plan iş parçacıklarından kullanın.
 */
#pragma once
#include "app.h"

/* GET isteği. Gövde malloc'lanmış ve NUL ile sonlanmış olarak döner (free ile
 * bırakın); hata durumunda NULL. status: HTTP durum kodu (isteğe bağlı). */
char *http_get(const wchar_t *url, const wchar_t *headers, DWORD max_bytes, DWORD *status);
/* İkili içerik için: gövde uzunluğu out_len'e yazılır */
char *http_get_n(const wchar_t *url, const wchar_t *headers, DWORD max_bytes, DWORD *status, DWORD *out_len);
/* Yönlendirme izlemeden ve vekilsiz GET: giriş sayfası (captive portal) denetimi için */
char *http_get_raw(const wchar_t *url, DWORD max_bytes, DWORD *status);

/* HTTPS HEAD ile ulaşılabilirlik: başarıda TRUE, ms süre; hata kodu err'de. */
BOOL  http_probe(const wchar_t *host, DWORD *ms, DWORD *err);

/* Basit JSON yardımcısı: "key":"değer" ilk eşleşmesini out'a yazar. */
BOOL  json_str(const char *json, const char *key, char *out, size_t cap);
