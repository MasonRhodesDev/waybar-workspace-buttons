#!/usr/bin/env python3
"""Compare hyprland-workspace-zones' per-target hyprland pin against the
compositor version each target currently ships.

Fedora targets are read live from the COPR project's chroot set (compositor:
nett00n/hyprland), so adding or dropping a chroot needs no change here.
The Arch stable target compares the [mason] pacman repo's
hyprland-workspace-zones pin against extra/hyprland. For every target the
plugin either matches the compositor (ok), lags it (drift -> a rebuild
release is needed; same-NEVRA COPR resubmits never reach installed clients,
and pacman blocks the compositor upgrade until the pin catches up), or is
missing entirely (also drift, e.g. a freshly added target with no build
yet). A target the compositor source does not build for is reported and
skipped. The hyprland-git channel is NOT checked here — arch-repo's
git-builds workflow keeps that pair in lockstep on its own cadence.

Output: a Markdown table on stdout, and GitHub Actions outputs (drift=...,
targets=..., hyprland=...) appended to $GITHUB_OUTPUT when set.

Stdlib only. Exit code is 0 unless the check itself failed.
"""

from __future__ import annotations

import gzip
import io
import json
import os
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

OWNER = "solaris765"
PROJECT = "waybar-workspace-buttons"
PLUGIN = "hyprland-workspace-zones"
COMPOSITOR_OWNER = "nett00n"
COMPOSITOR_PROJECT = "hyprland"
COMPOSITOR = "hyprland"

API = "https://copr.fedorainfracloud.org/api_3"
RESULTS = "https://download.copr.fedorainfracloud.org/results"

NS_COMMON = "{http://linux.duke.edu/metadata/common}"
NS_RPM = "{http://linux.duke.edu/metadata/rpm}"
NS_REPO = "{http://linux.duke.edu/metadata/repo}"


def fetch(url: str) -> bytes | None:
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read()
    except Exception:
        return None


def primary_xml(repo_baseurl: str) -> ET.Element | None:
    """Return the parsed primary metadata for a repo, or None if absent."""
    repomd = fetch(f"{repo_baseurl}/repodata/repomd.xml")
    if repomd is None:
        return None
    root = ET.fromstring(repomd)
    href = None
    for data in root.findall(f"{NS_REPO}data"):
        if data.get("type") == "primary":
            href = data.find(f"{NS_REPO}location").get("href")
            break
    if href is None:
        return None
    blob = fetch(f"{repo_baseurl}/{href}")
    if blob is None:
        return None
    if href.endswith(".gz"):
        blob = gzip.decompress(blob)
    elif href.endswith((".xz", ".zst")):
        # COPR emits gzip primaries today; refuse quietly on anything else.
        return None
    return ET.fromstring(blob)


def ver_key(v: str) -> tuple:
    return tuple(int(p) if p.isdigit() else 0 for p in v.split("."))


def package_version(primary: ET.Element, name: str) -> str | None:
    """Highest version of `name` in the repo."""
    best = None
    for pkg in primary.findall(f"{NS_COMMON}package"):
        if pkg.find(f"{NS_COMMON}name").text != name:
            continue
        ver = pkg.find(f"{NS_COMMON}version").get("ver")
        if best is None or ver_key(ver) > ver_key(best):
            best = ver
    return best


def plugin_pin(primary: ET.Element) -> str | None:
    """The `hyprland = X` requirement of the newest plugin build in the repo."""
    best = (None, None)  # (version-of-plugin, pin)
    for pkg in primary.findall(f"{NS_COMMON}package"):
        if pkg.find(f"{NS_COMMON}name").text != PLUGIN:
            continue
        ver = pkg.find(f"{NS_COMMON}version").get("ver")
        pin = None
        fmt = pkg.find(f"{NS_COMMON}format")
        requires = fmt.find(f"{NS_RPM}requires") if fmt is not None else None
        if requires is not None:
            for entry in requires.findall(f"{NS_RPM}entry"):
                if entry.get("name") == COMPOSITOR and entry.get("flags") == "EQ":
                    pin = entry.get("ver")
        if best[0] is None or ver_key(ver) > ver_key(best[0]):
            best = (ver, pin)
    return best[1]


ARCH_EXTRA_JSON = "https://archlinux.org/packages/extra/x86_64/hyprland/json/"
MASON_DB = "https://masonrhodesdev.github.io/arch-repo/x86_64/mason.db"


def arch_stable_row() -> tuple[str, str, str, str, bool]:
    """(target, plugin pin, compositor, status, drifted) for extra/hyprland
    vs the [mason] repo's hyprland-workspace-zones pin."""
    target = "arch-stable-x86_64"
    raw = fetch(ARCH_EXTRA_JSON)
    if raw is None:
        return (target, "—", "—", "skipped: archlinux.org unreachable", False)
    extra_ver = json.loads(raw)["pkgver"]

    pin = None
    db = fetch(MASON_DB)
    if db is not None:
        with tarfile.open(fileobj=io.BytesIO(db), mode="r:*") as tar:
            for member in tar.getmembers():
                if not member.name.endswith("/desc"):
                    continue
                if not member.name.startswith(f"{PLUGIN}-"):
                    continue
                desc = tar.extractfile(member).read().decode()
                lines = iter(desc.splitlines())
                for line in lines:
                    if line == "%DEPENDS%":
                        for dep in lines:
                            if not dep:
                                break
                            if dep.startswith(f"{COMPOSITOR}="):
                                pin = dep.split("=", 1)[1].split("-", 1)[0]
                        break

    if pin is None:
        return (target, "missing", extra_ver, "DRIFT: no plugin in [mason]", True)
    if pin != extra_ver:
        return (target, pin, extra_ver, "DRIFT: pin behind extra/hyprland", True)
    return (target, pin, extra_ver, "ok", False)


def main() -> int:
    raw = fetch(f"{API}/project?ownername={OWNER}&projectname={PROJECT}")
    if raw is None:
        print("::error::could not read COPR project metadata", file=sys.stderr)
        return 1
    chroots = sorted(json.loads(raw)["chroot_repos"])

    rows = []
    drifted = []
    hypr_versions = set()
    for chroot in chroots:
        compositor_repo = f"{RESULTS}/{COMPOSITOR_OWNER}/{COMPOSITOR_PROJECT}/{chroot}"
        plugin_repo = f"{RESULTS}/{OWNER}/{PROJECT}/{chroot}"

        comp_primary = primary_xml(compositor_repo)
        comp_ver = package_version(comp_primary, COMPOSITOR) if comp_primary is not None else None
        if comp_ver is None:
            rows.append((chroot, "—", "—", "skipped: no compositor build upstream"))
            continue
        hypr_versions.add(comp_ver)

        plug_primary = primary_xml(plugin_repo)
        pin = plugin_pin(plug_primary) if plug_primary is not None else None
        if pin is None:
            rows.append((chroot, "missing", comp_ver, "DRIFT: no plugin build"))
            drifted.append(chroot)
        elif pin != comp_ver:
            rows.append((chroot, pin, comp_ver, "DRIFT: pin behind compositor"))
            drifted.append(chroot)
        else:
            rows.append((chroot, pin, comp_ver, "ok"))

    a_target, a_pin, a_ver, a_status, a_drift = arch_stable_row()
    rows.append((a_target, a_pin, a_ver, a_status))
    if a_drift:
        drifted.append(a_target)
    if a_ver not in ("—",):
        hypr_versions.add(a_ver)

    print("| target | plugin pin | hyprland | status |")
    print("|---|---|---|---|")
    for r in rows:
        print("| " + " | ".join(r) + " |")

    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            f.write(f"drift={'true' if drifted else 'false'}\n")
            f.write(f"targets={' '.join(drifted)}\n")
            f.write(f"hyprland={' '.join(sorted(hypr_versions))}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
