#!/usr/bin/env bash
# Idempotent installer for the waybar-workspace-buttons CFFI module.
# - Builds workspace_buttons.so (meson/ninja) if needed
# - Installs it to ~/.config/waybar/cffi/
#
# The companion workspace-zones Hyprland plugin is NOT installed here —
# it ships via hyprpm (see README, "Per-workspace special zones").
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)"

for c in meson ninja jq; do
    command -v "$c" >/dev/null || { echo "missing dependency: $c" >&2; exit 1; }
done

[ -f "$SRC/build/build.ninja" ] || meson setup "$SRC/build" "$SRC"
ninja -C "$SRC/build"

install -Dm644 "$SRC/build/workspace_buttons.so" "$HOME/.config/waybar/cffi/workspace_buttons.so"
echo "  -> ~/.config/waybar/cffi/workspace_buttons.so"

echo
echo "Add the module to your Waybar config (see README \"Configuration\"), then restart Waybar."
