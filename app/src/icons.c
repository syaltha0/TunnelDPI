/*
 * TunnelDPI - durum ikonları
 * Uygulama ikonunun sağ alt köşesine renkli bir durum noktası çizer.
 */
#include "icons.h"
#include "../res/resource.h"
#include <math.h>

static HICON g_icons[ICON_COUNT];

static HICON build(int state)
{
    static const COLORREF colors[ICON_COUNT] = {
        RGB(140, 140, 140),  /* kapalı */
        RGB(34, 197, 94),    /* aktif */
        RGB(239, 68, 68),    /* hata */
        RGB(245, 158, 11),   /* başlıyor */
    };
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    HICON base = LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    ICONINFO ii = {0};
    BITMAPINFO bi = {0};
    BYTE *px = NULL;
    HDC dc;
    HICON out = NULL;
    HBITMAP color = NULL, mask = NULL;
    int x, y, i;
    float r, ccx, ccy;
    BOOL has_alpha = FALSE;

    if (!base || !GetIconInfo(base, &ii))
        return base;

    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    dc = GetDC(NULL);
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!color || !GetDIBits(dc, ii.hbmColor, 0, cy, px, &bi, DIB_RGB_COLORS))
        goto done;

    for (i = 0; i < cx * cy; i++)
        if (px[i * 4 + 3]) { has_alpha = TRUE; break; }
    if (!has_alpha) {
        /* Alfa yoksa maskeden üret */
        BYTE *m = HeapAlloc(GetProcessHeap(), 0, cx * cy * 4);
        if (m && GetDIBits(dc, ii.hbmMask, 0, cy, m, &bi, DIB_RGB_COLORS))
            for (i = 0; i < cx * cy; i++)
                px[i * 4 + 3] = m[i * 4] ? 0 : 255;
        if (m)
            HeapFree(GetProcessHeap(), 0, m);
    }

    /* Durum noktası: kenar çizgili, kenar yumuşatmalı daire */
    r = cx * 0.27f;
    ccx = cx - r - 0.5f;
    ccy = cy - r - 0.5f;
    for (y = 0; y < cy; y++) {
        for (x = 0; x < cx; x++) {
            float d = sqrtf((x + 0.5f - ccx) * (x + 0.5f - ccx) + (y + 0.5f - ccy) * (y + 0.5f - ccy));
            float outer = r + 1.0f - d;          /* beyaz kenar dahil */
            float inner = r - d;
            BYTE *p = px + (y * cx + x) * 4;
            float a, sa, da, cr, cg, cb;

            if (outer <= 0)
                continue;
            a = outer > 1 ? 1 : outer;           /* toplam kapsama */
            inner = inner < 0 ? 0 : (inner > 1 ? 1 : inner);
            /* nokta rengi ile beyaz kenar karışımı */
            cr = GetRValue(colors[state]) * inner + 255 * (1 - inner);
            cg = GetGValue(colors[state]) * inner + 255 * (1 - inner);
            cb = GetBValue(colors[state]) * inner + 255 * (1 - inner);
            /* düz alfa ile "over" birleştirme */
            sa = a;
            da = p[3] / 255.0f;
            {
                float oa = sa + da * (1 - sa);
                if (oa > 0) {
                    p[2] = (BYTE)((cr * sa + p[2] * da * (1 - sa)) / oa);
                    p[1] = (BYTE)((cg * sa + p[1] * da * (1 - sa)) / oa);
                    p[0] = (BYTE)((cb * sa + p[0] * da * (1 - sa)) / oa);
                }
                p[3] = (BYTE)(oa * 255);
            }
        }
    }

    mask = CreateBitmap(cx, cy, 1, 1, NULL);
    {
        ICONINFO ni = { TRUE, 0, 0, mask, color };
        out = CreateIconIndirect(&ni);
    }

done:
    ReleaseDC(NULL, dc);
    if (color)
        DeleteObject(color);
    if (mask)
        DeleteObject(mask);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    if (out) {
        DestroyIcon(base);
        return out;
    }
    return base;
}

HICON icon_get(int state)
{
    if (state < 0 || state >= ICON_COUNT)
        state = 0;
    if (!g_icons[state])
        g_icons[state] = build(state);
    return g_icons[state];
}

void icons_reset(void)
{
    int i;

    for (i = 0; i < ICON_COUNT; i++) {
        if (g_icons[i])
            DestroyIcon(g_icons[i]);
        g_icons[i] = NULL;
    }
}
