# ADR 2026-09-29: Resolve the bar's output by xdg-output name, not logical position

## Status

Accepted. Supersedes decision 2 of ADR 2026-09-03 (match on logical x/y).
Decisions 1, 3, 5 and 6 of that record stand.

## Context

ADR 2026-09-03 binds each bar to the GdkMonitor that Waybar assigned it, then
resolves the Hyprland name by matching the monitor's logical (x, y) against
`hyprctl monitors -j`. That match is a snapshot taken when the bar maps.

On 2026-09-29 the snapshot was wrong. Mason moved the origin monitor in the
monitor profile, then replugged a Dell S2725QC (DP-4, later DP-5). The
journal shows the sequence for the DP-5 bar, all within the second 08:37:55:

| Source | Line |
|---|---|
| Hyprland | `monitoradded>>DP-5` |
| Waybar | `Bar configured (width: 2560, height: 40) for output: DP-5` |
| Module | `event=wsb.detect source=layer-shell gdk_x=0 gdk_y=0 gdk_model="DELL S2725QC" monitor=eDP-2` |

GDK reported DP-5 at (0, 0) because xdg-output had not yet delivered its
logical position. Hyprland reported eDP-2 at (0, 0) because hyprstate had
not yet applied the profile that moves it to (2560, 187). The (0, 0) match
therefore returned eDP-2, and the DP-5 bar showed the laptop's workspaces
(1, 2) instead of workspace 4 until the next bar restart. Two outputs
sharing a coordinate during a layout change is not specific to this layout:
every hotplug that also triggers a profile change passes through it.

The model string cannot break the tie. Both Dell panels report
`DELL S2725QC`. The Waybar CFFI ABI (v2) still passes no output name.

## Decision

1. The module reads the connector name from the compositor. It creates a
   private `zxdg_output_v1` for the GdkMonitor's `wl_output`
   (`gdk_wayland_monitor_get_wl_output`) on a private `wl_event_queue` and
   reads the `name` event. `wl_display_roundtrip_queue` dispatches only that
   queue and never dispatches GDK's own events.
2. The module confirms the name against `hyprctl monitors -j` before using
   it. On failure it falls back to the (x, y) match of ADR 2026-09-03, then
   to the focused monitor. Each fallback logs `reason=`.
3. The `event=wsb.detect` line gains `source=xdg-output` and an `xdg_name=`
   field. The geometry path logs as `source=layer-shell-geometry` (renamed
   from `layer-shell`).
4. New build dependencies: `wayland-client`, `wayland-protocols` (>= 1.14,
   which ships `xdg-output-unstable-v1.xml`) and `wayland-scanner`. meson
   generates the protocol code at build time.
5. The module binds `zxdg_output_manager_v1` at version 2 or 3. Version 2
   introduced the `name` event; Hyprland 0.56 advertises 3. A compositor
   advertising version 1 gets the geometry fallback.

## Why

### The name is the only stable identity

xdg-output sends `name` once per output and never changes it. Position,
size, scale and model all change or collide during a layout transition.
Resolving by name removes the ordering assumption between GDK, Hyprland and
hyprstate. The module needs no retry and no settle timer.

### A private queue rather than GDK's

Dispatching GDK's default queue from inside a module callback re-enters
GDK's event handlers at a point GDK did not choose. A private queue plus a
display wrapper (`wl_proxy_create_wrapper`) is the libwayland pattern for
this case, and GTK uses the same pattern for its own roundtrips.

### Rejected: re-detect on Hyprland `monitoradded` or `configreloaded`

Re-running the geometry match after each layout event narrows the window
but keeps the race, because hyprstate and GDK settle at unrelated times. On
2026-09-29 the gap between `monitoradded` and the profile apply was under
one second. A timer needs to guess that gap.

### Rejected: `gdk_monitor_get_model` as a tiebreaker

Identical panels report identical models. This layout has two.

## Consequences

- Build: three new `BuildRequires` (Fedora) and two new `makedepends`
  (Arch). GTK 3 already links wayland-client, so runtime dependencies do not
  change.
- Verified 2026-09-29 on the three-output layout (DP-5 at -2560,0; DP-1 at
  0,0; eDP-2 at 2560,187) with a throwaway Waybar instance loading the new
  build: all three bars logged `source=xdg-output` with their own connector
  name.
- Accepted weakness: the two roundtrips block the GTK thread until the
  compositor answers, the same as the existing `hyprctl` calls. A frozen
  compositor freezes the bar either way.
- Not verified yet: a DP unplug and replug with the new build installed,
  which is the exact trigger. Mason will run that after the 1.4.0 package
  lands.
