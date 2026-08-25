# Camera_AI_Test1

Real-time face-detection camera on **FRDM-MCXN947**. Captures frames from an
OV7670 camera via SmartDMA, runs a single-class ("face") Edge Impulse FOMO
model per frame, and shows the result as a text status line on a TFT panel.
On detection, saves a snapshot (with a box drawn around the face) to the
TFT shield's onboard microSD card, rate-limited to 1 photo/sec.

**Status:** working, confirmed on real hardware — last verified `2026-08-25`

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
| Display | `HSD024131-C1`-class TFT, 240x320, 8-bit parallel (ILI9341-family) | → Arduino header (GPIO bit-bang) |
| microSD | TFT shield's onboard slot (SPI mode, FAT-formatted card) | → Arduino D10..D13 (hardware LPSPI1) |
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

### Pinout — TFT (Arduino header, GPIO bit-bang)

| Shield pin | Arduino pin | MCU pin |
|---|---|---|
| LCD_D0..D7 | D8,D9,D2,D3,D4,D5,D6,D7 | P0_28, P0_10, P0_29, P1_23, P0_30, P1_21, P1_2, P0_31 |
| LCD_RS (DC) | A2 | P0_14 |
| LCD_CS | A3 | P0_22 |
| LCD_RST | A4 | P0_15 |
| LCD_RD | A0 → jumper → **D0** | P4_3 |
| LCD_WR | A1 → jumper → **D1** | P4_2 |
| LCD_BLK (backlight) | A5 | P0_23 |
| GND, 5V, 3V3 | board power header | direct |

`A0`/`A1` on FRDM-MCXN947 are analog-only (no GPIO), so `LCD_RD`/`LCD_WR`
need 2 short jumper wires from the shield's A0/A1 pads to board pins D0/D1
(not D10-D13 — those are the shield's onboard SD-card lines, see below).
Every other LCD pin plugs in normally, no rework.

### Pinout — microSD (TFT shield's onboard slot, Arduino D10..D13)

| Shield pin | Arduino pin | MCU pin | Function |
|---|---|---|---|
| SD_SS | D10 | P0_27 | LPSPI1 PCS0 (chip select) |
| SD_DI | D11 | P0_24 | LPSPI1 SDO (MCU → card) |
| SD_DO | D12 | P0_26 | LPSPI1 SDI (card → MCU) |
| SD_CK | D13 | P0_25 | LPSPI1 SCK |

Real hardware SPI (LPSPI1), not bit-banged — per NXP's UM12018, D10..D13 on
this board are wired to `LP_FLEXCOMM1` in SPI mode, not plain GPIO. Plugs
in normally, no rework/jumpers. Card must be pre-formatted FAT16/FAT32.

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
LCD: bit-bang GPIO on the Arduino header
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
| `LCD_CAMERA_PREVIEW` | `OFF` | Skip AI, stream raw camera feed to the LCD as fast as frames arrive — for focusing the lens by eye. |
| `LCD_ARDUINO_HEADER_BITBANG` | `ON` | `ON`: drive TFT via Arduino-header GPIO bit-bang (default). `OFF`: J8 header (FlexIO) — abandoned, see [ARCHITECTURE.md §4](ARCHITECTURE.md#4-known-constraints--trade-offs). |
| `USB_STREAM_DIAGNOSTIC_DISABLE` | `ON` | `OFF` opts into the abandoned UVC webcam streaming path (doesn't drive the LCD) — see [ARCHITECTURE.md §4](ARCHITECTURE.md#4-known-constraints--trade-offs). |

`AI_MODEL_USE_NPU` and `LCD_CAMERA_PREVIEW` are independent of the LCD/USB
flags; combining `LCD_CAMERA_PREVIEW=ON` with
`USB_STREAM_DIAGNOSTIC_DISABLE=OFF` builds but isn't a meaningful
combination.

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
      camera/                <- OV7670 + SmartDMA driver (from NXP reference)
      display/                <- lcd_bitbang.c (default), lcd_flexio_mculcd.c (abandoned J8 path), text_overlay.c
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
        sd_spi_disk.c/h         <- LPSPI1 SD-over-SPI + FatFs diskio glue (see ARCHITECTURE.md §2)
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
- LCD_RD/LCD_WR jumper mapping in the pinout table is this project's own
  best-effort reading — verify against your physical shield if the screen
  is blank/garbled (checklist: power → jumpers → RST → panel controller →
  MADCTL rotation → continuity check with a multimeter).
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
