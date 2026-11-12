/*
 * TunnelDPI - giriş noktası, tray ikonu ve menü
 */
#include "app.h"
#include "i18n.h"
#include "config.h"
#include "dns.h"
#include "engine.h"
#include "install.h"
#include "legacy.h"
#include "icons.h"
#include "settings.h"
#include "system.h"
#include "ui.h"
#include "flyout.h"
#include "nettest.h"
#include "captive.h"
#include "presets.h"
#include "share.h"
#include "guide.h"
#include "release.h"
#include "support.h"
#include "notify.h"
#include "diag.h"
#include "subs.h"
#include "update.h"
#include "netid.h"
#include "health.h"
#include "popup.h"
#include "browsers.h"
#include "backup.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>

HINSTANCE g_inst;
HWND      g_hwnd;
wchar_t   g_appdir[MAX_PATH];

static UINT  WM_TASKBARCREATED;
static int   g_state = ICON_OFF;
static BOOL  g_on;              /* kullanıcı bypass'ın açık olmasını istiyor */
static BOOL  g_autostart;       /* zamanlanmış görev var mı (önbellek) */
static int   g_restart_tries;
static wchar_t g_last_error[256];

enum {
    TIMER_RESTART = 1,
    TIMER_NET,
    TIMER_TRIM,
    TIMER_TEST,
    TIMER_TUNE,
    TIMER_SUBS,
    TIMER_UPDATE,
    TIMER_PAUSE,
    TIMER_HEALTH,   /* izlenen siteleri denetle */
    TIMER_AUTO,     /* tetikleyici programlar ve zamanlama */
    TIMER_CAPTIVE,  /* giriş sayfalı ağda internetin açılmasını bekle */
    TIMER_RESUME,   /* uykudan uyanınca korumayı yenile */
};

#define CAPTIVE_POLL    15000
#define CAPTIVE_GIVEUP  (15 * 60 * 1000)
#define NET_FRESH       (10 * 60 * 1000)   /* ağ değiştikten sonra denetimler sıklaşır */

#define HEALTH_PERIOD   (5 * 60 * 1000)
#define HEAL_COOLDOWN   (30 * 60 * 1000)

/* Kendiliğinden yapılanların durumu */
static int   g_heal_fails, g_dns_fails;
static DWORD g_heal_last;          /* son otomatik onarım (GetTickCount), 0 = hiç */
static DWORD g_fail_tick, g_dns_tick; /* son sayılan başarısız denetim */
static BOOL  g_failover;           /* DNS ölçümü yedek sunucu seçmek için çalışıyor */
static BOOL  g_trig_prev, g_trig_started;   /* tetikleyici program açıktı / korumayı o açtı */
static BOOL  g_sched_prev, g_sched_off;     /* zamanlama aralığındaydık / korumayı o kapattı */
static wchar_t g_game[MAX_PATH];            /* oyun modunda dışarıda bırakılan program */
static int   g_game_idle;                   /* oyun ön planda değilken geçen tur */

static ULONGLONG g_pause_until;   /* duraklatma bitişi (GetTickCount64), 0 = yok */

/* Giriş sayfası: bağlantı testi düşerse koruma kısa süre kapatılıp ağa bakılır */
static BOOL    g_open_check;      /* koruma açıldıktan sonraki ilk test */
static BOOL    g_captive_probe;   /* koruma denetim için kapatıldı, sonuç bekleniyor */
static BOOL    g_captive;         /* ağ giriş istiyor; koruma girişe kadar beklemede */
static DWORD   g_captive_since, g_captive_last;
static wchar_t g_captive_key[24]; /* son bakılan ağ: aynı ağda döngüye girme */
static DWORD   g_net_changed;     /* son ağ değişikliği (GetTickCount), 0 = yok */
static BOOL    g_probe_net;       /* denetimi ağ değişikliği sonrası test başlattı */

#define HOTKEY_TOGGLE 1

/* ---------------- log & bildirim ---------------- */

void app_log(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    SYSTEMTIME st;
    va_list ap;
    int n;

    GetLocalTime(&st);
    n = _snwprintf(buf, 1024, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_start(ap, fmt);
    _vsnwprintf(buf + n, 1024 - n - 2, fmt, ap);
    va_end(ap);
    buf[1021] = 0;
    wcscat(buf, L"\n");
    log_append(buf, wcslen(buf));
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_ENGINE_LOG, 0, 0);
}

void app_notify(const wchar_t *title, const wchar_t *text, BOOL error)
{
    if (!g_cfg.notify && !error)
        return;
    notify_show(title, text, error);
}

/* ---------------- tray ikonu ---------------- */

const wchar_t *app_state_text(void)
{
    switch (g_state) {
    case ICON_ON:    return tr(L"Açık");
    case ICON_ERROR: return tr(L"Hata");
    case ICON_BUSY:  return tr(L"Başlatılıyor");
    default:         return tr(L"Kapalı");
    }
}

static void dns_text(wchar_t *out, size_t cap)
{
    if (g_cfg.dns_mode == DNS_OFF)
        _snwprintf(out, cap, tr(L"DNS: varsayılan"));
    else
        _snwprintf(out, cap, tr(L"DNS: %s (%s)"), tr(g_dns_presets[g_cfg.dns_preset].name),
                   g_cfg.dns_mode == DNS_REDIRECT ? tr(L"uygulama üzerinden") : tr(L"Windows ayarı"));
    out[cap - 1] = 0;
}

static void tray_update(DWORD msg)
{
    NOTIFYICONDATAW nid = { sizeof(nid) };
    wchar_t d[96];

    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon_get(g_state);
    dns_text(d, 96);
    _snwprintf(nid.szTip, 128, L"%s: %s\n%s\n%s", APP_NAME, app_state_text(),
               config_active(&g_cfg)->name, d);
    nid.szTip[127] = 0;
    if (!Shell_NotifyIconW(msg, &nid) && msg == NIM_MODIFY)
        Shell_NotifyIconW(NIM_ADD, &nid);
    if (msg == NIM_ADD) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

static void set_state(int st)
{
    g_state = st;
    tray_update(NIM_MODIFY);
    settings_on_state();
    flyout_refresh();
}

int app_state(void)
{
    return g_state;
}

const wchar_t *app_last_error(void)
{
    return g_last_error;
}


void app_refresh_state(void)
{
    tray_update(NIM_MODIFY);
}

/* ---------------- bypass ---------------- */

BOOL bypass_running(void)
{
    return g_on;
}

/* Bağlı ağın kimliği. Motorun ve DNS'in kendisi de arayüz bildirimi
   ürettiği için "ağ değişti" sayılmadan önce bununla karşılaştırılır. */
static wchar_t g_net_key[24];

static void net_key_now(wchar_t *key)
{
    wchar_t name[64];
    if (!netid_current(key, 24, name, 64))
        key[0] = 0;
}

static void bypass_on(void)
{
    wchar_t err[512];

    g_on = TRUE;
    g_last_error[0] = 0;
    KillTimer(g_hwnd, TIMER_RESTART);
    KillTimer(g_hwnd, TIMER_PAUSE);
    g_pause_until = 0;
    set_state(ICON_BUSY);

    if (g_cfg.dns_mode == DNS_SYSTEM) {
        if (!dns_system_apply(&g_cfg, err, 512)) {
            app_log(L"Sistem DNS hatası: %s", err);
            app_notify(tr(L"DNS ayarlanamadı"), err, TRUE);
        }
    }
    dns_watch(TRUE);   /* ağ değişince DNS'i yeniden uygula ve bağlantıyı ölç */
    net_key_now(g_net_key);
    if (!engine_profile_overridden() && _wcsicmp(g_cfg.recent[0], g_cfg.active)) {
        recent_push(&g_cfg, g_cfg.active);
        config_save(&g_cfg);
    }
    if (!engine_start(&g_cfg, err, 512)) {
        app_log(L"%s", err);
        wcsncpy(g_last_error, err, 255);
        set_state(ICON_ERROR);
        app_notify(tr(L"Koruma başlatılamadı"), err, TRUE);
        return;
    }
    set_state(ICON_ON);
    g_open_check = TRUE;
    SetTimer(g_hwnd, TIMER_TEST, 2500, NULL);   /* ayağa kalkınca bağlantıyı ölç */
    SetTimer(g_hwnd, TIMER_HEALTH, 20000, NULL);
}

/* Bağlantı testi düştü: ağ bir giriş sayfası istiyor olabilir. Aynı ağa 10 dakikada
   bir kez bakılır; bakarken koruma kapalıdır, yoksa DNS yönlendirmesi sonucu bozar. */
static BOOL tuning(void);
static void bypass_off(void);

static BOOL captive_suspect(void)
{
    wchar_t key[24];
    DWORD now = GetTickCount();

    net_key_now(key);
    if (g_captive || g_captive_probe || tuning() ||
        (!wcscmp(key, g_captive_key) && g_captive_last && now - g_captive_last < NET_FRESH))
        return FALSE;
    wcscpy(g_captive_key, key);
    g_captive_last = now;
    g_captive_probe = TRUE;
    app_log(L"Bağlantı yok; ağın giriş sayfası isteyip istemediğine bakılıyor.");
    bypass_off();
    captive_check();
    return TRUE;
}

static void captive_stop(void)
{
    g_captive = g_captive_probe = FALSE;
    KillTimer(g_hwnd, TIMER_CAPTIVE);
}

BOOL app_captive(void)
{
    return g_captive;
}

static void on_captive(int r)
{
    if (g_captive_probe) {
        g_captive_probe = FALSE;
        if (r == CAPTIVE_PORTAL) {
            g_captive = TRUE;
            g_captive_since = GetTickCount();
            app_log(L"Ağ giriş sayfası istiyor; giriş yapılana kadar koruma beklemede.");
            app_notify(tr(L"Bu ağ giriş istiyor"),
                       tr(L"Tarayıcıda açılan sayfadan ağa giriş yap; internet gelince koruma kendiliğinden açılır."), FALSE);
            SetTimer(g_hwnd, TIMER_CAPTIVE, CAPTIVE_POLL, NULL);
        } else {
            app_log(r == CAPTIVE_ONLINE ? L"Ağ giriş istemiyor; koruma yeniden açılıyor."
                                        : L"Ağda internet yok; koruma yeniden açılıyor.");
            bypass_on();
            g_open_check = FALSE;           /* aynı testi yeniden şüpheyle izleme */
            if (r == CAPTIVE_ONLINE && g_probe_net)
                app_notify(tr(L"Ağ değişti, bağlantı testi başarısız"),
                           tr(L"Bu ağda seçili profil çalışmıyor. \"En uygun profili bul\" ile başka bir profil dene."), TRUE);
        }
    } else if (g_captive) {
        if (r == CAPTIVE_ONLINE) {
            captive_stop();
            app_log(L"Ağa giriş yapıldı; koruma açılıyor.");
            bypass_on();
            app_notify(APP_NAME, tr(L"Ağa giriş yapıldı; koruma yeniden açıldı."), FALSE);
        } else if (GetTickCount() - g_captive_since > CAPTIVE_GIVEUP) {
            captive_stop();
            app_log(L"Giriş sayfası 15 dakikadır bekleniyor; koruma yine de açılıyor.");
            bypass_on();
        } else {
            SetTimer(g_hwnd, TIMER_CAPTIVE, CAPTIVE_POLL, NULL);
        }
    }
    settings_on_state();
    flyout_refresh();
}

static void bypass_off(void)
{
    g_on = FALSE;
    KillTimer(g_hwnd, TIMER_RESTART);
    KillTimer(g_hwnd, TIMER_NET);
    KillTimer(g_hwnd, TIMER_HEALTH);
    engine_stop();
    dns_watch(FALSE);
    dns_system_restore();
    g_restart_tries = 0;
    set_state(ICON_OFF);
}

void bypass_restart_if_running(void)
{
    if (g_on) {
        bypass_off();
        bypass_on();
    } else {
        tray_update(NIM_MODIFY);
    }
}

static void on_engine_exit(DWORD code, LONG gen)
{
    wchar_t tail[256], msg[400];
    DWORD up = engine_uptime_ms();

    if (gen != engine_generation() || !g_on)
        return; /* bilerek durduruldu */

    log_tail(tail, 256, 3);
    app_log(L"Motor beklenmedik şekilde kapandı (çıkış kodu %lu).", code);
    _snwprintf(msg, 400, tr(L"Çıkış kodu %lu.\n%s"), code, tail);
    msg[399] = 0;
    wcsncpy(g_last_error, msg, 255);
    set_state(ICON_ERROR);

    if (g_cfg.auto_restart) {
        UINT delay;

        if (up > 60000)
            g_restart_tries = 0;
        delay = 2000u << (g_restart_tries < 5 ? g_restart_tries : 5);
        if (delay > 60000)
            delay = 60000;
        if (g_restart_tries == 0)
            app_notify(tr(L"Motor kapandı, yeniden başlatılıyor"), msg, TRUE);
        g_restart_tries++;
        app_log(L"%u sn sonra yeniden denenecek.", delay / 1000);
        SetTimer(g_hwnd, TIMER_RESTART, delay, NULL);
    } else {
        app_notify(tr(L"Motor kapandı"), msg, TRUE);
    }
}

/* Çıkışa takılırsa (motor ya da DNS geri yüklemesi yanıt vermezse) süreci yine de bitir */
static DWORD WINAPI quit_watchdog(LPVOID arg)
{
    (void)arg;
    Sleep(8000);
    engine_stop();
    dns_system_restore();
    ExitProcess(0);
    return 0;
}

/* Her yerden çıkış: pencere içinden doğrudan yıkmak yerine ana döngüye bırakılır */
void app_quit(void)
{
    static BOOL quitting;
    HANDLE th;

    if (quitting)
        return;
    quitting = TRUE;
    app_log(L"Çıkılıyor.");
    th = CreateThread(NULL, 64 * 1024, quit_watchdog, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (th)
        CloseHandle(th);
    PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}

void app_toggle(void)
{
    g_trig_started = g_sched_off = FALSE;   /* elle verilen karar kendiliğinden bozulmasın */
    captive_stop();
    if (g_on)
        bypass_off();
    else
        bypass_on();
}

void app_retry(void)
{
    if (app_tune_state(NULL, NULL)) return;
    g_trig_started = g_sched_off = FALSE;
    captive_stop();
    if (g_on) bypass_off();
    bypass_on();
}

/* Korumayı belirli bir süre kapatır, süre dolunca kendiliğinden açar */
void app_pause(int minutes)
{
    wchar_t msg[128];

    if (!g_on)
        return;
    bypass_off();
    g_pause_until = GetTickCount64() + (ULONGLONG)minutes * 60000;
    SetTimer(g_hwnd, TIMER_PAUSE, (UINT)minutes * 60000, NULL);
    _snwprintf(msg, 128, tr(L"Koruma %d dakika sonra kendiliğinden açılacak."), minutes);
    app_log(L"Duraklatıldı: %s", msg);
    app_notify(tr(L"Koruma duraklatıldı"), msg, FALSE);
    settings_on_state();
    flyout_refresh();
}

/* Kalan duraklatma süresi (dakika, yukarı yuvarlanmış); 0 = duraklatılmamış */
int app_paused(void)
{
    ULONGLONG now = GetTickCount64();

    if (!g_pause_until || g_on || now >= g_pause_until)
        return 0;
    return (int)((g_pause_until - now + 59999) / 60000);
}

/* ---------------- en uygun profili bul ----------------
 * Adaylar sırayla geçici olarak çalıştırılır (ayarlara yazılmaz); her birinde
 * motor ayağa kalkınca aynı test sitesine HTTPS isteği atılır.
 * Çalışanlar arasından en hızlısı seçilir; hiçbiri çalışmazsa önceki profil
 * kalır. Hızlı aramada adaylar mevcut profillerdir; derin taramada etkin
 * profilin listeleriyle üretilmiş yöntem birleşimleri denenir ve bulunan yeni
 * profil olarak kaydedilir. */

static struct {
    BOOL     on, waiting, deep;
    BOOL     site;          /* "Açılmasını sağla": tek site için, kural olarak kaydedilir */
    BOOL     heal;          /* otomatik onarım başlattı */
    int      idx, best, n;
    DWORD    best_ms;
    Profile *cands;
    wchar_t  host[128];
} g_tune;
static TuneProgress g_tune_progress;
static DWORD g_tune_started;
static BOOL g_net_check;

void app_tune_progress(TuneProgress *out)
{
    *out = g_tune_progress;
    out->running = g_tune.on;
    out->testing = g_tune.waiting;
    if (g_tune.on)
        out->elapsed = GetTickCount() - g_tune_started;
}

static BOOL tuning(void)
{
    return g_tune.on;
}

/* "Açılmasını sağla": site listeye eklendi, yeniden başlayan motorla test bekleniyor */
static struct {
    BOOL    waiting;
    wchar_t host[128];
} g_fix;

BOOL app_tune_state(int *idx, int *total)
{
    if (idx)
        *idx = g_tune.idx;
    if (total)
        *total = g_tune.n;
    return g_tune.on;
}

const wchar_t *app_tune_name(void)
{
    if (!g_tune.on || g_tune.idx >= g_tune.n)
        return g_cfg.active;
    return g_tune.cands[g_tune.idx].name;
}

static BOOL zapret_available(void)
{
    wchar_t exe[MAX_PATH];

    app_path(exe, MAX_PATH, ZAPRET_EXE);
    return file_exists(exe);
}

static Profile *cand_add(const Profile *base, const wchar_t *name)
{
    Profile *p;

    if (g_tune.n >= 32)
        return NULL;
    p = &g_tune.cands[g_tune.n++];
    profile_init(p, name);
    wcscpy(p->blacklist, base->blacklist);
    p->autolist = base->autolist;
    return p;
}

/* Derin tarama adayları: Türkiye'de işe yarayan GoodbyeDPI birleşimleri,
   ardından zapret stratejileri */
static void tune_gen_deep(const Profile *base)
{
    static const wchar_t *ttls[] = { L"3", L"4", L"5", L"6", L"8" };
    static const struct { const wchar_t *fool, *mode; } zs[] = {
        { L"md5sig",        L"fake,multisplit" },
        { L"badseq,md5sig", L"fake,multidisorder" },
        { L"badsum",        L"fake,multisplit" },
        { L"ts",            L"fake,fakedsplit" },
    };
    wchar_t name[NAME_CAP];
    Profile *p;
    size_t k;

    for (k = 0; k < sizeof(ttls) / sizeof(ttls[0]); k++) {
        _snwprintf(name, NAME_CAP, L"-5 · TTL %s", ttls[k]);
        if ((p = cand_add(base, name))) { p->preset = 5; wcscpy(p->set_ttl, ttls[k]); }
    }
    if ((p = cand_add(base, L"-5 · auto-ttl"))) p->preset = 5;
    if ((p = cand_add(base, L"-5 · MD5"))) { p->preset = 5; p->md5sig = 1; }
    if ((p = cand_add(base, L"-5 · SNI bölme"))) { p->preset = 5; wcscpy(p->split_pos, L"1,sni"); }
    if ((p = cand_add(base, L"-5 · seqovl"))) { p->preset = 5; wcscpy(p->split_pos, L"2,midsni"); wcscpy(p->seqovl, L"1"); }
    if ((p = cand_add(base, L"-5 · TTL 5 · MD5 · SNI"))) {
        p->preset = 5; wcscpy(p->set_ttl, L"5"); p->md5sig = 1; wcscpy(p->split_pos, L"1,sni,midsni");
    }
    if ((p = cand_add(base, L"-6 · wrong-seq"))) p->preset = 6;
    if ((p = cand_add(base, L"-7 · wrong-chksum"))) p->preset = 7;
    if ((p = cand_add(base, L"-9"))) p->preset = 9;
    if ((p = cand_add(base, L"-4 · SNI bölme"))) { p->preset = 4; p->native_frag = 1; wcscpy(p->split_pos, L"1,sni,endsni"); }

    if (!zapret_available())
        return;
    for (k = 0; k < sizeof(zs) / sizeof(zs[0]); k++) {
        _snwprintf(name, NAME_CAP, L"zapret · %s · %s", zs[k].mode, zs[k].fool);
        if ((p = cand_add(base, name))) {
            Profile tmp;
            wchar_t *q;

            p->engine = ENGINE_ZAPRET;
            config_zapret_default(&tmp);
            /* son (443) bloğu bu birleşimle değiştir */
            q = wcsstr(tmp.zapret, L"--filter-tcp=443");
            if (q)
                *q = 0;
            _snwprintf(p->zapret, ZAPRET_CAP,
                       L"%s--filter-tcp=443 %%HOSTLIST%% --dpi-desync=%s --dpi-desync-split-pos=1,midsld "
                       L"--dpi-desync-repeats=6 --dpi-desync-fooling=%s --dpi-desync-fake-tls=@fake/tls_clienthello_www_google_com.bin",
                       tmp.zapret, zs[k].mode, zs[k].fool);
            p->zapret[ZAPRET_CAP - 1] = 0;
        }
    }
}

/* Siteyi profilin kural listesine ekler (yoksa kuralı oluşturur) */
static void site_rule_add(const wchar_t *host, const wchar_t *profile)
{
    wchar_t rel[MAX_PATH], tag[40];
    int i, k = 0;

    rel[0] = 0;
    for (i = 0; i < g_cfg.nrules; i++)
        if (!_wcsicmp(g_cfg.rules[i].profile, profile)) {
            wcscpy(rel, g_cfg.rules[i].list);
            break;
        }
    if (!rel[0]) {
        /* klasik motor yolları ANSI okur: dosya adı yalnız ASCII */
        for (i = 0; profile[i] && k < 30; i++) {
            wchar_t ch = profile[i];
            if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
                tag[k++] = (wchar_t)towlower(ch);
            else if (k && tag[k - 1] != L'-')
                tag[k++] = L'-';
        }
        while (k && tag[k - 1] == L'-')
            k--;
        tag[k] = 0;
        _snwprintf(rel, MAX_PATH, L"lists\\site-%s.txt", k ? tag : L"kural");
        rule_set(&g_cfg, rel, profile);
    }
    list_append(rel, host);
}

static void tune_finish(BOOL cancelled)
{
    wchar_t msg[256];

    g_tune.on = g_tune.waiting = FALSE;
    KillTimer(g_hwnd, TIMER_TUNE);
    engine_set_override(NULL);
    if (g_tune.site && !cancelled) {
        if (g_tune.best >= 0) {
            const Profile *b = &g_tune.cands[g_tune.best];
            const Profile *a = config_active(&g_cfg);

            if (!_wcsicmp(b->name, a->name)) {
                _snwprintf(msg, 256, tr(L"%s artık açılıyor."), g_tune.host);
            } else if (a->engine == ENGINE_CLASSIC && b->engine == ENGINE_CLASSIC) {
                site_rule_add(g_tune.host, b->name);
                _snwprintf(msg, 256, tr(L"%s artık \"%s\" profiliyle açılıyor; diğer siteler \"%s\" ile devam ediyor."),
                           g_tune.host, b->name, a->name);
            } else {
                wcscpy(g_cfg.active, b->name);
                if (g_cfg.net_auto)
                    app_net_remember(g_cfg.active);
                _snwprintf(msg, 256, tr(L"%s \"%s\" profiliyle açılıyor; bu profile geçildi."), g_tune.host, b->name);
            }
            msg[255] = 0;
            app_log(L"Site düzeltme: %s", msg);
            app_notify(tr(L"Site açıldı"), msg, FALSE);
        } else {
            _snwprintf(msg, 256, tr(L"Hiçbir profille %s açılamadı. Durum sayfasından \"Derin tara\"yı dene."), g_tune.host);
            msg[255] = 0;
            app_log(L"Site düzeltme: %s", msg);
            app_notify(tr(L"Site açılamadı"), msg, TRUE);
        }
    } else if (!cancelled && g_tune.best >= 0) {
        const Profile *b = &g_tune.cands[g_tune.best];

        if (g_tune.deep && g_cfg.nprofiles < MAX_PROFILES) {
            Profile *np = &g_cfg.profiles[g_cfg.nprofiles];
            int k = 2;

            *np = *b;
            _snwprintf(np->name, NAME_CAP, tr(L"Bulunan: %s"), b->name);
            while (config_find(&g_cfg, np->name) >= 0)
                _snwprintf(np->name, NAME_CAP, tr(L"Bulunan %d: %s"), k++, b->name);
            g_cfg.nprofiles++;
            wcscpy(g_cfg.active, np->name);
        } else if (!g_tune.deep) {
            wcscpy(g_cfg.active, b->name);
        }
        _snwprintf(msg, 256, tr(L"\"%s\" seçildi: %s %lu ms'de yanıt verdi."), g_cfg.active, g_tune.host, g_tune.best_ms);
        app_log(L"Profil bulucu: %s", msg);
        app_notify(tr(L"En uygun profil bulundu"), msg, FALSE);
        if (g_cfg.net_auto)
            app_net_remember(g_cfg.active);     /* bu ağda bir dahaki sefere doğrudan bu profil */
    } else {
        _snwprintf(msg, 256, cancelled ? tr(L"İptal edildi; \"%s\" profiline dönüldü.")
                                       : tr(L"Hiçbir profille %s'e ulaşılamadı; \"%s\" profiline dönüldü."),
                   cancelled ? g_cfg.active : g_tune.host, g_cfg.active);
        app_log(L"Profil bulucu: %s", msg);
        if (!cancelled)
            app_notify(tr(L"Çalışan profil bulunamadı"), msg, TRUE);
    }
    msg[255] = 0;
    g_tune_progress.running = FALSE;
    g_tune_progress.elapsed = GetTickCount() - g_tune_started;
    _snwprintf(g_tune_progress.summary, 512, tr(L"%s\nDenenen: %d/%d · Çalışan: %d"),
               msg, g_tune_progress.completed, g_tune_progress.total, g_tune_progress.passed);
    g_tune_progress.summary[511] = 0;
    g_tune.site = g_tune.heal = FALSE;
    free(g_tune.cands);
    g_tune.cands = NULL;
    g_tune.n = g_tune.idx = 0;
    config_save(&g_cfg);
    bypass_restart_if_running();
    settings_on_config();
    flyout_refresh();
}

static void tune_try(void)
{
    if (g_tune.idx >= g_tune.n) {
        tune_finish(FALSE);
        return;
    }
    engine_set_override(&g_tune.cands[g_tune.idx]);
    app_log(L"Profil bulucu %d/%d: %s", g_tune.idx + 1, g_tune.n, g_tune.cands[g_tune.idx].name);
    if (g_on)
        bypass_off();
    bypass_on();
    KillTimer(g_hwnd, TIMER_TEST);           /* olağan testi değil, sabit hedefli testi yap */
    SetTimer(g_hwnd, TIMER_TUNE, g_tune.cands[g_tune.idx].engine == ENGINE_ZAPRET ? 3500 : 2500, NULL);
    settings_on_config();
    flyout_refresh();
}

/* site: tek bir site için (host verilmeli); adaylar etkin profilin listeleriyle
   denenir ki site her adayda kapsansın. */
static int tune_begin(BOOL deep, const wchar_t *host, BOOL site)
{
    const Profile *a = config_active(&g_cfg);
    int i;

    if (g_tune.on)
        return 2;
    if (host && host[0]) {
        wcsncpy(g_tune.host, host, 127);
        g_tune.host[127] = 0;
    } else if (!nettest_target(g_tune.host, 128)) {
        return 1;
    }
    g_tune.cands = malloc(32 * sizeof(Profile));
    if (!g_tune.cands)
        return 2;
    g_tune.n = 0;
    if (deep) {
        tune_gen_deep(a);
    } else {
        BOOL zok = zapret_available();
        for (i = 0; i < g_cfg.nprofiles && g_tune.n < 32; i++)
            if (g_cfg.profiles[i].engine != ENGINE_ZAPRET || zok) {
                Profile *c = &g_tune.cands[g_tune.n++];
                *c = g_cfg.profiles[i];
                if (site) {
                    wcscpy(c->blacklist, a->blacklist);
                    c->autolist = a->autolist;
                }
            }
    }
    g_tune.site = site;
    g_tune.on = TRUE;
    g_tune.deep = deep;
    g_tune.waiting = FALSE;
    g_tune.idx = 0;
    g_tune.best = -1;
    g_tune.best_ms = (DWORD)-1;
    memset(&g_tune_progress, 0, sizeof(g_tune_progress));
    g_tune_progress.total = g_tune.n;
    g_tune_started = GetTickCount();
    app_log(deep ? L"Derin tarama başladı (%d aday); test sitesi: %s" : L"Profil bulucu başladı (%d profil); test sitesi: %s",
            g_tune.n, g_tune.host);
    tune_try();
    return 0;
}

int app_tune_start(BOOL deep)
{
    return tune_begin(deep, NULL, FALSE);
}

/* ---------------- "bu site açılmıyor" ---------------- */

/* "https://www.x.com/yol" → "www.x.com" (küçük harf). FALSE: geçerli değil */
BOOL host_normalize(const wchar_t *in, wchar_t *out, size_t cap)
{
    const wchar_t *s = in, *q;
    size_t n = 0;

    while (*s == L' ' || *s == L'\t')
        s++;
    q = wcsstr(s, L"://");
    if (q)
        s = q + 3;
    while (*s && n + 1 < cap && !wcschr(L"/:?# \t\r\n", *s))
        out[n++] = (wchar_t)towlower(*s++);
    out[n] = 0;
    while (n && out[n - 1] == L'.')
        out[--n] = 0;
    return n >= 3 && wcschr(out, L'.') && out[0] != L'.';
}

/* Satır olarak ekler; zaten varsa FALSE */
BOOL list_append(const wchar_t *rel, const wchar_t *host)
{
    wchar_t path[MAX_PATH], line[256], *t, *nt;
    const wchar_t *s;
    size_t len;
    BOOL ok;

    app_path(path, MAX_PATH, L"lists");
    CreateDirectoryW(path, NULL);
    app_path(path, MAX_PATH, rel);
    t = read_text_file(path);
    if (t) {
        s = t;
        while (next_token(&s, line, 256, L"\r\n")) {
            trim_ws(line);
            if (!_wcsicmp(line, host)) {
                free(t);
                return FALSE;
            }
        }
    }
    len = t ? wcslen(t) : 0;
    nt = malloc((len + wcslen(host) + 4) * sizeof(wchar_t));
    if (!nt) {
        free(t);
        return FALSE;
    }
    nt[0] = 0;
    if (t) {
        wcscpy(nt, t);
        if (len && nt[len - 1] != L'\n')
            wcscat(nt, L"\r\n");
    }
    wcscat(nt, host);
    wcscat(nt, L"\r\n");
    ok = write_text_file(path, nt);
    free(nt);
    free(t);
    return ok;
}

/* 0: başladı, 1: geçersiz adres, 2: başka bir arama sürüyor */
int app_site_fix(const wchar_t *input)
{
    wchar_t host[128], entry[128];
    const Profile *a = config_active(&g_cfg);
    const wchar_t *s = a->blacklist;
    wchar_t first[MAX_PATH];
    BOOL added = FALSE;

    if (!host_normalize(input, host, 128))
        return 1;
    if (g_tune.on || g_fix.waiting)
        return 2;
    /* liste girdisi alt alan adlarını da kapsar: www. olmadan yaz */
    wcscpy(entry, !wcsncmp(host, L"www.", 4) && wcschr(host + 4, L'.') ? host + 4 : host);
    if (next_token(&s, first, MAX_PATH, L";\r\n"))
        added = list_append(first, entry);
    else if (a->autolist)
        added = list_append(AUTOLIST_FILE, entry);
    if (added)
        app_log(L"Site düzeltme: %s listeye eklendi.", entry);
    wcscpy(g_fix.host, host);
    if (!g_on) {
        bypass_on();
    } else if (added) {
        bypass_restart_if_running();
    }
    /* olağan açılış testi yerine bu siteyi dene */
    KillTimer(g_hwnd, TIMER_TEST);
    g_fix.waiting = TRUE;
    SetTimer(g_hwnd, TIMER_TEST, 2500, NULL);
    settings_on_nettest();
    return 0;
}

BOOL app_site_fix_busy(void)
{
    return g_fix.waiting || (g_tune.on && g_tune.site);
}

/* ---------------- son kullanılan profiller ---------------- */

void app_use_profile(const wchar_t *name)
{
    if (config_find(&g_cfg, name) < 0 || !_wcsicmp(name, g_cfg.active))
        return;
    wcscpy(g_cfg.active, name);
    if (g_cfg.net_auto)
        app_net_remember(name);
    config_save(&g_cfg);
    bypass_restart_if_running();
    settings_on_config();
    flyout_refresh();
}

/* Simgeye orta tık: son profiller arasında hızlı geçiş */
static void recent_menu(int x, int y)
{
    PopItem it[MAX_RECENT + 4];
    RECT a = { x, y, x + 1, y + 1 };
    int n = 0, i, cmd;

    for (i = 0; i < MAX_RECENT; i++)
        if (g_cfg.recent[i][0] && config_find(&g_cfg, g_cfg.recent[i]) >= 0) {
            it[n].id = 10 + i;
            it[n].flags = !_wcsicmp(g_cfg.recent[i], g_cfg.active) ? PI_CHECK : 0;
            wcsncpy(it[n].text, g_cfg.recent[i], 95);
            it[n].text[95] = 0;
            n++;
        }
    if (n) {
        it[n].id = 0;
        it[n].flags = PI_SEP;
        it[n++].text[0] = 0;
    }
    it[n].id = 1;
    it[n].flags = 0;
    wcscpy(it[n++].text, tr(L"Tüm profiller…"));
    it[n].id = 2;
    it[n].flags = PI_DANGER;
    wcscpy(it[n++].text, tr(L"Çıkış"));
    SetForegroundWindow(g_hwnd);
    cmd = ui_popup(g_hwnd, &a, it, n, TRUE);
    if (cmd == 1)
        settings_show(SETTINGS_PAGE_PROFILES);
    else if (cmd == 2)
        app_quit();
    else if (cmd >= 10)
        app_use_profile(g_cfg.recent[cmd - 10]);
}

/* ---------------- kendiliğinden onarım ---------------- */

static void on_health(void)
{
    const Health *h = health_get();
    int ok = health_ok_count();
    DWORD now = GetTickCount();

    settings_on_nettest();
    flyout_refresh();
    if (!g_on || g_tune.on)
        return;

    /* siteler: internet varken hiçbiri açılmıyorsa iki turda bir onar. Panel açılınca
       yapılan ek denetim sayılmasın diye başarısızlıklar en az 2 dk arayla sayılır. */
    if (h->n && !ok && h->control_ok) {
        if (!g_fail_tick || now - g_fail_tick >= 120000) {
            g_heal_fails++;
            g_fail_tick = now;
        }
    } else {
        g_heal_fails = 0;
        g_fail_tick = 0;
    }
    if (g_heal_fails >= 2 && g_cfg.auto_heal && (!g_heal_last || now - g_heal_last > HEAL_COOLDOWN)) {
        wchar_t m[200];

        g_heal_fails = 0;
        g_heal_last = now;
        _snwprintf(m, 200, tr(L"%s açılmıyor; çalışan bir profil aranıyor."), h->s[0].host);
        m[199] = 0;
        app_log(L"Otomatik onarım: %s", m);
        app_notify(tr(L"Koruma onarılıyor"), m, FALSE);
        if (tune_begin(FALSE, h->s[0].host, FALSE) == 0)
            g_tune.heal = TRUE;
        return;
    }

    /* DNS: seçili sunucu iki turdur yanıt vermiyorsa en hızlı çalışana geç */
    if (h->dns == SITE_FAIL) {
        if (!g_dns_tick || now - g_dns_tick >= 120000) {
            g_dns_fails++;
            g_dns_tick = now;
        }
    } else {
        g_dns_fails = 0;
        g_dns_tick = 0;
    }
    if (g_dns_fails >= 2 && g_cfg.dns_failover && g_cfg.dns_mode != DNS_OFF && !g_failover) {
        g_dns_fails = 0;
        g_failover = TRUE;
        app_log(L"DNS sunucusu yanıt vermiyor; yedek aranıyor.");
        dnsbench_start();
    }
}

static void on_dnsbench(BOOL done)
{
    const DnsBench *b = dnsbench_get();

    settings_on_nettest();
    if (!done || !g_failover)
        return;
    g_failover = FALSE;
    if (b->best >= 0 && b->best != g_cfg.dns_preset) {
        wchar_t m[200];

        _snwprintf(m, 200, tr(L"%s yanıt vermiyordu; %s kullanılıyor."), tr(g_dns_presets[g_cfg.dns_preset].name),
                   tr(g_dns_presets[b->best].name));
        m[199] = 0;
        g_cfg.dns_preset = b->best;
        config_save(&g_cfg);
        app_log(L"DNS yedeği: %s", m);
        app_notify(tr(L"DNS değiştirildi"), m, FALSE);
        bypass_restart_if_running();
        settings_on_config();
    }
}

/* ---------------- tetikleyiciler ve zamanlama ---------------- */

/* trig_apps'teki programlardan biri çalışıyor mu ("discord", "Discord.exe" ikisi de olur) */
static BOOL trigger_running(wchar_t *which, size_t cap)
{
    HANDLE snap;
    PROCESSENTRY32W pe = { sizeof(pe) };
    BOOL found = FALSE;

    if (!g_cfg.trig_apps[0])
        return FALSE;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return FALSE;
    if (Process32FirstW(snap, &pe)) {
        do {
            const wchar_t *s = g_cfg.trig_apps;
            wchar_t tok[MAX_PATH];
            size_t el = wcslen(pe.szExeFile);

            while (!found && next_token(&s, tok, MAX_PATH, L",;\r\n")) {
                size_t tl;
                trim_ws(tok);
                tl = wcslen(tok);
                if (tl && (!_wcsicmp(tok, pe.szExeFile) ||
                           (el == tl + 4 && !_wcsnicmp(tok, pe.szExeFile, tl) && !_wcsicmp(pe.szExeFile + tl, L".exe")))) {
                    found = TRUE;
                    wcsncpy(which, pe.szExeFile, cap - 1);
                    which[cap - 1] = 0;
                }
            }
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* Şu an zamanlanmış kapalı aralıkta mıyız; aralık gece yarısını aşabilir,
   gün koşulu aralığın başladığı güne bakar. */
static BOOL schedule_active(void)
{
    SYSTEMTIME st;
    int now, from = g_cfg.sched_from, to = g_cfg.sched_to, wd;
    BOOL in, prevday = FALSE;

    if (!g_cfg.sched_on || from == to)
        return FALSE;
    GetLocalTime(&st);
    now = st.wHour * 60 + st.wMinute;
    if (from < to) {
        in = now >= from && now < to;
    } else {
        in = now >= from || now < to;
        prevday = now < to;
    }
    if (!in)
        return FALSE;
    wd = (st.wDayOfWeek + (prevday ? 6 : 0)) % 7;   /* 0 pazar */
    if (g_cfg.sched_days == 1)
        return wd >= 1 && wd <= 5;
    if (g_cfg.sched_days == 2)
        return wd == 0 || wd == 6;
    return TRUE;
}

/* Ön planda tam ekran bir oyun var mı; tarayıcı ve sistem programları sayılmaz
   (tam ekran video izlerken korumanın kalkması istenmez). */
static BOOL game_foreground(wchar_t *exe, size_t cap)
{
    static const wchar_t *skip[] = {
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"opera_gx.exe", L"brave.exe", L"vivaldi.exe",
        L"yandex.exe", L"browser.exe", L"explorer.exe", L"discord.exe", L"TunnelDPI.exe", L"ApplicationFrameHost.exe",
    };
    QUERY_USER_NOTIFICATION_STATE st;
    wchar_t path[MAX_PATH];
    DWORD pid = 0, len = MAX_PATH;
    const wchar_t *b;
    HANDLE h;
    size_t i;
    BOOL ok;

    if (FAILED(SHQueryUserNotificationState(&st)) || (st != QUNS_RUNNING_D3D_FULL_SCREEN && st != QUNS_BUSY))
        return FALSE;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    h = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
    if (!h)
        return FALSE;
    ok = QueryFullProcessImageNameW(h, 0, path, &len);
    CloseHandle(h);
    if (!ok)
        return FALSE;
    b = wcsrchr(path, L'\\');
    b = b ? b + 1 : path;
    for (i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (!_wcsicmp(b, skip[i]))
            return FALSE;
    wcsncpy(exe, b, cap - 1);
    exe[cap - 1] = 0;
    return TRUE;
}

static void game_set(const wchar_t *exe)
{
    if (exe)
        wcsncpy(g_game, exe, MAX_PATH - 1);
    else
        g_game[0] = 0;
    config_set_game(&g_cfg, g_game);
    bypass_restart_if_running();
}

static void game_tick(void)
{
    wchar_t exe[MAX_PATH], m[200];
    BOOL usable = g_cfg.game_mode && g_on && config_active(&g_cfg)->engine == ENGINE_CLASSIC;

    if (!usable) {
        if (g_game[0]) {
            app_log(L"Oyun modu kapandı.");
            g_game[0] = 0;
            config_set_game(&g_cfg, NULL);
            if (g_on)
                bypass_restart_if_running();
        }
        return;
    }
    if (game_foreground(exe, MAX_PATH)) {
        g_game_idle = 0;
        if (_wcsicmp(exe, g_game)) {
            _snwprintf(m, 200, tr(L"%s bağlantılarına dokunulmuyor; diğer programlar korunmaya devam ediyor."), exe);
            m[199] = 0;
            app_log(L"Oyun modu: %s", m);
            game_set(exe);
            app_notify(tr(L"Oyun modu"), m, FALSE);
        }
    } else if (g_game[0] && ++g_game_idle >= 4) {
        /* bir dakikadır ön planda değil: alt+tab ile her çıkışta motoru yeniden başlatmamak için beklendi */
        app_log(L"Oyun modu: %s kapandı ya da arka planda; koruma her şeye uygulanıyor.", g_game);
        game_set(NULL);
    }
}

static void auto_tick(void)
{
    wchar_t which[MAX_PATH] = L"", m[200];
    BOOL run, sch;

    if (g_tune.on || g_captive || g_captive_probe)
        return;
    game_tick();
    sch = schedule_active();
    if (sch && !g_sched_prev && g_on) {
        _snwprintf(m, 200, tr(L"Zamanlamaya göre %02d:%02d'e kadar kapalı."), g_cfg.sched_to / 60, g_cfg.sched_to % 60);
        app_log(L"Zamanlama: %s", m);
        bypass_off();
        g_sched_off = TRUE;
        app_notify(tr(L"Koruma kapatıldı"), m, FALSE);
    } else if (!sch && g_sched_prev && g_sched_off) {
        g_sched_off = FALSE;
        if (!g_on) {
            app_log(L"Zamanlama bitti; koruma açılıyor.");
            bypass_on();
            app_notify(tr(L"Koruma açıldı"), tr(L"Zamanlanmış kapalı süre bitti."), FALSE);
        }
    }
    g_sched_prev = sch;

    run = g_cfg.trig_on && trigger_running(which, MAX_PATH);
    if (run && !g_trig_prev && !g_on && !sch && !app_paused()) {
        _snwprintf(m, 200, tr(L"%s açıldı; koruma da açıldı."), which);
        app_log(L"Tetikleyici: %s", m);
        bypass_on();
        g_trig_started = TRUE;
        app_notify(APP_NAME, m, FALSE);
    } else if (!run && g_trig_prev && g_trig_started) {
        g_trig_started = FALSE;
        if (g_on) {
            app_log(L"Tetikleyici program kapandı; koruma kapatılıyor.");
            bypass_off();
            app_notify(APP_NAME, tr(L"Program kapandı; koruma da kapatıldı."), FALSE);
        }
    }
    g_trig_prev = run;
}

void app_tune_cancel(void)
{
    if (g_tune.on)
        tune_finish(TRUE);
}

static void tune_on_result(void)
{
    const NetTest *nt = nettest_get();

    g_tune.waiting = FALSE;
    g_tune_progress.completed++;
    if (nt->state == NT_OK)
        g_tune_progress.passed++;
    if (nt->state == NT_OK && nt->ms < g_tune.best_ms) {
        g_tune.best = g_tune.idx;
        g_tune.best_ms = nt->ms;
    }
    app_log(L"  %s: %s", g_tune.cands[g_tune.idx].name, nt->state == NT_OK ? L"çalışıyor" : nettest_error_text());
    g_tune.idx++;
    tune_try();
}

/* ---------------- ağa göre profil ---------------- */

/* Bağlı ağı bu profille eşleştir (varsa günceller) */
void app_net_remember(const wchar_t *profile)
{
    wchar_t key[24], name[64];
    int i;

    if (!netid_current(key, 24, name, 64))
        return;
    for (i = 0; i < g_cfg.nnets && _wcsicmp(g_cfg.nets[i].key, key); i++)
        ;
    if (i == g_cfg.nnets) {
        if (g_cfg.nnets >= MAX_NETS) {             /* en eskisini at */
            memmove(&g_cfg.nets[0], &g_cfg.nets[1], sizeof(g_cfg.nets[0]) * (MAX_NETS - 1));
            i = MAX_NETS - 1;
        } else {
            g_cfg.nnets++;
        }
        wcscpy(g_cfg.nets[i].key, key);
    }
    wcsncpy(g_cfg.nets[i].name, name, 63);
    wcsncpy(g_cfg.nets[i].profile, profile, NAME_CAP - 1);
    config_save(&g_cfg);
    app_log(L"\"%s\" ağı için \"%s\" profili hatırlandı.", name, profile);
}

/* Kayıtlı ağdaysak o ağın profiline geç */
static BOOL net_apply(void)
{
    wchar_t key[24], name[64];
    int i;

    if (!g_cfg.net_auto || g_tune.on || !netid_current(key, 24, name, 64))
        return FALSE;
    for (i = 0; i < g_cfg.nnets; i++) {
        if (_wcsicmp(g_cfg.nets[i].key, key))
            continue;
        if (config_find(&g_cfg, g_cfg.nets[i].profile) < 0 || !_wcsicmp(g_cfg.nets[i].profile, g_cfg.active))
            return FALSE;
        wcscpy(g_cfg.active, g_cfg.nets[i].profile);
        config_save(&g_cfg);
        app_log(L"Ağ: %s → \"%s\" profili.", name, g_cfg.active);
        {
            wchar_t m[200];
            _snwprintf(m, 200, tr(L"%s ağına bağlanıldı; \"%s\" profili kullanılıyor."), name, g_cfg.active);
            m[199] = 0;
            app_notify(APP_NAME, m, FALSE);
        }
        return TRUE;
    }
    return FALSE;
}

/* ---------------- kısayol tuşu ---------------- */

void app_apply_hotkey(void)
{
    UnregisterHotKey(g_hwnd, HOTKEY_TOGGLE);
    if (g_cfg.hotkey && !RegisterHotKey(g_hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'G'))
        app_log(L"Ctrl+Alt+G kısayolu başka bir uygulama tarafından kullanılıyor.");
}

BOOL app_autostart(void)
{
    return g_autostart;
}

void app_set_autostart(BOOL on)
{
    if (autostart_set(on)) {
        g_autostart = on;
        app_log(on ? L"Otomatik başlatma etkin (Görev Zamanlayıcı)." : L"Otomatik başlatma kapatıldı.");
    } else {
        app_notify(tr(L"Otomatik başlatma"), tr(L"Windows açılışı ayarı değiştirilemedi."), TRUE);
    }
    g_autostart = autostart_enabled();
    settings_on_config();
}

/* Modern soru penceresi (TaskDialog). Dönüş: IDYES / IDNO / IDCANCEL */
static int ask(const wchar_t *main, const wchar_t *content, BOOL cancel)
{
    int btn = IDNO;

    if (FAILED(task_box(NULL, main, content,
                          TDCBF_YES_BUTTON | TDCBF_NO_BUTTON | (cancel ? TDCBF_CANCEL_BUTTON : 0),
                          TD_INFORMATION_ICON, &btn)))
        btn = MessageBoxW(NULL, content, APP_NAME, (cancel ? MB_YESNOCANCEL : MB_YESNO) | MB_ICONQUESTION);
    return btn;
}

/* ---------------- pencere ---------------- */


static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TASKBARCREATED && msg) {
        icons_reset();
        tray_update(NIM_ADD);
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
            flyout_show((short)LOWORD(wp), (short)HIWORD(wp));
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
            settings_show(SETTINGS_PAGE_HOME);
            break;
        case NIN_BALLOONUSERCLICK:
            settings_show(SETTINGS_PAGE_LOG);
            break;
        case WM_MBUTTONUP:
            recent_menu((short)LOWORD(wp), (short)HIWORD(wp));
            break;
        }
        return 0;
    case WM_ENGINE_EXIT:
        on_engine_exit((DWORD)wp, (LONG)lp);
        return 0;
    case WM_ENGINE_LOG:
        settings_on_log();
        return 0;
    case WM_SHOW_SETTINGS:
        settings_show(-1);
        return 0;
    case WM_SHOW_GUIDE:
        guide_show(NULL);
        return 0;
    case WM_SHOW_RELEASE:
        release_show(NULL);
        return 0;
    case WM_APP_QUIT:
        app_quit();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_HEALTH_DONE:
        on_health();
        return 0;
    case WM_DNSBENCH_DONE:
        on_dnsbench(wp == 1);
        return 0;
    case WM_NETTEST:
        if (g_tune.on && g_tune.waiting) {
            tune_on_result();
        } else if (g_fix.waiting && !_wcsicmp(nettest_get()->host, g_fix.host) && nettest_get()->state != NT_RUNNING) {
            const NetTest *nt = nettest_get();
            wchar_t m[200];

            g_fix.waiting = FALSE;
            if (nt->state == NT_OK) {
                _snwprintf(m, 200, tr(L"%s açılıyor (%lu ms)."), g_fix.host, nt->ms);
                m[199] = 0;
                app_log(L"Site düzeltme: %s", m);
                app_notify(tr(L"Site açıldı"), m, FALSE);
            } else {
                app_log(L"Site düzeltme: %s şu profille açılmadı; profiller deneniyor.", g_fix.host);
                tune_begin(FALSE, g_fix.host, TRUE);
            }
        } else if (g_open_check && !g_net_check && nettest_get()->state != NT_RUNNING) {
            g_open_check = FALSE;
            g_probe_net = FALSE;
            if (nettest_get()->state == NT_FAIL && g_on)
                captive_suspect();
        } else if (g_net_check && nettest_get()->state != NT_RUNNING) {
            const NetTest *nt = nettest_get();
            g_net_check = g_open_check = FALSE;
            g_probe_net = TRUE;
            if (nt->state == NT_FAIL && g_on && captive_suspect()) {
                /* sonuç gelince karar verilir */
            } else if (nt->state == NT_FAIL) {
                wchar_t m[200];
                _snwprintf(m, 200, tr(L"%s: %s. \"En uygun profili bul\" ile başka bir profil dene."),
                           nt->host, nettest_error_text());
                m[199] = 0;
                app_notify(tr(L"Ağ değişti, bağlantı testi başarısız"), m, TRUE);
            }
        }
        settings_on_nettest();
        flyout_refresh();
        return 0;
    case WM_NET_CHANGED:
        if (g_cfg.share_proxy)
            SetTimer(h, TIMER_AUTO, 3000, NULL);  /* erişim noktası açılmış olabilir */
        if (g_on && !g_tune.on)
            SetTimer(h, TIMER_NET, 4000, NULL);   /* ağ olayları art arda gelir; yatışmasını bekle */
        return 0;
    case WM_CAPTIVE_DONE:
        on_captive((int)wp);
        return 0;
    case WM_PRESETS_DONE:
        if (wp == 1 && presets_load()) {
            app_log(L"Hazır ayarlar güncellendi (%s).", presets_version());
            settings_on_config();
        } else {
            settings_on_nettest();
        }
        flyout_refresh();
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC && g_on)
            SetTimer(h, TIMER_RESUME, 5000, NULL);   /* ağ bağdaştırıcıları ayağa kalksın */
        return TRUE;
    case WM_DIAG_DONE:
        settings_on_nettest();
        return 0;
    case WM_SUBS_DONE:
        /* güncellenen listeleri motor ancak yeniden başlayınca okur */
        if (wp > 0 && (config_active(&g_cfg)->blacklist[0] || g_cfg.block_on))
            bypass_restart_if_running();
        if (lp > 0)
            app_notify(tr(L"Liste güncellenemedi"), tr(L"Bazı listeler indirilemedi; ayrıntılar Günlük sayfasında."), TRUE);
        settings_on_lists();
        return 0;
    case WM_UPDATE_DONE: {
        const UpdateInfo *u = update_get();
        if (wp == 1) {
            app_notify(tr(L"Güncelleme indirilemedi"), tr(L"Sürüm sayfasından elle indirebilirsin."), TRUE);
        } else if (wp == 2) {
            app_notify(tr(L"Güncelleme doğrulanamadı"),
                       tr(L"İndirilen dosyanın imzası tutmadı; güvenliğin için kurulmadı."), TRUE);
        } else if (u->state == UPD_DONE && u->newer && _wcsicmp(u->tag, g_cfg.seen_tag)) {
            wchar_t msg[200];
            _snwprintf(msg, 200, tr(L"%s %s yayınlandı. Ayarlar → Genel → Güncellemeler."), APP_NAME, u->tag);
            msg[199] = 0;
            app_notify(tr(L"Yeni sürüm"), msg, FALSE);
            wcsncpy(g_cfg.seen_tag, u->tag, 63);
            config_save(&g_cfg);
        }
        settings_on_config();
        return 0;
    }
    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE && !g_tune.on) {
            app_toggle();
            app_notify(APP_NAME, g_on ? tr(L"Koruma açıldı (Ctrl+Alt+G)") : tr(L"Koruma kapatıldı (Ctrl+Alt+G)"), FALSE);
        }
        return 0;
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == TIMER_RESTART && g_on) {
            wchar_t err[512];
            if (engine_start(&g_cfg, err, 512)) {
                set_state(ICON_ON);
            } else {
                app_log(L"%s", err);
                set_state(ICON_ERROR);
                SetTimer(h, TIMER_RESTART, 60000, NULL);
            }
        } else if (wp == TIMER_NET && g_on) {
            wchar_t err[256], key[24];
            net_key_now(key);
            if (!wcscmp(key, g_net_key))
                return 0;                         /* aynı ağ: motorun kendi bildirimi */
            wcscpy(g_net_key, key);
            g_net_changed = GetTickCount();
            SetTimer(h, TIMER_HEALTH, 15000, NULL);   /* yeni ağda siteleri hemen denetle */
            isp_detect();
            if (net_apply()) {
                bypass_restart_if_running();      /* yeni profil; test bypass_on'dan sonra gelir */
                settings_on_config();
                flyout_refresh();
            } else {
                if (g_cfg.dns_mode == DNS_SYSTEM)
                    dns_system_apply(&g_cfg, err, 256);
                app_log(L"Ağ değişti; bağlantı test ediliyor.");
                g_net_check = TRUE;
                nettest_start();
            }
        } else if (wp == TIMER_PAUSE) {
            g_pause_until = 0;
            if (!g_on) {
                app_log(L"Duraklatma bitti; koruma açılıyor.");
                bypass_on();
            }
        } else if (wp == TIMER_SUBS) {
            subs_update(NULL, FALSE);
            SetTimer(h, TIMER_SUBS, 6 * 3600 * 1000, NULL);
        } else if (wp == TIMER_UPDATE) {
            if (g_cfg.update_check)
                update_check();
            presets_update();
            SetTimer(h, TIMER_UPDATE, 24 * 3600 * 1000, NULL);
        } else if (wp == TIMER_TUNE && g_tune.on) {
            if (nettest_get()->state == NT_RUNNING) {
                SetTimer(h, TIMER_TUNE, 1000, NULL);   /* önceki test bitsin */
            } else {
                g_tune.waiting = TRUE;
                nettest_start_host(g_tune.host);
                settings_on_config();
            }
        } else if (wp == TIMER_TEST) {
            if (!g_fix.waiting)
                nettest_start();
            else if (nettest_get()->state == NT_RUNNING)
                SetTimer(h, TIMER_TEST, 1000, NULL);   /* önceki test bitsin */
            else
                nettest_start_host(g_fix.host);
        } else if (wp == TIMER_HEALTH) {
            if (g_on) {
                if (!g_tune.on)
                    health_start();
                /* yeni ağda bozukluk çabuk anlaşılsın: ilk dakikalarda 2 dakikada bir */
                SetTimer(h, TIMER_HEALTH, g_net_changed && GetTickCount() - g_net_changed < NET_FRESH ? 120000
                                                                                                       : HEALTH_PERIOD, NULL);
            }
        } else if (wp == TIMER_CAPTIVE) {
            if (g_captive)
                captive_check();
        } else if (wp == TIMER_RESUME) {
            if (g_on && !g_tune.on) {
                app_log(L"Uykudan uyanıldı; koruma yenileniyor.");
                g_net_changed = GetTickCount();
                bypass_restart_if_running();
            }
        } else if (wp == TIMER_AUTO) {
            auto_tick();
            share_apply();
            SetTimer(h, TIMER_AUTO, 15000, NULL);
        } else if (wp == TIMER_TRIM) {
            trim_memory();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (msg == WM_DISPLAYCHANGE || wp == SPI_SETICONMETRICS) {
            icons_reset();
            tray_update(NIM_MODIFY);
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            engine_stop();
            dns_system_restore();
        }
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW nid = { sizeof(nid) };
        UnregisterHotKey(h, HOTKEY_TOGGLE);
        if (g_tune.on)
            tune_finish(TRUE);
        flyout_close();
        notify_close();
        settings_close();
        bypass_off();
        share_stop();
        nid.hWnd = h;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static BOOL relaunch_elevated(const wchar_t *args)
{
    wchar_t exe[MAX_PATH];
    SHELLEXECUTEINFOW sei = { sizeof(sei) };

    GetModuleFileNameW(NULL, exe, MAX_PATH);
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.lpDirectory = g_appdir;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei);
}

#ifdef DEV_SHOT
/* Geliştirme: --shot <sayfa> <dosya.bmp> [kaydırma] [adv]
   Ayar penceresini çizip BMP olarak kaydeder (yetki/tray olmadan). */
static void save_shot(HWND w, const wchar_t *path)
{
    RECT r;
    HDC sdc, mdc;
    HBITMAP bmp;
    BITMAPINFOHEADER bi = { sizeof(bi) };
    BITMAPFILEHEADER fh = { 0x4D42 };
    BYTE *px;
    HANDLE f;
    DWORD wr;

    GetWindowRect(w, &r);
    sdc = GetDC(NULL);
    mdc = CreateCompatibleDC(sdc);
    bi.biWidth = r.right - r.left;
    bi.biHeight = -(r.bottom - r.top);
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bmp = CreateDIBSection(sdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    SelectObject(mdc, bmp);
    PrintWindow(w, mdc, 2 /* PW_RENDERFULLCONTENT */);
    fh.bfOffBits = sizeof(fh) + sizeof(bi);
    fh.bfSize = fh.bfOffBits + bi.biWidth * -bi.biHeight * 4;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, &fh, sizeof(fh), &wr, NULL);
    WriteFile(f, &bi, sizeof(bi), &wr, NULL);
    WriteFile(f, px, bi.biWidth * -bi.biHeight * 4, &wr, NULL);
    CloseHandle(f);
    DeleteDC(mdc);
    DeleteObject(bmp);
    ReleaseDC(NULL, sdc);
}

static wchar_t g_shot_path[MAX_PATH];
static const wchar_t *g_shot_class = L"TunnelDPIPopup";

static VOID CALLBACK popup_shot(HWND h, UINT m, UINT_PTR id, DWORD t)
{
    HWND p = FindWindowW(g_shot_class, NULL);

    (void)h; (void)m; (void)t;
    KillTimer(NULL, id);
    if (p) {
        save_shot(p, g_shot_path);
        PostMessageW(p, WM_KEYDOWN, VK_ESCAPE, 0);
    }
}

static int dev_shot(const wchar_t *cmdline)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    int page = 0, scroll = 0, i;
    wchar_t path[MAX_PATH] = L"shot.bmp";
    HWND w, c;
    MSG m;

    swscanf(wcsstr(cmdline, L"--shot") + 6, L"%d %259ls %d", &page, path, &scroll);
    InitCommonControlsEx(&icc);
    ui_enable_dark_menus();
    engine_init();
    config_load(&g_cfg);
    i18n_set(g_cfg.lang);
    if (page == 99) {
        flyout_show(GetSystemMetrics(SM_CXSCREEN) - 200, GetSystemMetrics(SM_CYSCREEN));
        w = FindWindowW(L"TunnelDPIFlyout", NULL);
        c = NULL;
    } else {
        settings_show(page == 98 || page == 97 ? 0 : page);
        w = FindWindowW(L"TunnelDPIWindow", NULL);
        c = FindWindowExW(w, NULL, L"TunnelDPIContent", NULL);
    }
    if (!w)
        return 2;
    if (c && wcsstr(cmdline, L" adv")) {
        void settings_dev_adv(void);
        settings_dev_adv();
    }
    for (i = 0; c && i < scroll; i++)
        SendMessageW(c, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    for (i = 0; i < 150; i++) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
        Sleep(10);
    }
    if (page == 98) {
        /* açılır liste modaldır: zamanlayıcı içinde görüntüyü alıp kapat */
        void settings_dev_dropdown(void);
        wcscpy(g_shot_path, path);
        SetTimer(NULL, 0, 700, popup_shot);
        settings_dev_dropdown();
    } else if (page == 97) {
        void settings_dev_bubble(void);
        wcscpy(g_shot_path, path);
        g_shot_class = L"TunnelDPIBubble";
        SetTimer(NULL, 0, 700, popup_shot);
        settings_dev_bubble();
    } else {
        save_shot(w, path);
    }
    flyout_close();
    settings_close();
    return 0;
}
#endif

#ifdef DEV_SHOT
/* Geliştirme: yönetici izni gerektirmeyen denetimler. Sonuç selftest.txt'ye.
   - ayarlar kaydedilip yeniden okununca aynı mı
   - her profilin motor komutu
   - sürüm karşılaştırma, çeviri tablosu */
static int dev_selftest(void)
{
    static Config a, b;
    wchar_t *out = calloc(65536, sizeof(wchar_t)), cmd[4096];
    size_t n = 0;
    int i, fails = 0;

    if (!out)
        return 1;
    config_load(&a);
    config_save(&a);
    config_load(&b);
    config_load(&g_cfg);       /* yedek ve oyun modu denetimleri etkin ayarları kullanır */
    for (i = 0; i < a.nprofiles; i++) {
        wchar_t c2[4096];
        build_args(&a, &a.profiles[i], cmd, 4096);
        build_args(&b, &b.profiles[i], c2, 4096);
        if (wcscmp(cmd, c2) || wcscmp(a.profiles[i].name, b.profiles[i].name)) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL roundtrip %s\r\n", a.profiles[i].name);
            fails++;
        }
        n += _snwprintf(out + n, 65536 - n, L"[%s] %s %s\r\n", a.profiles[i].name, engine_name(&a.profiles[i]), cmd);
    }
    if (a.nprofiles != b.nprofiles || a.block_on != b.block_on || wcscmp(a.block_lists, b.block_lists) ||
        a.dns_mode != b.dns_mode || wcscmp(a.active, b.active) || a.nnets != b.nnets) {
        n += _snwprintf(out + n, 65536 - n, L"FAIL roundtrip general\r\n");
        fails++;
    }
    {
        static const struct { const wchar_t *x, *y; int r; } v[] = {
            { L"v1.2.0", L"1.1.0", 1 }, { L"1.1.0", L"1.1.0", 0 }, { L"v1.10", L"1.9.9", 1 }, { L"1.1", L"1.1.1", -1 },
        };
        for (i = 0; i < 4; i++)
            if (version_cmp(v[i].x, v[i].y) != v[i].r) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL version_cmp %s %s\r\n", v[i].x, v[i].y);
                fails++;
            }
    }
    {
        /* adres temizleme */
        static const struct { const wchar_t *in, *out; } hn[] = {
            { L"https://www.Discord.com/app?x=1", L"www.discord.com" },
            { L"  x.com:443/home", L"x.com" },
            { L"discord.gg.", L"discord.gg" },
            { L"abc", NULL },
            { L"http:///yol", NULL },
        };
        wchar_t h[128];
        for (i = 0; i < 5; i++) {
            BOOL ok = host_normalize(hn[i].in, h, 128);
            if (ok != (hn[i].out != NULL) || (ok && wcscmp(h, hn[i].out))) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL host_normalize %s -> %s\r\n", hn[i].in, ok ? h : L"(geçersiz)");
                fails++;
            }
        }
    }
    {
        /* site kuralı: ana motor kural listesini atlar, kural motoru yalnız onu işler */
        static Config c;
        wchar_t path[MAX_PATH], rule[4096];
        int idx[MAX_RULES], k;

        config_load(&c);
        k = config_find(&c, L"Superonline");
        app_path(path, MAX_PATH, L"lists\\site-selftest.txt");
        write_text_file(path, L"ornek.com\r\n");
        if (k >= 0 && c.profiles[0].engine == ENGINE_CLASSIC) {
            wcscpy(c.active, c.profiles[0].name);
            rule_set(&c, L"lists\\site-selftest.txt", c.profiles[k].name);
            c.dns_mode = DNS_REDIRECT;
            if (rules_usable(&c, &c.profiles[0], idx, MAX_RULES) != 1) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL rules_usable\r\n");
                fails++;
            }
            build_rule_args(&c, &c.profiles[k], c.rules[0].list, -10, rule, 4096);
            if (!wcsstr(rule, L"--priority -10") || !wcsstr(rule, L"site-selftest.txt") || wcsstr(rule, L"--dns-addr")) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL build_rule_args: %s\r\n", rule);
                fails++;
            }
            engine_cmdline(&c, cmd, 4096);
            if (!wcsstr(cmd, L"--skip-list") || !wcsstr(cmd, L"--dns-addr")) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL engine_cmdline: %s\r\n", cmd);
                fails++;
            }
            n += _snwprintf(out + n, 65536 - n, L"rule main: %s\r\nrule inst: %s\r\n", cmd, rule);
            rule_set(&c, L"lists\\site-selftest.txt", NULL);
            if (c.nrules) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL rule_set remove\r\n");
                fails++;
            }
        }
        DeleteFileW(path);
        /* son profiller: yeniden eskiye, tekrar yok */
        memset(c.recent, 0, sizeof(c.recent));
        recent_push(&c, L"A");
        recent_push(&c, L"B");
        recent_push(&c, L"C");
        recent_push(&c, L"B");
        recent_push(&c, L"D");
        if (wcscmp(c.recent[0], L"D") || wcscmp(c.recent[1], L"B") || wcscmp(c.recent[2], L"C")) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL recent %s|%s|%s\r\n", c.recent[0], c.recent[1], c.recent[2]);
            fails++;
        }
    }
    {
        /* oyun modu: oyun ters çevrilmiş süreç listesine girer */
        wchar_t path[MAX_PATH], *t;

        config_set_game(&g_cfg, L"oyun.exe");
        build_args(&g_cfg, &g_cfg.profiles[0], cmd, 4096);
        app_path(path, MAX_PATH, GAME_FILE);
        t = read_text_file(path);
        if (g_cfg.app_mode != APPS_ONLY &&
            (!wcsstr(cmd, L"--process-invert") || !wcsstr(cmd, GAME_FILE) || !t || !wcsstr(t, L"oyun.exe"))) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL game mode: %s\r\n", cmd);
            fails++;
        }
        free(t);
        config_set_game(&g_cfg, NULL);
        build_args(&g_cfg, &g_cfg.profiles[0], cmd, 4096);
        if (wcsstr(cmd, GAME_FILE)) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL game mode off: %s\r\n", cmd);
            fails++;
        }
        DeleteFileW(path);
    }
    {
        /* yedek: kaydet, bir listeyi boz, geri yükle, aynı mı */
        wchar_t bak[MAX_PATH], lst[MAX_PATH], *before, *after;
        int s, r;

        app_path(lst, MAX_PATH, L"lists\\yedek-test.txt");
        write_text_file(lst, L"bir.com\r\niki.com\r\n");
        GetTempPathW(MAX_PATH, bak);
        wcscat(bak, L"tunneldpi-selftest-yedek.txt");
        before = read_text_file(lst);
        s = backup_save(bak);
        write_text_file(lst, L"bozuldu\r\n");
        r = backup_restore(bak);
        after = read_text_file(lst);
        if (s <= 0 || r != s || !before || !after || wcscmp(before, after)) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL backup save=%d restore=%d\r\n", s, r);
            fails++;
        }
        {
            static Config c2;
            config_load(&c2);
            if (c2.nprofiles != g_cfg.nprofiles || wcscmp(c2.active, g_cfg.active)) {
                n += _snwprintf(out + n, 65536 - n, L"FAIL backup ini\r\n");
                fails++;
            }
        }
        n += _snwprintf(out + n, 65536 - n, L"backup: %d files\r\n", s);
        free(before);
        free(after);
        DeleteFileW(lst);
        write_text_file(bak, L"rastgele metin");
        if (backup_restore(bak) != -1) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL backup accepts junk\r\n");
            fails++;
        }
        DeleteFileW(bak);
    }
    {
        const BrowserDns *bd;
        int nb = browsers_dns(&bd), k;
        for (k = 0; k < nb; k++)
            n += _snwprintf(out + n, 65536 - n, L"browser %s: secure dns %d %s\r\n", bd[k].name, bd[k].state, bd[k].server);
    }
    {
        /* DNS yoklaması (ağ gerekir; başarısızlık hata sayılmaz) */
        DWORD ms;
        BOOL ok = dns_probe(L"1.1.1.1", 53, 1500, &ms);
        n += _snwprintf(out + n, 65536 - n, L"dns_probe 1.1.1.1: %s %lu ms\r\n", ok ? L"ok" : L"no answer", ms);
        ok = dns_probe(L"77.88.8.8", 1253, 1500, &ms);
        n += _snwprintf(out + n, 65536 - n, L"dns_probe 77.88.8.8:1253: %s %lu ms\r\n", ok ? L"ok" : L"no answer", ms);
        ok = dns_probe(L"192.0.2.1", 53, 800, &ms);
        if (ok) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL dns_probe answered from TEST-NET\r\n");
            fails++;
        }
    }
    {
        /* hazır ayarlar: imzalı dosya okunur; sağlayıcı ve giriş sayfası denetimi ağdan sonuç döndürür */
        wchar_t path[MAX_PATH];
        MSG m;
        DWORD t0;
        int got = 0, cap = -1;

        engine_init();                      /* günlük kilidi: iş parçacıkları app_log yazar */
        if (!g_hwnd)                        /* sonuç iletileri bu iş parçacığına gelsin */
            g_hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_inst, NULL);
        app_path(path, MAX_PATH, PRESETS_FILE);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            BOOL ok = presets_load();
            n += _snwprintf(out + n, 65536 - n, L"presets: %s %s\r\n", ok ? L"ok" : L"FAIL", presets_version());
            if (!ok)
                fails++;
        }
        isp_detect();
        captive_check();
        for (t0 = GetTickCount(); GetTickCount() - t0 < 15000 && (got < 1 || cap < 0);) {
            if (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_PRESETS_DONE && m.wParam == 2)
                    got = 1;
                else if (m.message == WM_CAPTIVE_DONE)
                    cap = (int)m.wParam;
                else
                    DispatchMessageW(&m);
            } else {
                Sleep(20);
            }
        }
        {
            const wchar_t *rec[4];
            int nr = isp_profiles(rec, 4);
            n += _snwprintf(out + n, 65536 - n, L"isp: %s, %d recommended%s%s\r\n", isp_name()[0] ? isp_name() : L"(none)",
                            nr, nr ? L", first " : L"", nr ? rec[0] : L"");
        }
        n += _snwprintf(out + n, 65536 - n, L"captive: %s\r\n",
                        cap == CAPTIVE_ONLINE ? L"online" : cap == CAPTIVE_PORTAL ? L"portal" : cap == CAPTIVE_OFFLINE ? L"offline" : L"timeout");
        if (cap != CAPTIVE_ONLINE) {
            n += _snwprintf(out + n, 65536 - n, L"FAIL captive (this PC is online)\r\n");
            fails++;
        }
    }
    i18n_set(2);
    if (!wcscmp(tr(L"Açık"), L"Açık")) {
        n += _snwprintf(out + n, 65536 - n, L"FAIL i18n\r\n");
        fails++;
    }
    n += _snwprintf(out + n, 65536 - n, L"%s\r\n", fails ? L"SOME CHECKS FAILED" : L"ALL OK");
    write_text_file(L"selftest.txt", out);
    free(out);
    return fails;
}
#endif

#ifdef DEV_SHOT
/* Geliştirme: tüm düğmeleri, anahtarları, listeleri ve menüleri sırayla dener.
   Açılan onay ve dosya pencereleri arka plandaki iş parçacığınca kapatılır.
   Sonuç uitest.txt'ye; dönüş değeri hata sayısıdır. */
extern int g_dev_pick;
int  settings_dev_uitest(FILE *f, int *fails);
int  flyout_dev_uitest(FILE *f, int *fails);
void flyout_dev_quit(void);
static volatile LONG g_dev_dialogs, g_dev_stuck;

/* Kendi sürecimizin iletişim kutularını kapatır. Her pencere bir kez sayılır;
   WM_CLOSE'a 1 sn'de yanıt vermeyen (kapatılamayan) pencere ayrıca sayılır. */
static DWORD WINAPI dev_dialog_closer(LPVOID arg)
{
    DWORD me = GetCurrentProcessId(), pid, seen_at[64] = { 0 };
    HWND seen[64] = { 0 }, w;
    int ns = 0, i;
    (void)arg;

    for (;;) {
        w = NULL;
        while ((w = FindWindowExW(NULL, w, L"#32770", NULL)) != NULL) {
            GetWindowThreadProcessId(w, &pid);
            if (pid != me || !IsWindowVisible(w))
                continue;
            for (i = 0; i < ns && seen[i] != w; i++)
                ;
            if (i == ns) {
                if (ns < 64) {
                    seen[ns] = w;
                    seen_at[ns++] = GetTickCount();
                } else {
                    ns = 0;
                }
                InterlockedIncrement(&g_dev_dialogs);
            } else if (seen_at[i] && GetTickCount() - seen_at[i] > 1000) {
                InterlockedIncrement(&g_dev_stuck);
                seen_at[i] = 0;
                PostMessageW(w, WM_COMMAND, IDOK, 0);
            }
            PostMessageW(w, WM_CLOSE, 0, 0);
        }
        Sleep(150);
    }
    return 0;
}

static int dev_uitest(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    WNDCLASSEXW wc = { sizeof(wc) };
    int fails = 0, n = 0;
    DWORD t;
    BOOL quit = FALSE;
    MSG m;
    FILE *f;

    InitCommonControlsEx(&icc);
    ui_enable_dark_menus();
    engine_init();
    config_load(&g_cfg);
    i18n_set(g_cfg.lang);
    g_cfg.kill_others = 0;                  /* başka motor süreçlerine dokunma */
    wc.lpfnWndProc = wndproc;
    wc.hInstance = g_inst;
    wc.lpszClassName = APP_CLASS;
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_inst, NULL);
    CloseHandle(CreateThread(NULL, 0, dev_dialog_closer, NULL, 0, NULL));
    f = _wfopen(L"uitest.txt", L"w, ccs=UTF-8");
    if (!f || !g_hwnd)
        return 99;

    n += settings_dev_uitest(f, &fails);
    settings_close();
    n += flyout_dev_uitest(f, &fails);

    /* simgeye orta tık: son profiller menüsü → "Tüm profiller…" ayarları açmalı */
    g_dev_pick = 1;
    SendMessageW(g_hwnd, WM_TRAY, MAKEWPARAM(200, 200), MAKELPARAM(WM_MBUTTONUP, 0));
    g_dev_pick = 0;
    for (t = GetTickCount(); GetTickCount() - t < 300;)
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
    {
        BOOL ok = FindWindowW(L"TunnelDPIWindow", NULL) != NULL;
        fwprintf(f, L"\n=== Simge\n  [orta tık] Tüm profiller: %s\n", ok ? L"ayarlar açıldı" : L"HATA: açılmadı");
        if (!ok)
            fails++;
        settings_close();
        n++;
    }

    fwprintf(f, L"\n=== Çıkış (⋯ → Çıkış)\n");
    fflush(f);
    flyout_dev_quit();
    for (t = GetTickCount(); !quit && GetTickCount() - t < 6000;) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                quit = TRUE;
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
    fwprintf(f, L"  %s\n", quit ? L"uygulama kapandı" : L"HATA: kapanmadı");
    if (!quit)
        fails++;
    fwprintf(f, L"\n%d işlem denendi, %ld onay/dosya penceresi açıldı ve kapatıldı (%ld tanesi Kapat'a yanıt vermedi), %d hata\n%s\n",
             n + 1, g_dev_dialogs, g_dev_stuck, fails, fails ? L"SOME CHECKS FAILED" : L"ALL OK");
    fclose(f);
    return fails;
}
#endif

#ifdef DEV_SHOT
static int dev_qualitytest(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    FILE *f;
    int failures = 0;
    wchar_t report[2048], original[NAME_CAP];
    int settings_dev_qualitytest(FILE *f, int *fails);
    InitCommonControlsEx(&icc);
    engine_init();
    config_defaults(&g_cfg);
    i18n_set(LANG_TR);
    g_hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_inst, NULL);
    f = _wfopen(L"qualitytest.txt", L"w, ccs=UTF-8");
    if (!f || !g_hwnd) return 99;
    {
        const wchar_t *private_line = L"Hata: secret.example 192.0.2.7 C:\\Users\\Private token=SECRET\n";
        log_append(private_line, wcslen(private_line));
    }
    support_report(report, 2048);
    if (wcsstr(report, L"secret.example") || wcsstr(report, L"192.0.2.7") ||
        wcsstr(report, L"Private") || wcsstr(report, L"SECRET") || !wcsstr(report, L"error keywords: 1")) failures++;
    fwprintf(f, L"Support privacy: %s\n", failures ? L"FAIL" : L"OK");
    wcscpy(g_cfg.notes_version, APP_VERSION);
    config_save(&g_cfg);
    {
        static Config loaded;
        config_load(&loaded);
        if (wcscmp(loaded.notes_version, APP_VERSION)) failures++;
    }
    wcscpy(original, g_cfg.active);
    g_cfg.net_auto = 0;
    g_tune.on = TRUE; g_tune.best = -1;
    g_tune_progress.total = 2; g_tune_progress.completed = 1;
    g_tune_started = GetTickCount();
    tune_finish(TRUE);
    if (wcscmp(original, g_cfg.active) || !wcsstr(g_tune_progress.summary, L"İptal") ||
        !wcsstr(g_tune_progress.summary, L"1/2")) failures++;
    g_tune.cands = calloc(1, sizeof(Profile));
    if (!g_tune.cands) return 99;
    g_tune.cands[0] = *config_active(&g_cfg);
    wcscpy(g_tune.cands[0].name, L"Quality-test-profile");
    g_cfg.profiles[g_cfg.nprofiles++] = g_tune.cands[0];
    g_tune.on = TRUE; g_tune.best = 0; g_tune.best_ms = 42;
    g_tune_progress.total = g_tune_progress.completed = g_tune_progress.passed = 1;
    tune_finish(FALSE);
    if (wcscmp(g_cfg.active, L"Quality-test-profile") || !wcsstr(g_tune_progress.summary, L"42")) failures++;
    fwprintf(f, L"Search completion/cancellation and notes persistence: %s\n", failures ? L"FAIL" : L"OK");
    settings_dev_qualitytest(f, &failures);
    fwprintf(f, L"%s\n", failures ? L"FAILED" : L"ALL OK");
    fclose(f);
    return failures;
}
#endif

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    HANDLE mutex;
    MSG m;
    wchar_t *slash, ini[MAX_PATH];
    BOOL first_run, autostarted;
    int i, migrated = -1;                    /* g_legacy_apps içindeki indis */

    (void)prev; (void)show;
    g_inst = inst;
    GetModuleFileNameW(NULL, g_appdir, MAX_PATH);
    slash = wcsrchr(g_appdir, L'\\');
    if (slash)
        *slash = 0;
    autostarted = wcsstr(cmdline, L"--autostart") != NULL;
#ifdef DEV_SHOT
    if (wcsstr(cmdline, L"--qualitytest"))
        return dev_qualitytest();
    if (wcsstr(cmdline, L"--shot"))
        return dev_shot(cmdline);
    if (wcsstr(cmdline, L"--selftest"))
        return dev_selftest();
    if (wcsstr(cmdline, L"--notice")) {
        /* --notice <dosya.bmp> <0|1>: bildirim kartını çizip görüntüsünü kaydet */
        HWND notify_dev_card(const wchar_t *title, const wchar_t *body, BOOL error);
        wchar_t out[MAX_PATH] = L"";
        int err = 0;
        HWND w;
        MSG m;
        DWORD t0;
        swscanf(wcsstr(cmdline, L"--notice") + 8, L"%259ls %d", out, &err);
        config_load(&g_cfg);
        i18n_set(g_cfg.lang);
        w = err ? notify_dev_card(tr(L"Koruma başlatılamadı"), L"Motor 2 numaralı hata ile kapandı. WinDivert sürücüsü yüklenemedi; antivirüs engelliyor olabilir.", TRUE)
                : notify_dev_card(tr(L"Koruma onarılıyor"), L"discord.com açılmıyor; çalışan bir profil aranıyor.", FALSE);
        for (t0 = GetTickCount(); GetTickCount() - t0 < 600;)
            while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&m);
        if (w)
            save_shot(w, out);
        return 0;
    }
    if (wcsstr(cmdline, L"--sharetest")) {
        void share_dev_start(DWORD ip, DWORD ms);
        engine_init();
        share_dev_start(0x0100007F, 30000);   /* 127.0.0.1 */
        return 0;
    }
    if (wcsstr(cmdline, L"--uitest"))
        return dev_uitest();
#endif

    /* Yüklü uygulamalar → Kaldır */
    if (wcsstr(cmdline, L"--uninstall")) {
        if (!is_elevated())
            return relaunch_elevated(cmdline) ? 0 : 1;
        return app_uninstall(wcsstr(cmdline, L"--silent") != NULL);
    }

    /* Tek örnek: zaten çalışıyorsa ayar penceresini açtır */
    mutex = CreateMutexW(NULL, FALSE, APP_MUTEX);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(APP_CLASS, NULL);
        if (w)
            PostMessageW(w, wcsstr(cmdline, L"--quit") ? WM_APP_QUIT : WM_SHOW_SETTINGS, 0, 0);
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }

    if (!is_elevated()) {
        CloseHandle(mutex);
        if (!relaunch_elevated(cmdline))
            MessageBoxW(NULL, tr(L"TunnelDPI'ın çalışması için yönetici izni gerekiyor."),
                        APP_NAME, MB_ICONWARNING);
        return 0;
    }

    SetCurrentDirectoryW(g_appdir);
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    InitCommonControlsEx(&icc);
    ui_enable_dark_menus();
    engine_init();

    app_path(ini, MAX_PATH, APP_INI);
    for (i = 0; i < LEGACY_COUNT && !file_exists(ini); i++) {
        /* eski adla kullanılmış klasör: ayar dosyasını devral */
        wchar_t old[MAX_PATH];
        app_path(old, MAX_PATH, g_legacy_apps[i].ini);
        if (file_exists(old) && MoveFileW(old, ini))
            migrated = i;
    }
    first_run = !file_exists(ini);
    config_load(&g_cfg);
    i18n_set(g_cfg.lang);
    log_file_enable(g_cfg.log_file);
    /* eski adlı otomatik başlatma görevini bu exe'ye taşı; kurulum da ilk açılışta ister */
    if (migrated >= 0) {
        wchar_t cmd[128];
        _snwprintf(cmd, 128, L"schtasks.exe /Query /TN \"%s\"", g_legacy_apps[migrated].task);
        if (run_hidden(cmd, 10000) == 0) {
            _snwprintf(cmd, 128, L"schtasks.exe /Delete /F /TN \"%s\"", g_legacy_apps[migrated].task);
            run_hidden(cmd, 10000);
            autostart_set(TRUE);
        }
    }
    if (wcsstr(cmdline, L"--setup-autostart"))
        autostart_set(TRUE);
    app_log(L"%s %s başladı.", APP_NAME, APP_VERSION);

    /* Önceki oturum çöktüyse sistem DNS'ini geri al */
    if (dns_restore_pending()) {
        app_log(L"Önceki oturumdan kalan DNS değişiklikleri geri alınıyor.");
        dns_system_restore();
    }

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, APP_NAME, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!g_hwnd)
        return 1;
    /* Yükseltilmiş süreç: Explorer ve normal yetkili ikinci örnekten mesaj kabul et */
    ChangeWindowMessageFilterEx(g_hwnd, WM_TASKBARCREATED, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(g_hwnd, WM_SHOW_SETTINGS, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(g_hwnd, WM_APP_QUIT, MSGFLT_ALLOW, NULL);

    g_autostart = autostart_enabled();
    tray_update(NIM_ADD);
    app_apply_hotkey();

    if (!g_cfg.ignore_service && legacy_service_exists()) {
        int r = ask(tr(L"Eski GoodbyeDPI hizmeti bulundu"),
                    tr(L"İkisi aynı anda çalışırsa çakışır. Kaldırılsın mı?\n\"Hayır\" dersen bir daha sorulmaz."), TRUE);
        if (r == IDYES)
            legacy_service_remove();
        else if (r == IDNO) {
            g_cfg.ignore_service = 1;
            config_save(&g_cfg);
        }
    }

    if (first_run && !g_autostart && !wcsstr(cmdline, L"--setup")) {
        if (ask(tr(L"Windows açılışında otomatik başlatılsın mı?"),
                tr(L"TunnelDPI bilgisayarı açınca saatin yanında sessizce başlar. Bunu sonra ayarlardan "
                   L"değiştirebilirsin."), FALSE) == IDYES)
            app_set_autostart(TRUE);
    }

    presets_load();
    isp_detect();
    net_apply();                                   /* kayıtlı ağdaysak o ağın profiliyle başla */
    if (g_cfg.start_bypass && !g_cfg.trig_on)      /* tetikleyici varsa program açılınca açılır */
        bypass_on();
    SetTimer(g_hwnd, TIMER_SUBS, 60 * 1000, NULL);
    SetTimer(g_hwnd, TIMER_UPDATE, 90 * 1000, NULL);
    SetTimer(g_hwnd, TIMER_AUTO, 3000, NULL);
    if (first_run) {
        wcscpy(g_cfg.notes_version, APP_VERSION);
        config_save(&g_cfg);
        if (!autostarted)
            PostMessageW(g_hwnd, WM_SHOW_GUIDE, 0, 0);
    } else if (!autostarted && wcscmp(g_cfg.notes_version, APP_VERSION)) {
        PostMessageW(g_hwnd, WM_SHOW_RELEASE, 0, 0);
    }
    if (!autostarted && wcsstr(cmdline, L"--settings"))
        settings_show(-1);
    SetTimer(g_hwnd, TIMER_TRIM, 5000, NULL);

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (settings_dialog_msg(&m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CloseHandle(mutex);
    return 0;
}
