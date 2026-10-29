#!/bin/bash
# scripts/devkit-toolchain.sh
#
# Installs a private devkitA64 + libnx toolchain without devkitPro's pacman,
# without root, and without touching /opt/devkitpro.
#
# devkitPro publishes its packages as ordinary .pkg.tar.zst archives, so this
# script downloads the official package index, resolves the current versions
# and SHA-256 checksums, fetches the packages into a cache and unpacks them
# into a private prefix that can be used directly as DEVKITPRO.
#
# Usage:
#   bash scripts/devkit-toolchain.sh [OPTIONS]
#
# Options:
#   -p, --prefix DIR   Install prefix (default:
#                      ${XDG_DATA_HOME:-$HOME/.local/share}/devkitpro,
#                      override the default with DEVKITPRO_ROOT)
#       --force        Reinstall all packages, even when up to date
#       --print-env    Only print the DEVKITPRO export line and exit
#   -h, --help         Show this help
#
# Afterwards:
#   export DEVKITPRO="$HOME/.local/share/devkitpro"
#   bash scripts/build.sh
#
# Requirements: curl, bsdtar (libarchive), sha256sum, awk.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=devkit-env.sh
source "$SCRIPT_DIR/devkit-env.sh"

REPO_BASE="https://pkg.devkitpro.org/packages"
REPO_LINUX="$REPO_BASE/linux/x86_64"
# pkg.devkitpro.org is behind Cloudflare, which rejects generic user agents.
CURL_UA="pacman/6.0.2 libalpm/13.0.2"
CURL_RETRY=(--retry 3 --retry-delay 2)

# Packages the build needs directly. Dependencies are resolved from the
# package index (devkita64-gcc -> binutils/newlib/rules, switch-cmake -> ...).
ROOT_PACKAGES=(devkita64-gcc libnx switch-zlib switch-cmake)

# Directories inside the prefix that this script owns.
MANAGED_DIRS=(cmake devkitA64 libnx licenses portlibs tools)

PREFIX="$DEVKITPRO_LOCAL_PREFIX"
FORCE=0
PRINT_ENV=0

log() { printf '==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Install a private devkitA64 + libnx toolchain without devkitPro's pacman.

Usage: bash scripts/devkit-toolchain.sh [OPTIONS]

Options:
  -p, --prefix DIR   Install prefix (default: $XDG_DATA_HOME/devkitpro,
                     i.e. ~/.local/share/devkitpro; DEVKITPRO_ROOT overrides)
      --force        Reinstall all packages, even when up to date
      --print-env    Only print the DEVKITPRO export line and exit
  -h, --help         Show this help

The prefix is unpacked from the official devkitPro pacman packages; no root
privileges and no system package manager are used.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -p|--prefix)
            [ $# -ge 2 ] || die "--prefix needs a value"
            PREFIX="$2"
            shift 2
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            shift
            ;;
        --force)
            FORCE=1
            shift
            ;;
        --print-env)
            PRINT_ENV=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1 (try --help)"
            ;;
    esac
done

if [ "$PRINT_ENV" = "1" ]; then
    printf 'export DEVKITPRO="%s"\n' "$PREFIX"
    exit 0
fi

for tool in curl bsdtar sha256sum awk; do
    command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required but not found in PATH"
done

CACHE_DIR="$PREFIX/.cache"
DB_DIR="$CACHE_DIR/db"
PKG_CACHE="$CACHE_DIR/packages"
INDEX="$CACHE_DIR/index.tsv"
MANIFEST="$PREFIX/.dkp-manifest"

mkdir -p "$CACHE_DIR" "$DB_DIR/main" "$DB_DIR/linux" "$PKG_CACHE"

fetch_index_file() {
    local name="$1" url="$2"
    local dest="$DB_DIR/$name.db.tar.gz"
    local tmp="$dest.part"

    if curl -fsSL "${CURL_RETRY[@]}" -A "$CURL_UA" -o "$tmp" "$url"; then
        mv "$tmp" "$dest"
    else
        rm -f "$tmp"
        [ -f "$dest" ] || die "failed to download the package index from $url"
        warn "could not refresh the $name index, using the cached copy"
    fi
}

build_index() {
    local repo entry dir tmp="$INDEX.tmp"
    : > "$tmp"
    for repo in main linux; do
        dir="$DB_DIR/$repo"
        for entry in "$dir"/*/; do
            [ -f "${entry}desc" ] || continue
            awk -v repo="$repo" '
                /^%/ { f = $0; gsub(/%/, "", f); next }
                /^[[:space:]]*$/ { f = ""; next }
                {
                    if (f == "NAME") name = $0
                    else if (f == "VERSION") ver = $0
                    else if (f == "FILENAME") file = $0
                    else if (f == "SHA256SUM") sha = $0
                    else if (f == "DEPENDS") deps = (deps == "" ? $0 : deps "," $0)
                }
                END {
                    if (name != "")
                        printf "%s\t%s\t%s\t%s\t%s\t%s\n", name, repo, ver, file, sha, deps
                }
            ' "${entry}desc" >> "$tmp"
        done
    done
    mv "$tmp" "$INDEX"
}

declare -A PKG_REPO=() PKG_VER=() PKG_FILE=() PKG_SHA=() PKG_DEPS=()
RESOLVED=()

load_index() {
    local name repo ver file sha deps
    while IFS=$'\t' read -r name repo ver file sha deps; do
        [ -n "$name" ] || continue
        PKG_REPO["$name"]="$repo"
        PKG_VER["$name"]="$ver"
        PKG_FILE["$name"]="$file"
        PKG_SHA["$name"]="$sha"
        PKG_DEPS["$name"]="$deps"
    done < "$INDEX"
}

resolve_packages() {
    local queue=("${ROOT_PACKAGES[@]}") pkg dep dep_name deps
    local -A seen=()

    while [ "${#queue[@]}" -gt 0 ]; do
        pkg="${queue[0]}"
        queue=("${queue[@]:1}")
        [ -n "${seen[$pkg]:-}" ] && continue
        [ -n "${PKG_FILE[$pkg]:-}" ] || die "package '$pkg' is not in the devkitPro index"
        seen["$pkg"]=1
        RESOLVED+=("$pkg")

        deps="${PKG_DEPS[$pkg]:-}"
        [ -n "$deps" ] || continue
        local IFS=','
        for dep in $deps; do
            dep_name="${dep%%[<>=]*}"
            [ -n "$dep_name" ] || continue
            if [ -z "${PKG_FILE[$dep_name]:-}" ]; then
                warn "'$pkg' depends on '$dep_name', which is not in the devkitPro index (assuming it is a host package)"
                continue
            fi
            queue+=("$dep_name")
        done
    done
}

expected_manifest() {
    local pkg
    for pkg in "${RESOLVED[@]}"; do
        printf '%s\t%s\n' "$pkg" "${PKG_VER[$pkg]}"
    done | sort
}

is_installed() {
    local expected actual
    [ -f "$MANIFEST" ] || return 1
    [ -x "$PREFIX/devkitA64/bin/aarch64-none-elf-gcc" ] || return 1
    [ -f "$PREFIX/libnx/lib/libnx.a" ] || return 1
    # shellcheck disable=SC2218  # shellcheck 0.11 false positive: defined above
    expected="$(expected_manifest)"
    actual="$(sort "$MANIFEST")"
    [ "$expected" = "$actual" ]
}

wipe_managed_dirs() {
    local dir
    for dir in "${MANAGED_DIRS[@]}"; do
        rm -rf "${PREFIX:?}/$dir"
    done
}

verify_sha() {
    printf '%s  %s\n' "$2" "$1" | sha256sum -c --status - >/dev/null 2>&1
}

package_url() {
    local pkg="$1"
    if [ "${PKG_REPO[$pkg]}" = "linux" ]; then
        printf '%s/%s\n' "$REPO_LINUX" "${PKG_FILE[$pkg]}"
    else
        printf '%s/%s\n' "$REPO_BASE" "${PKG_FILE[$pkg]}"
    fi
}

fetch_package() {
    local pkg="$1" url dest tmp
    url="$(package_url "$pkg")"
    dest="$PKG_CACHE/${PKG_FILE[$pkg]}"

    if [ -f "$dest" ] && verify_sha "$dest" "${PKG_SHA[$pkg]}"; then
        printf '    cached   %s\n' "${PKG_FILE[$pkg]}"
        return 0
    fi

    tmp="$dest.part"
    local progress=(-sS)
    [ -t 2 ] && progress=(--progress-bar)
    printf '    download %s\n' "${PKG_FILE[$pkg]}"
    if ! curl -fL "${CURL_RETRY[@]}" "${progress[@]}" -A "$CURL_UA" -o "$tmp" "$url"; then
        rm -f "$tmp"
        die "failed to download $url"
    fi
    if ! verify_sha "$tmp" "${PKG_SHA[$pkg]}"; then
        rm -f "$tmp"
        die "checksum mismatch for ${PKG_FILE[$pkg]}"
    fi
    mv "$tmp" "$dest"
}

unpack_package() {
    local pkg="$1"
    local archive="$PKG_CACHE/${PKG_FILE[$pkg]}"
    # Packages store everything under opt/devkitpro/, hence --strip-components=2.
    bsdtar -xf "$archive" -C "$PREFIX" \
        --strip-components=2 \
        --no-same-owner \
        --exclude='.BUILDINFO' \
        --exclude='.MTREE' \
        --exclude='.PKGINFO'
}

sanity_check() {
    local rel missing=0
    for rel in \
        devkitA64/bin/aarch64-none-elf-gcc \
        libnx/lib/libnx.a \
        libnx/switch.specs \
        portlibs/switch/lib/libz.a \
        tools/bin/elf2nso \
        tools/bin/nacptool \
        tools/bin/npdmtool \
        tools/bin/build_pfs0; do
        if [ ! -e "$PREFIX/$rel" ]; then
            warn "missing after install: $PREFIX/$rel"
            missing=1
        fi
    done
    [ "$missing" -eq 0 ] || die "toolchain installation is incomplete"
    if ! "$PREFIX/devkitA64/bin/aarch64-none-elf-gcc" --version >/dev/null 2>&1; then
        die "the installed cross compiler does not run on this host"
    fi
}

log "refreshing the devkitPro package index"
fetch_index_file dkp-libs "$REPO_BASE/dkp-libs.db.tar.gz"
fetch_index_file dkp-linux "$REPO_LINUX/dkp-linux.db.tar.gz"

rm -rf "$DB_DIR/main" "$DB_DIR/linux"
mkdir -p "$DB_DIR/main" "$DB_DIR/linux"
bsdtar -xf "$DB_DIR/dkp-libs.db.tar.gz" -C "$DB_DIR/main"
bsdtar -xf "$DB_DIR/dkp-linux.db.tar.gz" -C "$DB_DIR/linux"

build_index
load_index
resolve_packages

log "resolved ${#RESOLVED[@]} packages: ${RESOLVED[*]}"

if [ "$FORCE" != "1" ] && is_installed; then
    log "toolchain is up to date: $PREFIX"
    printf '\n    export DEVKITPRO="%s"\n    bash scripts/build.sh\n\n' "$PREFIX"
    exit 0
fi

if [ -d "$PREFIX/devkitA64" ]; then
    log "replacing the existing toolchain in $PREFIX"
    wipe_managed_dirs
fi

log "downloading packages"
for pkg in "${RESOLVED[@]}"; do
    fetch_package "$pkg"
done

log "unpacking packages into $PREFIX"
for pkg in "${RESOLVED[@]}"; do
    unpack_package "$pkg"
done

sanity_check
expected_manifest > "$MANIFEST"

log "done"
printf '\n    export DEVKITPRO="%s"\n    bash scripts/build.sh\n\n' "$PREFIX"

if [ -n "${DEVKITPRO:-}" ] && [ "$DEVKITPRO" != "$PREFIX" ]; then
    warn "DEVKITPRO currently points at '$DEVKITPRO'; unset it or use the export line above"
fi
