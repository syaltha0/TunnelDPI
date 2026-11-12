/* İlk kullanım rehberi: bilgi verir; koruma ve ağ ayarlarını değiştirmez. */
#include "guide.h"
#include "i18n.h"
#include "settings.h"

enum { GUIDE_BACK = 100, GUIDE_NEXT, GUIDE_DONE };

typedef struct {
    TASKDIALOGCONFIG pages[4];
    TASKDIALOG_BUTTON buttons[4][2];
} Guide;

static HRESULT CALLBACK guide_cb(HWND h, UINT event, WPARAM wp, LPARAM lp, LONG_PTR ref)
{
    Guide *g = (Guide *)ref;
    int page;
    (void)lp;
    if (event != TDN_BUTTON_CLICKED)
        return S_OK;
    /* Geçerli sayfa pencerenin özelliğinde tutulur; tüm içerik modal çağrı boyunca yaşar. */
    page = (int)(INT_PTR)GetPropW(h, L"TunnelDPIGuidePage");
    if (wp == GUIDE_BACK || wp == GUIDE_NEXT) {
        page += wp == GUIDE_NEXT ? 1 : -1;
        if (page >= 0 && page < 4) {
            SetPropW(h, L"TunnelDPIGuidePage", (HANDLE)(INT_PTR)page);
            SendMessageW(h, TDM_NAVIGATE_PAGE, 0, (LPARAM)&g->pages[page]);
        }
        return S_FALSE;
    }
    RemovePropW(h, L"TunnelDPIGuidePage");
    return S_OK;
}

void guide_show(HWND owner)
{
    static BOOL showing;
    Guide g = { 0 };
    int i, result = 0;
    const wchar_t *head[] = {
        tr(L"TunnelDPI'a hoş geldin"), tr(L"Korumayı kontrol et"),
        tr(L"Açılmayan bir siteyi dene"), tr(L"Saatin yanında hazır")
    };
    const wchar_t *body[] = {
        tr(L"Bu kısa rehberle başlayabilirsin.\n\nTunnelDPI, bazı erişim engellerini aşmak için bağlantı yöntemini değiştirir. VPN değildir; IP adresini gizlemez.\n\nRehberi daha sonra Genel sayfasından yeniden açabilirsin."),
        tr(L"Durum sayfasındaki büyük güç düğmesi korumayı açar ve kapatır. Durumda Açık yazdığında koruma çalışıyordur.\n\nProfil, bağlantıda kullanılan yöntemdir. Başlangıç ayarıyla başlayabilirsin; her ayarı değiştirmen gerekmez."),
        tr(L"Durum sayfasında Site adresi kutusuna sende açılmayan bir site yaz ve Açılmasını sağla düğmesine bas.\n\nHâlâ açılmıyorsa Genel sayfasında Test sitesi olarak bu adresi seç. Ardından Durum sayfasında En uygun profili bul → Hızlı ara seçeneğini kullan.\n\nArama sürerken bağlantın kısa süreliğine kesilebilir."),
        tr(L"Pencereyi kapatsan da TunnelDPI saatin yanında çalışır. Simge gizliyse saatin yanındaki oka bak.\n\nSimgeye tıkla: ayarlar. Sağ tıkla: hızlı menü. Tamamen çıkmak için Genel → TunnelDPI'ı kapat seçeneğini kullan.\n\nWindows açılışında başlatma seçeneğini Genel sayfasından değiştirebilirsin.")
    };
    if (showing)
        return;
    showing = TRUE;
    for (i = 0; i < 4; i++) {
        TASKDIALOGCONFIG *p = &g.pages[i];
        p->cbSize = sizeof(*p);
        p->hwndParent = owner;
        p->dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        p->pszWindowTitle = tr(L"Başlangıç rehberi");
        p->pszMainIcon = TD_INFORMATION_ICON;
        p->pszMainInstruction = head[i];
        p->pszContent = body[i];
        p->pszFooter = i == 0 ? L"1 / 4" : i == 1 ? L"2 / 4" : i == 2 ? L"3 / 4" : L"4 / 4";
        p->cxWidth = 360;
        p->cButtons = i ? 2 : 1;
        p->pButtons = g.buttons[i];
        if (i) {
            g.buttons[i][0].nButtonID = GUIDE_BACK;
            g.buttons[i][0].pszButtonText = tr(L"Geri");
        }
        g.buttons[i][i ? 1 : 0].nButtonID = i == 3 ? GUIDE_DONE : GUIDE_NEXT;
        g.buttons[i][i ? 1 : 0].pszButtonText = i == 3 ? tr(L"Durum sayfasını aç") : tr(L"İleri");
        p->nDefaultButton = i == 3 ? GUIDE_DONE : GUIDE_NEXT;
        p->dwCommonButtons = TDCBF_CANCEL_BUTTON;
        p->pfCallback = guide_cb;
        p->lpCallbackData = (LONG_PTR)&g;
    }
    if (SUCCEEDED(TaskDialogIndirect(&g.pages[0], &result, NULL, NULL)) && result == GUIDE_DONE)
        settings_show(0);
    showing = FALSE;
}
