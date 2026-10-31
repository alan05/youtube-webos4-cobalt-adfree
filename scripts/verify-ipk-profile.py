#!/usr/bin/env python3
"""Validate a webOS 4 Cobalt IPK against its selected public application profile."""
import argparse
import io
import json
from pathlib import Path, PurePosixPath
import re
import runpy
import struct
import sys
import tarfile


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def normal_path(name):
    require(not name.startswith("/"), f"Absolute IPK member path: {name}")
    parts = PurePosixPath(name).parts
    require(".." not in parts, f"Parent traversal in IPK: {name}")
    return PurePosixPath(*(p for p in parts if p != ".")).as_posix()


def get_file(archive, items, name):
    item = items.get(name)
    require(item is not None and item.isfile(), f"Missing regular file: {name}")
    handle = archive.extractfile(item)
    require(handle is not None, f"Cannot read {name}")
    return handle.read()


def check_elf(blob, name):
    require(len(blob) >= 20 and blob[:4] == b"\x7fELF",
            f"{name} is not an ELF executable/library")
    require(blob[4] == 1 and blob[5] == 1,
            f"{name} must be 32-bit little-endian ELF")
    require(struct.unpack_from("<H", blob, 18)[0] == 40,
            f"{name} must target ARM (EM_ARM=40)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--profile", choices=("youtube", "youtube-kids"), required=True)
    ap.add_argument("--ipk", type=Path, required=True)
    ap.add_argument("--version", help="Expected package version; default: profile version")
    ap.add_argument("--cobalt", type=Path, help="Optional exact compiled cobalt file to compare")
    ap.add_argument("--webapp-dir", type=Path, help="Optional expected compiled webapp assets")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    profile_file = root / "packaging" / "profiles" / f"{args.profile}.json"
    require(profile_file.is_file(), f"Missing profile {profile_file}")
    profile = json.loads(profile_file.read_text())
    expected_identity = {
        "youtube": ("com.alan05.youtube.cobalt.webos4", "YouTube"),
        "youtube-kids": ("com.alan05.youtubekids.cobalt.webos4", "YouTube Kids"),
    }[args.profile]
    require((profile.get("id"), profile.get("title")) == expected_identity,
            "Profile contains an unexpected public app identity")
    expected_version = args.version or profile["version"]
    require(re.fullmatch(r"\d+\.\d+\.\d+", expected_version) is not None,
            "Expected version must be MAJOR.MINOR.PATCH")
    require(args.ipk.is_file(), f"IPK does not exist: {args.ipk}")

    container = runpy.run_path(str(root / "scripts" / "verify-ipk-container.py"))
    members = container["read_members"](args.ipk)
    require(members["debian-binary"][1] == b"2.0\n", "Invalid Debian format marker")
    container["verify_tar"]("control.tar.gz", members["control.tar.gz"][1])
    container["verify_data_metadata"](members["data.tar.gz"][1])

    with tarfile.open(fileobj=io.BytesIO(members["control.tar.gz"][1]), mode="r:gz") as ctl:
        control_members = {
            normal_path(m.name): m for m in ctl.getmembers() if m.isfile()
        }
        controls = [name for name in control_members if PurePosixPath(name).name == "control"]
        require(len(controls) == 1, f"Expected one control file; got {controls}")
        text = get_file(ctl, control_members, controls[0]).decode("utf-8")
        declared = dict(re.findall(r"(?m)^(Package|Version):\s*(\S+)\s*$", text))
        require(declared.get("Package") == profile["id"], "Control package ID mismatch")
        require(declared.get("Version") == expected_version, "Control version mismatch")

    with tarfile.open(fileobj=io.BytesIO(members["data.tar.gz"][1]), mode="r:gz") as archive:
        items = {}
        for member in archive.getmembers():
            name = normal_path(member.name)
            if name in items and member.isfile():
                raise ValueError(f"Duplicate file member: {name}")
            items[name] = member
            require("sophia" not in name.lower(), f"Personal filename in IPK: {name}")

        manifests = [name for name, m in items.items()
                     if m.isfile() and PurePosixPath(name).name == "appinfo.json"]
        require(len(manifests) == 1, f"Expected one appinfo.json; got {manifests}")
        manifest_name = manifests[0]
        app_root = PurePosixPath(manifest_name).parent
        require(app_root.name == profile["id"], "IPK directory does not match application ID")
        require("usr/palm/applications" in app_root.as_posix(),
                "Unexpected IPK application location")

        actual = json.loads(get_file(archive, items, manifest_name))
        for field in ("id", "title", "vendor", "appDescription", "type", "main",
                      "icon", "largeIcon"):
            require(actual.get(field) == profile.get(field),
                    f"appinfo field mismatch: {field}")
        require(actual.get("version") == expected_version, "App version mismatch")
        require(actual["id"] != "youtube.leanback.v4", "Stock YouTube identity forbidden")
        require("sophia" not in json.dumps(actual).lower(), "Personal metadata found")

        icon_dir = root / ("assets" if args.profile == "youtube"
                           else "packaging/icons/youtube-kids")
        for filename, size in (("icon.png", 80), ("largeIcon.png", 130)):
            blob = get_file(archive, items, (app_root / filename).as_posix())
            require(blob == (icon_dir / filename).read_bytes(),
                    f"{filename} differs from approved {args.profile} icon")
            require(blob[:8] == b"\x89PNG\r\n\x1a\n"
                    and struct.unpack_from(">II", blob, 16) == (size, size),
                    f"Wrong {filename} dimensions")

        binary = get_file(archive, items, (app_root / "cobalt").as_posix())
        check_elf(binary, "cobalt")
        require(b"sophia" not in binary.lower(), "Personal reference embedded in cobalt")
        if args.cobalt:
            require(binary == args.cobalt.read_bytes(), "Cobalt does not match source build")

        for filename in ("libstdc++.so.6", "libgcc_s.so.1", "libatomic.so.1"):
            check_elf(get_file(archive, items, (app_root / "lib" / filename).as_posix()),
                      filename)

        fonts = get_file(archive, items,
                         (app_root / "content/system_fonts/fonts.xml").as_posix())
        require(fonts == (root / "cobalt-platform/webos/system_fonts/fonts.xml").read_bytes(),
                "Incorrect webOS system fonts metadata")

        for filename in ("adblockMain.js", "adblockMain.css", "adblockPreload.js"):
            blob = get_file(archive, items,
                            (app_root / "content/web/adblock" / filename).as_posix())
            require(blob and b"sophia" not in blob.lower(), f"Invalid webapp {filename}")
            if args.webapp_dir:
                require(blob == (args.webapp_dir / filename).read_bytes(),
                        f"Webapp artifact mismatch: {filename}")

    print(f"PASS: {args.profile} IPK identity, icons, ARM runtime, fonts, webapp and ownership")
    print(f"PASS: Package={profile['id']} Version={expected_version}")
    if args.cobalt:
        print("PASS: Exact Cobalt build verified")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, tarfile.TarError, json.JSONDecodeError) as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
