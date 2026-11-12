/*
 * TunnelDPI - kaldırma
 *
 * Kurulum (TunnelDPI-Setup.exe) uygulamayı Program Files altındaki kendi
 * klasörüne kurar, Başlat menüsüne kısayol ve "Yüklü uygulamalar" kaydı
 * ekler. Kaldırma bu exe'nin --uninstall argümanıyla yapılır: çalışan
 * örnek kapatılır (DNS geri yüklenir), otomatik başlatma görevi, WinDivert
 * sürücüsü, kısayol ve kayıt silinir; klasör yalnızca kayıttaki kurulum
 * klasörüyle aynıysa silinir (taşınabilir kullanımda dosyalara dokunulmaz).
 */
#include "install.h"
#include "i18n.h"
#include "system.h"
#include "legacy.h"
#include <shlobj.h>
#include <stdio.h>

void close_running_instances(void)
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
            if (pid == GetCurrentProcessId())
                break;
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
}

static void stop_delete_service(const wchar_t *name)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    SC_HANDLE svc;
    SERVICE_STATUS st;

    if (!scm)
        return;
    svc = OpenServiceW(scm, name, SERVICE_STOP | DELETE);
    if (svc) {
        ControlService(svc, SERVICE_CONTROL_STOP, &st);
        DeleteService(svc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

BOOL installed_dir(wchar_t *out, DWORD cap)
{
    DWORD sz = cap * sizeof(wchar_t), type;
    HKEY k;
    LONG r;

    out[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k))
        return FALSE;
    r = RegQueryValueExW(k, L"InstallLocation", NULL, &type, (BYTE *)out, &sz);
    RegCloseKey(k);
    if (r || type != REG_SZ)
        return FALSE;
    out[cap - 1] = 0;
    sz = (DWORD)wcslen(out);
    while (sz && out[sz - 1] == L'\\')
        out[--sz] = 0;
    return out[0] != 0;
}

void shortcut_path(wchar_t *out, size_t cap)
{
    wchar_t dir[MAX_PATH];

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, dir)))
        dir[0] = 0;
    _snwprintf(out, cap, L"%s\\%s.lnk", dir, APP_NAME);
    out[cap - 1] = 0;
}

int app_uninstall(BOOL silent)
{
    wchar_t inst[MAX_PATH], lnk[MAX_PATH], cmd[MAX_PATH * 2 + 128], sys[MAX_PATH];
    BOOL ours;
    int btn = IDYES;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!silent) {
        task_box(NULL, tr(L"TunnelDPI kaldırılsın mı?"),
                   tr(L"Koruma kapanır ve DNS ayarların eski haline döner. Ayarlar ve listeler silinir."),
                   TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_WARNING_ICON, &btn);
        if (btn != IDYES)
            return 1;
    }

    close_running_instances();
    autostart_set(FALSE);
    {
        int k;
        wchar_t cmd2[128];
        for (k = 0; k < LEGACY_COUNT; k++) {
            _snwprintf(cmd2, 128, L"schtasks.exe /Delete /F /TN \"%s\"", g_legacy_apps[k].task);
            run_hidden(cmd2, 10000);
        }
    }
    stop_delete_service(L"WinDivert");
    run_hidden(L"taskkill.exe /F /IM winws.exe", 5000);

    shortcut_path(lnk, MAX_PATH);
    DeleteFileW(lnk);

    ours = installed_dir(inst, MAX_PATH) && !_wcsicmp(inst, g_appdir);
    if (ours)
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, KEY_WOW64_64KEY, 0);

    if (!silent)
        task_box(NULL, tr(L"TunnelDPI kaldırıldı"), NULL, TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);

    /* Klasör: kendi exe'miz çalışırken silinemez; kısa bir gecikmeyle cmd siler.
       Yalnızca kayıttaki kurulum klasörü ve adı uygulama kimliğiyle bitiyorsa. */
    if (ours) {
        size_t il = wcslen(inst), al = wcslen(APP_ID);
        if (il > al && inst[il - al - 1] == L'\\' && !_wcsicmp(inst + il - al, APP_ID)) {
            GetSystemDirectoryW(sys, MAX_PATH);
            _snwprintf(cmd, MAX_PATH * 2 + 128,
                       L"\"%s\\cmd.exe\" /d /c ping 127.0.0.1 -n 3 >nul & rmdir /s /q \"%s\"", sys, inst);
            cmd[MAX_PATH * 2 + 127] = 0;
            SetCurrentDirectoryW(sys);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, sys, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    return 0;
}
