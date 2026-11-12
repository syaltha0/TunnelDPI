/*
 * TunnelDPI - giriş sayfası (captive portal) denetimi
 *
 * Otel, kafe, havalimanı Wi-Fi'ları internete çıkmadan önce bir giriş sayfası
 * ister. Koruma açıkken DNS yönlendirmesi bu sayfayı açılmaz hale getirebilir.
 * Windows'un kendi bağlantı denetimindeki adrese düz HTTP ile, yönlendirme
 * izlemeden bakılır: beklenen metin gelirse internet var, başka bir yanıt
 * gelirse ağ giriş istiyor.
 */
#pragma once
#include "app.h"

#define WM_CAPTIVE_DONE (WM_APP + 27)   /* wParam: CAPTIVE_* */

enum { CAPTIVE_OFFLINE = 0, CAPTIVE_ONLINE, CAPTIVE_PORTAL };

void captive_check(void);   /* arka planda; zaten çalışıyorsa bir şey yapmaz */
