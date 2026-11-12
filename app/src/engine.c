/*
 * TunnelDPI - koruma motorunun süreç yönetimi
 *
 * Motor (tunneldpi-engine.exe ya da winws.exe) gizli olarak başlatılır ve bir Job Object'e atanır
 * (KILL_ON_JOB_CLOSE): uygulama kapanır ya da çökerse motor da
 * kapanır. stdout/stderr bir pipe üzerinden küçük bir okuyucu iş parçacığı
 * ile log tamponuna aktarılır; süreç bitince ana pencereye WM_ENGINE_EXIT gider.
 *
 * Site kuralları: ana motor kural listelerindeki sitelere dokunmaz
 * (--skip-list); her kural için o profille ve yalnız o listeyle ikinci bir
 * motor, ana motordan sonra sıralanacak şekilde (--priority) çalışır.
 */
#include "engine.h"
#include "i18n.h"
#include <tlhelp32.h>
#include <stdlib.h>
#include <stdio.h>

#define LOG_CAP (32 * 1024)   /* karakter (64 KB) */

static HANDLE g_job, g_proc, g_thread;
static DWORD  g_pid, g_start_tick;
static volatile LONG g_gen;

/* kural motorları; çıkışları ana motorun yeniden başlatma mantığına girmez */
static HANDLE g_rproc[MAX_RULES], g_rthread[MAX_RULES];
static DWORD  g_rpid[MAX_RULES];
static int    g_nr;

static CRITICAL_SECTION g_logcs;
static wchar_t *g_log;
static size_t   g_loglen;
static HANDLE   g_logf = INVALID_HANDLE_VALUE;   /* kalıcı günlük dosyası */
static volatile LONG g_blocked;                  /* motorun "Blocked: alan" satırları */
static wchar_t  g_last_blocked[128];

#define LOGFILE_MAX (1024 * 1024)

static void logf_open(void)
{
    wchar_t path[MAX_PATH];

    app_path(path, MAX_PATH, LOG_FILE);
    g_logf = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Günlüğü dosyaya da yaz (gdpi-tray.log, 1 MB'ta .old'a döner) */
void log_file_enable(BOOL on)
{
    EnterCriticalSection(&g_logcs);
    if (on && g_logf == INVALID_HANDLE_VALUE)
        logf_open();
    if (!on && g_logf != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logf);
        g_logf = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_logcs);
}

static void logf_write(const wchar_t *s, size_t n)
{
    char *u;
    wchar_t *w;
    size_t i, k = 0;
    int len;
    DWORD wr;
    LARGE_INTEGER sz;

    if (g_logf == INVALID_HANDLE_VALUE || !n)
        return;
    if (GetFileSizeEx(g_logf, &sz) && sz.QuadPart > LOGFILE_MAX) {
        wchar_t path[MAX_PATH], old[MAX_PATH];

        CloseHandle(g_logf);
        app_path(path, MAX_PATH, LOG_FILE);
        _snwprintf(old, MAX_PATH, L"%s.old", path);
        old[MAX_PATH - 1] = 0;
        MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
        logf_open();
        if (g_logf == INVALID_HANDLE_VALUE)
            return;
    }
    w = malloc((n * 2 + 1) * sizeof(wchar_t));
    if (!w)
        return;
    for (i = 0; i < n; i++) {
        if (s[i] == L'\r')
            continue;
        if (s[i] == L'\n')
            w[k++] = L'\r';
        w[k++] = s[i];
    }
    len = WideCharToMultiByte(CP_UTF8, 0, w, (int)k, NULL, 0, NULL, NULL);
    u = len > 0 ? malloc(len) : NULL;
    if (u) {
        WideCharToMultiByte(CP_UTF8, 0, w, (int)k, u, len, NULL, NULL);
        WriteFile(g_logf, u, (DWORD)len, &wr, NULL);
        free(u);
    }
    free(w);
}

typedef struct { HANDLE pipe, proc; LONG gen; } ReaderArg;

BOOL engine_init(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};

    InitializeCriticalSection(&g_logcs);
    g_job = CreateJobObjectW(NULL, NULL);
    if (!g_job)
        return FALSE;
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
}

/* ---------------- log ---------------- */

void log_append(const wchar_t *s, size_t n)
{
    size_t i;

    EnterCriticalSection(&g_logcs);
    if (!g_log)
        g_log = malloc(LOG_CAP * sizeof(wchar_t));
    if (g_log) {
        for (i = 0; i < n; i++) {
            if (g_loglen + 2 >= LOG_CAP) {
                /* İlk yarıyı at, satır başından devam et */
                size_t cut = LOG_CAP / 2;
                while (cut < g_loglen && g_log[cut - 1] != L'\n')
                    cut++;
                memmove(g_log, g_log + cut, (g_loglen - cut) * sizeof(wchar_t));
                g_loglen -= cut;
            }
            if (s[i] == L'\r')
                continue;
            if (s[i] == L'\n')
                g_log[g_loglen++] = L'\r';
            g_log[g_loglen++] = s[i];
        }
    }
    logf_write(s, n);
    {
        /* engellenen alan adları: motor her liste girdisini bir kez yazar */
        const wchar_t *q = s, *end = s + n;
        while (q < end && (q = wcsstr(q, L"Blocked: ")) != NULL && q < end) {
            size_t k = 0;
            q += 9;
            while (q + k < end && q[k] != L'\r' && q[k] != L'\n' && k < 127)
                k++;
            wmemcpy(g_last_blocked, q, k);
            g_last_blocked[k] = 0;
            InterlockedIncrement(&g_blocked);
            q += k;
        }
    }
    LeaveCriticalSection(&g_logcs);
}

size_t log_get(wchar_t *out, size_t cap)
{
    size_t n;

    EnterCriticalSection(&g_logcs);
    n = g_loglen;
    if (out && cap) {
        if (n >= cap)
            n = cap - 1;
        if (g_log)
            wmemcpy(out, g_log + (g_loglen - n), n);
        out[n] = 0;
    }
    LeaveCriticalSection(&g_logcs);
    return n;
}

void log_clear(void)
{
    EnterCriticalSection(&g_logcs);
    free(g_log);
    g_log = NULL;
    g_loglen = 0;
    LeaveCriticalSection(&g_logcs);
}

/* Son N satır (bildirimler için) */
void log_tail(wchar_t *out, size_t cap, int lines)
{
    size_t i, n;

    out[0] = 0;
    EnterCriticalSection(&g_logcs);
    if (g_log && g_loglen) {
        i = g_loglen;
        while (i > 0 && (g_log[i - 1] == L'\n' || g_log[i - 1] == L'\r'))
            i--;
        n = i;
        while (i > 0 && lines > 0) {
            i--;
            if (g_log[i] == L'\n')
                lines--;
        }
        if (g_log[i] == L'\n')
            i++;
        if (n - i >= cap)
            i = n - (cap - 1);
        wmemcpy(out, g_log + i, n - i);
        out[n - i] = 0;
    }
    LeaveCriticalSection(&g_logcs);
}

/* ---------------- süreç ---------------- */

static DWORD WINAPI reader_thread(LPVOID p)
{
    ReaderArg *ra = p;
    char buf[1024];
    wchar_t wbuf[1024];
    DWORD rd, code = 0;
    int n;

    while (ReadFile(ra->pipe, buf, sizeof(buf), &rd, NULL) && rd) {
        n = MultiByteToWideChar(CP_ACP, 0, buf, rd, wbuf, 1024);
        if (n > 0) {
            log_append(wbuf, n);
            PostMessageW(g_hwnd, WM_ENGINE_LOG, 0, 0);
        }
    }
    WaitForSingleObject(ra->proc, INFINITE);
    GetExitCodeProcess(ra->proc, &code);
    PostMessageW(g_hwnd, WM_ENGINE_EXIT, code, ra->gen);
    CloseHandle(ra->pipe);
    CloseHandle(ra->proc);
    free(ra);
    return 0;
}

static const Profile *g_override;

/* Profil bulucu: ayarlara yazmadan geçici bir profille çalıştır */
void engine_set_override(const Profile *p)
{
    g_override = p;
}

BOOL engine_profile_overridden(void)
{
    return g_override != NULL;
}

const Profile *engine_profile(const Config *c)
{
    return g_override ? g_override : config_active((Config *)c);
}

LONG engine_blocked(wchar_t *last, size_t cap)
{
    if (last) {
        EnterCriticalSection(&g_logcs);
        wcsncpy(last, g_last_blocked, cap - 1);
        last[cap - 1] = 0;
        LeaveCriticalSection(&g_logcs);
    }
    return g_blocked;
}

void engine_cmdline(const Config *c, wchar_t *out, size_t cap)
{
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    size_t len;
    int idx[MAX_RULES], n, i;

    app_path(exe, MAX_PATH, engine_exe(c, engine_profile(c)));
    len = _snwprintf(out, cap, L"\"%s\" ", exe);
    if (len < cap)
        build_args(c, engine_profile(c), out + len, cap - len);
    n = g_override ? 0 : rules_usable(c, engine_profile(c), idx, MAX_RULES);
    for (i = 0; i < n; i++) {
        len = wcslen(out);
        app_path(path, MAX_PATH, c->rules[idx[i]].list);
        _snwprintf(out + len, cap - len, L" --skip-list \"%s\"", path);
    }
    out[cap - 1] = 0;
}

/* Gizli, iş nesnesine bağlı, çıktısı günlüğe akan bir süreç başlatır */
static BOOL spawn(const wchar_t *exe, wchar_t *cmd, LONG gen, HANDLE *proc, DWORD *pid, HANDLE *thread)
{
    wchar_t dir[MAX_PATH], *slash;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL, wr = NULL;
    ReaderArg *ra;

    wcscpy(dir, exe);
    slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return FALSE;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = wr;

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        NULL, dir, &si, &pi)) {
        DWORD e = GetLastError();
        CloseHandle(rd);
        CloseHandle(wr);
        SetLastError(e);
        return FALSE;
    }
    CloseHandle(wr);
    if (g_job)
        AssignProcessToJobObject(g_job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    *proc = pi.hProcess;
    *pid = pi.dwProcessId;
    *thread = NULL;

    ra = malloc(sizeof(*ra));
    if (ra) {
        ra->pipe = rd;
        ra->gen = gen;
        DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &ra->proc,
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
        *thread = CreateThread(NULL, 64 * 1024, reader_thread, ra, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (!*thread) {
            CloseHandle(ra->proc);
            CloseHandle(rd);
            free(ra);
        }
    } else {
        CloseHandle(rd);
    }
    return TRUE;
}

/* Kural motorları: ana motordan sonra, her biri bir öncekinin altında */
static void start_rules(const Config *c)
{
    wchar_t exe[MAX_PATH], cmd[4096];
    int idx[MAX_RULES], n, i, pi;
    size_t len;

    n = g_override ? 0 : rules_usable(c, engine_profile(c), idx, MAX_RULES);
    app_path(exe, MAX_PATH, CLASSIC_EXE);
    for (i = 0; i < n; i++) {
        pi = config_find(c, c->rules[idx[i]].profile);
        len = _snwprintf(cmd, 4096, L"\"%s\" ", exe);
        build_rule_args(c, &c->profiles[pi], c->rules[idx[i]].list, -10 * (i + 1), cmd + len, 4096 - len);
        cmd[4095] = 0;
        app_log(L"Site kuralı: %s → \"%s\"", c->rules[idx[i]].list, c->profiles[pi].name);
        if (spawn(exe, cmd, 0, &g_rproc[g_nr], &g_rpid[g_nr], &g_rthread[g_nr]))
            g_nr++;
        else
            app_log(L"Site kuralı motoru başlatılamadı (hata %lu).", GetLastError());
    }
}

static void stop_rules(void)
{
    int i;

    for (i = 0; i < g_nr; i++) {
        TerminateProcess(g_rproc[i], 0);
        WaitForSingleObject(g_rproc[i], 5000);
        CloseHandle(g_rproc[i]);
        if (g_rthread[i]) {
            WaitForSingleObject(g_rthread[i], 3000);
            CloseHandle(g_rthread[i]);
        }
    }
    g_nr = 0;
}

int engine_rule_count(void)
{
    return g_nr;
}

BOOL engine_start(const Config *c, wchar_t *err, size_t errcap)
{
    wchar_t exe[MAX_PATH], *cmd;
    const size_t cmdcap = 8192;
    const Profile *p;
    BOOL ok;

    err[0] = 0;
    if (g_proc || g_nr)
        engine_stop();
    InterlockedExchange(&g_blocked, 0);

    p = engine_profile(c);
    app_path(exe, MAX_PATH, engine_exe(c, p));
    if (!file_exists(exe)) {
        _snwprintf(err, errcap, tr(L"%s bulunamadı:\n%s\nTunnelDPI'ı yeniden kurmak bunu düzeltir."), engine_name(p), exe);
        return FALSE;
    }

    if (c->kill_others)
        engine_kill_others();

    cmd = malloc(cmdcap * sizeof(wchar_t));
    if (!cmd)
        return FALSE;
    engine_cmdline(c, cmd, cmdcap);
    app_log(L"Başlatılıyor: %s", cmd);
    ok = spawn(exe, cmd, InterlockedIncrement(&g_gen), &g_proc, &g_pid, &g_thread);
    free(cmd);
    if (!ok) {
        _snwprintf(err, errcap, tr(L"%s başlatılamadı (hata %lu). Antivirüs engelliyor olabilir."), engine_name(p), GetLastError());
        g_proc = NULL;
        return FALSE;
    }
    g_start_tick = GetTickCount();
    start_rules(c);
    return TRUE;
}

void engine_stop(void)
{
    stop_rules();
    if (!g_proc)
        return;
    InterlockedIncrement(&g_gen); /* bekleyen çıkış mesajı geçersiz olsun */
    TerminateProcess(g_proc, 0);
    WaitForSingleObject(g_proc, 5000);
    CloseHandle(g_proc);
    g_proc = NULL;
    g_pid = 0;
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    app_log(L"Motor durduruldu.");
}

BOOL engine_running(void)
{
    return g_proc && WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT;
}

LONG engine_generation(void)
{
    return g_gen;
}

DWORD engine_uptime_ms(void)
{
    return g_proc ? GetTickCount() - g_start_tick : 0;
}

static BOOL own_rule_pid(DWORD pid)
{
    int i;

    for (i = 0; i < g_nr; i++)
        if (g_rpid[i] == pid)
            return TRUE;
    return FALSE;
}

/* Bizim başlatmadığımız motor süreçlerini kapat (eski .cmd kurulumları, başka kopyalar) */
void engine_kill_others(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };

    if (snap == INVALID_HANDLE_VALUE)
        return;
    if (Process32FirstW(snap, &pe)) {
        do {
            if ((!_wcsicmp(pe.szExeFile, L"goodbyedpi.exe") || !_wcsicmp(pe.szExeFile, L"winws.exe") ||
                 !_wcsicmp(pe.szExeFile, L"tunneldpi-engine.exe")) &&
                pe.th32ProcessID != g_pid && !own_rule_pid(pe.th32ProcessID)) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 0);
                    WaitForSingleObject(h, 3000);
                    CloseHandle(h);
                    app_log(L"Başka bir %s kapatıldı (PID %lu).", pe.szExeFile, pe.th32ProcessID);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}
