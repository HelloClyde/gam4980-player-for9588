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
  40 Hz and schedules core frames in a 1, 2, 1, 2 pattern. LCD changes are
  captured after every core frame into a 32-slot, 1-bit LCD queue. The queue is
  60 KiB instead of the 960 KiB required by RGB565 frames. Each queued frame
  remains visible for at least three 25 ms host slots before the next frame is
  presented, which prevents a 30 Hz panel scanout from missing updates while
  retaining enough throughput for the game's animation cadence. If the queue
  fills during a long overrun, the oldest frame is dropped to keep latency
  bounded. User input clears queued historical frames so controls are not held
  behind animation playback.
- LCD RAM writes set a dirty flag. Unchanged frames skip both RGB565 conversion
  and GUI submission. Packed LCD frames are expanded to RGB565 only when they
  reach the head of the presentation queue.
- The active 159x96 LCD is scaled in software to 240x145. A settings button
  between the LCD and touch controls selects nearest-neighbor, bilinear, or
  native-resolution display. Bilinear is the default. Native mode centers the
  unscaled 159x96 image inside the 240x145 view. Coordinate maps for the scaled
  modes are computed once at startup, and every RGB565 buffer is submitted at
  its native descriptor size through the formal picture API; the application
  does not depend on unverified firmware picture scaling.
- Changed LCD frames are submitted directly to the visible draw context under
  the firmware draw guard. The full 240x320 RGB565 interface is submitted when
  the view is created or settings change; ordinary updates cover only the
  240x145 LCD view.
- The LCD unpacker uses a 16-entry nibble lookup table and aligned 32-bit stores.
- HALT periods are advanced to the next timer event instead of interpreting idle
  CPU cycles one instruction at a time.
- Save flash is written to the filesystem only after the emulated save region
  has actually changed.

In the 8013 emulator, the `Fumo Ji` title animation improved from 116 visible
updates in 13.44 seconds (8.63 FPS) to 189 updates in 14.26 seconds (13.26 FPS).
The upstream no-ghosting core baseline is 13.86 FPS for the same animation. The
final run reported no invalid GUI calls or recovery events, and a static settings
panel produced no redundant frames over five seconds. These are emulator
measurements, not physical-device benchmarks.

## Controls

- Direction keys: emulated direction keys
- Enter: emulated Enter
- Escape (short press): emulated Exit
- Escape (hold for one second): close the emulator
- Touch controls: direction pad, Enter, Exit, Page Up, and Page Down
- Settings: touch the gear button; use Up/Down and Enter in the settings panel,
  or touch an algorithm row. The panel's X button closes it without changing.

The selected scaling algorithm is stored at:

```text
A:\应用\数据\游戏\gam4980\GAM4980.CFG
```

Missing or invalid configuration files fall back to bilinear scaling.

Save data is written under `A:\应用\数据\游戏\gam4980` using a game-specific
hash filename.

## Upstream

Core source is derived from:

- <https://codeberg.org/iyzsong/gam4980>
- upstream commit `36ce6d076d1103fa4a48e9e775cee28c31c03480`

The emulator core remains licensed under GPLv3; see `COPYING`.
