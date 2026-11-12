/*
 * TunnelDPI - ortak tanımlar
 */
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wchar.h>
#include "version.h"

/* Uygulama kimliği. Kurulum klasörü ve dosya adları ASCII kalmalı:
   klasik motor argümanları ANSI kod sayfasına çevrilerek okur. */
#define APP_NAME     L"TunnelDPI"
#define APP_ID       L"TunnelDPI"                 /* klasör, kayıt defteri, dosya adları */
#define APP_PUBLISHER L"syaltha0"
#define APP_UA       L"TunnelDPI/" APP_VERSION
#define APP_CLASS    L"TunnelDPIWnd"
#define APP_MUTEX    L"Local\\TunnelDPI.SingleInstance"
#define APP_INI      L"tunneldpi.ini"
#define APP_REPO_DEFAULT L"syaltha0/TunnelDPI"  /* GitHub sahip/ad; sürümler buradan denetlenir */


/* Uygulama mesajları */
#define WM_TRAY           (WM_APP + 1)
#define WM_ENGINE_EXIT      (WM_APP + 2)  /* wParam = exit code, lParam = generation */
#define WM_ENGINE_LOG       (WM_APP + 3)
#define WM_SHOW_SETTINGS  (WM_APP + 4)  /* wParam = page index */
#define WM_NET_CHANGED    (WM_APP + 5)
#define WM_APP_QUIT       (WM_APP + 6)  /* başka örnekten: TunnelDPI.exe --quit */
#define WM_SHOW_GUIDE     (WM_APP + 7)
#define WM_SHOW_RELEASE   (WM_APP + 8)

extern HINSTANCE g_inst;
extern HWND      g_hwnd;          /* gizli ana pencere */
extern wchar_t   g_appdir[MAX_PATH];

/* util.c */
void   app_path(wchar_t *out, size_t cap, const wchar_t *rel);
BOOL   file_exists(const wchar_t *path);
BOOL   is_elevated(void);
DWORD  run_hidden(const wchar_t *cmdline, DWORD timeout_ms);
void   trim_ws(wchar_t *s);
wchar_t *read_text_file(const wchar_t *path);              /* UTF-8 -> wide, free() ile serbest bırak */
BOOL   write_text_file(const wchar_t *path, const wchar_t *text); /* wide -> UTF-8 */
void   trim_memory(void);
int    next_token(const wchar_t **p, wchar_t *out, size_t cap, const wchar_t *seps);
HRESULT task_box(HWND owner, PCWSTR main, PCWSTR content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon, int *btn);

/* main.c */
void   app_log(const wchar_t *fmt, ...);
void   app_notify(const wchar_t *title, const wchar_t *text, BOOL error);
BOOL   bypass_running(void);
void   bypass_restart_if_running(void);
void   app_refresh_state(void);
const wchar_t *app_state_text(void);
int    app_state(void);                 /* ICON_OFF / ICON_ON / ICON_ERROR / ICON_BUSY */
const wchar_t *app_last_error(void);
void   app_toggle(void);
void   app_quit(void);                  /* korumayı kapatıp uygulamadan çık */
BOOL   app_autostart(void);
void   app_set_autostart(BOOL on);
BOOL   app_tune_state(int *idx, int *total);   /* en uygun profili bul */
int    app_tune_start(BOOL deep);     /* deep: yöntem birleşimlerini tara */
const wchar_t *app_tune_name(void);
typedef struct {
    BOOL running, testing;
    int completed, total, passed;
    DWORD elapsed;
    wchar_t summary[512];
} TuneProgress;
void app_tune_progress(TuneProgress *out);
void app_retry(void);
void   app_pause(int minutes);         /* korumayı süreli kapat */
int    app_paused(void);               /* kalan dakika, 0 = yok */
BOOL   app_captive(void);              /* ağ giriş sayfası istiyor, koruma beklemede */
void   app_tune_cancel(void);
void   app_apply_hotkey(void);
void   app_net_remember(const wchar_t *profile);  /* bağlı ağı bu profille eşleştir */
void   app_use_profile(const wchar_t *name);      /* hemen geç, ağ için hatırla */
int    app_site_fix(const wchar_t *input);        /* "açılmasını sağla": 0 başladı, 1 geçersiz, 2 meşgul */
BOOL   app_site_fix_busy(void);
BOOL   host_normalize(const wchar_t *in, wchar_t *out, size_t cap);
BOOL   list_append(const wchar_t *rel, const wchar_t *host);
