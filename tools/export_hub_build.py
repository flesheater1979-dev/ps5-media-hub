"""Export compiled artifacts; never contacts a console. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=Path, default=root / "dist")
    parser.add_argument("--source-commit", default="HEAD")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", args.source_commit], cwd=root, text=True).strip()
    out = args.destination.resolve()
    out.mkdir(parents=True, exist_ok=True)
    artifacts = []
    for name in ("PPSA99179.ffpfsc", "PPSA99179.zip"):
        source = root / "output/app" / name
        if not source.is_file():
            raise FileNotFoundError("Native packaging did not produce " + name)
        if name.endswith(".zip"):
            with zipfile.ZipFile(source) as archive:
                bad = archive.testzip()
                if bad:
                    raise ValueError("ZIP CRC failure: " + bad)
                required = {"PPSA99179/eboot.bin", "PPSA99179/sce_sys/param.json", "PPSA99179/sce_module/libc.prx"}
                if not required.issubset(archive.namelist()):
                    raise ValueError("Incomplete native application ZIP")
                param = json.loads(archive.read("PPSA99179/sce_sys/param.json"))
                if param["titleId"] != "PPSA99179":
                    raise ValueError("Unexpected native title ID")
                if param["localizedParameters"]["en-US"]["titleName"] != "PS5 Media Hub (Test)":
                    raise ValueError("Unexpected title name")
        target = out / name
        shutil.copy2(source, target)
        with target.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        artifacts.append({"name": name, "size": target.stat().st_size, "sha256": digest})
    manifest = {"project": "PS5 Media Hub", "version": "0.1.0-hub-test1",
                "native_source_commit": commit, "title_id": "PPSA99179",
                "target_firmware": "10.00", "hardware_validated": False,
                "loader_target": "ShadowMountPlus with compatible kstuff environment; untested",
                "toolchain": {"sdk": "v0.42", "clang": "18", "pacbrew": "v0.39", "ffmpeg": "7.1.1"},
                "artifacts": artifacts}
    (out / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out / "SHA256SUMS.txt").write_text("".join(a["sha256"] + "  " + a["name"] + "\n" for a in artifacts), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
