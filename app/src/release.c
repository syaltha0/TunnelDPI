/* Bu özet, birlikte dağıtıldığı APP_VERSION sürümünün yerel notudur. */
#include "release.h"
#include "config.h"
#include "i18n.h"

void release_show(HWND owner)
{
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    wchar_t heading[128];
    _snwprintf(heading, 128, tr(L"TunnelDPI %s · Bu sürümde"), APP_VERSION);
    tc.hwndParent = owner;
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = APP_NAME;
    tc.pszMainInstruction = heading;
    tc.pszContent = tr(L"• Kurulum sihirbazı ve ilk kullanım rehberi yenilendi.\n\n• Hatalara çözüm düğmeleri, profil aramasına kalıcı sonuç özeti eklendi.\n\n• Destek raporu, klavye erişimi ve ekran ölçeği iyileştirildi.");
    tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    tc.cxWidth = 360;
    if (SUCCEEDED(TaskDialogIndirect(&tc, NULL, NULL, NULL))) {
        wcscpy(g_cfg.notes_version, APP_VERSION);
        config_save(&g_cfg);
    }
}
