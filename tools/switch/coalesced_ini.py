#!/usr/bin/env python3
"""Decode / encode Deadpool's config archive (TransGame/Config/Xenon/Cooked/Coalesced.ini).

The archive is the game's 16 ini files in one: a big-endian count of strings (2 per file), then per file its name and
its whole text, each as a big-endian length (including a terminator) and that many bytes. The text is XORed with a
key the executable holds as a UTF-16 string at 0x82005F00; the key index runs on across all strings of the archive,
and each terminator is stored as a raw 0 that does not take a key byte.

The key is not part of this repository: it is read from your own executable, decrypted and decompressed to a basic
image (the README says how), given with --image (or DEADPOOL_IMAGE in the environment).

    tools/switch/coalesced_ini.py --image <default.bin> decode <Coalesced.ini> <folder>   one .ini per file in <folder>
    tools/switch/coalesced_ini.py --image <default.bin> encode <folder> <Coalesced.ini>   back (names in order.txt)
    tools/switch/coalesced_ini.py --image <default.bin> set <Coalesced.ini> <output> File.ini:[Section]:Key=Value ...

`set` changes keys in place (a key missing from the section is added at its end). Decoding then encoding an unchanged
folder gives the original file byte for byte.
"""
import os
import re
import struct
import sys

KEY = b""
KEY_OFFSET = 0x5F00  # 0x82005F00 in the basic image, which starts at 0x82000000


def load_key(image_path):
    """The UTF-16 (big-endian) string at KEY_OFFSET of the decrypted, decompressed executable, up to its terminator."""
    data = open(image_path, "rb").read()
    key = bytearray()
    for i in range(KEY_OFFSET, min(len(data) - 1, KEY_OFFSET + 512), 2):
        hi, lo = data[i], data[i + 1]
        if hi == 0 and lo == 0:
            break
        if hi != 0 or not 0x20 < lo < 0x7F:
            key = bytearray()
            break
        key.append(lo)
    if len(key) < 16:
        raise SystemExit(f"{image_path}: no key at 0x{KEY_OFFSET:X} (not a decrypted, decompressed Deadpool image?)")
    return bytes(key)


def decode(data):
    (count,) = struct.unpack_from(">i", data, 0)
    pos, ki, strings = 4, 0, []
    for _ in range(count):
        (n,) = struct.unpack_from(">i", data, pos)
        pos += 4
        raw = data[pos:pos + n]
        pos += n
        if n < 1 or raw[-1] != 0:
            raise SystemExit(f"string at byte {pos - n}: no terminator")
        out = bytearray()
        for x in raw[:-1]:
            out.append(x ^ KEY[ki % len(KEY)])
            ki += 1
        strings.append(out.decode("latin1"))
    if pos != len(data):
        raise SystemExit(f"{len(data) - pos} bytes after the last string")
    return list(zip(strings[0::2], strings[1::2]))


def encode(files):
    out, ki = bytearray(struct.pack(">i", 2 * len(files))), 0
    for name, text in files:
        for s in (name, text):
            b = s.encode("latin1")
            out += struct.pack(">i", len(b) + 1)
            for x in b:
                out.append(x ^ KEY[ki % len(KEY)])
                ki += 1
            out.append(0)
    return bytes(out)


def set_key(text, section, key, value):
    lines = text.split("\r\n")
    start = next((i for i, l in enumerate(lines) if l.strip().lower() == section.lower()), None)
    if start is None:
        raise SystemExit(f"no section {section}")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("[")), len(lines))
    pat = re.compile(r"^\s*" + re.escape(key) + r"\s*=", re.I)
    hits = [i for i in range(start + 1, end) if pat.match(lines[i])]
    if len(hits) > 1:
        raise SystemExit(f"{section} {key}: {len(hits)} lines")
    if hits:
        lines[hits[0]] = f"{key}={value}"
    else:
        while end > start + 1 and not lines[end - 1].strip():
            end -= 1
        lines.insert(end, f"{key}={value}")
    return "\r\n".join(lines)


def main(argv):
    global KEY
    image = os.environ.get("DEADPOOL_IMAGE")
    if len(argv) >= 2 and argv[0] == "--image":
        image, argv = argv[1], argv[2:]
    if not image:
        print(__doc__)
        return 1
    KEY = load_key(image)
    if len(argv) == 3 and argv[0] == "decode":
        files = decode(open(argv[1], "rb").read())
        os.makedirs(argv[2], exist_ok=True)
        for name, text in files:
            with open(os.path.join(argv[2], name.split("\\")[-1]), "w", encoding="latin1", newline="") as f:
                f.write(text)
        with open(os.path.join(argv[2], "order.txt"), "w") as f:
            f.write("\n".join(name for name, _ in files) + "\n")
        print(f"{len(files)} files to {argv[2]}")
        return 0
    if len(argv) == 3 and argv[0] == "encode":
        names = open(os.path.join(argv[1], "order.txt")).read().split()
        files = [(n, open(os.path.join(argv[1], n.split("\\")[-1]), encoding="latin1", newline="").read())
                 for n in names]
        open(argv[2], "wb").write(encode(files))
        print(f"{len(files)} files to {argv[2]}")
        return 0
    if len(argv) >= 4 and argv[0] == "set":
        data = open(argv[1], "rb").read()
        files = decode(data)
        assert encode(files) == data, "round trip differs"
        for change in argv[3:]:
            m = re.fullmatch(r"([^:]+):(\[[^\]]+\]):([^=]+)=(.*)", change)
            if not m:
                raise SystemExit(f"bad change {change!r} (File.ini:[Section]:Key=Value)")
            fname, section, key, value = m.groups()
            i = next((i for i, (n, _) in enumerate(files) if n.split("\\")[-1].lower() == fname.lower()), None)
            if i is None:
                raise SystemExit(f"no file {fname}")
            files[i] = (files[i][0], set_key(files[i][1], section, key, value))
            print(f"{fname} {section} {key}={value}")
        open(argv[2], "wb").write(encode(files))
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
