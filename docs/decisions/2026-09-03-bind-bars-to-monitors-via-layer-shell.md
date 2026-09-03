# ADR 2026-09-03: Bind bars to monitors via gtk-layer-shell, not layer-surface guessing

## Status

Accepted

## Context

Each Waybar bar loads its own instance of `workspace_buttons.so`, and the
module must learn which output that bar sits on before it can filter
workspaces. The Waybar CFFI ABI (v2) does not pass the output name, so since
1.0 the module guessed: list `hyprctl layers -j`, keep the `waybar` surfaces
whose width equals this bar's GTK allocation, take the first x position not yet
claimed by another instance (a file-scope table), and map that x to a monitor
by x-range only.

On 2026-09-03 a third output (HDMI-A-1, rotated, placed at logical `-1080,0`)
showed another monitor's workspaces on every startup and every hotplug. The
journal showed why: the guess used `-1` as its "nothing found" sentinel and
guarded the lookup with `selected_x >= 0`, so a real negative x was discarded,
`monitor_name` stayed empty, and the code fell back to the focused monitor
while logging `Detected monitor: DP-1` as if it had succeeded. Six occurrences,
always binding to whichever output had focus; the two bars at x ≥ 0 never
failed. The same function had two more latent faults: the claim table was
reset by every `wbcffi_init`, so a hotplugged bar wiped the claims of bars
already running, and matching by width and x alone is ambiguous for
equal-width or vertically stacked outputs.

The trace was also insufficient. Only the final name was logged, never the
inputs or whether the fallback had fired, so a wrong binding was
indistinguishable from a right one without reproducing the jq pipeline by hand.

## Decision

1. Ask Waybar instead of guessing. Waybar assigns every bar its output with
   `gtk_layer_set_monitor()`; the module calls `gtk_layer_get_monitor()` on the
   bar's toplevel and uses that `GdkMonitor`.
2. Resolve the Hyprland name by matching the GdkMonitor's logical geometry
   against `hyprctl monitors -j` on **both** `.x` and `.y`.
3. Keep the `output` config override as the first path, and keep a
   focused-monitor fallback as the last, but every fallback logs a `reason=`.
4. Delete the layer-surface width match, the x-claim table and its reset in
   `wbcffi_init`. Nothing replaces them.
5. Log exactly one `event=wsb.detect source=<config|layer-shell|fallback-focused> ...`
   line per resolution, with `gdk_x`, `gdk_y`, `gdk_model` and `monitor`,
   following the suite's `key=value` journald style.
6. Link `gtk-layer-shell-0` at build time (meson dependency, PKGBUILD depends
   and makedepends, spec `BuildRequires`).

## Why

### The one-line fix was rejected

Replacing the `-1` sentinel with a found flag would have fixed this layout and
nothing else. The claim table would still be wiped on hotplug, and two
same-width outputs would still race for the first x. The repo is alpha; the
rule is to remove a wrong mechanism rather than patch its symptoms.

### Layer-shell is the source of truth, not a heuristic

Waybar sets the monitor per bar before the bar maps, so `gtk_layer_get_monitor`
returns the exact output with no ordering, width, or claim assumptions. It
also works for the hotplug case, where only the new bar's module instance runs
detection, because it needs no shared state across instances.

### Match on logical x/y rather than model or connector

GTK 3 has no public connector-name getter on `GdkMonitor` (that is GTK 4).
`gdk_monitor_get_model()` is public but ambiguous for two identical panels.
Logical position is unique per output by construction, and GDK derives it from
the same xdg-output data Hyprland reports in `hyprctl monitors`, so the two
agree exactly (verified: `3440,-560`, `0,0`, `-1080,0` on a mixed rotated,
scaled layout). Matching on y as well as x is what makes stacked layouts
unambiguous.

### Link the library rather than `dlsym` it

Waybar hard-depends on gtk-layer-shell, so the symbol is always present in the
process, but a compile-time link is checked by the toolchain and costs nothing
extra at install time. `dlsym(RTLD_DEFAULT, ...)` would only have avoided a
build dependency that every builder already has.

### Trace the inputs and the path, not just the answer

The defect hid for months because the log line for a fallback looked identical
to a success. A `source=` field plus `reason=` on every fallback means the next
"why does this bar show the wrong workspaces" is answered by
`journalctl --user -u waybar.service | grep wsb.detect`, not by re-deriving the
pipeline.

## Consequences

### Behaviour

- A bar now binds to its own output regardless of position sign, rotation,
  scale, equal widths, or stacking.
- The focused-monitor fallback still exists but is loud. If it ever fires the
  journal names the reason: `no-layer-window`, `no-gdk-monitor` (after five
  400 ms retries), or `no-monitor-at` (GDK and Hyprland disagree on position).
- The `output` config override is unchanged and remains the escape hatch.

### Packaging

- gtk-layer-shell is a declared dependency on Arch and Fedora. It was already
  pulled in transitively by waybar.

### Future work must preserve

- If Waybar moves this module to GTK 4, use `gtk4_layer_get_monitor` and
  `gdk_monitor_get_connector()`, which removes the hyprctl lookup entirely.
- If a future CFFI ABI passes the output name in `wbcffi_init_info`, prefer it
  over the GdkMonitor path and delete the geometry match.
- Do not reintroduce cross-instance shared state for detection. Hotplug
  creates instances one at a time and any shared table will be wrong for at
  least one of them.

### Verification recipe

Run a throwaway bar on the bottom edge against a fresh build:
`waybar -c <scratch config with module_path=build/workspace_buttons.so> -s /dev/null`,
then check that each `wsb.detect` line's `monitor=` matches the preceding
`Bar configured ... for output:` line. On Hyprland 0.56 an output can be
removed and re-added for the hotplug case with
`echo <OUTPUT> > ~/.config/hypr/edp-off && hyprctl reload`, then
`rm ~/.config/hypr/edp-off && hyprctl reload`.
