#ifndef R4_CLOCK64_H
#define R4_CLOCK64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R4 Clock64
 *
 * The STM32F446 DWT cycle counter is only 32 bits.  This module is the sole
 * extension state used by R4 logical time.  Callers that already own the R4
 * short transaction use ReadLocked(); all other callers use their platform
 * wrapper to protect the read before calling it.  A raw CYCCNT value must
 * never be combined with a second, private wrap extension.
 */

typedef enum
{
    R4_CLOCK64_OK = 0,
    R4_CLOCK64_INVALID_ARGUMENT,
    R4_CLOCK64_NOT_INITIALIZED,
    R4_CLOCK64_TIME_REGRESSION
} R4_Clock64Status;

typedef struct
{
    uint32_t initialized;
    uint32_t last_raw;
    uint32_t high_word;
    uint64_t last_time;
    uint64_t read_count;
    uint32_t wrap_count;
    R4_Clock64Status first_error;
} R4_Clock64;

/* Initialise from one protected raw read.  The caller owns the protection. */
R4_Clock64Status R4_Clock64_InitializeLocked(R4_Clock64 *clock, uint32_t raw);

/* Extend one protected CYCCNT read.  The function deliberately does not
 * manipulate PRIMASK: that lets RuntimeEvent use it inside its single atomic
 * transaction rather than creating nested, independently-restored masks. */
R4_Clock64Status R4_Clock64_ReadLocked(
    R4_Clock64 *clock,
    uint32_t raw,
    uint64_t *out_time);

R4_Clock64Status R4_Clock64_GetStatus(const R4_Clock64 *clock);

#ifdef __cplusplus
}
#endif

#endif /* R4_CLOCK64_H */
