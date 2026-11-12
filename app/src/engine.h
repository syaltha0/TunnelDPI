/*
 * TunnelDPI - koruma motorunun süreç yönetimi
 */
#pragma once
#include "config.h"

BOOL   engine_init(void);
BOOL   engine_start(const Config *c, wchar_t *err, size_t errcap);
void   engine_stop(void);
BOOL   engine_running(void);
LONG   engine_generation(void);
DWORD  engine_uptime_ms(void);
void   engine_kill_others(void);
void   engine_cmdline(const Config *c, wchar_t *out, size_t cap);
void   engine_set_override(const Profile *p);   /* NULL: etkin profil */
const Profile *engine_profile(const Config *c);
BOOL   engine_profile_overridden(void);         /* profil bulucu deniyor */
int    engine_rule_count(void);              /* çalışan site kuralı motoru */

/* Log halka tamponu */
void   log_append(const wchar_t *s, size_t n);
size_t log_get(wchar_t *out, size_t cap);      /* out NULL ise uzunluk döner */
void   log_clear(void);
void   log_file_enable(BOOL on);               /* gdpi-tray.log */
LONG   engine_blocked(wchar_t *last, size_t cap);   /* engellenen alan adı sayısı (bu oturum), son engellenen */
void   log_tail(wchar_t *out, size_t cap, int lines);
