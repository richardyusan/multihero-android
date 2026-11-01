#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Unpack the Epic Online Services SDK zips into third_party/eos/ (only the parts the build uses)
and download the Android libraries the EOS Java code needs into third_party/android-deps/.

    python3 tools/setup_eos.py EOS-SDK-Android-<build>-v1.19.x.zip [EOS-SDK-<build>-v1.19.x.zip]

The EOS SDK is free but not redistributable, so it is never committed (third_party/ is in
.gitignore). Download it from dev.epicgames.com/portal -> SDK Download.

  Android SDK zip -> SDK/Include, SDK/Bin/Android/... (the .aar with libEOSSDK.so + Java classes)
  C SDK zip       -> SDK/Include, SDK/Bin/libEOSSDK-Linux-Shipping.so (optional, for Linux tests)
"""
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(HERE, "third_party", "eos")
WANT = ("SDK/Include/", "SDK/Bin/Android/", "SDK/Bin/libEOSSDK-Linux-Shipping.so", "ThirdPartyNotices/")


def main():
    zips = sys.argv[1:]
    if not zips:
        print(__doc__)
        sys.exit(1)
    for z in zips:
        n = 0
        with zipfile.ZipFile(z) as zf:
            for info in zf.infolist():
                name = info.filename.replace("\\", "/")
                # some zips have a top-level folder: find "SDK/" or "ThirdPartyNotices/" in the path
                for key in ("SDK/", "ThirdPartyNotices/"):
                    i = name.find(key)
                    if i >= 0:
                        name = name[i:]
                        break
                if info.is_dir() or not name.startswith(WANT) or ".." in name:
                    continue
                out = os.path.join(DST, name)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with zf.open(info) as src, open(out, "wb") as fh:
                    fh.write(src.read())
                n += 1
        print("%s: %d files -> %s" % (os.path.basename(z), n, DST))
    aar = [os.path.join(r, f) for r, _, fs in os.walk(os.path.join(DST, "SDK", "Bin", "Android")) for f in fs if f.endswith(".aar")]
    print("EOS headers:", "ok" if os.path.exists(os.path.join(DST, "SDK", "Include", "eos_sdk.h")) else "MISSING")
    print("EOS Android aar:", aar[0] if aar else "MISSING (use the EOS-SDK-Android zip)")
    subprocess.check_call([sys.executable, os.path.join(HERE, "tools", "fetch_android_deps.py"),
                           os.path.join(HERE, "third_party", "android-deps")])


if __name__ == "__main__":
    main()
