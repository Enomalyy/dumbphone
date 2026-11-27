#!/usr/bin/env bash
# golden.sh — the golden-frame gate engine (spec §16.1, issue #7).
#
# Usage:  ./golden.sh check    compare every scene with its committed frame
#         ./golden.sh update   rewrite the committed frames from scenes.txt
#                               (the ONLY way a golden frame changes)
#
# scenes.txt is the recipe list: one line per scene,
#     <frame> <kind> <flags...>
# kind=panel renders with --shot (device-pixel panel), kind=window with
# --shot-win (bezel + panel compose). Flags are passed verbatim to
# build-host/dumbphone-desktop. Shot mode freezes the clock at 20:36, so a
# scene is reproducible at any time.
#
# Comparison is done on raw pixels: each render is compared against the
# committed PNG converted to PPM (ImageMagick embeds a tIME chunk in PNGs it
# writes, so byte-comparing PNGs is not a meaningful check).
#
# `check` never touches ../screenshots/ — renders go to a temp dir. It also
# fails if a committed frame has no recipe here (except the documented
# exclusions below), so a frame can never silently drop out of the gate.
#
# Run with no live dumbphone-desktop instance active: two instances share
# firmware/data/ (the storage stub's directory) and port 8099 when the server
# is on, and a busy machine makes renders flaky.

set -u

FW_DIR=$(cd "$(dirname "$0")" && pwd)
SHOTS="$FW_DIR/../screenshots"
BIN="$FW_DIR/build-host/dumbphone-desktop"
SCENES="$FW_DIR/scenes.txt"

# Frames that are live captures, not frozen states — no recipe, not checked.
EXCLUDED="browser-viewport.png"

mode=${1:-check}
[ "$mode" = check ] || [ "$mode" = update ] || {
    echo "usage: $0 check|update" >&2; exit 2; }

[ -x "$BIN" ] || { echo "golden: $BIN missing — run 'just build-desktop' first" >&2; exit 2; }
command -v magick >/dev/null 2>&1 || { echo "golden: ImageMagick (magick) not found" >&2; exit 2; }
[ -f "$SCENES" ] || { echo "golden: $SCENES missing" >&2; exit 2; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

n=0; fail=0; missing=0
failed_scenes=""

while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in ''|\#*) continue ;; esac
    # read splits on any run of whitespace, so padded columns are fine;
    # flags_str takes the rest of the line verbatim.
    read -r frame kind flags_str <<< "$line"
    [ -n "${flags_str:-}" ] || flags_str=""

    n=$((n + 1))
    # Fresh user storage per scene: settings persist through hal_storage
    # (spec §16.4), so a state-changing scene must not leak into the next.
    rm -rf "$FW_DIR/data/user"

    shot_flag=--shot
    [ "$kind" = window ] && shot_flag=--shot-win

    # shellcheck disable=SC2086  # flags are verbatim CLI words from scenes.txt
    if ! "$BIN" $shot_flag "$tmp/frame.ppm" $flags_str --no-server >/dev/null 2>&1; then
        echo "FAIL $frame: render failed" >&2
        fail=$((fail + 1)); failed_scenes="$failed_scenes $frame(render)"
        continue
    fi

    ref="$SHOTS/$frame.png"
    if [ ! -f "$ref" ]; then
        echo "FAIL $frame: no committed frame at $ref" >&2
        fail=$((fail + 1)); failed_scenes="$failed_scenes $frame(missing)"
        continue
    fi

    magick "$ref" "$tmp/ref.ppm" >/dev/null 2>&1 || {
        echo "FAIL $frame: cannot convert committed PNG to PPM" >&2
        fail=$((fail + 1)); failed_scenes="$failed_scenes $frame(convert)"
        continue; }

    if [ "$mode" = update ]; then
        magick "$tmp/frame.ppm" "$ref" >/dev/null 2>&1 || {
            echo "FAIL $frame: cannot write $ref" >&2
            fail=$((fail + 1)); failed_scenes="$failed_scenes $frame(write)"
            continue; }
        echo "updated $frame.png"
        continue
    fi

    if ! cmp -s "$tmp/frame.ppm" "$tmp/ref.ppm"; then
        echo "FAIL $frame: differs from committed frame" >&2
        fail=$((fail + 1)); failed_scenes="$failed_scenes $frame"
    fi
done < "$SCENES"

# Coverage: every committed frame must have a recipe (or be excluded).
if [ "$mode" = check ]; then
    for png in "$SHOTS"/*.png; do
        name=${png##*/}
        covered=0
        while IFS= read -r line || [ -n "$line" ]; do
            case "$line" in ''|\#*) continue ;; esac
            [ "${line%% *}" = "${name%.png}" ] && { covered=1; break; }
        done < "$SCENES"
        if [ "$covered" = 0 ]; then
            is_excluded=0
            for ex in $EXCLUDED; do
                [ "$ex" = "$name" ] && is_excluded=1
            done
            if [ "$is_excluded" = 0 ]; then
                echo "FAIL $name: committed frame has no recipe in scenes.txt" >&2
                fail=$((fail + 1)); failed_scenes="$failed_scenes $name(uncovered)"
            fi
        fi
    done
fi

if [ "$fail" -gt 0 ]; then
    echo "golden ($mode): $fail of $n scene(s) failed:$failed_scenes" >&2
    exit 1
fi
echo "golden ($mode): all $n scenes OK"
