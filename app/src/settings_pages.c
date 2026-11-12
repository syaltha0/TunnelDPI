/*
 * TunnelDPI - ayar sayfalarının içerikleri ve eylemleri
 *
 * Her sayfa page_*() içinde satır listesi olarak kurulur (add, section,
 * toggle, drop, edit, buttons); tıklamalar on_action(), açılır listeler
 * do_dropdown(), metin kutuları on_edit() üzerinden işlenir.
 */
#include "settings_int.h"
#include "presets.h"
#include "share.h"
#include "guide.h"
#include "release.h"
#include "support.h"
#include "i18n.h"
#include "config.h"
#include "dns.h"
#include "engine.h"
#include "block.h"
#include "subs.h"
#include "icons.h"
#include "nettest.h"
#include "popup.h"
#include "diag.h"
#include "subs.h"
#include "update.h"
#include "netid.h"
#include "system.h"
#include "ui.h"
#include "health.h"
#include "browsers.h"
#include "backup.h"
#include "../res/resource.h"
#include <commdlg.h>
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* Profil alanları için anlaşılır başlık/açıklamalar (config.c'deki anahtarlarla) */
static const struct { const wchar_t *key, *title, *desc; } g_fdesc[] = {
    { L"p",            L"Sahte yönlendirmeleri engelle",          L"-p · İnternet sağlayıcının araya soktuğu sahte yönlendirme sayfalarını düşürür" },
    { L"q",            L"HTTP/3'ü kapat",        L"-q · Tarayıcıları engelin aşılabildiği normal HTTPS bağlantısına yönlendirir" },
    { L"r",            L"Host başlığını değiştir",       L"-r · Host → hoSt (yalnızca HTTP)" },
    { L"s",            L"Host boşluğunu kaldır",         L"-s · Host başlığındaki boşluğu siler (HTTP)" },
    { L"a",            L"Ek boşluk ekle",                L"-a · HTTP isteğine fazladan boşluk ekler; bazı siteleri bozabilir" },
    { L"m",            L"Host harflerini karıştır",      L"-m · test.com → tEsT.cOm (HTTP)" },
    { L"w",            L"Tüm portlarda HTTP ara",        L"-w · Yalnızca 80. port yerine her portta HTTP trafiği işlenir" },
    { L"n",            L"İlk ACK'i bekleme",             L"-n · Keep-alive parçalamada (-k) ilk segment onayını beklemez" },
    { L"wrong_chksum", L"Sahte paket: hatalı checksum",  L"--wrong-chksum · Sahte paketler bozuk sağlama toplamıyla gönderilir" },
    { L"wrong_seq",    L"Sahte paket: hatalı SEQ/ACK",   L"--wrong-seq · Sahte paketler geçmiş sıra numarasıyla gönderilir" },
    { L"native_frag",  L"Doğal parçalama",               L"--native-frag · Paketler küçültülmek yerine bölünerek gönderilir" },
    { L"reverse_frag", L"Ters sıralı parçalama",         L"--reverse-frag · Parçalar ters sırada gönderilir" },
    { L"allow_no_sni", L"SNI olmasa da uygula",          L"--allow-no-sni · Listeli modda SNI'siz bağlantılara da uygular" },
    { L"frag_by_sni",  L"SNI'den böl",                   L"--frag-by-sni · HTTPS paketi tam SNI konumundan bölünür" },
    { L"dns_verb",     L"Ayrıntılı DNS günlüğü",         L"--dns-verb · DNS yönlendirmesi için ayrıntılı çıktı" },
    { L"md5sig",       L"Sahte paket: MD5 imzası",       L"--md5sig · Sunucunun düşürdüğü TCP MD5 seçenekli sahte paketler de gönderilir" },
    { L"autolist",     L"Engelli siteleri kendisi öğrensin", L"--autolist · Bağlantısı kesilen siteler lists\\otomatik.txt'ye eklenir" },
    { L"split_pos",    L"Bölme noktaları",               L"--split-pos · Virgülle: 1, sni, sni+3, midsni, -10" },
    { L"seqovl",       L"Sıra örtüşmesi (bayt)",         L"--seqovl · İlk parçanın önüne DPI'ı şaşırtan çöp veri eklenir" },
    { L"udp_fake",     L"UDP sahte paket sayısı",        L"--udp-fake · Yeni UDP akışlarından önce gönderilir" },
    { L"udp_ports",    L"UDP portları",                  L"--udp-ports · Ör. 50000-65535, 3478" },
    { L"f",            L"HTTP parça boyutu",             L"-f · Boş bırakılırsa kullanılmaz" },
    { L"k",            L"Keep-alive parça boyutu",       L"-k · HTTP kalıcı bağlantı parçalaması" },
    { L"e",            L"HTTPS parça boyutu",            L"-e · TLS ClientHello parçalama boyutu" },
    { L"set_ttl",      L"Sahte paket TTL'i",             L"--set-ttl · Sahte paketler sağlayıcıya ulaşsın ama siteye ulaşmasın diye; Türkiye'de genelde 3-5" },
    { L"min_ttl",      L"En düşük TTL",                  L"--min-ttl · Bu mesafenin altında sahte paket gönderilmez" },
    { L"fake_gen",     L"Rastgele sahte paket sayısı",   L"--fake-gen · Rastgele SNI'li sahte paket üretir" },
    { L"fake_resend",  L"Sahte paket tekrarı",           L"--fake-resend · Her sahte paket kaç kez gönderilsin" },
    { L"ip_id",        L"IP ID'leri",                    L"--ip-id · Virgülle ayrılmış; bu ID'li paketler de işlenir" },
    { L"auto_ttl",     L"Otomatik TTL",                  L"--auto-ttl · Sahte paket TTL'ini sunucu mesafesine göre seçer" },
    { L"max_payload",  L"En büyük payload",              L"--max-payload · Bu boyuttan büyük paketlere dokunulmaz" },
    { L"port",         L"Ek portlar",                    L"--port · Virgülle ayrılmış, ör. 8080, 8443" },
    { L"fake_sni",     L"Sahte SNI",                     L"--fake-with-sni · Virgülle ayrılmış alan adları" },
    { L"fake_hex",     L"Sahte paket (hex)",             L"--fake-from-hex · Sahte paket içeriği" },
    { L"blacklist",    L"Liste dosyaları (elle)",     L"--blacklist · ';' ile ayrılmış, uygulama klasörüne göre dosya yolları" },
    { L"extra",        L"Ek komut seçenekleri",                 L"Motorun komutuna olduğu gibi eklenir" },
};

const wchar_t *g_preset_short[10] = {
    L"Yok", L"Yöntem 1 · En uyumlu", L"Yöntem 2", L"Yöntem 3", L"Yöntem 4", L"Yöntem 5 · Türkiye için önerilen",
    L"Yöntem 6", L"Yöntem 7", L"Yöntem 8", L"Yöntem 9 · En kapsamlı",
};

/* ---------------- yardımcılar ---------------- */

/* Windows 11 metin kutusu: dururken karttan biraz açık, odakta koyulaşır (açık temada beyazlaşır) */
COLORREF input_fill(BOOL focus)
{
    if (g_th.dark)
        return focus ? RGB(0x1F, 0x1F, 0x1F) : RGB(0x32, 0x32, 0x32);
    return focus ? RGB(0xFF, 0xFF, 0xFF) : RGB(0xFB, 0xFB, 0xFB);
}

static int field_index(const wchar_t *key)
{
    int i;

    for (i = 0; i < g_nfields; i++)
        if (!wcscmp(g_fields[i].key, key))
            return i;
    return -1;
}

static const wchar_t *ftitle(int fi)
{
    size_t i;

    for (i = 0; i < sizeof(g_fdesc) / sizeof(g_fdesc[0]); i++)
        if (!wcscmp(g_fdesc[i].key, g_fields[fi].key))
            return tr(g_fdesc[i].title);
    return g_fields[fi].label;
}

static const wchar_t *fdesc(int fi)
{
    size_t i;

    for (i = 0; i < sizeof(g_fdesc) / sizeof(g_fdesc[0]); i++)
        if (!wcscmp(g_fdesc[i].key, g_fields[fi].key))
            return tr(g_fdesc[i].desc);
    return L"";
}

#define FINT(p, f)  ((int *)((char *)(p) + (f)->off))
#define FSTR(p, f)  ((wchar_t *)((char *)(p) + (f)->off))
#define FSTR2(p, f) ((wchar_t *)((char *)(p) + (f)->off2))

/* 0 kapalı, 1 Discord sesi, 2 oyunlar, 3 özel port listesi */
int udp_mode(const Profile *p)
{
    if (!p->udp_fake[0] || !_wtoi(p->udp_fake))
        return 0;
    if (!p->udp_ports[0] || !wcscmp(p->udp_ports, UDP_DISCORD))
        return 1;
    if (!wcscmp(p->udp_ports, UDP_GAMES))
        return 2;
    return 3;
}

Profile *ep(void)
{
    if (g_ep < 0 || g_ep >= g_cfg.nprofiles)
        g_ep = 0;
    return &g_cfg.profiles[g_ep];
}

static BOOL ep_is_active(void)
{
    return !_wcsicmp(ep()->name, g_cfg.active);
}

static void copy_clip(const wchar_t *s)
{
    size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
    HGLOBAL h;

    if (!OpenClipboard(g_main))
        return;
    EmptyClipboard();
    h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h) {
        memcpy(GlobalLock(h), s, n);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

/* Onay penceresi: eylemi adıyla söyleyen tek buton + İptal */
static BOOL confirm(const wchar_t *main, const wchar_t *content, const wchar_t *action)
{
    TASKDIALOG_BUTTON b = { 100, action };
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    int btn = 0;

    tc.hwndParent = g_main;
    tc.hInstance = g_inst;
    tc.pszWindowTitle = APP_NAME;
    tc.pszMainInstruction = main;
    tc.pszContent = content;
    tc.cButtons = 1;
    tc.pButtons = &b;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.nDefaultButton = IDCANCEL;
    TaskDialogIndirect(&tc, &btn, NULL, NULL);
    return btn == 100;
}

void dns_value(wchar_t *out, size_t cap)
{
    if (g_cfg.dns_mode == DNS_OFF)
        _snwprintf(out, cap, tr(L"Windows varsayılanı"));
    else
        _snwprintf(out, cap, L"%s", tr(g_dns_presets[g_cfg.dns_preset].name));
    out[cap - 1] = 0;
}

static const wchar_t *dns_method_text(void)
{
    return g_cfg.dns_method == DNS_SYSTEM ? tr(L"Windows ayarı") : tr(L"Uygulama üzerinden");
}

void fmt_duration(DWORD ms, wchar_t *out, size_t cap)
{
    DWORD m = ms / 60000;

    if (m < 1)
        _snwprintf(out, cap, tr(L"%lu sn"), ms / 1000);
    else if (m < 60)
        _snwprintf(out, cap, tr(L"%lu dk"), m);
    else
        _snwprintf(out, cap, tr(L"%lu sa %lu dk"), m / 60, m % 60);
    out[cap - 1] = 0;
}

/* Kaydet / yeniden başlat (gecikmeli) */
static void changed(BOOL restart)
{
    g_pending_save = TRUE;
    SetTimer(g_content, T_SAVE, 400, NULL);
    if (restart) {
        g_pending_restart = TRUE;
        SetTimer(g_content, T_RESTART, 900, NULL);
    }
    app_refresh_state();
    InvalidateRect(g_main, NULL, FALSE);
}

void flush_pending(void)
{
    if (g_pending_save) {
        config_save(&g_cfg);
        g_pending_save = FALSE;
    }
    if (g_pending_restart) {
        g_pending_restart = FALSE;
        bypass_restart_if_running();
    }
}

/* Elle seçilen profil: hemen kullanılır ve bağlı ağ için hatırlanır, yoksa
   ağa göre profil bir sonraki bağlanışta eskisine döndürürdü. */
static void use_profile(const wchar_t *name)
{
    if (!_wcsicmp(name, g_cfg.active)) {
        rebuild(FALSE);
        return;
    }
    wcscpy(g_cfg.active, name);
    g_ep = config_find(&g_cfg, name);
    if (g_cfg.net_auto)
        app_net_remember(name);
    changed(TRUE);
    rebuild(TRUE);
}

/* ---- blacklist <-> liste dosyaları ---- */

static void list_rel(const wchar_t *name, wchar_t *out, size_t cap)
{
    _snwprintf(out, cap, L"lists\\%s", name);
    out[cap - 1] = 0;
}

BOOL bl_has(const Profile *p, const wchar_t *rel)
{
    const wchar_t *s = p->blacklist;
    wchar_t tok[MAX_PATH], a[MAX_PATH], b[MAX_PATH];

    app_path(b, MAX_PATH, rel);
    while (next_token(&s, tok, MAX_PATH, L";\r\n")) {
        app_path(a, MAX_PATH, tok);
        if (!_wcsicmp(a, b))
            return TRUE;
    }
    return FALSE;
}

static void bl_set(Profile *p, const wchar_t *rel, BOOL on)
{
    const wchar_t *s = p->blacklist;
    wchar_t tok[MAX_PATH], a[MAX_PATH], b[MAX_PATH], out[BIG_CAP];

    out[0] = 0;
    app_path(b, MAX_PATH, rel);
    while (next_token(&s, tok, MAX_PATH, L";\r\n")) {
        app_path(a, MAX_PATH, tok);
        if (!_wcsicmp(a, b))
            continue;
        if (out[0])
            wcsncat(out, L";", BIG_CAP - wcslen(out) - 1);
        wcsncat(out, tok, BIG_CAP - wcslen(out) - 1);
    }
    if (on) {
        if (out[0])
            wcsncat(out, L";", BIG_CAP - wcslen(out) - 1);
        wcsncat(out, rel, BIG_CAP - wcslen(out) - 1);
    }
    wcscpy(p->blacklist, out);
}

static int list_files(wchar_t names[][MAX_PATH], int max)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0, i, j;

    app_path(pat, MAX_PATH, L"lists");
    CreateDirectoryW(pat, NULL);
    app_path(pat, MAX_PATH, L"lists\\*.txt");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n < max)
            wcscpy(names[n++], fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && _wcsicmp(names[j - 1], names[j]) > 0; j--) {
            wchar_t t[MAX_PATH];
            wcscpy(t, names[j]);
            wcscpy(names[j], names[j - 1]);
            wcscpy(names[j - 1], t);
        }
    return n;
}

static int count_hosts(const wchar_t *name)
{
    wchar_t rel[MAX_PATH], path[MAX_PATH], line[512], *t;
    const wchar_t *s;
    int n = 0;

    list_rel(name, rel, MAX_PATH);
    app_path(path, MAX_PATH, rel);
    t = read_text_file(path);
    if (!t)
        return 0;
    s = t;
    while (next_token(&s, line, 512, L"\r\n"))
        n++;
    free(t);
    return n;
}

/* ---------------- satır kurucuları ---------------- */

static Row *add(int type, const wchar_t *title, const wchar_t *desc)
{
    Row *r = &g_rows[g_nrows < MAX_ROWS ? g_nrows++ : MAX_ROWS - 1];

    memset(r, 0, sizeof(*r));
    r->type = type;
    r->enabled = TRUE;
    if (title)
        wcsncpy(r->title, title, 95);
    if (desc)
        wcsncpy(r->desc, desc, 255);
    return r;
}

Row *title(const wchar_t *t, const wchar_t *sub)
{
    return add(T_TITLE, t, sub);
}

/* Liste boşken sayfanın ortasında: simge, tek cümle, eylem butonları */
static Row *empty_state(wchar_t icon, const wchar_t *t, const wchar_t *d, BOOL fill)
{
    Row *r = add(T_EMPTY, t, d);

    r->arg = icon;
    r->min_h = fill ? -1 : 0;
    return r;
}

static void section(const wchar_t *t, const wchar_t *d)
{
    add(T_SECTION, t, d);
}

Row *toggle(int id, const wchar_t *t, const wchar_t *d, int *p)
{
    Row *r = add(T_TOGGLE, t, d);

    r->id = id;
    r->pint = p;
    return r;
}

static Row *drop(int id, const wchar_t *t, const wchar_t *d, const wchar_t *v)
{
    Row *r = add(T_DROP, t, d);

    r->id = id;
    wcsncpy(r->value, v, 1023);
    return r;
}

void btn(Row *r, int id, const wchar_t *label, int kind, BOOL enabled)
{
    Btn *b;

    if (r->nbtn >= MAX_BTN)
        return;
    b = &r->btn[r->nbtn++];
    b->id = id;
    b->kind = kind;
    b->enabled = enabled;
    wcsncpy(b->label, label, 31);
}

static Row *buttons(const wchar_t *t, const wchar_t *d)
{
    return add(T_BUTTONS, t, d);
}

/* Satır başlığının yanına ⓘ: tıklayınca açıklama baloncuğu */
static Row *info(Row *r, const wchar_t *text)
{
    r->info = text;
    return r;
}

Row *edit(int id, const wchar_t *t, const wchar_t *d, wchar_t *buf, int cap, int ctl_w, DWORD style)
{
    Row *r = add(T_EDIT, t, d);

    r->id = id;
    r->ebuf = buf;
    r->ecap = cap;
    r->ctl_w = ctl_w;
    r->estyle = style;
    return r;
}

/* "2 dk önce" */
static void age_text(DWORD when, wchar_t *out, size_t cap)
{
    DWORD s = (GetTickCount() - when) / 1000;

    if (s < 10)
        _snwprintf(out, cap, tr(L"az önce"));
    else if (s < 60)
        _snwprintf(out, cap, tr(L"%lu sn önce"), s);
    else if (s < 3600)
        _snwprintf(out, cap, tr(L"%lu dk önce"), s / 60);
    else
        _snwprintf(out, cap, tr(L"%lu sa önce"), s / 3600);
    out[cap - 1] = 0;
}

/* Panodaki adres (tarayıcıdan kopyalanmış) site kutusunu bir kez doldurur */
wchar_t g_site_input[256];
BOOL g_site_clip;

static void site_from_clipboard(void)
{
    HANDLE h;
    const wchar_t *t;
    wchar_t host[128];

    g_site_clip = TRUE;
    if (g_site_input[0] || !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_main))
        return;
    h = GetClipboardData(CF_UNICODETEXT);
    t = h ? GlobalLock(h) : NULL;
    if (t) {
        if (wcslen(t) < 250 && wcsstr(t, L"://") && !wcschr(t, L'\n') && host_normalize(t, host, 128))
            wcscpy(g_site_input, host);
        GlobalUnlock(h);
    }
    CloseClipboard();
}

/* ---------------- sayfalar ---------------- */

void page_home(void)
{
    wchar_t d[128], dsc[200];
    Row *r;
    int i;

    title(tr(L"Durum"), NULL);              /* alt satır çizimde canlı hesaplanır */
    add(T_STATUS, NULL, NULL)->id = A_BYPASS;
    add(T_KPIS, NULL, NULL);                /* Profil ve DNS kutucukları tıklanınca seçim açılır */
    if (app_state() == ICON_ERROR && !app_tune_state(NULL, NULL)) {
        r = buttons(tr(L"Koruma başlatılamadı"), tr(L"Yeniden dene; sorun sürerse günlükteki ayrıntılara bak"));
        btn(r, A_RETRY, tr(L"Yeniden dene"), B_PRIMARY, TRUE);
        btn(r, A_OPEN_LOG, tr(L"Günlüğü aç"), B_SECONDARY, TRUE);
    }
    if (share_status(NULL, 0) == SHARE_ERROR) {
        r = buttons(tr(L"Telefonla paylaşım açılamadı"), tr(L"Paylaşım ayarlarını kontrol edip yeniden deneyebilirsin"));
        btn(r, A_SHARE_SETTINGS, tr(L"Paylaşım ayarları"), B_SECONDARY, TRUE);
    }

    /* izlenen siteler */
    {
        const Health *h = health_get();

        section(tr(L"Siteler"), NULL);
        if (!h->when) {
            r = buttons(h->running ? tr(L"Denetleniyor…") : tr(L"Henüz denetlenmedi"),
                        bypass_running() ? tr(L"Koruma açıkken siteler 5 dakikada bir denetlenir")
                                         : tr(L"Koruma kapalı"));
        } else {
            for (i = 0; i < h->n; i++) {
                if (h->s[i].ok == SITE_OK)
                    _snwprintf(dsc, 200, tr(L"✓ Açılıyor · %lu ms"), h->s[i].ms);
                else
                    wcscpy(dsc, tr(L"✗ Açılmıyor"));
                buttons(h->s[i].host, dsc);
            }
            age_text(h->when, d, 128);
            _snwprintf(dsc, 200, h->running ? tr(L"Denetleniyor…") : tr(L"Son denetim: %s"), d);
            r = buttons(tr(L"Denetim"), dsc);
        }
        btn(r, A_HEALTH_CHECK, tr(L"Şimdi denetle"), B_SECONDARY, bypass_running() && !h->running);
        info(r, tr(L"İzlenen siteler: Genel sayfasındaki test sitesi ve kullandığın listelerin ilk siteleri. Koruma açıkken "
                   L"5 dakikada bir bağlanmayı dener; sonuç saatin yanındaki panelde de görünür."));
        info(toggle(A_APP_BOOL, tr(L"Bozulunca kendisi onarsın"),
                    tr(L"Siteler açılmaz olursa çalışan profili kendisi bulur ve geçer"), &g_cfg.auto_heal),
             tr(L"İnternet sağlayıcılar engelleme yöntemlerini zaman zaman değiştirir. Bu açıkken izlenen sitelerin hiçbiri "
                L"iki denetim üst üste açılmazsa (ve internetin çalışıyorsa) TunnelDPI profilleri tek tek dener, çalışana "
                L"geçer ve sana bildirir. En fazla yarım saatte bir yapılır."));
    }

    section(tr(L"Bir site açılmıyor mu?"), NULL);
    if (!g_site_clip)
        site_from_clipboard();
    {
        const DiagResult *dg = diag_get();
        BOOL busy = app_site_fix_busy();

        r = edit(A_SITE_HOST, tr(L"Site adresi"),
                 busy ? tr(L"Deneniyor; sonucu bildirim olarak göreceksin")
                      : tr(L"Açılmayan sitenin adresini yaz ya da tarayıcıdan yapıştır"),
                 g_site_input, 256, 0, 0);
        r->cue = tr(L"ör. discord.com");
        btn(r, A_SITE_FIX, busy ? tr(L"Deneniyor…") : tr(L"Açılmasını sağla"), B_PRIMARY, !busy && !app_tune_state(NULL, NULL));
        btn(r, A_DIAG, dg->state == DIAG_RUNNING ? tr(L"İnceleniyor") : tr(L"İncele"), B_SECONDARY,
            dg->state != DIAG_RUNNING);
        info(r, tr(L"\"Açılmasını sağla\" siteyi listene ekler ve dener. Açılmazsa profilleri tek tek dener; çalışan profil "
                   L"başkaysa yalnızca bu site için bir site kuralı oluşturur, diğer siteler bugünkü profille devam eder.\n\n"
                   L"\"İncele\" engelin nereden geldiğini söyler: site adı mı yanlış çözülüyor (DNS), yoksa bağlantı mı "
                   L"kesiliyor. Kutu boşsa test sitesi incelenir."));
    }
    {
        int ti, tn;
        TuneProgress progress;
        wchar_t td[160];
        app_tune_progress(&progress);

        if (app_tune_state(&ti, &tn)) {
            _snwprintf(td, 160, tr(L"%d/%d tamamlandı · %d kaldı · %d çalıştı"), progress.completed,
                       progress.total, progress.total - progress.completed, progress.passed);
            r = buttons(tr(L"En uygun profili bul"), td);
            btn(r, A_TUNE, tr(L"Durdur"), B_SECONDARY, TRUE);
        } else {
            r = buttons(tr(L"En uygun profili bul"), tr(L"Yöntemleri tek tek dener, test sitesini açanı seçer"));
            btn(r, A_TUNE, tr(L"Hızlı ara"), B_PRIMARY, TRUE);
            btn(r, A_TUNE_DEEP, tr(L"Derin tara"), B_SECONDARY, TRUE);
        }
        info(r, tr(L"Hızlı ara: kayıtlı profillerini sırayla dener ve test sitesini en hızlı açanı seçer, yarım dakika sürer.\n\n"
                   L"Derin tara: 20 kadar yöntem birleşimini dener, birkaç dakika sürer. Çalışan yöntemi yeni bir profil "
                   L"olarak kaydeder. Hızlı arama işe yaramazsa bunu dene.\n\nBulunan profil bağlı olduğun ağ için hatırlanır."));
        if (progress.running) {
            _snwprintf(td, 160, tr(L"%s · %s"), app_tune_name(), progress.testing ? tr(L"Bağlantı deneniyor") : tr(L"Yöntem hazırlanıyor"));
            add(T_NOTICE, NULL, td);
        } else if (progress.summary[0]) {
            add(T_NOTICE, NULL, progress.summary);
        }
    }

    /* son teşhisin sonucu */
    {
        const DiagResult *dg = diag_get();
        wchar_t buf[700];

        if (dg->state == DIAG_DONE) {
            section(dg->verdict, dg->advice);
            _snwprintf(buf, 700, tr(L"Site         %s\nSistem DNS   %s\nDoH          %s\nHTTPS        %s"),
                       dg->host, dg->sys[0] ? dg->sys : tr(L"çözülemedi"), dg->doh[0] ? dg->doh : tr(L"ulaşılamadı"),
                       dg->https_ok ? tr(L"bağlandı") : tr(L"bağlantı kurulamadı"));
            r = add(T_CODE, NULL, NULL);
            wcsncpy(r->value, buf, 1023);
            r->mono = TRUE;
        }
    }
}

void page_profiles(void)
{
    Profile *p = ep();
    wchar_t files[32][MAX_PATH], d[128], rel[MAX_PATH], sub[160];
    int nf, i;
    Row *r;

    _snwprintf(sub, 160, tr(L"%d profil · kullanılan: %s"), g_cfg.nprofiles, g_cfg.active);
    title(tr(L"Profiller"), sub);

    r = drop(A_EP_SELECT, tr(L"Profil"), tr(L"Seçtiğin profil hemen kullanılır; aşağıdaki ayarlar onundur"), p->name);
    btn(r, A_EP_MENU, L"\xE712", B_SECONDARY, TRUE);     /* ⋯ yeniden adlandır, kopyala, sil… */
    if (g_ep_rename)
        edit(A_EP_NAME, tr(L"Yeni ad"), tr(L"Başka yere tıklayınca kaydedilir"), p->name, NAME_CAP, 280, 0);
    if (isp_name()[0]) {
        const wchar_t *rec[8];
        wchar_t t[128], d[256];
        int nr = isp_profiles(rec, 8), k;

        _snwprintf(t, 128, tr(L"İnternet sağlayıcın: %s"), isp_name());
        t[127] = 0;
        if (nr) {
            _snwprintf(d, 256, tr(L"Bu sağlayıcıda önerilen: %s"), rec[0]);
            for (k = 1; k < nr && wcslen(d) < 200; k++) {
                wcscat(d, L", ");
                wcscat(d, rec[k]);
            }
        } else {
            wcscpy(d, tr(L"Bu sağlayıcı için öneri yok; \"En uygun profili bul\" çalışanı bulur"));
        }
        r = buttons(t, d);
        if (nr && _wcsicmp(rec[0], g_cfg.active))
            btn(r, A_ISP_USE, tr(L"Önerileni kullan"), B_SECONDARY, TRUE);
        info(r, tr(L"Sağlayıcın, internet bağlantının genel adresinden RIPE NCC kayıtlarına bakılarak bulunur. Hangi "
                   L"sağlayıcıda hangi profilin çalıştığı, TunnelDPI deposundaki imzalı hazır ayar dosyasından gelir ve "
                   L"günde bir güncellenir. VPN açıksa VPN'in sağlayıcısı görünür."));
    }

    section(tr(L"Yöntem"), NULL);
    info(drop(A_EP_ENGINE, tr(L"Motor"), p->engine == ENGINE_ZAPRET ? tr(L"Gelişmiş: daha çok yöntem; Discord sesi ve zorlu engellerde güçlü")
                                                                    : tr(L"Klasik: hafif ve hızlı, çoğu engel için yeterli"),
              p->engine == ENGINE_ZAPRET ? tr(L"Gelişmiş (zapret)") : tr(L"Klasik")),
         tr(L"Klasik motor az bellek kullanır ve çoğu engeli aşar; reklam engelleme, uygulama filtresi ve site kuralları "
            L"yalnızca onunla çalışır.\n\nGelişmiş motor (zapret) daha çok yöntem dener; Discord sesi ya da klasik motorun "
            L"aşamadığı engeller için."));
    if (p->engine == ENGINE_ZAPRET) {
        wchar_t zexe[MAX_PATH];

        app_path(zexe, MAX_PATH, ZAPRET_EXE);
        if (!file_exists(zexe))
            add(T_NOTICE, NULL, tr(L"Gelişmiş motorun dosyaları eksik. TunnelDPI'ı yeniden kurmayı dene."));
        section(tr(L"Motor komutu"), tr(L"Uzmanlar için. %HOSTLIST% seçili site listeleriyle değişir; liste yoksa tüm sitelere uygulanır. "
                                             L"Uygulamalar sayfası ve \"Uygulama üzerinden\" DNS bu motorda çalışmaz."));
        r = add(T_AREA, NULL, NULL);
        r->id = A_EP_ZAPRET;
        r->ebuf = p->zapret;
        r->ecap = ZAPRET_CAP;
        r->mono = TRUE;
        r->estyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
        goto lists;
    }
    info(drop(A_EP_PRESET, tr(L"Hazır ayar"), tr(L"Temel atlatma ayarı; aşağıdaki seçenekler bunun üzerine eklenir"),
              tr(g_preset_short[p->preset])),
         tr(L"GoodbyeDPI'ın -1 ile -9 arası hazır yöntemleri. -1 en uyumlu ama en zayıfı, -9 en kapsamlısı. Türkiye'de "
            L"genelde -5 çalışır."));
    info(drop(A_EP_TTL, tr(L"Sahte paket ömrü (TTL)"), tr(L"Türkiye'de genelde 5 ya da Otomatik iyi çalışır"),
              p->set_ttl[0] ? p->set_ttl : p->auto_ttl ? tr(L"Otomatik") : tr(L"Hazır ayara göre")),
         tr(L"Engelleyiciyi şaşırtmak için sahte paketler gönderilir. Bu sayı, sahte paketin kaç ağ cihazından geçtikten "
            L"sonra yok olacağını belirler: engelleyiciye ulaşmalı ama siteye ulaşmamalı. Siteler açılıyor ama bazıları "
            L"bozuk geliyorsa sayıyı küçült; hiç açılmıyorsa büyüt."));
    i = field_index(L"q");
    r = toggle(A_EP_FIELD, ftitle(i), tr(L"Bazı sitelerde engeli aşmayı kolaylaştırır"), FINT(p, &g_fields[i]));
    r->arg = i;
    info(r, tr(L"HTTP/3 (QUIC) bağlantıları bu yöntemle değiştirilemez. Kapatınca tarayıcılar normal HTTPS'e döner ve "
               L"engel aşılabilir. Hız farkı fark edilmez."));

lists:
    section(tr(L"Site listeleri"), p->blacklist[0] ? tr(L"Koruma yalnızca seçili listelerdeki sitelere uygulanır.")
                                               : tr(L"Liste seçilmedi; koruma tüm sitelere uygulanır."));
    nf = list_files(files, 32);
    if (!nf) {
        r = buttons(tr(L"Henüz liste yok"), tr(L"Bir liste oluştur, sonra buradan bu profile ekle"));
        btn(r, A_EP_MAKELIST, tr(L"Liste oluştur"), B_SECONDARY, TRUE);
    }
    for (i = 0; i < nf; i++) {
        if (!_wcsicmp(files[i], AUTOLIST_NAME))
            continue;                       /* öğrenilen liste aşağıda ayrıca gösterilir */
        list_rel(files[i], rel, MAX_PATH);
        _snwprintf(d, 128, tr(L"%d site"), count_hosts(files[i]));
        r = toggle(A_EP_LIST, files[i], d, NULL);
        r->arg = i;
        wcsncpy(r->value, rel, 1023);
    }
    i = field_index(L"autolist");
    r = toggle(A_EP_FIELD, tr(L"Şüpheli bağlantıları kaydet"),
               tr(L"Deneysel: adayları kaydeder; engelli olduklarını varsaymaz ve otomatik uygulamaz"),
               FINT(p, &g_fields[i]));
    r->arg = i;
    info(r, tr(L"Bağlantının kesilmesi DPI engeli olduğunu kanıtlamaz. Tekrar gönderilen paketler engel sayılmaz; yanıt veren sunucular aday yapılmaz. "
               L"Adaylar ayrı tutulur ve korumaya eklenmez. Onaylı listeye yalnızca engelli bir ağda kendin doğruladığın siteleri yaz. "
               L"Eski otomatik keşif listesi korunur, ancak bu özellik tarafından artık kullanılmaz."));
    if (p->autolist) {
        int learned = count_hosts(AUTOLIST_NAME);
        if (learned > 0)
            _snwprintf(d, 128, tr(L"%d site elle onaylandı"), learned);
        else
            wcscpy(d, tr(L"Onaylı liste boş; adaylar otomatik eklenmez"));
        r = buttons(tr(L"Elle onaylanan siteler"), d);
        btn(r, A_AUTO_SHOW, tr(L"Düzenle"), B_SECONDARY, TRUE);
        btn(r, A_AUTO_CLEAR, tr(L"Temizle"), B_SECONDARY, learned > 0);
        r = buttons(tr(L"Doğrulanmamış adaylar"), tr(L"Bu liste erişim engeli kanıtı değildir"));
        btn(r, A_AUTO_CANDIDATES, tr(L"Göster"), B_SECONDARY, TRUE);
        btn(r, A_AUTO_LEGACY, tr(L"Eski liste"), B_SECONDARY, TRUE);
    }

    if (p->engine == ENGINE_ZAPRET)
        goto networks;
    section(tr(L"Ses ve oyunlar"), tr(L"Sesli sohbet ve oyun bağlantıları için ek koruma."));
    r = toggle(A_EP_UDP, tr(L"Discord sesli sohbet"), tr(L"Discord'da sesli kanala bağlanamıyorsan aç"), NULL);
    r->arg = 1;
    r = toggle(A_EP_UDP, tr(L"Oyunlar ve diğer sesli uygulamalar"), tr(L"Daha geniş kapsamlı; Discord sesini de içerir"), NULL);
    r->arg = 2;

networks:
    section(tr(L"Ağlar"), tr(L"Ev, iş ya da telefon gibi her ağ için ayrı profil kaydedebilirsin; o ağa bağlanınca profil "
                             L"kendiliğinden seçilir."));
    info(toggle(A_APP_BOOL, tr(L"Ağa göre profil değiştir"), NULL, &g_cfg.net_auto),
         tr(L"Ağlar modem adresiyle tanınır. Bir profil seçtiğinde o an bağlı olduğun ağ için hatırlanır; o ağa yeniden "
            L"bağlanınca kendiliğinden seçilir."));
    {
        wchar_t key[24], name[64], t[128], dsc[160];
        int k, cur = -1;

        if (netid_current(key, 24, name, 64)) {
            for (k = 0; k < g_cfg.nnets; k++)
                if (!_wcsicmp(g_cfg.nets[k].key, key))
                    cur = k;
            _snwprintf(t, 128, tr(L"Bu ağ: %s"), name);
            if (cur >= 0)
                _snwprintf(dsc, 160, tr(L"Kayıtlı profil: %s"), g_cfg.nets[cur].profile);
            else
                wcscpy(dsc, tr(L"Bu ağ için kayıtlı profil yok"));
            r = buttons(t, dsc);
            btn(r, A_NET_REMEMBER, tr(L"Bu profili hatırla"), B_SECONDARY,
                cur < 0 || _wcsicmp(g_cfg.nets[cur].profile, p->name));
        }
        for (k = 0; k < g_cfg.nnets; k++) {
            if (k == cur)
                continue;
            _snwprintf(dsc, 160, tr(L"Profil: %s"), g_cfg.nets[k].profile);
            r = buttons(g_cfg.nets[k].name[0] ? g_cfg.nets[k].name : g_cfg.nets[k].key, dsc);
            r->arg = k;
            btn(r, A_NET_FORGET, tr(L"Unut"), B_SECONDARY, TRUE);
        }
    }


    if (p->engine == ENGINE_ZAPRET)
        return;
    section(tr(L"Gelişmiş"), NULL);
    r = add(T_EXPANDER, tr(L"Tüm motor seçenekleri"), tr(L"Uzmanlar için: parçalama, sahte paket ve diğer ayarlar"));
    r->id = A_EP_ADV;
    r->pint = &g_adv_open;
    if (g_adv_open) {
        for (i = 0; i < g_nfields; i++) {
            const Field *f = &g_fields[i];

            if (!wcscmp(f->key, L"set_ttl") || !wcscmp(f->key, L"q") || !wcscmp(f->key, L"autolist"))
                continue;
            switch (f->type) {
            case F_BOOL:
                r = toggle(A_EP_FIELD, ftitle(i), fdesc(i), FINT(p, f));
                r->arg = i;
                break;
            case F_NUM:
                r = edit(A_EP_FIELD, ftitle(i), fdesc(i), FSTR(p, f), f->cap, 96, ES_NUMBER);
                r->arg = i;
                break;
            case F_OPT:
                r = toggle(A_EP_FIELD, ftitle(i), fdesc(i), FINT(p, f));
                r->arg = i;
                if (*FINT(p, f)) {
                    _snwprintf(d, 128, tr(L"%s değeri"), ftitle(i));
                    r = edit(A_EP_FIELD_VALUE, d, tr(L"Boş bırakılırsa varsayılan"), FSTR2(p, f), f->cap, 140, 0);
                    r->arg = i;
                }
                break;
            default:
                r = edit(A_EP_FIELD, ftitle(i), fdesc(i), FSTR(p, f), f->cap,
                         !wcscmp(f->key, L"ip_id") ? 220 : 0, 0);
                r->arg = i;
                break;
            }
        }
        r = add(T_CODE, NULL, NULL);
        r->id = A_EP_COPYCMD;
        r->mono = TRUE;
        btn(r, A_EP_COPYCMD, tr(L"Kopyala"), B_SECONDARY, TRUE);
    }
}

void page_dns(void)
{
    wchar_t d[160];
    Row *r;

    dns_value(d, 160);
    if (g_cfg.dns_mode != DNS_OFF) {
        wcsncat(d, L" · ", 159 - wcslen(d));
        wcsncat(d, dns_method_text(), 159 - wcslen(d));
    }
    title(tr(L"DNS"), d);

    dns_value(d, 160);
    drop(A_DNS_SERVER, tr(L"Sunucu"), tr(L"Site adreslerini çözen sunucu"), d);
    info(drop(A_DNS_METHOD, tr(L"Yöntem"),
              g_cfg.dns_method == DNS_SYSTEM ? tr(L"Windows'un DNS ayarı değişir; koruma kapanınca eski haline döner")
                                             : tr(L"DNS istekleri uygulama tarafından yönlendirilir; Windows ayarlarına dokunulmaz"),
              dns_method_text()),
         tr(L"Uygulama üzerinden: DNS istekleri koruma açıkken seçtiğin sunucuya yönlendirilir; Windows ayarları değişmez. "
            L"Sağlayıcılar 53. porta müdahale edebildiği için başka port kullanan Yandex, Quad9, AdGuard ya da OpenDNS'i seç.\n\n"
            L"Windows ayarı: ağ bağdaştırıcının DNS'i değiştirilir, şifreli DNS kullanılabilir. Koruma kapanınca eski hale döner."));
    r = toggle(A_DNS_DOH, tr(L"Şifreli DNS (DoH)"), g_cfg.dns_method == DNS_SYSTEM ? tr(L"İnternet sağlayıcın hangi sitelere girdiğini DNS'ten göremez (Windows 11)")
                                                                            : tr(L"Yalnızca \"Windows ayarı\" yönteminde"),
               &g_cfg.dns_doh);
    r->enabled = g_cfg.dns_method == DNS_SYSTEM;
    info(r, tr(L"DNS istekleri şifreli gönderilir; sağlayıcın onları göremez ve değiştiremez. Windows 11 gerekir."));
    info(toggle(A_APP_BOOL, tr(L"Yanıt vermezse yedeğe geç"), tr(L"Seçili sunucu çalışmazsa en hızlı çalışan sunucuya geçer"),
                &g_cfg.dns_failover),
         tr(L"Koruma açıkken seçili DNS sunucusu 5 dakikada bir yoklanır. İki kez üst üste yanıt vermezse tüm sunucular "
            L"ölçülür, en hızlı çalışana geçilir ve sana bildirilir."));

    section(tr(L"Hız"), NULL);
    {
        const DnsBench *b = dnsbench_get();
        int k;
        BOOL any = FALSE;

        for (k = 0; k < b->n; k++)
            if (b->ms[k] != -2)
                any = TRUE;
        r = buttons(tr(L"En hızlı sunucuyu bul"), b->running ? tr(L"Ölçülüyor…")
                                                             : tr(L"Her sunucunun yanıt süresini ölçer"));
        btn(r, A_DNS_BENCH, tr(L"Ölç"), B_SECONDARY, !b->running);
        info(r, tr(L"Her sunucuya üç kez sorgu gönderilir ve en iyi süre gösterilir. Düşük süre sitelerin daha hızlı "
                   L"açılmaya başlaması demektir; 10-20 ms farkı hissedilmez."));
        for (k = 0; any && k < b->n; k++) {
            if (b->ms[k] == -2)
                wcscpy(d, tr(L"ölçülüyor…"));
            else if (b->ms[k] < 0)
                wcscpy(d, tr(L"yanıt vermedi"));
            else
                _snwprintf(d, 160, k == b->best && !b->running ? tr(L"%d ms · en hızlı") : tr(L"%d ms"), b->ms[k]);
            r = buttons(tr(g_dns_presets[k].name), d);
            r->arg = k;
            if (b->ms[k] >= 0)
                btn(r, A_DNS_BEST, g_cfg.dns_mode != DNS_OFF && g_cfg.dns_preset == k ? tr(L"Kullanılıyor") : tr(L"Kullan"),
                    k == b->best ? B_PRIMARY : B_SECONDARY, g_cfg.dns_mode == DNS_OFF || g_cfg.dns_preset != k);
        }
    }

    if (g_cfg.dns_preset == DNS_PRESET_CUSTOM) {
        section(tr(L"Özel sunucu"), NULL);
        edit(A_DNS_CUSTOM, tr(L"IPv4 adresleri"), tr(L"Virgülle ayrılmış"), g_cfg.dns_v4, MID_CAP, 260, 0);
        edit(A_DNS_CUSTOM, tr(L"IPv6 adresleri"), tr(L"İsteğe bağlı"), g_cfg.dns_v6, MID_CAP, 260, 0);
        if (g_cfg.dns_method == DNS_REDIRECT)
            edit(A_DNS_CUSTOM, tr(L"Port"), tr(L"Boş bırakırsan 53"), g_cfg.dns_port, SMALL_CAP, 96, ES_NUMBER);
        else if (g_cfg.dns_doh)
            edit(A_DNS_CUSTOM, tr(L"Şifreli DNS adresi"), tr(L"ör. https://dns.example/dns-query"),
                 g_cfg.dns_doh_tpl, MID_CAP * 2, 0, 0);
    }
    if (g_cfg.dns_mode == DNS_OFF)
        add(T_NOTICE, NULL, tr(L"Sağlayıcının DNS'i engelli siteleri yanlış adrese çevirebilir; yukarıdan bir sunucu seç."));
    else if (g_cfg.dns_method == DNS_SYSTEM && !g_cfg.dns_doh)
        add(T_NOTICE, NULL, tr(L"Şifresiz DNS'e sağlayıcın müdahale edebilir; şifreli DNS'i aç."));

    /* tarayıcıların kendi Güvenli DNS'i */
    {
        const BrowserDns *bd;
        int nb = browsers_dns(&bd), k, own = 0;

        if (nb) {
            section(tr(L"Tarayıcılar"), NULL);
            for (k = 0; k < nb; k++) {
                if (bd[k].state == BDNS_ON) {
                    own++;
                    if (bd[k].server[0])
                        _snwprintf(d, 160, tr(L"Güvenli DNS açık (%s): site adlarını kendisi çözer"), bd[k].server);
                    else
                        wcscpy(d, tr(L"Güvenli DNS açık: site adlarını kendisi çözer"));
                } else if (bd[k].state == BDNS_AUTO) {
                    wcscpy(d, tr(L"Güvenli DNS otomatik: buradaki DNS ayarı kullanılır"));
                } else {
                    wcscpy(d, tr(L"Güvenli DNS kapalı: buradaki DNS ayarı kullanılır"));
                }
                r = buttons(bd[k].name, d);
                if (k == 0)
                    info(r, tr(L"Tarayıcıların çoğunda \"Güvenli DNS\" (DNS over HTTPS) ayarı vardır. Açıkken tarayıcı site "
                               L"adlarını kendi seçtiği sunucuya şifreli sorar: DNS engelleri o tarayıcıda zaten aşılır, ama "
                               L"burada seçtiğin DNS o tarayıcıda kullanılmaz. Diğer programlar (Discord, oyunlar) yine "
                               L"buradaki ayarı kullanır.\n\nDNS engeli yaşıyorsan tarayıcının ayarlarında Gizlilik ve "
                               L"güvenlik → Güvenli DNS'i açmak da işe yarar; ECH destekleyen sitelerde engel tamamen kalkabilir."));
            }
            if (own && g_cfg.dns_mode != DNS_OFF)
                add(T_NOTICE, NULL, tr(L"Güvenli DNS'i açık olan tarayıcılar buradaki DNS sunucusunu kullanmaz. Bu sorun "
                                       L"değildir; yalnızca DNS değişikliğinin o tarayıcıda etkisi olmaz."));
        }
    }
}

void list_load(void)
{
    wchar_t rel[MAX_PATH], path[MAX_PATH], *t;
    size_t n;

    free(g_list_buf);
    g_list_buf = NULL;
    if (!g_list_name[0])
        return;
    list_rel(g_list_name, rel, MAX_PATH);
    app_path(path, MAX_PATH, rel);
    t = read_text_file(path);
    g_list_buf = calloc(64 * 1024, sizeof(wchar_t));
    if (g_list_buf && t)
        wcsncpy(g_list_buf, t, 64 * 1024 - 1);
    free(t);
    wcscpy(g_list_rename, g_list_name);
    n = wcslen(g_list_rename);
    if (n > 4 && !_wcsicmp(g_list_rename + n - 4, L".txt"))
        g_list_rename[n - 4] = 0;
}

void page_lists(void)
{
    wchar_t files[32][MAX_PATH], sub[128];
    int nf = list_files(files, 32), i;
    BOOL found = FALSE;
    Row *r;

    for (i = 0; i < nf; i++)
        if (!_wcsicmp(files[i], g_list_name))
            found = TRUE;
    if (!found) {
        wcscpy(g_list_name, nf ? files[0] : L"");
        list_load();
    } else if (!g_list_buf) {
        list_load();
    }

    _snwprintf(sub, 128, tr(L"%d liste"), nf);
    r = title(tr(L"Site listeleri"), sub);
    if (!nf) {
        r = empty_state(IC_LIST, tr(L"Henüz liste yok"), tr(L"Engeli aşılacak siteleri bir listeye yaz; her satıra bir site"), TRUE);
        btn(r, A_LIST_TR, tr(L"Türkiye listesini ekle"), B_PRIMARY, TRUE);
        btn(r, A_LIST_NEW, tr(L"Boş liste"), B_SECONDARY, TRUE);
        return;
    }
    btn(r, A_LIST_NEW, tr(L"Yeni liste"), B_SECONDARY, TRUE);
    r = drop(A_LIST_SELECT, tr(L"Liste"), NULL, g_list_name);
    btn(r, A_LIST_MENU, L"\xE712", B_SECONDARY, TRUE);
    if (g_list_renaming)
        edit(A_LIST_NAME, tr(L"Yeni ad"), tr(L"Başka yere tıklayınca kaydedilir"), g_list_rename, MAX_PATH - 8, 240, 0);
    {
        wchar_t rel[MAX_PATH];
        int ri;
        BOOL zap = config_active(&g_cfg)->engine != ENGINE_CLASSIC;

        list_rel(g_list_name, rel, MAX_PATH);
        ri = rule_find(&g_cfg, rel);
        r = drop(A_LIST_RULE, tr(L"Bu sitelerde kullanılacak profil"),
                 zap && ri >= 0 ? tr(L"Etkin profil gelişmiş motorlu; kural şu an uygulanmıyor")
                                : tr(L"Bu listedeki siteler başka bir yöntem istiyorsa seç"),
                 ri >= 0 ? g_cfg.rules[ri].profile : tr(L"Etkin profil"));
        info(r, tr(L"Site kuralı: bu listedeki siteler seçtiğin profille, diğer tüm siteler etkin profille açılır. Örneğin "
                   L"Discord bir yöntemle, başka bir site başka bir yöntemle açılıyorsa ikisini birlikte kullanabilirsin.\n\n"
                   L"Yalnızca klasik motorlu profillerde çalışır. Durum sayfasındaki \"Açılmasını sağla\" kuralları "
                   L"kendisi de oluşturur."));
    }
    {
        LONG64 up = subs_updated(g_list_name);
        wchar_t dsc[160];

        subs_get_url(g_list_name, g_sub_url, 1024);
        if (!g_sub_url[0]) {
            wcscpy(dsc, tr(L"Listeyi bir internet adresinden güncel tutmak için; boş bırakırsan kendin düzenlersin"));
        } else if (subs_running()) {
            wcscpy(dsc, tr(L"Güncelleniyor…"));
        } else if (up > 0) {
            LONG64 ago = (LONG64)time(NULL) - up;
            if (ago < 3600)
                _snwprintf(dsc, 160, tr(L"Günde bir güncellenir · son: %lld dk önce"), (long long)(ago / 60));
            else if (ago < 86400)
                _snwprintf(dsc, 160, tr(L"Günde bir güncellenir · son: %lld sa önce"), (long long)(ago / 3600));
            else
                _snwprintf(dsc, 160, tr(L"Günde bir güncellenir · son: %lld gün önce"), (long long)(ago / 86400));
        } else {
            wcscpy(dsc, tr(L"Henüz indirilmedi"));
        }
        r = edit(A_SUB_URL, tr(L"Güncelleme adresi"), dsc, g_sub_url, 1024, 0, 0);
        btn(r, A_SUB_UPDATE, tr(L"Şimdi güncelle"), B_SECONDARY, g_sub_url[0] && !subs_running());
    }

    if (g_list_buf) {
        _snwprintf(sub, 128, tr(L"%d site. Alt adresler de kapsanır; değişiklikler kendiliğinden kaydedilir."),
                   count_hosts(g_list_name));
        section(tr(L"Siteler"), sub);
        r = add(T_AREA, NULL, NULL);
        r->id = A_LIST_TEXT;
        r->ebuf = g_list_buf;
        r->ecap = 64 * 1024;
        r->mono = TRUE;
        r->min_h = -1;
        r->estyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    }
}

void page_app(void)
{
    static const wchar_t *themes[3] = { L"Sistem", L"Açık tema", L"Koyu tema" };
    BOOL svc = legacy_service_exists();
    wchar_t buf[64];
    Row *r;

    _snwprintf(buf, 64, tr(L"Sürüm %s"), APP_VERSION);
    title(tr(L"Genel"), buf);

    section(tr(L"Davranış"), NULL);
    toggle(A_AUTOSTART, tr(L"Windows açılışında başlat"), tr(L"Bilgisayarı açınca kendiliğinden, onay sormadan başlar"), NULL);
    r = toggle(A_APP_BOOL, tr(L"Açılınca korumayı aç"),
               g_cfg.trig_on ? tr(L"Program tetikleyicisi açıkken koruma o programla açılır")
                             : tr(L"Uygulama başlayınca koruma hemen açılır"), &g_cfg.start_bypass);
    r->enabled = !g_cfg.trig_on;

    toggle(A_APP_BOOL, tr(L"Bildirimler"), tr(L"Hatalar her zaman bildirilir"), &g_cfg.notify);
    toggle(A_APP_HOTKEY, tr(L"Kısayol tuşu"), tr(L"Ctrl+Alt+G ile korumayı her yerden aç/kapat"), &g_cfg.hotkey);

    section(tr(L"Otomatik aç/kapat"), NULL);
    info(toggle(A_AUTO_BOOL, tr(L"Program açılınca korumayı aç"), tr(L"Program kapanınca koruma da kapanır"), &g_cfg.trig_on),
         tr(L"Koruma yalnızca ihtiyaç olduğunda açık kalsın istiyorsan. Listedeki programlardan biri açılınca koruma "
            L"açılır, hepsi kapanınca kapanır. Korumayı elle açıp kapatırsan senin kararın geçerli olur."));
    if (g_cfg.trig_on)
        edit(A_TRIG_APPS, tr(L"Programlar"), tr(L"Virgülle ayır, ör. Discord, oyun.exe"), g_cfg.trig_apps, BIG_CAP, 280, 0);
    info(toggle(A_AUTO_BOOL, tr(L"Belirli saatlerde kapat"), tr(L"Seçtiğin saatler arasında koruma kapalı kalır"), &g_cfg.sched_on),
         tr(L"Örneğin gece saatlerinde korumayı kapatmak için. Aralık başlayınca koruma kapanır, bitince yeniden açılır. "
            L"Aralığın içinde elle açarsan açık kalır."));
    if (g_cfg.sched_on) {
        static const wchar_t *days[3] = { L"Her gün", L"Hafta içi", L"Hafta sonu" };
        wchar_t t[16];

        _snwprintf(t, 16, L"%02d:%02d", g_cfg.sched_from / 60, g_cfg.sched_from % 60);
        drop(A_SCHED_FROM, tr(L"Başlangıç"), NULL, t);
        _snwprintf(t, 16, L"%02d:%02d", g_cfg.sched_to / 60, g_cfg.sched_to % 60);
        drop(A_SCHED_TO, tr(L"Bitiş"), NULL, t);
        drop(A_SCHED_DAYS, tr(L"Günler"), NULL, tr(days[g_cfg.sched_days]));
    }

    section(tr(L"Telefonla paylaşım"), NULL);
    {
        wchar_t ip[32], d[200];
        int st = share_status(ip, 32);
        Row *sr;

        if (st == SHARE_ON)
            _snwprintf(d, 200, tr(L"Telefonun Wi-Fi ayarında vekil: %s, port %d"), ip, SHARE_PORT);
        else if (st == SHARE_WAITING)
            wcscpy(d, tr(L"Windows'ta Mobil erişim noktasını açınca başlar"));
        else if (st == SHARE_ERROR)
            wcscpy(d, tr(L"Paylaşım açılamadı; 8881 portunu başka bir uygulama kullanıyor olabilir"));
        else
            wcscpy(d, tr(L"Bilgisayarın internetini paylaştığın cihazlarda da engeller aşılır"));
        d[199] = 0;
        sr = toggle(A_SHARE, tr(L"Erişim noktasındaki cihazlara uygula"), d, &g_cfg.share_proxy);
        info(sr, tr(L"Windows Ayarlar → Ağ ve İnternet → Mobil erişim noktası'nı aç ve telefonu bu ağa bağla. Telefonda "
                    L"Wi-Fi ağının ayrıntılarından Vekil sunucu (Proxy) → Elle seç; sunucuya buradaki adresi, porta 8881 "
                    L"yaz.\n\nTarayıcı ve çoğu uygulama bu yolla açılır. Sesli görüşme gibi UDP trafiği vekilden geçmez. "
                    L"Vekil yalnızca erişim noktası ağından gelen bağlantıları kabul eder."));
        if (st == SHARE_ERROR) {
            sr = buttons(tr(L"Paylaşımı yeniden başlat"), NULL);
            btn(sr, A_SHARE_RETRY, tr(L"Yeniden dene"), B_SECONDARY, TRUE);
        }
    }

    section(tr(L"Görünüm"), NULL);
    drop(A_APP_THEME, tr(L"Tema"), NULL, tr(themes[g_cfg.theme]));
    {
        static const wchar_t *langs[3] = { L"Otomatik", L"Türkçe", L"English" };
        drop(A_APP_LANG, tr(L"Dil"), tr(L"Arayüz dili; değişiklik hemen uygulanır"),
             g_cfg.lang ? langs[g_cfg.lang] : tr(langs[0]));
    }

    section(tr(L"Bağlantı testi"), NULL);
    info(edit(A_APP_TESTHOST, tr(L"Test sitesi"), tr(L"Sende engelli olan bir site yaz, ör. discord.com; boş bırakırsan listedeki ilk site kullanılır"),
              g_cfg.test_host, MID_CAP, 240, 0),
         tr(L"Bağlantı testi, profil bulucu ve site denetimi bu siteyi kullanır. Engelsiz bir site (ör. google.com) "
            L"yazarsan her profil çalışıyor görünür; mutlaka sende açılmayan bir site seç."));

    section(tr(L"Güncellemeler"), NULL);
    toggle(A_APP_BOOL, tr(L"Güncellemeleri denetle"), tr(L"Günde bir kez yeni sürüm var mı diye bakılır"), &g_cfg.update_check);
    {
        const UpdateInfo *u = update_get();
        wchar_t dsc[200];

        if (!g_cfg.app_repo[0])
            wcscpy(dsc, tr(L"Güncelleme kaynağı ayarlanmadı"));
        else if (u->state == UPD_RUNNING)
            wcscpy(dsc, tr(L"Denetleniyor…"));
        else if (u->state == UPD_DOWNLOADING)
            wcscpy(dsc, tr(L"Yeni sürüm indiriliyor…"));
        else if (u->state == UPD_FAIL)
            wcscpy(dsc, tr(L"Denetlenemedi (sunucuya ulaşılamadı)"));
        else if (u->state == UPD_DONE && u->newer && !u->signed_release)
            _snwprintf(dsc, 200, tr(L"Yeni sürüm: %s · imzasız, sürüm sayfasından indir"), u->tag);
        else if (u->state == UPD_DONE && u->newer)
            _snwprintf(dsc, 200, tr(L"Yeni sürüm: %s · yüklü: %s"), u->tag, APP_VERSION);
        else if (u->state == UPD_DONE)
            _snwprintf(dsc, 200, tr(L"Güncel · %s"), APP_VERSION);
        else
            _snwprintf(dsc, 200, tr(L"Yüklü: %s · henüz denetlenmedi"), APP_VERSION);
        r = buttons(APP_NAME, dsc);
        if (u->newer && u->signed_release)
            btn(r, A_UPD_INSTALL, tr(L"Güncelle"), B_PRIMARY, u->state == UPD_DONE);
        info(r, tr(L"Güncellemeler GitHub'daki sürümlerden indirilir. Her sürüm yayıncının gizli anahtarıyla imzalanır; "
                   L"imza ya da dosya özeti tutmazsa güncelleme kurulmaz. Böylece sürüm sayfası ele geçirilse bile sahte "
                   L"bir dosya bilgisayarına kurulamaz."));
        btn(r, A_UPD_CHECK, tr(L"Şimdi denetle"), B_SECONDARY, g_cfg.app_repo[0] && u->state != UPD_RUNNING);
    }

    if (svc) {
        /* yalnızca gerçekten varsa göster: eski .cmd kurulumlarının bıraktığı hizmet */
        section(tr(L"Sorun giderme"), NULL);
        r = buttons(tr(L"Eski GoodbyeDPI hizmeti bulundu"), tr(L"TunnelDPI ile çakışır; kaldırman önerilir"));
        btn(r, A_APP_SERVICE, tr(L"Kaldır"), B_DANGER, TRUE);
    }

    section(tr(L"Yedekleme"), NULL);
    r = buttons(tr(L"Ayarlar ve listeler"), tr(L"Profiller, listeler, kurallar ve ayarlar tek dosyada"));
    btn(r, A_BACKUP, tr(L"Yedekle"), B_SECONDARY, TRUE);
    btn(r, A_RESTORE, tr(L"Geri yükle"), B_SECONDARY, TRUE);
    info(r, tr(L"Yedek dosyası tüm profillerini, site listelerini, site kurallarını, uygulama listesini, kendi engel ve "
               L"izin listelerini ve ayarlarını içerir. Başka bir bilgisayara taşımak ya da yeniden kurulumdan sonra geri "
               L"almak için. İndirilen reklam listeleri yedeğe girmez, kendiliğinden yeniden indirilir."));

    section(tr(L"Hakkında"), NULL);
    r = buttons(tr(L"Başlangıç rehberi"), tr(L"Koruma, site denemesi ve saatin yanındaki simge için kısa bir tur"));
    btn(r, A_GUIDE, tr(L"Rehberi aç"), B_SECONDARY, TRUE);
    r = buttons(tr(L"Bu sürümde neler var?"), NULL);
    btn(r, A_RELEASE, tr(L"Göster"), B_SECONDARY, TRUE);
    r = buttons(tr(L"Destek raporu"), tr(L"Kişisel metinler olmadan teknik durumları önizle, kopyala veya kaydet"));
    btn(r, A_SUPPORT, tr(L"Rapor hazırla"), B_SECONDARY, TRUE);
    r = buttons(APP_NAME, tr(L"İnternet engellerini VPN'siz aşar · Açık kaynak (Apache-2.0) · GoodbyeDPI ve zapret tabanlı"));
    btn(r, A_UPD_OPEN, L"GitHub", B_SECONDARY, TRUE);
    btn(r, A_APP_FOLDER, tr(L"Klasörü aç"), B_SECONDARY, TRUE);
    r = buttons(tr(L"TunnelDPI'ı kapat"), tr(L"Koruma kapanır ve simge saatin yanından kalkar"));
    btn(r, A_APP_QUIT, tr(L"Kapat"), B_DANGER, TRUE);
}

/* ---- uygulama filtresi listesi (apps.txt) ---- */

static int apps_read(wchar_t names[][MAX_PATH], int max)
{
    wchar_t path[MAX_PATH], *t;
    const wchar_t *s;
    int n = 0;

    app_path(path, MAX_PATH, APPS_FILE);
    t = read_text_file(path);
    if (!t)
        return 0;
    s = t;
    while (n < max && next_token(&s, names[n], MAX_PATH, L"\r\n"))
        if (names[n][0] != L'#')
            n++;
    free(t);
    return n;
}

static void apps_write(wchar_t names[][MAX_PATH], int n)
{
    wchar_t path[MAX_PATH], *buf = calloc((size_t)(n + 1) * (MAX_PATH + 2), sizeof(wchar_t));
    int i;

    if (!buf)
        return;
    for (i = 0; i < n; i++) {
        wcscat(buf, names[i]);
        wcscat(buf, L"\r\n");
    }
    app_path(path, MAX_PATH, APPS_FILE);
    write_text_file(path, buf);
    free(buf);
}

static void apps_add(const wchar_t *full)
{
    wchar_t names[64][MAX_PATH];
    const wchar_t *b = wcsrchr(full, L'\\');
    int n = apps_read(names, 64), i;

    b = b ? b + 1 : full;
    for (i = 0; i < n; i++)
        if (!_wcsicmp(names[i], b))
            return;
    if (n >= 64)
        return;
    wcsncpy(names[n], b, MAX_PATH - 1);
    names[n][MAX_PATH - 1] = 0;
    apps_write(names, n + 1);
}

static wchar_t *blk_load(const wchar_t *file)
{
    wchar_t path[MAX_PATH], *t, *b = calloc(32 * 1024, sizeof(wchar_t));

    block_path(file, path);
    t = read_text_file(path);
    if (b && t)
        wcsncpy(b, t, 32 * 1024 - 1);
    free(t);
    return b;
}

void blk_save(BOOL apply)
{
    wchar_t dir[MAX_PATH], path[MAX_PATH];

    if (g_content)
        KillTimer(g_content, T_BLKSAVE);
    app_path(dir, MAX_PATH, BLOCK_DIR);
    CreateDirectoryW(dir, NULL);
    if (g_blk_own) {
        block_path(BLOCK_OWN, path);
        write_text_file(path, g_blk_own);
    }
    if (g_blk_allow) {
        block_path(BLOCK_ALLOW, path);
        write_text_file(path, g_blk_allow);
    }
    if (apply && g_cfg.block_on)
        changed(TRUE);
}

void page_block(void)
{
    wchar_t sub[160], d[200], last[128];
    LONG nb = engine_blocked(last, 128);
    int i;
    Row *r;

    if (!g_cfg.block_on)
        wcscpy(sub, tr(L"Kapalı"));
    else if (!block_active(&g_cfg))
        wcscpy(sub, tr(L"Açık · listeler hazırlanıyor"));
    else if (nb > 0)
        _snwprintf(sub, 160, tr(L"Bu oturumda %ld alan adı engellendi · son: %s"), nb, last);
    else
        wcscpy(sub, tr(L"Açık · henüz bir şey engellenmedi"));
    title(tr(L"Engelleme"), sub);

    info(toggle(A_BLK_ON, tr(L"Reklam ve izleyicileri engelle"),
                tr(L"Reklam ve takip sitelerine bağlantı kesilir; tüm programlarda geçerli"), &g_cfg.block_on),
         tr(L"Seçtiğin listelerdeki reklam ve takip adreslerine bağlantı daha başlamadan kesilir. Tarayıcı eklentisi "
            L"gerekmez, tüm programlarda çalışır. Bir site bozulursa \"İzin verilenler\"e ekle."));
    if (g_cfg.block_on && config_active(&g_cfg)->engine == ENGINE_ZAPRET)
        add(T_NOTICE, NULL, tr(L"Seçili profil gelişmiş motoru kullanıyor; reklam engelleme yalnızca klasik motorla çalışır."));

    section(tr(L"Listeler"), tr(L"Seçtiğin listeler indirilir ve günde bir güncellenir. Çoğu kişi için ilki yeterli; büyük "
                                L"listeler daha çok bellek kullanır. Engelleme açıkken tarayıcılar HTTP/3 yerine normal HTTPS "
                                L"kullanır."));
    for (i = 0; i < g_nblock_src; i++) {
        const BlockSource *b = &g_block_src[i];
        int n = block_hosts(b->file);

        if (block_has(&g_cfg, b->file) && n < 0)
            _snwprintf(d, 200, L"%s · %s", tr(b->desc), subs_running() ? tr(L"indiriliyor…") : tr(L"indirilemedi"));
        else if (n >= 0)
            _snwprintf(d, 200, tr(L"%s · %d adres"), tr(b->desc), n);
        else
            wcsncpy(d, tr(b->desc), 199);
        d[199] = 0;
        r = toggle(A_BLK_SRC, tr(b->name), d, NULL);
        r->arg = i;
        r->enabled = g_cfg.block_on;        /* ana anahtar kapalıyken seçimler görünür ama pasif */
    }

    if (!g_blk_own)
        g_blk_own = blk_load(BLOCK_OWN);
    if (!g_blk_allow)
        g_blk_allow = blk_load(BLOCK_ALLOW);

    section(tr(L"Kendi sitelerin"), NULL);
    r = add(T_EXPANDER, tr(L"Engellenen ve izin verilen siteler"),
            tr(L"Listelere ek olarak engellemek ya da hiç engellememek istediğin siteler"));
    r->id = A_BLK_CUSTOM;
    r->pint = &g_blk_open;
    if (!g_blk_open)
        return;

    section(tr(L"Kendi listen"), tr(L"Engellemek istediğin siteler, her satıra bir tane. Alt adresler de engellenir."));
    r = add(T_AREA, NULL, NULL);
    r->id = A_BLK_OWN;
    r->min_h = 140;
    r->ebuf = g_blk_own;
    r->ecap = 32 * 1024;
    r->mono = TRUE;
    r->estyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;

    section(tr(L"İzin verilenler"), tr(L"Bunlar hiçbir zaman engellenmez. Bir site düzgün çalışmazsa buraya ekle."));
    r = add(T_AREA, NULL, NULL);
    r->id = A_BLK_ALLOW;
    r->min_h = 140;
    r->ebuf = g_blk_allow;
    r->ecap = 32 * 1024;
    r->mono = TRUE;
    r->estyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
}

void page_apps(void)
{
    static const wchar_t *modes[3] = { L"Tüm uygulamalar", L"Yalnızca listedekiler", L"Listedekiler hariç" };
    wchar_t names[64][MAX_PATH], sub[160];
    int n = apps_read(names, 64), i;
    Row *r;

    if (g_cfg.app_mode == APPS_ALL || !n)
        wcscpy(sub, tr(L"Koruma tüm uygulamaların bağlantılarına uygulanıyor"));
    else
        _snwprintf(sub, 160, g_cfg.app_mode == APPS_ONLY ? tr(L"Yalnızca %d uygulamaya uygulanıyor")
                                                         : tr(L"%d uygulama dışında tümüne uygulanıyor"), n);
    title(tr(L"Uygulamalar"), sub);
    info(drop(A_APPS_MODE, tr(L"Kapsam"), tr(L"Koruma hangi programların bağlantılarına uygulansın"), tr(modes[g_cfg.app_mode])),
         tr(L"Yalnızca listedekiler: koruma sadece seçtiğin programlara (ör. Discord) uygulanır, diğerleri hiç "
            L"etkilenmez.\n\nListedekiler hariç: seçtiğin programlar (ör. bir oyun ya da banka uygulaması) korumanın "
            L"dışında kalır.\n\nYalnızca klasik motorda çalışır."));

    section(tr(L"Oyunlar"), NULL);
    r = toggle(A_AUTO_BOOL, tr(L"Oyun modu"), tr(L"Tam ekran oyunun bağlantılarına dokunulmaz; Discord ve diğerleri korunur"),
               &g_cfg.game_mode);
    info(r, tr(L"Tam ekran bir oyun açılınca o oyun korumanın dışında bırakılır; oyunun bağlantısı hiç değiştirilmeden "
               L"gider, ping etkilenmez. Discord sesi ve diğer programlar korunmaya devam eder. Oyun kapanınca ya da bir "
               L"dakika arka planda kalınca koruma yeniden her şeye uygulanır.\n\nTarayıcılar oyun sayılmaz (tam ekran video "
               L"izlerken koruma kalkmaz). Yalnızca klasik motorda çalışır."));
    if (config_active(&g_cfg)->engine != ENGINE_CLASSIC)
        r->enabled = FALSE;

    section(tr(L"Liste"), tr(L"Programlar adlarına göre tanınır. Değişiklikler hemen uygulanır."));
    for (i = 0; i < n; i++) {
        r = buttons(names[i], NULL);
        r->arg = i;
        btn(r, A_APPS_REMOVE, tr(L"Kaldır"), B_SECONDARY, TRUE);
    }
    r = n ? buttons(NULL, NULL) : empty_state(IC_APPS, tr(L"Henüz program yok"), tr(L"Çalışan bir programı seç ya da .exe dosyasını ekle"), FALSE);
    btn(r, A_APPS_RUNNING, tr(L"Çalışanlardan ekle"), B_SECONDARY, n < 64);
    btn(r, A_APPS_BROWSE, tr(L"Dosyadan ekle"), B_SECONDARY, n < 64);
    if (g_cfg.app_mode != APPS_ALL && !n)
        add(T_NOTICE, NULL, tr(L"Liste boşken kapsam ayarı uygulanmaz; koruma tüm uygulamalara uygulanır."));
}

void page_log(void)
{
    Row *r;

    r = title(tr(L"Günlük"), tr(L"Bir sorun olursa nedenini buradaki kayıtlar gösterir"));
    btn(r, A_LOG_COPY, tr(L"Kopyala"), B_SECONDARY, TRUE);
    btn(r, A_LOG_CLEAR, tr(L"Temizle"), B_SECONDARY, TRUE);
    r = add(T_AREA, NULL, NULL);
    r->id = A_LOG_TEXT;
    r->mono = TRUE;
    r->min_h = -1;
    r->estyle = ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL;
}

/* ---------------- metin kutuları ---------------- */

void update_preview(void)
{
    wchar_t *buf = malloc(8192 * sizeof(wchar_t)), pre[MAX_PATH + 2], *q;
    size_t n, pl;
    int i;

    if (!buf)
        return;
    _snwprintf(pre, MAX_PATH + 2, L"%s%c", g_appdir, 92);
    pre[MAX_PATH + 1] = 0;
    pl = wcslen(pre);
    for (i = 0; i < g_nrows; i++) {
        if (g_rows[i].type != T_CODE || g_rows[i].id != A_EP_COPYCMD)
            continue;
        n = _snwprintf(buf, 8192, L"%s ", engine_name(ep()));
        build_args(&g_cfg, ep(), buf + n, 8192 - n);
        buf[8191] = 0;
        while ((q = wcsstr(buf, pre)) != NULL)       /* uygulama klasörünü kısalt */
            memmove(q, q + pl, (wcslen(q + pl) + 1) * sizeof(wchar_t));
        wcsncpy(g_rows[i].value, buf, 1023);
        g_rows[i].value[1023] = 0;
    }
    free(buf);
}

PopItem g_pi[48];
int     g_npi;

static void add_item(int id, const wchar_t *t, BOOL checked)
{
    if (g_npi >= 48)
        return;
    g_pi[g_npi].id = id;
    g_pi[g_npi].flags = checked ? PI_CHECK : 0;
    wcsncpy(g_pi[g_npi].text, t, 95);
    g_pi[g_npi].text[95] = 0;
    g_npi++;
}

static void pi_sep(void)
{
    if (g_npi < 48) {
        g_pi[g_npi].id = 0;
        g_pi[g_npi].flags = PI_SEP;
        g_pi[g_npi].text[0] = 0;
        g_npi++;
    }
}

/* Açılır listeyi kontrolün altında aç */
int popup(Row *r)
{
    RECT a = r->ctl;
    POINT tl = { a.left, a.top - g_scroll }, br = { a.right, a.bottom - g_scroll };

    ClientToScreen(g_content, &tl);
    ClientToScreen(g_content, &br);
    SetRect(&a, tl.x, tl.y, br.x, br.y);
    return ui_popup(g_main, &a, g_pi, g_npi, TRUE);
}

void do_dropdown(Row *r)
{
    wchar_t files[32][MAX_PATH];
    int i, cmd, nf;

    g_npi = 0;
    switch (r->id) {
    case A_HOME_PROFILE:
        for (i = 0; i < g_cfg.nprofiles; i++)
            add_item(i + 1, g_cfg.profiles[i].name, !_wcsicmp(g_cfg.profiles[i].name, g_cfg.active));
        cmd = popup(r);
        if (cmd > 0)
            use_profile(g_cfg.profiles[cmd - 1].name);
        break;
    case A_LIST_RULE: {
        wchar_t rel[MAX_PATH];
        int ri;

        list_rel(g_list_name, rel, MAX_PATH);
        ri = rule_find(&g_cfg, rel);
        add_item(1, tr(L"Etkin profil"), ri < 0);
        pi_sep();
        for (i = 0; i < g_cfg.nprofiles; i++)
            if (g_cfg.profiles[i].engine == ENGINE_CLASSIC)
                add_item(10 + i, g_cfg.profiles[i].name, ri >= 0 && !_wcsicmp(g_cfg.rules[ri].profile, g_cfg.profiles[i].name));
        cmd = popup(r);
        if (cmd == 1 || cmd >= 10) {
            if (cmd == 1)
                rule_set(&g_cfg, rel, NULL);
            else if (g_cfg.nrules < MAX_RULES || ri >= 0)
                rule_set(&g_cfg, rel, g_cfg.profiles[cmd - 10].name);
            changed(TRUE);
            rebuild(TRUE);
        }
        break;
    }
    case A_SCHED_FROM:
    case A_SCHED_TO: {
        int *v = r->id == A_SCHED_FROM ? &g_cfg.sched_from : &g_cfg.sched_to;
        static wchar_t times[48][8];

        for (i = 0; i < 48; i++) {
            _snwprintf(times[i], 8, L"%02d:%02d", i / 2, i % 2 * 30);
            add_item(1 + i, times[i], *v == i * 30);
        }
        cmd = popup(r);
        if (cmd > 0) {
            *v = (cmd - 1) * 30;
            changed(FALSE);
            rebuild(TRUE);
        }
        break;
    }
    case A_SCHED_DAYS: {
        static const wchar_t *days[3] = { L"Her gün", L"Hafta içi", L"Hafta sonu" };

        for (i = 0; i < 3; i++)
            add_item(1 + i, tr(days[i]), g_cfg.sched_days == i);
        cmd = popup(r);
        if (cmd > 0) {
            g_cfg.sched_days = cmd - 1;
            changed(FALSE);
            rebuild(TRUE);
        }
        break;
    }
    case A_HOME_DNS:
    case A_DNS_SERVER:
        add_item(1, tr(L"Windows varsayılanı"), g_cfg.dns_mode == DNS_OFF);
        pi_sep();
        for (i = 0; i < g_ndns_presets; i++)
            add_item(10 + i, tr(g_dns_presets[i].name), g_cfg.dns_mode != DNS_OFF && g_cfg.dns_preset == i);
        cmd = popup(r);
        if (cmd == 1)
            g_cfg.dns_mode = DNS_OFF;
        else if (cmd >= 10) {
            g_cfg.dns_preset = cmd - 10;
            g_cfg.dns_mode = g_cfg.dns_method;
        }
        if (cmd) {
            changed(TRUE);
            rebuild(TRUE);
        }
        break;
    case A_DNS_METHOD:
        add_item(1, tr(L"Uygulama üzerinden"), g_cfg.dns_method != DNS_SYSTEM);
        add_item(2, tr(L"Windows ayarı"), g_cfg.dns_method == DNS_SYSTEM);
        cmd = popup(r);
        if (cmd) {
            g_cfg.dns_method = cmd == 2 ? DNS_SYSTEM : DNS_REDIRECT;
            if (g_cfg.dns_mode != DNS_OFF)
                g_cfg.dns_mode = g_cfg.dns_method;
            changed(TRUE);
            rebuild(TRUE);
        }
        break;
    case A_EP_SELECT:
        for (i = 0; i < g_cfg.nprofiles; i++)
            add_item(i + 1, g_cfg.profiles[i].name, i == g_ep);
        cmd = popup(r);
        if (cmd > 0) {
            g_ep = cmd - 1;
            use_profile(g_cfg.profiles[g_ep].name);
        }
        break;
    case A_EP_ENGINE:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        add_item(1, tr(L"Klasik"), ep()->engine == ENGINE_CLASSIC);
        add_item(2, tr(L"Gelişmiş (zapret)"), ep()->engine == ENGINE_ZAPRET);
        cmd = popup(r);
        if (cmd > 0 && cmd - 1 != ep()->engine) {
            ep()->engine = cmd - 1;
            if (ep()->engine == ENGINE_ZAPRET && !ep()->zapret[0])
                config_zapret_default(ep());
            changed(ep_is_active());
            rebuild(TRUE);
        }
        break;
    case A_EP_TTL: {
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        static const wchar_t *ttl[] = { L"3", L"4", L"5", L"6", L"7", L"8" };
        Profile *p = ep();
        int k;

        add_item(1, tr(L"Otomatik"), p->auto_ttl && !p->set_ttl[0]);
        for (k = 0; k < 6; k++)
            add_item(10 + k, ttl[k], !wcscmp(p->set_ttl, ttl[k]));
        pi_sep();
        add_item(2, tr(L"Hazır ayara göre"), !p->auto_ttl && !p->set_ttl[0]);
        cmd = popup(r);
        if (cmd <= 0)
            break;
        p->auto_ttl = cmd == 1;
        if (cmd >= 10)
            wcscpy(p->set_ttl, ttl[cmd - 10]);
        else
            p->set_ttl[0] = 0;
        update_preview();
        changed(ep_is_active());
        rebuild(TRUE);
        break;
    }
    case A_EP_PRESET:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        for (i = 0; i < 10; i++)
            add_item(i + 1, tr(g_preset_names[i]), ep()->preset == i);
        cmd = popup(r);
        if (cmd > 0) {
            ep()->preset = cmd - 1;
            changed(ep_is_active());
            rebuild(TRUE);
        }
        break;
    case A_LIST_SELECT:
        nf = list_files(files, 32);
        for (i = 0; i < nf; i++)
            add_item(i + 1, files[i], !_wcsicmp(files[i], g_list_name));
        cmd = popup(r);
        if (cmd > 0) {
            wcscpy(g_list_name, files[cmd - 1]);
            list_load();
            rebuild(FALSE);
        }
        break;
    case A_APPS_MODE:
        add_item(1, tr(L"Tüm uygulamalar"), g_cfg.app_mode == APPS_ALL);
        add_item(2, tr(L"Yalnızca listedekiler"), g_cfg.app_mode == APPS_ONLY);
        add_item(3, tr(L"Listedekiler hariç"), g_cfg.app_mode == APPS_EXCEPT);
        cmd = popup(r);
        if (cmd > 0 && cmd - 1 != g_cfg.app_mode) {
            g_cfg.app_mode = cmd - 1;
            changed(TRUE);
            rebuild(TRUE);
        }
        break;
    case A_APP_LANG:
        add_item(1, tr(L"Otomatik"), g_cfg.lang == 0);
        add_item(2, L"Türkçe", g_cfg.lang == 1);
        add_item(3, L"English", g_cfg.lang == 2);
        cmd = popup(r);
        if (cmd > 0 && cmd - 1 != g_cfg.lang) {
            g_cfg.lang = cmd - 1;
            i18n_set(g_cfg.lang);
            changed(FALSE);
            app_refresh_state();
            rebuild(TRUE);
            InvalidateRect(g_main, NULL, FALSE);
        }
        break;
    case A_APP_THEME:
        add_item(1, tr(L"Sistem"), g_cfg.theme == 0);
        add_item(2, tr(L"Açık tema"), g_cfg.theme == 1);
        add_item(3, tr(L"Koyu tema"), g_cfg.theme == 2);
        cmd = popup(r);
        if (cmd > 0) {
            g_cfg.theme = cmd - 1;
            changed(FALSE);
            SendMessageW(g_main, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
            rebuild(TRUE);
        }
        break;
    default:
        break;
    }
}

/* ---------------- eylemler ---------------- */

void save_list(void)
{
    wchar_t rel[MAX_PATH], path[MAX_PATH], *t, *src, *dst;
    int i;

    if (g_content)
        KillTimer(g_content, T_LISTSAVE);
    if (!g_list_name[0] || !g_list_buf)
        return;
    t = malloc((wcslen(g_list_buf) * 2 + 4) * sizeof(wchar_t));
    if (!t)
        return;
    /* boş satırları ve kenar boşluklarını at */
    dst = t;
    src = g_list_buf;
    while (*src) {
        wchar_t line[512];
        size_t l = wcscspn(src, L"\r\n"), c = l < 511 ? l : 511;
        wmemcpy(line, src, c);
        line[c] = 0;
        trim_ws(line);
        if (line[0]) {
            size_t ll = wcslen(line);
            wmemcpy(dst, line, ll);
            dst += ll;
            *dst++ = L'\r';
            *dst++ = L'\n';
        }
        src += l;
        while (*src == L'\r' || *src == L'\n')
            src++;
    }
    *dst = 0;
    list_rel(g_list_name, rel, MAX_PATH);
    app_path(path, MAX_PATH, rel);
    write_text_file(path, t);
    free(t);
    /* aktif profil bu listeyi kullanıyorsa motor listeyi yeniden okusun */
    i = config_find(&g_cfg, g_cfg.active);
    if (i >= 0 && bl_has(&g_cfg.profiles[i], rel))
        changed(TRUE);
}

static void new_list(void)
{
    wchar_t name[MAX_PATH], rel[MAX_PATH], path[MAX_PATH];
    int k = 1;

    save_list();
    for (;;) {
        if (k == 1)
            wcscpy(name, L"yeni-liste.txt");
        else
            _snwprintf(name, MAX_PATH, L"yeni-liste-%d.txt", k);
        list_rel(name, rel, MAX_PATH);
        app_path(path, MAX_PATH, rel);
        if (!file_exists(path))
            break;
        k++;
    }
    write_text_file(path, L"");
    wcscpy(g_list_name, name);
    list_load();
}

/* Türkiye'de engelli siteler: depodaki listeye abone olunur, günde bir güncellenir.
   İnternet gelene kadar boş kalmasın diye bilinen siteler hemen yazılır. */
#define TR_LIST L"turkiye.txt"
#define TR_LIST_URL L"https://raw.githubusercontent.com/%s/main/app/abonelik/turkiye.txt"

static void add_tr_list(void)
{
    static const wchar_t *seed =
        L"discord.com\r\ndiscord.gg\r\ndiscord.media\r\ndiscord.co\r\ndiscord.gift\r\ndiscord.new\r\ndiscord.dev\r\n"
        L"discordapp.com\r\ndiscordapp.net\r\ndiscordcdn.com\r\ndiscordstatus.com\r\ndis.gd\r\n"
        L"roblox.com\r\nrbxcdn.com\r\nrbxinfra.net\r\nrbxtrk.com\r\narkoselabs.com\r\nwattpad.com\r\n";
    wchar_t rel[MAX_PATH], path[MAX_PATH], url[256];

    save_list();
    list_rel(TR_LIST, rel, MAX_PATH);
    app_path(path, MAX_PATH, rel);
    if (!file_exists(path))
        write_text_file(path, seed);
    if (g_cfg.app_repo[0]) {
        _snwprintf(url, 256, TR_LIST_URL, g_cfg.app_repo);
        url[255] = 0;
        subs_set_url(TR_LIST, url);
        subs_update(TR_LIST, TRUE);
    }
    wcscpy(g_list_name, TR_LIST);
    list_load();
}

static BOOL tr_list_exists(void)
{
    wchar_t rel[MAX_PATH], path[MAX_PATH];

    list_rel(TR_LIST, rel, MAX_PATH);
    app_path(path, MAX_PATH, rel);
    return file_exists(path);
}

static void rename_list(void)
{
    wchar_t name[MAX_PATH], from[MAX_PATH], to[MAX_PATH], rel[MAX_PATH], newrel[MAX_PATH];
    int i;

    wcsncpy(name, g_list_rename, MAX_PATH - 5);
    name[MAX_PATH - 5] = 0;
    trim_ws(name);
    for (i = 0; name[i]; i++)
        if (wcschr(L"\\/:*?\"<>|;", name[i]))
            name[i] = L'_';
    if (!name[0])
        return;
    wcscat(name, L".txt");
    if (!_wcsicmp(name, g_list_name))
        return;
    save_list();
    list_rel(g_list_name, rel, MAX_PATH);
    list_rel(name, newrel, MAX_PATH);
    app_path(from, MAX_PATH, rel);
    app_path(to, MAX_PATH, newrel);
    if (file_exists(to) || !MoveFileW(from, to)) {
        list_load();
        rebuild(TRUE);
        return;
    }
    for (i = 0; i < g_cfg.nprofiles; i++)
        if (bl_has(&g_cfg.profiles[i], rel)) {
            bl_set(&g_cfg.profiles[i], rel, FALSE);
            bl_set(&g_cfg.profiles[i], newrel, TRUE);
        }
    subs_rename(g_list_name, name);
    wcscpy(g_list_name, name);
    changed(FALSE);
    list_load();
    rebuild(TRUE);
}

static void sanitize_ep_name(void)
{
    Profile *p = ep();
    BOOL was_active = ep_is_active();
    wchar_t base[NAME_CAP];
    int i, k = 2;

    for (i = 0; p->name[i]; i++)
        if (p->name[i] == L'[' || p->name[i] == L']' || p->name[i] == L'=' || p->name[i] < 32)
            p->name[i] = L' ';
    trim_ws(p->name);
    if (!p->name[0])
        wcscpy(p->name, tr(L"Profil"));
    wcscpy(base, p->name);
    for (;;) {
        BOOL dup = FALSE;
        for (i = 0; i < g_cfg.nprofiles; i++)
            if (i != g_ep && !_wcsicmp(g_cfg.profiles[i].name, p->name))
                dup = TRUE;
        if (!dup)
            break;
        _snwprintf(p->name, NAME_CAP, L"%.*s %d", NAME_CAP - 6, base, k++);
    }
    if (was_active || config_find(&g_cfg, g_cfg.active) < 0)
        wcscpy(g_cfg.active, p->name);
}

typedef struct {
    wchar_t names[48][MAX_PATH];
    int     n;
} ProcList;

/* Görünür penceresi olan programlar: kullanıcının tanıdığı uygulamalar */
static BOOL CALLBACK enum_app_windows(HWND h, LPARAM lp)
{
    ProcList *pl = (ProcList *)lp;
    wchar_t path[MAX_PATH];
    const wchar_t *b;
    DWORD pid = 0, len = MAX_PATH;
    HANDLE ph;
    int i;

    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || !GetWindowTextLengthW(h))
        return TRUE;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;
    ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!ph)
        return TRUE;
    if (QueryFullProcessImageNameW(ph, 0, path, &len)) {
        b = wcsrchr(path, L'\\');
        b = b ? b + 1 : path;
        if (_wcsicmp(b, L"explorer.exe") && _wcsicmp(b, L"ApplicationFrameHost.exe") &&
            _wcsicmp(b, L"TextInputHost.exe") && _wcsicmp(b, L"SystemSettings.exe")) {
            for (i = 0; i < pl->n && _wcsicmp(pl->names[i], b); i++)
                ;
            if (i == pl->n && pl->n < 48)
                wcscpy(pl->names[pl->n++], b);
        }
    }
    CloseHandle(ph);
    return pl->n < 48;
}

static int popup_at(const RECT *content_rc)
{
    POINT tl = { content_rc->left, content_rc->top - g_scroll }, br = { content_rc->right, content_rc->bottom - g_scroll };
    RECT a;

    ClientToScreen(g_content, &tl);
    ClientToScreen(g_content, &br);
    SetRect(&a, tl.x, tl.y, br.x, br.y);
    return ui_popup(g_main, &a, g_pi, g_npi, FALSE);
}


void on_action(Row *r, int id)
{
    Profile *p = ep();
    wchar_t buf[MAX_PATH], path[MAX_PATH];
    int i;

    switch (id) {
    case A_BYPASS:
        app_toggle();
        break;
    case A_ISP_USE: {
        const wchar_t *rec[1];
        if (isp_profiles(rec, 1))
            use_profile(rec[0]);
        break;
    }
    case A_HOME_PROFILE:
    case A_HOME_DNS: {
        /* Durum sayfasındaki özet kutucukları: açılır listeyi kutucuğun altında aç */
        Row tmp = *r;
        int k;

        for (k = 0; k < r->nbtn && r->btn[k].id != id; k++)
            ;
        if (k < r->nbtn) {
            tmp.id = id;
            tmp.ctl = r->btn[k].rc;
            do_dropdown(&tmp);
        }
        break;
    }
    case A_TUNE:
    case A_TUNE_DEEP:
        if (app_tune_state(NULL, NULL)) {
            app_tune_cancel();
        } else if (app_tune_start(id == A_TUNE_DEEP) == 1) {
            task_box(g_main, tr(L"Önce bir test sitesi seç"),
                       tr(L"Profilleri karşılaştırmak için sende engelli olan bir site gerekir. Genel sayfasındaki \"Test "
                          L"sitesi\" alanına yaz ya da profile bir site listesi ekle."),
                       TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
            switch_page(SETTINGS_PAGE_APP);
            return;
        }
        rebuild(TRUE);
        break;
    case A_APP_HOTKEY:
        g_cfg.hotkey = !g_cfg.hotkey;
        app_apply_hotkey();
        changed(FALSE);
        break;
    case A_AUTO_BOOL:
        if (r->pint) {
            *r->pint = !*r->pint;
            changed(FALSE);
            rebuild(TRUE);
        }
        break;
    case A_SHARE:
        g_cfg.share_proxy = !g_cfg.share_proxy;
        changed(FALSE);
        share_apply();
        rebuild(TRUE);
        break;
    case A_GUIDE:
        guide_show(g_main);
        break;
    case A_RETRY:
        app_retry();
        rebuild(TRUE);
        break;
    case A_SUPPORT:
        support_show(g_main);
        break;
    case A_RELEASE:
        release_show(g_main);
        break;
    case A_OPEN_LOG:
        switch_page(SETTINGS_PAGE_LOG);
        break;
    case A_SHARE_SETTINGS:
        switch_page(SETTINGS_PAGE_APP);
        break;
    case A_SHARE_RETRY:
        share_stop();
        share_apply();
        rebuild(TRUE);
        break;
    case A_HEALTH_CHECK:
        health_start();
        rebuild(TRUE);
        break;
    case A_SITE_FIX:
        if (r->edit)
            GetWindowTextW(r->edit, g_site_input, 256);
        switch (app_site_fix(g_site_input)) {
        case 1:
            task_box(g_main, tr(L"Bu bir site adresine benzemiyor"),
                       tr(L"Örneğin discord.com ya da https://www.ornek.com/sayfa yaz."), TDCBF_OK_BUTTON,
                       TD_INFORMATION_ICON, NULL);
            break;
        case 2:
            task_box(g_main, tr(L"Başka bir deneme sürüyor"),
                       tr(L"Bitmesini bekle ya da Durum sayfasından durdur."), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
            break;
        }
        rebuild(TRUE);
        break;
    case A_DNS_BENCH:
        dnsbench_start();
        rebuild(TRUE);
        break;
    case A_APP_QUIT:
        app_quit();
        return;
    case A_BACKUP: {
        OPENFILENAMEW of = { sizeof(of) };
        SYSTEMTIME st;
        wchar_t file[MAX_PATH];
        int n;

        GetLocalTime(&st);
        _snwprintf(file, MAX_PATH, L"tunneldpi-yedek-%04d-%02d-%02d.txt", st.wYear, st.wMonth, st.wDay);
        of.hwndOwner = g_main;
        of.lpstrFilter = L"TunnelDPI yedeği (*.txt)\0*.txt\0";
        of.lpstrDefExt = L"txt";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&of))
            break;
        flush_pending();
        n = backup_save(file);
        _snwprintf(buf, MAX_PATH, n > 0 ? tr(L"%d dosya yedeklendi.") : tr(L"Yedek dosyası yazılamadı."), n);
        task_box(g_main, tr(L"Yedekleme"), buf, TDCBF_OK_BUTTON,
                   n > 0 ? TD_INFORMATION_ICON : TD_WARNING_ICON, NULL);
        break;
    }
    case A_RESTORE: {
        OPENFILENAMEW of = { sizeof(of) };
        wchar_t file[MAX_PATH] = L"";
        int n;

        of.hwndOwner = g_main;
        of.lpstrFilter = L"TunnelDPI yedeği (*.txt)\0*.txt\0Tüm dosyalar\0*.*\0";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&of))
            break;
        if (!confirm(tr(L"Yedek geri yüklensin mi?"),
                     tr(L"Şu anki ayarların, profillerin ve listelerin yedektekilerle değiştirilir. Yedekte olmayan "
                        L"listeler silinmez."), tr(L"Geri yükle")))
            break;
        flush_pending();
        n = backup_restore(file);
        if (n < 0) {
            task_box(g_main, tr(L"Bu bir TunnelDPI yedeği değil"),
                       tr(L"Ayarlar penceresindeki \"Yedekle\" ile oluşturulan dosyayı seç."), TDCBF_OK_BUTTON,
                       TD_WARNING_ICON, NULL);
            break;
        }
        config_load(&g_cfg);
        i18n_set(g_cfg.lang);
        app_apply_hotkey();
        g_ep = config_find(&g_cfg, g_cfg.active);
        g_list_name[0] = 0;
        free(g_list_buf);
        g_list_buf = NULL;
        app_log(L"Yedekten %d dosya geri yüklendi: %s", n, file);
        bypass_restart_if_running();
        _snwprintf(buf, MAX_PATH, tr(L"%d dosya geri yüklendi."), n);
        task_box(g_main, tr(L"Geri yükleme"), buf, TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
        rebuild(FALSE);
        break;
    }
    case A_DNS_BEST:
        if (r->arg >= 0 && r->arg < g_ndns_presets) {
            g_cfg.dns_preset = r->arg;
            if (g_cfg.dns_mode == DNS_OFF)
                g_cfg.dns_mode = g_cfg.dns_method;
            changed(TRUE);
            rebuild(TRUE);
        }
        break;
    case A_AUTOSTART:
        app_set_autostart(!app_autostart());
        break;
    case A_APP_BOOL:
    case A_DNS_DOH:
        if (r->pint && r->enabled) {
            *r->pint = !*r->pint;
            if (r->pint == &g_cfg.log_file)
                log_file_enable(g_cfg.log_file);
            changed(id == A_DNS_DOH);
        }
        break;
    case A_EP_FIELD:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        if (r->pint && r->enabled) {
            *r->pint = !*r->pint;
            update_preview();
            changed(ep_is_active());
            if (g_fields[r->arg].type == F_OPT || !wcscmp(g_fields[r->arg].key, L"autolist"))
                rebuild(TRUE);
            else
                relayout();
        }
        break;
    case A_EP_LIST:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        bl_set(p, r->value, !bl_has(p, r->value));
        changed(ep_is_active());
        rebuild(TRUE);
        break;
    case A_BLK_ON:
        g_cfg.block_on = !g_cfg.block_on;
        if (g_cfg.block_on && !g_cfg.block_lists[0])
            block_set(&g_cfg, 0, TRUE);     /* ilk açılışta önerilen liste */
        changed(TRUE);
        rebuild(TRUE);
        break;
    case A_BLK_SRC:
        block_set(&g_cfg, r->arg, !block_has(&g_cfg, g_block_src[r->arg].file));
        changed(g_cfg.block_on);
        rebuild(TRUE);
        break;
    case A_AUTO_SHOW:
    case A_AUTO_CANDIDATES:
    case A_AUTO_LEGACY:
        save_list();
        wcscpy(g_list_name, id == A_AUTO_CANDIDATES ? AUTO_CANDIDATE_NAME : id == A_AUTO_LEGACY ? L"otomatik.txt" : AUTOLIST_NAME);
        list_rel(g_list_name, buf, MAX_PATH);
        app_path(path, MAX_PATH, buf);
        if (!file_exists(path)) write_text_file(path, L"");
        list_load();
        switch_page(SETTINGS_PAGE_LISTS);
        return;
    case A_AUTO_CLEAR:
        app_path(buf, MAX_PATH, AUTOLIST_FILE);
        write_text_file(buf, L"");
        changed(ep_is_active());
        rebuild(TRUE);
        break;
    case A_EP_UDP:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        if (udp_mode(p) == r->arg) {
            p->udp_fake[0] = p->udp_ports[0] = 0;
        } else {
            if (!p->udp_fake[0])
                wcscpy(p->udp_fake, L"6");
            wcscpy(p->udp_ports, r->arg == 1 ? UDP_DISCORD : UDP_GAMES);
        }
        update_preview();
        changed(ep_is_active());
        rebuild(TRUE);
        break;
    case A_EP_ADV:
    case A_BLK_CUSTOM:
        if (r->pint)
            *r->pint = !*r->pint;
        rebuild(TRUE);
        break;
    case A_EP_NEW:
    case A_EP_COPY:
        if (g_cfg.nprofiles >= MAX_PROFILES)
            break;
        if (id == A_EP_COPY) {
            g_cfg.profiles[g_cfg.nprofiles] = *p;
            _snwprintf(g_cfg.profiles[g_cfg.nprofiles].name, NAME_CAP, L"%.*s (kopya)", NAME_CAP - 10, p->name);
        } else {
            profile_init(&g_cfg.profiles[g_cfg.nprofiles], tr(L"Yeni profil"));
            g_cfg.profiles[g_cfg.nprofiles].preset = 5;
        }
        g_ep = g_cfg.nprofiles++;
        sanitize_ep_name();
        changed(FALSE);
        rebuild(FALSE);
        break;
    case A_EP_DELETE:
        if (g_cfg.nprofiles <= 1)
            break;
        _snwprintf(buf, MAX_PATH, tr(L"\"%s\" silinsin mi?"), p->name);
        if (!confirm(buf, tr(L"Profil kalıcı olarak silinir."), tr(L"Profili sil")))
            break;
        {
            BOOL was_active = ep_is_active();
            for (i = g_ep; i < g_cfg.nprofiles - 1; i++)
                g_cfg.profiles[i] = g_cfg.profiles[i + 1];
            g_cfg.nprofiles--;
            if (g_ep >= g_cfg.nprofiles)
                g_ep = g_cfg.nprofiles - 1;
            if (was_active)
                wcscpy(g_cfg.active, g_cfg.profiles[0].name);
            changed(was_active);
        }
        rebuild(FALSE);
        break;
    case A_EP_COPYCMD: {
        wchar_t *c = malloc(8192 * sizeof(wchar_t));
        if (c) {
            size_t n = _snwprintf(c, 8192, L"%s ", engine_name(p));
            build_args(&g_cfg, p, c + n, 8192 - n);
            c[8191] = 0;
            copy_clip(c);
            free(c);
        }
        break;
    }
    case A_EP_MAKELIST:
        new_list();
        switch_page(SETTINGS_PAGE_LISTS);
        return;
    case A_EP_MENU:
    case A_LIST_MENU: {
        int k, cmd;

        for (k = 0; k < r->nbtn && r->btn[k].id != id; k++)
            ;
        g_npi = 0;
        if (id == A_EP_MENU) {
            add_item(A_EP_RENAME, tr(L"Yeniden adlandır"), FALSE);
            add_item(A_EP_COPY, tr(L"Kopyasını oluştur"), FALSE);
            add_item(A_EP_NEW, tr(L"Yeni profil"), FALSE);
            pi_sep();
            add_item(A_CLIP_COPY, tr(L"Panoya kopyala"), FALSE);
            add_item(A_CLIP_PASTE, tr(L"Panodan ekle"), FALSE);
            add_item(A_EXPORT, tr(L"Tüm profilleri dosyaya kaydet"), FALSE);
            add_item(A_IMPORT, tr(L"Dosyadan profil ekle"), FALSE);
            pi_sep();
            add_item(A_EP_DELETE, tr(L"Profili sil"), FALSE);
            g_pi[g_npi - 1].flags = PI_DANGER | (g_cfg.nprofiles > 1 ? 0 : PI_DISABLED);
            if (g_cfg.nprofiles >= MAX_PROFILES)
                for (i = 0; i < g_npi; i++)
                    if (g_pi[i].id == A_EP_COPY || g_pi[i].id == A_EP_NEW || g_pi[i].id == A_CLIP_PASTE ||
                        g_pi[i].id == A_IMPORT)
                        g_pi[i].flags |= PI_DISABLED;
        } else {
            if (!tr_list_exists()) {
                add_item(A_LIST_TR, tr(L"Türkiye listesini ekle"), FALSE);
                pi_sep();
            }
            add_item(A_LIST_RENAME, tr(L"Yeniden adlandır"), FALSE);
            add_item(A_LIST_FOLDER, tr(L"Klasörü aç"), FALSE);
            pi_sep();
            add_item(A_LIST_DELETE, tr(L"Listeyi sil"), FALSE);
            g_pi[g_npi - 1].flags = PI_DANGER;
        }
        cmd = popup_at(&r->btn[k < r->nbtn ? k : 0].rc);
        if (cmd > 0)
            on_action(r, cmd);
        return;
    }
    case A_EP_RENAME:
    case A_LIST_RENAME:
        if (id == A_EP_RENAME)
            g_ep_rename = TRUE;
        else
            g_list_renaming = TRUE;
        rebuild(TRUE);
        for (i = 0; i < g_nrows; i++)
            if (g_rows[i].edit && g_rows[i].id == (id == A_EP_RENAME ? A_EP_NAME : A_LIST_NAME)) {
                SetFocus(g_rows[i].edit);
                SendMessageW(g_rows[i].edit, EM_SETSEL, 0, -1);
            }
        return;
    case A_LIST_NEW:
        new_list();
        rebuild(FALSE);
        break;
    case A_LIST_TR:
        add_tr_list();
        rebuild(FALSE);
        break;
    case A_LIST_DELETE:
        if (!g_list_name[0])
            break;
        _snwprintf(buf, MAX_PATH, tr(L"\"%s\" silinsin mi?"), g_list_name);
        if (!confirm(buf, tr(L"Dosya kalıcı olarak silinir ve onu kullanan profillerden çıkarılır."), tr(L"Listeyi sil")))
            break;
        {
            wchar_t rel[MAX_PATH], path[MAX_PATH];
            BOOL act = FALSE;
            list_rel(g_list_name, rel, MAX_PATH);
            app_path(path, MAX_PATH, rel);
            KillTimer(g_content, T_LISTSAVE);
            DeleteFileW(path);
            subs_set_url(g_list_name, L"");
            for (i = 0; i < g_cfg.nprofiles; i++)
                if (bl_has(&g_cfg.profiles[i], rel)) {
                    if (!_wcsicmp(g_cfg.profiles[i].name, g_cfg.active))
                        act = TRUE;
                    bl_set(&g_cfg.profiles[i], rel, FALSE);
                }
            g_list_name[0] = 0;
            free(g_list_buf);
            g_list_buf = NULL;
            changed(act);
        }
        rebuild(FALSE);
        break;
    case A_LIST_FOLDER:
        app_path(buf, MAX_PATH, L"lists");
        ShellExecuteW(g_main, L"open", buf, NULL, NULL, SW_SHOWNORMAL);
        break;
    case A_APP_FOLDER:
        ShellExecuteW(g_main, L"open", g_appdir, NULL, NULL, SW_SHOWNORMAL);
        break;
    case A_APP_SERVICE:
        if (confirm(tr(L"Eski GoodbyeDPI hizmeti kaldırılsın mı?"), tr(L"Eski kurulumdan kalan GoodbyeDPI ve WinDivert hizmetleri durdurulup silinir."),
                    tr(L"Hizmetleri kaldır"))) {
            BOOL was = bypass_running();
            if (was)
                engine_stop();
            legacy_service_remove();
            if (was)
                bypass_restart_if_running();
            rebuild(TRUE);
        }
        break;
    case A_APPS_REMOVE: {
        wchar_t names[64][MAX_PATH];
        int n = apps_read(names, 64);
        if (r->arg >= 0 && r->arg < n) {
            for (i = r->arg; i < n - 1; i++)
                wcscpy(names[i], names[i + 1]);
            apps_write(names, n - 1);
            changed(g_cfg.app_mode != APPS_ALL);
        }
        rebuild(TRUE);
        break;
    }
    case A_APPS_RUNNING: {
        ProcList *pl = calloc(1, sizeof(ProcList));
        int k, cmd;
        if (!pl)
            break;
        EnumWindows(enum_app_windows, (LPARAM)pl);
        for (i = 1; i < pl->n; i++)            /* alfabetik sırala */
            for (k = i; k > 0 && _wcsicmp(pl->names[k - 1], pl->names[k]) > 0; k--) {
                wchar_t t[MAX_PATH];
                wcscpy(t, pl->names[k]);
                wcscpy(pl->names[k], pl->names[k - 1]);
                wcscpy(pl->names[k - 1], t);
            }
        g_npi = 0;
        for (i = 0; i < pl->n; i++)
            add_item(i + 1, pl->names[i], FALSE);
        if (!pl->n)
            add_item(-1, tr(L"Pencereli program bulunamadı"), FALSE);
        for (k = 0; k < r->nbtn; k++)
            if (r->btn[k].id == A_APPS_RUNNING)
                break;
        cmd = popup_at(&r->btn[k < r->nbtn ? k : 0].rc);
        if (cmd > 0) {
            apps_add(pl->names[cmd - 1]);
            changed(g_cfg.app_mode != APPS_ALL);
        }
        free(pl);
        rebuild(TRUE);
        break;
    }
    case A_APPS_BROWSE: {
        OPENFILENAMEW of = { sizeof(of) };
        wchar_t file[MAX_PATH] = L"";
        of.hwndOwner = g_main;
        of.lpstrFilter = L"Programlar (*.exe)\0*.exe\0";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&of)) {
            apps_add(file);
            changed(g_cfg.app_mode != APPS_ALL);
            rebuild(TRUE);
        }
        break;
    }
    case A_DIAG: {
        wchar_t host[128];

        if (r->edit)
            GetWindowTextW(r->edit, g_site_input, 256);
        diag_start_host(host_normalize(g_site_input, host, 128) ? host : NULL);
        rebuild(TRUE);
        break;
    }
    case A_NET_REMEMBER:
        app_net_remember(p->name);
        rebuild(TRUE);
        break;
    case A_NET_FORGET:
        if (r->arg >= 0 && r->arg < g_cfg.nnets) {
            memmove(&g_cfg.nets[r->arg], &g_cfg.nets[r->arg + 1],
                    sizeof(g_cfg.nets[0]) * (size_t)(g_cfg.nnets - r->arg - 1));
            g_cfg.nnets--;
            changed(FALSE);
        }
        rebuild(TRUE);
        break;
    case A_EXPORT: {
        OPENFILENAMEW of = { sizeof(of) };
        wchar_t file[MAX_PATH] = L"tunneldpi-profilleri.ini", *t = profiles_export(&g_cfg, -1);
        of.hwndOwner = g_main;
        of.lpstrFilter = L"Profil dosyası (*.ini)\0*.ini\0";
        of.lpstrDefExt = L"ini";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (t && GetSaveFileNameW(&of))
            write_text_file(file, t);
        free(t);
        break;
    }
    case A_IMPORT: {
        OPENFILENAMEW of = { sizeof(of) };
        wchar_t file[MAX_PATH] = L"", *t;
        int n;
        of.hwndOwner = g_main;
        of.lpstrFilter = L"Profil dosyası (*.ini)\0*.ini\0Tüm dosyalar\0*.*\0";
        of.lpstrFile = file;
        of.nMaxFile = MAX_PATH;
        of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&of))
            break;
        t = read_text_file(file);
        n = profiles_import(&g_cfg, t);
        free(t);
        _snwprintf(buf, MAX_PATH, n ? tr(L"%d profil eklendi.") : tr(L"Dosyada profil bulunamadı."), n);
        task_box(g_main, tr(L"İçe aktarma"), buf, TDCBF_OK_BUTTON,
                   n ? TD_INFORMATION_ICON : TD_WARNING_ICON, NULL);
        if (n) {
            changed(FALSE);
            rebuild(TRUE);
        }
        break;
    }
    case A_CLIP_COPY: {
        wchar_t *t = profiles_export(&g_cfg, g_ep);
        if (t) {
            copy_clip(t);
            free(t);
        }
        break;
    }
    case A_CLIP_PASTE: {
        wchar_t *t = NULL;
        int n = 0;
        if (OpenClipboard(g_main)) {
            HANDLE hd = GetClipboardData(CF_UNICODETEXT);
            const wchar_t *src = hd ? GlobalLock(hd) : NULL;
            if (src) {
                t = _wcsdup(src);
                GlobalUnlock(hd);
            }
            CloseClipboard();
        }
        n = profiles_import(&g_cfg, t);
        free(t);
        if (n) {
            g_ep = g_cfg.nprofiles - 1;
            changed(FALSE);
            rebuild(FALSE);
        } else {
            task_box(g_main, tr(L"Panoda profil yok"),
                       tr(L"\"Bu profili kopyala\" ile kopyalanmış bir profil metni yapıştırılabilir."),
                       TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
        }
        break;
    }
    case A_SUB_UPDATE:
        subs_set_url(g_list_name, g_sub_url);
        subs_update(g_list_name, TRUE);
        rebuild(TRUE);
        break;
    case A_UPD_CHECK:
        update_check();
        rebuild(TRUE);
        break;
    case A_UPD_INSTALL:
        update_install();
        rebuild(TRUE);
        break;
    case A_UPD_OPEN: {
        /* yeni sürüm varsa onun sayfası, yoksa proje sayfası */
        const UpdateInfo *u = update_get();
        wchar_t url[300];
        if (u->newer && !wcsncmp(u->url, L"https://", 8))
            wcsncpy(url, u->url, 299);
        else
            _snwprintf(url, 300, L"https://github.com/%s", g_cfg.app_repo);
        url[299] = 0;
        ShellExecuteW(g_main, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case A_LOG_COPY: {
        size_t n = log_get(NULL, 0);
        wchar_t *t = malloc((n + 1) * sizeof(wchar_t));
        if (t) {
            log_get(t, n + 1);
            copy_clip(t);
            free(t);
        }
        break;
    }
    case A_LOG_CLEAR:
        log_clear();
        for (i = 0; i < g_nrows; i++)
            if (g_rows[i].id == A_LOG_TEXT && g_rows[i].edit)
                SetWindowTextW(g_rows[i].edit, L"");
        break;
    }
    if (g_content) {
        InvalidateRect(g_content, NULL, FALSE);
        anim_kick();
    }
}

void on_edit(int idx, int code)
{
    Row *r;

    if (idx < 0 || idx >= g_nrows)
        return;
    r = &g_rows[idx];
    if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
        InvalidateRect(g_content, NULL, FALSE);
        InvalidateRect(r->edit, NULL, TRUE);   /* odak dolgusu değişti */
        if (code == EN_KILLFOCUS) {
            if (r->id == A_EP_NAME) {
                sanitize_ep_name();
                changed(FALSE);
                g_ep_rename = FALSE;
                rebuild(TRUE);
            } else if (r->id == A_LIST_NAME) {
                rename_list();
                g_list_renaming = FALSE;
                rebuild(TRUE);
            }
            else if (r->id == A_SUB_URL) {
                wchar_t old[1024];
                subs_get_url(g_list_name, old, 1024);
                if (wcscmp(old, g_sub_url)) {
                    subs_set_url(g_list_name, g_sub_url);
                    if (g_sub_url[0])
                        subs_update(g_list_name, TRUE);   /* yeni adres: hemen indir */
                    rebuild(TRUE);
                }
            }
        }
        return;
    }
    if (code != EN_CHANGE || !r->ebuf)
        return;
    if (r->id == A_EP_NAME) {
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        BOOL was_active = ep_is_active();
        GetWindowTextW(r->edit, r->ebuf, r->ecap);
        if (was_active)
            wcscpy(g_cfg.active, r->ebuf);
        changed(FALSE);
        return;
    }
    GetWindowTextW(r->edit, r->ebuf, r->ecap);
    switch (r->id) {
    case A_EP_FIELD:
    case A_EP_FIELD_VALUE:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        update_preview();
        relayout();
        changed(ep_is_active());
        break;
    case A_DNS_CUSTOM:
        changed(TRUE);
        break;
    case A_APP_TESTHOST:
    case A_TRIG_APPS:
        changed(FALSE);
        break;
    case A_LIST_TEXT:
        SetTimer(g_content, T_LISTSAVE, 800, NULL);
        break;
    case A_EP_ZAPRET:
        ep()->managed = 0;              /* değiştirilen hazır profil artık senin */
        update_preview();
        relayout();
        changed(ep_is_active());
        break;
    case A_BLK_OWN:
    case A_BLK_ALLOW:
        SetTimer(g_content, T_BLKSAVE, 1000, NULL);
        break;
    }
}

/* ---------------- fare ---------------- */

