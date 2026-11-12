"""README başlık görseli: python .github/make_banner.py  -> .github/banner.png

Gereken: Pillow. Uygulama simgesi app/res/icon.png'den, ekran görüntüsü
(isteğe bağlı) .github/panel.png'den alınır.
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W, H = 1280, 640
FONTS = "C:/Windows/Fonts/"


def font(names, size):
    for n in names:
        try:
            return ImageFont.truetype(FONTS + n, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    im = Image.new("RGB", (W, H), (16, 20, 30))
    d = ImageDraw.Draw(im)
    # yumuşak mavi ışık lekeleri
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    g.ellipse([-200, -260, 620, 520], fill=(24, 80, 170))
    g.ellipse([820, 300, 1500, 900], fill=(18, 60, 130))
    glow = glow.filter(ImageFilter.GaussianBlur(160))
    im = Image.blend(im, glow, 0.55)
    d = ImageDraw.Draw(im)

    icon = Image.open(os.path.join(ROOT, "app", "res", "icon.png")).convert("RGBA").resize((132, 132), Image.LANCZOS)
    im.paste(icon, (96, 150), icon)

    title = font(["segoeuib.ttf", "arialbd.ttf"], 92)
    sub = font(["segoeui.ttf", "arial.ttf"], 34)
    small = font(["segoeuisb.ttf", "segoeui.ttf"], 22)
    d.text((252, 150), "TunnelDPI", font=title, fill=(255, 255, 255))
    d.text((256, 262), "İnternet engellerini VPN'siz aş", font=sub, fill=(190, 205, 230))

    x, y = 100, 360
    for chip in ["Tek tıkla aç / kapat", "Engelli siteleri kendi öğrenir", "Reklam engelleme",
                 "Discord sesi", "Hafif: ~2 MB bellek"]:
        w = d.textlength(chip, font=small) + 36
        if x + w > 820:
            x, y = 100, y + 54
        d.rounded_rectangle([x, y, x + w, y + 40], radius=20, fill=(34, 44, 64), outline=(64, 84, 120))
        d.text((x + 18, y + 7), chip, font=small, fill=(220, 230, 245))
        x += w + 12

    d.text((100, H - 70), "Windows 10 / 11  ·  Açık kaynak  ·  GoodbyeDPI ve zapret tabanlı", font=small,
           fill=(130, 145, 170))

    panel = os.path.join(HERE, "panel.png")
    if os.path.exists(panel):
        p = Image.open(panel).convert("RGBA")
        p = p.resize((int(p.width * 300 / p.height), 300), Image.LANCZOS)
        shadow = Image.new("RGBA", (p.width + 60, p.height + 60), (0, 0, 0, 0))
        ImageDraw.Draw(shadow).rounded_rectangle([30, 36, p.width + 30, p.height + 36], radius=14, fill=(0, 0, 0, 150))
        shadow = shadow.filter(ImageFilter.GaussianBlur(14))
        px, py = W - p.width - 110, 120
        im.paste(shadow, (px - 30, py - 30), shadow)
        mask = Image.new("L", p.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, p.width, p.height], radius=12, fill=255)
        im.paste(p, (px, py), mask)

    im.save(os.path.join(HERE, "banner.png"), optimize=True)


if __name__ == "__main__":
    main()
