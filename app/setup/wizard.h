/* Klasik Windows kurulum sihirbazı (Wizard97). setup.c tarafından eklenir. */
enum { PAGE_WELCOME, PAGE_DIR, PAGE_OPTIONS, PAGE_INSTALL, PAGE_FINISH, PAGE_COUNT };
enum { IDC_DIR = 202, IDC_BROWSE, IDC_STARTUP, IDC_LAUNCH, IDC_HEADING, IDC_BODY };
#define WM_INSTALL_DONE (WM_APP + 50)

static BOOL g_ok, g_finished, g_launch_now, g_started;
static HICON g_icon;
static HFONT g_heading_font;
static HANDLE g_worker;
#ifdef DEV_SHOT
static int g_preview, g_preview_page;
#endif

static HWND wizard_control(HWND page, const wchar_t *cls, const wchar_t *text,
                           DWORD style, int id, int x, int y, int w, int h)
{
    RECT r = { x, y, x + w, y + h };
    HWND ctl;
    MapDialogRect(page, &r);
    ctl = CreateWindowExW((style & WS_BORDER) ? WS_EX_CLIENTEDGE : 0, cls, text,
                         WS_CHILD | WS_VISIBLE | style, r.left, r.top,
                         r.right - r.left, r.bottom - r.top, page, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(ctl, WM_SETFONT, SendMessageW(page, WM_GETFONT, 0, 0), TRUE);
    return ctl;
}

static BOOL wizard_valid_dir(const wchar_t *path)
{
    size_t n = wcslen(path);
    const wchar_t *last = wcsrchr(path, L'\\');
    return n > 3 && n < MAX_PATH - 64 && path[1] == L':' && path[2] == L'\\' &&
           last && !_wcsicmp(last + 1, APP_ID) && !wcspbrk(path, L"\"<>|?*");
}

static void wizard_browse(HWND page)
{
    BROWSEINFOW bi = { 0 };
    PIDLIST_ABSOLUTE pid;
    wchar_t dir[MAX_PATH], *last;
    bi.hwndOwner = page;
    bi.lpszTitle = T(L"TunnelDPI'ın kurulacağı klasörü seç", L"Choose a folder for TunnelDPI");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pid = SHBrowseForFolderW(&bi);
    if (!pid)
        return;
    if (SHGetPathFromIDListW(pid, dir)) {
        last = wcsrchr(dir, L'\\');
        if (!last || _wcsicmp(last + 1, APP_ID)) {
            size_t n = wcslen(dir);
            if (n && dir[n - 1] == L'\\') dir[--n] = 0;
            if (n + 1 + wcslen(APP_ID) < MAX_PATH)
                wcscat(dir, L"\\" APP_ID);
        }
        SetDlgItemTextW(page, IDC_DIR, dir);
    }
    CoTaskMemFree(pid);
}

static DWORD WINAPI install_thread(LPVOID arg)
{
    HWND page = arg;
#ifdef DEV_SHOT
    if (g_preview) {
        g_ok = g_preview == 1;
        wcscpy(g_err, T(L"Deneme: dosya yazılamadı.", L"Test: could not write a file."));
    } else
#endif
        g_ok = install(page);
    PostMessageW(page, WM_INSTALL_DONE, 0, 0);
    return 0;
}

static void wizard_labels(HWND sheet)
{
    SetDlgItemTextW(sheet, 0x3023, T(L"< Geri", L"< Back"));
    SetDlgItemTextW(sheet, 0x3024, T(L"İleri >", L"Next >"));
    SetDlgItemTextW(sheet, IDCANCEL, T(L"İptal", L"Cancel"));
}

static INT_PTR CALLBACK wizard_page(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    int index = (int)GetWindowLongPtrW(page, GWLP_USERDATA);
    HWND sheet = GetParent(page);
    switch (msg) {
    case WM_INITDIALOG: {
        PROPSHEETPAGEW *p = (PROPSHEETPAGEW *)lp;
        HWND ctl;
        index = (int)p->lParam;
        SetWindowLongPtrW(page, GWLP_USERDATA, index);
        if (index == PAGE_WELCOME || index == PAGE_FINISH) {
            ctl = wizard_control(page, L"STATIC", L"", SS_LEFT, IDC_HEADING, 116, 16, 190, 46);
            SendMessageW(ctl, WM_SETFONT, (WPARAM)g_heading_font, TRUE);
            wizard_control(page, L"STATIC", L"", SS_LEFT, IDC_BODY, 116, 68, 190, 104);
            if (index == PAGE_WELCOME) {
                SetDlgItemTextW(page, IDC_HEADING, T(L"TunnelDPI kurulum sihirbazına hoş geldin", L"Welcome to the TunnelDPI Setup Wizard"));
                SetDlgItemTextW(page, IDC_BODY, g_prev[0]
                    ? T(L"Bu sihirbaz TunnelDPI'ı güncelleyecek. Ayarların, profillerin ve listelerin korunur.\n\nDevam etmek için İleri'ye tıkla.",
                        L"This wizard will update TunnelDPI. Your settings, profiles and lists will be kept.\n\nClick Next to continue.")
                    : T(L"Bu sihirbaz TunnelDPI'ı bilgisayarına kuracak.\n\nTunnelDPI, bazı erişim engellerini aşmak için bağlantı yöntemini değiştirir. VPN değildir; IP adresini gizlemez.\n\nDevam etmek için İleri'ye tıkla.",
                        L"This wizard will install TunnelDPI on your computer.\n\nTunnelDPI changes the connection method to help get around some access blocks. It is not a VPN and does not hide your IP address.\n\nClick Next to continue."));
            } else {
                wizard_control(page, L"BUTTON", T(L"TunnelDPI'ı başlat", L"Launch TunnelDPI"),
                               BS_AUTOCHECKBOX | WS_TABSTOP, IDC_LAUNCH, 116, 171, 190, 16);
                CheckDlgButton(page, IDC_LAUNCH, BST_CHECKED);
            }
        } else if (index == PAGE_DIR) {
            wizard_control(page, L"STATIC", T(L"TunnelDPI aşağıdaki klasöre kurulacak.", L"Setup will install TunnelDPI in the following folder."),
                           SS_LEFT, -1, 8, 12, 300, 25);
            ctl = wizard_control(page, L"EDIT", g_dir, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                 IDC_DIR, 8, 45, 236, 15);
            SendMessageW(ctl, EM_SETLIMITTEXT, MAX_PATH - 65, 0);
            EnableWindow(ctl, !g_prev[0]);
            ctl = wizard_control(page, L"BUTTON", T(L"Gözat…", L"Browse…"), WS_TABSTOP,
                                 IDC_BROWSE, 249, 44, 60, 17);
            EnableWindow(ctl, !g_prev[0]);
            wizard_control(page, L"STATIC", g_prev[0]
                ? T(L"Güncelleme, kurulu sürümün konumuna yapılır.", L"The update uses the existing installation folder.")
                : T(L"Farklı bir konum seçmek için Gözat'a tıkla.\n\nKurulum konumunda TunnelDPI adlı bir klasör oluşturulur.",
                    L"To choose a different location, click Browse.\n\nA folder named TunnelDPI is created at the selected location."),
                SS_LEFT, -1, 8, 76, 300, 52);
        } else if (index == PAGE_OPTIONS) {
            wizard_control(page, L"STATIC", T(L"Başlangıç tercihini seç, ardından kurulumu başlat.", L"Choose your startup preference, then start the installation."),
                           SS_LEFT, -1, 8, 12, 300, 26);
            wizard_control(page, L"BUTTON", T(L"Windows açılışında başlat", L"Start with Windows"),
                           BS_AUTOCHECKBOX | WS_TABSTOP, IDC_STARTUP, 8, 45, 300, 18);
            CheckDlgButton(page, IDC_STARTUP, g_autostart ? BST_CHECKED : BST_UNCHECKED);
            wizard_control(page, L"STATIC", T(L"TunnelDPI açılışta saatin yanında çalışır. Bu tercihi daha sonra Genel sayfasından değiştirebilirsin.",
                                             L"TunnelDPI runs next to the clock at startup. You can change this later from General."),
                           SS_LEFT, -1, 20, 70, 286, 37);
            wizard_control(page, L"STATIC", g_has_legacy
                ? T(L"Önceki sürümün ayarları taşınacak ve eski sürüm kaldırılacak.", L"Settings from the previous version will be migrated and the old version removed.")
                : T(L"Kurulum sırasında çalışan TunnelDPI kısa süreliğine kapanır.", L"The running TunnelDPI app will close briefly during setup."),
                SS_LEFT, -1, 8, 124, 300, 35);
        } else if (index == PAGE_INSTALL) {
            wizard_control(page, L"STATIC", T(L"Kurulum tamamlanırken lütfen bekle.", L"Please wait while Setup installs TunnelDPI."), SS_LEFT, -1, 8, 12, 300, 24);
            wizard_control(page, L"STATIC", L"", SS_LEFT, IDC_STATUS, 8, 48, 300, 28);
            ctl = wizard_control(page, PROGRESS_CLASSW, L"", 0, IDC_PROGRESS, 8, 82, 300, 13);
            SendMessageW(ctl, PBM_SETRANGE32, 0, 100);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BROWSE) wizard_browse(page);
        break;
    case WM_INSTALL_DONE:
        if (g_worker) { CloseHandle(g_worker); g_worker = NULL; }
        PropSheet_SetCurSel(sheet, NULL, PAGE_FINISH);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        switch (n->code) {
        case PSN_SETACTIVE:
#ifdef DEV_SHOT
            g_preview_page = index;
#endif
            PropSheet_SetWizButtons(sheet, index == PAGE_WELCOME ? PSWIZB_NEXT :
                index == PAGE_INSTALL ? 0 : index == PAGE_FINISH ? PSWIZB_FINISH : PSWIZB_BACK | PSWIZB_NEXT);
            wizard_labels(sheet);
            EnableWindow(GetDlgItem(sheet, IDCANCEL), index != PAGE_INSTALL);
            if (index == PAGE_OPTIONS)
                SetDlgItemTextW(sheet, 0x3024, g_prev[0] ? T(L"Güncelle", L"Update") : T(L"Kur", L"Install"));
            if (index == PAGE_INSTALL && !g_started) {
                g_started = TRUE;
                g_worker = CreateThread(NULL, 0, install_thread, page, 0, NULL);
                if (!g_worker) {
                    wcscpy(g_err, T(L"Kurulum başlatılamadı. Yeniden dene.", L"Setup could not start. Try again."));
                    PostMessageW(page, WM_INSTALL_DONE, 0, 0);
                }
            }
            if (index == PAGE_FINISH) {
                SetDlgItemTextW(page, IDC_HEADING, g_ok ? T(L"Kurulum tamamlandı", L"Completing the TunnelDPI Setup Wizard")
                                                                      : T(L"Kurulum tamamlanamadı", L"Setup could not finish"));
                SetDlgItemTextW(page, IDC_BODY, g_ok
                    ? T(L"TunnelDPI bilgisayarına kuruldu.\n\nİlk açılışta kısa bir kullanım rehberi seni karşılar. Rehberi daha sonra Genel sayfasından yeniden açabilirsin.\n\nSihirbazı kapatmak için Bitir'e tıkla.",
                        L"TunnelDPI has been installed on your computer.\n\nA short guide opens on the first launch. You can reopen it later from General.\n\nClick Finish to close Setup.") : g_err);
                ShowWindow(GetDlgItem(page, IDC_LAUNCH), g_ok ? SW_SHOW : SW_HIDE);
                SendMessageW(sheet, PSM_SETFINISHTEXTW, 0, (LPARAM)(g_ok ? T(L"Bitir", L"Finish") : T(L"Kapat", L"Close")));
                ShowWindow(GetDlgItem(sheet, IDCANCEL), SW_HIDE);
            }
            return TRUE;
        case PSN_WIZNEXT:
            if (index == PAGE_DIR && !g_prev[0]) {
                wchar_t dir[MAX_PATH];
                GetDlgItemTextW(page, IDC_DIR, dir, MAX_PATH);
                if (!wizard_valid_dir(dir)) {
                    MessageBoxW(page, T(L"TunnelDPI adlı bir klasör seç. Örnek: D:\\Programlar\\TunnelDPI",
                                       L"Choose a folder named TunnelDPI, for example D:\\Programs\\TunnelDPI."), APP_NAME, MB_ICONWARNING);
                    SetWindowLongPtrW(page, DWLP_MSGRESULT, -1);
                    return TRUE;
                }
                wcscpy(g_dir, dir);
            }
            if (index == PAGE_OPTIONS)
                g_autostart = IsDlgButtonChecked(page, IDC_STARTUP) == BST_CHECKED;
            return TRUE;
        case PSN_QUERYCANCEL:
            SetWindowLongPtrW(page, DWLP_MSGRESULT, index == PAGE_INSTALL);
            return TRUE;
        case PSN_WIZFINISH:
            g_finished = TRUE;
            g_launch_now = g_ok && IsDlgButtonChecked(page, IDC_LAUNCH) == BST_CHECKED;
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        if (index == PAGE_WELCOME || index == PAGE_FINISH) {
            SetBkMode((HDC)wp, TRANSPARENT);
            return (INT_PTR)GetStockObject(WHITE_BRUSH);
        }
        break;
    case WM_ERASEBKGND:
        if (index == PAGE_WELCOME || index == PAGE_FINISH) {
            RECT r;
            GetClientRect(page, &r);
            FillRect((HDC)wp, &r, GetStockObject(WHITE_BRUSH));
            return TRUE;
        }
        break;
    case WM_PAINT:
        if (index == PAGE_WELCOME || index == PAGE_FINISH) {
            PAINTSTRUCT ps;
            RECT r, band = { 0, 0, 100, 193 };
            HDC dc = BeginPaint(page, &ps);
            HBRUSH blue = CreateSolidBrush(RGB(28, 58, 93));
            MapDialogRect(page, &band);
            GetClientRect(page, &r);
            band.bottom = r.bottom;
            FillRect(dc, &band, blue);
            DeleteObject(blue);
            if (g_icon) DrawIconEx(dc, (band.right - 64) / 2, 40, g_icon, 64, 64, 0, NULL, DI_NORMAL);
            SetTextColor(dc, RGB(255,255,255));
            SetBkMode(dc, TRANSPARENT);
            r = band; r.top = 120;
            SelectObject(dc, g_heading_font);
            DrawTextW(dc, APP_NAME, -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE);
            EndPaint(page, &ps);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int run_ui(void)
{
    PROPSHEETPAGEW pages[PAGE_COUNT] = { 0 };
    PROPSHEETHEADERW sheet = { 0 };
    union { DWORD align; BYTE bytes[128]; } templates[PAGE_COUNT];
    const wchar_t *titles[] = { L"", T(L"Kurulum konumu", L"Select Destination Location"),
        T(L"Ek görevler", L"Select Additional Tasks"), T(L"Kuruluyor", L"Installing"), L"" };
    const wchar_t *subtitles[] = { L"", T(L"TunnelDPI nereye kurulsun?", L"Where should TunnelDPI be installed?"),
        T(L"Hangi ek görevler yapılsın?", L"Which additional tasks should be performed?"),
        T(L"Dosyalar bilgisayarına kopyalanıyor.", L"Setup is copying files to your computer."), L"" };
    wchar_t caption[80];
    HDC dc = GetDC(NULL);
    int i, dpi = GetDeviceCaps(dc, LOGPIXELSY);
    INT_PTR result;
    ReleaseDC(NULL, dc);
    g_ok = g_finished = g_started = FALSE;
    g_launch_now = TRUE;
    g_err[0] = 0;
    g_heading_font = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (i = 0; i < PAGE_COUNT; i++) {
        DLGTEMPLATE *d = (DLGTEMPLATE *)templates[i].bytes;
        WORD *w;
        memset(templates[i].bytes, 0, sizeof(templates[i].bytes));
        d->style = WS_CHILD | WS_DISABLED | DS_CONTROL | DS_SETFONT;
        d->cx = 317; d->cy = 193;
        w = (WORD *)(d + 1);
        *w++ = 0; *w++ = 0; *w++ = 0; *w++ = 9;
        wcscpy((wchar_t *)w, L"Segoe UI");
        pages[i].dwSize = sizeof(pages[i]);
        pages[i].dwFlags = PSP_DLGINDIRECT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        if (i == PAGE_WELCOME || i == PAGE_FINISH) pages[i].dwFlags |= PSP_HIDEHEADER;
        pages[i].hInstance = g_inst;
        pages[i].pResource = d;
        pages[i].pfnDlgProc = wizard_page;
        pages[i].lParam = i;
        pages[i].pszHeaderTitle = titles[i];
        pages[i].pszHeaderSubTitle = subtitles[i];
    }
    _snwprintf(caption, 80, T(L"Kurulum - %s", L"Setup - %s"), APP_NAME);
    sheet.dwSize = sizeof(sheet);
    sheet.dwFlags = PSH_WIZARD97 | PSH_PROPSHEETPAGE | (g_icon ? PSH_USEHICON : 0);
    sheet.hInstance = g_inst;
    sheet.hIcon = g_icon;
    sheet.pszCaption = caption;
    sheet.nPages = PAGE_COUNT;
    sheet.ppsp = pages;
    result = PropertySheetW(&sheet);
    DeleteObject(g_heading_font);
    if (result == -1) return 1;
    if (!g_finished) return 2;
    if (!g_ok) return 1;
    if (g_launch_now
#ifdef DEV_SHOT
        && !g_preview
#endif
       ) launch();
    return 0;
}
