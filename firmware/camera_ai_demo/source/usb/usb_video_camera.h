/*
 * usb_video_camera.h - USB Video Class (UVC) device streaming the OV7670
 * camera over USB High-Speed as a standard uncompressed-YUY2 webcam.
 *
 * Adapted from mcuxsdk's usb_device_video_virtual_camera example
 * (examples/usb_examples/usb_device_video_virtual_camera/bm/virtual_camera.h).
 */

#ifndef __USB_VIDEO_CAMERA_H__
#define __USB_VIDEO_CAMERA_H__

/* Self-contained: pulls in its own USB stack dependencies so any file can
 * just #include "usb_video_camera.h" without having to know (and get the
 * order right) that usb_device_class.h/usb_device_video.h/
 * usb_device_descriptor.h have to come first. */
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_device_video.h"
#include "usb_device_descriptor.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerEhci0
#endif
#endif
#if defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U)
#ifndef CONTROLLER_ID
#define CONTROLLER_ID kUSB_ControllerKhci0
#endif
#endif

#if defined(__GIC_PRIO_BITS)
#define USB_DEVICE_INTERRUPT_PRIORITY (25U)
#elif defined(__NVIC_PRIO_BITS) && (__NVIC_PRIO_BITS >= 3)
#define USB_DEVICE_INTERRUPT_PRIORITY (6U)
#else
#define USB_DEVICE_INTERRUPT_PRIORITY (3U)
#endif

typedef struct _usb_video_virtual_camera_struct
{
    usb_device_handle deviceHandle;
    class_handle_t videoHandle;
    uint32_t currentMaxPacketSize;
    uint8_t *imageBuffer;
    uint32_t imageBufferLength;
    usb_device_video_probe_and_commit_controls_struct_t *probeStruct;
    usb_device_video_probe_and_commit_controls_struct_t *commitStruct;
    uint32_t framePixelOffset; /* how many source pixels of the current frame have been sent so far */
    uint32_t *classRequestBuffer;
    uint8_t currentFrameId;
    uint8_t currentStreamInterfaceAlternateSetting;
    uint8_t probeLength;
    uint8_t commitLength;
    uint8_t probeInfo;
    uint8_t commitInfo;
    uint8_t currentConfiguration;
    uint8_t currentInterfaceAlternateSetting[USB_VIDEO_VIRTUAL_CAMERA_INTERFACE_COUNT];
    uint8_t speed;
    uint8_t attach;
} usb_video_virtual_camera_struct_t;

/*******************************************************************************
 * API
 ******************************************************************************/

/* MCXN947-specific SPC/SCG/SYSCON bring-up for the USB HS PHY's PLL - raises DCDC/CoreLDO to
 * Overdrive among other things (board_port/cm33_core0/hardware_init.c). Call once from main(),
 * after CAMERA_CAPTURE_Deinit() has cleanly stopped SmartDMA - see WORKLOG.md "time-multiplex
 * fallback" entry for why camera capture and USB HS can't be active at the same time on this
 * chip, and CAMERA_CAPTURE_Deinit()'s doc comment (camera_capture.h) for the ordering rule. */
void USB_DeviceClockInit(void);

/* Registers the UVC class, brings up the USB HS controller and starts the device running.
 * Call once from main(), after USB_DeviceClockInit(). Streaming to the host only actually
 * starts once a host selects the streaming alternate setting (i.e. an app opens the camera). */
void USB_VideoCamera_Init(void);

/* Pumps USB device stack housekeeping (control transfers, enumeration, etc). Call in the main
 * loop's while(1); actual frame payloads are produced from CAMERA_CAPTURE_GetFrameBuffer() on
 * demand from the USB IN-endpoint-complete callback, not from this function. */
void USB_VideoCamera_Task(void);

#endif /* __USB_VIDEO_CAMERA_H__ */
