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

# Pixel regression check for the anonymous-inline-box paint bug (see
# Kind::Anonymous handling in paint.cpp): an anonymous box used to repaint
# its containing block's border once per whitespace gap between block
# siblings, instead of only at the real box's own edges. Skipped (not
# failed) when there's no X display to render against, or no PIL to decode
# the PNG with — neither is a hard dependency of the renderer itself.
f=test/regression-anon-box.html
name=$(basename "$f")
total=$((total + 1))
png=$(mktemp --suffix=.png)
render_err=$("$BIN" --render-to "$png" "$f" 2>&1 >/dev/null)
if printf '%s' "$render_err" | grep -q "cannot open X display"; then
    echo "SKIP   $name [render] (no X display)"
    total=$((total - 1))
elif ! border_rows=$(python3 -c "
from PIL import Image
im = Image.open('$png').convert('RGB')
w, h = im.size
print(sum(1 for y in range(h) if im.getpixel((w // 2, y)) != (255, 255, 255)))
" 2>/dev/null); then
    echo "SKIP   $name [render] (python3/PIL unavailable)"
    total=$((total - 1))
elif [ "$border_rows" != "2" ]; then
    echo "FAIL   $name [render] (expected 2 border rows at the outer box's" \
         "top/bottom edge, got $border_rows -- a border is being repainted" \
         "somewhere it shouldn't be)"
    fail=$((fail + 1))
fi
rm -f "$png"

if [ -n "${UPDATE:-}" ]; then
    echo "updated goldens for $total cases"
    exit 0
fi

passed=$((total - fail))
echo "$passed/$total passed"
[ "$fail" -eq 0 ]
