"""Optional one-time acquisition; never called by CMake/build/tests. TLS stays enabled."""
import argparse
import hashlib
from pathlib import Path
import urllib.request
from check_public_reference import PIN, FIXTURE_SHA256

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path, help="Cache path outside this Git checkout")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
if args.output.resolve().is_relative_to(root):
    parser.error("keep reference material outside the source checkout")
url = f"https://raw.githubusercontent.com/FreeREAC/reac-protocol/{PIN}/spec/fixtures/real_frames.json"
with urllib.request.urlopen(url, timeout=60) as response:
    data = response.read(1024 * 1024)
if hashlib.sha256(data).hexdigest() != FIXTURE_SHA256:
    raise SystemExit("Reference hash mismatch: no file written")
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("xb") as output:
    output.write(data)
print(f"Verified {len(data)} public fixture bytes: {FIXTURE_SHA256}")
