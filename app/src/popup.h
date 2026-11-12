/*
 * TunnelDPI - uygulamanın görünümüyle çizilen açılır liste
 * (standart Win32 menüsünün yerine)
 */
#pragma once
#include "app.h"

enum {
    PI_CHECK    = 1,   /* seçili öğe */
    PI_SEP      = 2,   /* ayırıcı */
    PI_DISABLED = 4,
    PI_DANGER   = 8,   /* yıkıcı eylem (kırmızı yazı) */
};

typedef struct {
    int     id;            /* 0 olmamalı; seçim bu değeri döndürür */
    int     flags;
    wchar_t text[96];
} PopItem;

/* Modal: seçilen öğenin id'sini, vazgeçilirse 0 döndürür.
 * anchor ekran koordinatlarında; liste altına (sığmazsa üstüne) açılır.
 * align_right: liste anchor'ın sağ kenarına hizalanır. */
int ui_popup(HWND owner, const RECT *anchor, const PopItem *items, int n, BOOL align_right);

/* Modal açıklama baloncuğu: anchor'ın altında (sığmazsa üstünde) açılır,
 * herhangi bir tıklama ya da Esc kapatır. */
void ui_bubble(HWND owner, const RECT *anchor, const wchar_t *title, const wchar_t *text);
