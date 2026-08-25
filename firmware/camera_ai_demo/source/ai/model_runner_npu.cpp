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
 * Model: neutron/tflite_learn_1095726_3_npu.h - a single-class `face`
 * FOMO detector (Face_Detection_NXP, project ID 1095726, deploy version 2
 * - retrained 2026-08-25 with a smaller FOMO backbone, 72x72 input, after
 * the deploy-version-1 model's 96x96/alpha=0.35 config turned out too big
 * to fit this chip's available RAM on either backend, see WORKLOG.md's
 * top entry for the full incident), run through `neutron_converter
 * --target mcxn94x` (same tool/target as the original 3-class model, see
 * WORKLOG.md "Phase 2"). 31 of 33 ops got folded into one NEUTRON_GRAPH
 * custom op; the two that stayed regular TFLM ops are Slice (this
 * model's NPU output channel dim comes back padded to 4 - [1,9,9,4] -
 * and needs slicing down to the real 2 channels, [1,9,9,2]; the original
 * 3-class model's channel count happened to not need this) and Softmax -
 * confirmed via the converter's own generated header comment, reproduced
 * below.
 *
 * Preprocessing (resize + quantize) is the same nearest-neighbor "squash"
 * used in model_runner.cpp's get_signal_data(), just writing straight
 * into an int8 NHWC tensor instead of Edge Impulse's packed-float
 * 0xRRGGBB signal format. Input quantization here (scale=0.003922,
 * zero_point=-128, confirmed via a Python flatbuffer dump of the
 * converted model) works out to the trivial `q = channel_value - 128`
 * (scale is ~1/255, so one pixel unit is one quantized unit) - same as
 * the earlier model, this is a property of how Edge Impulse quantizes
 * image inputs in general, not specific to either model.
 *
 * Postprocessing (FOMO grid decode) is hand-rolled, ported from Edge
 * Impulse's own process_fomo_i8()/ei_handle_cube()/process_cubes()
 * (edge-impulse-sdk/classifier/postprocessing/ei_postprocessing_common.h)
 * since this path skips ei_run_classifier() and thus EI's own
 * postprocessing entirely. Output tensor (post-Slice, post-Softmax, so
 * this is what interpreter->output(0) returns) is INT8[1,9,9,2] - a
 * 9x9 grid (72x72 input / 8, FOMO's fixed stride for this backbone,
 * same stride the earlier 64x64/96x96 models used), 2 channels per cell:
 * channel 0 is FOMO's implicit "background" class (skipped, matching
 * EI's own `for (ix = 1; ix < label_count+1; ix++)` convention), channel
 * 1 is the single `face` class. Adjacent same-class detected cells get
 * merged into one box the same way EI's ei_cube_check_overlap() does,
 * just with fixed-size arrays instead of EI's std::vector (this model's
 * grid is small - 81 cells, 1 class - so a fixed AI_MODEL_MAX_BOXES-
 * sized array is enough).
 */
#include "model_runner.h"
#include "fsl_debug_console.h"
#include "fsl_common.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/neutron/neutron.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "neutron/tflite_learn_1095726_3_npu.h"

/* Model's own fixed shapes/labels - not read from EI's model_metadata.h
 * on this path (that's EI-SDK specific), hard-coded to match this exact
 * exported model instead. Grid = input / 8 (FOMO's fixed stride for this
 * backbone), confirmed via a Python flatbuffer dump: INT8[1,72,72,3] in,
 * INT8[1,9,9,2] out (post-Slice/Softmax). */
#define NPU_MODEL_INPUT_WIDTH 72U
#define NPU_MODEL_INPUT_HEIGHT 72U
#define NPU_MODEL_GRID_WIDTH 9U
#define NPU_MODEL_GRID_HEIGHT 9U
#define NPU_MODEL_CLASS_COUNT 1U /* +1 implicit background channel in the raw tensor */
#define NPU_MODEL_DETECTION_THRESHOLD 0.5f /* matches model_variables.h's .threshold */

static const char *const s_labels[NPU_MODEL_CLASS_COUNT] = {"face"};

/* Sized over the converter's own report for this model's NeutronGraph
 * node (inputs 15,552 + NeutronGraph-internal outputs 324 + scratch
 * 77,760 = 93,636 bytes), plus ~28% margin for the Slice/Softmax
 * intermediate tensors and TFLM's own per-tensor bookkeeping - the same
 * proportional margin the project's very first (64x64, 3-class) NPU
 * model used (99.8KB estimate -> 112KB arena). Comfortably fits m_data
 * alongside the 153,600-byte camera frame buffer (153,600 + 122,880 =
 * 276,480 of 319,488 bytes stock m_data, ~43KB spare) - unlike the
 * deploy-version-1 96x96 model, which needed >=166KB here and did not
 * fit at all (see WORKLOG.md's top entry). Still not confirmed against
 * a real AllocateTensors() run on hardware as of writing - if it fails
 * at runtime (logged clearly below), there's plenty of headroom in
 * m_data to bump this further, unlike the v1 model's situation. */
constexpr int kTensorArenaSize = 120 * 1024;
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
     * itself generated at the top of tflite_learn_1095726_3_npu.h - one
     * more op (Slice) than the earlier 3-class model needed, see the
     * file-level comment above for why. */
    static tflite::MicroMutableOpResolver<3> s_opResolver;
    s_opResolver.AddSlice();
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

    PRINTF("AI_MODEL_Init: Neutron NPU face detector ready (%dx%d input, %d class(es), arena used %u/%u bytes)\r\n",
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
