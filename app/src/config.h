/*
 * TunnelDPI - ayarlar ve profiller
 */
#pragma once
#include "app.h"
#include <stddef.h>

#define MAX_PROFILES  24
#define MAX_NETS      16
#define MAX_RULES     8
#define MAX_RECENT    3
#define NAME_CAP      64
#define SMALL_CAP     32
#define MID_CAP       128
#define BIG_CAP       512
#define ZAPRET_CAP    2048

/* Motorlar */
enum { ENGINE_CLASSIC = 0, ENGINE_ZAPRET = 1 };
#define CLASSIC_EXE   L"engine\\tunneldpi-engine.exe"   /* GoodbyeDPI tabanlı motor */
#define ZAPRET_EXE    L"zapret\\winws.exe"
#define SEED_VERSION  1

typedef struct {
    wchar_t name[NAME_CAP];
    int     engine;                 /* ENGINE_CLASSIC / ENGINE_ZAPRET */
    int     preset;                 /* 0 = yok, 1..9 = -1..-9 */
    wchar_t zapret[ZAPRET_CAP];     /* winws argümanları; %HOSTLIST% listelerle değişir */

    int p, q, r, s, a, m, w, n;
    int wrong_chksum, wrong_seq, native_frag, reverse_frag;
    int allow_no_sni, frag_by_sni, dns_verb;
    int md5sig, autolist;
    int managed;                    /* hazır ayar dosyasından: güncellemeyle değişebilir */

    wchar_t f[SMALL_CAP], k[SMALL_CAP], e[SMALL_CAP];
    wchar_t set_ttl[SMALL_CAP], min_ttl[SMALL_CAP];
    wchar_t fake_gen[SMALL_CAP], fake_resend[SMALL_CAP];
    int     auto_ttl;    wchar_t auto_ttl_v[SMALL_CAP];
    int     max_payload; wchar_t max_payload_v[SMALL_CAP];

    wchar_t port[MID_CAP], ipid[MID_CAP];
    wchar_t split_pos[MID_CAP], seqovl[SMALL_CAP];
    wchar_t udp_fake[SMALL_CAP], udp_ports[MID_CAP];
    wchar_t fake_sni[MID_CAP * 2];
    wchar_t fake_hex[BIG_CAP];
    wchar_t blacklist[BIG_CAP];
    wchar_t extra[BIG_CAP];
} Profile;

/* Profil alan tipleri */
enum {
    F_BOOL,   /* var/yok bayrak            : -p */
    F_STR,    /* tek değer                 : --set-ttl 5 */
    F_NUM,    /* tek sayısal değer         : -e 40 */
    F_MULTI,  /* virgül/; ile çoklu değer  : --blacklist a --blacklist b */
    F_OPT,    /* bayrak + isteğe bağlı değer: --auto-ttl[=1-4-10] */
    F_RAW     /* ham argümanlar, olduğu gibi eklenir */
};

typedef struct {
    const wchar_t *key;    /* INI anahtarı */
    const wchar_t *flag;   /* komut satırı bayrağı */
    int            type;
    size_t         off;    /* Profile içindeki int (BOOL/OPT) ya da wchar_t[] (STR...) */
    size_t         off2;   /* F_OPT: değer dizisi */
    int            cap;    /* dizi kapasitesi */
    const wchar_t *label;  /* arayüz etiketi */
} Field;

extern const Field g_fields[];
extern const int   g_nfields;
extern const wchar_t *g_preset_names[10];

enum { DNS_OFF = 0, DNS_REDIRECT = 1, DNS_SYSTEM = 2 };
enum { APPS_ALL = 0, APPS_ONLY = 1, APPS_EXCEPT = 2 };
#define APPS_FILE L"apps.txt"
#define LOG_FILE  L"tunneldpi.log"
#define AUTOLIST_NAME L"otomatik-onayli.txt"
#define AUTOLIST_FILE L"lists\\otomatik-onayli.txt"
#define AUTO_CANDIDATE_NAME L"otomatik-onayli.txt.candidates.txt"
#define UDP_DISCORD L"50000-65535"
#define UDP_GAMES   L"1024-65535"

typedef struct {
    /* [General] */
    int     start_bypass;     /* uygulama açılınca bypass'ı başlat */
    int     auto_restart;     /* motor kapanırsa yeniden başlat */
    int     notify;           /* balon bildirimleri */
    int     ignore_service;   /* eski servis uyarısını gösterme */
    int     kill_others;      /* çakışan motor süreçlerini kapat */
    int     app_mode;         /* APPS_ALL / APPS_ONLY / APPS_EXCEPT */
    int     hotkey;           /* Ctrl+Alt+G ile aç/kapat */
    int     lang;             /* 0 otomatik, 1 Türkçe, 2 English */
    int     theme;            /* 0 sistem, 1 açık, 2 koyu */
    int     seed;             /* eklenmiş hazır profil sürümü */
    int     log_file;         /* günlüğü dosyaya da yaz */
    wchar_t test_host[MID_CAP]; /* bağlantı testi sitesi; boş = profilin ilk listesi */
    wchar_t active[NAME_CAP];

    /* [DNS] */
    int     dns_mode;         /* DNS_OFF ya da etkin yöntem */
    int     dns_method;       /* son seçilen yöntem (DNS_REDIRECT / DNS_SYSTEM) */
    int     dns_preset;
    int     dns_doh;
    wchar_t dns_v4[MID_CAP];
    wchar_t dns_v6[MID_CAP];
    wchar_t dns_port[SMALL_CAP];
    wchar_t dns_doh_tpl[MID_CAP * 2];

    /* [Block] reklam ve izleyici engelleme (tüm profillerde) */
    int     block_on;
    wchar_t block_lists[BIG_CAP];   /* lists\engel\ içindeki hazır listeler, ';' ile */

    /* [Updates] */
    int     update_check;     /* günlük sürüm denetimi */
    wchar_t seen_tag[64];     /* bildirilen son GoodbyeDPI-Turkey sürümü */
    wchar_t notes_version[64]; /* gösterilen yerel sürüm özeti */
    wchar_t app_repo[128];    /* uygulamanın GitHub deposu (sahip/ad), boş = denetleme */

    /* [Networks] ağa göre profil */
    int     net_auto;
    int     nnets;
    struct {
        wchar_t key[24];          /* ağ geçidi MAC */
        wchar_t name[64];         /* SSID / adaptör adı */
        wchar_t profile[NAME_CAP];
    } nets[MAX_NETS];

    /* [Rules] site kuralları: bu listedeki sitelerde başka bir profil (klasik motor) */
    int     nrules;
    struct {
        wchar_t list[MAX_PATH];   /* lists\x.txt */
        wchar_t profile[NAME_CAP];
    } rules[MAX_RULES];

    /* [Auto] kendiliğinden yapılanlar */
    int     auto_heal;        /* siteler açılmaz olunca çalışan profili kendisi bul */
    int     dns_failover;     /* seçili DNS yanıt vermezse yedeğe geç */
    int     trig_on;          /* trig_apps'teki bir program açılınca korumayı aç */
    wchar_t trig_apps[BIG_CAP];   /* "Discord.exe, oyun.exe" */
    int     sched_on;         /* sched_from..sched_to arası korumayı kapat */
    int     sched_from, sched_to; /* gece yarısından beri dakika */
    int     sched_days;       /* 0 her gün, 1 hafta içi, 2 hafta sonu */
    int     game_mode;        /* tam ekran oyunun bağlantısına dokunma */
    int     share_proxy;      /* erişim noktasındaki cihazlara vekil aç */
    wchar_t recent[MAX_RECENT][NAME_CAP];   /* son kullanılan profiller, yeniden eskiye */

    int     nprofiles;
    Profile profiles[MAX_PROFILES];
} Config;

extern Config g_cfg;

void     config_defaults(Config *c);
void     config_load(Config *c);
BOOL     config_save(const Config *c);
Profile *config_active(Config *c);
int      config_find(const Config *c, const wchar_t *name);
void     profile_init(Profile *p, const wchar_t *name);

/* Profil -> argüman dizisi (exe hariç). out boyutu cap. DNS argümanları dahil. */
int      apps_count(void);
wchar_t *profiles_export(const Config *c, int idx);   /* idx < 0: hepsi; free() */
int      profiles_import(Config *c, const wchar_t *text);
/* Hazır ayar profilleri: aynı adlı hazır profili günceller, yoksa ekler;
   kullanıcının kendi profiline dokunmaz. Dönüş: eklenen + değişen sayısı. */
int      profiles_merge_presets(Config *c, const wchar_t *text);
void     build_args(const Config *c, const Profile *p, wchar_t *out, size_t cap);
const wchar_t *engine_exe(const Config *c, const Profile *p);   /* uygulama klasörüne göre */
const wchar_t *engine_name(const Profile *p);
void     config_zapret_default(Profile *p);

/* Site kuralları: etkin profille birlikte çalışabilenlerin indisleri (klasik
   motor, listesi var, profili bulunuyor ve etkin profil değil). */
int      rules_usable(const Config *c, const Profile *base, int *idx, int max);
int      rule_find(const Config *c, const wchar_t *list);        /* -1 yok */
void     rule_set(Config *c, const wchar_t *list, const wchar_t *profile);  /* profile NULL/boş: kaldır */
/* Kural için motor komutu: profilin yöntemi, yalnız kuralın listesi, sıra önceliği */
void     build_rule_args(const Config *c, const Profile *p, const wchar_t *list, int prio, wchar_t *out, size_t cap);
void     recent_push(Config *c, const wchar_t *name);
/* Oyun modu: bu programın bağlantıları korumanın dışında kalır (NULL: kapat) */
void     config_set_game(const Config *c, const wchar_t *exe);
#define  GAME_FILE L"oyun-modu.txt"
