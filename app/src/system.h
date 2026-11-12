/*
 * TunnelDPI - otomatik başlatma ve eski servis
 */
#pragma once
#include "app.h"

BOOL autostart_enabled(void);
BOOL autostart_set(BOOL on);

BOOL legacy_service_exists(void);
int  legacy_service_remove(void);
