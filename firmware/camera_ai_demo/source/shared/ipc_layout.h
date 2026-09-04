/*
 * ipc_layout.h - dual-core RAM layout constants (Camera_AI_Test1).
 *
 * The shared region below is carved out of core0's m_data, immediately
 * below core1's own 0x2004E000 region (see ARCHITECTURE.md / WORKLOG.md:
 * a QVGA RGB565 frame, 153,600 bytes, does not fit inside core1's 104KB
 * region at all - it has to live in core0's much larger m_data instead).
 * Only core0's linker script (board_port/cm33_core0/
 * MCXN947_cm33_core0_dualcore.ld) actually declares a MEMORY region/output
 * section for this range, so the linker can catch core0 overflowing into
 * it by accident. core1 does NOT need its own linker awareness of this
 * region - it just reads/writes through the fixed-address pointers below,
 * the same way any two cores share a physical RAM bank without either one
 * "owning" it at the toolchain level.
 *
 * IPC_SHARED_BASE/IPC_SHARED_SIZE MUST match
 * board_port/cm33_core0/MCXN947_cm33_core0_dualcore.ld's `m_shared` MEMORY
 * block exactly - there is no automated check tying the two together.
 */
#ifndef IPC_LAYOUT_H_
#define IPC_LAYOUT_H_

#include <stdint.h>

/* Physical placement: 0x20024000-0x2004E000, immediately below core1's own
 * 0x2004E000-0x20068000 region. core0's own m_data shrinks from its vendor
 * default (0x20000000, length 0x4E000) to (0x20000000, length 0x24000) to
 * make room. */
#define IPC_SHARED_BASE 0x20024000u
#define IPC_SHARED_SIZE 0x0002A000u /* 172KB */

/* Layout within the shared region. */
#define IPC_FRAME_BUFFER_OFFSET 0x00000000u
#define IPC_FRAME_BUFFER_SIZE   153600u /* 320*240*2 bytes, QVGA RGB565 - camera_capture.c's frame buffer */

#define IPC_CROP_BUFFER_OFFSET (IPC_FRAME_BUFFER_OFFSET + IPC_FRAME_BUFFER_SIZE)
#define IPC_CROP_BUFFER_SIZE   15552u /* 72*72*3 bytes, int8 NHWC - the AI model's actual input tensor size */

#define IPC_RESULT_OFFSET (IPC_CROP_BUFFER_OFFSET + IPC_CROP_BUFFER_SIZE)
#define IPC_RESULT_SIZE   256u /* sized generously for ai_ipc_result_t (source/shared/ipc_events.h) */

#define IPC_USED_SIZE (IPC_RESULT_OFFSET + IPC_RESULT_SIZE)

#if IPC_USED_SIZE > IPC_SHARED_SIZE
#error "ipc_layout.h: shared region contents overflow IPC_SHARED_SIZE - grow it here AND in MCXN947_cm33_core0_dualcore.ld's m_shared MEMORY block"
#endif

#define IPC_FRAME_BUFFER_ADDR ((void *)(uintptr_t)(IPC_SHARED_BASE + IPC_FRAME_BUFFER_OFFSET))
#define IPC_CROP_BUFFER_ADDR  ((void *)(uintptr_t)(IPC_SHARED_BASE + IPC_CROP_BUFFER_OFFSET))
#define IPC_RESULT_ADDR       ((void *)(uintptr_t)(IPC_SHARED_BASE + IPC_RESULT_OFFSET))

#endif /* IPC_LAYOUT_H_ */
