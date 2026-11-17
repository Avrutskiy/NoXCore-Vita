#!/usr/bin/env python3
"""Build the stock Vita viewer, validate its VPK and retain a compact build receipt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zipfile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cache_check(root):
    cache = root.parent / "opennox/build/port-cache/go"
    if not cache.exists():
        return {"exists": False, "bytes": 0}
    size = int(subprocess.check_output(["du", "-sk", str(cache)], text=True).split()[0]) * 1024
    if size > 50_000_000_000:
        raise SystemExit("Go cache exceeds 50 GB: perform idle-cache housekeeping under the workspace AGENTS.md before this build")
    return {"exists": True, "allocated_bytes": size}


def sfo_fields(data):
    magic, _, key_base, data_base, count = struct.unpack_from("<5I", data)
    if magic != 0x46535000:
        raise ValueError("Invalid SFO magic")
    result = {}
    for i in range(count):
        key, fmt, size, _, offset = struct.unpack_from("<HHIII", data, 20 + i * 16)
        start = key_base + key
        name = data[start:data.index(b"\0", start)].decode()
        value = data[data_base + offset:data_base + offset + size]
        result[name] = struct.unpack("<I", value)[0] if fmt == 0x404 else value.rstrip(b"\0").decode()
    return result


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("VITASDK", root.parent / "vita-lab/tools/vitasdk")))
    parser.add_argument("--sdl-source", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    if not (sdk / "share/vita.toolchain.cmake").is_file():
        parser.error("Set VITASDK or pass --sdk to an installed VitaSDK")
    if args.sdl_source and not (args.sdl_source / "CMakeLists.txt").is_file():
        parser.error("--sdl-source must be an unpacked SDL3 source directory")
    before = cache_check(root)
    build = root / "build/vita-viewer"
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, VITASDK=str(sdk))
    commands = [["cmake", "-S", str(root / "vita"), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=" + str(sdk / "share/vita.toolchain.cmake")]]
    if args.sdl_source:
        commands[0].append("-DNOX_SDL_SOURCE=" + str(args.sdl_source.resolve()))
    commands.append(["cmake", "--build", str(build), "-j", str(max(1, args.jobs))])
    log_path = build / "build.log"
    try:
        with log_path.open("w") as log:
            for command in commands:
                print("Configuring viewer" if "-S" in command else "Building Vita VPK", flush=True)
                subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    except subprocess.CalledProcessError:
        print("\n".join(log_path.read_text().splitlines()[-50:]))
        raise
    finally:
        after = cache_check(root)
    vpk = build / "nox-vita-viewer.vpk"
    with zipfile.ZipFile(vpk) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("VPK CRC failure")
        fields = sfo_fields(archive.read("sce_sys/param.sfo"))
        if fields.get("TITLE_ID") != "NXVM00001" or fields.get("APP_VER") != "01.00":
            raise RuntimeError("Unexpected VPK identity")
        if archive.getinfo("eboot.bin").file_size == 0:
            raise RuntimeError("Empty executable")
    inputs = {root / "vita/CMakeLists.txt", root / "vita/build.py"}
    inputs.update((root / "vita/src").glob("*"))
    inputs.update((root / "noxcore/include").rglob("*.hpp"))
    cmake = (root / "vita/CMakeLists.txt").read_text()
    for relative in re.findall(r"\$\{CORE\}/([^\s)]+)", cmake):
        path = root / "noxcore" / relative
        if path.is_file():
            inputs.add(path)
            inputs.update(path.parent.glob("*.hpp"))
    manifest = {str(p.relative_to(root)): digest(p) for p in sorted(inputs) if p.is_file()}
    receipt = {
        "title_id": fields["TITLE_ID"], "version": fields["APP_VER"], "sfo": fields,
        "vpk": vpk.name, "bytes": vpk.stat().st_size, "sha256": digest(vpk),
        "elf_sha256": digest(build / "nox_vita_viewer"),
        "source_commit": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
        "upstream_base": "be29b2b1cca091349ab118ae8a354e9a04e983e1",
        "source_inputs": manifest, "commands": commands,
        "sdk": (sdk / "version_info.txt").read_text(),
        "compiler": subprocess.check_output([str(sdk / "bin/arm-vita-eabi-g++"), "--version"], text=True).splitlines()[0],
        "cache_before": before, "cache_after": after,
        "validation": "ELF linked; SELF and VPK built; ZIP CRCs, title ID, app version and executable checked",
        "hardware_run": "not performed",
    }
    if args.sdl_source:
        sdl = args.sdl_source.resolve()
        # A complete hash manifest identifies local SDK source modifications too.
        files = sorted(p for folder in ["src", "include", "cmake"] for p in (sdl / folder).rglob("*") if p.is_file())
        source_digest = hashlib.sha256()
        for path in files + [sdl / "CMakeLists.txt"]:
            source_digest.update((str(path.relative_to(sdl)) + " " + digest(path) + "\n").encode())
        receipt["sdl_source_sha256"] = source_digest.hexdigest()
        receipt["sdl_revision"] = (sdl / "REVISION.txt").read_text().strip()
    (build / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({key: receipt[key] for key in ["vpk", "bytes", "sha256", "hardware_run"]}, indent=2))


if __name__ == "__main__":
    main()
