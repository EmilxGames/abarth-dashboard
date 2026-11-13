#!/bin/sh
# Genera i font LVGL della UI v2 "Moderno" da Manrope (fonts/manrope, OFL).
# Usa solo il compilatore Swift di macOS (CoreText), nessun download.
set -e
cd "$(dirname "$0")/.."
OUT=main/ui_v2/fonts
BIN=/tmp/ttf2lvgl_$$
swiftc -O tools/ttf2lvgl.swift -o "$BIN"
F=fonts/manrope
ASCII=$(printf '%s' ' !"#$%&'"'"'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~')
EXTRA='°·−–≤àèéìòùÀÈÉÌÒÙ’→←Ω'
FULL="$ASCII$EXTRA"
"$BIN" $F/Manrope-Light.ttf   228 manrope_hero_228   '0123456789–' $OUT/manrope_hero_228.c 1 "" 1
"$BIN" $F/Manrope-Regular.ttf 75  manrope_value_75   '0123456789.−–+' $OUT/manrope_value_75.c 0 "" 1
"$BIN" $F/Manrope-Regular.ttf 48  manrope_value_48   '0123456789.−–+' $OUT/manrope_value_48.c 0 "" 1
"$BIN" $F/Manrope-Medium.ttf  32  manrope_medium_32  "$FULL" $OUT/manrope_medium_32.c 1 lv_font_montserrat_32
"$BIN" $F/Manrope-Medium.ttf  24  manrope_body_24    "$FULL" $OUT/manrope_body_24.c 0 lv_font_montserrat_24
"$BIN" $F/Manrope-Medium.ttf  22  manrope_unit_22    "$FULL" $OUT/manrope_unit_22.c 0 lv_font_montserrat_22
"$BIN" $F/Manrope-Medium.ttf  19  manrope_status_19  "$FULL" $OUT/manrope_status_19.c 0 lv_font_montserrat_18
"$BIN" $F/Manrope-Medium.ttf  17  manrope_label_17   "$FULL" $OUT/manrope_label_17.c 0 lv_font_montserrat_16
rm -f "$BIN"
