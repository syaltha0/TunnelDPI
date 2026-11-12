/*
 * TunnelDPI - indirilen dosyaların doğrulanması
 *
 * Yayın sahibinin gizli anahtarıyla (ECDSA P-256) imzalanmış dosyalar,
 * uygulamaya gömülü genel anahtarla doğrulanır. Gizli anahtar depoda
 * değildir; GitHub'da RELEASE_SIGNING_KEY gizli değişkeninde ve yayıncının
 * bilgisayarında durur. İmzası tutmayan güncelleme ya da hazır ayar
 * kullanılmaz.
 */
#pragma once
#include "app.h"

BOOL sha256_buf(const void *data, DWORD len, BYTE out[32]);
/* sig: openssl dgst -sha256 -sign çıktısı (DER) */
BOOL signature_ok(const void *msg, DWORD len, const BYTE *sig, DWORD siglen);
void hex_lower(const BYTE *b, int n, char *out);   /* out: 2n+1 */
