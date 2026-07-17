# gam4980-9588

BBK 9588 port of the GPLv3 `gam4980` emulator core.

This project is intentionally separate from the SDK examples. It builds a
standalone `GAM4980.BDA` with its own entry point, icons, system file selector,
and freestanding 9588 payload. It does not read or patch another BDA as a template.
The payload includes only the parent SDK's formal `sdk/include/bda_sdk.h`; the
research header and `reverse` include directory are not build dependencies.

## Local requirements

- `8.BIN` and `E.BIN` installed on the device at:

  ```text
  A:\应用\数据\游戏\gam4980\8.BIN
  A:\应用\数据\游戏\gam4980\E.BIN
  ```

- The MIPS toolchain bundled or configured by the parent 9588 SDK.
- A parent SDK revision whose formal include provides the verified heap,
  seek, system file selector, frame lifecycle, and raw RGB565 picture APIs.

Firmware dumps, games, saves, the patched BDA, and other generated artifacts
must not be committed.

Place `.gam` files in:

```text
A:\gam4980\
```

## Build

From the SDK workspace root:

```powershell
python .\gam4980-9588\build.py
python -m bda_packer.validate .\gam4980-9588\build\GAM4980.BDA
```

The application opens the firmware's formal system file selector through
`bda_gui_select_file()`. It defaults to `A:\gam4980\` and filters for `.gam`
files. The selected game is streamed directly into emulated flash, so a second
game-sized heap buffer is not required.

## Rendering and timing

- The emulator advances at the upstream 60 Hz rate. The 9588 host loop runs at
  40 Hz and schedules core frames in a 1, 2, 1, 2 pattern, then presents only
  the newest frame. If a GUI call overruns a 25 ms host slot, the next iteration
  catches up the missed simulation slots in one batch instead of slowing down
  emulated time or drawing every intermediate frame.
- LCD RAM writes set a dirty flag. Unchanged frames skip both RGB565 conversion
  and GUI submission.
- The active 159x96 LCD is nearest-neighbor scaled in software to 240x145 using
  coordinate maps computed once at startup. The scaled RGB565 buffer is then
  submitted at its native descriptor size through the formal picture API; the
  application does not depend on unverified firmware picture scaling.
- Changed LCD frames are submitted directly to the visible draw context under
  the firmware draw guard. The full 240x320 interface is drawn only when the
  game view is first created; later updates cover only the 240x145 LCD view.
- The LCD unpacker uses a 16-entry nibble lookup table and aligned 32-bit stores.
- HALT periods are advanced to the next timer event instead of interpreting idle
  CPU cycles one instruction at a time.
- Save flash is written to the filesystem only after the emulated save region
  has actually changed.

An 8013 emulator A/B run advanced the same game through the same 30-input
sequence in about 23.3 seconds. The 240x145 build executed 0.4% fewer guest
instructions and submitted 97 changed GUI frames versus 105 at native size.
This is an emulator comparison, not a physical-device benchmark.

## Controls

- Direction keys: emulated direction keys
- Enter: emulated Enter
- Escape (short press): emulated Exit
- Escape (hold for one second): close the emulator
- Touch controls: direction pad, Enter, Exit, Page Up, and Page Down

Save data is written under `A:\应用\数据\游戏\gam4980` using a game-specific
hash filename.

## Upstream

Core source is derived from:

- <https://codeberg.org/iyzsong/gam4980>
- upstream commit `36ce6d076d1103fa4a48e9e775cee28c31c03480`

The emulator core remains licensed under GPLv3; see `COPYING`.
