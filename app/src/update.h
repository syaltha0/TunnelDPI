/*
 * TunnelDPI - güncelleme denetimi ve kendini güncelleme (GitHub sürümleri)
 */
#pragma once
#include "app.h"

#define WM_UPDATE_DONE (WM_APP + 23)   /* wParam: 0 denetim bitti, 1 indirme başarısız, 2 doğrulanamadı */

enum { UPD_NONE = 0, UPD_RUNNING, UPD_DONE, UPD_FAIL, UPD_DOWNLOADING };

typedef struct {
    int     state;
    BOOL    newer;          /* yüklü sürümden yeni */
    wchar_t tag[64];        /* en son sürüm etiketi */
    wchar_t url[256];       /* sürüm sayfası */
    wchar_t setup[512];     /* kurulum dosyasının indirme adresi (varsa) */
    char    setup_name[128];
    wchar_t sums[512], sig[512];   /* imzalı özet listesi ve imzası */
    BOOL    signed_release; /* kurulum, özet listesi ve imza var: güncellenebilir */
    DWORD   when;           /* GetTickCount */
} UpdateInfo;

void              update_check(void);      /* arka planda; depo ayarlı değilse bir şey yapmaz */
void              update_install(void);    /* kurulumu indirip sessiz kipte çalıştırır */
const UpdateInfo *update_get(void);
int               version_cmp(const wchar_t *a, const wchar_t *b);   /* "v1.2.0" biçimleri */
