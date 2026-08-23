/*
 * model_runner.c - stub implementation, replace with a real model.
 *
 * Two ready-made integration paths (main.c and model_runner.h don't need
 * to change for either - only this file/its .cpp replacement and
 * CMakeLists.txt):
 *
 * - TensorFlow Lite Micro: follow the pattern in this SDK's
 *   examples/eiq_examples/tflm_label_image (tflite::MicroInterpreter +
 *   tensor arena + Invoke()). Export the model via
 *   `xxd -i model.tflite > model_data.h`, add the TFLM sources to
 *   CMakeLists.txt, and turn this file into model_runner.cpp (TFLM is
 *   C++, wrap it in `extern "C"` functions).
 * - Edge Impulse: export the project as a "C++ library" (Deployment tab),
 *   drop it under source/ai/edge_impulse/, add it to CMakeLists.txt, and
 *   call run_classifier() with a signal_t reading from `pixels` below.
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
