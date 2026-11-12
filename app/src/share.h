/*
 * TunnelDPI - erişim noktasındaki cihazlarla paylaşım
 *
 * Windows Mobil erişim noktası açıkken, bağlanan cihazlar (telefon vb.) için
 * yalnızca o ağda (192.168.137.0/24) dinleyen küçük bir HTTP/HTTPS vekil
 * sunucu açılır. Telefon Wi-Fi ayarında bu vekili kullanınca bağlantıları bu
 * bilgisayardan çıkar ve motor onlara da uygulanır. Paylaşım ağı yokken
 * hiçbir şey dinlenmez; ws2_32 yalnızca vekil açıkken yüklenir.
 */
#pragma once
#include "app.h"

#define SHARE_PORT 8881

enum { SHARE_OFF = 0, SHARE_WAITING, SHARE_ON, SHARE_ERROR };

void share_apply(void);                 /* ayara ve erişim noktasına göre aç/kapat; ana iş parçacığından */
void share_stop(void);
int  share_status(wchar_t *ip, size_t cap);   /* SHARE_*; açıksa dinlenen adres */
