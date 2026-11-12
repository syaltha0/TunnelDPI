/*
 * TunnelDPI - internet sağlayıcısına göre hazır ayarlar
 *
 * Hazır ayar dosyası (hazir-ayarlar.ini) hangi sağlayıcıda hangi profillerin
 * çalıştığını ve o profillerin tanımlarını içerir. Kurulumla birlikte gelir,
 * günde bir depodan yenisi indirilir; yayıncının imzası tutmayan dosya
 * kullanılmaz. Sağlayıcı, bağlantının genel IP adresinin AS numarasından
 * (RIPE NCC) bulunur.
 */
#pragma once
#include "app.h"

#define WM_PRESETS_DONE (WM_APP + 26)   /* wParam: 1 hazır ayarlar güncellendi, 2 sağlayıcı bulundu */
#define PRESETS_FILE    L"hazir-ayarlar.ini"

BOOL           presets_load(void);          /* yerel dosyayı doğrular, profilleri birleştirir */
void           presets_update(void);        /* arka planda depodan yenisini indirir */
const wchar_t *presets_version(void);       /* "" = yüklenmedi */
void           isp_detect(void);            /* arka planda; sonuç WM_PRESETS_DONE (2) */
const wchar_t *isp_name(void);              /* "" = bilinmiyor */
/* Bulunan sağlayıcı için önerilen ve var olan profiller, en iyisi önce. Dönüş: sayı */
int            isp_profiles(const wchar_t **names, int max);
