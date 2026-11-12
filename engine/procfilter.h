/*
 * Process filter for GoodbyeDPI
 *
 * Restricts DPI circumvention to TCP connections opened by (or, in invert
 * mode, not opened by) the executables listed in a text file.
 */
#ifndef PROCFILTER_H
#define PROCFILTER_H

#include <windows.h>

int  procfilter_load_list(const char *filename);   /* returns number of names loaded */
void procfilter_set_invert(int invert);
int  procfilter_enabled(void);
int  procfilter_start(void);                        /* opens the SOCKET layer handle */
void procfilter_stop(void);
/* local_port in network byte order. is_client_hello: the packet starts a TLS
 * handshake (waits briefly for a pending socket event). */
int  procfilter_port_allowed(UINT16 local_port, int is_client_hello);

#endif
