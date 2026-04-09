#!/usr/bin/env python3
"""
build_sprites_pack.py
Builds sprites-pack.zip and sprites-pack-manifest.json for PKSM-Switch.

The ZIP structure expected by SpriteAssetDownloader:
  data.json
  sprites/<name>.png        (regular)
  sprites/<name>_shiny.png  (shiny)

Source: pokesprite-images npm package tarball (npm registry).
  package/pokemon-gen8/regular/<name>.png  → sprites/<name>.png
  package/pokemon-gen8/shiny/<name>.png    → sprites/<name>_shiny.png

Usage:
  python3 build_sprites_pack.py [--out <dir>]

Outputs (in <out>, default: dist/):
  sprites-pack.zip
  sprites-pack-manifest.json
"""

import argparse
import hashlib
import io
import json
import os
import sys
import tarfile
import urllib.request
import zipfile

NPM_TARBALL_URL = "https://registry.npmjs.org/pokesprite-images/-/pokesprite-images-2.7.0.tgz"
DATA_JSON_SRC   = os.path.join(os.path.dirname(__file__), "..", "romfs", "gfx", "data", "data.json")
OUT_DIR_DEFAULT = os.path.join(os.path.dirname(__file__), "..", "dist")

def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def download_with_progress(url: str, label: str) -> bytes:
    print(f"Downloading {label}...")
    req = urllib.request.Request(url, headers={"User-Agent": "PKSM-SpritePacker/1.0"})
    with urllib.request.urlopen(req) as resp:
        total = int(resp.headers.get("Content-Length", 0))
        buf = bytearray()
        block = 65536
        while True:
            chunk = resp.read(block)
            if not chunk:
                break
            buf.extend(chunk)
            if total:
                pct = len(buf) * 100 // total
                print(f"\r  {len(buf)//1024} / {total//1024} KB ({pct}%)   ", end="", flush=True)
        print(f"\r  Done: {len(buf)//1024} KB          ")
        return bytes(buf)

def main():
    parser = argparse.ArgumentParser(description="Build PKSM sprites-pack.zip")
    parser.add_argument("--out", default=OUT_DIR_DEFAULT, help="Output directory")
    parser.add_argument("--version", default="2.7.0", help="pokesprite-images npm version")
    args = parser.parse_args()

    tarball_url = f"https://registry.npmjs.org/pokesprite-images/-/pokesprite-images-{args.version}.tgz"
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)

    # ── 1. Load PKSM data.json ────────────────────────────────────────────────
    data_json_path = os.path.abspath(DATA_JSON_SRC)
    if not os.path.isfile(data_json_path):
        print(f"ERROR: data.json not found at {data_json_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Loading data.json from {data_json_path}")
    with open(data_json_path, "rb") as f:
        data_json_bytes = f.read()

    entries = json.loads(data_json_bytes)["pokemon"]
    sprite_filenames = set(e["file_path"].split("/")[-1] for e in entries)
    print(f"  {len(sprite_filenames)} unique sprite filenames")

    # ── 2. Download npm tarball ───────────────────────────────────────────────
    tgz_bytes = download_with_progress(tarball_url, f"pokesprite-images {args.version}")

    # ── 3. Extract sprites from tarball ──────────────────────────────────────
    print("Extracting sprites from tarball...")
    sprite_data: dict[str, bytes] = {}  # filename (e.g. "abra.png") → bytes
    missing: list[str] = []

    with tarfile.open(fileobj=io.BytesIO(tgz_bytes), mode="r:gz") as tar:
        members = {m.name: m for m in tar.getmembers() if m.isfile()}

        for sprite_filename in sprite_filenames:
            if sprite_filename.endswith("_shiny.png"):
                base = sprite_filename[: -len("_shiny.png")] + ".png"
                folder = "shiny"
            else:
                base = sprite_filename
                folder = "regular"

            # npm tarballs name their root "package/"
            tar_path = f"package/pokemon-gen8/{folder}/{base}"
            if tar_path not in members:
                missing.append(sprite_filename)
                continue

            member = members[tar_path]
            f = tar.extractfile(member)
            if f is None:
                missing.append(sprite_filename)
                continue
            sprite_data[sprite_filename] = f.read()

    found = len(sprite_data)
    print(f"  Found {found} / {len(sprite_filenames)} sprites")
    if missing:
        print(f"  Missing {len(missing)} sprites (will be skipped — incremental sync picks them up):")
        for m in missing[:20]:
            print(f"    {m}")
        if len(missing) > 20:
            print(f"    ... and {len(missing) - 20} more")

    if found == 0:
        print("ERROR: no sprites found — check the npm package version/structure", file=sys.stderr)
        sys.exit(1)

    # ── 4. Build ZIP ──────────────────────────────────────────────────────────
    zip_path = os.path.join(out_dir, "sprites-pack.zip")
    print(f"Building {zip_path} ...")
    zip_buf = io.BytesIO()
    with zipfile.ZipFile(zip_buf, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        # data.json at root
        zf.writestr("data.json", data_json_bytes)
        # sprites
        for sprite_filename, sprite_bytes in sorted(sprite_data.items()):
            zf.writestr(f"sprites/{sprite_filename}", sprite_bytes)

    zip_bytes = zip_buf.getvalue()
    with open(zip_path, "wb") as f:
        f.write(zip_bytes)
    print(f"  ZIP size: {len(zip_bytes) // 1024 // 1024} MB  ({len(zip_bytes):,} bytes)")

    # ── 5. Build manifest ─────────────────────────────────────────────────────
    zip_hash = sha256_hex(zip_bytes)
    data_json_hash = sha256_hex(data_json_bytes)

    manifest = {
        "zip_sha256": zip_hash,
        "data_json_sha256": data_json_hash,
        "sprite_count": found,
        "pokesprite_version": args.version,
    }
    manifest_path = os.path.join(out_dir, "sprites-pack-manifest.json")
    manifest_bytes = json.dumps(manifest, indent=2).encode()
    with open(manifest_path, "wb") as f:
        f.write(manifest_bytes)
    print(f"  Manifest: {manifest_path}")
    print(f"  zip_sha256:       {zip_hash}")
    print(f"  data_json_sha256: {data_json_hash}")

    print("\nDone! Files ready to upload to a GitHub Release:")
    print(f"  {zip_path}")
    print(f"  {manifest_path}")
    print()
    print("After uploading, update SpriteAssetDownloader.cpp:")
    print("  TryApplyZipPack — set the ZIP URL to the release download URL")
    print("  e.g. https://github.com/YOUR/REPO/releases/download/sprites-v1/sprites-pack.zip")

if __name__ == "__main__":
    main()
