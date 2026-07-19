from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


PROJECT_ROOT = Path(__file__).resolve().parent
SDK_ROOT = PROJECT_ROOT / "sdk"
BUILD_ROOT = PROJECT_ROOT / "build"

if not (SDK_ROOT / "bda_packer").is_dir():
    raise SystemExit(
        "missing sdk submodule; run "
        "git submodule update --init sdk"
    )
sys.path.insert(0, str(SDK_ROOT))

from bda_packer.build import (  # noqa: E402
    ENTRY_OFFSET,
    ENTRY_VA,
    ICON_SPECS,
    ICON_SIZES,
    ICON_START,
    bundled_prefix,
    find_tool,
)
from bda_packer.header import BdaHeaderFields, write_header  # noqa: E402
from bda_packer.validate import validate_bda  # noqa: E402
from bda_packer.vx_icon import make_vx, read_png, resize_cover, rgb565_bytes  # noqa: E402


TITLE = "GAM4980"
CATEGORY = 0x80000004
ICON_PATH = PROJECT_ROOT / "assets" / "gam4980-icon.png"
ICON_TRANSPARENT_KEY = (255, 0, 255)


def run(command: list[str], step: str) -> None:
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(f"{step}: tool not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"{step} failed with exit code {exc.returncode}") from exc


def compile_payload(prefix: str, output_dir: Path) -> bytes:
    cc = find_tool(prefix, "gcc")
    objcopy = find_tool(prefix, "objcopy")
    source = PROJECT_ROOT / "src" / "gam4980_payload.c"
    include_dirs = (
        PROJECT_ROOT / "src",
        SDK_ROOT / "sdk" / "include",
    )
    for path in (source, *include_dirs):
        if not path.exists():
            raise SystemExit(f"missing build input: {path}")

    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        elf = work / "payload.elf"
        raw = work / "payload.bin"
        linker_script = work / "payload.ld"
        map_path = output_dir / "payload.map"
        linker_script.write_text(
            f"""
ENTRY(bda_main)
SECTIONS
{{
  . = 0x{ENTRY_VA:x};
  .text : {{ *(.text.bda_main) *(.text*) }}
  .rodata : {{ *(.rodata*) }}
  .data : {{ *(.data*) *(.sdata*) *(.bss*) *(COMMON) }}
}}
""".strip()
            + "\n",
            encoding="ascii",
        )
        include_args = [item for path in include_dirs for item in ("-I", str(path))]
        run(
            [
                cc,
                "-EL",
                "-march=mips32",
                "-mtune=24kc",
                "-mno-abicalls",
                "-G0",
                "-fno-pic",
                # The verified firmware heap call does not preserve $fp/$s8.
                "-ffixed-fp",
                "-Os",
                "-fomit-frame-pointer",
                "-fno-strict-aliasing",
                "-std=gnu11",
                "-ffreestanding",
                "-fno-builtin",
                "-nostdlib",
                "-Wall",
                "-Wextra",
                *include_args,
                "-Wl,--build-id=none",
                f"-Wl,-T,{linker_script}",
                f"-Wl,-Map,{map_path}",
                str(source),
                "-o",
                str(elf),
            ],
            "payload compile",
        )
        run([objcopy, "-O", "binary", str(elf), str(raw)], "payload objcopy")
        return raw.read_bytes()


def build_app_icons() -> bytes:
    source_width, source_height, source_pixels = read_png(ICON_PATH)
    output = bytearray()

    for width, height in ICON_SPECS:
        resized = resize_cover(
            source_width, source_height, source_pixels, width, height
        )
        pixels = rgb565_bytes(
            resized,
            (10, 20, 27),
            transparent_key=ICON_TRANSPARENT_KEY,
        )
        output.extend(make_vx(width, height, pixels))
    return bytes(output)


def package_bda(payload: bytes) -> bytearray:
    data = bytearray(b"\0" * ENTRY_OFFSET)
    icons = build_app_icons()
    expected_icon_bytes = ENTRY_OFFSET - ICON_START
    if len(icons) != expected_icon_bytes:
        raise SystemExit(
            f"icon area is 0x{len(icons):x}, expected 0x{expected_icon_bytes:x}"
        )
    data[ICON_START:ENTRY_OFFSET] = icons
    data.extend(payload)
    if len(data) % 4:
        data.extend(b"\0" * (4 - len(data) % 4))

    fields = BdaHeaderFields(
        category=CATEGORY,
        file_size_minus_4=len(data) - 4,
        entry_offset=ENTRY_OFFSET,
        icon_start=ICON_START,
        icon0_size=ICON_SIZES[0],
        icon1_size=ICON_SIZES[1],
        icon2_size=ICON_SIZES[2],
        icon3_size=ICON_SIZES[3],
    )
    write_header(data, fields, TITLE)
    return data


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the standalone BBK 9588 gam4980 port")
    parser.add_argument(
        "--output",
        type=Path,
        default=PROJECT_ROOT / "build" / "GAM4980.BDA",
    )
    parser.add_argument("--prefix", help="MIPS toolchain prefix")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    prefix = args.prefix or bundled_prefix() or "mipsel-none-elf-"
    payload = compile_payload(prefix, BUILD_ROOT)
    data = package_bda(payload)
    args.output.write_bytes(data)

    report = validate_bda(args.output)
    if not report["ok"]:
        raise SystemExit(f"output validation failed: {report['errors']}")
    digest = hashlib.sha256(data).hexdigest()
    print(f"built standalone: {args.output}")
    print(f"payload: {len(payload)} bytes at VA 0x{ENTRY_VA:08x}")
    print(f"BDA: {len(data)} bytes, sha256={digest}")


if __name__ == "__main__":
    main()
