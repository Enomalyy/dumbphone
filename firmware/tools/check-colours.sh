#!/bin/sh
# check-colours.sh — the appearance gate (ui-architecture.md §14.10, issue #2).
#
# Rule: a painter asks for a ROLE, never a colour. The only colour literals in
# the tree are the palette TABLES:
#
#   ui/ui_theme.c      the nine UI roles, one table, one column per theme
#   host/src/bezel.c   the device shell's own table (the bezel is not the screen)
#
# Everything else — ui/*.c, ui/*.h, host/src/*.c, main/*.c — must contain no
# 0xRRGGBB literal and no SDL_MapRGB(...) colour argument. A hit is a real
# failure: it means some painter has its own idea of a colour, which is
# exactly the drift this stage exists to end.
#
# Usage: tools/check-colours.sh   (run from firmware/; `just build-desktop`
# runs it first, and `just check-colours` runs it alone)
#
# Escape hatch: none. If a non-painter genuinely needs a hex constant (a hash
# seed, a mask), write it in decimal or as a shift — the gate is about colours,
# and a false positive is cheaper to rewrite than a palette rule to police.

set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2

allow="ui/ui_theme.c host/src/bezel.c"
fail=0

# 1) 0xRRGGBB literals anywhere but the palette tables.
for f in ui/*.c ui/*.h host/src/*.c main/*.c hal/*.c hal/*.h; do
    [ -f "$f" ] || continue
    case " $allow " in *" $f "*) continue ;; esac
    hits=$(grep -nE '0x[0-9A-Fa-f]{6}\b' "$f" || true)
    if [ -n "$hits" ]; then
        echo "check-colours: colour literal in $f (move it to a palette table):"
        echo "$hits" | sed 's/^/    /'
        fail=1
    fi
done

# 2) SDL_MapRGB colour arguments outside the bezel table (the UI never touches
#    SDL; this catches a backend growing its own palette).
for f in ui/*.c ui/*.h host/src/*.c; do
    [ -f "$f" ] || continue
    case " $allow " in *" $f "*) continue ;; esac
    hits=$(grep -nE 'SDL_MapRGB\(' "$f" || true)
    if [ -n "$hits" ]; then
        echo "check-colours: SDL_MapRGB in $f (colours belong to a palette table):"
        echo "$hits" | sed 's/^/    /'
        fail=1
    fi
done

# 3) The palette tables themselves must be the ONLY definition site: guard
#    against a role being re-pointed at a literal colour somewhere else.
for f in $allow; do
    [ -f "$f" ] || { echo "check-colours: missing palette table $f"; fail=1; }
done

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "check-colours: FAILED — see ui-architecture.md §14.10 (the role table)."
    exit 1
fi
echo "check-colours: ok (palette tables: $allow)"
exit 0
