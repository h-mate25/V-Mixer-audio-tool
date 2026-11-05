"""CLI and narrow PCAPNG adapter tests, using authored synthetic bytes only."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

EXE = sys.argv.pop(1)
spec = importlib.util.spec_from_file_location("pcapng_import", Path(__file__).parents[1] / "tools/import_pcapng.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


def frame():
    b = bytearray(340)
    b[:6] = b"\xff" * 6
    b[6] = 2
    b[12:16] = bytes.fromhex("88193412")
    b[-2:] = b"\xc2\xea"
    return bytes(b)


def block(kind, body, order):
    size = len(body) + 12
    return struct.pack(order + "II", kind, size) + body + struct.pack(order + "I", size)


def capture(order="<", link=1, cap=None, orig=None, extra=b"", fcs=False):
    raw = frame() if cap is None else cap
    shb = block(0x0a0d0d0a, struct.pack(order + "IHHq", 0x1a2b3c4d, 1, 0, -1), order)
    opts = struct.pack(order + "HH", 13, 1) + b"\x20\0\0\0" if fcs else b""
    idb = block(1, struct.pack(order + "HHI", link, 0, 65536) + opts, order)
    epb = block(6, struct.pack(order + "IIIII", 0, 0, 123, len(raw), len(raw) if orig is None else orig) + raw + b"\0" * (-len(raw) % 4) + extra, order)
    return shb + idb + epb


class Tools(unittest.TestCase):
    def cli(self, *args):
        return subprocess.run([EXE, *args], capture_output=True, text=True)

    def test_single_and_ambiguity(self):
        p = self.cli("--frame", frame().hex())
        self.assertEqual(p.returncode, 1)
        self.assertEqual(json.loads(p.stdout)["status"], "ambiguous")
        p = self.cli("--frame", frame().hex(), "--geometry", "8x12", "--pcm")
        self.assertEqual(p.returncode, 0)
        r = json.loads(p.stdout)
        self.assertEqual(r["sequence"], 0x1234)
        self.assertEqual(r["pcm_channel_major"], [0] * 96)

    def test_invalid_input(self):
        for args in [("--frame", "z"), ("--frame", "0"), ("--frame", "00", "--geometry", "-1x12"),
                     ("--hex-file", "/nonexistent-m1-fixture"), ("--other", "x")]:
            self.assertEqual(self.cli(*args).returncode, 2, args)
        self.assertEqual(self.cli("--frame", "00").returncode, 1)

    def test_stream_and_binary(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "frames.hex"
            path.write_text("# synthetic\n\n" + frame().hex() + "\n00\n" + frame().hex())
            p = self.cli("--hex-file", str(path), "--geometry", "8x12")
            self.assertEqual(p.returncode, 1)
            self.assertEqual([json.loads(x)["status"] for x in p.stdout.splitlines()], ["valid", "invalid", "valid"])
            path.write_bytes(frame())
            self.assertEqual(self.cli("--binary", str(path), "--geometry", "8x12").returncode, 0)
            path.write_bytes(b"")
            self.assertEqual(self.cli("--hex-file", str(path)).returncode, 2)

    def test_endian_and_sections(self):
        for data in [capture("<"), capture(">"), capture("<") + capture(">")]:
            rows = list(importer.records(io.BytesIO(data)))
            self.assertEqual(rows[0][-1], frame())
            self.assertTrue(all(row[-1] == frame() for row in rows))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[1][1], 2)

    def test_bad_pcapng(self):
        data = capture()
        for length in [1, 7, 8, 12, 27, len(data) - 1]:
            with self.assertRaises(ValueError):
                list(importer.records(io.BytesIO(data[:length])))
        for bad in [data[:-4] + b"\0" * 4, capture(link=113), capture(orig=341), capture(fcs=True),
                    capture(extra=struct.pack("<HH", 2, 100)), data + block(3, b"\0" * 4, "<")]:
            with self.assertRaises(ValueError):
                list(importer.records(io.BytesIO(bad)))

    def test_import_to_cli(self):
        with tempfile.TemporaryDirectory() as d:
            src, dst = Path(d) / "source.pcapng", Path(d) / "out.hex"
            src.write_bytes(capture())
            original = src.read_bytes()
            self.assertEqual(importer.convert(src, dst), 1)
            p = self.cli("--hex-file", str(dst), "--geometry", "8x12")
            self.assertEqual(p.returncode, 0)
            self.assertEqual(json.loads(p.stdout)["decoded_samples"], 96)
            self.assertEqual(src.read_bytes(), original)
            with self.assertRaises(ValueError):
                importer.convert(src, dst)
            failed = Path(d) / "failed.hex"
            src.write_bytes(capture()[:-1])
            with self.assertRaises(ValueError):
                importer.convert(src, failed)
            self.assertFalse(failed.exists())


if __name__ == "__main__":
    unittest.main()
