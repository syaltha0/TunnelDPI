/*
 * TunnelDPI - bağlı ağın kimliği
 *
 * En düşük metrikli, çalışan ve ağ geçidi olan adaptör seçilir. Ağ geçidinin
 * MAC adresi komşu tablosundan (GetIpNetEntry2) okunur. Wi-Fi ise SSID
 * wlanapi.dll'den alınır (yalnızca bu sırada yüklenir).
 */
#include "netid.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdlib.h>
#include <stdio.h>

/* wlanapi (yalnızca gereken kısım) */
typedef struct { DWORD len; UCHAR ssid[32]; } X_DOT11_SSID;
typedef DWORD (WINAPI *WlanOpen_t)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI *WlanClose_t)(HANDLE, PVOID);
typedef DWORD (WINAPI *WlanQuery_t)(HANDLE, const GUID *, int, PVOID, PDWORD, PVOID *, PVOID);
typedef VOID  (WINAPI *WlanFree_t)(PVOID);

static BOOL parse_guid(const char *s, GUID *g)
{
    unsigned long d1;
    unsigned int d2, d3, b[8];
    int i;

    if (sscanf(s, "{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d1, &d2, &d3,
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11)
        return FALSE;
    g->Data1 = d1;
    g->Data2 = (USHORT)d2;
    g->Data3 = (USHORT)d3;
    for (i = 0; i < 8; i++)
        g->Data4[i] = (BYTE)b[i];
    return TRUE;
}

static BOOL wifi_ssid(const char *adapter_name, wchar_t *out, size_t cap)
{
    HMODULE m = LoadLibraryExW(L"wlanapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    WlanOpen_t pOpen;
    WlanClose_t pClose;
    WlanQuery_t pQuery;
    WlanFree_t pFree;
    HANDLE h = NULL;
    DWORD ver, sz = 0;
    PVOID data = NULL;
    GUID g;
    BOOL ok = FALSE;

    if (!m)
        return FALSE;
    pOpen = (WlanOpen_t)(void *)GetProcAddress(m, "WlanOpenHandle");
    pClose = (WlanClose_t)(void *)GetProcAddress(m, "WlanCloseHandle");
    pQuery = (WlanQuery_t)(void *)GetProcAddress(m, "WlanQueryInterface");
    pFree = (WlanFree_t)(void *)GetProcAddress(m, "WlanFreeMemory");
    if (pOpen && pClose && pQuery && pFree && parse_guid(adapter_name, &g) && pOpen(2, NULL, &ver, &h) == 0) {
        /* wlan_intf_opcode_current_connection = 7; WLAN_CONNECTION_ATTRIBUTES:
           isState(4) + wlanConnectionMode(4) + strProfileName(256 wchar) + DOT11_SSID ... */
        if (pQuery(h, &g, 7, NULL, &sz, &data, NULL) == 0 && data) {
            const X_DOT11_SSID *ssid = (const X_DOT11_SSID *)((BYTE *)data + 8 + 256 * sizeof(wchar_t));
            if (ssid->len && ssid->len <= 32) {
                int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)ssid->ssid, (int)ssid->len, out, (int)cap - 1);
                out[n > 0 ? n : 0] = 0;
                ok = n > 0;
            }
            pFree(data);
        }
        pClose(h, NULL);
    }
    FreeLibrary(m);
    return ok;
}

BOOL netid_current(wchar_t *key, size_t kcap, wchar_t *name, size_t ncap)
{
    IP_ADAPTER_ADDRESSES *aa = NULL, *a, *best = NULL;
    ULONG len = 16 * 1024, r;
    MIB_IPNET_ROW2 row;
    BOOL ok = FALSE;

    key[0] = name[0] = 0;
    for (;;) {
        aa = malloc(len);
        if (!aa)
            return FALSE;
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST |
                                 GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &len);
        if (r != ERROR_BUFFER_OVERFLOW)
            break;
        free(aa);
    }
    if (r != NO_ERROR) {
        free(aa);
        return FALSE;
    }
    for (a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress ||
            a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL)
            continue;
        if (a->FirstGatewayAddress->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        if (!best || a->Ipv4Metric < best->Ipv4Metric)
            best = a;
    }
    if (best) {
        memset(&row, 0, sizeof(row));
        row.InterfaceIndex = best->IfIndex;
        row.Address.Ipv4 = *(SOCKADDR_IN *)best->FirstGatewayAddress->Address.lpSockaddr;
        row.Address.si_family = AF_INET;
        if ((GetIpNetEntry2(&row) == NO_ERROR || ResolveIpNetEntry2(&row, NULL) == NO_ERROR) &&
            row.PhysicalAddressLength == 6) {
            const BYTE *m = row.PhysicalAddress;
            _snwprintf(key, kcap, L"%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            key[kcap - 1] = 0;
            ok = TRUE;
        }
        if (!(best->IfType == IF_TYPE_IEEE80211 && wifi_ssid(best->AdapterName, name, ncap))) {
            wcsncpy(name, best->FriendlyName, ncap - 1);
            name[ncap - 1] = 0;
        }
    }
    free(aa);
    return ok;
}
