#pragma once
#include "app.h"
void notify_show(const wchar_t *title, const wchar_t *body, BOOL error);
void notify_close(void);
BOOL notify_allow(const wchar_t *title, const wchar_t *body, BOOL error, ULONGLONG now);
