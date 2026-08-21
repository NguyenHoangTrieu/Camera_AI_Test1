/*
 * model_runner.h - AI inference scaffold
 *
 * This is the extension point requested in requirement.md ("khuon code de
 * tich hop model AI ve sau"): a small, framework-agnostic C API that
 * main.c calls once per captured frame. The .c file is currently a stub
 * (always returns "no model") - see model_runner.c for how to wire in a
 * real TensorFlow Lite Micro or Edge Impulse model without changing main.c.
 */
#ifndef _MODEL_RUNNER_H_
#define _MODEL_RUNNER_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    bool valid;      /* false until a model is actually wired in */
    int32_t classId; /* index into your label list */
    float score;     /* 0..1 confidence */
} ai_model_result_t;

/*! @brief One-time setup (allocate tensor arena, load model, etc). */
void AI_MODEL_Init(void);

/*!
 * @brief Run inference on one RGB565 frame.
 *
 * @param pixels RGB565 frame, `width * height` pixels.
 * @param width  frame width in pixels.
 * @param height frame height in pixels.
 * @param result output classification/detection result.
 */
void AI_MODEL_RunInference(const uint16_t *pixels, uint16_t width, uint16_t height, ai_model_result_t *result);

#endif /* _MODEL_RUNNER_H_ */
