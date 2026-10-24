"""ds-cards test helper: contact sheet of a test's screenshots. cards_sheet.py <dir> <out.jpg> [first last] [cols] [w]"""
import sys, glob, os
from PIL import Image, ImageDraw
d, out = sys.argv[1], sys.argv[2]
fs = sorted(glob.glob(os.path.join(d, 'shot*.jpg')))
if len(sys.argv) > 4:
    a, b = int(sys.argv[3]), int(sys.argv[4])
    fs = [f for f in fs if a <= int(os.path.basename(f)[4:8]) <= b]
cols = int(sys.argv[5]) if len(sys.argv) > 5 else 8
W = int(sys.argv[6]) if len(sys.argv) > 6 else 320
im0 = Image.open(fs[0]); H = int(W * im0.size[1] / im0.size[0])
rows = (len(fs) + cols - 1) // cols
sheet = Image.new('RGB', (W * cols, H * rows))
for i, f in enumerate(fs):
    im = Image.open(f).resize((W, H)); dr = ImageDraw.Draw(im); dr.text((4, 4), os.path.basename(f)[4:8], fill=(255, 255, 0))
    sheet.paste(im, ((i % cols) * W, (i // cols) * H))
sheet.save(out, quality=85)
print(out, len(fs))
