/*
 * Additional desync techniques for GoodbyeDPI
 *
 * multi split / seqovl
 *   The TLS ClientHello (or HTTP request) is sent as several TCP segments cut
 *   at the given positions. With --seqovl N the first segment starts N bytes
 *   *before* the real sequence number and carries N junk bytes: the server
 *   has already acknowledged that range and discards it, while a DPI that
 *   reassembles the stream naively sees garbage in front of the handshake.
 *   Only the first segment changes length, so later sequence numbers of the
 *   connection are unaffected.
 *
 * autolist
 *   ClientHellos to hosts that are not on the list are remembered for a few
 *   seconds. Retransmissions are never treated as evidence of blocking.
 *   A correlated reset before any server data produces an unverified candidate
 *   in a separate file. Only the manually approved file is applied.
 *
 * UDP fakes
 *   Before the first packet of a new UDP flow to selected ports, N fake
 *   datagrams (64 zero bytes) are sent. This is the approach zapret uses for
 *   Discord voice: the DPI classifies the flow on junk and lets it through.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "windivert.h"
#include "goodbyedpi.h"
#include "blackwhitelist.h"
#include "desync.h"

/* ======================= multi split / seqovl ======================= */

enum { POS_ABS = 0, POS_SNI, POS_MIDSNI, POS_ENDSNI, POS_FROM_END };

static struct { int type; int off; } split_spec[16];
static int split_cnt = 0;
static unsigned int seqovl = 0;

int desync_parse_split(const char *spec) {
    char buf[256], *tok, *ctx = NULL;

    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (tok = strtok_s(buf, ",", &ctx); tok && split_cnt < 16; tok = strtok_s(NULL, ",", &ctx)) {
        int type = POS_ABS, off = 0;
        while (*tok == ' ')
            tok++;
        if (!strncmp(tok, "midsni", 6)) {
            type = POS_MIDSNI;
            tok += 6;
        } else if (!strncmp(tok, "endsni", 6)) {
            type = POS_ENDSNI;
            tok += 6;
        } else if (!strncmp(tok, "sni", 3)) {
            type = POS_SNI;
            tok += 3;
        }
        if (*tok)
            off = atoi(tok);
        if (type == POS_ABS && off < 0)
            type = POS_FROM_END;
        if (type == POS_ABS && off == 0)
            continue;
        split_spec[split_cnt].type = type;
        split_spec[split_cnt].off = off;
        split_cnt++;
    }
    return split_cnt;
}

void desync_set_seqovl(unsigned int bytes) {
    seqovl = bytes > 512 ? 512 : bytes;
}

int desync_split_active(void) {
    return split_cnt > 0 || seqovl > 0;
}

static int cmp_uint(const void *a, const void *b) {
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
    return x < y ? -1 : x > y;
}

static void send_part(HANDLE w, WINDIVERT_ADDRESS addr, const char *packet, UINT packetLen,
                      UINT dataLen, const char *data, PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6,
                      PWINDIVERT_TCPHDR tcp, UINT start, UINT len, UINT ovl) {
    char buf[MAX_PACKET_SIZE];
    UINT hdr = packetLen - dataLen, total;
    PWINDIVERT_TCPHDR t;

    if (hdr + ovl + len > MAX_PACKET_SIZE)
        ovl = 0;
    total = hdr + ovl + len;
    memcpy(buf, packet, hdr);
    memset(buf + hdr, 0, ovl);
    memcpy(buf + hdr + ovl, data + start, len);
    if (ip)
        ((PWINDIVERT_IPHDR)(buf + ((const char *)ip - packet)))->Length = htons((u_short)total);
    else if (ip6)
        ((PWINDIVERT_IPV6HDR)(buf + ((const char *)ip6 - packet)))->Length =
            htons((u_short)(ntohs(ip6->Length) - dataLen + ovl + len));
    t = (PWINDIVERT_TCPHDR)(buf + ((const char *)tcp - packet));
    t->SeqNum = htonl(ntohl(tcp->SeqNum) + start - ovl);
    addr.IPChecksum = 0;
    addr.TCPChecksum = 0;
    WinDivertHelperCalcChecksums(buf, total, &addr, 0);
    WinDivertSend(w, buf, total, NULL, &addr);
}

int desync_send_split(HANDLE w, WINDIVERT_ADDRESS addr, char *packet, UINT packetLen,
                      PVOID data, UINT dataLen, PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6,
                      PWINDIVERT_TCPHDR tcp, int sni_off, int sni_len, int reverse,
                      unsigned int fallback_pos) {
    unsigned int pos[18];
    int n = 0, i, k;

    for (i = 0; i < split_cnt; i++) {
        long p;
        switch (split_spec[i].type) {
        case POS_SNI:      if (sni_off < 0) continue; p = sni_off + split_spec[i].off; break;
        case POS_MIDSNI:   if (sni_off < 0) continue; p = sni_off + sni_len / 2 + split_spec[i].off; break;
        case POS_ENDSNI:   if (sni_off < 0) continue; p = sni_off + sni_len + split_spec[i].off; break;
        case POS_FROM_END: p = (long)dataLen + split_spec[i].off; break;
        default:           p = split_spec[i].off; break;
        }
        if (p > 0 && p < (long)dataLen)
            pos[n++] = (unsigned int)p;
    }
    if (!n && fallback_pos > 0 && fallback_pos < dataLen)
        pos[n++] = fallback_pos;
    if (!n)
        return 0;
    qsort(pos, (size_t)n, sizeof(pos[0]), cmp_uint);
    for (i = 1, k = 1; i < n; i++)            /* tekrarları at */
        if (pos[i] != pos[k - 1])
            pos[k++] = pos[i];
    n = k;

    /* segments: [0,pos0) [pos0,pos1) ... [posN-1,dataLen) */
    for (i = 0; i <= n; i++) {
        int idx = reverse ? n - i : i;
        UINT start = idx == 0 ? 0 : pos[idx - 1];
        UINT end = idx == n ? dataLen : pos[idx];
        send_part(w, addr, packet, packetLen, dataLen, (const char *)data, ip, ip6, tcp,
                  start, end - start, idx == 0 ? seqovl : 0);
    }
    return n + 1;
}

/* ============================ autolist ============================ */

#define PROBES  64
#define PENDING 16

typedef struct {
    UINT32 dst[4];
    UINT16 dport, sport;
    UINT32 seq, ack;
    int v6;
    DWORD  tick;
    int    used;
    char   sni[HOST_MAXLEN + 1];
} probe_t;

static char autolist_file[MAX_PATH];
static int autolist_on = 0;
static probe_t probes[PROBES];
static unsigned int probe_next = 0;
static char pending[PENDING][HOST_MAXLEN + 1];
static volatile LONG npending = 0;
static CRITICAL_SECTION cs;
static HANDLE rst_handle = INVALID_HANDLE_VALUE;

int autolist_init(const char *filename) {
    FILE *f;
    char line[512];
    int n = 0;

    strncpy(autolist_file, filename, sizeof(autolist_file) - 1);
    InitializeCriticalSection(&cs);
    autolist_on = 1;
    f = fopen(filename, "r");
    if (!f) {
        f = fopen(filename, "a");   /* create */
        if (f)
            fclose(f);
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) >= 2 && strlen(line) <= HOST_MAXLEN && blackwhitelist_add_hostname(line))
            n++;
    }
    fclose(f);
    printf("Autolist: %d hosts loaded from %s\n", n, filename);
    return n;
}

int autolist_enabled(void) {
    return autolist_on;
}

static void queue_host(const char *host) {
    if (!host[0] || npending >= PENDING)
        return;
    strcpy(pending[npending], host);
    InterlockedIncrement(&npending);
}

static int same_peer(const probe_t *p, const UINT32 *addr) {
    return memcmp(p->dst, addr, sizeof(p->dst)) == 0;
}

void autolist_note_clienthello(int v6, const UINT32 *dst, UINT16 dport, UINT16 sport,
                               UINT32 seq, unsigned int hello_len, const char *sni, unsigned int sni_len) {
    UINT32 a[4] = {0};
    DWORD now = GetTickCount();
    int i;

    if (!autolist_on || !sni_len || sni_len > HOST_MAXLEN)
        return;
    memcpy(a, dst, v6 ? 16 : 4);
    EnterCriticalSection(&cs);
    for (i = 0; i < PROBES; i++) {
        probe_t *p = &probes[i];
        if (!p->used || p->v6 != v6 || p->sport != sport || p->dport != dport || !same_peer(p, a))
            continue;
        /* Retransmission is also caused by ordinary loss/congestion, never proof of DPI. */
        LeaveCriticalSection(&cs);
        return;
    }
    i = (int)(probe_next++ % PROBES);
    memcpy(probes[i].dst, a, sizeof(a));
    probes[i].dport = dport;
    probes[i].sport = sport;
    probes[i].seq = seq;
    probes[i].ack = htonl(ntohl(seq) + hello_len);
    probes[i].v6 = v6;
    probes[i].tick = now;
    probes[i].used = 1;
    memcpy(probes[i].sni, sni, sni_len);
    probes[i].sni[sni_len] = '\0';
    LeaveCriticalSection(&cs);
}

static void autolist_observe(const UINT32 *a, int v6, const WINDIVERT_TCPHDR *tcp, UINT payload_len, DWORD now) {
    int i;
        EnterCriticalSection(&cs);
        for (i = 0; i < PROBES; i++) {
            probe_t *p = &probes[i];
            if (p->used && p->v6 == v6 && p->sport == tcp->DstPort && p->dport == tcp->SrcPort && same_peer(p, a) &&
                now - p->tick < 5000) {
                /* Any server data invalidates the failed-hello hypothesis. A later reset is normal. */
                if (payload_len) p->used = 0;
                else if (tcp->Rst && tcp->Ack && tcp->AckNum == p->ack) {
                    queue_host(p->sni); /* candidate only: servers can legitimately reject TLS */
                    p->used = 0;
                }
            }
        }
        LeaveCriticalSection(&cs);
}

static DWORD WINAPI rst_thread(LPVOID arg) {
    char packet[65535];
    UINT len;
    WINDIVERT_ADDRESS addr;
    PWINDIVERT_IPHDR ip;
    PWINDIVERT_IPV6HDR ip6;
    PWINDIVERT_TCPHDR tcp;
    UINT payload_len;
    (void)arg;

    while (WinDivertRecv(rst_handle, packet, sizeof(packet), &len, &addr)) {
        UINT32 a[4] = {0};
        DWORD now = GetTickCount();

        ip = NULL;
        ip6 = NULL;
        tcp = NULL;
        if (!WinDivertHelperParsePacket(packet, len, &ip, &ip6, NULL, NULL, NULL, &tcp, NULL,
                                        NULL, &payload_len, NULL, NULL) || !tcp)
            continue;
        if (ip)
            a[0] = ip->SrcAddr;
        else if (ip6)
            memcpy(a, ip6->SrcAddr, 16);
        autolist_observe(a, ip6 != NULL, tcp, payload_len, now);
    }
    return 0;
}

int autolist_start(void) {
    HANDLE th;

    if (!autolist_on)
        return 1;
    /* higher priority than the passive-DPI drop filter so RSTs are seen first */
    rst_handle = WinDivertOpen("inbound and !loopback and tcp and (tcp.Rst or tcp.PayloadLength > 0) and (tcp.SrcPort == 443 or tcp.SrcPort == 80)",
                               WINDIVERT_LAYER_NETWORK, 1, WINDIVERT_FLAG_SNIFF | WINDIVERT_FLAG_RECV_ONLY);
    if (rst_handle == INVALID_HANDLE_VALUE) {
        printf("Autolist: candidate observation unavailable (%lu); no sites will be inferred\n", GetLastError());
        return 1;
    }
    th = CreateThread(NULL, 0, rst_thread, NULL, 0, NULL);
    if (th)
        CloseHandle(th);
    return 1;
}

void autolist_stop(void) {
    if (rst_handle != INVALID_HANDLE_VALUE) {
        WinDivertClose(rst_handle);
        rst_handle = INVALID_HANDLE_VALUE;
    }
}

void autolist_poll(void) {
    char hosts[PENDING][HOST_MAXLEN + 1];
    int n, i;

    if (!npending)
        return;
    EnterCriticalSection(&cs);
    n = (int)npending;
    memcpy(hosts, pending, sizeof(pending[0]) * (size_t)n);
    npending = 0;
    LeaveCriticalSection(&cs);
    for (i = 0; i < n; i++) {
        {
            char path[MAX_PATH], line[512];
            const unsigned char *c = (const unsigned char *)hosts[i];
            int valid = *c != 0, exists = 0;
            FILE *f;
            for (; *c; c++) if (!isalnum(*c) && *c != '.' && *c != '-') valid = 0;
            if (!valid) continue;
            if (snprintf(path, sizeof(path), "%s.candidates.txt", autolist_file) >= (int)sizeof(path)) continue;
            f = fopen(path, "a+");
            if (f) {
                fseek(f, 0, SEEK_END);
                if (ftell(f) > 1024 * 1024) { fclose(f); continue; }
                rewind(f);
                while (fgets(line, sizeof(line), f)) {
                    line[strcspn(line, "\r\n")] = 0;
                    if (!strcmp(line, hosts[i])) { exists = 1; break; }
                }
                if (!exists) {
                    fseek(f, 0, SEEK_END);
                    fprintf(f, "%s\n", hosts[i]);
                }
                fclose(f);
                if (!exists) printf("Autolist: unverified connection-reset candidate: %s (not applied)\n", hosts[i]);
            }
            fflush(stdout);
        }
    }
}

/* ============================ UDP fakes ============================ */

#define FLOWS 4096

static struct { UINT16 lo, hi; } udp_ranges[16];
static int udp_nranges = 0;
static int udp_count = 0;
static struct {
    UINT32 dst[4];
    UINT16 dport, sport;
    DWORD  tick;
} flows[FLOWS];

int udpfake_parse_ports(const char *spec) {
    char buf[256], *tok, *ctx = NULL;

    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (tok = strtok_s(buf, ",", &ctx); tok && udp_nranges < 16; tok = strtok_s(NULL, ",", &ctx)) {
        int lo = atoi(tok), hi = lo;
        char *dash = strchr(tok, '-');
        if (dash)
            hi = atoi(dash + 1);
        if (lo < 1 || hi > 65535 || hi < lo)
            continue;
        udp_ranges[udp_nranges].lo = (UINT16)lo;
        udp_ranges[udp_nranges].hi = (UINT16)hi;
        udp_nranges++;
    }
    return udp_nranges;
}

void udpfake_set_count(int n) {
    udp_count = n < 1 ? 1 : n > 20 ? 20 : n;
}

int udpfake_enabled(void) {
    return udp_count > 0;
}

int udpfake_port_match(UINT16 port) {
    int i;

    for (i = 0; i < udp_nranges; i++)
        if (port >= udp_ranges[i].lo && port <= udp_ranges[i].hi)
            return 1;
    return 0;
}

char *udpfake_filter_clause(void) {
    char *s;
    size_t len = 0;
    int i;

    if (!udp_count)
        return NULL;
    if (!udp_nranges)
        udpfake_parse_ports("50000-65535");
    s = malloc(128 + 64 * (size_t)udp_nranges);
    if (!s)
        return NULL;
    len += (size_t)sprintf(s, "(outbound and !impostor and !loopback and udp and (");
    for (i = 0; i < udp_nranges; i++)
        len += (size_t)sprintf(s + len, "%s(udp.DstPort >= %u and udp.DstPort <= %u)", i ? " or " : "",
                               udp_ranges[i].lo, udp_ranges[i].hi);
    sprintf(s + len, "))");
    return s;
}

void udpfake_handle(HANDLE w, const WINDIVERT_ADDRESS *addr, const char *packet, UINT packetLen,
                    PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6, PWINDIVERT_UDPHDR udp, UINT dataLen) {
    UINT32 a[4] = {0};
    unsigned int h;
    DWORD now = GetTickCount();
    char buf[MAX_PACKET_SIZE];
    UINT hdr = packetLen - dataLen, total = hdr + 64;
    WINDIVERT_ADDRESS fa;
    int i;

    if (!addr->Outbound || total > MAX_PACKET_SIZE)
        return;
    if (ip)
        a[0] = ip->DstAddr;
    else if (ip6)
        memcpy(a, ip6->DstAddr, 16);
    h = (a[0] ^ a[1] ^ a[2] ^ a[3] ^ ((UINT32)udp->DstPort << 16) ^ udp->SrcPort) * 2654435761u;
    h = (h >> 20) % FLOWS;
    if (flows[h].dport == udp->DstPort && flows[h].sport == udp->SrcPort && !memcmp(flows[h].dst, a, 16) &&
        now - flows[h].tick < 60000) {
        flows[h].tick = now;           /* known flow */
        return;
    }
    memcpy(flows[h].dst, a, 16);
    flows[h].dport = udp->DstPort;
    flows[h].sport = udp->SrcPort;
    flows[h].tick = now;

    memcpy(buf, packet, hdr);
    memset(buf + hdr, 0, 64);
    if (ip)
        ((PWINDIVERT_IPHDR)(buf + ((const char *)ip - packet)))->Length = htons((u_short)total);
    else if (ip6)
        ((PWINDIVERT_IPV6HDR)(buf + ((const char *)ip6 - packet)))->Length =
            htons((u_short)(ntohs(ip6->Length) - dataLen + 64));
    ((PWINDIVERT_UDPHDR)(buf + ((const char *)udp - packet)))->Length = htons(8 + 64);
    memcpy(&fa, addr, sizeof(fa));
    fa.IPChecksum = 0;
    fa.UDPChecksum = 0;
    WinDivertHelperCalcChecksums(buf, total, &fa, 0);
    for (i = 0; i < udp_count; i++)
        WinDivertSend(w, buf, total, NULL, &fa);
}
