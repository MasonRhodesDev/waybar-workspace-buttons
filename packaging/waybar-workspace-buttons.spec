# RPM spec for waybar-workspace-buttons. Built in COPR from a local SRPM
# produced by packaging/build-srpm.sh (source tarball from the git tag).
#
# Ships the Waybar CFFI module and the workspace-zones(7) man page. The
# workspace-zones compositor plugin was retired in 1.5.0 because zones are
# Lua in the Hyprland config now (docs/decisions has the record). Nothing
# here is built against the compositor and nothing pins its version.

Name:           waybar-workspace-buttons
Version:        1.5.0
Release:        1%{?dist}
Summary:        Waybar CFFI workspace-buttons module for Hyprland
License:        MIT
URL:            https://github.com/MasonRhodesDev/waybar-workspace-buttons
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(gtk-layer-shell-0)
# xdg-output name resolution (protocol code generated at build time)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols) >= 1.14
BuildRequires:  pkgconfig(wayland-scanner)
Requires:       waybar
Requires:       jq
# The retired plugin package pinned `hyprland = <exact version>`. Obsolete it
# so an upgrade removes it and the pin stops holding hyprland back.
Obsoletes:      hyprland-workspace-zones < 1.5.0

%description
A Waybar CFFI module rendering per-monitor workspace buttons for Hyprland
(per-monitor filtering, special-workspace indicators). It talks to the
compositor over the Hyprland IPC socket at runtime, so the compositor itself
is not an RPM dependency (and is commonly not RPM-managed on Hyprland
setups).

Point Waybar at the module by setting the cffi/workspace-buttons
module_path to %{_libdir}/waybar/workspace_buttons.so.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%files
%license LICENSE
%doc README.md
%dir %{_libdir}/waybar
%{_libdir}/waybar/workspace_buttons.so
%{_mandir}/man7/workspace-zones.7*

%changelog
* Tue Nov 10 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.5.0-1
- Retire the workspace-zones compositor plugin and the
  hyprland-workspace-zones package. Zones are Lua in the Hyprland config
  (hypr-DE 0.2.39 ships them), built on Hyprland 0.57's Lua API. The plugin
  compiled against Hyprland's internal headers, broke on 0.55, 0.56 and
  0.57, and pinned an exact hyprland version, which held machines on
  0.56.2 after 0.57 fixed a compositor crash.
- This package obsoletes hyprland-workspace-zones, so an upgrade removes it
  and its hyprland pin.
- The daily pin check and its rebuild releases are gone with the plugin.
- workspace-zones(7) describes the Lua zones.

* Tue Nov 10 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.13-1
- Rebuild against hyprland 0.57.1 0.57.2 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Mon Nov 09 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.12-1
- Rebuild against hyprland 0.57.1 0.57.2 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Sun Nov 08 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.11-1
- Rebuild against hyprland 0.57.1 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Sat Nov 07 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.10-1
- Rebuild against hyprland 0.57.1 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Fri Nov 06 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.9-1
- Rebuild against hyprland 0.57.1 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Thu Nov 05 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.8-1
- Rebuild against hyprland 0.57.1 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Wed Nov 04 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.7-1
- Rebuild against hyprland 0.57.1 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Tue Nov 03 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.6-1
- Rebuild against hyprland 0.57.0 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Mon Nov 02 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.5-1
- Rebuild against hyprland 0.57.0 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64 arch-stable-x86_64).

* Sun Nov 01 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.4-1
- Rebuild against hyprland 0.56.2 0.57.0 (targets: fedora-rawhide-x86_64 arch-stable-x86_64).

* Sat Oct 31 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.3-1
- Rebuild against hyprland 0.56.2 0.57.0 (targets: arch-stable-x86_64).

* Fri Oct 30 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.2-1
- Rebuild against hyprland 0.56.2 0.57.0 (targets: arch-stable-x86_64).

* Thu Oct 29 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.1-1
- Rebuild against hyprland 0.56.2 0.57.0 (targets: arch-stable-x86_64).

* Tue Sep 29 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.4.0-1
- Resolve each bar's output by its xdg-output connector name instead of
  matching logical x/y. The x/y snapshot taken on map raced hotplug and
  monitor-profile changes: a replugged DP-5 and eDP-2 both sat at (0,0) for
  a moment, so the DP-5 bar bound to eDP-2 and showed its workspaces.
  The x/y match stays as the first fallback, the focused monitor as the last.
- `event=wsb.detect` logs `source=xdg-output` and `xdg_name=`; the geometry
  path is now `source=layer-shell-geometry`.
- New build dependencies: wayland-client, wayland-protocols, wayland-scanner.

* Wed Sep 09 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.3.2-1
- Rebuild against hyprland 0.56.2 (targets: fedora-43-aarch64 fedora-43-x86_64 fedora-44-aarch64 fedora-44-x86_64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64).

* Thu Sep 03 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.3.1-1
- Bind each bar to the monitor waybar assigned it (gtk_layer_get_monitor,
  matched on logical x/y) instead of guessing from layer-surface x positions.
  Bars on outputs at negative x silently fell back to the focused monitor and
  showed its workspaces; hotplugged bars also wiped the other bars' claims.
- Log one `event=wsb.detect source=...` line per resolution so the journal
  shows which output a bar bound to and why.
- New build dependency: gtk-layer-shell.

* Mon Aug 24 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.3.0-1
- Arch: split hyprland-workspace-zones into its own package with an exact
  hyprland pin (mirroring Fedora); waybar-workspace-buttons depends on the
  virtual name so the -git variant from [mason] can satisfy it.
- Pin-check now also watches extra/hyprland vs the [mason] repo pin.

* Mon Aug 24 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.2.1-1
- Rebuild against hyprland 0.56.2 (targets: fedora-43-aarch64 fedora-44-aarch64 fedora-45-aarch64 fedora-45-x86_64 fedora-rawhide-aarch64 fedora-rawhide-x86_64).

* Mon Aug 24 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.2.0-1
- Ship the workspace-zones Hyprland plugin on Fedora as the
  hyprland-workspace-zones subpackage, pinned to the exact hyprland version
  it was built against. The compositor is RPM-managed now, so the plugin is
  stack-managed too instead of a manual hyprpm/local rebuild.
* Fri Aug 22 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.1.2-1
- Workspace click always dispatches the Lua form. The dialect detection
  keyed on ~/.config/hypr/hyprland.lua existing emitted the removed
  classic string dispatch whenever the stub was absent (hypr-DE 0.2.11+
  packages the entrypoint under /etc/xdg), so bar clicks silently failed.

* Fri Aug 22 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.1.1-1
- Call /usr/bin/lmtt by absolute path for the tertiary token: a stale
  hand-built /usr/local/bin/lmtt shadowed the packaged binary and the
  workspace dot silently fell back to the default color.

* Thu Aug 20 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.1.0-1
- workspace-zones: port to Hyprland 0.56 (typed Config::Actions replace the
  removed string-dispatcher registry; config value moves to the V2 API)
- Lua config is now the only supported configuration; the zones:* string
  dispatchers are gone (removed compositor-wide by Hyprland 0.56)
- Require hyprland >= 0.56

* Sun Aug 16 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.3-1
- Snapshot Arch sources on tag builds so the PKGBUILD checksum can match.

* Sun Aug 16 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.2-1
- Ship workspace-zones(7). Arch also ships the Hyprland plugin .so.

* Wed Jul 15 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.1-1
- Workspace click: detect the active Hyprland config dialect and emit the
  matching hyprctl dispatch syntax (Lua expression vs classic string)

* Wed Jul 15 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.0-1
- Initial Fedora packaging: Waybar CFFI module via COPR (the workspace-zones
  Hyprland plugin stays on hyprpm — ABI-locked to the running compositor)
