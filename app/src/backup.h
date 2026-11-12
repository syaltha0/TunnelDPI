/*
 * TunnelDPI - ayarları yedekleme ve geri yükleme
 *
 * Ayarlar, profiller, site listeleri, kurallar, uygulama listesi ve kendi
 * engel/izin listelerin tek bir metin dosyasına yazılır. İndirilen reklam
 * listeleri dahil edilmez (yeniden indirilir).
 */
#pragma once
#include "app.h"

int backup_save(const wchar_t *dest);       /* yazılan dosya sayısı, hata: -1 */
int backup_restore(const wchar_t *src);     /* geri yüklenen dosya sayısı, geçersiz dosya: -1 */
