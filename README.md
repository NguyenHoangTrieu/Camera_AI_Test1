# Camera_AI_Test1

Face-detection camera on **FRDM-MCXN947**, built against the local
`mcuxsdk` checkout (`../mcuxsdk`). Captures frames from an **OV7670** camera
via SmartDMA, runs a trained Edge Impulse FOMO object-detection model
(single class `face`) on every frame, and shows the result on a TFT panel
wired to the board's **Arduino header** (GPIO bit-bang 8080 bus) as a text
status line:

- `FACE: 1` - a face was detected this frame
- `FACE: 0` - no face detected this frame

Inference can run on either the chip's **CPU (CMSIS-NN)** or its **Neutron
NPU**, selected at build time - see "AI model integration" below. On the
earlier 3-class drowsiness model this project shipped before (see
WORKLOG.md), the NPU path measured roughly **370-390x faster** than the CPU
path on real hardware (~3.3ms vs. ~1.27s per inference) - the current
face-only model is architecturally the same integration (same two
interchangeable backends), but hasn't had its own on-hardware NPU timing
re-measured yet (no debug probe was available in the session that converted
and wired it in - see WORKLOG.md's top entry for what's confirmed vs. not).

Source of truth for the original ask: [requirement.md](requirement.md).
[WORKLOG.md](WORKLOG.md) has the full messier bring-up history (bugs found,
dead ends, exact hardware measurements) if you need more detail than this
file - this README stays at "what does it do and how do I run it".
[KNOWLEDGE.md](KNOWLEDGE.md) is a focused background explainer on three
things this project required learning from scratch: the DVP camera protocol
vs. the SmartDMA coprocessor, the Neutron NPU (how model conversion and
init/usage work), and the hardware limitations encountered (the SmartDMA/AI
RAM collision, the USB/DCDC-voltage conflict) - useful if you're new to any
of these and want the "why", not just the how-to.

## Hardware

- FRDM-MCXN947 board
- OV7670 camera module -> **J9 (SmartDMA/Camera header)**
- TFT panel with an 8-bit parallel data bus (`HSD024131-C1` per
  requirement.md, almost certainly ILI9341-family, 240x320) -> **Arduino
  header** (J1-J4), with 2 short jumper wires - see "Pinout" below.
- The MCU-Link debug-probe USB connection (for flashing/serial console -
  `./build.sh flash`/`monitor`).

## Pinout

### Camera (OV7670 -> J9 SmartDMA/Camera header)

| Camera signal | MCU pin | J9 pin # |
|---|---|---|
| SIOC / SIOD (SCCB, I2C-like) | P3_2 / P3_3 (LP_FLEXCOMM7) | 19 / 20 |
| XCLK | P2_2 (CLKOUT) | 16 |
| PCLK | P0_5 | — |
| HREF | P0_11 | — |
| VSYNC | P0_4 | — |
| D0..D7 | P1_4, P1_5, P1_6, P1_7, P3_4, P3_5, P1_10, P1_11 | 7,8,9,10,11,12,13,14 |
| 3V3 / GND | J9 pin 21 / 22 | 21 / 22 |

Board rework note carried over from the NXP reference design: **change SJ16, SJ26,
SJ27 from the right side to the left side** before attaching the camera to J9.

### TFT (Arduino header, GPIO bit-bang)

| Shield pin | Arduino pin | MCU pin |
|---|---|---|
| LCD_D0 | D8 | P0_28 |
| LCD_D1 | D9 | P0_10 |
| LCD_D2 | D2 | P0_29 |
| LCD_D3 | D3 | P1_23 |
| LCD_D4 | D4 | P0_30 |
| LCD_D5 | D5 | P1_21 |
| LCD_D6 | D6 | P1_2 |
| LCD_D7 | D7 | P0_31 |
| LCD_RS (DC) | A2 | P0_14 |
| LCD_CS | A3 | P0_22 |
| LCD_RST | A4 | P0_15 |
| LCD_RD | A0 *(no GPIO)* -> jumper -> **D0** | P4_3 |
| LCD_WR | A1 *(no GPIO)* -> jumper -> **D1** | P4_2 |
| LCD_BLK (backlight, if your panel has a separate enable pin) | A5 | P0_23 |
| GND, 5V, 3V3 | board power header | direct |

**LCD_D0..D7 and LCD_RS/CS/RST plug in normally** - all real GPIO pins, no
rework. **Only LCD_RD and LCD_WR need jumper wires**: they land on the
shield's A0/A1 socket positions, and A0/A1 on FRDM-MCXN947 are analog-only
(no GPIO at all) - so:
- Bend the shield's A0 and A1 pins up/out of the way instead of seating them
  in their native sockets (the rest of that header - RS/CS/RST on A2/A3/A4 -
  plugs in normally).
- Run 2 short jumper wires: shield's **A0 pad (LCD_RD) -> board Arduino D0**,
  and shield's **A1 pad (LCD_WR) -> board Arduino D1**. (Not D10-D13: on this
  shield those are already SD_SS/SD_DI/SD_DO/SD_SCK for its onboard SD slot,
  which this project doesn't use but shouldn't be shorted against either.)

**LCD_BLK** is wired to Arduino A5 as a harmless just-in-case default -
`LCD_Init()` drives it high automatically. If your panel doesn't have a
separate backlight-enable pin (the original shield this was bringing up
worked fine without one), it's fine to leave that wire disconnected.

### If the screen shows nothing / garbled

1. **Power**: GND and 3.3V (or 5V, check your panel) actually connected?
2. **Jumpers**: are LCD_RD/LCD_WR actually reaching Arduino D0/D1 (not just
   plugged into the shield's native A0/A1, which have no GPIO)?
3. **RST held low**: if `LCD_RST` isn't wired/is stuck low, the controller
   never leaves reset. Check continuity to P0_15 (Arduino A4).
4. **Wrong controller**: `LCD_InitPanel()` (`source/display/lcd_bitbang.c`)
   uses a generic MIPI-DCS sequence (works across ILI9481/ILI9486/HX8357/
   ST7796-family panels) - if your panel uses something else entirely, this
   may need adjusting.
5. **Rotated/mirrored image**: the panel is driven in landscape (MADCTL MV
   bit) to match the camera's 320x240 buffer 1:1. If the status-color fill
   comes out sideways, adjust the MADCTL byte in `LCD_InitPanel()`.
6. Double-check each of the 14 signals against the table above with a
   multimeter (continuity mode, panel unpowered) - easy to swap one with 14
   wires on a breadboard.

## Building and flashing

**Verified working** on this machine (arm-none-eabi-gcc, west via the venv at
`../../tools/westenv`, MCU-Link over pyOCD) - builds clean with `-Werror`:

```
$ ./firmware/camera_ai_demo/build.sh          # build, then flash
$ ./firmware/camera_ai_demo/build.sh build    # build only
$ ./firmware/camera_ai_demo/build.sh flash    # flash the last build
$ ./firmware/camera_ai_demo/build.sh monitor  # open the serial console (115200-8-N-1)
```

`build.sh` mirrors `../../touch_rgb/build.sh`: it runs `west build -b frdmmcxn947
firmware/camera_ai_demo --toolchain armgcc -Dcore_id=cm33_core0` from inside the
`../mcuxsdk` west workspace, then flashes the resulting `.elf` over the on-board
MCU-Link (CMSIS-DAP) with pyOCD, auto-selecting the probe by its MCU-LINK unique ID
so it doesn't prompt if another debug probe is also plugged in. See
`../../touch_rgb/README.md` for the probe-permissions / udev-rule note if `pyocd`
needs `sudo` on your setup.

### Build-time flags

Pass any of these to `build`/`rebuild`/`all` as an extra `-D<FLAG>=ON|OFF`,
e.g. `./build.sh rebuild -DAI_MODEL_USE_NPU=OFF`:

| Flag | Default | Effect |
|---|---|---|
| `AI_MODEL_USE_NPU` | `OFF` | Run inference on CPU+CMSIS-NN via the Edge Impulse SDK (the default, as of 2026-08-25 - **neither backend actually fits in RAM for the current alpha=0.35 model**, see WORKLOG.md's top entry; CPU was picked as the default only because it's the smaller shortfall to fix first). Set `ON` for the Neutron NPU instead once its own arena-size issue is resolved. |
| `LCD_CAMERA_PREVIEW` | `OFF` | Skip AI entirely and stream the raw camera feed straight to the LCD as fast as frames arrive (no inference stall in the way) - for physically focusing the lens by eye. Re-flash with this back `OFF` afterwards for actual face detection. |
| `LCD_ARDUINO_HEADER_BITBANG` | `ON` | Drive the TFT via GPIO bit-bang on the Arduino header (current default, requirement.md's original design). `OFF` switches to the J8 header instead - abandoned, see "Abandoned/disabled features" below. |
| `USB_STREAM_DIAGNOSTIC_DISABLE` | `ON` | Skip USB bring-up entirely - camera+AI loop only. `OFF` opts into the abandoned USB Video Class streaming path (see "Abandoned/disabled features"), which doesn't drive the LCD at all. |

`AI_MODEL_USE_NPU` and `LCD_CAMERA_PREVIEW` are independent of each other and
of the two LCD/USB flags above - any combination builds, though combining
`LCD_CAMERA_PREVIEW=ON` with `USB_STREAM_DIAGNOSTIC_DISABLE=OFF` isn't a
meaningful combination (camera preview doesn't touch USB at all).

**Build-verified only** (both `AI_MODEL_USE_NPU` on and off build clean and
link - see WORKLOG.md's top entry for the exact `arm-none-eabi-g++`/`ld`
output). **Not yet confirmed on real hardware** - no debug probe was
attached in the session that wired this model in, so the example log below
is what the earlier 3-class model printed, kept only to show the log
*format* (line names/shape), not real numbers for this model:
```
Camera_AI_Test1 - FRDM-MCXN947
Camera: OV7670 on J9 SmartDMA/Camera header
Display: Arduino-header LCD status text (camera + AI hook)

Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
AI_MODEL_Init: Neutron NPU face detector ready (96x96 input, 1 class(es), arena used <N>/188416 bytes)
LCD: bit-bang GPIO on the Arduino header
AI_MODEL_RunInference: total classifier time = <N>us (<N>ms)
AI result: box[0] label=face x=<N> y=<N> w=<N> h=<N> score=<N>%
```
(build with `-DAI_MODEL_USE_NPU=OFF` for the CPU/CMSIS-NN path instead -
same log shape, different `AI_MODEL_Init`/timing lines - see
`source/ai/model_runner.cpp`. LCD shows one text line - `FACE: 1` or
`FACE: 0` depending on whether a face was detected this frame - not a live
image, see `DEMO_DrawStatusLine()` in `source/main.c`.)

## AI model integration

Model: Edge Impulse Studio project **"Face_Detection_NXP"** (project ID
1095726) - a FOMO (Faster Objects, More Objects) detector, 96x96
int8-quantized input, single class `face`, trained on WIDER FACE + DarkFace
(see WORKLOG.md for dataset/training details and the license caveat - both
source datasets are CC-BY-NC-ND/research-only, not cleared for a commercial
product as-is). `main.c` runs `AI_MODEL_RunInference()` once per captured
camera frame and drives the LCD's 1-line text status readout from the
result - see the file header comments in `source/main.c` and
`source/ai/model_runner.h` for the exact API.

Two interchangeable inference backends implement the identical
`model_runner.h` API, selected at build time:

| | Neutron NPU (default) | CPU + CMSIS-NN |
|---|---|---|
| CMake flag | (default) | `-DAI_MODEL_USE_NPU=OFF` |
| Source | `source/ai/model_runner_npu.cpp` | `source/ai/model_runner.cpp` |
| Engine | Raw TFLite Micro against NXP's `middleware/eiq/tensorflow-lite`, model's conv/pool ops compiled to run on the Neutron NPU coprocessor | Edge Impulse's own `ei_run_classifier()`, non-EON TFLite Micro interpreter |
| Tensor arena | 184KB static (`m_data`) | ~185KB (96KB `m_sramx` primary pool + 96KB `m_data` overflow, see `source/ai/ei_sramx_alloc.c`) |
| On-hardware timing | not yet re-measured for this model | not yet re-measured for this model |

Both print the same `AI_MODEL_RunInference: total classifier time = ...`
line (timed via the Cortex-M33's DWT cycle counter - Edge Impulse's own
`ei_result.timing` reads all-zero on this SDK/porting combination), so
switching the flag and rebuilding gives a direct apples-to-apples
comparison once measured on hardware.

This model's `m_data` footprint (camera frame buffer + NPU arena, or camera
frame buffer + CPU-path overflow pool) no longer fits inside the SDK's
stock `m_data` region (312KB) - `board_port/m_data_ext.ld` widens it by
reclaiming the 104KB the SDK's board linker script reserves for this dual-
core chip's second Cortex-M33 (core1), which this single-core project never
boots. See that file's header comment for the full reasoning and the
"not yet confirmed on real hardware" caveat.

The NPU model (`source/ai/neutron/tflite_learn_1095726_3_npu.tflite`/`.h`)
was produced by running the same `.tflite` Edge Impulse Studio already
exports (`source/ai/edge_impulse/tflite-model/tflite_learn_1095726_3.tflite`)
through NXP's `neutron_converter` CLI tool (`eiq_neutron_sdk` package,
installed from NXP's own package index, target `mcxn94x`) - 31 of the
model's 33 operators got folded into a single NPU-accelerated op (the other
2 are `Slice` + `Softmax`, kept as regular TFLM ops - one more op than the
earlier 3-class model needed, since this model's NPU output channel count
comes back padded and needs slicing down to the real 2 channels). See
[WORKLOG.md](WORKLOG.md) for the exact commands and the full story,
including the hand-rolled FOMO grid-decode postprocessing the NPU path
needs (it bypasses Edge Impulse's own postprocessing entirely).

To point either backend at a different/retrained model: re-export from Edge
Impulse Studio (a new "C++ library" for the CPU path, replacing
`source/ai/edge_impulse/`; a new plain `.tflite` run back through
`neutron_converter` for the NPU path) and update `model_runner.cpp`/
`model_runner_npu.cpp` if the input/output tensor shapes or class labels
changed.

## Project layout

Same out-of-tree layout as the sibling projects in `NPX_Workspace` (`touch_rgb`,
`i2s_sniffer`, `wifi_sensing_npu`): `board_port/` holds only pin muxing + app.h;
`board.c`/`clock_config.c` are **not** duplicated here - the SDK supplies them
automatically for `board=frdmmcxn947`.

```
Camera_AI_Test1/
  README.md                  <- this file
  WORKLOG.md                 <- full bring-up history (messier, chronological)
  requirement.md, image*.png <- original request
  firmware/camera_ai_demo/
    CMakeLists.txt, prj.conf, build.sh
    board_port/
      pin_mux.c/h                    <- camera (J9) + LCD (Arduino/J8, both) pin routing
      ei_sramx.ld                    <- linker fragment placing the CPU-path tensor arena's primary pool in m_sramx
      m_data_ext.ld                  <- linker fragment widening m_data by reclaiming core1's unused RAM
      cm33_core0/
        app.h                         <- BOARD_InitHardware() proto, shared pin/geometry macros
        hardware_init.c                <- BOARD_InitHardware()
        prj.conf                       <- board-port Kconfig (inputmux, pinmux_project_folder)
    source/
      main.c                         <- capture -> AI inference -> LCD status-text loop (default)
      fault_handler.c                <- HardFault dump handler (register decode over PRINTF)
      camera/camera_capture.c/h      <- OV7670 + SmartDMA (from NXP reference)
      display/lcd_bitbang.c/h        <- GPIO bit-bang LCD driver (current default - Arduino header)
      display/text_overlay.c/h, font5x7.h <- 3-line status text renderer (see main.c)
      ai/model_runner.h              <- shared inference API both backends implement
      ai/model_runner.cpp            <- CPU + CMSIS-NN backend (-DAI_MODEL_USE_NPU=OFF), via Edge Impulse's SDK
      ai/model_runner_npu.cpp        <- Neutron NPU backend (default), raw TFLite Micro
      ai/ei_sramx_alloc.c/h          <- custom allocator serving the CPU path's tensor arena from m_sramx
      ai/ei_debug_porting.c          <- routes Edge Impulse's ei_printf through this project's PRINTF
      ai/edge_impulse/               <- Edge Impulse "C++ library" export (Face_Detection_NXP project)
      ai/neutron/                    <- NPU-converted model (tflite_learn_..._npu.tflite/.h)
```

## Known limitations

- **LCD_RD/LCD_WR jumper-wiring assumption**: the exact per-pin order (which
  of RD/WR/RS/CS/RST maps to which Arduino pin) is this project's own
  best-effort reading, re-derived across several sessions - see "If the
  screen shows nothing/garbled" above if it doesn't match your physical
  shield.
- Bit-bang GPIO is inherently slow for pushing pixels, but that's no longer
  the frame-rate bottleneck - the LCD only ever draws a one-line text status
  readout, not a live image, and inference time dominates (camera capture is
  stopped/restarted around each inference call - see WORKLOG.md "Bug #3").
  Actual per-inference timing for this face model hasn't been re-measured on
  hardware yet (see "AI model integration" above).
- Detection results (label/score/box) haven't been validated on real
  hardware yet for this model - the face-detection dataset/training results
  documented in WORKLOG.md are Studio-side (validation/test-set metrics
  only), and the firmware integration (this session) was build-verified but
  not flashed/run.
- **The LCD only shows a text status line, not the live camera image or
  bounding boxes** - by design, to save RAM. See "Abandoned/disabled
  features" below if you want to revisit this.

## Abandoned / disabled features

These are not part of the current default build, kept in the tree (still
compile, in most cases) for reference. See [WORKLOG.md](WORKLOG.md) for the
full trail on any of them.

- **Live camera image + bounding-box overlay on the LCD**: the original
  design drew the live camera frame with colored boxes around detections.
  Tried again later and reverted in favor of the current 3-line text
  status readout (much less data to push over the bit-bang LCD bus per
  frame) - `source/display/bbox_overlay.c/h` (box-drawing helper) is still
  in the tree, unused, in case box-on-image display is revisited. For just
  seeing the raw camera feed (e.g. to focus the lens), see
  `LCD_CAMERA_PREVIEW` in "Build-time flags" above instead - no boxes, no
  AI, just the live image.
- **J8 FlexIO/LCD header** (hardware-accelerated LCD bus): wiring/panel/init
  sequence were all confirmed good (a bit-bang diagnostic on the same J8
  pins displayed a correct image), but the FlexIO hardware-bus path itself
  never got past "responds to commands but pixel data comes out as
  black/white noise" - a bus-speed/signal-integrity issue that wasn't fully
  resolved. Still builds: `./build.sh rebuild -DLCD_ARDUINO_HEADER_BITBANG=OFF`
  (or add `-DLCD_BITBANG_DIAGNOSTIC=ON` for the J8 bit-bang diagnostic
  instead of FlexIO). Source: `source/display/lcd_flexio_mculcd.c/h`.
- **USB Video Class (UVC) webcam streaming**: a genuine hardware/board
  limitation, not fixable in software - SmartDMA camera capture only runs
  reliably with the chip's DCDC regulator at Mid voltage, while the USB HS
  PHY only locks its PLL at Overdrive voltage, and this board's single USB
  connector is hard-wired to the HS controller only (no accessible USB FS
  alternative - confirmed in NXP's UM12018 board user manual, section 2.3).
  A software time-multiplex workaround (periodic Mid/Overdrive switching)
  was built and confirmed stable on real hardware, low-frame-rate but
  genuinely live. Still builds:
  `./build.sh rebuild -DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF` - enumerates as a
  standard UVC device, "Uncompressed YUY2", 320x240, 30fps, over USB
  High-Speed. Doesn't drive the LCD at all (mutually exclusive DCDC level).
  Source: `source/usb/usb_video_camera.c/h`, `usb_device_descriptor.c/h`.

## History

This project originally targeted a TFT shield plugged into the board's
**Arduino header** (bit-banged GPIO 8080 bus) - the current design, and also
the original one, per the literal wording of `requirement.md`. In between, it
was switched to the **J8 FlexIO** header (NXP's officially-supported,
hardware-accelerated path) for reliability/speed, and separately explored
**USB Video Class streaming** as an alternative to a physical display
entirely - both abandoned (see above) and the project reverted to its
original Arduino-header design.

Camera and LCD bring-up were completed first (live image on screen, no AI
yet). AI integration came next: wiring in the Edge Impulse FOMO model hit a
HardFault that took most of a session to properly diagnose (turned out to be
a stack overflow, initially misread as a CMSIS-NN pointer-alignment issue)
and a second bug where the AI tensor arena's memory bank collided with the
SmartDMA camera coprocessor's own working RAM. Once both were fixed, the
full pipeline (capture -> resize -> CPU inference -> LCD status color) was
confirmed running repeatedly on real hardware without faults. The Neutron
NPU backend was added after that as a speed upgrade, once the CPU path was
proven correct - see [WORKLOG.md](WORKLOG.md) for the complete, much more
detailed trail of everything above.

The model itself started as a 3-class drowsy-eye detector
(`closed_eye`/`open_eye`/`yawning`) and was later swapped for the current,
lighter single-class `face` detector - same firmware architecture/bugs/
fixes throughout (this swap didn't need to touch the camera/LCD/NPU
integration code at all, only the model files and the small pieces of
`main.c` that turned per-class detection flags into LCD text), see
WORKLOG.md's top entry for that transition.
