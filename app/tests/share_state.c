/* Durum testi; gerçek ağ veya güvenlik duvarı değiştirilmez. */
#define GetAdaptersAddresses mock_adapters
#include "../src/share.c"
Config g_cfg;
static BOOL has_hotspot;
DWORD run_hidden(const wchar_t *cmd, DWORD timeout) { return 0; }
void app_log(const wchar_t *fmt, ...) {}
ULONG WINAPI mock_adapters(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES a, PULONG size)
{
    static struct sockaddr_in addr;
    static IP_ADAPTER_UNICAST_ADDRESS u;
    memset(a, 0, sizeof(*a));
    if (!has_hotspot) return ERROR_NO_DATA;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = BSWAP32(HOTSPOT_NET | 1);
    u.Address.lpSockaddr = (struct sockaddr *)&addr;
    u.Address.iSockaddrLength = sizeof(addr);
    a->OperStatus = IfOperStatusUp;
    a->FirstUnicastAddress = &u;
    return NO_ERROR;
}
static SOCKET WSAAPI mock_socket(int a, int b, int c) { return 10; }
static int WSAAPI mock_bind(SOCKET s, const struct sockaddr *a, int len) { return SOCKET_ERROR; }
static int WSAAPI mock_option(SOCKET s, int a, int b, const char *v, int n) { return 0; }
static int WSAAPI mock_close(SOCKET s) { return 0; }
int main(void)
{
    W.m = (HMODULE)1;
    W.socket = mock_socket; W.bind = mock_bind; W.setsockopt = mock_option; W.close = mock_close;
    g_cfg.share_proxy = 1;
    share_apply();
    if (share_status(NULL, 0) != SHARE_WAITING) return 1;
    has_hotspot = TRUE;
    share_apply();
    if (share_status(NULL, 0) != SHARE_ERROR) return 2;
    g_cfg.share_proxy = 0;
    share_apply();
    if (share_status(NULL, 0) != SHARE_OFF) return 3;
    puts("share waiting/bind error/off: OK");
    return 0;
}
