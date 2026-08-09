#!/usr/bin/env bash
#
# Regenerates assets/ImeModePersistence.ico from assets/app_icon.png.
#
# Requires ImageMagick 7:  brew install imagemagick
#
# The source artwork carries three elements (A, 中, a cycle arrow) inside a
# double border, which turns to mush below about 32 px. Windows picks the icon
# image whose size matches the slot it is filling, so the small slots that the
# notification area actually uses get a purpose-drawn 中 badge instead:
#
#   16, 20, 24 px  hand-placed pixels, integer coordinates, no antialiasing
#   32 px and up   the source artwork, background removed and cropped
#
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$repo_root/assets/app_icon.png"
out="$repo_root/assets/ImeModePersistence.ico"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Sampled from the artwork rather than hardcoded, so re-exporting it at another
# size or on another background still works.
badge_colour="$(magick "$src" -format '%[pixel:p{w/2,h/2}]' info:)"
canvas_colour="$(magick "$src" -format '%[pixel:p{2,2}]' info:)"

echo "canvas $canvas_colour -> transparent, badge $badge_colour"

# Flood filling inward from a corner is what makes this safe: the glyphs are
# white and the canvas is near-white, so matching on colour alone would punch
# holes in the artwork. The glyphs are enclosed by the badge, so the fill
# cannot reach them.
magick "$src" \
    -alpha set -fuzz 12% -fill none -floodfill +0+0 "$canvas_colour" \
    -trim +repage \
    "$work/badge.png"

for size in 32 40 48 64 128 256; do
    magick "$work/badge.png" -background none -filter Lanczos \
        -resize "${size}x${size}" "$work/art-$size.png"
done

# 中 as axis-aligned rectangles: a box, and a vertical stroke through it that
# overhangs top and bottom. Coordinates are inclusive, so a 1 px line is
# drawn with equal start and end.
draw_small() {
    local size=$1 radius=$2 \
        box_l=$3 box_r=$4 box_t=$5 box_b=$6 wall=$7 \
        stem_l=$8 stem_r=$9 stem_t=${10} stem_b=${11}

    magick -size "${size}x${size}" xc:none \
        -fill "$badge_colour" \
        -draw "roundrectangle 0,0,$((size - 1)),$((size - 1)),$radius,$radius" \
        -fill white \
        -draw "rectangle $box_l,$box_t,$box_r,$((box_t + wall - 1))" \
        -draw "rectangle $box_l,$((box_b - wall + 1)),$box_r,$box_b" \
        -draw "rectangle $box_l,$box_t,$((box_l + wall - 1)),$box_b" \
        -draw "rectangle $((box_r - wall + 1)),$box_t,$box_r,$box_b" \
        -draw "rectangle $stem_l,$stem_t,$stem_r,$stem_b" \
        "$work/art-$size.png"
}

#          size radius  box: l  r  t   b  wall   stem: l  r  t   b
draw_small   16      3        3 12  5  10     1         7  8  2  13
draw_small   20      4        4 15  6  13     1         9 10  2  17
draw_small   24      5        5 18  7  16     2        11 12  3  20

# -type TrueColorAlpha keeps every entry 32-bit BGRA. Left to itself ImageMagick
# reduces the two-colour small sizes to a 4-bit palette, and palette ICO entries
# carry a 1-bit mask instead of an alpha channel, which turns the antialiased
# badge corners into jagged steps.
magick \
    "$work/art-16.png" "$work/art-20.png" "$work/art-24.png" \
    "$work/art-32.png" "$work/art-40.png" "$work/art-48.png" \
    "$work/art-64.png" "$work/art-128.png" "$work/art-256.png" \
    -type TrueColorAlpha -depth 8 \
    "$out"

echo "wrote $out"
magick identify "$out"
