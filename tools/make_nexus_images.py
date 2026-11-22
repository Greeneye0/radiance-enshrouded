"""Nexus listing images for Radiance - Player Light Source, in the Embervale Teleport style (dark brown boxes with a
gold outline, Georgia bold titles, dashed gold arrows). Raw shots: Radiance/assets/raw (taken 2026-11-19 at night in
a base, light settings changed through radiance.ini between shots). Output: Radiance/dist/nexus/images.

    python Radiance/tools/make_nexus_images.py
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "..", "assets", "raw")
OUT = os.path.join(HERE, "..", "dist", "nexus", "images")
GOLD = (232, 200, 130)
HOT = (255, 214, 140)
TXT = (230, 220, 200)
BOX = (22, 18, 14, 215)
# The part of a 2560x1600 shot with the character and the ground, without the HUD (bars, minimap, hotbar).
SCENE = (480, 330, 2000, 1400)


def font(bold, size, italic=False):
    name = "georgiaz" if bold and italic else "georgiab" if bold else "georgiai" if italic else "georgia"
    return ImageFont.truetype(f"C:/Windows/Fonts/{name}.ttf", size)


def load(name, box=SCENE):
    im = Image.open(os.path.join(RAW, name + ".png")).convert("RGB").crop(box)
    im = ImageEnhance.Contrast(im).enhance(1.06)
    return ImageEnhance.Color(im).enhance(1.06).convert("RGBA")


def callout(im, x, y, title, sub, target=None, size=40, width=None, anchor_side=None):
    """Box at (x, y) with a title and sub line; optional dashed line + arrow to target."""
    over = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    ft, fs = font(True, size), font(False, int(size * 0.72))
    w = width or int(max(d.textlength(title, font=ft), d.textlength(sub, font=fs) if sub else 0) + size * 1.2)
    h = int(size * (2.45 if sub else 1.55))
    d.rounded_rectangle((x, y, x + w, y + h), radius=int(size * 0.35), fill=BOX, outline=GOLD + (235,), width=3)
    d.text((x + size * 0.55, y + size * 0.28), title, font=ft, fill=HOT + (255,))
    if sub:
        d.text((x + size * 0.57, y + size * 1.42), sub, font=fs, fill=TXT + (240,))
    if target:
        side = anchor_side or ("right" if target[0] > x + w else "left" if target[0] < x else "bottom")
        sx, sy = {"right": (x + w, y + h / 2), "left": (x, y + h / 2), "bottom": (x + w / 2, y + h), "top": (x + w / 2, y)}[side]
        tx, ty = target
        dist = math.hypot(tx - sx, ty - sy)
        ux, uy = (tx - sx) / dist, (ty - sy) / dist
        dash, gap, t, end = 16, 10, 0.0, dist - 22
        while t < end:
            a, b = t, min(t + dash, end)
            d.line((sx + ux * a, sy + uy * a, sx + ux * b, sy + uy * b), fill=GOLD + (240,), width=5)
            t += dash + gap
        px, py = -uy, ux
        base = (tx - ux * 26, ty - uy * 26)
        d.polygon([(tx, ty), (base[0] + px * 12, base[1] + py * 12), (base[0] - px * 12, base[1] - py * 12)], fill=GOLD + (250,))
    im.alpha_composite(over)
    return w, h


def wordmark(im, x=28, y=24, sub="Player Light Source"):
    over = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    wm, ws = font(True, 54), font(False, 28)
    w = int(max(d.textlength("RADIANCE", font=wm), d.textlength(sub, font=ws)) + 56)
    d.rounded_rectangle((x, y, x + w, y + 122), radius=16, fill=BOX, outline=GOLD + (235,), width=3)
    d.text((x + 28, y + 12), "RADIANCE", font=wm, fill=HOT + (255,))
    d.text((x + 30, y + 78), sub, font=ws, fill=TXT + (240,))
    im.alpha_composite(over)


def tag(im, x, y, text, size=34):
    """A small label box."""
    return callout(im, x, y, text, None, size=size)


def frame(im):
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, im.width - 1, im.height - 1), outline=GOLD + (255,), width=4)


def save(im, name):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    im.convert("RGB").save(path, quality=92)
    print(path, im.size)


def before_after():
    off, on = load("01-off"), load("02-torch")
    w, h = off.size
    im = Image.new("RGBA", (w, h))
    im.paste(off.crop((0, 0, w // 2, h)), (0, 0))
    im.paste(on.crop((w // 2, 0, w, h)), (w // 2, 0))
    d = ImageDraw.Draw(im)
    d.line((w // 2, 0, w // 2, h), fill=GOLD + (255,), width=4)
    wordmark(im)
    callout(im, 60, h - 150, "Without Radiance", "a night in Embervale", size=38)
    callout(im, w // 2 + 60, h - 150, "With Radiance", "the light of a Wax Torch, hands free", size=38)
    frame(im)
    save(im, "01-before-after.jpg")


def colours():
    shots = [("02-torch", "Torch"), ("03-moonlight", "Moonlight"), ("04-emerald", "Emerald"), ("05-violet", "Violet"),
             ("06-crimson", "Crimson"), ("07-white-300m", "White, brightest, 300 m")]
    cell_w, cell_h, gap = 900, 560, 8
    im = Image.new("RGBA", (cell_w * 3 + gap * 2, cell_h * 2 + gap), GOLD + (255,))
    for i, (name, label) in enumerate(shots):
        # the middle of the scene: the character and the lit ground
        cell = load(name, (680, 420, 1880, 1167)).resize((cell_w, cell_h), Image.LANCZOS)
        tag(cell, 22, cell_h - 78, label, size=32)
        im.paste(cell, ((i % 3) * (cell_w + gap), (i // 3) * (cell_h + gap)))
    callout(im, 24, 24, "Pick your colour", "nine in the menu, any colour in the ini", size=40)
    frame(im)
    save(im, "02-colours.jpg")


def menu():
    im = load("08-menu", (60, 302, 1990, 1400))
    wordmark(im, x=im.width - 400, y=im.height - 160)
    # (1.1.0 shot, 2026-11-22; targets in this crop's pixels: the panel's right edge is at x 972, its rows at
    # y 146, 229, 311, 394)
    callout(im, 1060, 40, "Shift + F8 opens the menu", "F8 turns the light on or off; both can be rebound", size=38)
    callout(im, 1060, 200, "Brightness and Range", "up to three times a torch, up to 300 m", target=(945, 270), size=36)
    callout(im, 1060, 340, "Nine colors", "more with Color= in radiance.ini", target=(935, 394), size=36)
    callout(im, 1060, 480, "Use the mouse", "Esc or Close when done", size=36)
    callout(im, 80, 820, "In your game's language", "all 15 languages Enshrouded supports", size=36)
    frame(im)
    save(im, "03-menu.jpg")


if __name__ == "__main__":
    before_after()
    colours()
    menu()
