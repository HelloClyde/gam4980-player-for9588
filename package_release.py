from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys
import zipfile


PROJECT_ROOT = Path(__file__).resolve().parent
SDK_ROOT = PROJECT_ROOT / "sdk"
APPLICATION_ROOT = PROJECT_ROOT / "应用"
BDA_PATH = APPLICATION_ROOT / "程序" / "GAM4980.BDA"
ROM_PATHS = (
    APPLICATION_ROOT / "数据" / "游戏" / "gam4980" / "8.BIN",
    APPLICATION_ROOT / "数据" / "游戏" / "gam4980" / "E.BIN",
)
ROM_SHA256 = {
    "8.BIN": "1c8f0b75f478cc42b1cc4292ff6c3b022b11384f0b6fc1b9601873a9da656d6f",
    "E.BIN": "9d13aa4593d97b790afc37d73da8be985e7a3aa7f3dcfe6b91c798671067aa5e",
}
ROM_SIZE = 0x200000

if not (SDK_ROOT / "bda_packer").is_dir():
    raise SystemExit(
        "missing sdk submodule; run "
        "git submodule update --init sdk"
    )
sys.path.insert(0, str(SDK_ROOT))

from bda_packer.validate import validate_bda  # noqa: E402


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_inputs() -> tuple[Path, ...]:
    if not BDA_PATH.is_file():
        raise SystemExit(
            f"missing BDA: {BDA_PATH}; run build.py with "
            "--output 应用/程序/GAM4980.BDA"
        )
    report = validate_bda(BDA_PATH)
    if not report["ok"]:
        raise SystemExit(f"BDA validation failed: {report['errors']}")

    for path in ROM_PATHS:
        if not path.is_file():
            raise SystemExit(f"missing runtime ROM: {path}")
        if path.stat().st_size != ROM_SIZE:
            raise SystemExit(f"runtime ROM has the wrong size: {path}")
        digest = file_sha256(path)
        if digest != ROM_SHA256[path.name]:
            raise SystemExit(f"runtime ROM checksum mismatch: {path}")
    return (BDA_PATH, *ROM_PATHS)


def write_release_zip(output: Path, files: tuple[Path, ...]) -> str:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(
        output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in files:
            relative = path.relative_to(PROJECT_ROOT)
            info = zipfile.ZipInfo(relative.as_posix(), (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())

    expected = [path.relative_to(PROJECT_ROOT).as_posix() for path in files]
    with zipfile.ZipFile(output, "r") as archive:
        if archive.namelist() != expected:
            raise SystemExit("release ZIP contains unexpected paths")
        bad_member = archive.testzip()
        if bad_member is not None:
            raise SystemExit(f"release ZIP CRC check failed: {bad_member}")
    return file_sha256(output)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package the deployable GAM4980 application tree"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "build" / "gam4980-player-for9588.zip",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output = args.output.resolve()
    files = validate_inputs()
    digest = write_release_zip(output, files)
    print(f"built release: {output}")
    for path in files:
        print(f"included: {path.relative_to(PROJECT_ROOT).as_posix()}")
    print(f"ZIP: {output.stat().st_size} bytes, sha256={digest}")


if __name__ == "__main__":
    main()
