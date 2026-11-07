#!/usr/bin/env bash
# Regenerates the LVGL font files in firmware/src/fonts from Atkinson Hyperlegible.
# Needs: npm i lv_font_conv@1.5.3   (run from this tools/ folder)
# Fonts: put AtkinsonHyperlegible-Regular.ttf and -Bold.ttf in tools/fonts/ from
#   https://github.com/google/fonts/tree/main/ofl/atkinsonhyperlegible (SIL OFL 1.1)
set -euo pipefail
cd "$(dirname "$0")"
REG=${REG:-fonts/AtkinsonHyperlegible-Regular.ttf}
BOLD=${BOLD:-fonts/AtkinsonHyperlegible-Bold.ttf}
OUT=../firmware/src/fonts
CONV=${CONV:-npx lv_font_conv}
mkdir -p "$OUT"

ASCII="0x20-0x7E"
EXTRA="0xB0,0xB7"   # degree sign, middle dot

gen() { # name file size range
  $CONV --no-compress --format lvgl --bpp 4 --size "$3" --font "$2" -r "$4" \
        --lv-include lvgl.h -o "$OUT/$1.c"
  echo "  $1"
}

echo "Generating fonts into $OUT"
gen atk_b168 "$BOLD" 168 "0x30-0x39,0x2D,0xB0"
gen atk_b64  "$BOLD" 64  "0x30-0x39,0x2D,0x2E,0x20"
gen atk_b48  "$BOLD" 48  "0x30-0x39"
gen atk_b40  "$BOLD" 40  "$ASCII,$EXTRA"
gen atk_b34  "$BOLD" 34  "$ASCII,$EXTRA"
gen atk_b30  "$BOLD" 30  "$ASCII,$EXTRA"
gen atk_b28  "$BOLD" 28  "$ASCII,$EXTRA"
gen atk_b22  "$BOLD" 22  "$ASCII,$EXTRA"
gen atk_b20  "$BOLD" 20  "$ASCII,$EXTRA"
gen atk_r34  "$REG"  34  "$ASCII,$EXTRA"
gen atk_r30  "$REG"  30  "$ASCII,$EXTRA"
gen atk_r28  "$REG"  28  "$ASCII,$EXTRA"
gen atk_r24  "$REG"  24  "$ASCII,$EXTRA"
gen atk_r20  "$REG"  20  "$ASCII,$EXTRA"
