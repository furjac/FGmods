#!/usr/bin/env python3
"""
encrypt_strings.py
──────────────────
Companion script for native-lib.cpp.

Usage:
    python3 encrypt_strings.py

Outputs C++ array declarations ready to paste into the
"ENCRYPTED DATA" section of native-lib.cpp.

The key is a random multi-byte rolling key (not a single byte), so
simple single-key XOR crackers won't work.
"""

import os
import secrets
import textwrap

# ── Strings to encrypt ────────────────────────────────────────────────────────
# Replace these with your actual values.
STRINGS = {
    "URL":        "https://sync-81735-default-rtdb.firebaseio.com/",
    "BANS":       "/bans/",
    "KEYS":       "/keys/",
    "DEVICES":    "/devices/",
    "CFG_UPDATE": "/config/update.json",
    "CFG_UI":     "/config/ui_texts.json",
}

KEY_LEN = 16  # bytes per key


def encrypt(plaintext: str, key: bytes) -> bytes:
    data = plaintext.encode("utf-8")
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def to_c_array(name_enc: str, name_key: str,
               enc: bytes, key: bytes) -> str:
    def fmt(b: bytes) -> str:
        hex_vals = ", ".join(f"0x{x:02X}" for x in b)
        # Wrap at 80 chars
        return "\n    ".join(textwrap.wrap(hex_vals, width=72))

    return (
        f"static const unsigned char {name_enc}[] = {{\n"
        f"    {fmt(enc)}\n"
        f"}};\n"
        f"static const unsigned char {name_key}[] = {{\n"
        f"    {fmt(key)}\n"
        f"}};\n"
    )


if __name__ == "__main__":
    print("// ── Auto-generated encrypted string arrays ──────────────────────")
    print("// Paste this block into native-lib.cpp replacing the TODO sections.")
    print()
    for name, plaintext in STRINGS.items():
        key = secrets.token_bytes(KEY_LEN)
        enc = encrypt(plaintext, key)
        print(f"// Original: {plaintext!r}")
        print(to_c_array(f"{name}_ENC", f"{name}_KEY", enc, key))
