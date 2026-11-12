/*
 * Ad / tracker blocking for GoodbyeDPI (--block-list)
 *
 * Hostnames are loaded into their own sorted table (independent of
 * --blacklist). When an outbound TLS ClientHello (SNI) or HTTP request
 * (Host header) goes to a listed host or one of its subdomains, the packet
 * is dropped and the connection is reset in both directions, so the
 * browser fails immediately instead of waiting for a timeout.
 *
 * Accepted list formats, one entry per line:
 *   example.com
 *   0.0.0.0 example.com / 127.0.0.1 example.com      (hosts files)
 *   ||example.com^                                    (adblock domain rules)
 * Lines starting with '#' or '!' and adblock rules with other syntax are
 * ignored. "@@||example.com^" exceptions go to a separate allow table.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "windivert.h"
#include "goodbyedpi.h"
#include "blocklist.h"
/* Compact table: hostnames packed into one NUL-separated buffer, a sorted
 * offset array for binary search and one "already logged" byte per entry.
 * About 30 bytes per host instead of ~130 with a hash table, which matters
 * for 100k+ entry ad lists. */
typedef struct {
    char          *buf;
    size_t         len, cap;
    UINT32        *off;
    size_t         n, ncap;
    unsigned char *logged;
} table_t;

static table_t g_block, g_allow;
static unsigned long g_count;
static const char *g_sort_buf;

static int cmp_off(const void *a, const void *b)
{
    return strcmp(g_sort_buf + *(const UINT32 *)a, g_sort_buf + *(const UINT32 *)b);
}

static int add(table_t *t, const char *h)
{
    size_t l = strlen(h) + 1;

    if (t->len + l > t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 256 * 1024;
        char *nb;
        while (nc < t->len + l)
            nc *= 2;
        if (nc > 0xFFFFFFFFu || !(nb = realloc(t->buf, nc)))
            return 0;
        t->buf = nb;
        t->cap = nc;
    }
    if (t->n == t->ncap) {
        size_t nc = t->ncap ? t->ncap * 2 : 8192;
        UINT32 *no = realloc(t->off, nc * sizeof(UINT32));
        if (!no)
            return 0;
        t->off = no;
        t->ncap = nc;
    }
    memcpy(t->buf + t->len, h, l);
    t->off[t->n++] = (UINT32)t->len;
    t->len += l;
    return 1;
}

/* sort, drop duplicates, give back the slack */
static void finish(table_t *t)
{
    size_t i, w = 0;
    void *p;

    if (!t->n)
        return;
    g_sort_buf = t->buf;
    qsort(t->off, t->n, sizeof(UINT32), cmp_off);
    for (i = 0; i < t->n; i++)
        if (!w || strcmp(t->buf + t->off[w - 1], t->buf + t->off[i]))
            t->off[w++] = t->off[i];
    t->n = w;
    if ((p = realloc(t->off, t->n * sizeof(UINT32))) != NULL) {
        t->off = p;
        t->ncap = t->n;
    }
    if ((p = realloc(t->buf, t->len)) != NULL) {
        t->buf = p;
        t->cap = t->len;
    }
    free(t->logged);
    t->logged = calloc(t->n, 1);
}

static long find(const table_t *t, const char *h)
{
    size_t lo = 0, hi = t->n;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(h, t->buf + t->off[mid]);
        if (!c)
            return (long)mid;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return -1;
}

/* Normalise one list line into a hostname; returns 1 = block, 2 = allow, 0 = skip */
static int parse_line(char *s, char *out, size_t cap)
{
    char *e;
    int allow = 0;
    size_t n;

    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s || *s == '#' || *s == '!' || *s == '[')
        return 0;
    if (!strncmp(s, "@@", 2)) {
        allow = 1;
        s += 2;
    }
    if (!strncmp(s, "||", 2)) {
        s += 2;
        e = s + strcspn(s, "^/$*|");
        /* only whole-domain rules: "||host^" optionally with $important;
           path, wildcard and modifier rules ($third-party, ...) are skipped */
        if (*e == '^') {
            const char *rest = e + 1;
            if (*rest == '|')
                rest++;
            if (*rest && strcmp(rest, "$important"))
                return 0;
        } else if (*e) {
            return 0;
        }
        *e = 0;
    } else {
        /* hosts file: "0.0.0.0 host" */
        e = s + strcspn(s, " \t");
        if (*e && (!strncmp(s, "0.0.0.0", 7) || !strncmp(s, "127.0.0.1", 9) || !strncmp(s, "::", 2))) {
            s = e;
            while (*s == ' ' || *s == '\t')
                s++;
        }
        s[strcspn(s, " \t#")] = 0;
    }
    n = strlen(s);
    while (n && s[n - 1] == '.')
        s[--n] = 0;
    if (n < 3 || n >= cap || !strchr(s, '.') || strchr(s, '*') || strchr(s, '/'))
        return 0;
    if (!strcmp(s, "localhost") || !strcmp(s, "local") || strspn(s, "0123456789.") == n)
        return 0;                           /* IP literals never appear as SNI */
    for (e = s; *e; e++)
        out[e - s] = (char)tolower((unsigned char)*e);
    out[n] = 0;
    return allow ? 2 : 1;
}

static int load(const char *filename, int force_allow)
{
    FILE *f = fopen(filename, "r");
    char line[1024], host[HOST_MAXLEN + 1];
    int n = 0, kind;

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        kind = parse_line(line, host, sizeof(host));
        if (kind && force_allow)
            kind = 2;
        if (kind == 1)
            n += add(&g_block, host);
        else if (kind == 2)
            add(&g_allow, host);
    }
    fclose(f);
    finish(&g_block);
    finish(&g_allow);
    if (!force_allow)
        printf("Block list: %d hosts loaded from %s\n", n, filename);
    return n;
}

int blocklist_load(const char *filename)
{
    return load(filename, 0);
}

/* hosts that are never blocked (plain domain list) */
int blocklist_load_allow(const char *filename)
{
    return load(filename, 1);
}

int blocklist_enabled(void)
{
    return g_block.n > 0;
}

/* exact host or any parent domain; index or -1 */
static long match(const table_t *t, const char *h)
{
    long i;

    if (!t->n)
        return -1;
    while (h) {
        if ((i = find(t, h)) >= 0)
            return i;
        h = strchr(h, '.');
        if (h)
            h++;
    }
    return -1;
}

int blocklist_check(const char *host, unsigned int len)
{
    char h[HOST_MAXLEN + 1];
    long i;
    unsigned int k;

    if (!g_block.n || !host || !len || len > HOST_MAXLEN)
        return 0;
    for (k = 0; k < len; k++)
        h[k] = (char)tolower((unsigned char)host[k]);
    h[len] = 0;
    if (match(&g_allow, h) >= 0)
        return 0;
    i = match(&g_block, h);
    if (i < 0)
        return 0;
    g_count++;
    if (g_block.logged && !g_block.logged[i]) {   /* each listed entry once, not every connection */
        g_block.logged[i] = 1;
        printf("Blocked: %s\n", h);
    }
    return 1;
}

unsigned long blocklist_count(void)
{
    return g_count;
}

/* Reset the connection: RST towards the server (rewritten original packet,
 * payload removed) and a forged RST towards the local application. */
void blocklist_reject(HANDLE w, const WINDIVERT_ADDRESS *addr, char *packet, UINT packetLen,
                      PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6, PWINDIVERT_TCPHDR tcp, UINT dataLen)
{
    char rst[40 + 60 + 64];   /* IPv6 header + TCP header with options + IPv4 options */
    UINT hdr = packetLen - dataLen;
    WINDIVERT_ADDRESS a = *addr;
    PWINDIVERT_IPHDR rip = NULL;
    PWINDIVERT_IPV6HDR rip6 = NULL;
    PWINDIVERT_TCPHDR rtcp = NULL;
    UINT32 seq = ntohl(tcp->SeqNum), ack = ntohl(tcp->AckNum);

    if (hdr > sizeof(rst))
        return;

    /* 1) to the server: original headers, no payload, RST */
    memcpy(rst, packet, hdr);
    WinDivertHelperParsePacket(rst, hdr, &rip, &rip6, NULL, NULL, NULL, &rtcp, NULL, NULL, NULL, NULL, NULL);
    if (!rtcp)
        return;
    if (rip)
        rip->Length = htons((UINT16)hdr);
    else if (rip6)
        rip6->Length = htons((UINT16)(hdr - sizeof(WINDIVERT_IPV6HDR)));
    rtcp->Rst = 1;
    rtcp->Ack = 1;
    rtcp->Psh = 0;
    rtcp->Syn = 0;
    rtcp->Fin = 0;
    WinDivertHelperCalcChecksums(rst, hdr, &a, 0);
    WinDivertSend(w, rst, hdr, NULL, &a);

    /* 2) to the application: swapped addresses and ports, inbound */
    if (rip) {
        UINT32 t = rip->SrcAddr;
        rip->SrcAddr = rip->DstAddr;
        rip->DstAddr = t;
    } else if (rip6) {
        UINT32 t[4];
        memcpy(t, rip6->SrcAddr, sizeof(t));
        memcpy(rip6->SrcAddr, rip6->DstAddr, sizeof(t));
        memcpy(rip6->DstAddr, t, sizeof(t));
    }
    {
        UINT16 p = rtcp->SrcPort;
        rtcp->SrcPort = rtcp->DstPort;
        rtcp->DstPort = p;
    }
    rtcp->SeqNum = htonl(ack);
    rtcp->AckNum = htonl(seq + dataLen);
    a.Outbound = 0;
    WinDivertHelperCalcChecksums(rst, hdr, &a, 0);
    WinDivertSend(w, rst, hdr, NULL, &a);
    (void)ip; (void)ip6;
}
