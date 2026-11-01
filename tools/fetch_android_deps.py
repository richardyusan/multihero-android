#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Download the Android libraries the EOS SDK's Java part needs (androidx.browser,
androidx.security:security-crypto and everything they depend on) without Gradle.

    python3 tools/fetch_android_deps.py third_party/android-deps

Reads the Maven POM files, follows compile/runtime dependencies, keeps the highest version
of each artifact and downloads the .aar/.jar files plus a deps.txt summary.
Sources: Google Maven (dl.google.com) and Maven Central.
"""
import os
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

ROOTS = [
    ("androidx.browser", "browser", "1.8.0"),
    ("androidx.security", "security-crypto", "1.0.0"),
    # EOSOverlayBrowser uses WindowInsetsCompat.getSystemWindowInsets() -> Insets (core >= 1.5);
    # 1.6.0 is the newest androidx.core without Kotlin.
    ("androidx.core", "core", "1.6.0"),
]
REPOS = ["https://dl.google.com/dl/android/maven2", "https://repo1.maven.org/maven2"]
# Kotlin stdlib is not referenced by the EOS classes we use, skip it (and its friends) to keep the APK small.
SKIP_GROUPS = {"org.jetbrains.kotlin", "org.jetbrains"}
NS = {"m": "http://maven.apache.org/POM/4.0.0"}


def vkey(v):
    out = []
    for p in v.replace("-", ".").split("."):
        out.append((0, int(p)) if p.isdigit() else (1, p))
    return out


def fetch(path):
    for repo in REPOS:
        for attempt in range(6):
            try:
                with urllib.request.urlopen(repo + "/" + path, timeout=30) as r:
                    return r.read()
            except urllib.error.HTTPError as e:
                if e.code == 429:            # rate limited: wait and retry
                    time.sleep(5 * (attempt + 1))
                    continue
                break                        # 404 etc: try the next repository
            except Exception:
                time.sleep(2)
    return None


_pom_cache = {}


def pom(g, a, v):
    key = (g, a, v)
    if key not in _pom_cache:
        data = fetch("%s/%s/%s/%s-%s.pom" % (g.replace(".", "/"), a, v, a, v))
        _pom_cache[key] = ET.fromstring(data) if data else None
    return _pom_cache[key]


def text(el, tag):
    x = el.find("m:" + tag, NS)
    return x.text.strip() if x is not None and x.text else None


def props(root, g, v):
    p = {"project.version": v, "project.groupId": g, "pom.version": v}
    pr = root.find("m:properties", NS)
    if pr is not None:
        for c in pr:
            p[c.tag.split("}")[1]] = (c.text or "").strip()
    return p


def subst(s, p):
    for _ in range(5):
        if not s or "${" not in s:
            break
        for k, val in p.items():
            s = s.replace("${%s}" % k, val)
    return s


def deps_of(g, a, v):
    root = pom(g, a, v)
    if root is None:
        return [], "jar"
    packaging = text(root, "packaging") or "jar"
    p = props(root, g, v)
    managed = {}
    dm = root.find("m:dependencyManagement/m:dependencies", NS)
    if dm is not None:
        for d in dm.findall("m:dependency", NS):
            managed[(text(d, "groupId"), text(d, "artifactId"))] = subst(text(d, "version"), p)
    out = []
    ds = root.find("m:dependencies", NS)
    if ds is not None:
        for d in ds.findall("m:dependency", NS):
            scope = text(d, "scope") or "compile"
            if scope not in ("compile", "runtime") or text(d, "optional") == "true":
                continue
            dg, da = subst(text(d, "groupId"), p), subst(text(d, "artifactId"), p)
            dv = subst(text(d, "version"), p) or managed.get((dg, da))
            if not dv:
                continue
            dv = dv.strip("[]()").split(",")[0]
            out.append((dg, da, dv))
    return out, packaging


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "third_party/android-deps"
    os.makedirs(dst, exist_ok=True)
    best = {}
    queue = list(ROOTS)
    while queue:
        g, a, v = queue.pop(0)
        if g in SKIP_GROUPS:
            continue
        cur = best.get((g, a))
        if cur and vkey(cur[0]) >= vkey(v):
            continue
        children, packaging = deps_of(g, a, v)
        best[(g, a)] = (v, packaging)
        queue.extend(children)
    lines = []
    for (g, a), (v, packaging) in sorted(best.items()):
        ext = "aar" if packaging == "aar" else "jar"
        name = "%s__%s-%s.%s" % (g, a, v, ext)
        path = os.path.join(dst, name)
        if not os.path.exists(path):
            data = fetch("%s/%s/%s/%s-%s.%s" % (g.replace(".", "/"), a, v, a, v, ext))
            if data is None and ext == "jar":
                data = fetch("%s/%s/%s/%s-%s.aar" % (g.replace(".", "/"), a, v, a, v))
                if data is not None:
                    path = path[:-3] + "aar"
                    name = name[:-3] + "aar"
            if data is None:
                print("  (no binary for %s:%s:%s, skipped)" % (g, a, v))
                continue
            with open(path, "wb") as fh:
                fh.write(data)
        lines.append("%s:%s:%s %s" % (g, a, v, name))
        print("  " + lines[-1])
    with open(os.path.join(dst, "deps.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("%d libraries in %s" % (len(lines), dst))


if __name__ == "__main__":
    main()
