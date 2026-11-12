/*
 * TunnelDPI - indirilen dosyaların doğrulanması
 *
 * bcrypt.dll yalnızca doğrulama sırasında yüklenir. İmza DER biçiminde
 * gelir (SEQUENCE { INTEGER r, INTEGER s }); BCrypt ise 64 baytlık r||s
 * bekler.
 */
#include "verify.h"
#include <bcrypt.h>
#include <string.h>

/* Yayın imzası genel anahtarı (P-256, X||Y). Değiştirmek için:
   openssl ec -in imza-anahtari.pem -pubout -outform DER | tail -c 64 */
static const BYTE g_pub[64] = {
    0xe6, 0xae, 0x15, 0xe3, 0x9c, 0x26, 0xed, 0x0b, 0x6e, 0x6f, 0x22, 0x09, 0xd2, 0x8b, 0x77, 0xcf,
    0xb7, 0xf1, 0xe2, 0xb6, 0x50, 0xda, 0x62, 0xc9, 0xd1, 0x5a, 0x2d, 0x77, 0x2a, 0xbd, 0xf5, 0xc9,
    0xb3, 0xb9, 0x95, 0x9b, 0x82, 0x33, 0xaf, 0x11, 0xc0, 0x0b, 0x38, 0x83, 0x83, 0xb5, 0x86, 0xc9,
    0x45, 0xad, 0x6d, 0x8a, 0xff, 0x8e, 0x93, 0xee, 0xeb, 0x8f, 0x07, 0x30, 0x7d, 0x81, 0x9d, 0xde,
};

typedef NTSTATUS (WINAPI *OpenAlg_t)(BCRYPT_ALG_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
typedef NTSTATUS (WINAPI *CloseAlg_t)(BCRYPT_ALG_HANDLE, ULONG);
typedef NTSTATUS (WINAPI *Hash_t)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, PUCHAR, ULONG);
typedef NTSTATUS (WINAPI *ImportKey_t)(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE, LPCWSTR, BCRYPT_KEY_HANDLE *,
                                       PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *Verify_t)(BCRYPT_KEY_HANDLE, VOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *DestroyKey_t)(BCRYPT_KEY_HANDLE);

static HMODULE load(void)
{
    return LoadLibraryExW(L"bcrypt.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

BOOL sha256_buf(const void *data, DWORD len, BYTE out[32])
{
    HMODULE m = load();
    OpenAlg_t pOpen;
    CloseAlg_t pClose;
    Hash_t pHash;
    BCRYPT_ALG_HANDLE alg;
    BOOL ok = FALSE;

    if (!m)
        return FALSE;
    pOpen = (OpenAlg_t)(void *)GetProcAddress(m, "BCryptOpenAlgorithmProvider");
    pClose = (CloseAlg_t)(void *)GetProcAddress(m, "BCryptCloseAlgorithmProvider");
    pHash = (Hash_t)(void *)GetProcAddress(m, "BCryptHash");
    if (pOpen && pClose && pHash && pOpen(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        ok = pHash(alg, NULL, 0, (PUCHAR)data, len, out, 32) == 0;
        pClose(alg, 0);
    }
    FreeLibrary(m);
    return ok;
}

/* DER INTEGER → 32 bayt, büyük uçlu, soldan sıfırla doldurulmuş */
static const BYTE *der_int(const BYTE *p, const BYTE *end, BYTE out[32])
{
    DWORD n;

    if (p + 2 > end || p[0] != 0x02)
        return NULL;
    n = p[1];
    p += 2;
    if (p + n > end || n == 0)
        return NULL;
    while (n > 32 && *p == 0) {
        p++;
        n--;
    }
    if (n > 32)
        return NULL;
    memset(out, 0, 32);
    memcpy(out + 32 - n, p, n);
    return p + n;
}

BOOL signature_ok(const void *msg, DWORD len, const BYTE *sig, DWORD siglen)
{
    struct { BCRYPT_ECCKEY_BLOB h; BYTE xy[64]; } blob;
    BYTE hash[32], rs[64];
    const BYTE *p = sig, *end = sig + siglen;
    HMODULE m;
    OpenAlg_t pOpen;
    CloseAlg_t pClose;
    ImportKey_t pImport;
    Verify_t pVerify;
    DestroyKey_t pDestroy;
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_KEY_HANDLE key;
    BOOL ok = FALSE;

    if (siglen < 8 || p[0] != 0x30 || p[1] + 2u != siglen)
        return FALSE;
    p += 2;
    if (!(p = der_int(p, end, rs)) || !(p = der_int(p, end, rs + 32)) || p != end)
        return FALSE;
    if (!sha256_buf(msg, len, hash))
        return FALSE;

    m = load();
    if (!m)
        return FALSE;
    pOpen = (OpenAlg_t)(void *)GetProcAddress(m, "BCryptOpenAlgorithmProvider");
    pClose = (CloseAlg_t)(void *)GetProcAddress(m, "BCryptCloseAlgorithmProvider");
    pImport = (ImportKey_t)(void *)GetProcAddress(m, "BCryptImportKeyPair");
    pVerify = (Verify_t)(void *)GetProcAddress(m, "BCryptVerifySignature");
    pDestroy = (DestroyKey_t)(void *)GetProcAddress(m, "BCryptDestroyKey");
    if (pOpen && pClose && pImport && pVerify && pDestroy && pOpen(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) == 0) {
        blob.h.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
        blob.h.cbKey = 32;
        memcpy(blob.xy, g_pub, 64);
        if (pImport(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key, (PUCHAR)&blob, sizeof(blob), 0) == 0) {
            ok = pVerify(key, NULL, hash, 32, rs, 64, 0) == 0;
            pDestroy(key);
        }
        pClose(alg, 0);
    }
    FreeLibrary(m);
    return ok;
}

void hex_lower(const BYTE *b, int n, char *out)
{
    static const char d[] = "0123456789abcdef";
    int i;

    for (i = 0; i < n; i++) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = 0;
}
