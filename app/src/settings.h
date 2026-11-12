/*
 * TunnelDPI - ayar penceresi (Fluent tarzı)
 */
#pragma once
#include "app.h"

enum {
    SETTINGS_PAGE_HOME = 0,
    SETTINGS_PAGE_PROFILES,
    SETTINGS_PAGE_DNS,
    SETTINGS_PAGE_LISTS,
    SETTINGS_PAGE_BLOCK,
    SETTINGS_PAGE_APPS,
    SETTINGS_PAGE_APP,
    SETTINGS_PAGE_LOG,
    SETTINGS_PAGE_COUNT
};

void settings_show(int page);          /* -1 = son açılan sayfa */
void settings_close(void);
BOOL settings_dialog_msg(MSG *m);
void settings_on_log(void);
void settings_on_state(void);
void settings_on_config(void);         /* g_cfg dışarıdan değişti (tray menüsü) */
void settings_on_nettest(void);        /* bağlantı testi / teşhis bitti */
void settings_on_lists(void);          /* abonelik listeleri güncelledi */
