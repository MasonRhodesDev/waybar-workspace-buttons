#!/bin/bash
# Build the SRPM (source tarball from a git tag) and optionally submit it to
# COPR. Fedora ships the Waybar CFFI module AND the workspace-zones Hyprland
# plugin (hyprland-workspace-zones subpackage, version-pinned to the
# hyprland it was built against — resubmit a COPR build on every hyprland
# bump so the pin tracks the compositor).
#
# Release flow (Fedora + Arch from the same tag):
#   1. Bump root meson.build version + spec Version (+ %changelog) + PKGBUILD
#      pkgver — one commit.
#   2. git tag vX.Y.Z && git push --tags
#   3. ./packaging/build-srpm.sh [--copr]        # Fedora
#   4. cd packaging && updpkgsums && makepkg     # Arch (or let the shared
#      packaging-workflows CI release it)
#
# --head builds from HEAD instead of the tag (local testing only — never
# submit a --head build).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
NAME=waybar-workspace-buttons
SPEC="$REPO/packaging/$NAME.spec"
SOURCES="${HOME}/rpmbuild/SOURCES"
COPR_PROJECT="${COPR_PROJECT:-$NAME}"

VER=$(sed -n 's/^Version:[[:space:]]*//p' "$SPEC")
MESON_VER=$(sed -n "s/^[[:space:]]*version: '\([0-9.]*\)',*$/\1/p" "$REPO/meson.build" | head -1)
PKGBUILD_VER=$(sed -n 's/^pkgver=//p' "$REPO/packaging/PKGBUILD")
mismatch=""
[ "$MESON_VER" = "$VER" ] || mismatch="$mismatch\n  meson.build=$MESON_VER"
[ "$PKGBUILD_VER" = "$VER" ] || mismatch="$mismatch\n  PKGBUILD pkgver=$PKGBUILD_VER"
if [ -n "$mismatch" ]; then
    echo "ERROR: version mismatch (spec Version=$VER):$(printf "$mismatch")" >&2
    echo "Bump spec, root meson.build, and PKGBUILD pkgver together." >&2
    exit 1
fi

REF="v$VER"
if [ "${1:-}" = "--head" ]; then
    REF="HEAD"
    echo "WARNING: building from HEAD (testing only)"
    shift
elif ! git -C "$REPO" rev-parse -q --verify "refs/tags/$REF" >/dev/null; then
    echo "ERROR: tag $REF not found — tag the release first (or use --head to test)" >&2
    exit 1
fi

mkdir -p "$SOURCES"
echo "==> source tarball from $REF"
git -C "$REPO" archive --format=tar.gz --prefix="$NAME-$VER/" \
    -o "$SOURCES/$NAME-$VER.tar.gz" "$REF"

echo "==> building SRPM"
SRPM=$(rpmbuild -bs "$SPEC" | sed -n 's/^Wrote: //p')
echo "    $SRPM"
# Gating: a clean tree should pass (domain-term/spelling noise filtered by the
# rpmlintrc). Failures here are real spec defects worth stopping for.
rpmlint --rpmlintrc "$REPO/packaging/$NAME.rpmlintrc" "$SRPM"

if [ "${1:-}" = "--copr" ]; then
    echo "==> submitting to COPR project $COPR_PROJECT"
    if ! copr-cli build "$COPR_PROJECT" "$SRPM"; then
        echo "ERROR: copr build failed. If this was a 401, the API token has" >&2
        echo "expired (~180 days) — renew at https://copr.fedorainfracloud.org/api/" >&2
        exit 1
    fi
fi
