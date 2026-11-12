/*
 * TunnelDPI - küçük HTTPS istemcisi
 *
 * winhttp.dll her çağrıda yüklenir ve sonra bırakılır; tepside boşta dururken
 * bellekte tutulmaz.
 */
#include "http.h"
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>

typedef HINTERNET (WINAPI *Open_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL      (WINAPI *Timeouts_t)(HINTERNET, int, int, int, int);
typedef BOOL      (WINAPI *Crack_t)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);
typedef HINTERNET (WINAPI *Connect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *Request_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL      (WINAPI *Send_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL      (WINAPI *Receive_t)(HINTERNET, LPVOID);
typedef BOOL      (WINAPI *Query_t)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL      (WINAPI *Read_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL      (WINAPI *Close_t)(HINTERNET);
typedef BOOL      (WINAPI *SetOpt_t)(HINTERNET, DWORD, LPVOID, DWORD);

typedef struct {
    HMODULE    m;
    Open_t     Open;
    Timeouts_t Timeouts;
    Crack_t    Crack;
    Connect_t  Connect;
    Request_t  Request;
    Send_t     Send;
    Receive_t  Receive;
    Query_t    Query;
    Read_t     Read;
    Close_t    Close;
    SetOpt_t   SetOpt;      /* isteğe bağlı */
} Api;

static BOOL api_load(Api *a)
{
    memset(a, 0, sizeof(*a));
    a->m = LoadLibraryExW(L"winhttp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!a->m)
        return FALSE;
#define L_(f, n) a->f = (__typeof__(a->f))(void *)GetProcAddress(a->m, n)
    L_(Open, "WinHttpOpen");
    L_(Timeouts, "WinHttpSetTimeouts");
    L_(Crack, "WinHttpCrackUrl");
    L_(Connect, "WinHttpConnect");
    L_(Request, "WinHttpOpenRequest");
    L_(Send, "WinHttpSendRequest");
    L_(Receive, "WinHttpReceiveResponse");
    L_(Query, "WinHttpQueryHeaders");
    L_(Read, "WinHttpReadData");
    L_(Close, "WinHttpCloseHandle");
    L_(SetOpt, "WinHttpSetOption");
#undef L_
    if (a->Open && a->Timeouts && a->Crack && a->Connect && a->Request && a->Send && a->Receive &&
        a->Query && a->Read && a->Close)
        return TRUE;
    FreeLibrary(a->m);
    a->m = NULL;
    return FALSE;
}

/* raw: yönlendirmeyi izleme, vekil kullanma (giriş sayfası denetimi için) */
static char *fetch(const wchar_t *url, const wchar_t *headers, DWORD max_bytes, DWORD *status, DWORD *out_len, BOOL raw)
{
    Api a;
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256], path[2048];
    HINTERNET ses = NULL, con = NULL, req = NULL;
    char *buf = NULL;
    DWORD len = 0, cap = 0, got, code = 0, sz = sizeof(code);

    if (status)
        *status = 0;
    if (out_len)
        *out_len = 0;
    if (!api_load(&a))
        return NULL;
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!a.Crack(url, 0, 0, &uc))
        goto out;
    ses = a.Open(APP_UA, raw ? 1 /* NO_PROXY */ : 4 /* AUTOMATIC_PROXY */, NULL, NULL, 0);
    if (!ses)
        goto out;
    a.Timeouts(ses, 6000, 6000, 8000, 15000);
    con = a.Connect(ses, host, uc.nPort, 0);
    if (!con)
        goto out;
    req = a.Request(con, L"GET", path[0] ? path : L"/", NULL, NULL, NULL,
                    uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req && raw && a.SetOpt) {
        DWORD v = WINHTTP_DISABLE_REDIRECTS;
        a.SetOpt(req, WINHTTP_OPTION_DISABLE_FEATURE, &v, sizeof(v));
    }
    if (!req || !a.Send(req, headers, headers ? (DWORD)-1L : 0, NULL, 0, 0, 0) || !a.Receive(req, NULL))
        goto out;
    a.Query(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
    if (status)
        *status = code;
    for (;;) {
        if (cap - len < 16384) {
            char *nb;
            cap = cap ? cap * 2 : 65536;
            nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                buf = NULL;
                goto out;
            }
            buf = nb;
        }
        if (!a.Read(req, buf + len, cap - len, &got) || !got)
            break;
        len += got;
        if (len > max_bytes)
            break;
    }
    if (buf)
        buf[len] = 0;
    if (out_len)
        *out_len = len;
out:
    if (req) a.Close(req);
    if (con) a.Close(con);
    if (ses) a.Close(ses);
    FreeLibrary(a.m);
    return buf;
}

char *http_get_n(const wchar_t *url, const wchar_t *headers, DWORD max_bytes, DWORD *status, DWORD *out_len)
{
    return fetch(url, headers, max_bytes, status, out_len, FALSE);
}

char *http_get(const wchar_t *url, const wchar_t *headers, DWORD max_bytes, DWORD *status)
{
    return fetch(url, headers, max_bytes, status, NULL, FALSE);
}

char *http_get_raw(const wchar_t *url, DWORD max_bytes, DWORD *status)
{
    return fetch(url, NULL, max_bytes, status, NULL, TRUE);
}

BOOL http_probe(const wchar_t *host, DWORD *ms, DWORD *err)
{
    Api a;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    ULONGLONG t0;
    BOOL ok = FALSE;

    *ms = 0;
    *err = 0;
    if (!api_load(&a)) {
        *err = ERROR_MOD_NOT_FOUND;
        return FALSE;
    }
    ses = a.Open(APP_UA, 4 /* AUTOMATIC_PROXY */, NULL, NULL, 0);
    if (ses) {
        a.Timeouts(ses, 4000, 4000, 4000, 5000);
        t0 = GetTickCount64();
        con = a.Connect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (con)
            req = a.Request(con, L"HEAD", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (req && a.Send(req, NULL, 0, NULL, 0, 0, 0) && a.Receive(req, NULL))
            ok = TRUE;
        else
            *err = GetLastError();
        *ms = (DWORD)(GetTickCount64() - t0);
    } else {
        *err = GetLastError();
    }
    if (req) a.Close(req);
    if (con) a.Close(con);
    if (ses) a.Close(ses);
    FreeLibrary(a.m);
    return ok;
}

BOOL json_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[96];
    const char *p, *e;
    size_t n;

    if (!json || strlen(key) > 80)
        return FALSE;
    strcpy(pat, "\"");
    strcat(pat, key);
    strcat(pat, "\"");
    p = strstr(json, pat);
    if (!p)
        return FALSE;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return FALSE;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != '"')
        return FALSE;
    e = strchr(++p, '"');
    if (!e)
        return FALSE;
    n = (size_t)(e - p);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return TRUE;
}
