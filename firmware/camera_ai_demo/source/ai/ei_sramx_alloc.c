/*
 * ei_sramx_alloc.c - overrides Edge Impulse SDK's weak ei_malloc/ei_calloc/
 * ei_free (see edge_impulse/edge-impulse-sdk/porting/clib/
 * ei_classifier_porting.cpp) to allocate from a static pool placed in
 * m_sramx instead of the default heap.
 *
 * m_data (main SRAM) is ~99% full (camera + LCD framebuffers + stack) with
 * essentially no free room - nowhere near what the tensor arena + DSP
 * scratch buffers actually need. m_sramx is a separate, otherwise-unused
 * 96KB SRAM bank (see board_port/ei_sramx.ld for how it's wired in).
 *
 * Two-tier allocator: the primary pool (m_sramx) is tried first; once
 * exhausted, an optional "overflow" pool - lent in via
 * EI_SRAMX_SetOverflowPool(), see ei_sramx_alloc.h - is used instead.
 * main.c lends it s_lcdSnapshot's memory (150KB, otherwise idle exactly
 * during the AI_MODEL_RunInference() call - see the ordering note in
 * ei_sramx_alloc.h).
 *
 * This is a bump allocator with a small LIFO free-record stack, not a
 * general-purpose one. The tensor arena itself is allocated once and kept
 * for the whole inference call, but the DSP image-resize step
 * (extract_image_features_quantized(), ei_run_dsp.h) allocates and frees a
 * scratch buffer (matrix_t) once per ~1024-pixel page it reads - for a
 * 320x240 frame that's ~75 alloc/free cycles in *one* AI_MODEL_RunInference()
 * call. An earlier version of this file made ei_free() a no-op on the
 * (wrong) assumption of "allocate once, free once" - that leaked every one
 * of those ~75 page buffers instead of reusing the space, blew through
 * both pools well before the DSP step finished, and crashed with a
 * precise bus fault at the exact byte past the end of m_sramx once the
 * SDK's own bounds-unchecked arithmetic ran off the end of a NULL/short
 * allocation. Tracking each allocation's pool + prior offset and rewinding
 * on a matching free() (strict LIFO - matches this SDK's actual usage
 * pattern of nested alloc/use/free, not free-in-any-order) fixes this
 * without needing a real heap.
 */
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "fsl_debug_console.h"
#include "ei_sramx_alloc.h"

typedef struct
{
    uint8_t *ptr;
    size_t prevOffset;
    uint8_t fromOverflow;
} alloc_record_t;

/* Generous margin over the ~75 DSP page-read cycles per inference call,
 * plus a handful of longer-lived allocations (tensor arena, outputs
 * array, small scratch buffers). Recorded in m_sramx too (see
 * EI_SRAMX_POOL_SIZE below), so this is not "free" - kept modest. */
#define EI_SRAMX_MAX_RECORDS 128U
#define EI_SRAMX_RECORDS_BYTES (EI_SRAMX_MAX_RECORDS * sizeof(alloc_record_t))

/* Matches EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE (~92876 bytes) plus room
 * for auxiliary allocations - sized to the full m_sramx region (96KB)
 * minus the record stack above, since nothing else uses that bank. */
#define EI_SRAMX_POOL_SIZE ((96U * 1024U) - EI_SRAMX_RECORDS_BYTES)

__attribute__((section(".ei_sramx"), aligned(16))) static uint8_t s_pool[EI_SRAMX_POOL_SIZE];
__attribute__((section(".ei_sramx"), aligned(16))) static alloc_record_t s_records[EI_SRAMX_MAX_RECORDS];
static size_t s_recordCount = 0;

static size_t s_offset = 0;
static size_t s_highWaterMark = 0;

static uint8_t *s_overflowPool = NULL;
static size_t s_overflowPoolSize = 0;
static size_t s_overflowOffset = 0;
static size_t s_overflowHighWaterMark = 0;

void EI_SRAMX_PoolReset(void)
{
    s_offset = 0;
    s_overflowOffset = 0;
    s_recordCount = 0;
}

void EI_SRAMX_SetOverflowPool(void *ptr, size_t size)
{
    s_overflowPool = (uint8_t *)ptr;
    s_overflowPoolSize = size;
}

/* Exposed for diagnostics - see PRINTF in model_runner.cpp. */
size_t EI_SRAMX_GetHighWaterMark(void)
{
    return s_highWaterMark + s_overflowHighWaterMark;
}

static void *sramx_alloc(size_t size)
{
    size_t aligned = (size + 15U) & ~(size_t)15U;
    void *p = NULL;
    uint8_t fromOverflow;
    size_t prevOffset;

    if (s_offset + aligned <= EI_SRAMX_POOL_SIZE)
    {
        prevOffset = s_offset;
        p = &s_pool[s_offset];
        s_offset += aligned;
        if (s_offset > s_highWaterMark)
        {
            s_highWaterMark = s_offset;
        }
        fromOverflow = 0U;
    }
    else if ((s_overflowPool != NULL) && (s_overflowOffset + aligned <= s_overflowPoolSize))
    {
        prevOffset = s_overflowOffset;
        p = &s_overflowPool[s_overflowOffset];
        s_overflowOffset += aligned;
        if (s_overflowOffset > s_overflowHighWaterMark)
        {
            s_overflowHighWaterMark = s_overflowOffset;
        }
        fromOverflow = 1U;
    }
    else
    {
        PRINTF("EI_SRAMX: alloc of %u bytes failed (pool=%u/%u, overflow=%u/%u)\r\n", (unsigned)size,
               (unsigned)s_offset, (unsigned)EI_SRAMX_POOL_SIZE, (unsigned)s_overflowOffset,
               (unsigned)s_overflowPoolSize);
        return NULL;
    }

    if (s_recordCount < EI_SRAMX_MAX_RECORDS)
    {
        s_records[s_recordCount].ptr = (uint8_t *)p;
        s_records[s_recordCount].prevOffset = prevOffset;
        s_records[s_recordCount].fromOverflow = fromOverflow;
        s_recordCount++;
    }
    /* else: record stack full - this allocation can't be reclaimed by
     * ei_free() until the next EI_SRAMX_PoolReset(), same as the old
     * no-op behavior, just for this one block instead of all of them. */

    return p;
}

void *ei_malloc(size_t size)
{
    return sramx_alloc(size);
}

void *ei_calloc(size_t nitems, size_t size)
{
    void *p = sramx_alloc(nitems * size);
    if (p != NULL)
    {
        memset(p, 0, nitems * size);
    }
    return p;
}

void ei_free(void *ptr)
{
    /* Only reclaims space if freeing the most recent still-tracked
     * allocation (strict LIFO) - matches this SDK's actual alloc/use/free
     * pattern. Freeing anything else (or a pointer that fell off the
     * record stack, see EI_SRAMX_MAX_RECORDS) is a safe no-op: the space
     * simply isn't reclaimed until EI_SRAMX_PoolReset(). */
    if ((ptr == NULL) || (s_recordCount == 0))
    {
        return;
    }

    alloc_record_t *top = &s_records[s_recordCount - 1];
    if (top->ptr != (uint8_t *)ptr)
    {
        return;
    }

    if (top->fromOverflow)
    {
        s_overflowOffset = top->prevOffset;
    }
    else
    {
        s_offset = top->prevOffset;
    }
    s_recordCount--;
}
