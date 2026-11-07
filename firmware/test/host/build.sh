#!/usr/bin/env bash
# Builds the host renderer (needs `pio run` once so LVGL is downloaded) and writes PNGs to out/.
set -euo pipefail
cd "$(dirname "$0")"
LV=../../.pio/libdeps/station/lvgl
mkdir -p obj out
CF="-O1 -w -I. -I$LV -I../../src -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE"
objs=()
for c in $(find $LV/src -name '*.c') ../../src/fonts/*.c ../../src/icons/icons.c; do
  o=obj/$(echo "$c" | tr '/.' '__').o; objs+=("$o")
  [ -f "$o" ] || gcc $CF -c "$c" -o "$o" &
done
wait
g++ -std=c++17 $CF -include Arduino.h render.cpp stubs.cpp ../../src/ui_main.cpp ../../src/ui_setup.cpp "${objs[@]}" -o render
./render
python3 -c "
from PIL import Image; import glob
for p in sorted(glob.glob('out/*.ppm')):
    Image.open(p).save(p[:-4]+'.png')
"
rm -f out/*.ppm
