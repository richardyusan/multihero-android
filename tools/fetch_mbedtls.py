#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Download Mbed TLS (Apache-2.0) into third_party/mbedtls/ - the TLS library the native builds
(Android, Windows, Linux) use for the online servers (wss://). The web build does not need it.

    python3 tools/fetch_mbedtls.py
"""
import hashlib
import io
import os
import shutil
import sys
import tarfile
import urllib.request

VERSION = "3.6.5"
URL = "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-%s/mbedtls-%s.tar.bz2" % (VERSION, VERSION)
SHA256 = "4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(HERE, "third_party", "mbedtls")


def main():
    if os.path.exists(os.path.join(DST, "VERSION")) and open(os.path.join(DST, "VERSION")).read().strip() == VERSION:
        print("mbedtls %s already in %s" % (VERSION, DST))
        return
    src = sys.argv[1] if len(sys.argv) > 1 else None
    data = open(src, "rb").read() if src else urllib.request.urlopen(URL).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        sys.exit("checksum mismatch - not the expected mbedtls %s download" % VERSION)
    shutil.rmtree(DST, ignore_errors=True)
    os.makedirs(DST)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:bz2") as t:
        for m in t.getmembers():
            parts = m.name.split("/", 1)
            if len(parts) < 2 or not (parts[1].startswith(("include/", "library/")) or parts[1] == "LICENSE"):
                continue
            if not m.isfile() or ".." in parts[1]:
                continue
            out = os.path.join(DST, parts[1])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as fh:
                fh.write(t.extractfile(m).read())
    open(os.path.join(DST, "VERSION"), "w").write(VERSION + "\n")
    print("mbedtls %s -> %s" % (VERSION, DST))


if __name__ == "__main__":
    main()
