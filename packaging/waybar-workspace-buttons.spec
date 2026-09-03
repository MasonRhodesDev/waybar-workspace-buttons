# RPM spec for waybar-workspace-buttons. Built in COPR from a local SRPM
# produced by packaging/build-srpm.sh (source tarball from the git tag).
#
# Ships the Waybar CFFI module (main package) and the workspace-zones
# Hyprland plugin (hyprland-workspace-zones subpackage). The plugin is
# ABI-locked to the exact Hyprland build it runs on (runtime API-hash
# guard); now that the compositor itself is an RPM this is expressed as a
# strict `Requires: hyprland = <built-against>`, computed from hyprland.pc
# at build time. dnf then refuses a hyprland upgrade until a matching
# plugin build exists — rebuild this package on every hyprland bump.
# hyprland-git users keep a ~/.local rebuild (it shadows the packaged .so).

# Exact compositor version the plugin is built against. Evaluates inside the
# binary-build chroot where hyprland-devel is installed; the SRPM stage may
# see 0, which is fine (SRPM Requires are not consumed).
%global hyprland_version %(pkg-config --modversion hyprland 2>/dev/null || echo 0)

Name:           waybar-workspace-buttons
Version:        1.3.1
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
# workspace-zones plugin (hyprland.pc pulls the full hypr*-devel chain)
BuildRequires:  pkgconfig(hyprland) >= 0.56
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(libdrm)
Requires:       waybar
Requires:       jq

%description
A Waybar CFFI module rendering per-monitor workspace buttons for Hyprland
(per-monitor filtering, special-workspace indicators). It talks to the
compositor over the Hyprland IPC socket at runtime, so the compositor itself
is not an RPM dependency (and is commonly not RPM-managed on Hyprland
setups).

Point Waybar at the module by setting the cffi/workspace-buttons
module_path to %{_libdir}/waybar/workspace_buttons.so.

%package -n hyprland-workspace-zones
Summary:        Per-workspace special zones plugin for Hyprland
# Strict lock: the plugin's API-hash guard requires the exact compositor
# build it was compiled against. See the header comment.
Requires:       hyprland = %{hyprland_version}

%description -n hyprland-workspace-zones
Hyprland plugin implementing per-workspace special zones: workspace N owns
special:N, auto-dismissed on leave. Registers Lua functions
(hl.plugin.zones.toggle/move/movesilent) for keybinds; hypr-DE loads it
from %{_libdir}/hyprland/plugins/libworkspace-zones.so. A
~/.local/lib/hyprland-plugins copy, if present, shadows this one (for
hyprland-git rebuilds).

%prep
%autosetup

%build
# Refuse to build a plugin with an unresolved compositor pin.
[ "%{hyprland_version}" != "0" ]
%meson
%meson_build
meson setup plugin-build plugin --prefix=%{_prefix} --libdir=%{_lib} --buildtype=release
meson compile -C plugin-build

%install
%meson_install
meson install -C plugin-build --destdir %{buildroot}

%files
%license LICENSE
%doc README.md
%dir %{_libdir}/waybar
%{_libdir}/waybar/workspace_buttons.so
%{_mandir}/man7/workspace-zones.7*

%files -n hyprland-workspace-zones
%license LICENSE
%dir %{_libdir}/hyprland
%dir %{_libdir}/hyprland/plugins
%{_libdir}/hyprland/plugins/libworkspace-zones.so

%changelog
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
