# gam4980-9588

BBK 9588 port of the GPLv3 `gam4980` emulator core.

This project is intentionally separate from the SDK examples. It builds a
standalone `GAM4980.BDA` with its own entry point, icons, game selector, and
freestanding 9588 payload. It does not read or patch another BDA as a template.

## Local requirements

- `8.BIN` and `E.BIN` installed on the device at:

  ```text
  A:\应用\数据\游戏\gam4980\8.BIN
  A:\应用\数据\游戏\gam4980\E.BIN
  ```

- The MIPS toolchain bundled or configured by the parent 9588 SDK.

Firmware dumps, games, saves, the patched BDA, and other generated artifacts
must not be committed.

## Build

From the SDK workspace root:

```powershell
python .\gam4980-9588\build.py
python -m bda_packer.validate .\gam4980-9588\build\GAM4980.BDA
```

The application scans `.gam` files only in
`A:\应用\数据\游戏\gam4980`. A single game starts automatically; when several
games are present, the built-in selector is shown. The selected game is streamed
directly into emulated flash, so a second game-sized heap buffer is not required.

## Rendering and timing

- The emulator advances at the upstream 60 Hz rate. The 9588 host loop runs at
  40 Hz and schedules core frames in a 1, 2, 1, 2 pattern, then presents only
  the newest frame. If a GUI call overruns a 25 ms host slot, the next iteration
  catches up the missed simulation slots in one batch instead of slowing down
  emulated time or drawing every intermediate frame.
- LCD RAM writes set a dirty flag. Unchanged frames skip both RGB565 conversion
  and GUI submission.
- Changed LCD frames are submitted directly to the visible draw context under
  the firmware draw guard. The full 240x320 interface is drawn only when the
  game view is first created; later updates cover only the native 159x96 LCD.
- The LCD unpacker uses a 16-entry nibble lookup table and aligned 32-bit stores.
- HALT periods are advanced to the next timer event instead of interpreting idle
  CPU cycles one instruction at a time.
- Save flash is written to the filesystem only after the emulated save region
  has actually changed.

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
