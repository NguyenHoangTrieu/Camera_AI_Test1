/*
 * model_runner.c - stub implementation, replace with a real model.
 *
 * ============================================================================
 * Option A: TensorFlow Lite Micro (matches "tflite" in requirement.md)
 * ============================================================================
 * The SDK already ships a working TFLM camera-less image-classification demo
 * at examples/eiq_examples/tflm_label_image (see mcuxsdk/mcuxsdk/examples/
 * eiq_examples/tflm_label_image/main.cpp for the exact API calls: it builds a
 * tflite::MicroInterpreter, allocates a tensor arena, copies the image into
 * the input tensor, calls Invoke(), then reads output->data.uint8/f).
 * To wire that pattern in here:
 *   1. Export your trained model as a C array (xxd -i model.tflite > model_data.h)
 *      and drop it next to this file, replacing source/ai/model_data.h.
 *   2. Add the TFLM sources to CMakeLists.txt the same way tflm_label_image's
 *      CMakeLists.txt does (examples/eiq_examples/common/tflm/model.cpp,
 *      get_top_n.cpp, etc. - see that file for the exact source list).
 *   3. Turn this file into model_runner.cpp (TFLM is C++), build a
 *      tflite::MicroInterpreter in AI_MODEL_Init(), and call Invoke() in
 *      AI_MODEL_RunInference(). Keep model_runner.h's C API unchanged (wrap
 *      the C++ interpreter in `extern "C"` functions) so main.c doesn't change.
 *
 * ============================================================================
 * Option B: Edge Impulse (matches "edge impulse" in requirement.md)
 * ============================================================================
 * Export your Edge Impulse project as a "C++ library" (Deployment tab), drop
 * the exported edge-impulse-sdk/ + model-parameters/ + tflite-model/ folders
 * under source/ai/edge_impulse/, add them to CMakeLists.txt, and call
 * run_classifier() with a signal_t that reads from the `pixels` buffer below
 * (EI's camera examples show the RGB565->RGB888->float conversion needed).
 *
 * Either way, main.c and model_runner.h do not need to change - only this
 * .c file (or its .cpp replacement) and CMakeLists.txt do.
 */

#include "model_runner.h"
#include "fsl_debug_console.h"

static bool s_warned = false;

void AI_MODEL_Init(void)
{
    PRINTF("AI_MODEL_Init: no model linked yet (model_runner.c is a stub).\r\n");
    PRINTF("  See the comment at the top of source/ai/model_runner.c to plug in\r\n");
    PRINTF("  a TensorFlow Lite Micro or Edge Impulse model.\r\n");
}

void AI_MODEL_RunInference(const uint16_t *pixels, uint16_t width, uint16_t height, ai_model_result_t *result)
{
    (void)pixels;
    (void)width;
    (void)height;

    if (!s_warned)
    {
        PRINTF("AI_MODEL_RunInference: stub, always returns \"no result\".\r\n");
        s_warned = true;
    }

    result->valid   = false;
    result->classId = -1;
    result->score   = 0.0f;
}
