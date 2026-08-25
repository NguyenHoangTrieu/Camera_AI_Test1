# Camera_AI_Test1

Real-time face-detection camera on **FRDM-MCXN947**. Captures frames from an
OV7670 camera via SmartDMA, runs a single-class ("face") Edge Impulse FOMO
model per frame, and shows the result as a text status line on a TFT panel.

**Status:** working, confirmed on real hardware — last verified `2026-08-25`

## Overview

Pipeline: `Camera (OV7670/SmartDMA)` → `AI inference (CPU or Neutron NPU)` → `LCD status text`.

Inference runs on either the CPU (CMSIS-NN) or the Neutron NPU (default,
~4ms/inference on real hardware). See [ARCHITECTURE.md](ARCHITECTURE.md)
for design decisions, memory-conflict pitfalls, and NPU integration
details, and [WORKLOG.md](WORKLOG.md) for the full dated bring-up history.
Original request: [requirement.md](requirement.md).

## Hardware

| Component | Model / Part | Notes |
|---|---|---|
| Board | FRDM-MCXN947 | |
| Camera | OV7670 | → **J9** (SmartDMA/Camera header) |
| Display | `HSD024131-C1`-class TFT, 240x320, 8-bit parallel (ILI9341-family) | → Arduino header (GPIO bit-bang) |
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
(not D10-D13 — those are the shield's onboard SD-card lines). Every other
LCD pin plugs in normally, no rework.

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

LCD shows one text line — `FACE: 1` or `FACE: 0` — not a live image.

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
```

## Known Limitations

- LCD shows a one-line text status only, not a live image or bounding
  boxes (by design, to save RAM — see "Abandoned Features" below).
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

## Abandoned Features

Not part of the current default build; kept in the tree (still compile in
most cases) for reference. Full trail in [WORKLOG.md](WORKLOG.md).

| Feature | Why abandoned | Still buildable via |
|---|---|---|
| Live camera image + bounding-box overlay on LCD | Too much data over the bit-bang bus per frame; reverted to 1-line text status | `source/display/bbox_overlay.c/h` (unused, kept for reference) |
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
