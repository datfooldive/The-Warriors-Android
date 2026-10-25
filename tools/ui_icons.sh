#!/usr/bin/env bash
# The on-screen controls' lettering: native/ui/letter_*.svg and label_*.svg drawn to the raw 8-bit coverage the build
# reads, one byte a pixel, the glyph white where it is set. The pad's letters are 128 x 128, the labels 256 x 64, each its
# .svg's own size, in Noto Sans ExtraBold set to shapes here so the phone needs no font. The icons are vectors instead
# (tools/ui_paths.py). The Android build
# carries no image decoder, so the art travels as bytes in ui_icons.cpp (CMakeLists.txt). Edit an .svg, run this,
# commit both. Needs rsvg-convert, ImageMagick and Noto Sans.
set -e
cd "$(dirname "$0")/.."
for svg in native/ui/letter_*.svg native/ui/label_*.svg; do
    name=$(basename "$svg" .svg)
    rsvg-convert "$svg" -o "/tmp/ui_$name.png"
    magick "/tmp/ui_$name.png" -alpha extract -depth 8 "gray:native/ui/$name.a8"
    echo "native/ui/$name.a8  $(stat -c%s "native/ui/$name.a8") bytes"
done
