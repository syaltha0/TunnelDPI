"""TunnelDPI arayüz çevirileri.

en.txt satırları: <Türkçe metin><TAB><İngilizce metin>; metinler kaynakta
yazıldığı gibi (C kaçışlarıyla) tutulur. Arayüz Türkçe yazılır ve tr()
çalışma anında İngilizcesini bu tablodan bulur.

Kullanım: python -X utf8 gen.py
  - src/i18n_table.h dosyasını üretir
  - kaynakta tr() ile kullanılıp tabloda olmayan metinleri listeler
    (eksik varsa çıkış kodu 1)
"""
import glob
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")

table = {}
for ln in io.open(os.path.join(HERE, "en.txt"), encoding="utf-8"):
    ln = ln.rstrip("\r\n")
    if "\t" in ln:
        tr_, en = ln.split("\t", 1)
        table[tr_] = en

with io.open(os.path.join(SRC, "i18n_table.h"), "w", encoding="utf-8", newline="\n") as f:
    f.write("/* Üretilmiş dosya: app/i18n/gen.py ile en.txt'den. Elle düzenlemeyin. */\n")
    for k, v in table.items():
        f.write('    { L"%s", L"%s" },\n' % (k, v))

# tr(L"..." L"...") çağrıları; dizilerde tutulup tr(dizi[i]) ile çevrilenler
# (sayfa adları, açıklamalar) tabloda olduğu sürece bu denetime takılmaz.
call = re.compile(r'tr\(((?:L"(?:[^"\\]|\\.)*"\s*)+)\)')
piece = re.compile(r'L"((?:[^"\\]|\\.)*)"')
missing = []
for path in sorted(glob.glob(os.path.join(SRC, "*.c"))):
    for m in call.finditer(io.open(path, encoding="utf-8").read()):
        text = "".join(piece.findall(m.group(1)))
        if text not in table and text not in missing:
            missing.append(text)

print("table: %d entries" % len(table))
for t in missing:
    print("missing:", t)
sys.exit(1 if missing else 0)
