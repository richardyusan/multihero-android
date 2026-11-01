#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy the original game folder into a normalised asset tree:
lower-case paths, JPEG files re-encoded as BMP (same name), plus manifest.txt.

Usage: prep_assets.py <game dir> <output dir> [extra dir ...]
Files in the extra dirs (e.g. config/ with online.cfg) are added at the asset root."""
import os, sys, shutil, io
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
extras = sys.argv[3:]
SKIP = ('.exe', '.dll', '.zip', '.psd', '.bat', '.tgz')
if os.path.exists(dst):
    shutil.rmtree(dst)
names = []
for root, dirs, files in os.walk(src):
    for f in files:
        if f.lower().endswith(SKIP) or f.lower() == 'readme.md':   # readme.md = repo placeholder in game/
            continue
        sp = os.path.join(root, f)
        rel = os.path.relpath(sp, src).replace(os.sep, '/').lower()
        dp = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(dp), exist_ok=True)
        if f.lower().endswith(('.jpg', '.jpeg', '.png')):
            Image.open(sp).convert('RGB').save(dp, format='BMP')
        else:
            shutil.copyfile(sp, dp)
        names.append(rel)
for extra in extras:
    for root, dirs, files in os.walk(extra):
        for f in files:
            if f.lower().endswith('.example'):              # templates stay in the repo only
                continue
            sp = os.path.join(root, f)
            rel = os.path.relpath(sp, extra).replace(os.sep, '/').lower()
            dp = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(dp) or dst, exist_ok=True)
            shutil.copyfile(sp, dp)
            if rel not in names:
                names.append(rel)
names.sort()
with open(os.path.join(dst, 'manifest.txt'), 'w') as fh:
    fh.write('\n'.join(names) + '\n')
print(len(names), 'assets')
