/* Destek raporu yalnızca aşağıdaki izin verilen sayısal alanlardan üretilir.
 * Ham günlük, komut satırı, adres, yol veya kullanıcı tanımlı metin eklenmez. */
#include "support.h"
#include "config.h"
#include "engine.h"
#include "share.h"
#include "i18n.h"
#include <commdlg.h>
#include <stdlib.h>
#include <stdio.h>

void support_report(wchar_t *out, size_t cap)
{
    size_t size = log_get(NULL, 0);
    wchar_t *logs = malloc((size + 1) * sizeof(wchar_t)), *line;
    int errors = 0, warnings = 0;
    TuneProgress progress;
    const Profile *p = config_active(&g_cfg);
    if (!out || !cap) return;
    if (logs) {
        log_get(logs, size + 1);
        for (line = logs; line && *line;) {
            wchar_t *end = wcschr(line, L'\n');
            if (end) *end = 0;
            if (wcsstr(line, L"hata") || wcsstr(line, L"Hata") || wcsstr(line, L"FAIL") ||
                wcsstr(line, L"error") || wcsstr(line, L"başlatılamadı")) errors++;
            if (wcsstr(line, L"uyarı") || wcsstr(line, L"Uyarı") || wcsstr(line, L"warning")) warnings++;
            line = end ? end + 1 : NULL;
        }
        free(logs);
    }
    app_tune_progress(&progress);
    _snwprintf(out, cap,
        L"TunnelDPI " APP_VERSION L"\r\n"
        L"Protection state (0=off,1=on,2=error,3=busy): %d\r\n"
        L"Engine running: %d\r\nEngine (0=classic,1=zapret): %d\r\n"
        L"Profiles: %d\r\nSite rules: %d\r\n"
        L"DNS mode: %d\r\nDNS method: %d\r\nDNS failover: %d\r\n"
        L"Network auto profile: %d\r\nCaptive portal: %d\r\n"
        L"Sharing (0=off,1=waiting,2=on,3=error): %d\r\n"
        L"Profile search running: %d\r\nProfiles tested: %d/%d\r\nProfiles passed: %d\r\n"
        L"Log lines matching error keywords: %d\r\nLog lines matching warning keywords: %d\r\n\r\n"
        L"No raw logs, addresses, paths, profile names or application lists are included.\r\n",
        app_state(), engine_running(), p ? p->engine : -1, g_cfg.nprofiles, g_cfg.nrules,
        g_cfg.dns_mode, g_cfg.dns_method, g_cfg.dns_failover, g_cfg.net_auto, app_captive(),
        share_status(NULL, 0), progress.running, progress.completed, progress.total, progress.passed, errors, warnings);
    out[cap - 1] = 0;
}

static BOOL copy_report(HWND owner, const wchar_t *report)
{
    size_t bytes = (wcslen(report) + 1) * sizeof(wchar_t);
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *ptr;
    if (!data) return FALSE;
    ptr = GlobalLock(data);
    if (!ptr) { GlobalFree(data); return FALSE; }
    memcpy(ptr, report, bytes);
    GlobalUnlock(data);
    if (!OpenClipboard(owner)) { GlobalFree(data); return FALSE; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, data)) {
        CloseClipboard(); GlobalFree(data); return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

static HRESULT CALLBACK report_cb(HWND h, UINT event, WPARAM wp, LPARAM lp, LONG_PTR ref)
{
    const wchar_t *report = (const wchar_t *)ref;
    BOOL ok = TRUE;
    (void)lp;
    if (event != TDN_BUTTON_CLICKED || (wp != 100 && wp != 101)) return S_OK;
    if (wp == 100) {
        ok = copy_report(h, report);
        if (ok) SendMessageW(h, TDM_SET_ELEMENT_TEXT, TDE_FOOTER, (LPARAM)tr(L"Rapor panoya kopyalandı."));
    } else {
        wchar_t path[MAX_PATH] = L"TunnelDPI-support.txt";
        OPENFILENAMEW of = { sizeof(of) };
        of.hwndOwner = h;
        of.lpstrFilter = L"Text (*.txt)\0*.txt\0\0";
        of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"txt";
        of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&of)) {
            ok = write_text_file(path, report);
            if (ok) SendMessageW(h, TDM_SET_ELEMENT_TEXT, TDE_FOOTER, (LPARAM)tr(L"Rapor kaydedildi."));
        }
    }
    if (!ok) MessageBoxW(h, tr(L"Rapor aktarılamadı. Yeniden dene."), APP_NAME, MB_ICONWARNING);
    return S_FALSE;
}

void support_show(HWND owner)
{
    wchar_t report[2048];
    TASKDIALOG_BUTTON buttons[] = { {100, tr(L"Kopyala")}, {101, tr(L"Dosyaya kaydet")} };
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    support_report(report, 2048);
    tc.hwndParent = owner;
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = APP_NAME;
    tc.pszMainInstruction = tr(L"Destek raporu önizlemesi");
    tc.pszContent = report;
    tc.pszFooter = tr(L"Yalnız teknik durumlar ve hata sayıları var. Hiçbir yere kendiliğinden gönderilmez.");
    tc.pButtons = buttons; tc.cButtons = 2;
    tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    tc.pfCallback = report_cb; tc.lpCallbackData = (LONG_PTR)report;
    tc.cxWidth = 400;
    TaskDialogIndirect(&tc, NULL, NULL, NULL);
}
