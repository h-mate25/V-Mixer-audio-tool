"""Check the external, pinned public corpus against our compiled CLI. No downloads."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

PIN = "81466f2c86d7e27e66ddabf82e31ba1b8f8a8335"
FIXTURE_SHA256 = "817e49b3d9b6243273dd4b9d82c1fec09002288dc9c7f3efc2491e0089ec898f"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def run(exe, raw, width, residue=0):
    p = subprocess.run([exe, "--frame", raw.hex(), "--geometry", f"{width}x12",
                        "--residue", str(residue), "--pcm"], capture_output=True, text=True)
    require(p.returncode == 0, f"CLI failed: {p.stderr or p.stdout}")
    return json.loads(p.stdout)


def main(exe, path):
    data = Path(path).read_bytes()
    require(hashlib.sha256(data).hexdigest() == FIXTURE_SHA256, "wrong public fixture revision/content")
    corpus = json.loads(data)
    require(len(corpus["frames"]) == 17 and len(corpus["windows"]) == 61, "unexpected fixture counts")
    for f in corpus["frames"]:
        r = run(exe, bytes.fromhex(f["hex"]), f["width"], f["residue"])
        pcm = b"".join(struct.pack("<i", v) for v in r["pcm_channel_major"])
        require(hashlib.sha256(pcm).hexdigest() == f["libreac"]["pcm_sha256"], f"PCM mismatch: {f['name']}")
        require(r["sequence"] == f["libreac"]["counter"] and r["source"] == f["src"], f"envelope mismatch: {f['name']}")
        require(r["clean_length"] == f["libreac"]["clean_len"], "length mismatch")
    complete = 0
    for f in corpus["windows"]:
        # Isolated public control windows in a clearly synthetic carrier.
        raw = bytearray(340)
        raw[:6] = b"\xff" * 6
        raw[6:12] = bytes.fromhex(f["src"].replace(":", ""))
        raw[12:14] = b"\x88\x19"
        raw[16:50] = bytes.fromhex(f["hex"])
        raw[-2:] = b"\xc2\xea"
        c = run(exe, raw, 8)["control"]
        require(c["outer_checksum"] == ("exempt" if raw[16:18] == b"\0\0" else "pass"), "outer checksum mismatch")
        if c["dt1"] == "complete_dt1":
            complete += 1
            require(c["lengths_match"] and c["terminator_ok"] and c["inner_checksum_ok"], "invalid complete DT1")
        if not any(raw[18:50]):
            require(run(exe, raw, 8)["rate_class"] is None, "zero carrier announced a rate")
    require(complete == 18, f"expected 18 complete DT1s, got {complete}")
    print(f"PASS: 17 public PCM SHA-256 matches, 61 control windows, {complete} complete DT1s; pin {PIN}")


if __name__ == "__main__":
    main(*sys.argv[1:])
