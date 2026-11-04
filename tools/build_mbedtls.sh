#!/bin/bash
# Compile third_party/mbedtls (see tools/fetch_mbedtls.py) into one static library.
#   tools/build_mbedtls.sh <out dir> "<C compiler command>" "<ar command>" [extra CFLAGS]
# Prints nothing and does nothing when the library is already built. Exit 2 = no mbedtls source.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"; CC="$2"; AR="$3"; EXTRA="$4"
SRC="$HERE/third_party/mbedtls"
[ -f "$SRC/library/ssl_tls.c" ] || exit 2
[ -f "$OUT/libmhtls.a" ] && exit 0
mkdir -p "$OUT/o"
PIDS=""; N=0
for f in "$SRC"/library/*.c; do
  $CC -O2 -w -ffile-prefix-map="$SRC/"=mbedtls/ -I"$SRC/include" -I"$SRC/library" $EXTRA -c "$f" -o "$OUT/o/$(basename "${f%.c}").o" & PIDS="$PIDS $!"
  N=$((N+1)); if [ $N -ge 16 ]; then for p in $PIDS; do wait $p || exit 1; done; PIDS=""; N=0; fi
done
for p in $PIDS; do wait $p || exit 1; done
rm -f "$OUT/libmhtls.a"
$AR rcs "$OUT/libmhtls.a" "$OUT"/o/*.o
rm -rf "$OUT/o"
