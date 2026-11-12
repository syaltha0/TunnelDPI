/*
 * Process filter for GoodbyeDPI
 *
 * A sniff-only WinDivert handle on the SOCKET layer reports every TCP
 * connect() with the owning process ID and local port, before the SYN is
 * sent. Ports opened by listed executables are marked in a 64K table which
 * the packet loop consults; packets of other connections are re-injected
 * untouched. Invert mode applies circumvention to everything except the
 * listed executables.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "windivert.h"
#include "procfilter.h"

#define MAX_NAMES 256
#define NAME_LEN  128
#define CACHE     64

enum { PORT_UNKNOWN = 0, PORT_ALLOWED = 1, PORT_DENIED = 2 };

static char names[MAX_NAMES][NAME_LEN];
static int names_cnt = 0;
static int invert = 0;
static volatile unsigned char port_state[65536];
static HANDLE sock_handle = INVALID_HANDLE_VALUE;
static HANDLE sock_thread = NULL;
static volatile int stopping = 0;

static struct {
    DWORD pid;
    DWORD tick;
    int   match;
} cache[CACHE];
static unsigned int cache_next = 0;

/* lower-case, strip directory and ".exe" */
static void normalize(char *s) {
    char *b = strrchr(s, '\\');
    size_t n;

    if (b)
        memmove(s, b + 1, strlen(b + 1) + 1);
    for (b = s; *b; b++)
        *b = (char)tolower((unsigned char)*b);
    n = strlen(s);
    if (n > 4 && strcmp(s + n - 4, ".exe") == 0)
        s[n - 4] = '\0';
}

int procfilter_load_list(const char *filename) {
    FILE *fp = fopen(filename, "r");
    char line[512];

    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp) && names_cnt < MAX_NAMES) {
        char *p = line, *e;

        while (*p == ' ' || *p == '\t')
            p++;
        e = p + strcspn(p, "\r\n#");
        while (e > p && (e[-1] == ' ' || e[-1] == '\t'))
            e--;
        *e = '\0';
        if (!*p || strlen(p) >= NAME_LEN)
            continue;
        strcpy(names[names_cnt], p);
        normalize(names[names_cnt]);
        if (names[names_cnt][0])
            names_cnt++;
    }
    fclose(fp);
    printf("Loaded %d process names from %s\n", names_cnt, filename);
    return names_cnt;
}

void procfilter_set_invert(int inv) {
    invert = inv;
}

int procfilter_enabled(void) {
    return names_cnt > 0;
}

static int name_matches(DWORD pid) {
    HANDLE h;
    char path[MAX_PATH];
    DWORD len = MAX_PATH;
    int i, match = 0;

    if (pid <= 4)
        return 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return 0;
    if (QueryFullProcessImageNameA(h, 0, path, &len)) {
        normalize(path);
        for (i = 0; i < names_cnt; i++)
            if (strcmp(path, names[i]) == 0) {
                match = 1;
                break;
            }
    }
    CloseHandle(h);
    return match;
}

/* PIDs are reused, so cached answers expire after a minute */
static int pid_matches(DWORD pid) {
    DWORD now = GetTickCount();
    unsigned int i;
    int m;

    for (i = 0; i < CACHE; i++)
        if (cache[i].pid == pid && now - cache[i].tick < 60000)
            return cache[i].match;
    m = name_matches(pid);
    i = cache_next++ % CACHE;
    cache[i].pid = pid;
    cache[i].tick = now;
    cache[i].match = m;
    return m;
}

static DWORD WINAPI socket_thread(LPVOID arg) {
    WINDIVERT_ADDRESS addr;
    (void)arg;

    while (!stopping) {
        if (!WinDivertRecv(sock_handle, NULL, 0, NULL, &addr)) {
            if (stopping || GetLastError() == ERROR_INVALID_HANDLE)
                break;
            continue;
        }
        if (addr.Socket.Protocol != IPPROTO_TCP)
            continue;
        if (addr.Event == WINDIVERT_EVENT_SOCKET_CONNECT) {
            int ok = pid_matches(addr.Socket.ProcessId) != invert;
            port_state[addr.Socket.LocalPort] = (unsigned char)(ok ? PORT_ALLOWED : PORT_DENIED);
        }
        else if (addr.Event == WINDIVERT_EVENT_SOCKET_CLOSE) {
            port_state[addr.Socket.LocalPort] = PORT_UNKNOWN;
        }
    }
    return 0;
}

int procfilter_start(void) {
    if (!procfilter_enabled())
        return 1;
    sock_handle = WinDivertOpen("event == CONNECT or event == CLOSE", WINDIVERT_LAYER_SOCKET, 0,
                                WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY);
    if (sock_handle == INVALID_HANDLE_VALUE) {
        printf("Error opening process filter (socket layer): %lu\n", GetLastError());
        return 0;
    }
    sock_thread = CreateThread(NULL, 0, socket_thread, NULL, 0, NULL);
    printf("Process filter active (%s %d processes)\n", invert ? "all except" : "only", names_cnt);
    return sock_thread != NULL;
}

void procfilter_stop(void) {
    stopping = 1;
    if (sock_handle != INVALID_HANDLE_VALUE) {
        WinDivertShutdown(sock_handle, WINDIVERT_SHUTDOWN_BOTH);
        WinDivertClose(sock_handle);
        sock_handle = INVALID_HANDLE_VALUE;
    }
}

int procfilter_port_allowed(UINT16 local_port, int is_client_hello) {
    UINT16 p = ntohs(local_port);
    unsigned char s = port_state[p];
    int i;

    /* The connect event normally arrives long before the first data packet;
     * for a TLS ClientHello give the socket thread a moment just in case. */
    for (i = 0; s == PORT_UNKNOWN && is_client_hello && i < 10; i++) {
        Sleep(1);
        s = port_state[p];
    }
    if (s == PORT_UNKNOWN)
        return invert;   /* connection predates the filter */
    return s == PORT_ALLOWED;
}
