# SPDX-License-Identifier: MIT
from PIL import Image, ImageDraw, ImageFont
f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 13)
H = 16
rows = []
for c in range(32, 127):
    im = Image.new('L', (8, H), 0)
    d = ImageDraw.Draw(im)
    d.text((0, 1), chr(c), font=f, fill=255)
    bits = []
    for y in range(H):
        b = 0
        for x in range(8):
            if im.getpixel((x, y)) > 110: b |= 0x80 >> x
        bits.append('0x%02x' % b)
    rows.append('{' + ','.join(bits) + '}')
open('../runtime/font8x16.h', 'w').write('#pragma once\n#include <cstdint>\nstatic const int FONT_H = 16;\nstatic const uint8_t FONT8X16[95][16] = {\n' + ',\n'.join(rows) + '};\n')
