# ~/dumbphone/firmware/env.sh — firmware build environment.
# Source this before any idf.py work (the justfile does it per-recipe).
# Keeps ESP-IDF + its qemu isolated from the global login profile, so bare
# `python3` elsewhere stays the system interpreter.

export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.12_env"
. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1

# Espressif's qemu-xtensa (idf.py qemu) is an *optional* tool; export.sh does not put
# its bin/ on PATH, but idf.py requires qemu-system-xtensa to be findable.
for _q in "$HOME"/.espressif/tools/qemu-xtensa/*/qemu/bin; do
    [ -x "$_q/qemu-system-xtensa" ] || continue
    case ":${PATH}:" in
        *":$_q:"*) ;;
        *) PATH="$_q:$PATH" ;;
    esac
done
unset _q

# qemu-xtensa links against libslirp.so.0; this distro ships it only as a package we
# can't install without root, so a local copy is used instead.
if [ -d "$HOME/.local/lib/dumbphone-qemu" ]; then
    case ":${LD_LIBRARY_PATH:-}:" in
        *":$HOME/.local/lib/dumbphone-qemu:"*) ;;
        *) export LD_LIBRARY_PATH="$HOME/.local/lib/dumbphone-qemu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    esac
fi
