#!/bin/sh
# Golden-file test harness for htmlview.
#
# Runs every test/*.html (and *.htm) fixture through --dump-dom,
# --dump-style and --dump-errors, and diffs the output against the
# checked-in goldens in test/golden/.
#
# Usage:
#   test/run.sh            run all tests
#   UPDATE=1 test/run.sh    regenerate goldens from current output

set -u
cd "$(dirname "$0")/.."

BIN=./htmlview
GOLDEN=test/golden
fail=0
total=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

[ -x "$BIN" ] || { echo "error: $BIN not built (run 'make' first)" >&2; exit 1; }

for f in test/*.html test/*.htm; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    for mode in dom style errors; do
        total=$((total + 1))
        golden="$GOLDEN/$name.$mode.txt"
        "$BIN" "--dump-$mode" "$f" >"$tmp" 2>&1

        if [ -n "${UPDATE:-}" ]; then
            cp "$tmp" "$golden"
            continue
        fi

        if [ ! -f "$golden" ]; then
            echo "NEW    $name [$mode] (no golden — run with UPDATE=1 to create)"
            fail=$((fail + 1))
            continue
        fi

        if ! diff -u "$golden" "$tmp"; then
            echo "FAIL   $name [$mode]"
            fail=$((fail + 1))
        fi
    done
done

# Pixel regression checks for layout/paint bugs --dump-dom/-style can't see
# (they don't run layout or paint at all). Skipped, not failed, when there's
# no X display to render against or no PIL to decode the PNG with — neither
# is a hard dependency of the renderer itself.
pixel_check() {  # fixture, python expr (sets `result`), expected, description
    f="test/$1" py="$2" expected="$3" desc="$4"
    name=$(basename "$f")
    total=$((total + 1))
    png=$(mktemp --suffix=.png)
    render_err=$("$BIN" --render-to "$png" "$f" 2>&1 >/dev/null)
    if printf '%s' "$render_err" | grep -q "cannot open X display"; then
        echo "SKIP   $name [render] (no X display)"
        total=$((total - 1))
    elif ! result=$(python3 -c "
from PIL import Image
im = Image.open('$png').convert('RGB')
w, h = im.size
$py
print(result)
" 2>/dev/null); then
        echo "SKIP   $name [render] (python3/PIL unavailable)"
        total=$((total - 1))
    elif [ "$result" != "$expected" ]; then
        echo "FAIL   $name [render] ($desc: expected $expected, got $result)"
        fail=$((fail + 1))
    fi
    rm -f "$png"
}

# Anonymous-inline-box paint bug (Kind::Anonymous in paint.cpp): an
# anonymous box used to repaint its containing block's border once per
# whitespace gap between block siblings, instead of only at the real box's
# own edges -- expect exactly the outer box's own top/bottom border rows.
pixel_check regression-anon-box.html \
    'result = sum(1 for y in range(h) if im.getpixel((w // 2, y)) != (255, 255, 255))' \
    2 "non-white pixel rows down the vertical center"

# Column-width bug (layout_table in layout.cpp): an explicit CSS width on a
# table cell used to be ignored when sizing columns, leaving a gap before
# the next column starts at its old even-split position.
pixel_check regression-col-width.html \
    "result = im.getpixel((150, 10))" \
    "(0, 255, 0)" "pixel well past the 48px first column"

# Column width vs. padding (layout_table in layout.cpp): the column-width
# pass treated a cell's explicit CSS width as the whole slot, but CSS width
# is content-box only -- a padded cell rendered wider than its slot and
# overflowed into the next column.
pixel_check regression-col-padding.html \
    "result = im.getpixel((60, 10))" \
    "(255, 0, 0)" "pixel inside the 48px+40px-padding first column"

if [ -n "${UPDATE:-}" ]; then
    echo "updated goldens for $total cases"
    exit 0
fi

passed=$((total - fail))
echo "$passed/$total passed"
[ "$fail" -eq 0 ]
