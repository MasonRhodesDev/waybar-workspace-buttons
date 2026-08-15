# RPM spec for waybar-workspace-buttons. Built in COPR from a local SRPM
# produced by packaging/build-srpm.sh (source tarball from the git tag).
#
# Scope: this package ships ONLY the Waybar CFFI module. The companion
# workspace-zones Hyprland plugin is ABI-locked to the exact Hyprland build
# it runs on (runtime API-hash guard), so it cannot be usefully prebuilt as
# an RPM — it is distributed via hyprpm (see hyprpm.toml) instead.
Name:           waybar-workspace-buttons
Version:        1.0.1
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
* Wed Jul 15 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.1-1
- Workspace click: detect the active Hyprland config dialect and emit the
  matching hyprctl dispatch syntax (Lua expression vs classic string)

* Wed Jul 15 2026 Mason Rhodes <mrhodesdev@gmail.com> - 1.0.0-1
- Initial Fedora packaging: Waybar CFFI module via COPR (the workspace-zones
  Hyprland plugin stays on hyprpm — ABI-locked to the running compositor)
