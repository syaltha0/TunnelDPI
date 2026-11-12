/*
 * TunnelDPI - arayüz dili
 * Türkçe metin anahtardır; İngilizce seçiliyse tablodan karşılığı döner.
 */
#pragma once
#include <wchar.h>

enum { LANG_AUTO = 0, LANG_TR = 1, LANG_EN = 2 };

void           i18n_set(int lang);
int            i18n_is_en(void);
const wchar_t *tr(const wchar_t *s);
