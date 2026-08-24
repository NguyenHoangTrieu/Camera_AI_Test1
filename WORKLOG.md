# WORKLOG - Camera_AI_Test1

Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

> Older history (camera/LCD/USB bring-up, all of that now stable/working -
> see README.md's "History" section for the summary) was trimmed from this
> file to keep it focused on the current, unresolved problem below.

## CURRENT STATUS: Edge Impulse AI integration WORKING END-TO-END on real hardware (2026-08-24) - both a CPU+CMSIS-NN path (~1.27s/inference) and a Neutron NPU path (~3.3ms/inference, ~370-390x faster, `AI_MODEL_USE_NPU` CMake option) confirmed running repeatedly without faults, both timed via the same DWT cycle-counter print for direct comparison (`ei_result.timing` is a dead stub on this SDK build). LCD status-color display (red/green/blue per detection state) was already wired up and runs every frame; had a real bug (only painted one row - see "Bug #4"), fixed but not yet visually re-confirmed on the physical panel. See "Bug #2"/"Bug #3" below for what was actually wrong with the original HardFault (stack overflow, misdiagnosed as alignment last session; then a `m_sramx`/SmartDMA memory collision), "NPU (Neutron) plan" for the full NPU integration story (Phases 1-6, all done), and "Next steps" sections throughout for what's left (further detection-quality validation on both paths, optional bbox display, minor NPU arena size tuning).

## NPU (Neutron) plan - Phase 1 DONE: raw `.tflite` located, no Studio re-export needed

Goal (per user request): get the FOMO model running on this chip's Neutron16
NPU instead of the CPU+CMSIS-NN path, to cut the ~1.27s/inference time
measured above, with a build-time flag to compare NPU on/off using the
same DWT timing print already added to `model_runner.cpp`. Full plan (5
more phases after this one) discussed with the user before starting -
not repeated in full here, see the phase list below for what's left.

**Phase 1 result:** no need to log into Edge Impulse Studio and re-export
- the plain quantized `.tflite` this Studio project already produced is
sitting right next to the C++ library export that's already in the tree:
`source/ai/edge_impulse/tflite-model/tflite_learn_1094697_39.tflite`
(53.4KB). Confirmed this is the exact model currently running on-device -
`1094697` matches `EI_CLASSIFIER_PROJECT_ID` in
`model-parameters/model_metadata.h`.

Peeked at the flatbuffer's embedded strings (no `tensorflow`/`tflite_runtime`
Python package available in this environment to fully parse it, so this
is `strings`-level inspection only, not a real flatbuffer dump):
- Backbone is a MobileNetV2-style stack, `block_1` through `block_6`
  (inverted-residual blocks - `expand`/`depthwise`/`project`/`add`,
  standard fused Conv2D/DepthwiseConv2D/Relu6/BiasAdd naming), consistent
  with FOMO's usual "truncated MobileNetV2 backbone + a small detection
  head" architecture and the alpha=0.35 already noted for this Studio
  project elsewhere in this file.
- Head: `model_1/head/...` (Conv2D+BiasAdd+Relu) -> `model_1/logits/...`
  (Conv2D+BiasAdd) -> `output_0`.
- This is architecturally very close to the MobileNetV1 model NXP's own
  `middleware/eiq/mpp/tests/test_camera_mobilenet_view` example already
  runs on this exact board's Neutron16 NPU (int8, Conv2D/DepthwiseConv2D-
  heavy) - a good sign for op-support compatibility, though not a
  guarantee (won't know for sure until Phase 2's converter actually runs
  on it).

### Remaining phases (unstarted)

2. **Run `neutron_converter`** (Python package `eiq_neutron_sdk`, per
   `middleware/eiq/executorch/backends/nxp/backend/neutron_converter_manager.py`'s
   import) against the `.tflite` above, targeting the Neutron16 variant
   (matches `APP_USE_NEUTRON16_MODEL` in NXP's own frdmmcxn947 NPU
   example) - produces a new `.tflite` with supported subgraphs replaced
   by one `NEUTRON_GRAPH` custom op. **Not yet confirmed this package is
   installable/available in this environment** - may need an NXP/eIQ
   account; check before assuming this phase is a quick step.
3. **Convert the NPU `.tflite` to a C byte-array header**, same pattern as
   NXP's own `mobilenetv1_model_data_npu16_tflite.h`.
4. **Write `model_runner_npu.cpp`** - raw TFLM `MicroInterpreter` (not
   `ei_run_classifier()`, which has no way to register the `NEUTRON_GRAPH`
   custom op without patching EI's generated code) + `MicroMutableOpResolver`
   registering `Register_NEUTRON_GRAPH()` plus whatever ops stay
   un-offloaded (Dequantize/Softmax etc., per NXP's own
   `mobilenetv1_ops_micro_tflite.cpp` pattern) + link
   `middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`.
   Reuse the existing resize/quantize preprocessing from `model_runner.cpp`
   (NPU only accelerates the conv/pool ops, not DSP preprocessing). FOMO's
   grid-decode postprocessing needs to be hand-written here too, since
   this path bypasses EI's `ei_run_classifier()` entirely (and thus its
   postprocessing) - fairly small, it's a per-cell argmax+threshold over
   the output grid tensor.
5. **`CMakeLists.txt`**: add an `AI_MODEL_USE_NPU` option (default OFF)
   selecting `model_runner.cpp` vs `model_runner_npu.cpp`, linking the
   Neutron libs and enabling the `middleware.eiq.tensorflow_lite_micro.neutron`
   Kconfig component only when ON.
6. **Build both configs, flash each, compare** the existing `total
   classifier time` DWT print (baseline already recorded above: ~1.27s,
   CPU+CMSIS-NN, non-EON) against the NPU build's number.

**Known risks, flagged before starting Phase 2:** `eiq_neutron_sdk`
package availability in this environment is unverified; not all of the
MobileNetV2-style graph is guaranteed to be Neutron-offloadable (partial
offload is normal/expected, real speedup unknown until measured); the
NPU-compiled model likely needs a different arena/scratch memory budget
than the current CMSIS-NN path's ~93KB (re-check against the `m_sramx`
budget worked out in "Bug #2"/"Bug #3" below before assuming it just
fits).

## Phase 2 DONE (2026-08-24): `eiq_neutron_sdk` installed, model converted, results very promising

**Install:** the package is NOT on public PyPI - it's on NXP's own index,
found by grepping the `mcuxsdk` checkout for install instructions
(`middleware/eiq/executorch/backends/nxp/requirements-eiq.txt` and
`docs/nxp/topics/overview.md`):
```
pip install --index-url https://eiq.nxp.com/repository eiq_neutron_sdk==3.1.1
```
Confirmed with the user before running (installing from a third-party
index isn't something to do silently). Installed cleanly into this
project's `tools/westenv` venv (140MB wheel). Provides CLI tools, not a
Python API to `import` directly - `neutron_converter`, `tflite_profiler`,
`tflite_quantizer` all land on `PATH` inside the venv.

**Target name for this chip:** `neutron_converter --show-targets` lists
`mcxn54x`/`mcxn94x`/`imxrt700`/`imx95`/`imx943`/`imx952`/`s32k5`/`s32n79`.
**MCXN947 is `mcxn94x`** (MCX N94x family). Note this contradicts the
ExecuTorch Neutron backend's own README (`backends/nxp/README.md`), which
claims only "eIQ Neutron N3-64 (i.MX RT700)" is supported - that's a
narrower, higher-level ExecuTorch-specific backend; the lower-level
`neutron_converter` CLI used here (the same tool, invoked directly)
supports MCX N94x directly and is what NXP's own TFLM-based mcxn947
examples (`middleware/eiq/mpp/tests/test_camera_mobilenet_view`) actually
use under the hood.

**Ran it against the exact model in this project** (Phase 1's
`tflite_learn_1094697_39.tflite`):
```
neutron_converter --input tflite_learn_1094697_39.tflite --target mcxn94x \
  --output tflite_learn_1094697_39_npu.tflite --dump-header-file-output true
```
**Result: 31 of 32 operators (96.9%) got offloaded into a single
`NeutronGraph` custom op** - only one operator stayed as a regular
builtin op. Converter's own cycle estimate for the NPU-accelerated part:
**353,039 cycles**. At this board's 150MHz core clock
(`BOARD_BOOTCLOCKPLL150M_CORE_CLOCK`, `examples/_boards/frdmmcxn947/clock_config.h`
- not yet confirmed this is the exact clock config this project's
`BOARD_InitHardware()` actually selects, so treat as an estimate, not a
promise) that's roughly **~2.35ms** for the NPU-accelerated portion -
compare against the ~1.27s (1270ms) CPU+CMSIS-NN baseline measured
earlier this session. That's a very large potential speedup (~500x) on
paper, but **this is the converter's static cycle estimate for the NPU
graph alone**, not a real on-device measurement - it does NOT include the
DSP resize/quantize preprocessing (still runs on CPU regardless of NPU),
the one un-offloaded operator, or any Neutron driver/data-marshalling
overhead. Treat as "very promising, worth pursuing" not "confirmed 500x
faster" until Phase 4-6 actually run it on hardware with the same DWT
timing method used for the CPU baseline.

**Memory footprint (from the converter's own report) is smaller than the
current CMSIS-NN arena, too:** NPU path needs 73,984 bytes of data
(inputs+outputs+scratch) + 25,840 bytes of weights = ~99.8KB total vs. the
current path's ~93KB tensor arena - roughly comparable, maybe fits the
same `m_sramx` budget, but not yet checked against the exact
`ei_sramx_alloc.c` allocator (which is EI-SDK-specific and won't be used
by the NPU path's raw TFLM interpreter anyway - Phase 4 needs its own
memory plan, likely simpler since it's not sharing an allocator with EI's
DSP step).

**Bonus - the converter tells you exactly what op resolver to write**,
right in a comment at the top of the generated header
(`tflite_learn_1094697_39_npu.h`):
```cpp
static tflite::MicroMutableOpResolver<2> s_microOpResolver;
s_microOpResolver.AddSoftmax();
s_microOpResolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());
```
Only 2 ops needed (simpler than NXP's own MobileNetV1 example, which
needed 3) - directly usable in Phase 4's `model_runner_npu.cpp`.

**Output files saved in-tree** (not just `/tmp`, so a fresh session can
pick this up without re-running the converter):
`source/ai/neutron/tflite_learn_1094697_39_npu.tflite` (27KB - smaller
than the original 53.4KB `.tflite`, since 31 ops collapsed into one
compact NeutronGraph blob) and
`source/ai/neutron/tflite_learn_1094697_39_npu.h` (168KB, ready-to-embed
C header, has the op resolver snippet above at the top).

## Phases 3-6 DONE (2026-08-24): NPU path fully working on real hardware - ~370-390x faster than CPU+CMSIS-NN

**Phase 3** (C header) was already done as a side effect of Phase 2's
`--dump-header-file-output` - nothing more needed there.

**Phase 4:** `source/ai/model_runner_npu.cpp` (new file) - implements the
same `model_runner.h` API as `model_runner.cpp`, but talks to TFLite
Micro directly instead of going through `ei_run_classifier()`:
- Op resolver exactly as the converter's generated header suggested
  (`AddSoftmax()` + `AddCustom(NEUTRON_GRAPH)`).
- Preprocessing: same nearest-neighbor squash resize as
  `model_runner.cpp`'s `get_signal_data()`, but writes straight into an
  int8 NHWC tensor instead of EI's packed-float signal format. Turned out
  trivial once the input tensor's actual quantization was known (via
  `neutron_converter --dump-after-import console`, see Phase 2 above):
  scale=0.003922 (~1/255), zero_point=-128, so quantizing an RGB channel
  value is just `q = channel_value - 128`.
- Postprocessing: hand-ported from Edge Impulse's own
  `process_fomo_i8()`/`ei_handle_cube()`/`process_cubes()`
  (`edge-impulse-sdk/classifier/postprocessing/ei_postprocessing_common.h`)
  - confirmed via that source and the model dump that the output tensor
  is `INT8[1,8,8,4]` (8x8 grid, channel 0 = FOMO's implicit "background"
  class, channels 1-3 map to `categories[0..2]` =
  closed_eye/open_eye/yawning), with the same adjacent-cell merge-into-box
  logic EI uses, just with fixed-size arrays instead of `std::vector`
  (this model's grid is tiny - 64 cells, 3 classes - so no dynamic
  allocation needed). Detection threshold 0.5, matching
  `model_variables.h`'s `.threshold`.
- Same DWT cycle-counter timing print as `model_runner.cpp`, so both
  paths' "total classifier time" lines are directly comparable.

**Phase 5:** `CMakeLists.txt` - `AI_MODEL_USE_NPU` option (default OFF).
ON selects `model_runner_npu.cpp` over `model_runner.cpp` and skips the
whole Edge Impulse SDK glob entirely (the two TFLM snapshots - EI's
vendored copy vs. NXP's `middleware/eiq/tensorflow-lite` - must not both
be linked into the same image). NPU branch instead: compiles
`tensorflow/lite/micro/kernels/neutron/neutron.cpp` +
`micro_time.cpp` + `debug_log.cpp` from source (none of these three are
in the precompiled lib - confirmed by first getting `undefined reference
to DebugLog` and fixing it by adding NXP's own ready-made
`debug_log.cpp`, which just wraps `PRINTF`/`fsl_debug_console`), links
the precompiled `lib/cm33/armgcc/libtflm.a` (whole TFLM core +
CMSIS-NN reference kernels) plus
`middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`
directly via `target_link_libraries()` (simpler than fighting
`mcux_add_library()`'s CORES/TOOLCHAINS condition machinery for a
project that isn't Kconfig-driven anyway). **Did NOT need to touch
`main.c` at all** - `ei_sramx_alloc.c`/`ei_debug_porting.c` stay built in
both configs (harmless dead code in the NPU build, avoids having to
conditionally guard `main.c`'s `EI_SRAMX_SetOverflowPool()` call), and
both model runners implement the identical header.

Hit two build issues, both fixed:
- `mcux_add_include()` without `BASE_PATH` silently prepends
  `CMAKE_CURRENT_LIST_DIR` onto every entry - mangled the
  already-absolute `TFLM_ROOT`/`NEUTRON_ROOT` paths into a bogus nested
  path. Fixed by using `BASE_PATH ${SdkRootDirPath}` with relative
  `INCLUDES`, same pattern the EI SDK block above already uses.
- `kTensorArenaSize` first tried at 160KB - `m_data overflowed by 36648
  bytes` at link time (this project's non-NPU baseline already uses
  ~186KB of the 312KB `m_data` region for camera/LCD buffers etc., see
  "Bug #2"/"Bug #3" above - not much room left for a large static
  arena). Shrunk to 112KB, which fits (96.09% of `m_data` at link time -
  tight but works) and turned out to be plenty at runtime (see below).

**Phase 6 - built, flashed, measured on real hardware:**
```
AI_MODEL_Init: Neutron NPU FOMO ready (64x64 input, 3 classes, arena used 74660/114688 bytes)
AI_MODEL_RunInference: total classifier time = 3279us (3ms)
AI result: box[0] label=open_eye x=32 y=32 w=8 h=8 score=74%
AI_MODEL_RunInference: total classifier time = 3443us (3ms)
AI_MODEL_RunInference: total classifier time = 3270us (3ms)
AI result: box[0] label=open_eye x=24 y=24 w=8 h=8 score=52%
AI result: box[1] label=closed_eye x=40 y=32 w=8 h=8 score=59%
AI_MODEL_RunInference: total classifier time = 3264us (3ms)
AI_MODEL_RunInference: total classifier time = 3274us (3ms)
```
**~3.3ms per inference, consistently, across many consecutive runs - no
faults, no stalls.** Compare against the ~1.27s (1,270,000us) CPU+CMSIS-NN
baseline measured earlier this session: **roughly 370-390x faster**,
close to (a bit slower than, as expected - this includes the Softmax op
and framework overhead the converter's own 353,039-cycle/~2.35ms estimate
didn't count) the theoretical estimate from Phase 2. Arena headroom is
comfortable too - only 74,660 of the allocated 114,688 bytes actually
used, real margin above `AllocateTensors()`'s actual needs despite the
tight `m_data` link-time budget. Detection results vary sensibly
frame-to-frame (`open_eye`/`closed_eye`, different box positions/sizes/
scores, multi-cell boxes merging correctly, e.g. `w=8 h=8` single-cell
vs. a later `w=16 h=16` merged box) - the hand-rolled FOMO postprocessing
port is producing sane-looking output, not just "not crashing".

**Verified the CPU/non-NPU default path still builds identical to
before** (`rm -rf build && ./build.sh build` with `AI_MODEL_USE_NPU`
unset/OFF) - same `m_text`/`m_data`/`m_sramx` numbers as earlier in this
file, so the CMakeLists.txt changes for the NPU path are additive, not a
regression on the default build.

### Next steps for a fresh session

The NPU path is now functionally complete and confirmed fast/stable on
hardware. What's left is refinement, not core functionality:

1. **Detection-quality validation** (same caveat as the CPU path's own
   "Next steps" above) - varied/plausible results were observed, but
   accuracy hasn't been cross-checked frame-by-frame against what's
   actually in front of the camera.
2. **`kTensorArenaSize` (112KB) has ~40KB of unused headroom** at runtime
   (74,660 used) despite being link-time-tight against `m_data` (96.09%) -
   could shrink it back down (e.g. to ~80-88KB) to free up `m_data`
   margin for other uses, now that the real runtime number is known
   instead of guessing from the converter's static report.
3. **LCD status color + inference timing prints already work identically
   on both paths** (`main.c` didn't need to change) - no further wiring
   needed there.
4. **Not yet tried:** disabling `EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN` on
   the CPU path (older "Next steps" note, superseded in priority by the
   NPU result above - CMSIS-NN vs. reference-kernel CPU speed is much
   less interesting now that NPU is ~370x faster than CMSIS-NN already).

**Goal:** run a trained Edge Impulse FOMO object-detection model
("Test_Drowsy_NXP" Studio project, 3 classes: `closed_eye`/`open_eye`/
`yawning`, 64x64 input, int8 quantized) on-device, fed from the OV7670
camera, with results shown on the LCD (originally bounding boxes on the
live image, now just a solid status color - see below for why).

**Current export in tree:** `source/ai/edge_impulse/` = Studio deployment
v14 (impulse #11), **non-EON** (`EI_CLASSIFIER_COMPILED=0`, plain TFLite
Micro interpreter, not EON-compiled generated code - see "Bug #2" below
for why this was switched).

### Firmware architecture added for this (all still in the tree, believed
### structurally correct - see "Open problem" below for what's still broken)

- `source/ai/edge_impulse/` - the Edge Impulse C++ library export, built
  via `mcux_add_source()` with a hand-rolled `file(GLOB_RECURSE ...)` in
  the top-level `CMakeLists.txt` (**not** the export's own
  `CMakeLists.txt`/`add_subdirectory()` - that hardcodes `if(NOT TARGET
  app)`, but this SDK's west/non-find_package build mode names its real
  target `${MCUX_SDK_PROJECT_NAME}`, e.g. `camera_ai_demo_cm33_core0`, not
  literally `app`).
- `source/ai/model_runner.cpp` (was `.c` - had to become C++ to call the
  SDK) - calls `ei_run_classifier()` (the `extern "C"` wrapper in
  `ei_run_classifier_c.h`, not the templated `run_classifier()` in
  `ei_run_classifier.h` directly - including that header caused ODR
  "multiple definition" link errors against
  `edge-impulse-sdk/classifier/ei_run_classifier_c.cpp`, which already
  includes it. Only `ei_classifier_types.h` + `dsp/returntypes.h` are
  included here, plus a hand-written `extern "C"` forward-declaration of
  `ei_run_classifier()` - see the comment in that file for the exact ODR
  reasoning if this needs revisiting).
- `source/ai/ei_sramx_alloc.c/.h` - **custom allocator**, overrides the
  SDK's weak `ei_malloc`/`ei_calloc`/`ei_free`
  (`edge-impulse-sdk/porting/clib/ei_classifier_porting.cpp`) to serve
  memory from `m_sramx` (a 96KB SRAM bank that exists on this chip but
  isn't used by anything else, declared but never placed into any output
  section by the SDK's own linker script) instead of the real heap
  (`m_data`, which is ~99% full from camera+LCD framebuffers + stack -
  nowhere near enough for a ~93KB tensor arena). Two-tier: primary pool
  (m_sramx, fixed 96KB) + an optional "overflow" pool lent in via
  `EI_SRAMX_SetOverflowPool()` (main.c lends it a dedicated
  `s_aiScratchPool` buffer, repurposed from what used to be the live LCD
  image framebuffer - see below). Proper LIFO free-record stack (not just
  a no-op) - the DSP image-resize step allocates/frees a small scratch
  buffer ~75 times per inference (once per 1024-pixel page), and a naive
  no-op free() leaked every one of those and exhausted the pool well
  before the real bug (see "Bug #1" below) was found and fixed.
- `source/ai/ei_debug_porting.c` - overrides `ei_printf`/`ei_printf_float`
  (also weak in the SDK's clib porting layer) to go through this
  project's `fsl_debug_console` `PRINTF()`/`DbgConsole_Vprintf()` instead
  of plain `vprintf()`/`printf()`, which go nowhere useful under this
  project's `--specs=nosys.specs` link.
- `board_port/ei_sramx.ld` - **additive** linker fragment (a *second* `-T`
  passed via `mcux_add_armgcc_linker_script()` in `CMakeLists.txt`, after
  the SDK's own board linker script) adding one new output section that
  places `.ei_sramx`-tagged symbols into the `m_sramx` region. Does NOT
  use `INSERT AFTER` - tried that first, GNU ld rejected it ("`.bss` not
  found for insert"); a plain second `SECTIONS {}` block without INSERT
  just gets concatenated after the base script's sections by default,
  which is sufficient here (position within `m_sramx` relative to `m_data`
  sections doesn't matter, they're different physical banks).
- `CMakeLists.txt` additions: the linker fragment above; the hand-rolled
  EI source glob; `mcux_add_macro(... -DEI_PORTING_CLIB=1
  -DEI_C_LINKAGE=1 -DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1
  -DARM_MATH_LOOPUNROLL
  -DSILENCE_EI_CLASSFIER_OBJECT_DETECTION_COUNT_WARNING=1)` both in `CC`
  and `CX` categories (`mcux_add_macro()`'s C++ flag category is `CX`, not
  `CXX` - easy to miss); `-Wno-error` scoped to just the EI source files
  (the whole project otherwise builds `-Werror`, and EI's vendored
  TFLite-Micro/CMSIS-NN isn't fully warning-clean under this toolchain);
  `mcux_add_linker_symbol(SYMBOLS "__stack_size__=0x1000")` (bumped from
  the SDK default 2KB to 4KB - TFLite Micro's C++ call chain is deep).
- `source/fault_handler.c` - pre-existing HardFault dump handler, fixed
  during this work: it used `%08lX` throughout, which
  `debug_console_lite`'s minimal printf mishandles (prints the literal
  characters `lX` instead of the value - same class of bug as this
  project's pre-existing "`%f` not supported" notes elsewhere). Now uses
  `%08X` with `(unsigned int)` casts (fine, `unsigned long`/`unsigned int`
  are both 32-bit on this target). **Without this fix the fault dumps are
  useless** (all register values print as literal `0xlX`) - if a fresh
  session sees garbled fault dumps again, check this hasn't regressed.
- `source/display/bbox_overlay.c/.h` - draws bounding-box rectangles into
  an RGB565 buffer. **Currently unused** - see "Live image display
  disabled" below. Still compiles (harmless dead code), not deleted.
- `source/main.c` - runs `AI_MODEL_RunInference()` once per captured
  camera frame; on success, was drawing bounding boxes on the live camera
  image and pushing it to the LCD - **this is currently disabled** (see
  below).

### Live image display disabled (RAM optimization + simplification)

Per explicit direction mid-session: `main.c`'s live-camera-image-on-LCD
path (`memcpy` into `s_lcdSnapshot` + `DEMO_DrawAiBoxes()` +
`LCD_DrawImage()`) is commented out (`#if 0`, search
`DEMO_DrawAiBoxes`/`DEMO_ColorForLabel` in `main.c`), not deleted. The LCD
now just fills solid with a status color instead
(`DEMO_ShowStatusColor()`): red = `closed_eye`/`yawning` detected this
frame, green = `open_eye`, blue = nothing detected. The RAM that used to
be `s_lcdSnapshot` (a second 150KB framebuffer, needed for tearing-free
image display) is now `s_aiScratchPool`, permanently dedicated as the AI
allocator's overflow pool (see `ei_sramx_alloc.c` above) - no more of the
fragile "don't touch this buffer while inference is running" ordering
constraint that came with temporarily borrowing the display buffer (an
earlier, now-abandoned approach).

If image + bounding-box display is revisited, the commented-out code in
`main.c` is the starting point - but see the RAM budget math in
README.md/this file first, since re-adding a 150KB display buffer
directly competes with the AI model's RAM needs again.

### Bug #1 (FOUND AND FIXED): wrong `signal.total_length` - the real
### cause of the very first HardFault symptom, took most of this session
### to find

**Symptom:** a precise Bus Fault (`BFSR=0x82`, `PRECISERR`+`BFARVALID`),
`BFAR`/`MMFAR` = `0x04018000` **exactly** - the first byte past the end of
`m_sramx` (`0x04000000` + `0x18000` = `0x04018000`) - every single time,
regardless of how much RAM was given to the allocator (tried: bigger
primary pool, a 150KB overflow pool = 246KB combined, EON vs.
non-EON/interpreter model, proper LIFO free instead of a leaking no-op).
PC always inside `extract_image_features_quantized()`
(`edge-impulse-sdk/classifier/ei_run_dsp.h`), called via
`signal->get_data()` → my callback.

**Root cause:** `extract_image_features()`/`_quantized()` do **not
resize** - they read exactly `signal->total_length` elements via
`get_data()` and write that many pixels straight into `output_matrix`,
which the *caller* sizes for `EI_CLASSIFIER_INPUT_WIDTH *
EI_CLASSIFIER_INPUT_HEIGHT` (64*64 = 4096 pixels for this model).
`model_runner.cpp` was setting `signal.total_length = camera_width *
camera_height` (320*240 = 76800 - the **raw camera frame** size, not the
model's input size) - so the loop wrote ~76800*3 = 230,400 output values
into a buffer sized for 4096*3 = 12,288. A ~218KB overflow, silently
marching through `m_sramx` for the entire ~93KB tensor arena's worth of
memory and beyond, until it finally ran off the end of the whole bank and
faulted - **not** a capacity problem, which is why every RAM-budget fix
tried first had zero effect.

**Fix applied** (`source/ai/model_runner.cpp`): `signal.total_length` is
now `EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT` (4096), and
`get_signal_data()` does the resize itself - nearest-neighbor "squash"
(independent per-axis scaling, matching `EI_CLASSIFIER_RESIZE_MODE`):
for each requested *target* (64x64) pixel index, maps back to the nearest
*source* (320x240) camera pixel (`sx = tx * srcWidth / dstWidth`, same for
y), then converts that source pixel RGB565 → packed `0xRRGGBB` float as
before.

**Confirmed fixed**: after this change, the crash signature changed
entirely (see Bug #2) - no more `0x04018000` bus fault, so the DSP
resize/feature-extraction step now completes successfully. This was the
right fix.

### Bug #2 (FOUND AND FIXED, but see "Open problem" - crash persists past this): missing 8-byte alignment on the overflow pool

**Symptom (after fixing Bug #1):** a Usage Fault, not a Bus Fault this
time - `UFSR = 0x10` (bit 4, `UNALIGNED`), `MMFSR`/`BFSR` both 0. `MMFAR`/
`BFAR` show `0xE000ED34`/`0xE000ED38` (the *addresses of those two
registers themselves* - not meaningful; MMFAR/BFAR aren't valid/written
for a pure UsageFault, ignore them for this fault type). PC inside
`arm_nn_mat_mult_nt_t_s8` (CMSIS-NN), called from
`arm_convolve_1x1_s8_fast` - i.e. **actual NN convolution now running**,
real progress past the DSP stage.

**Suspected cause:** `s_aiScratchPool` (`main.c`, the overflow pool lent
to the AI allocator) was declared as a plain `static uint8_t
s_aiScratchPool[...]` with no alignment attribute, unlike `s_pool` in
`ei_sramx_alloc.c` (which has `__attribute__((aligned(8)))`). CMSIS-NN
kernels use wide/vectorized loads on tensor buffers and require alignment
- the allocator computes offsets that are 8-aligned *relative to the
pool's own start*, which only produces real 8-byte-aligned *addresses* if
the pool's start address is itself 8-aligned.

**Fix applied:** added `__attribute__((aligned(8)))` to
`s_aiScratchPool`'s declaration in `main.c`.

**Verified via `nm` after rebuilding** - all three pool-related symbols
land on 8-byte-aligned addresses:
```
04000000 b s_pool            (ei_sramx_alloc.c, primary pool)
04017a00 b s_records          (ei_sramx_alloc.c, LIFO free-record stack)
200009b8 b s_aiScratchPool.0  (main.c, overflow pool - 0x9b8 = 184 = 23*8)
```

## Bug #2 was MISDIAGNOSED - real cause was stack overflow (STKOF), not alignment; FOUND AND FIXED (2026-08-24)

**The "UsageFault, UNALIGNED" diagnosis above was wrong.** `CFSR = 0x00100000`
is bit 20 of CFSR, which is `UFSR` bit 4 - and per this SDK's own
`core_cm33.h`, **bit 4 of UFSR is `STKOF` (hardware stack-limit check,
ARMv8-M-only), not `UNALIGNED`** (`UNALIGNED` is UFSR bit 8, i.e. would
need `CFSR = 0x01000000`, not `0x00100000` - easy to misread without
checking the actual bit position against `core_cm33.h`). Confirmed by
disassembly: `arm-none-eabi-objdump
-d` at the faulting PC (`0x18A44`) shows `subw sp, sp, #2780` - the
*prologue* of `arm_nn_mat_mult_nt_t_s8`, reserving 2780 bytes of locals for
its unrolled `q31` accumulators. That's a pure arithmetic instruction, not
a memory access - it cannot raise `UNALIGNED`. It's exactly the kind of
instruction that trips a hardware `SP < PSPLIM` check the instant it
executes, which is what `STKOF` is. The `aligned(8)`/`aligned(16)` changes
made chasing the wrong diagnosis were harmless (real fixes, just not
_the_ fix) and have been kept.

**Fix applied:**
- `CMakeLists.txt`: `__stack_size__` `0x1000` (4KB) -> `0x4000` (16KB).
- `source/main.c`: `s_aiScratchPool` (the AI allocator's overflow pool)
  shrunk from `DEMO_BUFFER_WIDTH*DEMO_BUFFER_HEIGHT*sizeof(uint16_t)`
  (150KB) down to a fixed `16*1024` (16KB) - the tensor arena
  (`EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` = 92876 bytes) fits entirely
  inside the primary 96KB `m_sramx` pool on its own (96KB minus the
  `ei_sramx_alloc.c` free-record stack leaves ~94KB), so the overflow pool
  was never doing much beyond covering the DSP resize step's small
  per-page scratch buffers - 150KB was massive overkill, and was exactly
  what was starving `m_data` of room to grow the stack (`m_data` was at
  98.67% before this fix, 59.56% after).

**Confirmed fixed**: flashed and reset - **no HardFault at all**, boot log
completes and the main loop runs. (See Bug #3 below for what turned up
next, once inference was actually completing multiple times in a row.)

## Bug #3 (FOUND, NOT YET FIXED): `m_sramx` collides with the SmartDMA camera coprocessor's own firmware RAM - camera capture stalls a few frames after AI starts allocating there

**This is a bigger problem than Bug #2 and needs a real design decision,
not just a one-line fix - flagging clearly rather than guessing at a fix.**

**Symptom:** after the Bug #2 fix above, inference runs and completes
without faulting - genuine progress - but `s_frameCount`
(`camera_capture.c`) freezes after a small number of frames (observed:
stuck at exactly `3`, for 3+ seconds of continued execution, confirmed via
`pyocd commander` - halted the core, read `pc`/`lr`/`sp`, saw it sitting in
`CAMERA_CAPTURE_IsFrameReady()`'s poll loop in `main.c` (not stuck *inside*
inference, not faulted - just legitimately idle, waiting on a
`s_frameReady` flag that never gets set again). Confirmed via `read32` on
`s_frameCount`'s address, taken twice 3 seconds apart while the core was
resumed and running: identical value both times. Per README.md, the camera
+ SmartDMA path was previously confirmed to run continuously and
indefinitely on its own (frame #16, #46, ... in old logs) - so freezing
after 3 frames is new, and only showed up once AI inference started
actually completing multiple times in a row (every earlier session's
crash happened on/near the very first inference, before this had a chance
to surface).

**Root cause:** `SMARTDMA_CAMERA_MEM_ADDR` (defined in the MCUXpresso
SDK's own `drivers/smartdma/mcxn/fsl_smartdma_fw.h`) is `0x04000000` -
**the exact same physical address as `m_sramx`**, which is also where
`ei_sramx_alloc.c`'s `s_pool` (the AI tensor arena's primary pool) is
placed (confirmed via `nm`: `04000000 b s_pool`). `CAMERA_CAPTURE_InitSmartDma()`
(`camera_capture.c`) calls `SMARTDMA_InstallFirmware(SMARTDMA_CAMERA_MEM_ADDR,
...)`, loading the SmartDMA coprocessor's own camera-capture microcode
into that bank, and the coprocessor keeps using that memory as its
live working RAM for as long as it's capturing frames. This directly
contradicts this project's own working assumption (stated earlier in this
file and in the `ei_sramx_alloc.c` header comment) that `m_sramx` "exists
on this chip but isn't used by anything else" - **that's wrong**: it's not
used by anything in the *statically-linked* image (hence "not placed into
any output section by the SDK's own linker script"), but it *is* actively
used at runtime by the SmartDMA coprocessor, which explains why nothing in
the base linker script reserves it (SmartDMA firmware is installed
programmatically via `SMARTDMA_InstallFirmware()`, not through the linker).

Once the AI classifier starts bump-allocating and writing real data into
`s_pool` (tensor arena, working buffers, etc.), it's directly overwriting
the SmartDMA camera coprocessor's own firmware/state in the same physical
bytes. The coprocessor doesn't share the ARM core's fault mechanism - it
just silently stops producing frame-complete interrupts once its own
memory gets corrupted enough, which is consistent with the "frame count
just stops, no crash" symptom (the first few frames survive because the
tensor arena bump-allocator hasn't grown far/into the specific bytes
SmartDMA still needs yet).

### Next steps for a fresh session

This needs an actual architectural decision, not a quick patch - the AI
tensor arena and live SmartDMA camera capture **cannot coexist in
`m_sramx`** as currently structured. Options, roughly cheapest to most
invasive:

1. **Stop SmartDMA before running inference, restart it after**
   (`CAMERA_CAPTURE_Deinit()` + `CAMERA_CAPTURE_Reinit()`, both already
   exist in `camera_capture.c` and are already used for the USB-streaming
   build's time-multiplexing - see the `#else` branch in `main.c`). Turns
   this into a strictly time-multiplexed pipeline: capture one frame,
   deinit SmartDMA, run inference (safe to fill `m_sramx` now), reinit
   SmartDMA, wait for the next frame. Simplest fix, matches a pattern this
   codebase already has precedent for - but re-check `OV7670_Init()`/SCCB
   re-negotiation cost each `Reinit()` (may not be as cheap as just
   toggling an IRQ) and confirm the sensor doesn't need a settle delay
   after re-enabling before the first frame is trustworthy.
2. **Move the AI tensor arena somewhere else entirely** (not `m_sramx`) -
   but `m_data` is the only other RAM candidate and it's already
   constrained (59.56% used after the Bug #2 fix, but that's *without* a
   ~93KB arena in it - adding one back there would blow past capacity
   again, the exact problem `m_sramx` was originally adopted to solve).
   Not obviously viable unless something else in `m_data` shrinks a lot
   first.
3. **Only allocate/write to the parts of `m_sramx` SmartDMA isn't using** -
   would require knowing the SmartDMA camera firmware's actual size/layout
   precisely (not just its base address) and carving the AI pool to start
   after it - fragile (undocumented, could change with SDK updates) and
   not recommended over option 1.

**Recommended: option 1** - it directly matches this project's own
time-multiplexed USB-streaming precedent, and per the file-level comment
in `main.c`, capture and heavy compute already can't overlap on this chip
for unrelated reasons (SmartDMA + USB HS conflict) - suggesting
capture/inference mutual exclusion is already an accepted constraint here,
not a new one.

**FIXED (option 1 implemented, 2026-08-24):** `source/main.c`'s default
(non-USB) loop now calls `CAMERA_CAPTURE_Deinit()` right before
`AI_MODEL_RunInference()` and `CAMERA_CAPTURE_Reinit()` right after -
exactly mirroring the existing `DEMO_CaptureFramesAtMidVoltage()` /
USB-streaming pattern already in the same file. SmartDMA is fully torn
down (IRQ disabled, coprocessor deinited) before the AI arena touches
`m_sramx`, and only reinstalled/re-armed once inference has returned and
freed it again.

**Confirmed fixed on real hardware** - flashed, reset, monitored ~45s
continuously:
```
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=61%
Camera: frame #16 ready, 792 samples, pixel range 0x31A7..0xCE1A, avg=0x9AE2
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=53%
...
Camera: frame #46 ready, 792 samples, pixel range 0x31C7..0xCF1A, avg=0xA48B
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=61%
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=72%
```
No HardFault, no frame-count stall - `frame #16`/`#46` confirm SmartDMA
keeps delivering frames indefinitely across many deinit/reinit cycles, and
`AI result` lines confirm the whole pipeline (capture -> DSP resize ->
CMSIS-NN convolution -> FOMO postprocessing -> box output) is now running
end-to-end repeatedly without crashing. **The AI integration goal stated
at the top of this file is met** - closed_eye is genuinely being detected
each cycle (consistent with the camera currently pointed at a closed/no
eye scene during this test - not yet validated against open_eye/yawning
scenes or bounding-box position/size accuracy, see "Next steps" below).

### Inference timing MEASURED (2026-08-24) - `ei_result.timing` is a dead end on this SDK build, use DWT instead

`ei_result.timing` (the SDK's own timing struct, `ei_impulse_result_t.timing`)
reads all-zero on this platform - traced to
`edge-impulse-sdk/porting/clib/ei_classifier_porting.cpp`'s
`ei_read_timer_us()`, which is hard-coded `return 0;` (and, unlike
`ei_malloc`/`ei_printf` in the same file, **not** marked `__attribute__((weak))`,
so it can't be overridden the usual way this project overrides other clib
porting stubs).

**Fix applied:** `source/ai/model_runner.cpp` now times the whole
`ei_run_classifier()` call itself using the Cortex-M33's DWT cycle counter
(`AI_MODEL_InitTiming()`, called once from `AI_MODEL_Init()`, enables
`DWT->CYCCNT` via `CoreDebug->DEMCR`/`DWT->CTRL`; `AI_MODEL_RunInference()`
samples `DWT->CYCCNT` before/after and converts to microseconds via
`SystemCoreClock`). Prints `AI_MODEL_RunInference: total classifier time =
<N>us (<N>ms)` after every inference.

**Measured on real hardware, 5 consecutive frames:** 1271927us, 1271390us,
1271188us, 1271286us, 1270365us - **consistently ~1.27 seconds per
inference**, +/-2ms across samples (i.e. essentially deterministic - makes
sense for a fixed-size int8 model with no early-exit/data-dependent
branching). Combined with the `CAMERA_CAPTURE_Deinit()`/`Reinit()`
round-trip added for Bug #3, full pipeline throughput is well under 1fps
right now (~0.78 inferences/sec, ignoring the deinit/reinit overhead
itself, which wasn't separately measured).

This is the non-EON TFLite Micro interpreter (`EI_CLASSIFIER_COMPILED=0`,
see the file's top for why EON was switched off) with CMSIS-NN
acceleration on a plain Cortex-M33 core - no surprise it's slow by
smart-camera standards. Worth revisiting EON and/or the Neutron NPU path
(see the trimmed "Next steps" list further up this file, item 4 - NPU
alternative) if sub-second/higher-fps response ever becomes a real
requirement; out of scope for just getting the pipeline correct, which is
what this session was about.

### Next steps for a fresh session

1. **Validate detection quality further**: this session's live testing
   (after the Bug #3 fix) has already shown varied, plausible-looking
   results across frames - `closed_eye`, `open_eye`, and `yawning` all
   appeared with different box positions/sizes and confidence scores
   50-97% as the camera view changed, not just one fixed-looking result
   like the very first post-fix test suggested. That's a good sign (rules
   out the "stuck on one static wrong answer" failure mode), but scores
   and box positions haven't been cross-checked against what's actually in
   front of the camera frame-by-frame - still worth a proper side-by-side
   check if detection accuracy matters for real use, not just "does it
   produce varied output".
2. ~~Check inference timing~~ - DONE, see above (~1.27s/inference,
   non-EON+CMSIS-NN on Cortex-M33 core).
3. **Bounding-box display is still disabled** ("Live image display
   disabled" section above) - now that inference is actually working
   end-to-end, revisiting that (if wanted) means re-budgeting RAM again:
   `m_data` is currently at 59.56% (`build.sh build` output), so there's
   real headroom now (unlike when that feature was disabled), but a 150KB
   live-image buffer would eat most of it back up - check current numbers
   before re-adding. Given ~1.27s/inference, live boxes would also update
   well under 1fps - may be worth confirming that's an acceptable UX
   before spending the RAM budget on it.

## Bug #4 (FOUND AND FIXED, 2026-08-24): LCD status-color fill only ever painted the first row - `LCD_PushPixels()` closes CS every call, `DEMO_ShowStatusColor()` called it in a loop

**Symptom (reported by user, on real hardware):** the LCD showed only a
single vertical stripe of the status color, with the rest of the screen
alternating black/white horizontal stripes (stale/garbage GRAM content).

**Root cause:** `LCD_SetWindow()` (both `lcd_bitbang.c` and
`lcd_flexio_mculcd.c`) asserts CS and issues the column/row/memory-write
(`0x2A`/`0x2B`/`0x2C`) commands, leaving the transfer open - by design,
so one `LCD_SetWindow()` + one `LCD_PushPixels()` pushes one block of
pixels and then `LCD_PushPixels()` itself closes CS at the end. But
`DEMO_ShowStatusColor()` (`main.c`) doesn't have a full 320x240 framebuffer
to push in one call (that's the whole point of it - filling the screen
with one small 320-pixel row buffer reused `DEMO_BUFFER_HEIGHT` times to
avoid a 150KB buffer), so it was calling `LCD_SetWindow()` **once** and
then `LCD_PushPixels()` **240 times** (once per row) - and every one of
those calls closes CS at the end. Only the first row's bytes actually
reach the panel while CS is still asserted; the other 239 calls send
their WR pulses/data with CS already deasserted, which the panel simply
ignores (GRAM position doesn't advance, pixel data isn't latched). With
this panel's MADCTL `MV=1` (row/column exchange, see `LCD_InitPanel()`),
one "row" of memory-write data physically renders as one vertical stripe,
matching the reported symptom exactly. The rest of the screen kept
whatever was already in GRAM from before (explains the black/white
stripe pattern - leftover panel-internal content, unrelated to anything
this firmware wrote).

**Fix applied:** added `LCD_PushPixelsOpen()` (same as `LCD_PushPixels()`
but does NOT close CS) and `LCD_EndWindow()` (closes CS) to both LCD
backends (`lcd_bitbang.c/.h` and `lcd_flexio_mculcd.c/.h`, same API on
both per the existing "same public API" convention - `lcd_display.h`
picks one at compile time). `LCD_PushPixels()` itself is now just
`LCD_PushPixelsOpen()` + `LCD_EndWindow()`, so existing single-call users
(`LCD_DrawImage()`) are unaffected. `DEMO_ShowStatusColor()` (`main.c`)
now calls `LCD_PushPixelsOpen()` in its per-row loop and a single
`LCD_EndWindow()` after the loop, keeping CS asserted for the whole
320x240 fill.

**Build/flash verified** (builds clean, flashes, boots without fault) -
**not yet visually re-confirmed on the physical panel** (no camera
access from this session to see the screen) - a fresh session or the
user should visually check the LCD now shows a solid full-screen color
that changes with the detected state, not just one stripe.

## OPEN PROBLEM (SUPERSEDED - see Bug #2 fix above): identical HardFault persists even after the alignment fix above

Flashed the `aligned(8)` fix and got **the exact same fault** as before it
- byte-for-byte identical register dump:
```
CFSR  = 0x00100000 (MMFSR=0x00 BFSR=0x00 UFSR=0x0010)   -> UsageFault, UNALIGNED
HFSR  = 0x40000000
MMFAR = 0xE000ED34  (not meaningful for UsageFault, see above)
BFAR  = 0xE000ED38  (not meaningful for UsageFault, see above)
Stacked r0=0x04000020 r1=0x0003A284 r2=0x0003A310 r3=0x0400C020
Stacked r12=0xFF414651 LR=0x0000E30B PC=0x00018A44 xPSR=0x69100200
```
(PC/LR resolve to `arm_nn_mat_mult_nt_t_s8`
(`edge-impulse-sdk/CMSIS/NN/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s8.c:63`)
called from `arm_convolve_1x1_s8_fast`
(`.../ConvolutionFunctions/arm_convolve_1x1_s8_fast.c:133`), via
`arm-none-eabi-addr2line -e build/camera_ai_demo_cm33_core0.elf -f -C -i <addr>`.)

**This means the `aligned(8)` fix on `s_aiScratchPool` either wasn't the
real cause, or wasn't sufficient** (e.g. CMSIS-NN might need 16-byte
alignment for this specific fast-path kernel, not just 8 - untested).
Register values worth noting for whoever picks this up:
`r0=0x04000020` and `r3=0x0400C020` are both addresses *inside*
`s_pool`/`m_sramx` (`0x04000000`-`0x04018000` range) - `0x04000020` is
only 0x20 (32 bytes) past `s_pool`'s start, `0x0400C020` is 0x4000C020 -
0x04000000 = 0xC020 = 49184 bytes in. Neither is obviously misaligned to
8 (`0x20 % 8 = 0`, `0xC020 % 8 = 0`) or even 16 (`0x20 % 16 = 0`, but
`0xC020 % 16 = 0` too) - so if one of these two registers is the actual
faulting address, plain alignment doesn't explain it at first glance;
might be worth checking 4-byte vs. wider access width assumptions, or
whether the *access size* (e.g. a `LDRD`/64-bit load) combined with a
32-but-not-64-bit-aligned address is the actual trigger (8-aligned isn't
automatically 8-byte-*access*-safe on all instruction forms - some need
the access size itself, not just 8, as the alignment requirement, e.g. a
16-byte NEON-style load needing 16-byte alignment where 8 isn't enough).

### Next steps for a fresh session

1. **Try 16-byte alignment** on `s_aiScratchPool` (and maybe `s_pool` /
   `s_records` too, for consistency) instead of 8 - cheap to try, CMSIS-NN
   / TFLite tensor arenas are often documented as wanting 16-byte
   alignment, not just 8.
2. **Try disabling CMSIS-NN acceleration**
   (`EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN` - currently forced to `1` in
   `CMakeLists.txt`) to fall back to TFLite Micro's generic reference
   kernels instead of the CMSIS-NN fast-path (`arm_convolve_1x1_s8_fast`).
   Slower inference, but reference kernels are less likely to have strict
   alignment assumptions - useful to confirm/rule out CMSIS-NN alignment
   requirements specifically, even if not the final answer (much slower
   inference isn't great long-term, given `ei_result.timing` wasn't even
   checked yet this session).
3. **Check `arm_nn_mat_mult_nt_t_s8.c:63`** and
   `arm_convolve_1x1_s8_fast.c:133` directly (both under
   `source/ai/edge_impulse/edge-impulse-sdk/CMSIS/NN/Source/`) to see
   exactly which pointer is being dereferenced at the fault, and whether
   it's the tensor arena/activation buffer, a weights pointer (which would
   point into flash/`.rodata`, `m_text`, not `m_sramx`/`m_data` - a
   different fix entirely if so, since flash addresses aren't under this
   project's control the same way), or something else.
4. **NPU alternative** (raised mid-session, not pursued yet): this exact
   chip (MCXN947) genuinely has a Neutron NPU -
   `middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`
   exist in the SDK, and there are real NXP example projects using it on
   this exact board (`examples/_boards/frdmmcxn947/eiq_examples/tflm_kws/npu/`,
   `.../mpp/tests/test_camera_mobilenet_view/`, `test_image_ultraface/`).
   This is a **substantial rewrite** - bypasses the Edge Impulse SDK's
   `run_classifier()` entirely, needs the model converted with NXP's own
   `neutron-converter` toolchain (not Edge Impulse Studio), and a new
   `model_runner.cpp` written against NXP's own Neutron driver API. Only
   worth it if the CMSIS-NN alignment issue above turns out to be a deep
   rabbit hole - try the cheaper alignment/CMSIS-NN-disable steps first.
5. If none of the above resolves it: the model itself (FOMO, 64x64,
   alpha 0.35, int8, Studio project "Test_Drowsy_NXP" v14/impulse #11) is
   otherwise confirmed sound - trained/tested in Studio with reasonable
   results (F1 ~63%, see prior session's Studio screenshots/history, not
   repeated here). Re-exporting from Studio isn't likely to help further
   unless specifically changing something that affects tensor
   alignment/layout (e.g. a different quantization or backbone) - the
   remaining issue looks firmware/integration-side, not model-side.
