"""Kurulum paketi: dist klasörünü setup.c'nin okuduğu payload.bin biçimine çevirir.

Kullanım: python pack.py <dist klasörü> <payload.bin>

Biçim: "GCPK" u32 sayı, her kayıt: u16 bayrak, u16 yol uzunluğu, yol (UTF-8,
'/' ayraçlı), u32 boyut, veri. Bayrak 1 = kullanıcı verisi (kurulu dosya
varsa üzerine yazılmaz): site listeleri ve uygulama listesi.
"""
import os
import struct
import sys

KEEP = 1
SKIP = {"tunneldpi.ini", "gdpi-tray.ini", "tunneldpi.log", "tunneldpi.log.old", "dns-restore.ini"}


def flags_for(rel):
    low = rel.lower()
    if low.startswith("lists/") and (low.endswith(".txt") or low.endswith(".ini")):
        return KEEP
    if low == "apps.txt":
        return KEEP
    return 0


def main(src, out):
    entries = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            if rel.lower() in SKIP or rel.lower().endswith((".new", ".tmp")) or ".old" in rel.lower():
                continue
            with open(full, "rb") as f:
                entries.append((flags_for(rel), rel.encode("utf-8"), f.read()))
    with open(out, "wb") as f:
        f.write(b"GCPK" + struct.pack("<I", len(entries)))
        for fl, path, data in entries:
            f.write(struct.pack("<HH", fl, len(path)) + path + struct.pack("<I", len(data)) + data)
    total = sum(len(e[2]) for e in entries)
    print("payload: %d files, %.1f MB" % (len(entries), total / 1048576))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
