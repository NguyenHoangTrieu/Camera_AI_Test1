/*
 * model_runner_npu.cpp - alternate model_runner.h implementation using
 * this chip's Neutron NPU (target "mcxn94x") instead of the CPU+CMSIS-NN
 * path in model_runner.cpp. Selected at build time by AI_MODEL_USE_NPU
 * (CMakeLists.txt) - see WORKLOG.md "NPU (Neutron) plan" for the full
 * story, including how the model below was produced.
 *
 * Bypasses the Edge Impulse SDK entirely - ei_run_classifier() has no way
 * to register the NEUTRON_GRAPH custom TFLM op without patching EI's own
 * generated code, so this talks to TFLite Micro directly, using NXP's own
 * middleware/eiq/tensorflow-lite tree (a different TFLM snapshot than the
 * one Edge Impulse vendors under source/ai/edge_impulse/edge-impulse-sdk/
 * - the two must not be mixed in the same translation unit).
 *
 * Model: neutron/tflite_learn_1094697_39_npu.h - the same FOMO model as
 * model_runner.cpp (Test_Drowsy_NXP, project ID 1094697), run through
 * `neutron_converter --target mcxn94x` (see WORKLOG.md "Phase 2"). 31 of
 * the original 32 ops got folded into one NEUTRON_GRAPH custom op; only
 * Softmax stays a regular op - the converter's own generated header
 * comment says exactly this op resolver is needed (AddSoftmax() +
 * AddCustom(NEUTRON_GRAPH)), reproduced below. neutronInit() is called
 * internally by the NEUTRON_GRAPH kernel itself
 * (tensorflow/lite/micro/kernels/neutron/neutron.cpp) on first Prepare()
 * - no manual NPU init needed here.
 *
 * Preprocessing (resize + quantize) is the same nearest-neighbor "squash"
 * used in model_runner.cpp's get_signal_data(), just writing straight
 * into an int8 NHWC tensor instead of Edge Impulse's packed-float
 * 0xRRGGBB signal format. Input quantization here (scale=0.003922,
 * zero_point=-128, confirmed via `neutron_converter --dump-after-import
 * console`) works out to the trivial `q = channel_value - 128` (scale is
 * ~1/255, so one pixel unit is one quantized unit).
 *
 * Postprocessing (FOMO grid decode) is hand-rolled, ported from Edge
 * Impulse's own process_fomo_i8()/ei_handle_cube()/process_cubes()
 * (edge-impulse-sdk/classifier/postprocessing/ei_postprocessing_common.h)
 * since this path skips ei_run_classifier() and thus EI's own
 * postprocessing entirely. Output tensor is INT8[1,8,8,4] (confirmed via
 * the same --dump-after-import) - an 8x8 grid, 4 channels per cell:
 * channel 0 is FOMO's implicit "background" class (skipped, matching
 * EI's own `for (ix = 1; ix < label_count+1; ix++)` convention),
 * channels 1..3 map to ei_classifier_inferencing_categories[0..2]
 * ("closed_eye"/"open_eye"/"yawning" - model_variables.h). Adjacent
 * same-class detected cells get merged into one box the same way EI's
 * ei_cube_check_overlap() does, just with fixed-size arrays instead of
 * EI's std::vector (this model's grid is small - 64 cells, 3 classes -
 * so a fixed AI_MODEL_MAX_BOXES-sized array is enough).
 */
#include "model_runner.h"
#include "fsl_debug_console.h"
#include "fsl_common.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/neutron/neutron.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "neutron/tflite_learn_1094697_39_npu.h"

/* Model's own fixed shapes/labels - not read from EI's model_metadata.h
 * on this path (that's EI-SDK specific), hard-coded to match this exact
 * exported model instead. Grid = input / 8 (FOMO's fixed stride for this
 * backbone), confirmed via the tensor dump: INT8[1,64,64,3] in,
 * INT8[1,8,8,4] out. */
#define NPU_MODEL_INPUT_WIDTH 64U
#define NPU_MODEL_INPUT_HEIGHT 64U
#define NPU_MODEL_GRID_WIDTH 8U
#define NPU_MODEL_GRID_HEIGHT 8U
#define NPU_MODEL_CLASS_COUNT 3U /* +1 implicit background channel in the raw tensor */
#define NPU_MODEL_DETECTION_THRESHOLD 0.5f /* matches model_variables.h's .threshold */

static const char *const s_labels[NPU_MODEL_CLASS_COUNT] = {"closed_eye", "open_eye", "yawning"};

/* Sized over the converter's own report for this model (~99.8KB for the
 * NeutronGraph's own input+output+scratch+weights) to leave room for
 * TFLM's own bookkeeping + the Softmax op's buffers. Constrained by
 * m_data's free space (this project's non-NPU baseline already uses
 * ~186KB of the 312KB region for camera/LCD buffers etc. - see
 * WORKLOG.md "Bug #2"/"Bug #3" for that budget) - 160KB overflowed
 * m_data by ~36KB on the first attempt; 112KB fits with some margin.
 * If AllocateTensors() fails at runtime (logged clearly below), the
 * arena is too small for real bookkeeping overhead, not the linker
 * budget - shrink something else in m_data first before just bumping
 * this further. */
constexpr int kTensorArenaSize = 112 * 1024;
static uint8_t s_tensorArena[kTensorArenaSize] __attribute__((aligned(16)));

static const tflite::Model *s_model = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;

/* One detected grid cell before merging into a box - mirrors EI's
 * ei_classifier_cube_t, fixed-size instead of heap/vector-allocated. */
typedef struct
{
    uint16_t x, y;     /* grid cell coords, not pixels */
    uint16_t width, height; /* in grid cells */
    float confidence;
    const char *label;
} npu_cube_t;

static npu_cube_t s_cubes[NPU_MODEL_GRID_WIDTH * NPU_MODEL_GRID_HEIGHT];
static uint32_t s_cubeCount;

/* Ported from ei_cube_check_overlap() (ei_postprocessing_common.h) - if
 * (x,y,w,h) overlaps cube c (same class, checked by caller), grow c to
 * cover both and return true; otherwise return false untouched. */
static bool NPU_CubeCheckOverlap(npu_cube_t *c, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                 float confidence)
{
    bool isOverlapping = !((c->x + c->width < x) || (c->y + c->height < y) || (c->x > x + width) ||
                           (c->y > y + height));
    if (!isOverlapping)
    {
        return false;
    }

    if (x < c->x)
    {
        c->width += c->x - x;
        c->x = x;
    }
    if (y < c->y)
    {
        c->height += c->y - y;
        c->y = y;
    }
    if ((x + width) > (c->x + c->width))
    {
        c->width += (x + width) - (c->x + c->width);
    }
    if ((y + height) > (c->y + c->height))
    {
        c->height += (y + height) - (c->y + c->height);
    }
    if (confidence > c->confidence)
    {
        c->confidence = confidence;
    }
    return true;
}

/* Ported from ei_handle_cube() - single-cell detection, merged into an
 * existing same-class cube if adjacent/overlapping, otherwise a new
 * 1x1 cube. Silently drops detections past the fixed array size (would
 * only happen with a much bigger/denser grid than this model has). */
static void NPU_HandleCube(uint16_t x, uint16_t y, float confidence, const char *label)
{
    if (confidence < NPU_MODEL_DETECTION_THRESHOLD)
    {
        return;
    }

    for (uint32_t i = 0; i < s_cubeCount; i++)
    {
        if (s_cubes[i].label != label)
        {
            continue;
        }
        if (NPU_CubeCheckOverlap(&s_cubes[i], x, y, 1U, 1U, confidence))
        {
            return;
        }
    }

    if (s_cubeCount < (sizeof(s_cubes) / sizeof(s_cubes[0])))
    {
        s_cubes[s_cubeCount].x = x;
        s_cubes[s_cubeCount].y = y;
        s_cubes[s_cubeCount].width = 1U;
        s_cubes[s_cubeCount].height = 1U;
        s_cubes[s_cubeCount].confidence = confidence;
        s_cubes[s_cubeCount].label = label;
        s_cubeCount++;
    }
}

/* Same DWT cycle-counter timing as model_runner.cpp's AI_MODEL_InitTiming()
 * - kept identical so the two paths' "total classifier time" prints are
 * directly comparable (see WORKLOG.md "NPU (Neutron) plan" Phase 6). */
static void AI_MODEL_InitTiming(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

extern "C" void AI_MODEL_Init(void)
{
    AI_MODEL_InitTiming();

    s_model = tflite::GetModel(model_data);
    if (s_model->version() != TFLITE_SCHEMA_VERSION)
    {
        PRINTF("AI_MODEL_Init: NPU model schema version %d != supported %d\r\n", (int)s_model->version(),
               TFLITE_SCHEMA_VERSION);
        return;
    }

    /* Op resolver exactly as suggested in the comment neutron_converter
     * itself generated at the top of tflite_learn_1094697_39_npu.h. */
    static tflite::MicroMutableOpResolver<2> s_opResolver;
    s_opResolver.AddSoftmax();
    s_opResolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());

    static tflite::MicroInterpreter s_staticInterpreter(s_model, s_opResolver, s_tensorArena, kTensorArenaSize);
    s_interpreter = &s_staticInterpreter;

    if (s_interpreter->AllocateTensors() != kTfLiteOk)
    {
        PRINTF("AI_MODEL_Init: AllocateTensors() failed - kTensorArenaSize (%d bytes) is too small, "
               "bump it and rebuild\r\n",
               kTensorArenaSize);
        s_interpreter = nullptr;
        return;
    }

    PRINTF("AI_MODEL_Init: Neutron NPU FOMO ready (%dx%d input, %d classes, arena used %u/%u bytes)\r\n",
           NPU_MODEL_INPUT_WIDTH, NPU_MODEL_INPUT_HEIGHT, (int)NPU_MODEL_CLASS_COUNT,
           (unsigned)s_interpreter->arena_used_bytes(), (unsigned)kTensorArenaSize);
}

extern "C" void AI_MODEL_RunInference(const uint16_t *pixels, uint16_t width, uint16_t height,
                                       ai_model_result_t *result)
{
    result->valid = false;
    result->boxCount = 0;

    if (s_interpreter == nullptr)
    {
        PRINTF("AI_MODEL_RunInference: interpreter not initialized (AllocateTensors() failed earlier)\r\n");
        return;
    }

    /* Resize (nearest-neighbor squash, same math as model_runner.cpp's
     * get_signal_data()) straight into the int8 NHWC input tensor. */
    TfLiteTensor *inputTensor = s_interpreter->input(0);
    int8_t *inputData = inputTensor->data.int8;

    for (uint16_t ty = 0; ty < NPU_MODEL_INPUT_HEIGHT; ty++)
    {
        uint16_t sy = (uint16_t)(((uint32_t)ty * height) / NPU_MODEL_INPUT_HEIGHT);
        for (uint16_t tx = 0; tx < NPU_MODEL_INPUT_WIDTH; tx++)
        {
            uint16_t sx = (uint16_t)(((uint32_t)tx * width) / NPU_MODEL_INPUT_WIDTH);
            uint16_t px = pixels[(uint32_t)sy * width + sx];

            uint8_t r = (uint8_t)(((px >> 11) & 0x1FU) << 3);
            uint8_t g = (uint8_t)(((px >> 5) & 0x3FU) << 2);
            uint8_t b = (uint8_t)((px & 0x1FU) << 3);

            size_t base = ((size_t)ty * NPU_MODEL_INPUT_WIDTH + tx) * 3U;
            inputData[base + 0U] = (int8_t)((int)r - 128);
            inputData[base + 1U] = (int8_t)((int)g - 128);
            inputData[base + 2U] = (int8_t)((int)b - 128);
        }
    }

    uint32_t cycStart = DWT->CYCCNT;
    TfLiteStatus invokeStatus = s_interpreter->Invoke();
    uint32_t elapsedCycles = DWT->CYCCNT - cycStart;
    uint32_t elapsedUs = (uint32_t)(((uint64_t)elapsedCycles * 1000000ULL) / SystemCoreClock);
    PRINTF("AI_MODEL_RunInference: total classifier time = %uus (%ums)\r\n", (unsigned)elapsedUs,
           (unsigned)(elapsedUs / 1000U));

    if (invokeStatus != kTfLiteOk)
    {
        PRINTF("AI_MODEL_RunInference: Invoke() failed\r\n");
        return;
    }

    /* FOMO grid decode - see the file-level comment above for the
     * channel/threshold/merge conventions ported from EI's own
     * process_fomo_i8(). */
    TfLiteTensor *outputTensor = s_interpreter->output(0);
    const int8_t *outputData = outputTensor->data.int8;
    float outScale = outputTensor->params.scale;
    int32_t outZeroPoint = outputTensor->params.zero_point;

    s_cubeCount = 0;
    for (uint16_t gy = 0; gy < NPU_MODEL_GRID_HEIGHT; gy++)
    {
        for (uint16_t gx = 0; gx < NPU_MODEL_GRID_WIDTH; gx++)
        {
            size_t base = ((size_t)gy * NPU_MODEL_GRID_WIDTH + gx) * (NPU_MODEL_CLASS_COUNT + 1U);
            for (uint32_t cls = 0; cls < NPU_MODEL_CLASS_COUNT; cls++)
            {
                int8_t q = outputData[base + 1U + cls]; /* +1: skip background channel 0 */
                float confidence = (float)((int32_t)q - outZeroPoint) * outScale;
                NPU_HandleCube(gx, gy, confidence, s_labels[cls]);
            }
        }
    }

    /* Merge overlapping/adjacent same-class cubes into boxes, same
     * quadratic-but-tiny-N approach as EI's process_cubes(). Grid cell
     * size in pixels = model input / grid (8 for this model). */
    const uint16_t cellPixels = NPU_MODEL_INPUT_WIDTH / NPU_MODEL_GRID_WIDTH;
    uint32_t boxCount = 0;

    for (uint32_t i = 0; i < s_cubeCount && boxCount < AI_MODEL_MAX_BOXES; i++)
    {
        bool merged = false;
        for (uint32_t b = 0; b < boxCount; b++)
        {
            ai_bbox_t *box = &result->boxes[b];
            if (box->label != s_cubes[i].label)
            {
                continue;
            }
            uint16_t bx = box->x / cellPixels;
            uint16_t by = box->y / cellPixels;
            uint16_t bw = box->width / cellPixels;
            uint16_t bh = box->height / cellPixels;
            npu_cube_t asBox = {bx, by, bw, bh, box->score, box->label};
            if (NPU_CubeCheckOverlap(&asBox, s_cubes[i].x, s_cubes[i].y, s_cubes[i].width, s_cubes[i].height,
                                     s_cubes[i].confidence))
            {
                box->x = (uint16_t)(asBox.x * cellPixels);
                box->y = (uint16_t)(asBox.y * cellPixels);
                box->width = (uint16_t)(asBox.width * cellPixels);
                box->height = (uint16_t)(asBox.height * cellPixels);
                box->score = asBox.confidence;
                merged = true;
                break;
            }
        }

        if (!merged)
        {
            ai_bbox_t *box = &result->boxes[boxCount];
            box->label = s_cubes[i].label;
            box->x = (uint16_t)(s_cubes[i].x * cellPixels);
            box->y = (uint16_t)(s_cubes[i].y * cellPixels);
            box->width = (uint16_t)(s_cubes[i].width * cellPixels);
            box->height = (uint16_t)(s_cubes[i].height * cellPixels);
            box->score = s_cubes[i].confidence;
            boxCount++;
        }
    }

    result->boxCount = boxCount;
    result->valid = (boxCount > 0U);
}

extern "C" uint16_t AI_MODEL_GetInputWidth(void)
{
    return NPU_MODEL_INPUT_WIDTH;
}

extern "C" uint16_t AI_MODEL_GetInputHeight(void)
{
    return NPU_MODEL_INPUT_HEIGHT;
}
