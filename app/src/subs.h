/*
 * TunnelDPI - uzaktan site listesi aboneliği
 * Abonelikler lists\subscriptions.ini'de tutulur: [Subscriptions] dosya=URL,
 * [Updated] dosya=Unix zamanı.
 */
#pragma once
#include "app.h"

#define WM_SUBS_DONE (WM_APP + 22)   /* wParam: güncellenen, lParam: başarısız */

void    subs_get_url(const wchar_t *file, wchar_t *url, size_t cap);
void    subs_set_url(const wchar_t *file, const wchar_t *url);
void    subs_rename(const wchar_t *from, const wchar_t *to);
LONG64  subs_updated(const wchar_t *file);          /* Unix zamanı, 0 = hiç */
void    subs_update(const wchar_t *file, BOOL force); /* file NULL: süresi dolanların hepsi */
BOOL    subs_running(void);
