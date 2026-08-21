/*
 * camera_capture.c - see camera_capture.h
 *
 * Adapted from the camera portion of the NXP example
 * `display_examples/smartdma_camera_flexio_mculcd` (board port for
 * frdmmcxn947), which drives the same OV7670-on-J9 hardware described in
 * requirement.md.
 */

#include <string.h>
#include "camera_capture.h"
#include "app.h"
#include "board.h"
#include "fsl_ov7670.h"
#include "fsl_smartdma.h"
#include "fsl_smartdma_prv.h"
#include "fsl_debug_console.h"

static const ov7670_resource_t s_cameraResource = {
    .i2cSendFunc    = BOARD_Camera_I2C_SendSCCB,
    .i2cReceiveFunc = BOARD_Camera_I2C_ReceiveSCCB,
    .xclock         = kOV7670_InputClock24MHZ,
};

static camera_device_handle_t s_cameraHandle = {
    .resource = (void *)&s_cameraResource,
    .ops      = &ov7670_ops,
};

static volatile bool s_frameReady = false;
static volatile uint32_t s_frameCount = 0;
static uint16_t s_frameBuffer[DEMO_BUFFER_WIDTH * DEMO_BUFFER_HEIGHT];
static uint8_t s_smartdmaStack[32];

static void CAMERA_CAPTURE_CompleteCallback(void *param)
{
    (void)param;
    s_frameReady = true;
    s_frameCount++;
}

static void CAMERA_CAPTURE_InitDevice(void)
{
    camera_config_t camConfig = {
        .pixelFormat                = kVIDEO_PixelFormatRGB565,
        .resolution                 = DEMO_CAMERA_RESOLUTION,
        .framePerSec                = 30,
        .interface                  = kCAMERA_InterfaceGatedClock,
        .frameBufferLinePitch_Bytes = 0,
        .controlFlags               = 0,
        .bytesPerPixel              = 0,
        .mipiChannel                = 0,
        .csiLanes                   = 0,
    };

    BOARD_Camera_I2C_Init();

    /* CAMERA_DEVICE_Init() -> OV7670_Init() reads back the PID/VER
     * registers over SCCB and checks them against the fixed OV7670 values
     * (0x76 / 0x73) before doing anything else, so success here is a real
     * "camera is alive and answering on J9's I2C/SCCB" confirmation, not
     * just "no bus error". */
    if (CAMERA_DEVICE_Init(&s_cameraHandle, &camConfig) != kStatus_Success)
    {
        PRINTF("Camera: OV7670 NOT detected on J9 SCCB (PID/VER register\r\n");
        PRINTF("  readback failed or mismatched) - check J9 wiring/rework\r\n");
        PRINTF("  (SJ16/SJ26/SJ27) in README.md.\r\n");
        while (1)
        {
        }
    }

    PRINTF("Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), %ux%u @ %u fps.\r\n",
           DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, camConfig.framePerSec);
}

static void CAMERA_CAPTURE_InitSmartDma(void)
{
    static smartdma_camera_param_t smartdmaParam;

    memset((void *)s_frameBuffer, 0, sizeof(s_frameBuffer));

    SMARTDMA_InitWithoutFirmware();
    SMARTDMA_InstallFirmware(SMARTDMA_CAMERA_MEM_ADDR, s_smartdmaCameraFirmware, SMARTDMA_CAMERA_FIRMWARE_SIZE);
    SMARTDMA_InstallCallback(CAMERA_CAPTURE_CompleteCallback, NULL);
    NVIC_EnableIRQ(SMARTDMA_IRQn);
    NVIC_SetPriority(SMARTDMA_IRQn, 3);

    smartdmaParam.smartdma_stack = (uint32_t *)s_smartdmaStack;
    smartdmaParam.p_buffer       = (uint32_t *)s_frameBuffer;
    SMARTDMA_Boot(DEMO_SMARTDMA_API, &smartdmaParam, 0x2);
}

void CAMERA_CAPTURE_Init(void)
{
    CAMERA_CAPTURE_InitDevice();
    CAMERA_CAPTURE_InitSmartDma();
}

bool CAMERA_CAPTURE_IsFrameReady(void)
{
    return s_frameReady;
}

void CAMERA_CAPTURE_ClearFrameReady(void)
{
    s_frameReady = false;
}

uint16_t *CAMERA_CAPTURE_GetFrameBuffer(void)
{
    return s_frameBuffer;
}

uint32_t CAMERA_CAPTURE_GetFrameCount(void)
{
    return s_frameCount;
}
