/*
 * Additional desync techniques for GoodbyeDPI
 *  - multi-position TCP split with optional sequence overlap (seqovl)
 *  - autolist: learn blocked domains from RSTs / ClientHello retransmissions
 *  - UDP fakes before the first packet of new flows (e.g. Discord voice)
 */
#ifndef DESYNC_H
#define DESYNC_H

#include <windows.h>
#include "windivert.h"

/* --- multi split / seqovl --- */
int  desync_parse_split(const char *spec);   /* "1,sni,sni+5,midsni,-10" ; returns count */
void desync_set_seqovl(unsigned int bytes);
int  desync_split_active(void);
/* Sends the TCP payload as several segments. fallback_pos is used when no
 * --split-pos was given (seqovl only). Returns number of segments sent (0 =
 * nothing done, caller should handle the packet itself). */
int  desync_send_split(HANDLE w, WINDIVERT_ADDRESS addr, char *packet, UINT packetLen,
                       PVOID data, UINT dataLen, PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6,
                       PWINDIVERT_TCPHDR tcp, int sni_off, int sni_len, int reverse,
                       unsigned int fallback_pos);

/* --- autolist --- */
int  autolist_init(const char *filename);     /* loads the file into the blacklist */
int  autolist_enabled(void);
int  autolist_start(void);                    /* inbound RST watcher */
void autolist_stop(void);
void autolist_note_clienthello(int v6, const UINT32 *dst, UINT16 dport, UINT16 sport,
                               UINT32 seq, unsigned int hello_len, const char *sni, unsigned int sni_len);
void autolist_poll(void);                     /* main thread: save candidates, never apply them */

/* --- UDP fakes --- */
int  udpfake_parse_ports(const char *spec);   /* "50000-65535,3478" */
void udpfake_set_count(int n);
int  udpfake_enabled(void);
int  udpfake_port_match(UINT16 dport_host);
char *udpfake_filter_clause(void);            /* malloc'd WinDivert filter clause */
void udpfake_handle(HANDLE w, const WINDIVERT_ADDRESS *addr, const char *packet, UINT packetLen,
                    PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6, PWINDIVERT_UDPHDR udp, UINT dataLen);

#endif
