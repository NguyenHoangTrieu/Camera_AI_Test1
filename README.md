# Camera_AI_Test1

Real-time face-detection camera on **FRDM-MCXN947**. Captures frames from an
OV7670 camera via SmartDMA, runs a single-class ("face") Edge Impulse FOMO
model per frame, and shows the result as a text status line on a TFT panel.
On detection, saves a snapshot (with a box drawn around the face) to the
TFT panel's onboard microSD card, rate-limited to 1 photo/sec.

**Status:** camera + AI + SD snapshot pipeline confirmed working on real
hardware, last verified `2026-08-25` — but that was against the earlier
8-bit-parallel LCD shield. The Arduino-header display was since swapped to
a 2.4" SPI TFT module (`source/display/lcd_spi_hw.c`), sharing one
hardware SPI bus with the module's onboard microSD slot and XPT2046 touch
controller (`source/spi1_bus.c`, `source/display/touch_xpt2046.c`).
**The LCD itself is now confirmed working on real hardware** (`2026-09-04`,
camera-preview build: correctly oriented image, no visible bus-sharing
corruption). fps was measured and improved on real hardware — **2fps →
5fps → 7fps** across two confirmed bug fixes (see Known Limitations /
WORKLOG.md) — but the ~24fps target is not yet reached, and the next step
up is meaningfully riskier, so work is paused there pending user input.
The bus-sharing baud-reclaim logic and touch are still **build-verified
only, not yet exercised on real hardware** (see Known Limitations).

## Overview

Pipeline: `Camera (OV7670/SmartDMA)` → `AI inference (CPU or Neutron NPU)` → `LCD status text`.

Inference runs on either the CPU (CMSIS-NN) or the Neutron NPU (default,
~4ms/inference on real hardware).

- [ARCHITECTURE.md](ARCHITECTURE.md) — design decisions, memory-conflict
  pitfalls, NPU integration details for this specific build.
- [KNOWLEDGE.md](KNOWLEDGE.md) — plain-language explainer of the underlying
  concepts (DVP camera protocol, coprocessors, NPU/quantization, embedded
  RAM/power domains, SWD debug, DCDC voltage conflicts) for anyone new to
  this domain.
- [WORKLOG.md](WORKLOG.md) — full dated bring-up history.
- Original request: [requirement.md](requirement.md).

## Hardware

| Component | Model / Part | Notes |
|---|---|---|
| Board | FRDM-MCXN947 | |
| Camera | OV7670 | → **J9** (SmartDMA/Camera header) |
| Display | 2.4" TFT, 240x320, SPI (ILI9341-family) | → Arduino header (hardware LPSPI1, shared bus) |
| Touch | XPT2046 (on the same TFT module) | → Arduino header, same shared SPI bus; wired up but not read from anywhere in the app - see Known Limitations |
| microSD | TFT panel's onboard slot (SPI mode, FAT-formatted card) | → Arduino D10..D13, same shared SPI bus (hardware LPSPI1) |
| Debug probe | On-board MCU-Link (CMSIS-DAP) | flashing + serial console |

**Board rework required:** change `SJ16`, `SJ26`, `SJ27` from the right side
to the left side before attaching the camera to J9 (per NXP's reference
design).

### Pinout — Camera (OV7670 → J9)

| Camera signal | MCU pin | J9 pin # |
|---|---|---|
| SIOC / SIOD (SCCB) | P3_2 / P3_3 (LP_FLEXCOMM7) | 19 / 20 |
| XCLK | P2_2 (CLKOUT) | 16 |
| PCLK | P0_5 | — |
| HREF | P0_11 | — |
| VSYNC | P0_4 | — |
| D0..D7 | P1_4, P1_5, P1_6, P1_7, P3_4, P3_5, P1_10, P1_11 | 7,8,9,10,11,12,13,14 |
| 3V3 / GND | — | 21 / 22 |

### Pinout — TFT + microSD + touch (Arduino header, ONE shared hardware SPI bus)

2.4" SPI TFT module (ILI9341-family LCD + onboard microSD slot + XPT2046
touch controller). Not a stacking Arduino shield like the previous panel —
wire each pin to the FRDM-MCXN947's Arduino header with jumper wires.

**All three devices share one physical SPI bus** (SCK/MOSI/MISO on Arduino
D13/D11/D12 — the board's only Arduino-header SPI peripheral, hardware
LPSPI1), each with its own chip-select. This is standard wiring for this
class of module (the LCD's SDI/SCK/SDO, the microSD slot's SD_MOSI/SD_SCK/
SD_MISO, and touch's T_DIN/T_CLK/T_DO are 3 silkscreen labels for the same
3 electrical signals) and keeps pin usage low — only 10 Arduino pins used
total, `D0`-`D7` entirely free. See `source/spi1_bus.c` for how the sharing
works (bus init, per-transaction baud-rate reclaiming, chip-select
handling) — **this bus-sharing logic is new and untested on real
hardware**, more so than a single-device SPI driver would be; see Known
Limitations before assuming it's solid.

| Panel pin | Arduino pin | MCU pin | Notes |
|---|---|---|---|
| SCK / SD_SCK / T_CLK | D13 | P0_25 | shared bus, hardware LPSPI1 SCK |
| SDI(MOSI) / SD_MOSI / T_DIN | D11 | P0_24 | shared bus, hardware LPSPI1 SDO (MCU→devices) |
| SDO(MISO) / SD_MISO / T_DO | D12 | P0_26 | shared bus, hardware LPSPI1 SDI (devices→MCU); needs the internal pull-up `pin_mux.c` already enables here |
| LCD CS | A3 | P0_22 | manual GPIO, LCD-only |
| LCD DC | A2 | P0_14 | manual GPIO, command/data select |
| LCD RESET | A4 | P0_15 | manual GPIO |
| LCD LED (backlight) | A5 | P0_23 | manual GPIO, driven high in `LCD_Init()` |
| SD_CS | D10 | P0_27 | **real hardware** LPSPI1 PCS0 - SD-only, see below |
| T_CS | D9 | P0_10 | manual GPIO, touch-only |
| T_IRQ | D8 | P0_28 | manual GPIO input (pull-up enabled), active low when touched |
| VCC, GND | board power header | direct | 3.3V or 5V per the panel's regulator — check the specific module |

Only the microSD slot uses the peripheral's real hardware chip-select
(PCS0/D10) — `SDSPI_Init()` needs to flip its active polarity at runtime,
which only works through actual PCS hardware. The LCD and touch each use a
plain GPIO pin for CS instead, toggled manually around every transfer.

**MADCTL (`0x36`) in `lcd_spi_hw.c`'s `LCD_InitPanel()`**: `MV=1`
(rotation) confirmed correct on real hardware (`2026-09-04`). `BGR` was
carried over from the previous (parallel) panel and confirmed WRONG on
this one — showed as a strong blue/cyan color cast over the whole image;
fixed by clearing the bit (`0x28` → `0x20`). If colors still look off
after that fix, or the image is mirrored, keep adjusting the `MV`/`MX`/
`MY`/`BGR` bits there against real hardware (see Known Limitations).

**Touch (XPT2046) is wired up but not read from anywhere in `main.c`** —
this face-detection pipeline has no touch UI. `source/display/
touch_xpt2046.c` provides `TOUCH_Init()`/`TOUCH_IsPressed()`/
`TOUCH_ReadRaw()` (raw, uncalibrated 12-bit ADC counts) for a future
feature to build on; X/Y channel mapping is a common-convention guess
(0x90/0xD0 command bytes), unverified against this specific panel.

## Getting Started

### Prerequisites

- `arm-none-eabi-gcc`, `west` (via the venv at `../../tools/westenv`)
- Local `mcuxsdk` checkout at `../mcuxsdk`
- `pyocd` for flashing over the on-board MCU-Link

### Build / Flash / Run

```bash
./firmware/camera_ai_demo/build.sh          # build, then flash
./firmware/camera_ai_demo/build.sh build    # build only
./firmware/camera_ai_demo/build.sh flash    # flash the last build
./firmware/camera_ai_demo/build.sh monitor  # serial console (115200-8-N-1)
```

`build.sh` runs `west build -b frdmmcxn947 firmware/camera_ai_demo
--toolchain armgcc -Dcore_id=cm33_core0` from the `../mcuxsdk` west
workspace, then flashes the `.elf` via pyOCD, auto-selecting the probe by
its MCU-Link unique ID. See `../../touch_rgb/README.md` for the
probe-permissions/udev-rule note if `pyocd` needs `sudo`.

**If flashing fails with `DebugPortStart`/`ResetCatchClear`/`ResetSystem`
`WAIT ACK`/`FAULT ACK` errors** — a standing probe/pyOCD/CMSIS-Pack quirk on
this board, unrelated to firmware correctness — see
[ARCHITECTURE.md §5](ARCHITECTURE.md#5-debugging--tooling-notes) for the
working recipe (`nxpdebugmbox` + specific pyOCD flags).

### Expected output on success

```
Camera_AI_Test1 - FRDM-MCXN947
Camera: OV7670 on J9 SmartDMA/Camera header
Display: Arduino-header LCD status text (camera + AI hook)

Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
AI_MODEL_Init: Neutron NPU face detector ready (72x72 input, 1 class(es), arena used 94388/122880 bytes)
LCD: hardware SPI (LPSPI1, shared bus) on the Arduino header
AI_MODEL_RunInference: total classifier time = 3957us (3ms)
AI result: box[0] label=face x=<N> y=<N> w=<N> h=<N> score=<N>%
```

LCD shows two text lines — `FACE: 1`/`FACE: 0`, and `CAPTURE: 1`/`CAPTURE: 0`
(lit for 4s right after a snapshot is saved, see "Snapshot on Face
Detection" below) — not a live image.

## Configuration

Pass any of these to `build`/`rebuild`/`all` as `-D<FLAG>=ON|OFF`.

| Flag | Default | Effect |
|---|---|---|
| `AI_MODEL_USE_NPU` | `ON` | Inference backend: Neutron NPU (`ON`, ~4ms/inference) vs. CPU + CMSIS-NN via Edge Impulse SDK (`OFF`). Same detection logic either way. |
| `LCD_CAMERA_PREVIEW` | `OFF` | Skip AI, stream raw camera feed to the LCD as fast as frames arrive — for focusing the lens by eye. Prints `LCD preview: N fps` once/sec over UART — see Known Limitations for the fps math/target. |
| `LCD_ARDUINO_HEADER_BITBANG` | `ON` | `ON`: drive TFT via hardware SPI on the Arduino header, shared with the microSD/touch bus (default). `OFF`: J8 header (FlexIO/bit-bang) — abandoned, no touch/SD sharing there, see [ARCHITECTURE.md §4](ARCHITECTURE.md#4-known-constraints--trade-offs). |
| `USB_STREAM_DIAGNOSTIC_DISABLE` | `ON` | `OFF` opts into the abandoned UVC webcam streaming path (doesn't drive the LCD) — see [ARCHITECTURE.md §4](ARCHITECTURE.md#4-known-constraints--trade-offs). |

`AI_MODEL_USE_NPU` and `LCD_CAMERA_PREVIEW` are independent of the LCD/USB
flags; combining `LCD_CAMERA_PREVIEW=ON` with
`USB_STREAM_DIAGNOSTIC_DISABLE=OFF` builds but isn't a meaningful
combination.

### Example commands

```bash
# Default build (Neutron NPU, camera+AI+LCD status text, no USB)
./firmware/camera_ai_demo/build.sh build

# Lens-focus diagnostic: raw camera feed straight to the LCD, no AI
./firmware/camera_ai_demo/build.sh build -DLCD_CAMERA_PREVIEW=ON

# CPU + CMSIS-NN inference instead of the Neutron NPU
./firmware/camera_ai_demo/build.sh build -DAI_MODEL_USE_NPU=OFF

# J8 header (FlexIO) instead of the Arduino header's SPI panel - abandoned, see ARCHITECTURE.md §4
./firmware/camera_ai_demo/build.sh build -DLCD_ARDUINO_HEADER_BITBANG=OFF

# Flags combine freely - build only (no flash), CPU backend + lens-focus preview
./firmware/camera_ai_demo/build.sh build -DAI_MODEL_USE_NPU=OFF -DLCD_CAMERA_PREVIEW=ON
```

`build` builds without flashing; `all` (or bare `./build.sh` with no
arguments) builds then flashes; `rebuild` cleans `build/` first - useful
when switching flags that change which source files get compiled (e.g.
`LCD_ARDUINO_HEADER_BITBANG`), since CMake doesn't always notice a
source-file-list change from a stale cache. Flags stick in `build/`'s
CMake cache once set, so a later plain `./build.sh build` reuses whatever
flags were last passed - pass the flags again (or `rebuild`) to be sure.

## AI Model

Edge Impulse project **"Face_Detection_NXP"** (ID `1095726`, deploy v2),
FOMO detector, 72x72 int8 input, single class `face`, trained on
WIDER FACE + DarkFace (**both CC-BY-NC-ND/research-only — not cleared for
commercial use as-is**).

| | Neutron NPU (default) | CPU + CMSIS-NN |
|---|---|---|
| Build flag | (default) | `-DAI_MODEL_USE_NPU=OFF` |
| Source | `source/ai/model_runner_npu.cpp` | `source/ai/model_runner.cpp` |
| Tensor arena | 120KB static (`m_data`), 94,388 bytes used | 112,460 bytes (`m_sramx` primary + `m_data` overflow) |
| Measured on hardware | ~3.96ms/inference | not yet separately measured for this model |

To swap in a different/retrained model: re-export from Edge Impulse Studio
(new C++ library for the CPU path; new `.tflite` run through
`neutron_converter` for the NPU path) and update
`model_runner.cpp`/`model_runner_npu.cpp` if tensor shapes or class labels
changed. See [ARCHITECTURE.md §2](ARCHITECTURE.md#2-components) for how
the NPU conversion and integration actually work.

## Snapshot on Face Detection

When a face is detected, `source/storage/snapshot.c` draws a green box
around it (reusing `source/display/bbox_overlay.c`, originally built for
the abandoned live-image LCD path — see "Abandoned Features") directly
into the camera frame buffer and saves it as an uncompressed 16-bit BMP
(`FACE0001.BMP`, `FACE0002.BMP`, ...) to the microSD card — **at most 1
capture/sec** (`SNAPSHOT_RATE_LIMIT_MS`), enforced via the DWT cycle
counter, never a second capture within the same one-second window.
Filenames never overwrite a previous session's snapshots (probes for the
first free name once per boot).

The box drawn in the saved file is the model's raw grid-cell box,
unscaled/unpadded — small relative to a real face, since FOMO doesn't
regress an actual bounding box size (see
[ARCHITECTURE.md §2](ARCHITECTURE.md#2-components)); a 2.5x cosmetic
expansion was tried and reverted (kept the raw box on purpose).

The LCD gets a second status line, `CAPTURE: 1`/`CAPTURE: 0`, lit for 4s
after a save (`SNAPSHOT_NOTICE_DURATION_MS`, independent of the 1s rate
limit — 1s wasn't enough time for a person to notice and react, confirmed
on real hardware) — the box itself is never drawn on the LCD, only in
the saved file. No SD card present → logged once at boot, snapshot
capture silently no-ops every frame after that (rest of the pipeline runs
normally).

Every save logs how long the actual SD card write took (DWT cycle
counter, same technique `AI_MODEL_RunInference` uses):
```
Snapshot: saved FACE0030.BMP (write took 187342us, 187ms)
```
Confirmed on real hardware (2026-08-25) this was originally **~3.3
seconds/save** — `s_host.busBaudRate` (`source/storage/sd_spi_disk.c`)
was stuck at the mandatory 400kHz card-identification speed forever,
never switched up to a real operating speed. Fixed (`SD_SPI_OPERATING_BAUDRATE`,
now 8MHz) — see [WORKLOG.md](WORKLOG.md) for the full measurement and
root cause.

Implementation notes: writes the frame buffer to the file directly with a
single `f_write()` call (top-down BMP row order matches the buffer's own
layout, RGB565 needs no pixel conversion) — no second full-frame buffer,
the single biggest lever for keeping this feature's RAM cost low given
`m_data` is already >90% used (see below). See
[ARCHITECTURE.md §2](ARCHITECTURE.md#2-components) for the LPSPI1/FatFs
integration details.

## Project Structure

```
Camera_AI_Test1/
  README.md, ARCHITECTURE.md, WORKLOG.md, requirement.md
  firmware/camera_ai_demo/
    CMakeLists.txt, prj.conf, build.sh
    board_port/            <- pin muxing, hardware_init.c, ei_sramx.ld (board.c/clock_config.c come from the SDK)
    source/
      main.c                <- capture -> AI inference -> LCD status-text loop
      fault_handler.c        <- HardFault register-decode dump handler
      spi1_bus.c/h            <- shared hardware LPSPI1 bus (LCD + microSD + touch), see file header comment
      camera/                <- OV7670 + SmartDMA driver (from NXP reference)
      display/                <- lcd_spi_hw.c (default, Arduino header, hardware SPI), touch_xpt2046.c (Arduino header only), lcd_bitbang.c/lcd_flexio_mculcd.c (abandoned J8 path), text_overlay.c
      ai/
        model_runner.h        <- shared inference API, both backends implement
        model_runner.cpp      <- CPU + CMSIS-NN backend
        model_runner_npu.cpp  <- Neutron NPU backend (default)
        ei_sramx_alloc.c/h    <- custom allocator for the CPU path's tensor arena in m_sramx
        edge_impulse/          <- Edge Impulse C++ library export
        neutron/                <- NPU-converted model (tflite_learn_..._npu.tflite/.h)
      usb/                    <- abandoned UVC streaming path
      storage/
        snapshot.c/h           <- rate-limited box-draw + BMP save on face detection
        sd_spi_disk.c/h         <- shared-bus SD-over-SPI + FatFs diskio glue (see ARCHITECTURE.md §2)
        ffconf.h                 <- hand-written FatFs config (RAM-minimal: FF_FS_TINY=1, no LFN)
```

## Known Limitations

- LCD shows text status lines only, not a live image or bounding boxes
  (by design, to save RAM — see "Abandoned Features" below); the box drawn
  on face detection only ever exists in the saved BMP file.
- CPU-path inference timing for the current model hasn't been separately
  measured on hardware yet (NPU path is measured: ~4ms/inference).
- Not yet validated against a real face specifically — pipeline (capture →
  resize → inference → decode) confirmed working end-to-end with real
  non-flat camera data, but detection accuracy is only backed by Edge
  Impulse Studio's validation/test-set metrics so far (see WORKLOG.md).
- The Arduino-header panel was swapped from an 8-bit-parallel shield to a
  2.4" SPI TFT module sharing one hardware SPI bus with its onboard
  microSD slot and XPT2046 touch controller (`source/spi1_bus.c`,
  `source/display/lcd_spi_hw.c`, `source/display/touch_xpt2046.c`).
  **The LCD path itself is now confirmed on real hardware** (`2026-09-04`,
  camera-preview build): correct orientation, no visible corruption. One
  real bug was found this way and fixed — MADCTL's `BGR` bit (carried over
  from the previous panel) was wrong, causing a blue/cyan color cast;
  cleared to `0x20`, not yet re-confirmed after the fix (no hardware
  access at the time). Two things are still genuinely untested, not just
  build-verified: the bus-sharing baud-reclaim logic (`source/spi1_bus.h`;
  the camera-preview test above never touched the SD card, so this is
  unexercised) and touch (`TOUCH_ReadRaw()`, zero real-hardware
  verification at all). If SD snapshots stop working or come back
  corrupted (they worked before this panel swap, see below), suspect the
  baud-reclaim logic first (`disk_read()`/`disk_write()` in
  `sd_spi_disk.c`, `LCD_BeginTransaction()` in `lcd_spi_hw.c`).
- **fps target (~24fps) not reached — CONFIRMED at 7fps as of
  `2026-09-04`, up from 2fps, via two confirmed real-hardware bug fixes,
  self-debugged over live SWD (see WORKLOG.md for the full trail).**
  Summary: (1) `lcd_spi_hw.c`/`touch_xpt2046.c` were missing
  `kLPSPI_MasterPcsContinuous`, causing the LPSPI peripheral to insert a
  full chip-select setup/hold delay between every single byte regardless
  of whether the (intentionally unrouted) PCS pin was physically wired to
  anything — fixed, took fps from 2→5. (2) `SPI1_BUS_SetBaudRate()` only
  updated the SCK divider, never the PCS-to-SCK/last-SCK-to-PCS/between-
  transfer delay registers, which stayed calibrated to `spi1_bus.c`'s
  throwaway 400kHz init baseline and applied a stale ~1.25µs delay to
  every byte regardless of chunk size — CONFIRMED via a self-directed
  `pyocd commander` SWD register read (not just serial timing), fixed by
  recomputing those delays every time any device on the shared bus
  changes rate — took fps 5→7. Both fixes benefit every device on the
  shared bus (SD/LCD/touch), not just the LCD.
  **eDMA was then attempted and ABANDONED** — the remaining bottleneck
  (`fsl_lpspi.c`'s blocking transfer pushing 153,600 individual FIFO-
  register accesses per 320x240 frame) looked like a natural fit for
  DMA-driven transfers, but two independent eDMA variants (16-bit SPI
  frames, then 8-bit matching mcuxsdk's own tested reference example)
  both hung on real hardware — CONFIRMED via live SWD register reads that
  the TX eDMA channel completes but the RX eDMA channel (whose completion
  the SDK's LPSPI+eDMA driver waits on even for TX-only transfers) never
  signals done, root cause not found despite ruling out clock config,
  DMA channel-mux IDs, and NVIC auto-enable. Reverted to the CPU-polled
  7fps path rather than risk a non-functional display — `spi1_bus.c`'s
  eDMA code (`SPI1_BUS_TransferBytesDMA()`) is left in place, compiled
  but unused, for a future session. If the image comes back glitchy/
  torn/noisy at the current 24MHz SPI clock, lower `LCD_SPI_BAUDRATE_HZ`
  (`lcd_spi_hw.c`) toward 2-6MHz first.
- SD card mount/init is **confirmed working on real hardware**
  (`Snapshot: SD card ready.`) as of 2026-08-25, after fixing 5 separate
  bugs found via live SWD debugging and real-hardware testing - a boot
  hang (SDK Kconfig gap + a retry-loop-nesting issue), a floating
  SD_DO/MISO line that needed this chip's internal pull-up enabled (the
  shield's own SD slot doesn't provide one), a false "timed out" error on
  every real file write (a deadline check that was meant for boot-time
  init only, but ran on every SPI transaction forever after), and 3
  missing LCD font glyphs (`CAP   E` instead of `CAPTURE`). Full incident
  writeup: [WORKLOG.md](WORKLOG.md),
  [ARCHITECTURE.md §5](ARCHITECTURE.md#5-debugging--tooling-notes). Still
  not separately confirmed: an actual face-triggered capture
  (`Snapshot: saved FACE0001.BMP`) succeeding end-to-end - no face
  stayed in frame during the testing session, only the mount/init step
  and the fix for the false-timeout bug have been verified so far.

## Abandoned Features

Not part of the current default build; kept in the tree (still compile in
most cases) for reference. Full trail in [WORKLOG.md](WORKLOG.md).

| Feature | Why abandoned | Still buildable via |
|---|---|---|
| Live camera image + bounding-box overlay on LCD | Too much data over the bit-bang bus per frame; reverted to 1-line text status | `source/display/bbox_overlay.c/h` (no longer unused — reused by the SD snapshot feature above, just not drawn to the LCD) |
| J8 FlexIO LCD header (hardware-accelerated bus) | Wiring/init confirmed good, but pixel data came out as noise — unresolved bus-speed/signal-integrity issue | `-DLCD_ARDUINO_HEADER_BITBANG=OFF` |
| USB Video Class (UVC) webcam streaming | Genuine hardware conflict, not fixable in software — see [ARCHITECTURE.md §4](ARCHITECTURE.md#4-known-constraints--trade-offs) | `-DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF` (won't drive the LCD) |

## History

Started on the Arduino-header TFT design (per `requirement.md`), briefly
explored the J8 FlexIO header and USB Video Class streaming as
alternatives, then reverted to the original Arduino-header design once
both alternatives hit the issues in the table above. Camera+LCD bring-up
was completed first, then AI integration (a HardFault initially misread as
a CMSIS-NN alignment bug, actually a stack overflow; then a SmartDMA/AI RAM
bank collision — see [ARCHITECTURE.md §3](ARCHITECTURE.md#3-key-design-decisions)),
then the Neutron NPU backend was added as a speed upgrade once the CPU
path was proven correct. The model itself started as a 3-class drowsy-eye
detector and was later swapped for the current single-class `face`
detector without touching camera/LCD/NPU integration code. Full dated
trail: [WORKLOG.md](WORKLOG.md).
</content>
