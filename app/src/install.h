/*
 * TunnelDPI - kurulum kaydı ve kaldırma
 */
#pragma once
#include "app.h"

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_ID

void close_running_instances(void);           /* bu uygulamanın (ve eski adının) diğer örneklerini kapat */
BOOL installed_dir(wchar_t *out, DWORD cap);  /* kayıttaki kurulum klasörü */
void shortcut_path(wchar_t *out, size_t cap); /* Başlat menüsü kısayolu */
int  app_uninstall(BOOL silent);
