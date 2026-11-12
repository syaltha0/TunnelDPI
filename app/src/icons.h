/*
 * TunnelDPI - durum ikonları
 */
#pragma once
#include "app.h"

enum { ICON_OFF = 0, ICON_ON, ICON_ERROR, ICON_BUSY, ICON_COUNT };

HICON icon_get(int state);
void  icons_reset(void);   /* DPI değişince */
