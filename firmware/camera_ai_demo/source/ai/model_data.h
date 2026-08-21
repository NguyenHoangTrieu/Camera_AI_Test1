/*
 * model_data.h - placeholder for an exported TFLite Micro model.
 *
 * Replace this file with the output of:
 *   xxd -i your_model.tflite > model_data.h
 * (or the .tflite->C array converter of your choice), which defines an
 * array like `unsigned char your_model_tflite[]` and a matching `_len`
 * constant. Update the names below to match, then use them in
 * model_runner.c / model_runner.cpp when constructing the TFLM interpreter.
 */
#ifndef _MODEL_DATA_H_
#define _MODEL_DATA_H_

/* No model linked yet - see model_runner.c for integration instructions. */
static const unsigned char g_model_data[] = {0x00};
static const unsigned int g_model_data_len = 0;

#endif /* _MODEL_DATA_H_ */
