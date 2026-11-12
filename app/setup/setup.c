/*
 * TunnelDPI - kurulum
 *
 * Tek exe: uygulama dosyaları RCDATA kaynağı olarak gömülüdür (pack.py ile
 * üretilen payload.bin). Program Files\TunnelDPI altına kurar, Başlat menüsüne
 * kısayol ve "Yüklü uygulamalar" kaydı ekler. Eski adla (GoodbyeDPI Tray)
 * kurulmuş bir sürüm varsa ayarlarını, listelerini ve uygulama listesini
 * taşır, eski otomatik başlatma görevini kaldırır.
 *
 *   TunnelDPI-Setup.exe                 arayüzlü kurulum
 *   TunnelDPI-Setup.exe --silent        sessiz kurulum (otomatik başlatma açık)
 *   TunnelDPI-Setup.exe --silent --no-autostart
 *   TunnelDPI-Setup.exe --silent --update   uygulamanın kendini güncellemesi
 *   TunnelDPI-Setup.exe --dir "D:\Araclar\TunnelDPI"   başka klasöre kur
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <prsht.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* Uygulamayla ortak kimlik (src/app.h ile aynı tutun) */
#define APP_NAME      L"TunnelDPI"
#define APP_ID        L"TunnelDPI"
#define APP_EXE       L"TunnelDPI.exe"
#define APP_CLASS     L"TunnelDPIWnd"
#define APP_PUBLISHER L"syaltha0"
#define APP_INI       L"tunneldpi.ini"
#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_ID

#include "../src/version.h"
#include "../src/legacy.h"
#define SETUP_VERSION APP_VERSION

#define IDR_PAYLOAD 100
#define IDI_SETUP   1
#define PK_KEEP     1     /* kullanıcı verisi: varsa üzerine yazma */
#define IDC_PROGRESS 200
#define IDC_STATUS   201

static void setup_progress(HWND page, int percent, const wchar_t *text)
{
    if (!page)
        return;
    if (percent >= 0)
        SendDlgItemMessageW(page, IDC_PROGRESS, PBM_SETPOS, percent, 0);
    if (text)
        SetDlgItemTextW(page, IDC_STATUS, text);
}

static BOOL     g_tr;                 /* Türkçe arayüz */
static wchar_t  g_dir[MAX_PATH];      /* kurulum klasörü */
static wchar_t  g_legacy[LEGACY_COUNT][MAX_PATH];  /* eski sürümlerin klasörleri */
static BOOL     g_has_legacy;
static wchar_t  g_prev[64];           /* kurulu sürüm (varsa) */
static wchar_t  g_err[600];
static BOOL     g_autostart = TRUE, g_silent, g_update;
static const BYTE *g_pk;
static DWORD    g_pklen;
static HINSTANCE g_inst;

#define T(tr_, en_) (g_tr ? (tr_) : (en_))

/* ---------------- yardımcılar ---------------- */

static DWORD run_hidden(const wchar_t *cmd, DWORD timeout)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    wchar_t *c = _wcsdup(cmd);
    DWORD code = (DWORD)-1;

    if (!c)
        return code;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, timeout) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    free(c);
    return code;
}

static BOOL exists(const wchar_t *p)
{
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static void join(wchar_t *out, const wchar_t *a, const wchar_t *b)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", a, b);
    out[MAX_PATH - 1] = 0;
}

static void close_instances(void)
{
    int k;

    for (k = -1; k < LEGACY_COUNT; k++) {
        const wchar_t *cls = k < 0 ? APP_CLASS : g_legacy_apps[k].cls;
        HWND w;
        int guard = 0;

        while ((w = FindWindowW(cls, NULL)) != NULL && guard++ < 4) {
            DWORD pid = 0;
            HANDLE h;

            GetWindowThreadProcessId(w, &pid);
            h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
            PostMessageW(w, WM_CLOSE, 0, 0);       /* koruma kapanır, DNS geri yüklenir */
            if (h) {
                if (WaitForSingleObject(h, 10000) == WAIT_TIMEOUT)
                    TerminateProcess(h, 1);
                CloseHandle(h);
            } else {
                Sleep(1500);
            }
        }
    }
    /* koruma motorları: Job nesnesiyle kapanırlar, yine de bekleme olmasın */
    run_hidden(L"taskkill.exe /F /IM winws.exe", 5000);
}

/* Kullanımdaki dosyanın (ör. yüklü sürücü) yerine yenisini koy: eskisini
   yeniden adlandır, yeniden başlatmada sil. */
static BOOL write_file(const wchar_t *path, const BYTE *data, DWORD len)
{
    wchar_t tmp[MAX_PATH + 16], old[MAX_PATH + 32];
    HANDLE f;
    DWORD wr;
    BOOL ok;

    _snwprintf(tmp, MAX_PATH + 16, L"%s.new", path);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(f, data, len, &wr, NULL) && wr == len;
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp);
        return FALSE;
    }
    if (MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING))
        return TRUE;
    _snwprintf(old, MAX_PATH + 32, L"%s.old%lu", path, GetTickCount());
    if (MoveFileExW(path, old, 0)) {
        MoveFileExW(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        if (MoveFileExW(tmp, path, 0))
            return TRUE;
    }
    DeleteFileW(tmp);
    return FALSE;
}

static void ensure_parent(const wchar_t *path)
{
    wchar_t d[MAX_PATH], *s;

    wcsncpy(d, path, MAX_PATH - 1);
    d[MAX_PATH - 1] = 0;
    s = wcsrchr(d, L'\\');
    if (s) {
        *s = 0;
        SHCreateDirectoryExW(NULL, d, NULL);
    }
}

/* ---------------- paket ----------------
 * "GCPK" u32 sayı, her kayıt: u16 bayrak, u16 yol uzunluğu (UTF-8, '/'),
 * yol, u32 boyut, veri. */

typedef BOOL (*EntryFn)(const wchar_t *rel, WORD flags, const BYTE *data, DWORD len, void *ctx);

static BOOL pk_walk(EntryFn fn, void *ctx)
{
    const BYTE *p = g_pk, *end = g_pk + g_pklen;
    DWORD n, i;

    if (g_pklen < 8 || memcmp(p, "GCPK", 4))
        return FALSE;
    memcpy(&n, p + 4, 4);
    p += 8;
    for (i = 0; i < n; i++) {
        WORD fl, pl;
        DWORD sz;
        wchar_t rel[MAX_PATH], *q;
        int wl;

        if (end - p < 4)
            return FALSE;
        memcpy(&fl, p, 2);
        memcpy(&pl, p + 2, 2);
        p += 4;
        if (end - p < pl + 4)
            return FALSE;
        wl = MultiByteToWideChar(CP_UTF8, 0, (const char *)p, pl, rel, MAX_PATH - 1);
        rel[wl > 0 ? wl : 0] = 0;
        for (q = rel; *q; q++)
            if (*q == L'/')
                *q = L'\\';
        p += pl;
        memcpy(&sz, p, 4);
        p += 4;
        if ((DWORD)(end - p) < sz || wcsstr(rel, L"..") || rel[0] == L'\\' || wcschr(rel, L':'))
            return FALSE;
        if (!fn(rel, fl, p, sz, ctx))
            return FALSE;
        p += sz;
    }
    return TRUE;
}

typedef struct { DWORD total, done; ULONGLONG bytes; HWND dlg; } Prog;

static BOOL count_fn(const wchar_t *rel, WORD fl, const BYTE *d, DWORD len, void *ctx)
{
    Prog *pr = ctx;
    (void)rel; (void)fl; (void)d;
    pr->total++;
    pr->bytes += len;
    return TRUE;
}

static BOOL extract_fn(const wchar_t *rel, WORD fl, const BYTE *d, DWORD len, void *ctx)
{
    Prog *pr = ctx;
    wchar_t path[MAX_PATH];

    join(path, g_dir, rel);
    pr->done++;
    if (pr->dlg)
    {
        setup_progress(pr->dlg, 10 + pr->done * 80 / (pr->total ? pr->total : 1), rel);
    }
    if ((fl & PK_KEEP) && exists(path))
        return TRUE;
    ensure_parent(path);
    if (!write_file(path, d, len)) {
        _snwprintf(g_err, 600, T(L"Dosya yazılamadı: %s (hata %lu). Antivirüs engelliyor olabilir.",
                                 L"Could not write %s (error %lu). An antivirus may be blocking it."),
                   path, GetLastError());
        return FALSE;
    }
    return TRUE;
}

/* ---------------- eski sürümler ---------------- */

/* Kurulumlu sürüm: kayıttaki klasör; taşınabilir sürüm: otomatik başlatma
   görevindeki exe'nin klasörü */
static void find_legacy_dir(const LegacyApp *L, wchar_t *out)
{
    wchar_t task[MAX_PATH], *q, *e;
    HANDLE f;
    DWORD sz, rd, type;
    BYTE *buf;
    HKEY k;

    out[0] = 0;
    if (L->uninstall) {
        wchar_t key[200];
        _snwprintf(key, 200, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%s", L->uninstall);
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &k)) {
            sz = MAX_PATH * sizeof(wchar_t);
            if (RegQueryValueExW(k, L"InstallLocation", NULL, &type, (BYTE *)out, &sz) || type != REG_SZ)
                out[0] = 0;
            out[MAX_PATH - 1] = 0;
            RegCloseKey(k);
            if (out[0] && exists(out))
                return;
            out[0] = 0;
        }
    }
    GetSystemDirectoryW(task, MAX_PATH);
    wcsncat(task, L"\\Tasks\\", MAX_PATH - wcslen(task) - 1);
    wcsncat(task, L->task, MAX_PATH - wcslen(task) - 1);
    f = CreateFileW(task, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    sz = GetFileSize(f, NULL);
    buf = sz > 0 && sz < 1 << 20 ? calloc(1, sz + 4) : NULL;
    if (buf && ReadFile(f, buf, sz, &rd, NULL)) {
        wchar_t *x = (wchar_t *)buf;
        if (x[0] == 0xFEFF)
            x++;
        q = wcsstr(x, L"<Command>");
        if (q) {
            q += 9;
            if (*q == L'"')
                q++;
            if (!wcsncmp(q, L"&quot;", 6))
                q += 6;
            e = wcsstr(q, L"</Command>");
            if (e && e - q < MAX_PATH) {
                wchar_t exe[MAX_PATH], *s;
                wmemcpy(exe, q, e - q);
                exe[e - q] = 0;
                if ((s = wcsstr(exe, L"&quot;")) != NULL) *s = 0;
                if ((s = wcschr(exe, L'"')) != NULL) *s = 0;
                s = wcsrchr(exe, L'\\');
                if (s) {
                    *s = 0;
                    wcscpy(out, exe);
                }
            }
        }
    }
    free(buf);
    CloseHandle(f);
}

static void find_legacy(void)
{
    int i;

    g_has_legacy = FALSE;
    for (i = 0; i < LEGACY_COUNT; i++) {
        find_legacy_dir(&g_legacy_apps[i], g_legacy[i]);
        if (g_legacy[i][0] && _wcsicmp(g_legacy[i], g_dir))
            g_has_legacy = TRUE;
        else
            g_legacy[i][0] = 0;
    }
}

static void copy_if_missing(const wchar_t *from_dir, const wchar_t *from_rel, const wchar_t *to_rel)
{
    wchar_t a[MAX_PATH], b[MAX_PATH];

    join(a, from_dir, from_rel);
    join(b, g_dir, to_rel);
    if (exists(a) && !exists(b)) {
        ensure_parent(b);
        CopyFileW(a, b, TRUE);
    }
}

static void copy_lists(const wchar_t *from_dir, const wchar_t *sub)
{
    wchar_t pat[MAX_PATH], rel[MAX_PATH], d[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    join(d, from_dir, sub);
    join(pat, d, L"*.txt");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        _snwprintf(rel, MAX_PATH, L"%s\\%s", sub, fd.cFileName);
        copy_if_missing(from_dir, rel, rel);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* En yeni eski sürümden başlayarak ayarları ve listeleri al, sonra eski
   sürümü kaldır (kurulumluysa kendi kaldırıcısıyla). */
static void migrate_legacy(void)
{
    int i;

    for (i = LEGACY_COUNT - 1; i >= 0; i--) {
        const LegacyApp *L = &g_legacy_apps[i];
        const wchar_t *d = g_legacy[i];
        wchar_t cmd[MAX_PATH + 64], exe[MAX_PATH];

        if (!d[0])
            continue;
        copy_if_missing(d, L->ini, APP_INI);
        copy_if_missing(d, L"apps.txt", L"apps.txt");
        copy_if_missing(d, L"lists\\subscriptions.ini", L"lists\\subscriptions.ini");
        copy_lists(d, L"lists");
        copy_lists(d, L"lists\\engel");
        if (L->exe) {
            join(exe, d, L->exe);
            if (exists(exe)) {
                _snwprintf(cmd, MAX_PATH + 64, L"\"%s\" --uninstall --silent", exe);
                run_hidden(cmd, 60000);
                continue;
            }
        }
        _snwprintf(cmd, MAX_PATH + 64, L"schtasks.exe /Delete /F /TN \"%s\"", L->task);
        run_hidden(cmd, 10000);
    }
}

/* ---------------- kayıt ve kısayol ---------------- */

static void reg_str(HKEY k, const wchar_t *name, const wchar_t *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
}

static void reg_dword(HKEY k, const wchar_t *name, DWORD v)
{
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
}

static void read_prev_version(void)
{
    HKEY k;
    DWORD sz = sizeof(g_prev), type;

    g_prev[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) == 0) {
        if (RegQueryValueExW(k, L"DisplayVersion", NULL, &type, (BYTE *)g_prev, &sz) || type != REG_SZ)
            g_prev[0] = 0;
        g_prev[63] = 0;
        {
            wchar_t loc[MAX_PATH];
            DWORD ls = sizeof(loc);
            if (!RegQueryValueExW(k, L"InstallLocation", NULL, &type, (BYTE *)loc, &ls) && type == REG_SZ) {
                loc[MAX_PATH - 1] = 0;
                if (loc[0] && exists(loc))
                    wcscpy(g_dir, loc);          /* güncellemede aynı klasör */
            }
        }
        RegCloseKey(k);
    }
}

static void write_registry(ULONGLONG bytes)
{
    HKEY k;
    wchar_t exe[MAX_PATH], v[MAX_PATH + 64];

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, NULL, 0, KEY_WRITE | KEY_WOW64_64KEY, NULL, &k, NULL))
        return;
    join(exe, g_dir, APP_EXE);
    reg_str(k, L"DisplayName", APP_NAME);
    reg_str(k, L"DisplayVersion", SETUP_VERSION);
    reg_str(k, L"Publisher", APP_PUBLISHER);
    reg_str(k, L"InstallLocation", g_dir);
    _snwprintf(v, MAX_PATH + 64, L"\"%s\",0", exe);
    reg_str(k, L"DisplayIcon", v);
    _snwprintf(v, MAX_PATH + 64, L"\"%s\" --uninstall", exe);
    reg_str(k, L"UninstallString", v);
    _snwprintf(v, MAX_PATH + 64, L"\"%s\" --uninstall --silent", exe);
    reg_str(k, L"QuietUninstallString", v);
    reg_dword(k, L"EstimatedSize", (DWORD)(bytes / 1024));
    reg_dword(k, L"NoModify", 1);
    reg_dword(k, L"NoRepair", 1);
    RegCloseKey(k);
}

static void make_shortcut(void)
{
    wchar_t dir[MAX_PATH], lnk[MAX_PATH], exe[MAX_PATH];
    IShellLinkW *sl;
    IPersistFile *pf;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, dir)))
        return;
    _snwprintf(lnk, MAX_PATH, L"%s\\%s.lnk", dir, APP_NAME);
    join(exe, g_dir, APP_EXE);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl)))
        return;
    IShellLinkW_SetPath(sl, exe);
    IShellLinkW_SetWorkingDirectory(sl, g_dir);
    IShellLinkW_SetDescription(sl, T(L"İnternet engellerini VPN'siz aşar", L"Gets around internet blocks without a VPN"));
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        IPersistFile_Save(pf, lnk, TRUE);
        IPersistFile_Release(pf);
    }
    IShellLinkW_Release(sl);
}

/* ---------------- kurulum ---------------- */

static BOOL install(HWND dlg)
{
    Prog pr = { 0 };

    pr.dlg = dlg;
    if (!pk_walk(count_fn, &pr)) {
        wcscpy(g_err, T(L"Kurulum dosyası bozuk. Yeniden indirip tekrar dene.",
                        L"The setup file is damaged. Download it again and retry."));
        return FALSE;
    }
    if (dlg)
        setup_progress(dlg, 3, NULL);
    close_instances();
    if (dlg)
        setup_progress(dlg, -1, T(L"Uygulama dosyaları hazırlanıyor…", L"Preparing application files…"));
    if (SHCreateDirectoryExW(NULL, g_dir, NULL) != ERROR_SUCCESS && !exists(g_dir)) {
        _snwprintf(g_err, 600, T(L"Klasör oluşturulamadı: %s", L"Could not create %s"), g_dir);
        return FALSE;
    }
    if (!pk_walk(extract_fn, &pr))
        return FALSE;
    if (dlg)
        setup_progress(dlg, -1, T(L"Ayarlar ve kısayollar hazırlanıyor…", L"Preparing settings and shortcuts…"));
    migrate_legacy();
    if (!g_update && !g_autostart)
        run_hidden(L"schtasks.exe /Delete /F /TN \"" APP_ID L"\"", 10000);
    write_registry(pr.bytes);
    make_shortcut();
    if (dlg)
        setup_progress(dlg, 100, NULL);
    return TRUE;
}

static void launch(void)
{
    wchar_t exe[MAX_PATH];

    join(exe, g_dir, APP_EXE);
    ShellExecuteW(NULL, L"open", exe, g_update ? NULL : g_autostart ? L"--setup-autostart" : L"--setup", g_dir, SW_SHOWNORMAL);
}

#include "wizard.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    HRSRC rs;
    wchar_t pf[MAX_PATH];
    int ret;

    (void)prev; (void)show;
    g_inst = inst;
    g_tr = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH;
    g_silent = wcsstr(cmdline, L"--silent") != NULL || wcsstr(cmdline, L"/S") != NULL;
    g_update = wcsstr(cmdline, L"--update") != NULL;
    if (wcsstr(cmdline, L"--no-autostart"))
        g_autostart = FALSE;
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    rs = FindResourceW(inst, MAKEINTRESOURCEW(IDR_PAYLOAD), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (rs) {
        g_pk = LockResource(LoadResource(inst, rs));
        g_pklen = SizeofResource(inst, rs);
    }
    if (!g_pk) {
        MessageBoxW(NULL, T(L"Kurulum paketi bulunamadı.", L"Setup package not found."), APP_NAME, MB_ICONERROR);
        return 1;
    }

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, pf)))
        wcscpy(pf, L"C:\\Program Files");
    join(g_dir, pf, APP_ID);
    read_prev_version();
    {
        /* --dir "C:\Yol\TunnelDPI": başka bir klasöre kur (klasör adı TunnelDPI olmalı ki kaldırma silebilsin) */
        const wchar_t *d = wcsstr(cmdline, L"--dir ");
        if (d) {
            const wchar_t *e;
            d += 6;
            while (*d == L' ')
                d++;
            if (*d == L'"')
                e = wcschr(++d, L'"');
            else
                e = wcschr(d, L' ');
            if (!e)
                e = d + wcslen(d);
            if (e - d > 3 && e - d < MAX_PATH) {
                wmemcpy(g_dir, d, e - d);
                g_dir[e - d] = 0;
            }
        }
    }
    find_legacy();
    g_icon = LoadImageW(inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, 32, 32, 0);

    if (g_silent) {
        g_ok = install(NULL);
        if (g_ok)
            launch();
        ret = g_ok ? 0 : 1;
    } else {
        ret = run_ui();
    }
    CoUninitialize();
    return ret;
}
