# Camera_AI_Test1

Camera + AI + TFT display project for **FRDM-MCXN947**, built against the local
`mcuxsdk` checkout (`../mcuxsdk`). Captures frames from an **OV7670** camera via
SmartDMA, feeds them into a placeholder AI-model hook, and draws them to an
8-bit-parallel TFT on the board's **J8 FlexIO/LCD header** (hardware-
accelerated, the same connector/driver stack NXP's own reference example uses,
narrowed to 8 data bits to match the panel in hand).

> **LCD bring-up is currently blocked (white backlight, no image) - see
> [WORKLOG.md](WORKLOG.md) for the full debugging history, what's been ruled
> out, and what to try next before starting over.** Camera is fully working
> and unaffected by this.

Source of truth: [requirement.md](requirement.md). Camera wiring/pinout is
confirmed against the mcuxsdk pin tables (not just a photo). LCD wiring below
matches NXP's own `display_examples/smartdma_camera_flexio_mculcd` example's
pin assignments exactly (J8 is the officially-supported way to drive a parallel
display on this board), except the bus width is compiled as 8-bit instead of
that example's 16-bit, since the panel in hand only has `LCD_D0..D7` broken out
- see "History" at the bottom for how this project got here (it started on the
board's Arduino header with a different shield, then switched to J8 for
reliability/speed).

## Hardware

- FRDM-MCXN947 board
- OV7670 camera module -> **J9 (SmartDMA/Camera header)**
- TFT panel (`HSD024131-C1` per requirement.md) with an **8-bit** parallel data
  bus (`LCD_D0..D7` only, not the full 16-bit bus J8 supports) ->
  **J8 (FlexIO/LCD header)**. **Not actually ST7796S** despite this file's
  code comments still assuming it in a few places - see WORKLOG.md, this is
  almost certainly an ILI9341-family 240x320 panel (a common "2.4" TFT LCD
  shield for Arduino Uno" module), which is why `lcd_flexio_mculcd.c` talks
  to it with a generic MIPI-DCS sequence instead of the SDK's `ST7796S_*`
  driver.

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

### TFT (8-bit panel -> J8 FlexIO/LCD header)

Straight from `pin_mux.c` in NXP's `smartdma_camera_flexio_mculcd` board port
(not a guess), narrowed to only the 8 data pins this panel uses - **LCD_D8..D15
(P4_16..P4_23) are not wired/configured, this panel doesn't have them**:

| LCD signal | MCU pin | FlexIO signal |
|---|---|---|
| LCD_RD | P0_8 | FLEXIO0_D0 |
| LCD_WR | P0_9 | FLEXIO0_D1 |
| LCD_CS | P0_12 | plain GPIO |
| LCD_RS (DC) | P0_7 | plain GPIO |
| LCD_RST | P4_7 | plain GPIO |
| LCD_BLK (backlight enable) | P4_6 | plain GPIO |
| LCD_D0 | P2_8 | FLEXIO0_D16 |
| LCD_D1 | P2_9 | FLEXIO0_D17 |
| LCD_D2 | P2_10 | FLEXIO0_D18 |
| LCD_D3 | P2_11 | FLEXIO0_D19 |
| LCD_D4 | P4_12 | FLEXIO0_D20 |
| LCD_D5 | P4_13 | FLEXIO0_D21 |
| LCD_D6 | P4_14 | FLEXIO0_D22 |
| LCD_D7 | P4_15 | FLEXIO0_D23 |
| GND, 3.3V | — | power |

**LCD_BLK matters a lot: without it, the screen can stay completely dark even
though the bus/commands are all working correctly** (first bring-up hit exactly
this - screen showed nothing at all, fixed by wiring/driving this pin). NXP's
own reference example doesn't handle it because the official LCD-PAR-S035 panel
has an always-on backlight, but most generic bare modules need this pin driven
high (`LCD_Init()` in `lcd_flexio_mculcd.c` does this automatically now). If
your module's backlight pin is instead labeled `LED`, `BL`, `LED-A`, or similar,
wire it to P4_6; if it's a always-on/no-control-needed module, it's fine to
leave that wire disconnected (driving an unconnected GPIO high is harmless).

Match each of these 14 signals by name to the panel module's own labeled pins
(most bare "MCU 8-bit parallel TFT" modules label their pins `LCD_D0`..`LCD_D7`,
`RD`, `WR`, `CS`, `RS`/`DC`, `RST` directly - no ambiguity like the earlier
Arduino-shield attempt had). To find the matching physical pin **on J8 itself**,
check the board's own silkscreen next to the J8 connector first (this board's
other headers all have printed signal labels) before trusting any external
pin-number diagram.

The firmware sets `FLEXIO_MCULCD_DATA_BUS_WIDTH=8` (see `CMakeLists.txt`) to match
- this is a compile-time setting read by the SDK's own FlexIO MCULCD driver, so
it can't be changed at runtime; if you swap to a 16-bit panel later, that macro
and the pin table above both need updating together (the removed `LCD_D8..D15`
rows in an earlier revision of this README show the full 16-bit table).

## If the screen still shows nothing

In rough order of likelihood, having already fixed the backlight above:

1. **Power**: is the panel's GND and 3.3V (or 5V, if the module needs it - check
   its datasheet/silkscreen) actually connected? A panel with no power looks
   identical to one with no backlight - completely dark.
2. **RST held low**: if `LCD_RST` isn't wired, or is wired but stuck low, the
   controller stays in reset forever and never responds to anything. Check
   continuity from the panel's RST pin to P4_7.
3. **Wrong controller**: this code assumes ST7796S. If your module actually uses
   a different chip (ILI9341, ILI9486, ST7789, etc - check the panel's part
   number/datasheet), the ST7796S init command sequence may not correctly wake up
   a different controller. See "If your panel isn't ST7796S" below.
4. **CS/RS swapped, or a data bit swapped**: double check each of the 14 wires
   against the table above one at a time with a multimeter (continuity mode),
   panel unpowered.
5. **Loose jumper wire**: with 14 wires this is a lot of connections for
   breadboard/dupont wires - reseat each one and make sure none are making
   only intermittent contact.

## If your panel isn't ST7796S

`LCD_InitPanel()` in `source/display/lcd_flexio_mculcd.c` calls `ST7796S_Init()`
with NXP's LCD-PAR-S035 preset. If your module uses a different controller (e.g.
ILI9486), that call needs to change - this SDK ships driver components for a
handful of panels under `mcuxsdk/mcuxsdk/components/display/`; check there for a
matching one, or adapt the `dbi_xfer_ops_t` pattern already in that file to a
different init command sequence.

## Project layout

Same out-of-tree layout as the sibling projects in `NPX_Workspace` (`touch_rgb`,
`i2s_sniffer`, `wifi_sensing_npu`): `board_port/` holds only pin muxing + app.h;
`board.c`/`clock_config.c` are **not** duplicated here - the SDK supplies them
automatically for `board=frdmmcxn947`.

```
Camera_AI_Test1/
  README.md                  <- this file
  requirement.md, image*.png <- original request
  firmware/camera_ai_demo/
    CMakeLists.txt, prj.conf, build.sh
    board_port/
      pin_mux.c/h                    <- camera (J9) + LCD (J8 FlexIO) pin routing
      cm33_core0/
        app.h                         <- BOARD_InitHardware() proto, shared pin/geometry macros
        hardware_init.c                <- BOARD_InitHardware()
        prj.conf                       <- board-port Kconfig (inputmux, pinmux_project_folder)
    source/
      main.c                         <- capture -> AI hook -> display loop
      camera/camera_capture.c/h      <- OV7670 + SmartDMA (from NXP reference)
      display/lcd_flexio_mculcd.c/h  <- FlexIO + ST7796S driver (from NXP reference)
      ai/model_runner.c/h            <- AI integration stub (see below)
      ai/model_data.h                <- placeholder for an exported model
```

## Building and flashing

**Verified working** on this machine (arm-none-eabi-gcc, west via the venv at
`../../tools/westenv`, MCU-Link over pyOCD) - builds clean with `-Werror`:

```
$ ./firmware/camera_ai_demo/build.sh          # build, then flash
$ ./firmware/camera_ai_demo/build.sh build    # build only
$ ./firmware/camera_ai_demo/build.sh flash    # flash the last build
$ ./firmware/camera_ai_demo/build.sh monitor  # open the serial console (115200-8-N-1)
```

There's also a diagnostic build variant that bypasses the FlexIO peripheral
and bit-bangs the same J8 pins via plain GPIO instead, to help isolate LCD
bring-up issues (see WORKLOG.md):

```
$ ./firmware/camera_ai_demo/build.sh rebuild -DLCD_BITBANG_DIAGNOSTIC=ON
```

`build.sh` mirrors `../../touch_rgb/build.sh`: it runs `west build -b frdmmcxn947
firmware/camera_ai_demo --toolchain armgcc -Dcore_id=cm33_core0` from inside the
`../mcuxsdk` west workspace, then flashes the resulting `.elf` over the on-board
MCU-Link (CMSIS-DAP) with pyOCD, auto-selecting the probe by its MCU-LINK unique ID
so it doesn't prompt if another debug probe is also plugged in. See
`../../touch_rgb/README.md` for the probe-permissions / udev-rule note if `pyocd`
needs `sudo` on your setup.

**Camera confirmed working end-to-end** (from the earlier Arduino-header revision
of this project, same camera code, unchanged by the J8 switch): the OV7670 driver
reads back its PID/VER registers over J9's SCCB/I2C and matches a genuine OV7670
(`PID=0x76 VER=0x73`), and SmartDMA frame-ready interrupts fire with a periodic
diagnostic log (`CAMERA_CAPTURE_LogFrameSignature()` in `main.c`) confirming pixel
data is non-flat and changing frame to frame:
```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
Camera: frame #16 ready, 792 samples, pixel range 0xC0C6..0xFBEB, avg=0xE330
Camera: frame #46 ready, 792 samples, pixel range 0xC0C4..0xFDF1, avg=0xDE26
```

**LCD side (J8 FlexIO) is NOT yet visually verified** - this code compiles and
links cleanly and `LCD_Init()` runs without hanging, but nobody has confirmed a
picture actually appears on a physical J8-connected panel yet. Check the screen
after wiring your panel and flashing.

## AI model integration

`source/ai/model_runner.c` is currently a stub: `AI_MODEL_RunInference()` always
returns `result->valid = false`. It's the extension point requested in
`requirement.md`. Two ready-made paths, both documented in detail in the comment at
the top of that file:

- **TensorFlow Lite Micro** - reuse the pattern in this SDK's
  `examples/eiq_examples/tflm_label_image` (interpreter setup, tensor arena,
  `Invoke()`).
- **Edge Impulse** - drop in a "C++ library" export from an Edge Impulse project and
  call `run_classifier()`.

`main.c` and `model_runner.h`'s C API are written so that plugging in either one
only touches `model_runner.c`/`.cpp` and `CMakeLists.txt`.

## Known limitations

- The camera buffer (320x240) is drawn into the panel's top-left corner as-is, not
  scaled/centered to fill the full 480x320 panel. To change that, either add
  scaling in `main.c`, or switch `DEMO_SMARTDMA_API`/`DEMO_BUFFER_WIDTH/HEIGHT` in
  `app.h` to `kSMARTDMA_CameraWholeFrame480_320` to capture at the panel's native
  size instead (see `drivers/smartdma/mcxn/fsl_smartdma_fw.h` for the full list of
  SmartDMA capture modes).
- `LCD_InitPanel()` assumes an ST7796S controller with NXP's LCD-PAR-S035 preset -
  see "If your panel isn't ST7796S" above if that doesn't match your hardware.
- `source/ai/model_runner.c` has no real model wired in yet by design.

## History

This project originally targeted a TFT shield plugged into the board's **Arduino
header** (bit-banged GPIO 8080 bus), per the literal wording of `requirement.md`.
That path was fully brought up and visually confirmed working (camera image
displayed, correctly oriented, after fixing several wiring/orientation issues), but
had two accepted downsides: needing hand-soldered jumper wires to work around 2
analog-only pins on the Arduino header, and visible screen tearing from the slow
GPIO bit-bang draw racing the camera's SmartDMA writes. The project was then
switched to the **J8 FlexIO** header instead - NXP's own officially-supported,
hardware-accelerated path for driving a parallel LCD on this board - trading the
"exact literal shield from the photo" requirement for reliability and speed. The
Arduino-header bit-bang driver (`lcd_arduino_8080.c`) was removed; if you want to
go back to it, it's straightforward to recreate from this file's git history (or
ask for it to be regenerated - the pin derivation and wiring are documented in
this file's own version history).
