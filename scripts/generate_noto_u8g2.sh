#!/usr/bin/env bash
# Regenerate U8g2 bitmap fonts from Google Noto Sans (SIL OFL).
# Requires: curl, otf2bdf, gcc (to build bdfconv once from u8g2 sources).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/noto_u8g2_$$"
U8G2ZIP="${TMP}/u8g2.zip"
TTF="${TMP}/NotoSans-Regular.ttf"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

curl -sL -o "$TTF" \
  "https://github.com/googlefonts/noto-fonts/raw/main/hinted/ttf/NotoSans/NotoSans-Regular.ttf"
curl -sL -o "$U8G2ZIP" "https://github.com/olikraus/u8g2/archive/refs/heads/master.zip"
unzip -q -o "$U8G2ZIP" "u8g2-master/tools/font/bdfconv/*" "u8g2-master/tools/font/bdf/7x13.bdf" -d "$TMP"
BD="${TMP}/u8g2-master/tools/font/bdf"
BC="${TMP}/u8g2-master/tools/font/bdfconv"
make -C "$BC" clean CFLAGS='-O3 -Wall' bdfconv

MAP='32-255'
for PT NAME in \
  22 noto22 \
  28 noto28 \
  36 noto36; do
  otf2bdf -p "$PT" -r 72 -o "${TMP}/${NAME}.bdf" "$TTF"
  "$BC/bdfconv" -f 1 -m "$MAP" "${TMP}/${NAME}.bdf" -n "u8g2_font_${NAME}_tf" \
    -o "${ROOT}/src/${NAME}.c" -d "${BD}/7x13.bdf"
  sed -i.bak '1s/^/#include <u8g2_fonts.h>\n\n/' "${ROOT}/src/${NAME}.c" && rm -f "${ROOT}/src/${NAME}.c.bak"
done

echo "Wrote ${ROOT}/src/noto{22,28,36}.c"
