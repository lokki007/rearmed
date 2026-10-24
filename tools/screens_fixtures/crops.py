"""en-screens: crops of 7680x2160 screenshots for review.
python crops.py <dir> [shot names...] -> <dir>/view/<shot>_full.png (1920 wide), _centre.png (the middle
2400x1350 at half size), _right.png / _left.png (the HUD-edge bands of the 16:9 area beside the centre)."""
import pathlib
import sys

from PIL import Image

d = pathlib.Path(sys.argv[1])
names = sys.argv[2:] or sorted(p.name for p in d.glob('*.jpg'))
out = d / 'view'
out.mkdir(exist_ok=True)
for n in names:
    im = Image.open(d / n)
    w, h = im.size
    stem = pathlib.Path(n).stem
    im.resize((1920, int(1920 * h / w))).save(out / f'{stem}_full.png')
    cw, ch = int(w * 0.3125), int(h * 0.625)
    c = im.crop((w // 2 - cw // 2, h // 2 - ch // 2, w // 2 + cw // 2, h // 2 + ch // 2))
    c.resize((cw // 2, ch // 2)).save(out / f'{stem}_centre.png')
    # 16:9 area = the middle h*16/9 pixels; its right / left quarter + some margin
    a = int(h * 16 / 9)
    x0 = w // 2 + a // 4 - a // 16
    im.crop((x0, 0, min(w, w // 2 + a // 2 + a // 16), h)).resize((((min(w, w // 2 + a // 2 + a // 16)) - x0) // 2, h // 2)).save(out / f'{stem}_right.png')
    x1 = w // 2 - a // 4 + a // 16
    lx = max(0, w // 2 - a // 2 - a // 16)
    im.crop((lx, 0, x1, h)).resize(((x1 - lx) // 2, h // 2)).save(out / f'{stem}_left.png')
    print(stem, w, h)
