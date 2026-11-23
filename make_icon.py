"""Original ghost icon (drawn from scratch: a wispy spirit with a curling tail, raised arms,
cross brows and a sly grin). Writes a multi-size .ico and a preview PNG."""
import sys, math
from PIL import Image, ImageDraw, ImageFilter

S = 1024          # drawing size (rendered 2x for smooth edges)
K = 2
W = S * K

def bez(p0, p1, p2, p3, n=40):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0],
                    u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1]))
    return out

def path(segs):
    pts = []
    for s in segs:
        b = bez(*s)
        pts += b if not pts else b[1:]
    return [(x*K, y*K) for x, y in pts]

# body outline, clockwise from the top of the head
body = path([
    ((512, 110), (700, 110), (820, 250), (815, 430)),      # head, right
    ((815, 430), (812, 520), (830, 560), (900, 520)),      # right arm (raised hand)
    ((900, 520), (950, 490), (965, 560), (900, 610)),
    ((900, 610), (840, 655), (800, 640), (790, 700)),
    ((790, 700), (780, 800), (840, 860), (900, 850)),      # tail sweeps right and curls
    ((900, 850), (960, 840), (950, 930), (880, 945)),
    ((880, 945), (760, 970), (650, 900), (610, 840)),
    ((610, 840), (580, 880), (530, 900), (490, 860)),      # wisps along the bottom
    ((490, 860), (450, 900), (380, 900), (350, 850)),
    ((350, 850), (300, 880), (240, 860), (250, 800)),
    ((250, 800), (255, 720), (240, 650), (200, 630)),      # left arm
    ((200, 630), (120, 600), (70, 520), (110, 490)),
    ((110, 490), (150, 460), (190, 520), (215, 500)),
    ((215, 500), (220, 420), (210, 300), (300, 190)),      # head, left
    ((300, 190), (360, 130), (430, 110), (512, 110)),
])

def draw_icon():
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    # soft glow behind the ghost
    glow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(glow).polygon(body, fill=(120, 230, 255, 170))
    glow = glow.filter(ImageFilter.GaussianBlur(28 * K))
    img.alpha_composite(glow)

    # body: pale blue-white, shaded towards the lower left
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).polygon(body, fill=255)
    shade = Image.new("RGBA", (W, W))
    sd = ImageDraw.Draw(shade)
    for y in range(0, W, 4):
        t = y / W
        c = (int(245 - 70*t), int(250 - 45*t), 255, 255)
        sd.rectangle([0, y, W, y + 4], fill=c)
    hl = Image.new("L", (W, W), 0)                      # highlight on the head, upper right
    ImageDraw.Draw(hl).ellipse([560*K, 170*K, 760*K, 330*K], fill=120)
    hl = hl.filter(ImageFilter.GaussianBlur(40 * K))
    shade.paste((255, 255, 255, 255), (0, 0), hl)
    img.paste(shade, (0, 0), mask)

    d = ImageDraw.Draw(img)
    ink = (28, 30, 70, 255)
    d.line(body + [body[0]], fill=ink, width=22 * K, joint="curve")

    def e(x0, y0, x1, y1, c):
        d.ellipse([x0*K, y0*K, x1*K, y1*K], fill=c)

    # eyes: dark hollows with glowing green pupils, looking to the right
    e(330, 330, 470, 490, ink); e(550, 330, 690, 490, ink)
    e(400, 390, 450, 445, (150, 255, 120, 255)); e(620, 390, 670, 445, (150, 255, 120, 255))
    e(418, 400, 434, 416, (255, 255, 255, 255)); e(638, 400, 654, 416, (255, 255, 255, 255))
    # cross brows
    d.line([(310*K, 290*K), (480*K, 345*K)], fill=ink, width=34 * K)
    d.line([(710*K, 290*K), (540*K, 345*K)], fill=ink, width=34 * K)
    # sly lopsided grin with one tooth
    grin = path([((380, 560), (450, 650), (590, 660), (680, 540))])
    low = path([((680, 540), (620, 610), (470, 620), (380, 560))])
    d.polygon(grin + low[1:], fill=ink)
    d.polygon([(560*K, 598*K), (600*K, 590*K), (585*K, 630*K)], fill=(255, 255, 255, 255))
    return img.resize((S, S), Image.LANCZOS)

big = draw_icon()
out = sys.argv[1]
big.resize((256, 256), Image.LANCZOS).save(out + ".png")
sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]
big.save(out + ".ico", sizes=sizes)
sheet = Image.new("RGBA", (256 + 128 + 64 + 48 + 32 + 16 + 70, 256), (60, 60, 70, 255))
x = 0
for s in [256, 128, 64, 48, 32, 16]:
    sheet.alpha_composite(big.resize((s, s), Image.LANCZOS), (x, 0)); x += s + 10
sheet.save(out + "_sizes.png")
