/*
 * TunnelDPI - ayarlar (gdpi-tray.ini, UTF-16 INI)
 */
#include "config.h"
#include "i18n.h"
#include "dns.h"
#include "block.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

Config g_cfg;

#define O(x)  offsetof(Profile, x)
#define C(x)  ((int)(sizeof(((Profile *)0)->x) / sizeof(wchar_t)))

const Field g_fields[] = {
    /* bayraklar */
    { L"p",            L"-p",             F_BOOL,  O(p), 0, 0,               L"-p  Pasif DPI'ı engelle" },
    { L"q",            L"-q",             F_BOOL,  O(q), 0, 0,               L"-q  QUIC/HTTP3'ü engelle" },
    { L"r",            L"-r",             F_BOOL,  O(r), 0, 0,               L"-r  Host → hoSt" },
    { L"s",            L"-s",             F_BOOL,  O(s), 0, 0,               L"-s  Host değerindeki boşluğu kaldır" },
    { L"a",            L"-a",             F_BOOL,  O(a), 0, 0,               L"-a  Method/URI arasına ek boşluk" },
    { L"m",            L"-m",             F_BOOL,  O(m), 0, 0,               L"-m  Host harflerini karıştır" },
    { L"w",            L"-w",             F_BOOL,  O(w), 0, 0,               L"-w  Tüm portlarda HTTP ara" },
    { L"n",            L"-n",             F_BOOL,  O(n), 0, 0,               L"-n  -k ile ilk ACK'i bekleme" },
    { L"wrong_chksum", L"--wrong-chksum", F_BOOL,  O(wrong_chksum), 0, 0,    L"--wrong-chksum  Hatalı checksum" },
    { L"wrong_seq",    L"--wrong-seq",    F_BOOL,  O(wrong_seq), 0, 0,       L"--wrong-seq  Hatalı SEQ/ACK" },
    { L"native_frag",  L"--native-frag",  F_BOOL,  O(native_frag), 0, 0,     L"--native-frag  Native parçalama" },
    { L"reverse_frag", L"--reverse-frag", F_BOOL,  O(reverse_frag), 0, 0,    L"--reverse-frag  Ters sıralı parça" },
    { L"allow_no_sni", L"--allow-no-sni", F_BOOL,  O(allow_no_sni), 0, 0,    L"--allow-no-sni  SNI yoksa da uygula" },
    { L"frag_by_sni",  L"--frag-by-sni",  F_BOOL,  O(frag_by_sni), 0, 0,     L"--frag-by-sni  SNI'den parçala" },
    { L"dns_verb",     L"--dns-verb",     F_BOOL,  O(dns_verb), 0, 0,        L"--dns-verb  Ayrıntılı DNS log" },
    { L"md5sig",       L"--md5sig",       F_BOOL,  O(md5sig), 0, 0,          L"--md5sig  MD5 imzalı sahte paket" },
    { L"autolist",     L"--autolist",     F_BOOL,  O(autolist), 0, 0,        L"--autolist  Engelli siteleri öğren" },
    /* değerler */
    { L"f",            L"-f",             F_NUM,   O(f), 0, C(f),            L"-f  HTTP parça" },
    { L"k",            L"-k",             F_NUM,   O(k), 0, C(k),            L"-k  Keep-alive parça" },
    { L"e",            L"-e",             F_NUM,   O(e), 0, C(e),            L"-e  HTTPS parça" },
    { L"set_ttl",      L"--set-ttl",      F_NUM,   O(set_ttl), 0, C(set_ttl),         L"--set-ttl" },
    { L"min_ttl",      L"--min-ttl",      F_NUM,   O(min_ttl), 0, C(min_ttl),         L"--min-ttl" },
    { L"fake_gen",     L"--fake-gen",     F_NUM,   O(fake_gen), 0, C(fake_gen),       L"--fake-gen" },
    { L"fake_resend",  L"--fake-resend",  F_NUM,   O(fake_resend), 0, C(fake_resend), L"--fake-resend" },
    { L"split_pos",    L"--split-pos",    F_STR,   O(split_pos), 0, C(split_pos),     L"--split-pos" },
    { L"seqovl",       L"--seqovl",       F_NUM,   O(seqovl), 0, C(seqovl),           L"--seqovl" },
    { L"udp_fake",     L"--udp-fake",     F_NUM,   O(udp_fake), 0, C(udp_fake),       L"--udp-fake" },
    { L"udp_ports",    L"--udp-ports",    F_STR,   O(udp_ports), 0, C(udp_ports),     L"--udp-ports" },
    { L"ip_id",        L"--ip-id",        F_MULTI, O(ipid), 0, C(ipid),               L"--ip-id" },
    { L"auto_ttl",     L"--auto-ttl",     F_OPT,   O(auto_ttl), O(auto_ttl_v), C(auto_ttl_v),          L"--auto-ttl" },
    { L"max_payload",  L"--max-payload",  F_OPT,   O(max_payload), O(max_payload_v), C(max_payload_v), L"--max-payload" },
    /* geniş alanlar */
    { L"port",         L"--port",         F_MULTI, O(port), 0, C(port),               L"Ek portlar (--port)" },
    { L"fake_sni",     L"--fake-with-sni",F_MULTI, O(fake_sni), 0, C(fake_sni),       L"Fake SNI (--fake-with-sni)" },
    { L"fake_hex",     L"--fake-from-hex",F_STR,   O(fake_hex), 0, C(fake_hex),       L"Fake hex (--fake-from-hex)" },
    { L"blacklist",    L"--blacklist",    F_MULTI, O(blacklist), 0, C(blacklist),     L"Blacklist (--blacklist)" },
    { L"extra",        L"",               F_RAW,   O(extra), 0, C(extra),             L"Ek argümanlar (ham)" },
};
const int g_nfields = sizeof(g_fields) / sizeof(g_fields[0]);

const wchar_t *g_preset_names[10] = {
    L"Yok (yalnızca aşağıdaki ayarlar)",
    L"-1  -p -r -s -f 2 -k 2 -n -e 2 (en uyumlu)",
    L"-2  -p -r -s -f 2 -k 2 -n -e 40",
    L"-3  -p -r -s -e 40",
    L"-4  -p -r -s",
    L"-5  parçalama + fake + auto-ttl (Türkiye)",
    L"-6  parçalama + fake + wrong-seq",
    L"-7  parçalama + fake + wrong-chksum",
    L"-8  -7 + wrong-seq",
    L"-9  -8 + HTTP/3 kapalı (en kapsamlı)",
};

#define FPTR_INT(p, f)  ((int *)((char *)(p) + (f)->off))
#define FPTR_STR(p, f)  ((wchar_t *)((char *)(p) + (f)->off))
#define FPTR_STR2(p, f) ((wchar_t *)((char *)(p) + (f)->off2))

static void ini_path(wchar_t *out)
{
    app_path(out, MAX_PATH, APP_INI);
}

void profile_init(Profile *p, const wchar_t *name)
{
    memset(p, 0, sizeof(*p));
    wcsncpy(p->name, name, NAME_CAP - 1);
}

/* zapret (winws) hazır stratejileri. winws zapret klasöründe çalışır, bu
   yüzden yollar ona göredir. UDP blokları listeden bağımsız uygulanır. */
static const wchar_t ZAPRET_COMMON[] =
    L"--wf-tcp=80,443 --wf-udp=443,50000-65535\r\n"
    L"--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic=@fake/quic_initial_www_google_com.bin --new\r\n"
    L"--filter-udp=50000-65535 --filter-l7=discord,stun --dpi-desync=fake --dpi-desync-repeats=6 --new\r\n"
    L"--filter-tcp=80 %HOSTLIST% --dpi-desync=fake,multisplit --dpi-desync-autottl=2 --dpi-desync-fooling=md5sig --new\r\n";

static const struct { const wchar_t *name, *tcp443; } g_zapret_seeds[] = {
    { L"Zapret",
      L"--filter-tcp=443 %HOSTLIST% --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-repeats=6 "
      L"--dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@fake/tls_clienthello_www_google_com.bin" },
    { L"Zapret (disorder)",
      L"--filter-tcp=443 %HOSTLIST% --dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld --dpi-desync-repeats=6 "
      L"--dpi-desync-fooling=badseq,md5sig --dpi-desync-fake-tls=@fake/tls_clienthello_www_google_com.bin" },
    { L"Zapret (TTL)",
      L"--filter-tcp=443 %HOSTLIST% --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,sniext+1 --dpi-desync-autottl=2 "
      L"--dpi-desync-repeats=6 --dpi-desync-fake-tls=@fake/tls_clienthello_www_google_com.bin" },
};

static void seed_zapret(Config *c)
{
    size_t k;

    for (k = 0; k < sizeof(g_zapret_seeds) / sizeof(g_zapret_seeds[0]) && c->nprofiles < MAX_PROFILES; k++) {
        Profile *p;

        if (config_find(c, g_zapret_seeds[k].name) >= 0)
            continue;
        p = &c->profiles[c->nprofiles++];
        profile_init(p, g_zapret_seeds[k].name);
        p->engine = ENGINE_ZAPRET;
        _snwprintf(p->zapret, ZAPRET_CAP, L"%s%s", ZAPRET_COMMON, g_zapret_seeds[k].tcp443);
        p->zapret[ZAPRET_CAP - 1] = 0;
    }
}

void config_zapret_default(Profile *p)
{
    _snwprintf(p->zapret, ZAPRET_CAP, L"%s%s", ZAPRET_COMMON, g_zapret_seeds[0].tcp443);
    p->zapret[ZAPRET_CAP - 1] = 0;
}

void config_defaults(Config *c)
{
    Profile *p;

    memset(c, 0, sizeof(*c));
    c->start_bypass = 1;
    c->auto_restart = 1;
    c->notify = 1;
    c->kill_others = 1;
    c->hotkey = 1;
    c->update_check = 1;
    c->net_auto = 1;
    c->log_file = 1;
    c->dns_mode = DNS_OFF;
    c->dns_method = DNS_REDIRECT;
    c->dns_preset = 0; /* Yandex */
    wcscpy(c->dns_port, L"53");
    wcscpy(c->app_repo, APP_REPO_DEFAULT);
    c->auto_heal = 1;
    c->dns_failover = 1;
    c->sched_from = 2 * 60;
    c->sched_to = 8 * 60;

    /* Genel profiller: liste yok, tüm sitelere uygulanır. Argümanlar upstream
       turkey_dnsredir*.cmd dosyalarından; DNS kısmı DNS ayarından seçilir. */
    p = &c->profiles[c->nprofiles++];
    profile_init(p, L"Standart");
    p->preset = 5;
    wcscpy(p->set_ttl, L"5");

    p = &c->profiles[c->nprofiles++];
    profile_init(p, L"Superbox");
    p->preset = 5;

    p = &c->profiles[c->nprofiles++];
    profile_init(p, L"Superonline");
    wcscpy(p->set_ttl, L"3");

    p = &c->profiles[c->nprofiles++];
    profile_init(p, L"Agresif (-9)");
    p->preset = 9;

    seed_zapret(c);
    c->seed = SEED_VERSION;
    wcscpy(c->active, c->profiles[0].name);
}

/* --- okuma --- */

static const wchar_t *sect_get(const wchar_t *sect, const wchar_t *key)
{
    size_t kl = wcslen(key);
    const wchar_t *s;

    for (s = sect; *s; s += wcslen(s) + 1) {
        if (_wcsnicmp(s, key, kl) == 0 && s[kl] == L'=')
            return s + kl + 1;
    }
    return NULL;
}

static void get_str(const wchar_t *sect, const wchar_t *key, wchar_t *out, int cap)
{
    const wchar_t *v = sect_get(sect, key);

    if (v) {
        wcsncpy(out, v, cap - 1);
        out[cap - 1] = 0;
    }
}

static void get_int(const wchar_t *sect, const wchar_t *key, int *out)
{
    const wchar_t *v = sect_get(sect, key);

    if (v)
        *out = _wtoi(v);
}

#define SECT_CAP 16384

static void read_profile(const wchar_t *path, const wchar_t *section, wchar_t *sect, Profile *p)
{
    int i;

    profile_init(p, section + 8);
    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(section, sect, SECT_CAP, path);
    get_int(sect, L"preset", &p->preset);
    get_int(sect, L"engine", &p->engine);
    get_int(sect, L"hazir", &p->managed);
    if (p->engine != ENGINE_ZAPRET)
        p->engine = ENGINE_CLASSIC;
    get_str(sect, L"zapret", p->zapret, ZAPRET_CAP);
    {
        /* INI tek satır: satır sonları "\n" olarak saklanır */
        wchar_t *q;
        while ((q = wcsstr(p->zapret, L"\\n")) != NULL) {
            q[0] = L'\r';
            q[1] = L'\n';
        }
    }
    for (i = 0; i < g_nfields; i++) {
        const Field *f = &g_fields[i];

        if (f->type == F_BOOL) {
            get_int(sect, f->key, FPTR_INT(p, f));
        } else if (f->type == F_OPT) {
            wchar_t k2[64];

            get_int(sect, f->key, FPTR_INT(p, f));
            _snwprintf(k2, 64, L"%s_value", f->key);
            get_str(sect, k2, FPTR_STR2(p, f), f->cap);
        } else {
            get_str(sect, f->key, FPTR_STR(p, f), f->cap);
        }
    }
    if (p->preset < 0 || p->preset > 9)
        p->preset = 0;
}

void config_load(Config *c)
{
    wchar_t path[MAX_PATH];
    wchar_t *names, *sect, *n;
    Profile *defp = NULL;
    int defn;

    ini_path(path);
    config_defaults(c);
    if (!file_exists(path)) {
        config_save(c);
        return;
    }

    names = malloc(SECT_CAP * sizeof(wchar_t));
    sect  = malloc(SECT_CAP * sizeof(wchar_t));
    defp  = malloc(sizeof(c->profiles));
    if (!names || !sect || !defp)
        goto out;

    /* Varsayılan profiller yalnızca dosyada hiç profil yoksa kullanılır */
    memcpy(defp, c->profiles, sizeof(c->profiles));
    defn = c->nprofiles;
    c->nprofiles = 0;

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"General", sect, SECT_CAP, path);
    get_int(sect, L"StartBypass", &c->start_bypass);
    get_int(sect, L"AutoRestart", &c->auto_restart);
    get_int(sect, L"Notify", &c->notify);
    get_int(sect, L"IgnoreService", &c->ignore_service);
    get_int(sect, L"KillOthers", &c->kill_others);
    get_int(sect, L"Hotkey", &c->hotkey);
    get_int(sect, L"AppMode", &c->app_mode);
    if (c->app_mode < APPS_ALL || c->app_mode > APPS_EXCEPT)
        c->app_mode = APPS_ALL;
    get_int(sect, L"Lang", &c->lang);
    if (c->lang < 0 || c->lang > 2)
        c->lang = 0;
    get_int(sect, L"Theme", &c->theme);
    c->seed = 0;
    get_int(sect, L"Seed", &c->seed);
    get_int(sect, L"LogFile", &c->log_file);
    get_str(sect, L"TestHost", c->test_host, MID_CAP);
    if (c->theme < 0 || c->theme > 2)
        c->theme = 0;
    get_str(sect, L"ActiveProfile", c->active, NAME_CAP);

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(tr(L"DNS"), sect, SECT_CAP, path);
    get_int(sect, L"Mode", &c->dns_mode);
    get_int(sect, L"Method", &c->dns_method);
    get_int(sect, L"Preset", &c->dns_preset);
    get_int(sect, L"DoH", &c->dns_doh);
    get_str(sect, L"CustomV4", c->dns_v4, MID_CAP);
    get_str(sect, L"CustomV6", c->dns_v6, MID_CAP);
    get_str(sect, L"CustomPort", c->dns_port, SMALL_CAP);
    get_str(sect, L"CustomDoH", c->dns_doh_tpl, MID_CAP * 2);
    get_int(sect, L"Failover", &c->dns_failover);

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"Block", sect, SECT_CAP, path);
    get_int(sect, L"On", &c->block_on);
    get_str(sect, L"Lists", c->block_lists, BIG_CAP);

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"Updates", sect, SECT_CAP, path);
    get_int(sect, L"Check", &c->update_check);
    get_str(sect, L"SeenTag", c->seen_tag, 64);
    get_str(sect, L"NotesVersion", c->notes_version, 64);
    get_str(sect, L"AppRepo", c->app_repo, 128);

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"Networks", sect, SECT_CAP, path);
    get_int(sect, L"Auto", &c->net_auto);
    {
        const wchar_t *e;
        for (e = sect; *e && c->nnets < MAX_NETS; e += wcslen(e) + 1) {
            const wchar_t *eq = wcschr(e, L'='), *bar;
            size_t kl;
            if (!eq || !_wcsnicmp(e, L"Auto=", 5))
                continue;
            kl = (size_t)(eq - e);
            if (kl == 0 || kl >= 24)
                continue;
            wmemcpy(c->nets[c->nnets].key, e, kl);
            c->nets[c->nnets].key[kl] = 0;
            bar = wcschr(eq + 1, L'|');
            if (bar) {
                size_t pl = (size_t)(bar - eq - 1);
                if (pl >= NAME_CAP)
                    pl = NAME_CAP - 1;
                wmemcpy(c->nets[c->nnets].profile, eq + 1, pl);
                c->nets[c->nnets].profile[pl] = 0;
                wcsncpy(c->nets[c->nnets].name, bar + 1, 63);
            } else {
                wcsncpy(c->nets[c->nnets].profile, eq + 1, NAME_CAP - 1);
            }
            c->nnets++;
        }
    }
    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"Auto", sect, SECT_CAP, path);
    get_int(sect, L"Heal", &c->auto_heal);
    get_int(sect, L"Triggers", &c->trig_on);
    get_str(sect, L"TriggerApps", c->trig_apps, BIG_CAP);
    get_int(sect, L"Schedule", &c->sched_on);
    get_int(sect, L"From", &c->sched_from);
    get_int(sect, L"To", &c->sched_to);
    get_int(sect, L"Days", &c->sched_days);
    get_int(sect, L"Game", &c->game_mode);
    get_int(sect, L"Share", &c->share_proxy);
    {
        wchar_t rec[MAX_RECENT * NAME_CAP];
        const wchar_t *s = rec;
        int k = 0;

        get_str(sect, L"Recent", rec, MAX_RECENT * NAME_CAP);
        while (k < MAX_RECENT && next_token(&s, c->recent[k], NAME_CAP, L"|"))
            k++;
    }
    if (c->sched_from < 0 || c->sched_from >= 1440)
        c->sched_from = 120;
    if (c->sched_to < 0 || c->sched_to >= 1440)
        c->sched_to = 480;
    if (c->sched_days < 0 || c->sched_days > 2)
        c->sched_days = 0;

    sect[0] = sect[1] = 0;
    GetPrivateProfileSectionW(L"Rules", sect, SECT_CAP, path);
    {
        const wchar_t *e;
        for (e = sect; *e && c->nrules < MAX_RULES; e += wcslen(e) + 1) {
            const wchar_t *eq = wcschr(e, L'=');
            size_t kl;
            if (!eq || !eq[1])
                continue;
            kl = (size_t)(eq - e);
            if (kl == 0 || kl >= MAX_PATH)
                continue;
            wmemcpy(c->rules[c->nrules].list, e, kl);
            c->rules[c->nrules].list[kl] = 0;
            wcsncpy(c->rules[c->nrules].profile, eq + 1, NAME_CAP - 1);
            c->nrules++;
        }
    }

    if (c->dns_mode < DNS_OFF || c->dns_mode > DNS_SYSTEM)
        c->dns_mode = DNS_OFF;
    if (c->dns_method != DNS_SYSTEM)
        c->dns_method = DNS_REDIRECT;
    if (c->dns_mode != DNS_OFF)
        c->dns_method = c->dns_mode;
    if (c->dns_preset < 0 || c->dns_preset >= g_ndns_presets)
        c->dns_preset = 0;

    names[0] = names[1] = 0;
    GetPrivateProfileSectionNamesW(names, SECT_CAP, path);
    for (n = names; *n && c->nprofiles < MAX_PROFILES; n += wcslen(n) + 1) {
        Profile *p;

        if (_wcsnicmp(n, L"Profile.", 8) != 0 || !n[8])
            continue;
        p = &c->profiles[c->nprofiles++];
        read_profile(path, n, sect, p);
    }

    if (c->nprofiles == 0) {
        memcpy(c->profiles, defp, sizeof(c->profiles));
        c->nprofiles = defn;
    }
    /* eski sürümün Discord'a özel varsayılan adlarını genel adlara taşı */
    {
        static const wchar_t *ren[][2] = {
            { L"TTL 5 (Discord)", L"TTL 5" }, { L"Superbox (Discord)", L"Superbox" },
        };
        size_t k;

        for (k = 0; k < sizeof(ren) / sizeof(ren[0]); k++) {
            int i = config_find(c, ren[k][0]);
            if (i >= 0 && config_find(c, ren[k][1]) < 0) {
                wcscpy(c->profiles[i].name, ren[k][1]);
                if (!_wcsicmp(c->active, ren[k][0]))
                    wcscpy(c->active, ren[k][1]);
            }
        }
    }
    if (c->seed < SEED_VERSION) {
        seed_zapret(c);
        c->seed = SEED_VERSION;
    }
    /* Ayarlardan kaldırılan, her zaman açık olan davranışlar */
    c->auto_restart = c->kill_others = c->log_file = 1;
    if (!c->app_repo[0])
        wcscpy(c->app_repo, APP_REPO_DEFAULT);
    if (config_find(c, c->active) < 0)
        wcscpy(c->active, c->profiles[0].name);

out:
    free(names);
    free(sect);
    free(defp);
}

/* --- yazma: tüm dosyayı bellekte üretip tek seferde UTF-16LE olarak yaz --- */

typedef struct { wchar_t *buf; size_t len, cap; } SB;

static BOOL sb_grow(SB *b)
{
    size_t nc = b->cap ? b->cap * 2 : 8192;
    wchar_t *nb = realloc(b->buf, nc * sizeof(wchar_t));

    if (!nb)
        return FALSE;
    b->buf = nb;
    b->cap = nc;
    return TRUE;
}

static void sb_add(SB *b, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;

    for (;;) {
        if (b->cap - b->len < 1024 && !sb_grow(b))
            return;
        va_start(ap, fmt);
        n = _vsnwprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < b->cap - b->len) {
            b->len += n;
            return;
        }
        if (!sb_grow(b))
            return;
    }
}

static void write_profile(SB *b, const Profile *p)
{
    int j;

    sb_add(b, L"[Profile.%s]\r\npreset=%d\r\n", p->name, p->preset);
    if (p->managed)
        sb_add(b, L"hazir=1\r\n");
    if (p->engine == ENGINE_ZAPRET) {
        const wchar_t *s;

        sb_add(b, L"engine=%d\r\nzapret=", p->engine);
        for (s = p->zapret; *s; s++) {
            if (*s == L'\r')
                continue;
            if (*s == L'\n')
                sb_add(b, L"\\n");
            else
                sb_add(b, L"%c", *s);
        }
        sb_add(b, L"\r\n");
    }
    for (j = 0; j < g_nfields; j++) {
        const Field *fl = &g_fields[j];

        if (fl->type == F_BOOL) {
            if (*FPTR_INT(p, fl))
                sb_add(b, L"%s=1\r\n", fl->key);
        } else if (fl->type == F_OPT) {
            if (*FPTR_INT(p, fl))
                sb_add(b, L"%s=1\r\n", fl->key);
            if (FPTR_STR2(p, fl)[0])
                sb_add(b, L"%s_value=%s\r\n", fl->key, FPTR_STR2(p, fl));
        } else if (FPTR_STR(p, fl)[0]) {
            sb_add(b, L"%s=%s\r\n", fl->key, FPTR_STR(p, fl));
        }
    }
    sb_add(b, L"\r\n");
}

BOOL config_save(const Config *c)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    SB b = {0};
    HANDLE f;
    DWORD wr;
    BOOL ok = FALSE;
    int i;

    sb_add(&b, L"\xFEFF; %s ayarları - Ayarlar penceresinden düzenleyebilirsiniz.\r\n\r\n", APP_NAME);
    sb_add(&b, L"[General]\r\nStartBypass=%d\r\nAutoRestart=%d\r\nNotify=%d\r\nIgnoreService=%d\r\nKillOthers=%d\r\n"
               L"Hotkey=%d\r\nAppMode=%d\r\nLang=%d\r\nTheme=%d\r\nSeed=%d\r\nLogFile=%d\r\nTestHost=%s\r\nActiveProfile=%s\r\n\r\n",
           c->start_bypass, c->auto_restart, c->notify, c->ignore_service, c->kill_others,
           c->hotkey, c->app_mode, c->lang, c->theme, c->seed, c->log_file, c->test_host, c->active);
    sb_add(&b, L"; Mode: 0=kapalı 1=uygulama üzerinden 2=Windows DNS ayarı\r\n"
               L"[DNS]\r\nMode=%d\r\nMethod=%d\r\nPreset=%d\r\nDoH=%d\r\nCustomV4=%s\r\nCustomV6=%s\r\nCustomPort=%s\r\nCustomDoH=%s\r\n"
               L"Failover=%d\r\n\r\n",
           c->dns_mode, c->dns_method, c->dns_preset, c->dns_doh, c->dns_v4, c->dns_v6, c->dns_port, c->dns_doh_tpl,
           c->dns_failover);
    sb_add(&b, L"[Block]\r\nOn=%d\r\nLists=%s\r\n\r\n", c->block_on, c->block_lists);
    sb_add(&b, L"; From/To: gece yarısından beri dakika. Days: 0 her gün, 1 hafta içi, 2 hafta sonu\r\n"
               L"[Auto]\r\nHeal=%d\r\nTriggers=%d\r\nTriggerApps=%s\r\nSchedule=%d\r\nFrom=%d\r\nTo=%d\r\nDays=%d\r\nGame=%d\r\nShare=%d\r\n"
               L"Recent=%s|%s|%s\r\n\r\n",
           c->auto_heal, c->trig_on, c->trig_apps, c->sched_on, c->sched_from, c->sched_to, c->sched_days,
           c->game_mode, c->share_proxy, c->recent[0], c->recent[1], c->recent[2]);
    /* liste dosyası=profil */
    sb_add(&b, L"[Rules]\r\n");
    for (i = 0; i < c->nrules; i++)
        sb_add(&b, L"%s=%s\r\n", c->rules[i].list, c->rules[i].profile);
    sb_add(&b, L"\r\n");
    sb_add(&b, L"[Updates]\r\nCheck=%d\r\nSeenTag=%s\r\nNotesVersion=%s\r\nAppRepo=%s\r\n\r\n", c->update_check, c->seen_tag, c->notes_version, c->app_repo);
    /* ağ geçidi MAC=profil|ağ adı */
    sb_add(&b, L"[Networks]\r\nAuto=%d\r\n", c->net_auto);
    for (i = 0; i < c->nnets; i++)
        sb_add(&b, L"%s=%s|%s\r\n", c->nets[i].key, c->nets[i].profile, c->nets[i].name);
    sb_add(&b, L"\r\n");

    for (i = 0; i < c->nprofiles; i++)
        write_profile(&b, &c->profiles[i]);
    if (!b.buf)
        return FALSE;

    ini_path(path);
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", path);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        ok = WriteFile(f, b.buf, (DWORD)(b.len * sizeof(wchar_t)), &wr, NULL);
        CloseHandle(f);
        if (ok)
            ok = MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
    }
    free(b.buf);
    return ok;
}

/* Profilleri INI metni olarak verir (idx < 0: hepsi). free() ile bırakın. */
wchar_t *profiles_export(const Config *c, int idx)
{
    SB b = {0};
    int i;

    sb_add(&b, L"; %s profilleri\r\n\r\n", APP_NAME);
    for (i = 0; i < c->nprofiles; i++)
        if (idx < 0 || i == idx)
            write_profile(&b, &c->profiles[i]);
    return b.buf;
}

/* Metindeki [Profile.*] bölümlerini ekler; aynı adlıları "ad 2" diye yeniden
   adlandırır. Eklenen profil sayısını döndürür. */
/* Metni okunabilir bir UTF-16 INI dosyasına yazar (GetPrivateProfile* için) */
static BOOL temp_ini(const wchar_t *text, wchar_t *tmp)
{
    static const wchar_t bom = 0xFEFF;
    HANDLE f;
    DWORD wr;

    GetTempPathW(MAX_PATH, tmp);
    wcsncat(tmp, L"tunneldpi-import.ini", MAX_PATH - wcslen(tmp) - 1);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(f, &bom, sizeof(bom), &wr, NULL);
    WriteFile(f, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(f);
    return TRUE;
}

int profiles_merge_presets(Config *c, const wchar_t *text)
{
    wchar_t tmp[MAX_PATH], *names, *sect, *n;
    static Profile np;
    int changed = 0, i;

    if (!text || !wcsstr(text, L"[Profile.") || !temp_ini(text, tmp))
        return 0;
    names = malloc(SECT_CAP * sizeof(wchar_t));
    sect = malloc(SECT_CAP * sizeof(wchar_t));
    if (names && sect) {
        names[0] = names[1] = 0;
        GetPrivateProfileSectionNamesW(names, SECT_CAP, tmp);
        for (n = names; *n; n += wcslen(n) + 1) {
            if (_wcsnicmp(n, L"Profile.", 8) != 0 || !n[8])
                continue;
            read_profile(tmp, n, sect, &np);
            np.managed = 1;
            i = config_find(c, np.name);
            if (i >= 0) {
                if (!c->profiles[i].managed)
                    continue;              /* aynı adlı kendi profilin: dokunma */
                if (memcmp(&c->profiles[i], &np, sizeof(np))) {
                    c->profiles[i] = np;
                    changed++;
                }
            } else if (c->nprofiles < MAX_PROFILES) {
                c->profiles[c->nprofiles++] = np;
                changed++;
            }
        }
    }
    free(names);
    free(sect);
    DeleteFileW(tmp);
    return changed;
}

int profiles_import(Config *c, const wchar_t *text)
{
    wchar_t tmp[MAX_PATH], *names = NULL, *sect = NULL, *n;
    int added = 0;

    if (!text || !wcsstr(text, L"[Profile.") || !temp_ini(text, tmp))
        return 0;

    names = malloc(SECT_CAP * sizeof(wchar_t));
    sect = malloc(SECT_CAP * sizeof(wchar_t));
    if (names && sect) {
        names[0] = names[1] = 0;
        GetPrivateProfileSectionNamesW(names, SECT_CAP, tmp);
        for (n = names; *n && c->nprofiles < MAX_PROFILES; n += wcslen(n) + 1) {
            Profile *p;
            wchar_t base[NAME_CAP];
            int k = 2;

            if (_wcsnicmp(n, L"Profile.", 8) != 0 || !n[8])
                continue;
            p = &c->profiles[c->nprofiles];
            read_profile(tmp, n, sect, p);
            p->managed = 0;                     /* içe aktarılan profil artık senin */
            wcscpy(base, p->name);
            while (config_find(c, p->name) >= 0)
                _snwprintf(p->name, NAME_CAP, L"%.*s %d", NAME_CAP - 6, base, k++);
            c->nprofiles++;
            added++;
        }
    }
    free(names);
    free(sect);
    DeleteFileW(tmp);
    return added;
}

int config_find(const Config *c, const wchar_t *name)
{
    int i;

    for (i = 0; i < c->nprofiles; i++)
        if (_wcsicmp(c->profiles[i].name, name) == 0)
            return i;
    return -1;
}

Profile *config_active(Config *c)
{
    int i = config_find(c, c->active);

    return &c->profiles[i < 0 ? 0 : i];
}

/* --- komut satırı üretimi --- */

static void add_arg(wchar_t *out, size_t cap, const wchar_t *a)
{
    size_t len = wcslen(out);
    BOOL q = a[0] == 0 || wcspbrk(a, L" \t") != NULL;

    _snwprintf(out + len, cap - len, q ? L"%s\"%s\"" : L"%s%s", len ? L" " : L"", a);
    out[cap - 1] = 0;
}

static void add_arg2(wchar_t *out, size_t cap, const wchar_t *flag, const wchar_t *val)
{
    add_arg(out, cap, flag);
    add_arg(out, cap, val);
}

const wchar_t *engine_exe(const Config *c, const Profile *p)
{
    (void)c;
    return p->engine == ENGINE_ZAPRET ? ZAPRET_EXE : CLASSIC_EXE;
}

const wchar_t *engine_name(const Profile *p)
{
    return p->engine == ENGINE_ZAPRET ? L"winws.exe" : L"tunneldpi-engine.exe";
}

/* zapret listesi: winws zapret\ klasöründe çalıştığından göreli yollar ../ ile */
static void zapret_list_arg(wchar_t *out, size_t cap, const wchar_t *flag, const wchar_t *file)
{
    wchar_t tmp[MAX_PATH + 32], *q;

    if (wcschr(file, L':'))
        _snwprintf(tmp, MAX_PATH + 32, L"%s=%s", flag, file);
    else
        _snwprintf(tmp, MAX_PATH + 32, L"%s=../%s", flag, file);
    tmp[MAX_PATH + 31] = 0;
    for (q = tmp; *q; q++)
        if (*q == L'\\')
            *q = L'/';
    add_arg(out, cap, tmp);
}

static void build_zapret(const Profile *p, wchar_t *out, size_t cap)
{
    const wchar_t *s = p->zapret, *l;
    wchar_t tok[MAX_PATH];

    out[0] = 0;
    while (next_token(&s, tok, MAX_PATH, L" \t\r\n")) {
        if (wcscmp(tok, L"%HOSTLIST%")) {
            add_arg(out, cap, tok);
            continue;
        }
        l = p->blacklist;
        while (next_token(&l, tok, MAX_PATH, L";\r\n"))
            zapret_list_arg(out, cap, L"--hostlist", tok);
        if (p->autolist)
            zapret_list_arg(out, cap, L"--hostlist-auto", AUTOLIST_FILE);
    }
}

static BOOL g_game_on;

void config_set_game(const Config *c, const wchar_t *exe)
{
    wchar_t path[MAX_PATH], *t = NULL, *buf;
    size_t n;

    g_game_on = exe && exe[0];
    if (!g_game_on)
        return;
    if (c->app_mode == APPS_EXCEPT) {
        app_path(path, MAX_PATH, APPS_FILE);
        t = read_text_file(path);
    }
    n = (t ? wcslen(t) : 0) + wcslen(exe) + 8;
    buf = malloc(n * sizeof(wchar_t));
    if (!buf) {
        free(t);
        g_game_on = FALSE;
        return;
    }
    _snwprintf(buf, n, L"%s%s%s\r\n", t ? t : L"", t && t[0] && t[wcslen(t) - 1] != L'\n' ? L"\r\n" : L"", exe);
    buf[n - 1] = 0;
    app_path(path, MAX_PATH, GAME_FILE);
    if (!write_text_file(path, buf))
        g_game_on = FALSE;
    free(buf);
    free(t);
}

/* extras: DNS yönlendirmesi ve reklam engeli yalnızca ana motorda çalışır */
static void build_args_ex(const Config *c, const Profile *p, wchar_t *out, size_t cap, BOOL extras)
{
    wchar_t tok[MAX_PATH], tmp[BIG_CAP + 64];
    int i;

    if (p->engine == ENGINE_ZAPRET) {
        build_zapret(p, out, cap);
        return;
    }
    out[0] = 0;
    if (p->preset >= 1 && p->preset <= 9) {
        _snwprintf(tmp, 8, L"-%d", p->preset);
        add_arg(out, cap, tmp);
    }
    for (i = 0; i < g_nfields; i++) {
        const Field *f = &g_fields[i];
        const wchar_t *s;

        switch (f->type) {
        case F_BOOL:
            if (!*FPTR_INT(p, f))
                break;
            if (f->off == offsetof(Profile, autolist)) {
                /* öğrenilen liste motoru liste moduna sokar: listesiz
                   profilde koruma yalnızca engellendiği anlaşılan sitelere uygulanır */
                app_path(tmp, MAX_PATH, AUTOLIST_FILE);
                add_arg2(out, cap, f->flag, tmp);
            } else {
                add_arg(out, cap, f->flag);
            }
            break;
        case F_STR:
        case F_NUM:
            s = FPTR_STR(p, f);
            if (s[0]) {
                wcsncpy(tmp, s, BIG_CAP);
                tmp[BIG_CAP] = 0;
                trim_ws(tmp);
                if (tmp[0])
                    add_arg2(out, cap, f->flag, tmp);
            }
            break;
        case F_MULTI:
            s = FPTR_STR(p, f);
            while (next_token(&s, tok, MAX_PATH, f->off == offsetof(Profile, blacklist) ? L";\r\n" : L",; \r\n")) {
                if (f->off == offsetof(Profile, blacklist)) {
                    app_path(tmp, MAX_PATH, tok);
                    add_arg2(out, cap, f->flag, tmp);
                } else {
                    add_arg2(out, cap, f->flag, tok);
                }
            }
            break;
        case F_OPT:
            if (*FPTR_INT(p, f)) {
                wcsncpy(tok, FPTR_STR2(p, f), MAX_PATH - 1);
                tok[MAX_PATH - 1] = 0;
                trim_ws(tok);
                if (tok[0])
                    _snwprintf(tmp, BIG_CAP + 64, L"%s=%s", f->flag, tok);
                else
                    wcscpy(tmp, f->flag);
                add_arg(out, cap, tmp);
            }
            break;
        case F_RAW:
            s = FPTR_STR(p, f);
            if (s[0]) {
                size_t len = wcslen(out);
                _snwprintf(out + len, cap - len, L"%s%s", len ? L" " : L"", s);
                out[cap - 1] = 0;
            }
            break;
        }
    }
    if (extras && c->dns_mode == DNS_REDIRECT)
        dns_engine_args(c, out, cap);

    /* Uygulama filtresi: liste boşsa motor başlamaz, o yüzden yalnızca doluysa ekle.
       Oyun modunda oyun (ve "hariç" listesi) ayrı bir dosyadan dışarıda bırakılır. */
    if (g_game_on && (c->app_mode != APPS_ONLY || apps_count() == 0)) {
        app_path(tmp, MAX_PATH, GAME_FILE);
        add_arg2(out, cap, L"--process-list", tmp);
        add_arg(out, cap, L"--process-invert");
    } else if (c->app_mode != APPS_ALL && apps_count() > 0) {
        app_path(tmp, MAX_PATH, APPS_FILE);
        add_arg2(out, cap, L"--process-list", tmp);
        if (c->app_mode == APPS_EXCEPT)
            add_arg(out, cap, L"--process-invert");
    }
    if (extras)
        block_args(c, p, out, cap);
}

void build_args(const Config *c, const Profile *p, wchar_t *out, size_t cap)
{
    build_args_ex(c, p, out, cap, TRUE);
}

/* ---------------- site kuralları ---------------- */

int rule_find(const Config *c, const wchar_t *list)
{
    int i;

    for (i = 0; i < c->nrules; i++)
        if (!_wcsicmp(c->rules[i].list, list))
            return i;
    return -1;
}

void rule_set(Config *c, const wchar_t *list, const wchar_t *profile)
{
    int i = rule_find(c, list);

    if (!profile || !profile[0]) {
        if (i >= 0) {
            memmove(&c->rules[i], &c->rules[i + 1], (size_t)(c->nrules - i - 1) * sizeof(c->rules[0]));
            c->nrules--;
        }
        return;
    }
    if (i < 0) {
        if (c->nrules >= MAX_RULES)
            return;
        i = c->nrules++;
        wcsncpy(c->rules[i].list, list, MAX_PATH - 1);
    }
    wcsncpy(c->rules[i].profile, profile, NAME_CAP - 1);
}

int rules_usable(const Config *c, const Profile *base, int *idx, int max)
{
    wchar_t path[MAX_PATH];
    int i, n = 0, pi;

    if (base->engine != ENGINE_CLASSIC)
        return 0;
    for (i = 0; i < c->nrules && n < max; i++) {
        pi = config_find(c, c->rules[i].profile);
        if (pi < 0 || c->profiles[pi].engine != ENGINE_CLASSIC || !_wcsicmp(c->profiles[pi].name, base->name))
            continue;
        app_path(path, MAX_PATH, c->rules[i].list);
        if (!file_exists(path))
            continue;
        idx[n++] = i;
    }
    return n;
}

void build_rule_args(const Config *c, const Profile *p, const wchar_t *list, int prio, wchar_t *out, size_t cap)
{
    static Profile tmp;
    wchar_t pr[16];

    tmp = *p;
    wcsncpy(tmp.blacklist, list, BIG_CAP - 1);
    tmp.autolist = 0;
    build_args_ex(c, &tmp, out, cap, FALSE);
    _snwprintf(pr, 16, L"%d", prio);
    add_arg2(out, cap, L"--priority", pr);
}

void recent_push(Config *c, const wchar_t *name)
{
    int i, k;

    if (!name[0] || !_wcsicmp(c->recent[0], name))
        return;
    for (k = 1; k < MAX_RECENT - 1; k++)
        if (!_wcsicmp(c->recent[k], name))
            break;
    for (i = k; i > 0; i--)
        wcscpy(c->recent[i], c->recent[i - 1]);
    wcsncpy(c->recent[0], name, NAME_CAP - 1);
}

/* apps.txt'deki program adı sayısı */
int apps_count(void)
{
    wchar_t path[MAX_PATH], line[MAX_PATH], *t;
    const wchar_t *s;
    int n = 0;

    app_path(path, MAX_PATH, APPS_FILE);
    t = read_text_file(path);
    if (!t)
        return 0;
    s = t;
    while (next_token(&s, line, MAX_PATH, L"\r\n"))
        if (line[0] != L'#')
            n++;
    free(t);
    return n;
}
