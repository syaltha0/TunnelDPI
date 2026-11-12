/*
 * Ad / tracker blocking (--block-list): drop and reset connections to listed hosts
 */
#ifndef BLOCKLIST_H
#define BLOCKLIST_H

#include <windows.h>
#include "windivert.h"

int  blocklist_load(const char *filename);          /* hosts added, -1 if unreadable */
int  blocklist_load_allow(const char *filename);    /* exceptions */
int  blocklist_enabled(void);
int  blocklist_check(const char *host, unsigned int len);   /* 1 = blocked */
unsigned long blocklist_count(void);
void blocklist_reject(HANDLE w, const WINDIVERT_ADDRESS *addr, char *packet, UINT packetLen,
                      PWINDIVERT_IPHDR ip, PWINDIVERT_IPV6HDR ip6, PWINDIVERT_TCPHDR tcp, UINT dataLen);

#endif
