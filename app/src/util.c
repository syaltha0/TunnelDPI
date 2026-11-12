/*
 * TunnelDPI - yardımcı fonksiyonlar
 */
#include "app.h"
#include <stdlib.h>

void app_path(wchar_t *out, size_t cap, const wchar_t *rel)
{
    /* Mutlak yol ise olduğu gibi kullan */
    if (rel[0] && (rel[1] == L':' || (rel[0] == L'\\' && rel[1] == L'\\'))) {
        wcsncpy(out, rel, cap - 1);
        out[cap - 1] = 0;
        return;
    }
    _snwprintf(out, cap, L"%s\\%s", g_appdir, rel);
    out[cap - 1] = 0;
}

BOOL file_exists(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL is_elevated(void)
{
    HANDLE tok;
    TOKEN_ELEVATION el = {0};
    DWORD len;
    BOOL ret = FALSE;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len))
            ret = el.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return ret;
}

/* Konsol penceresi açmadan bir komut çalıştırır, çıkış kodunu döner. */
DWORD run_hidden(const wchar_t *cmdline, DWORD timeout_ms)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = (DWORD)-1;
    wchar_t *buf = _wcsdup(cmdline);

    if (!buf)
        return code;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_OBJECT_0)
            GetExitCodeProcess(pi.hProcess, &code);
        else
            TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    free(buf);
    return code;
}

void trim_ws(wchar_t *s)
{
    wchar_t *p = s;
    size_t n;

    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')
        p++;
    if (p != s)
        memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    n = wcslen(s);
    while (n && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n'))
        s[--n] = 0;
}

/* seps içindeki karakterlerden biriyle ayrılmış bir sonraki boş olmayan parçayı alır. */
int next_token(const wchar_t **p, wchar_t *out, size_t cap, const wchar_t *seps)
{
    const wchar_t *s = *p;
    size_t n;

    for (;;) {
        while (*s && wcschr(seps, *s))
            s++;
        if (!*s) {
            *p = s;
            return 0;
        }
        n = wcscspn(s, seps);
        if (n >= cap)
            n = cap - 1;
        wmemcpy(out, s, n);
        out[n] = 0;
        s += wcscspn(s, seps);
        *p = s;
        trim_ws(out);
        if (out[0])
            return 1;
    }
}

wchar_t *read_text_file(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);
    DWORD size, rd = 0;
    char *raw;
    wchar_t *w;
    int off = 0, n;

    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    size = GetFileSize(f, NULL);
    if (size > 8 * 1024 * 1024) {
        CloseHandle(f);
        return NULL;
    }
    raw = malloc(size + 1);
    if (!raw || !ReadFile(f, raw, size, &rd, NULL)) {
        CloseHandle(f);
        free(raw);
        return NULL;
    }
    CloseHandle(f);
    if (rd >= 3 && (BYTE)raw[0] == 0xEF && (BYTE)raw[1] == 0xBB && (BYTE)raw[2] == 0xBF)
        off = 3;
    n = MultiByteToWideChar(CP_UTF8, 0, raw + off, rd - off, NULL, 0);
    w = malloc((n + 1) * sizeof(wchar_t));
    if (w) {
        MultiByteToWideChar(CP_UTF8, 0, raw + off, rd - off, w, n);
        w[n] = 0;
    }
    free(raw);
    return w;
}

BOOL write_text_file(const wchar_t *path, const wchar_t *text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *raw = malloc(n);
    HANDLE f;
    DWORD wr;
    BOOL ok = FALSE;

    if (!raw)
        return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, raw, n, NULL, NULL);
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        ok = WriteFile(f, raw, n - 1, &wr, NULL) && wr == (DWORD)(n - 1);
        CloseHandle(f);
    }
    free(raw);
    return ok;
}

/* Boştayken çalışma kümesini küçült (pencere kapanınca vb.) */
void trim_memory(void)
{
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

/* TaskDialog gibi, ama Esc ve başlıktaki X ile de kapanır */
HRESULT task_box(HWND owner, PCWSTR main, PCWSTR content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons, PCWSTR icon, int *btn)
{
    TASKDIALOGCONFIG tc = { sizeof(tc) };

    tc.hwndParent = owner;
    tc.hInstance = g_inst;
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = APP_NAME;
    tc.pszMainInstruction = main;
    tc.pszContent = content;
    tc.dwCommonButtons = buttons;
    tc.pszMainIcon = icon;
    return TaskDialogIndirect(&tc, btn, NULL, NULL);
}
