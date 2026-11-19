"""The listing's header and preview card, both from the banner art (assets/banner.png, 2000x667).

    python Radiance/tools/make_banner_images.py

header.jpg (1300x372): Nexus covers about the bottom 45% of a header with the page's title and stats, so the crop
keeps the head and the Radiance wordmark in the top half.
card.jpg (1920x1080): the mod card's preview, shown about 176 px wide: the banner's character and wordmark set
into a 16:9 frame over the banner's own background.
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'assets', 'banner.png')
OUT = os.path.join(HERE, '..', 'dist', 'nexus', 'images')
GOLD = (232, 200, 130)


def feather(size, left, top, right, bottom):
    """An alpha mask that fades to nothing over the given margins."""
    w, h = size
    mask = Image.new('L', size, 255)
    px = mask.load()
    for y in range(h):
        for x in range(w):
            a = 1.0
            if left and x < left:
                a *= x / left
            if right and x > w - right:
                a *= (w - x) / right
            if top and y < top:
                a *= y / top
            if bottom and y > h - bottom:
                a *= (h - y) / bottom
            px[x, y] = int(255 * max(0.0, min(1.0, a)))
    return mask


def header(banner):
    w, h = banner.size
    crop_h = round(w * 372 / 1300)
    top = 30
    im = banner.crop((0, top, w, top + crop_h)).resize((1300, 372), Image.LANCZOS)
    im.save(os.path.join(OUT, 'header.jpg'), quality=93)
    print('header.jpg', im.size)


def card(banner):
    W, H = 1920, 1080
    # the banner's own dark stone, enlarged and blurred, as the backdrop
    back = banner.crop((1150, 0, 2000, 667)).resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(28))
    back = ImageEnhance.Brightness(back).enhance(0.42)
    canvas = back.convert('RGBA')
    # the character, full height at the left
    figure = banner.crop((250, 0, 860, 667))
    scale = H / 667
    figure = figure.resize((round(figure.width * scale), H), Image.LANCZOS).convert('RGBA')
    figure.putalpha(feather(figure.size, 200, 0, 260, 0))
    canvas.alpha_composite(figure, (40, 0))
    # the wordmark with its star and subtitle, at the right
    mark = banner.crop((960, 0, 1860, 560))
    k = 1040 / mark.width
    mark = mark.resize((1040, round(mark.height * k)), Image.LANCZOS).convert('RGBA')
    mark.putalpha(feather(mark.size, 150, 60, 110, 90))
    canvas.alpha_composite(mark, (W - 1040 - 30, (H - mark.height) // 2 - 10))
    d = ImageDraw.Draw(canvas)
    d.rectangle((0, 0, W - 1, H - 1), outline=GOLD + (255,), width=6)
    canvas.convert('RGB').save(os.path.join(OUT, 'card.jpg'), quality=92)
    print('card.jpg', canvas.size)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    art = Image.open(SRC).convert('RGB')
    header(art)
    card(art)
