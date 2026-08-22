# RPM spec for waybar-workspace-buttons. Built in COPR from a local SRPM
# produced by packaging/build-srpm.sh (source tarball from the git tag).
#
# Scope: this package ships ONLY the Waybar CFFI module. The companion
# workspace-zones Hyprland plugin is ABI-locked to the exact Hyprland build
# it runs on (runtime API-hash guard), so it cannot be usefully prebuilt as
# an RPM — it is distributed via hyprpm (see hyprpm.toml) instead.
Name:           waybar-workspace-buttons
Version:        1.1.2
Release:        1%{?dist}
Summary:        Waybar CFFI workspace-buttons module for Hyprland
License:        MIT
URL:            https://github.com/MasonRhodesDev/waybar-workspace-buttons
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig(gtk+-3.0)
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
