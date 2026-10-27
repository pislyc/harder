# Builds plugins/Harder/res/harder.ico (app icon): dark anodized body, white "H" in Michroma, orange LED.
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import sys, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024
im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
pad, rad = 40, 210
# body with a subtle vertical gradient
body = Image.new('RGBA', (S, S), (0, 0, 0, 0)); bd = ImageDraw.Draw(body)
for y in range(S):
    t = y / S; c = tuple(int(a + (b - a) * t) for a, b in zip((44, 50, 58), (24, 27, 32))); bd.line([(0, y), (S, y)], fill=c + (255,))
mask = Image.new('L', (S, S), 0); ImageDraw.Draw(mask).rounded_rectangle([pad, pad, S - pad, S - pad], rad, fill=255)
im.paste(body, (0, 0), mask)
d.rounded_rectangle([pad, pad, S - pad, S - pad], rad, outline=(70, 77, 86, 255), width=10)
# "H"
f = ImageFont.truetype(os.path.join(root, 'fonts', 'Michroma-Regular.ttf'), 560)
bb = d.textbbox((0, 0), 'H', font=f); w, h = bb[2] - bb[0], bb[3] - bb[1]
d.text(((S - w) / 2 - bb[0], (S - h) / 2 - bb[1] + 30), 'H', font=f, fill=(222, 228, 234, 255))
# orange LED with glow, top right
glow = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ImageDraw.Draw(glow).ellipse([700, 140, 860, 300], fill=(255, 106, 31, 170))
im = Image.alpha_composite(im, glow.filter(ImageFilter.GaussianBlur(28)))
ImageDraw.Draw(im).ellipse([735, 175, 825, 265], fill=(255, 106, 31, 255))
out = os.path.join(root, 'plugins', 'Harder', 'res', 'harder.ico')
im.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
im.resize((256, 256), Image.LANCZOS).save(os.path.join(root, 'plugins', 'Harder', 'res', 'harder-256.png'))
print(out)
