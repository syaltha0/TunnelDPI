/*
 * TunnelDPI - eski adlarla kurulmuş sürümler
 * Uygulama ve kurulum, bunların ayarlarını, listelerini ve otomatik
 * başlatmasını devralır. Kurulum (setup.c) da bu dosyayı kullanır.
 */
#pragma once

typedef struct {
    const wchar_t *cls;        /* gizli ana pencere sınıfı */
    const wchar_t *task;       /* otomatik başlatma görevi */
    const wchar_t *ini;        /* ayar dosyası */
    const wchar_t *uninstall;  /* "Yüklü uygulamalar" anahtarı (kurulumlu sürümler), yoksa NULL */
    const wchar_t *exe;        /* kendi kaldırıcısı olan exe */
} LegacyApp;

static const LegacyApp g_legacy_apps[] = {
    { L"GoodbyeDPITrayWnd", L"GoodbyeDPI Tray", L"gdpi-tray.ini", NULL,    NULL },
    { L"GecitWnd",          L"Gecit",           L"gecit.ini",     L"Gecit", L"Gecit.exe" },
};
#define LEGACY_COUNT ((int)(sizeof(g_legacy_apps) / sizeof(g_legacy_apps[0])))
