#!/usr/bin/env python3
"""Sign an OpenComfort AC profile for remote distribution.

Message format:
  profileId|sha256hex|profileVersion

Example:
  ./scripts/sign-profile.py \\
      --key keys/profile_signing_private.pem \\
      --profile main/spiffs/profiles/mhi.json \\
      --profile-id mitsubishi_heavy_industries \\
      --version 1.0.0
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", required=True, help="ECDSA P-256 private key PEM")
    parser.add_argument("--profile", required=True, help="Profile JSON path")
    parser.add_argument("--profile-id", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    profile_bytes = Path(args.profile).read_bytes()
    digest = hashlib.sha256(profile_bytes).hexdigest()
    message = f"{args.profile_id}|{digest}|{args.version}".encode("utf-8")

    private_key = serialization.load_pem_private_key(
        Path(args.key).read_bytes(), password=None
    )
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        print("Private key must be ECDSA", file=sys.stderr)
        return 1

    signature = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    print(f"sha256={digest}")
    print(f"message={message.decode('utf-8')}")
    print(f"signature={signature.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
