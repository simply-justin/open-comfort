#!/usr/bin/env python3
"""Build the signed AC profile catalog that devices download.

Reads profiles/index.json (the source list) and writes, into --out:
  index.json    the catalog, each entry with url, sha256 and an ECDSA P-256 signature
  <file>.json   each profile, byte for byte as signed

Devices verify sha256 and the signature over "id|sha256|profileVersion" with the public
key in main/src/Profiles/ProfileCrypto.cpp before they use a profile.

  ./scripts/build-profile-catalog.py --key profile-signing-key.pem \\
      --base-url https://simply-justin.github.io/open-comfort/profiles --out site/profiles
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parent.parent
# Limits from main/src/Profiles/ProfileTypes.hpp; longer values would be cut off on the device.
MAX_ENTRIES, MAX_MODELS, MAX_ID, MAX_MAKE_NAME, MAX_VERSION, MAX_URL = 32, 16, 48, 64, 16, 160
MAX_CATALOG_BYTES = 48 * 1024  # HttpDownloader limit in ProfileRepository.cpp
BUSES = {"uart", "spi", "can"}


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def check_length(entry_id: str, name: str, value: str, limit: int) -> None:
    if not value or len(value.encode()) > limit:
        fail(f"{entry_id}: {name} must be 1-{limit} bytes")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", required=True, help="ECDSA P-256 private key (PEM)")
    parser.add_argument("--base-url", required=True, help="HTTPS URL the --out directory is served at")
    parser.add_argument("--source", default=str(ROOT / "profiles"), help="directory with index.json")
    parser.add_argument("--out", required=True, help="output directory")
    args = parser.parse_args()

    if not args.base_url.startswith("https://"):
        fail("--base-url must be https")
    key = serialization.load_pem_private_key(Path(args.key).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        fail("the key must be ECDSA P-256")

    source = Path(args.source)
    catalog = json.loads((source / "index.json").read_text())
    entries = catalog.get("profiles", [])
    if not 0 < len(entries) <= MAX_ENTRIES:
        fail(f"profiles/index.json needs 1-{MAX_ENTRIES} profiles")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ids = set()
    signed = []
    for entry in entries:
        entry_id = entry.get("id", "")
        check_length(entry_id or "?", "id", entry_id, MAX_ID)
        if entry_id in ids:
            fail(f"{entry_id}: duplicate id")
        ids.add(entry_id)
        check_length(entry_id, "make", entry.get("make", ""), MAX_ID)
        check_length(entry_id, "makeName", entry.get("makeName", ""), MAX_MAKE_NAME)
        version = entry.get("profileVersion", "")
        check_length(entry_id, "profileVersion", version, MAX_VERSION)
        if entry.get("bus") not in BUSES:
            fail(f"{entry_id}: bus must be one of {sorted(BUSES)}")
        models = entry.get("models", [])
        if not 0 < len(models) <= MAX_MODELS:
            fail(f"{entry_id}: 1-{MAX_MODELS} models")
        for model in models:
            check_length(entry_id, "model", model, MAX_ID)
            # The setup page sends "<id>|<model>" in a 48-byte field.
            if len(f"{entry_id}|{model}".encode()) > MAX_ID:
                fail(f"{entry_id}: '{entry_id}|{model}' is longer than {MAX_ID} bytes")
        file_name = entry.get("file", "")
        if not re.fullmatch(r"[a-z0-9_-]+\.json", file_name):
            fail(f"{entry_id}: file must be a plain name like mhi.json")

        data = (source / file_name).read_bytes()
        profile = json.loads(data)
        if profile.get("profileVersion") not in (None, version):
            fail(f"{entry_id}: profileVersion differs between index.json and {file_name}")
        digest = hashlib.sha256(data).hexdigest()
        message = f"{entry_id}|{digest}|{version}".encode()
        signature = key.sign(message, ec.ECDSA(hashes.SHA256())).hex()
        url = f"{args.base_url.rstrip('/')}/{file_name}"
        check_length(entry_id, "url", url, MAX_URL)

        shutil.copyfile(source / file_name, out / file_name)
        published = {k: v for k, v in entry.items() if k != "file"}
        published.update({"url": url, "sha256": digest, "signature": signature})
        signed.append(published)

    text = json.dumps({"schemaVersion": catalog.get("schemaVersion", 1), "profiles": signed}, indent=2)
    if len(text.encode()) > MAX_CATALOG_BYTES:
        fail(f"catalog is {len(text.encode())} bytes; devices read at most {MAX_CATALOG_BYTES}")
    (out / "index.json").write_text(text + "\n")
    print(f"Signed {len(signed)} profile(s) into {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
