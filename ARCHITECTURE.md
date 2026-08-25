# Architecture

Design decisions and internal mechanics for this project. For build/run
instructions see [README.md](README.md); for the dated bring-up history
see [WORKLOG.md](WORKLOG.md).

## 1. System Overview

```
[OV7670 camera] --DVP(PCLK/HREF/VSYNC)--> [SmartDMA coprocessor] --frame buffer-->
  --> [AI inference: CPU/CMSIS-NN or Neutron NPU] --> [LCD status text, bit-bang GPIO]
```

Camera capture and AI inference run **sequentially, not in parallel** (see
§3) on core0 only — this is a single-core application despite the MCXN947
being dual-core (core1 never boots, see §4). Two coprocessors do the heavy
lifting off the main Cortex-M33: SmartDMA assembles camera frames without
CPU involvement, and the Neutron NPU (default backend) runs the
model's convolution-heavy layers without CPU involvement.

## 2. Components

### Camera capture — `source/camera/camera_capture.c`

- **Responsibility:** drive the OV7670 over SCCB (config) and DVP
  (PCLK/HREF/VSYNC/D0-D7, pixel data), using SmartDMA to assemble frames.
- **Why SmartDMA, not a software loop:** DVP is a parallel interface with
  camera-driven timing (not synchronized to the CPU); at 320x240@30fps
  (~2.3 MB/s) a software GPIO-polling loop can't keep up. SmartDMA is a
  separate programmable coprocessor that runs its own microcode
  understanding DVP timing directly, assembling frames in parallel with
  whatever the main core is doing.
- **Key APIs:**
  ```c
  SMARTDMA_InstallFirmware(SMARTDMA_CAMERA_MEM_ADDR, s_smartdmaCameraFirmware, ...);
  SMARTDMA_InstallCallback(CAMERA_CAPTURE_CompleteCallback, NULL);
  NVIC_EnableIRQ(SMARTDMA_IRQn);
  SMARTDMA_Boot(kSMARTDMA_CameraWholeFrameQVGA, &smartdmaParam, 0x2);
  ```
  Completion fires `SMARTDMA_IRQn` → `CAMERA_CAPTURE_CompleteCallback()` →
  sets a flag the main loop polls. Main CPU never touches pixel-level
  timing.
- **Gotcha:** the first frame captured immediately after
  `CAMERA_CAPTURE_Reinit()` is not yet synced and must be discarded (see
  WORKLOG.md for the bug this caused: every frame reading back flat/zero
  in the AI-integrated build).

### AI inference — `source/ai/model_runner.h` (+ two backends)

- **Responsibility:** run the FOMO face-detection model on a captured
  frame; identical API (`model_runner.h`) for both backends, selected at
  build time via `AI_MODEL_USE_NPU`.
- **CPU backend** (`model_runner.cpp`): Edge Impulse's own
  `ei_run_classifier()`, non-EON TFLite Micro interpreter.
- **NPU backend** (`model_runner_npu.cpp`): bypasses Edge Impulse's SDK
  entirely (its `ei_run_classifier()` has no hook for registering a custom
  op) and talks to TensorFlow Lite Micro directly:
  ```cpp
  static tflite::MicroMutableOpResolver<3> s_opResolver;
  s_opResolver.AddSlice();
  s_opResolver.AddSoftmax();
  s_opResolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());
  tflite::MicroInterpreter interpreter(model, s_opResolver, tensorArena, arenaSize);
  interpreter.AllocateTensors();
  interpreter.Invoke();  // NEUTRON_GRAPH portion runs on the NPU
  ```
  `neutronInit()` (real hardware init) is called lazily, internally, by the
  `NEUTRON_GRAPH` kernel on its first `Prepare()` — no manual NPU-start
  call needed anywhere in application code.
- **Model conversion for NPU:** the `.tflite` Edge Impulse Studio exports
  must be run through NXP's `neutron_converter` CLI (`eiq_neutron_sdk`
  package) before use:
  ```bash
  neutron_converter --input model.tflite --target mcxn94x --output model_npu.tflite
  ```
  This collapses supported layers (conv/pool/add/...) into one custom
  `NEUTRON_GRAPH` op; unsupported layers (`Slice`, `Softmax` for this
  model) stay as regular TFLM ops. Current model: 31/33 ops folded into
  the NPU op.
- **Timing:** measured by hand via the Cortex-M33 DWT cycle counter
  (`DWT->CYCCNT` → microseconds via `SystemCoreClock`), not Edge Impulse's
  own `ei_result.timing` field — that field reads `0` on this
  SDK/porting combination (`ei_read_timer_us()` is hard-coded to return 0
  in the bare-metal clib porting layer).
- **Gotcha:** quantization for this model reduces to `pixel_value - 128`
  (scale ≈ 1/255, zero_point = -128) — no runtime division/multiplication
  needed, convenient on an MCU without a FPU-heavy quantization path.

### Display — `source/display/lcd_bitbang.c` (default) / `lcd_flexio_mculcd.c` (abandoned)

- **Responsibility:** draw a 1-line text status (`FACE: 1`/`FACE: 0`) via
  GPIO bit-bang 8080 bus on the Arduino header.
- **Key APIs:** `LCD_Init()`, `LCD_InitPanel()` (generic MIPI-DCS init
  sequence, works across ILI9481/ILI9486/HX8357/ST7796-family panels),
  `DEMO_DrawStatusLine()` in `main.c`.

## 3. Key Design Decisions

| Decision | Alternative(s) considered | Why this way |
|---|---|---|
| Camera capture and AI inference run sequentially (`CAMERA_CAPTURE_Deinit()` before inference, `_Reinit()` after) | Run in parallel, both using `m_sramx` | Both need the same 96KB `m_sramx` RAM bank at the same time — parallel use silently corrupts SmartDMA's working state (camera stops firing frame-ready interrupts, no error logged). Sequential is slightly slower but inference already dominated total pipeline time, so no real throughput cost. |
| CPU-path tensor arena split across `m_sramx` (primary) + `m_data` (overflow) | Single pool in `m_data` only | Model doesn't fit in `m_data` alone; `m_sramx` looks unused in the linker map (SmartDMA firmware is loaded into it at runtime via a function call, not the static linker) but is only safe to reuse because capture is fully stopped first — see row above. |
| LCD shows text status only, not live image + bounding boxes | Live image + box overlay (the original design) | Bit-bang GPIO bus is too slow to push a full frame plus AI overlay per inference cycle at acceptable rate; text status is enough for the drowsiness/face-presence use case. `bbox_overlay.c/h` kept in tree, unused, if revisited. |
| TFT driven via Arduino-header bit-bang, not J8 FlexIO | J8 FlexIO (NXP's hardware-accelerated path) | FlexIO wiring/init confirmed correct (a bit-bang diagnostic on the same J8 pins worked), but the FlexIO hardware-bus path itself produced black/white noise pixel data — unresolved bus-speed/signal-integrity issue. Bit-bang is slower but correct. |
| No USB video streaming | UVC webcam streaming, or a Mid/Overdrive DCDC time-multiplex workaround | Real hardware constraint, not fixable in software — see §4. A working time-multiplex prototype existed but added complexity not justified since the product only needs the LCD status line. |
| core1 never boots; its reserved RAM region is left untouched | Reuse core1's reserved RAM bank for the tensor arena, same reasoning as the `m_sramx` reuse above | Different failure mode from `m_sramx` — see §4. Tried once, wedged the board (silent hang on the very first `.bss` zero-init access), reverted. |

## 4. Known Constraints & Trade-offs

### SmartDMA / AI tensor-arena RAM collision

- **Constraint:** the CPU-path tensor arena and SmartDMA's camera firmware
  cannot occupy `m_sramx` (96KB) at the same time.
- **Root cause:** `SMARTDMA_CAMERA_MEM_ADDR` (`0x04000000`) is the start of
  `m_sramx`; the SDK's board linker script has no output section for it
  because SmartDMA firmware is loaded at runtime, not link time — making
  the bank look free when it isn't.
- **Impact:** capture must be stopped (`CAMERA_CAPTURE_Deinit()`) before
  inference and restarted after — see §3, row 1.

### core1's reserved RAM bank is unsafe to reuse, for a different reason than `m_sramx`

- **Constraint:** cannot reuse the SDK-reserved core1 RAM region
  (`0x2004E000`-`0x20068000`, 104KB) even though core1 never boots in this
  project.
- **Root cause:** on MCX-family (and most NXP multicore) parts, SRAM
  instances are partitioned into separate **power domains**. A domain tied
  to a core that's never released from reset can be left in its default
  low-power/array-off state — accessing it is a bus fault against
  unpowered RAM, not a data-corruption problem like `m_sramx`. Nothing in
  this project's boot code powers up that domain, because nothing here
  ever asks for core1's resources.
- **Impact:** the moment `Reset_Handler`'s `.bss` zero-init loop touched a
  symbol placed in that region, the board produced zero UART output ever
  again — not even the pre-camera/AI boot banner. Confirming actual
  electrical status would require the reference manual's SPC/CMC
  power-domain register detail, not just the linker script. **"Reserved
  for another core that never boots" is a weaker safety argument than
  "reserved for a coprocessor whose activity this firmware controls
  directly" (i.e. the `m_sramx` case above) — don't reuse a region just
  because the current build never references it.**

### USB High-Speed streaming vs. camera capture

- **Constraint:** the camera and USB Video Class streaming cannot run at
  the same time on this board.
- **Root cause:** the MCXN947's DCDC core regulator needs Mid voltage
  (~1.0V) for reliable SmartDMA camera capture, but the USB HS PHY's PLL
  needs Overdrive voltage (~1.2V) to lock — mutually exclusive voltage
  requirements. The board's single USB-C connector is hard-wired to the HS
  controller only (no accessible USB FS alternative — UM12018 §2.3), so
  there's no wiring-level workaround either.
  - **Genuine hardware limitation, not fixable in software** — timing/config
    issues can be fixed in code; a PLL that physically cannot lock below a
    given voltage cannot.
- **Impact:** a working time-multiplexed prototype (periodic Mid ↔
  Overdrive switching) produced low-frame-rate, choppy UVC video; set
  aside since the product doesn't need a USB video feed. Source kept at
  `source/usb/usb_video_camera.c/h` (`-DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF`).

## 5. Debugging & Tooling Notes

### Plain `pyocd flash`/`reset`/`erase` is unreliable on this chip/probe/pyOCD combination

- **Symptom:** `pyocd flash -t mcxn947 <elf>` fails at the
  connection/init stage (never reaches the programming progress bar) with
  one of:
  - `Error while running debug sequence 'DebugPortStart' (core cm33_core0): SWD/JTAG communication failure (WAIT ACK)` or `(FAULT ACK)`
  - `Error while running debug sequence 'ResetCatchClear' (core cm33_core1): ... (FAULT ACK)` (pyOCD's default connect flow touches core1's debug-catch config even though this project only uses core0)
  - `Error while running debug sequence 'ResetSystem' (core cm33_core0): ... (FAULT ACK)`

  `DP IDR` (the most basic SWD register read) always succeeds (`0x6ba02477`)
  — the physical SWD link is fine. The failures are inside this chip's
  CMSIS-Pack-defined debug *sequences*, which need retry logic plain pyOCD
  doesn't have for this pack/probe pairing.
- **What doesn't help:** power-cycling, lowering SWD clock, different
  `--connect` modes, different USB cable/port.
- **Fix — use NXP's `spsdk`/`nxpdebugmbox` instead of relying on pyOCD's
  generic connect flow:**
  ```bash
  pip install spsdk   # provides the nxpdebugmbox CLI

  # 1. Unlock AHB/core access via the Debug Mailbox — must run immediately
  #    before every pyOCD flash/reset call, doesn't persist across a fresh connection
  nxpdebugmbox -i mcu-link cmd -f mcxn947 start-debug-session

  # 2. Flash with the broken debug sequences disabled
  pyocd flash -t mcxn947 \
    -O "pack.debug_sequences.disabled_sequences=ResetCatchSet:cm33_core1,ResetCatchClear:cm33_core1,ResetSystem:cm33_core0,ResetSystem:cm33_core1" \
    <path-to-elf>

  # 3. Disabling ResetSystem means step 2 won't reset into the new image — do it separately
  nxpdebugmbox -i mcu-link tool reset -f mcxn947 -h
  ```
  Mass-erase/recovery works directly, no extra flags:
  ```bash
  nxpdebugmbox -i mcu-link cmd -f mcxn947 erase
  ```
- **ISP mode (SW3 held + SW1) is a red herring for this problem** — it does
  enter ROM bootloader mode (error signature changes to
  `Invalid AP address (#0)`), but `nxpdebugmbox erase`/`start-debug-session`
  work fine without it. Note `nxpdebugmbox`'s `write-to-flash` does *not*
  work outside a specific device life-cycle state — use it only for
  `erase`/`start-debug-session`, then hand off to `pyocd flash` for actually
  programming.
- **Takeaway:** for MCX-family (and likely other LPC55xx-lineage) NXP
  parts, if pyOCD's CMSIS-Pack connect flow faults on `WAIT ACK`/`FAULT ACK`
  inside a chip-specific debug sequence (not a plain register read), reach
  for `spsdk`/`nxpdebugmbox` rather than varying pyOCD-level knobs.

## 6. References

- NXP UM12018 (FRDM-MCXN947 board user manual), §2.3 — USB connector wiring
- `neutron_converter` / `eiq_neutron_sdk` — NXP package index
- `spsdk` / `nxpdebugmbox` — NXP Debug Mailbox tooling
</content>
