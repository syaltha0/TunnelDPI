"""TunnelDPI simgesi: yuvarlatılmış kare zemin üzerinde kemerli geçit ve içinden geçen ok."""
import sys
from PIL import Image, ImageDraw

S = 1024  # süper örnekleme


def draw():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = 40
    # zemin: koyu mavi -> biraz açık, dikey
    top, bot = (24, 98, 196), (13, 71, 161)
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,)
        gd.line([(0, y), (S, y)], fill=c)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([pad, pad, S - pad, S - pad], radius=220, fill=255)
    im.paste(grad, (0, 0), mask)

    white = (255, 255, 255, 255)
    # kemer: dış kemerden iç kemeri çıkar
    w = 92                      # çizgi kalınlığı
    x0, x1 = 262, S - 262       # kemer dış kenarları
    ybase = S - 236             # ayakların alt ucu
    r = (x1 - x0) // 2
    cy = 430                    # yarım dairenin merkezi
    arch = Image.new("L", (S, S), 0)
    ad = ImageDraw.Draw(arch)
    ad.pieslice([x0, cy - r, x1, cy + r], 180, 360, fill=255)
    ad.rectangle([x0, cy, x1, ybase], fill=255)
    ri = r - w
    ad.pieslice([x0 + w, cy - ri, x1 - w, cy + ri], 180, 360, fill=0)
    ad.rectangle([x0 + w, cy, x1 - w, ybase], fill=0)
    # ayak uçlarını yuvarlat
    ad.ellipse([x0, ybase - w // 2, x0 + w, ybase + w // 2], fill=255)
    ad.ellipse([x1 - w, ybase - w // 2, x1, ybase + w // 2], fill=255)
    im.paste(white, (0, 0), arch)

    # içinden geçen ok (chevron), açık mavi-beyaz
    acc = (138, 212, 255, 255)
    cxm, cym = S // 2 + 6, 560
    a = 110
    t = 84
    pts = [(cxm - a // 2 - 20, cym - a), (cxm + a // 2 + 10, cym)]
    chev = Image.new("L", (S, S), 0)
    cd = ImageDraw.Draw(chev)
    cd.line([(cxm - 60, cym - 120), (cxm + 60, cym), (cxm - 60, cym + 120)], fill=255, width=t, joint="curve")
    for p in [(cxm - 60, cym - 120), (cxm + 60, cym), (cxm - 60, cym + 120)]:
        cd.ellipse([p[0] - t // 2, p[1] - t // 2, p[0] + t // 2, p[1] + t // 2], fill=255)
    im.paste(acc, (0, 0), chev)
    return im


def main(out_ico, out_png):
    big = draw()
    big.resize((256, 256), Image.LANCZOS).save(out_png)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(out_ico, sizes=[(s, s) for s in sizes])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
