# ADR 2026-11-10: Workspace zones are Lua config, not a compositor plugin

## Status

Accepted. The workspace-zones plugin, the `hyprland-workspace-zones` package
and the daily pin check are retired in 1.5.0.

## Context

Workspace zones give every numeric workspace a scratch area: workspace `N`
owns `special:N`. From 0.1.0 to 1.4.13 a compiled Hyprland plugin in this
repository implemented them.

A Hyprland plugin compiles against the compositor's internal headers and
loads only into the exact build it was compiled for. That had three costs.

1. Each Hyprland release broke the build. 0.55, 0.56 and 0.57 each needed a
   hand port. The 0.57 port changed about 40 lines because workspace ids
   became a variant and the workspace class moved.
2. The package had to pin `hyprland = <exact version>`. The package manager
   then refused a Hyprland upgrade until a matching plugin build existed.
3. The pin needed machinery: a daily pin check, a bot that cut a rebuild
   release for each drift, and a lockstep `-git` build in arch-repo.

On 2026-11-10 all three failed together. Hyprland 0.57 had fixed a crash in
`CInputManager::refocusLastWindow` that fires when a layer surface unmaps
while no real output is enabled. The plugin had no 0.57 port, so the release
bot failed on every run from 2026-11-05 and burned tags v1.4.8 to v1.4.13.
The pin held a laptop on 0.56.2, and the laptop hit the crash on an undocked
resume.

## Decision

1. Zones are Lua in the Hyprland config. hypr-DE 0.2.39 ships them, and the
   README and `workspace-zones(7)` carry the same code for other configs.
2. The plugin source, `hyprpm.toml`, the pin check workflow and its script
   are deleted.
3. The Fedora package obsoletes `hyprland-workspace-zones`. The Arch package
   replaces `hyprland-workspace-zones` and `hyprland-workspace-zones-git`.
   An upgrade removes the plugin package and its pin.
4. This repository builds nothing against the compositor.

## Why

Hyprland 0.57's Lua config API exposes everything the plugin used.

| Plugin behaviour | Lua replacement |
|---|---|
| Find the focused numeric workspace | `hl.get_active_workspace().id`, which is nil on a named workspace |
| Toggle `special:N` | `hl.dsp.workspace.toggle_special(tostring(N))` |
| Move the focused window to `special:N` | `hl.dsp.window.move({ workspace = "special:N", follow = ... })` |
| Close the zone when its workspace is left | `binds:hide_special_on_workspace_change = true`, built in |

The Lua API is the compositor's public config surface. Hyprland's own source
keeps the workspace `id` field populated "for compatibility under lua", so
upstream treats it as a contract. The internal headers carry no such promise.

A nested Hyprland 0.57.1 ran the Lua version through five cases, and each
matched the plugin: toggle opens and closes `special:N`, leaving the
workspace dismisses it, move without follow leaves the zone closed, move
with follow opens it, and a named workspace owns no zone.

## Tradeoffs

- `plugin:workspace-zones:auto_dismiss` no longer exists. The built-in option
  decides dismissal, and it applies to every special workspace. The plugin
  dismissed zones and left named specials such as `special:magic` open.
  hypr-DE already set the built-in option, so its behaviour does not change.
- Zones need Hyprland 0.57 or newer.
- The alternative was to keep the plugin, build it nightly against Hyprland
  git, and replace the exact pin with a soft failure. That keeps one hand
  port per Hyprland release, so it was rejected.

## Assumptions to preserve

- Do not reintroduce compiled code that includes Hyprland headers into this
  repository. If the Lua API ever lacks something zones need, request it
  upstream first.
- The Waybar module talks to Hyprland over IPC only. That is why it has no
  version pin, and it has to stay that way.
