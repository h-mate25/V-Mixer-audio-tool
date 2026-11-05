"""Narrow offline PCAPNG -> exact Ethernet hex-lines adapter (stdlib only).

Supported: v1.0 sections, IDB and Enhanced Packet Blocks, Ethernet linktype 1,
untruncated <=65536-byte records, no declared FCS. Both byte orders supported.
No repair, tag stripping, protocol filtering, decryption, or packet transmission.
"""
import argparse
import os
from pathlib import Path
import struct
import tempfile

MAX_BLOCK = 16 * 1024 * 1024
SHB = b"\x0a\x0d\x0d\x0a"


def options(data, endian):
    pos = 0
    while pos < len(data):
        if len(data) - pos < 4:
            raise ValueError("truncated option header")
        code, length = struct.unpack_from(endian + "HH", data, pos)
        pos += 4
        padded = (length + 3) & ~3
        if padded > len(data) - pos:
            raise ValueError("option length exceeds block")
        if code == 0:
            if length != 0 or any(data[pos:]):
                raise ValueError("invalid end-of-options")
            return
        yield code, data[pos:pos + length]
        pos += padded


def records(stream):
    endian = None
    interfaces = []
    packet = 0
    section = 0
    while True:
        header = stream.read(8)
        if not header:
            return
        if len(header) != 8:
            raise ValueError("truncated block header")
        if header[:4] == SHB:
            bom = stream.read(4)
            if bom == b"\x4d\x3c\x2b\x1a":
                endian = "<"
            elif bom == b"\x1a\x2b\x3c\x4d":
                endian = ">"
            else:
                raise ValueError("invalid section byte-order magic")
            size = struct.unpack(endian + "I", header[4:])[0]
            prefix = bom
        else:
            if endian is None:
                raise ValueError("missing section header")
            size = struct.unpack(endian + "I", header[4:])[0]
            prefix = b""
        if size < 12 + len(prefix) or size % 4 or size > MAX_BLOCK:
            raise ValueError("invalid/unsupported block size")
        rest = stream.read(size - 8 - len(prefix))
        if len(rest) != size - 8 - len(prefix):
            raise ValueError("truncated block body")
        if struct.unpack(endian + "I", rest[-4:])[0] != size:
            raise ValueError("block footer length mismatch")
        body = prefix + rest[:-4]
        kind = struct.unpack(endian + "I", header[:4])[0]
        if header[:4] == SHB:
            if len(body) < 16 or struct.unpack_from(endian + "HH", body, 4) != (1, 0):
                raise ValueError("unsupported section version/header")
            if struct.unpack_from(endian + "q", body, 8)[0] != -1:
                raise ValueError("only unknown-length sections supported")
            list(options(body[16:], endian))
            interfaces = []
            section += 1
        elif kind == 1:
            if len(body) < 8:
                raise ValueError("short interface description")
            link, reserved, snaplen = struct.unpack_from(endian + "HHI", body)
            if link != 1 or reserved != 0:
                raise ValueError("only Ethernet LINKTYPE_ETHERNET=1 supported")
            for code, value in options(body[8:], endian):
                if code == 13 and value != b"\0":
                    raise ValueError("declared interface FCS unsupported; preserve capture externally")
            interfaces.append(snaplen)
        elif kind == 6:
            packet += 1
            if len(body) < 20:
                raise ValueError("short enhanced packet block")
            interface, hi, lo, caplen, origlen = struct.unpack_from(endian + "IIIII", body)
            if interface >= len(interfaces):
                raise ValueError("unknown packet interface")
            if caplen != origlen:
                raise ValueError(f"packet {packet}: captured/original lengths differ; no reconstruction")
            snap = interfaces[interface]
            padded = (caplen + 3) & ~3
            if caplen == 0 or caplen > 65536 or (snap and caplen > snap) or padded > len(body) - 20:
                raise ValueError("invalid packet bounds/snaplen")
            for code, value in options(body[20 + padded:], endian):
                if code == 2:
                    if len(value) != 4 or (struct.unpack(endian + "I", value)[0] >> 5) & 15:
                        raise ValueError("invalid flags or per-packet FCS unsupported")
            yield packet, section, interface, (hi << 32) | lo, body[20:20 + caplen]
        elif kind in (4, 5):
            # Name Resolution and Interface Statistics do not contain frame bytes.
            continue
        else:
            raise ValueError(f"unsupported block type {kind}; use external extraction")


def convert(source, target):
    # Atomic output: a later damaged block cannot leave a plausible complete export.
    target = Path(target)
    if target.exists():
        raise ValueError("output already exists; choose a new path")
    temporary = None
    try:
        with open(source, "rb") as src, tempfile.NamedTemporaryFile(
                mode="w", encoding="ascii", dir=target.parent, delete=False) as dst:
            temporary = Path(dst.name)
            count = 0
            for number, section, interface, timestamp, raw in records(src):
                dst.write(f"# packet={number} section={section} interface={interface} timestamp_ticks={timestamp} length={len(raw)}\n")
                dst.write(raw.hex() + "\n")
                count += 1
            if count == 0:
                raise ValueError("no enhanced packet records")
        os.replace(temporary, target)
        return count
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        print(f"Imported {convert(args.input, args.output)} exact Ethernet records")
    except (OSError, ValueError) as exc:
        parser.exit(1, f"import_pcapng: {exc}\n")
