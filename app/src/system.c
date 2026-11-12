/*
 * TunnelDPI - otomatik başlatma (Görev Zamanlayıcı) ve eski servis
 *
 * Oturum açılışında yönetici yetkisiyle (UAC sorusu olmadan) başlamak için
 * "En yüksek ayrıcalıklarla çalıştır" işaretli bir zamanlanmış görev kullanılır.
 */
#include "system.h"
#include <stdlib.h>
#include <stdio.h>
#define SECURITY_WIN32
#include <security.h>

#define TASK_NAME APP_ID

static void xml_escape(const wchar_t *in, wchar_t *out, size_t cap)
{
    size_t len = 0;

    for (; *in && len + 7 < cap; in++) {
        switch (*in) {
        case L'&': len += _snwprintf(out + len, cap - len, L"&amp;"); break;
        case L'<': len += _snwprintf(out + len, cap - len, L"&lt;"); break;
        case L'>': len += _snwprintf(out + len, cap - len, L"&gt;"); break;
        case L'"': len += _snwprintf(out + len, cap - len, L"&quot;"); break;
        default:   out[len++] = *in;
        }
    }
    out[len] = 0;
}

BOOL autostart_enabled(void)
{
    return run_hidden(L"schtasks.exe /Query /TN \"" TASK_NAME L"\"", 10000) == 0;
}

BOOL autostart_set(BOOL on)
{
    wchar_t exe[MAX_PATH], eexe[MAX_PATH * 2], edir[MAX_PATH * 2], user[256], euser[512];
    wchar_t tmp[MAX_PATH], cmd[MAX_PATH + 128];
    wchar_t *xml;
    ULONG ulen = 256;
    HANDLE f;
    DWORD wr, code;
    BOOL ok;
    const size_t xcap = 8192;

    if (!on) {
        if (!autostart_enabled())
            return TRUE;
        return run_hidden(L"schtasks.exe /Delete /F /TN \"" TASK_NAME L"\"", 10000) == 0;
    }

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    xml_escape(exe, eexe, MAX_PATH * 2);
    xml_escape(g_appdir, edir, MAX_PATH * 2);
    if (!GetUserNameExW(NameSamCompatible, user, &ulen)) {
        ulen = 256;
        GetUserNameW(user, &ulen);
    }
    xml_escape(user, euser, 512);

    xml = malloc(xcap * sizeof(wchar_t));
    if (!xml)
        return FALSE;
    _snwprintf(xml, xcap,
        L"\xFEFF<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>%s - oturum açılışında başlatır</Description></RegistrationInfo>\r\n"
        L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>%s</UserId><Delay>PT3S</Delay></LogonTrigger></Triggers>\r\n"
        L"  <Principals><Principal id=\"Author\"><UserId>%s</UserId><LogonType>InteractiveToken</LogonType>"
        L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
        L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
        L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
        L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
        L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
        L"    <Enabled>true</Enabled>\r\n"
        L"    <Hidden>false</Hidden>\r\n"
        L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
        L"    <WakeToRun>false</WakeToRun>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
        L"    <Priority>4</Priority>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec><Command>\"%s\"</Command><Arguments>--autostart</Arguments>"
        L"<WorkingDirectory>%s</WorkingDirectory></Exec></Actions>\r\n"
        L"</Task>\r\n",
        APP_NAME, euser, euser, eexe, edir);
    xml[xcap - 1] = 0;

    GetTempPathW(MAX_PATH, tmp);
    wcsncat(tmp, L"tunneldpi-task.xml", MAX_PATH - wcslen(tmp) - 1);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        free(xml);
        return FALSE;
    }
    ok = WriteFile(f, xml, (DWORD)(wcslen(xml) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(f);
    free(xml);
    if (!ok)
        return FALSE;

    _snwprintf(cmd, MAX_PATH + 128, L"schtasks.exe /Create /F /TN \"" TASK_NAME L"\" /XML \"%s\"", tmp);
    code = run_hidden(cmd, 15000);
    DeleteFileW(tmp);
    return code == 0;
}

/* ---------------- eski GoodbyeDPI servisi ---------------- */

BOOL legacy_service_exists(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc;
    BOOL ret = FALSE;

    if (!scm)
        return FALSE;
    svc = OpenServiceW(scm, L"GoodbyeDPI", SERVICE_QUERY_STATUS);
    if (svc) {
        ret = TRUE;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ret;
}

/* service_remove.cmd ile aynı: GoodbyeDPI, WinDivert, WinDivert14 durdur + sil */
int legacy_service_remove(void)
{
    static const wchar_t *names[] = { L"GoodbyeDPI", L"WinDivert", L"WinDivert14" };
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    SERVICE_STATUS st;
    int i, n = 0;

    if (!scm)
        return -1;
    for (i = 0; i < 3; i++) {
        SC_HANDLE svc = OpenServiceW(scm, names[i], SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
        if (!svc)
            continue;
        ControlService(svc, SERVICE_CONTROL_STOP, &st);
        if (DeleteService(svc))
            n++;
        CloseServiceHandle(svc);
        app_log(L"Servis kaldırıldı: %s", names[i]);
    }
    CloseServiceHandle(scm);
    return n;
}
