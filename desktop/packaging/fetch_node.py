"""Fetch a pinned official node release; authenticate its manifest and archive."""
import hashlib
import pathlib
import platform
import shutil
import sys
import tarfile
import urllib.request
import zipfile

BASE = pathlib.Path(__file__).resolve().parents[1]
VERSION = "1.3.1"
MANIFEST_SHA256 = "477debd7f6b877b539ba9ae4b4ccbe1fceab2f4a2ff8b425946be45d68e6c6c5"
URL = f"https://github.com/chromatic/whippet-experimental-blockchain/releases/download/v{VERSION}/"


def fetch(target=None):
    target = target or {"Windows": "windows", "Linux": "linux", "Darwin": "macos"}[platform.system()]
    machine = platform.machine().lower()
    filename = {
        "windows": f"whippet-{VERSION}-win64.zip",
        "linux": f"whippet-{VERSION}-{'aarch64' if machine in ('aarch64', 'arm64') else 'x86_64'}-linux-gnu.tar.gz",
        "macos": f"whippet-{VERSION}-osx64.tar.gz",
    }[target]
    vendor = BASE / "vendor"
    manifest = vendor / "SHA256SUMS.txt"
    if hashlib.sha256(manifest.read_bytes()).hexdigest() != MANIFEST_SHA256:
        raise RuntimeError("Pinned checksum manifest failed verification")
    hashes = {name: digest for digest, name in (line.split() for line in manifest.read_text().splitlines())}
    archive = vendor / filename
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != hashes[filename]:
        urllib.request.urlretrieve(URL + filename, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != hashes[filename]:
        raise RuntimeError("Node archive checksum mismatch")
    destination = vendor / f"{target}-node"
    destination.mkdir(exist_ok=True)
    names = ["whippetd.exe", "whippet-cli.exe"] if target == "windows" else ["whippetd", "whippet-cli"]
    # Extract only the two fixed expected filenames, never arbitrary archive paths.
    if filename.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle:
            for name in names:
                matches = [m for m in bundle.namelist() if pathlib.PurePosixPath(m).name == name]
                if len(matches) != 1:
                    raise RuntimeError(f"Missing or ambiguous {name}")
                (destination / name).write_bytes(bundle.read(matches[0]))
    else:
        with tarfile.open(archive) as bundle:
            for name in names:
                matches = [m for m in bundle.getmembers() if pathlib.PurePosixPath(m.name).name == name and m.isfile()]
                if len(matches) != 1:
                    raise RuntimeError(f"Missing or ambiguous {name}")
                with bundle.extractfile(matches[0]) as source, (destination / name).open("wb") as output:
                    shutil.copyfileobj(source, output)
                (destination / name).chmod(0o755)
    (destination / "verified.json").write_text(__import__("json").dumps({"release": VERSION, "archive": filename, "sha256": hashes[filename]}, indent=2))
    print(destination)
    return destination


if __name__ == "__main__":
    fetch(sys.argv[1] if len(sys.argv) > 1 else None)
