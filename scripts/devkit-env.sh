#!/bin/bash
# Shared helpers for building NX-ScreenUploader without a system-wide
# devkitPro installation.
#
# Sourcing this file defines DEVKITPRO_LOCAL_PREFIX (the private toolchain
# location used by scripts/devkit-toolchain.sh) and provides:
#
#   resolve_devkitpro             exports DEVKITPRO
#   prepare_build_dir <dir>       validates/recreates a build directory
#
# Sourcing it does not change DEVKITPRO by itself.

DEVKITPRO_LOCAL_PREFIX="${DEVKITPRO_ROOT:-${XDG_DATA_HOME:-$HOME/.local/share}/devkitpro}"

_devkitpro_not_found() {
    cat >&2 <<EOF
error: no devkitA64 toolchain found.

Install a private, pacman-free toolchain with:

    bash scripts/devkit-toolchain.sh

or point DEVKITPRO at an existing devkitPro installation:

    DEVKITPRO=/path/to/devkitpro bash scripts/build.sh
EOF
}

# Export DEVKITPRO. Lookup order:
#   1. the DEVKITPRO environment variable
#   2. the private toolchain created by scripts/devkit-toolchain.sh
#   3. a system-wide installation in /opt/devkitpro (e.g. devkitPro pacman)
resolve_devkitpro() {
    if [ -n "${DEVKITPRO:-}" ]; then
        if [ -x "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc" ]; then
            export DEVKITPRO
            return 0
        fi
        echo "error: DEVKITPRO=${DEVKITPRO} does not look like a devkitPro installation" >&2
        return 1
    fi

    if [ -x "${DEVKITPRO_LOCAL_PREFIX}/devkitA64/bin/aarch64-none-elf-gcc" ]; then
        DEVKITPRO="$DEVKITPRO_LOCAL_PREFIX"
    elif [ -x "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc" ]; then
        DEVKITPRO="/opt/devkitpro"
        echo "warning: using the system-wide devkitPro at $DEVKITPRO" >&2
        echo "         (run 'bash scripts/devkit-toolchain.sh' for a private toolchain)" >&2
    else
        _devkitpro_not_found
        return 1
    fi

    # Put the matching host tools (elf2nso, nacptool, npdmtool, ...) first in
    # PATH so find_program() does not pick them up from another devkitPro
    # installation (for example /opt/devkitpro left in PATH by pacman).
    case ":$PATH:" in
        *":$DEVKITPRO/tools/bin:"*) ;;
        *) PATH="$DEVKITPRO/tools/bin:$PATH" ;;
    esac
    export DEVKITPRO PATH
    return 0
}

# Prepare BUILD_DIR for the current DEVKITPRO.
#
# CMake caches absolute paths (LIBNX_INCLUDE_DIR, ZLIB_LIBRARY_RELEASE,
# CMAKE_C_COMPILER), so a build directory is only valid for the toolchain it
# was configured with. A stale cache is rejected, or removed when CLEAN=1.
prepare_build_dir() {
    local build_dir="$1"
    local cache="$build_dir/CMakeCache.txt"
    local reason=""

    if [ -f "$cache" ]; then
        local cached var

        cached="$(sed -n 's/^LIBNX_INCLUDE_DIR:PATH=//p' "$cache" | head -n 1)"
        if [ -n "$cached" ] && [ "$cached" != "$DEVKITPRO/libnx/include" ]; then
            reason="LIBNX_INCLUDE_DIR = $cached"
        fi

        cached="$(sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p' "$cache" | head -n 1)"
        if [ -z "$reason" ] && [ -n "$cached" ] &&
           [ "$cached" != "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc" ]; then
            reason="CMAKE_C_COMPILER = $cached"
        fi

        # Host tools found by find_program() are cached too.
        if [ -z "$reason" ]; then
            for var in ELF2NSO NPDMTOOL NACPTOOL BUILD_PFS0; do
                cached="$(sed -n "s/^$var:[A-Z]*=//p" "$cache" | head -n 1)"
                if [ -n "$cached" ] && [ "$(dirname "$cached")" != "$DEVKITPRO/tools/bin" ]; then
                    reason="$var = $cached"
                    break
                fi
            done
        fi

        if [ -n "$reason" ]; then
            if [ "${CLEAN:-0}" = "1" ]; then
                echo "==> '$build_dir' was configured for a different toolchain, rebuilding from scratch"
                rm -rf "${build_dir:?}"
            else
                cat >&2 <<EOF
error: '$build_dir' was configured against a different toolchain.

    cached          : $reason
    current DEVKITPRO : $DEVKITPRO

Recreate the build directory and rebuild with:

    CLEAN=1 bash scripts/build.sh
EOF
                return 1
            fi
        fi
    fi

    mkdir -p "$build_dir"
}
