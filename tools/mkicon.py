# SPDX-License-Identifier: MIT
from PIL import Image, ImageDraw
import os, sys
assets = sys.argv[1]; out = sys.argv[2]
t = Image.open(os.path.join(assets, 'gfx/stuff/title.bmp')).convert('RGB')
w, h = t.size; px = t.load()
cols = [any(px[x, y] != (0, 0, 0) for y in range(h)) for x in range(w)]
end = 0
while end < w and cols[end]: end += 1
m = t.crop((0, 0, end, h))
a = Image.new('RGBA', m.size); mp = m.load(); ap = a.load()
for y in range(m.size[1]):
    for x in range(m.size[0]):
        r, g, b = mp[x, y]
        ap[x, y] = (r, g, b, 0 if (r, g, b) == (0, 0, 0) else 255)
a = a.crop(a.getbbox())
S = 192
base = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(base)
for i in range(S // 2, 0, -1):
    t2 = i / (S / 2)
    d.ellipse((S / 2 - i, S / 2 - i, S / 2 + i, S / 2 + i), fill=(int(190 - 140 * t2), int(50 - 35 * t2), int(25 - 10 * t2), 255))
sc = min(S * 0.70 / a.size[0], S * 0.70 / a.size[1])
sp = a.resize((int(a.size[0] * sc), int(a.size[1] * sc)), Image.LANCZOS)
base.alpha_composite(sp, ((S - sp.size[0]) // 2, (S - sp.size[1]) // 2))
for name, sz in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
    dd = os.path.join(out, 'mipmap-' + name)
    os.makedirs(dd, exist_ok=True)
    base.resize((sz, sz), Image.LANCZOS).save(os.path.join(dd, 'ic_launcher.png'))
base.save(os.path.join(out, '..', 'icon_preview.png'))
