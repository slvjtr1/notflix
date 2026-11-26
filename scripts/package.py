"""Stage the ARM binary, its private libraries and the native app manifest."""
from pathlib import Path
import hashlib
import io
import json
import re
import shutil
import subprocess
import sys
import tarfile

binary, sdk = map(lambda s: Path(s).resolve(), sys.argv[1:])
root = Path(__file__).resolve().parents[1]
stage = root / "stage"
dist = root / "dist"
if stage.exists():
    shutil.rmtree(stage)
shutil.copytree(root / "package", stage)
shutil.copytree(root / "licenses", stage / "licenses")
shutil.copy2(root / "LICENSE", stage / "licenses/GPL-3.0.txt")
shutil.copy2(root / "third_party/cjson/LICENSE", stage / "licenses/cJSON.txt")
shutil.copy2(binary, stage / "notflix")
libdir = stage / "lib"
libdir.mkdir()
dist.mkdir(exist_ok=True)
system = re.compile(r"^(?:ld-linux[^/]*|lib(?:c|m|dl|pthread|rt|resolv|util|gcc_s|stdc\+\+|SDL2-2\.0|SDL2|EGL|GLESv2|GL|wayland[^.]*|xkbcommon))\.so(?:\..*)?$")
sysroots = list(sdk.glob("*/sysroot"))
if len(sysroots) != 1:
    raise RuntimeError(f"Unexpected SDK sysroots {sysroots}")
sysroot = sysroots[0]
seen = set()
def dependencies(path):
    return re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", subprocess.check_output(["readelf","-d",path],text=True))
def bundle(path):
    for name in dependencies(path):
        if name in seen or system.fullmatch(name):
            continue
        seen.add(name)
        candidates = [folder/name for folder in (sysroot/"usr/lib",sysroot/"lib")]
        src = next((v for v in candidates if v.is_file()), None)
        if src is None:
            raise RuntimeError(f"Missing {name} required by {path}")
        dest=libdir/name
        shutil.copy2(src,dest)
        bundle(dest)
bundle(binary)
arm = subprocess.check_output(["readelf","-h",binary],text=True)
assert re.search(r"Machine:\s+ARM\b",arm)
# Redistribute Ubuntu's Mozilla trust store for verified TLS on older firmware.
shutil.copy2("/etc/ssl/certs/ca-certificates.crt",stage/"assets/ca.pem")
subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=640x360:rate=30","-f","lavfi","-i","sine=frequency=440:sample_rate=48000","-t","5","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-movflags","+faststart",str(stage/"assets/test.mp4")],check=True)
subprocess.run(["ares-package",str(stage),"-o",str(dist)],check=True)
artifact,=dist.glob("*.ipk")
assert artifact.name.endswith("_arm.ipk"), artifact.name
def data_archive(path):
    with path.open("rb") as f:
        if f.read(8)==b"!<arch>\n":
            while header:=f.read(60):
                size=int(header[48:58]); name=header[:16].decode().strip().rstrip("/")
                payload=f.read(size)
                if size%2: f.read(1)
                if name.startswith("data.tar"): return payload
        else:
            f.seek(0)
            with tarfile.open(fileobj=f,mode="r:*") as outer:
                member=next(x for x in outer if Path(x.name).name.startswith("data.tar"))
                return outer.extractfile(member).read()
    raise ValueError("Missing IPK payload")
with tarfile.open(fileobj=io.BytesIO(data_archive(artifact)),mode="r:*") as archive:
    files={m.name.lstrip("./"):m for m in archive if m.isfile()}
    prefix="usr/palm/applications/tv.notflix.native/"
    manifest=json.load(archive.extractfile(files[prefix+"appinfo.json"]))
    assert manifest["type"]=="native" and manifest["id"]=="tv.notflix.native"
    main=files[prefix+manifest["main"]]
    assert main.mode & 0o111
    assert archive.extractfile(main).read(4)==b"\x7fELF"
    assert prefix+"assets/test.mp4" in files and prefix+"assets/brand.ttf" in files
    for lib in seen: assert prefix+"lib/"+lib in files
    assert not any(p.lower().endswith((".apk",".dex",".kt",".aar")) for p in files)
checksum=hashlib.sha256(artifact.read_bytes()).hexdigest()
(dist/"SHA256SUMS").write_text(f"{checksum}  {artifact.name}\n")
print(f"Verified {artifact.name}; {len(files)} files, {len(seen)} bundled libraries.")

