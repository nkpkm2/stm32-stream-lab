#ifndef ADC_DBM_DRIVER_H
#define ADC_DBM_DRIVER_H

#include <stdint.h>

#include "adc_dbm_rebind_guard.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-2.
 *
 * This module is the future sole owner of the ADC1 / DMA2 Stream0 / TIM2
 * acquisition hardware lifecycle and hardware M0AR/M1AR writes. Historical
 * R1/R2 profiles remain immutable behavioral oracles until parity has been
 * established through a separate compatibility profile.
 *
 * Current foundation scope:
 *   - R2-compatible hardware configuration validation,
 *   - DBM arm with caller-provided M0/M1 addresses,
 *   - final TIM2 start commit,
 *   - bounded inactive-slot completion actions (KEEP / REBIND),
 *   - bounded hardware stop/quiescence,
 *   - diagnostic read-only hardware/state snapshots,
 *   - driver-owned DMA completion/error callback dispatch.
 *
 * Still outside this driver:
 *   - BufferPool ownership,
 *   - FreeBufferQueue / ReadyQueue,
 *   - R2_DmaSlots logical buffer-id mapping and mapping epoch,
 *   - admission/drop policy,
 *   - R3 RunContext / generation / START/STOP protocol / worker ACKs.
 *
 * Hardware/software commit boundary:
 *   A successful REBIND action proves only that the physical inactive MxAR
 *   write completed safely. The higher runtime layer may commit R2_DmaSlots
 *   logical mapping only AFTER AdcDbmDriver_ApplyInactiveAction() returns OK.
 *   A successful KEEP action proves the no-write DROP hardware invariant for
 *   that completion; it never authorizes a logical mapping advance.
 */

typedef enum
{
    ADC_DBM_DRIVER_STATE_UNINITIALIZED = 0,
    ADC_DBM_DRIVER_STATE_STOPPED,
    ADC_DBM_DRIVER_STATE_ARMED,
    ADC_DBM_DRIVER_STATE_RUNNING,
    ADC_DBM_DRIVER_STATE_STOPPING,
    ADC_DBM_DRIVER_STATE_ERROR
} AdcDbmDriverState;

typedef enum
{
    ADC_DBM_DRIVER_OK = 0,
    ADC_DBM_DRIVER_INVALID_ARGUMENT,
    ADC_DBM_DRIVER_INVALID_STATE,
    ADC_DBM_DRIVER_BUSY,
    ADC_DBM_DRIVER_CONFIG_ERROR,
    ADC_DBM_DRIVER_HAL_ERROR,
    ADC_DBM_DRIVER_HARDWARE_ERROR,
    ADC_DBM_DRIVER_COMPLETION_CONTEXT_ERROR,
    ADC_DBM_DRIVER_COMPLETION_GUARD_ERROR,
    ADC_DBM_DRIVER_COMPLETION_ADDRESS_ERROR,
    ADC_DBM_DRIVER_COMPLETION_VERIFY_ERROR
} AdcDbmDriverStatus;

typedef enum
{
    ADC_DBM_DRIVER_SLOT_M0 = 0,
    ADC_DBM_DRIVER_SLOT_M1 = 1
} AdcDbmDriverSlot;

typedef struct
{
    uint32_t sequence;
    AdcDbmDriverSlot completed_slot;
    uint32_t callback_cycle;
} AdcDbmDriverCompletionEvent;

/*
 * Both callbacks execute in DMA IRQ context.
 * They must remain bounded and nonblocking. They must not call ordinary
 * task-context FreeRTOS APIs, perform UART/printf work, allocate memory, or
 * wait on HAL/RTOS objects. Any RTOS interaction must obey the project ISR
 * priority contract and use the appropriate FromISR API.
 *
 * ApplyInactiveAction() is intentionally legal only while the completion
 * callback for the supplied event is active. The runtime must classify every
 * RUNNING completion exactly once: KEEP for a controlled DROP, or REBIND for
 * an admitted replacement. Returning from the callback without a successful
 * action is a fail-closed driver error.
 */
typedef void (*AdcDbmDriverCompleteCallback)(
    const AdcDbmDriverCompletionEvent *event,
    void *context);

typedef void (*AdcDbmDriverErrorCallback)(
    uint32_t dma_error_code,
    uint32_t adc_status,
    void *context);

typedef struct
{
    uint32_t m0_address;
    uint32_t m1_address;
    uint32_t block_samples;
    AdcDbmDriverCompleteCallback complete_callback;
    AdcDbmDriverErrorCallback error_callback;
    void *callback_context;
} AdcDbmDriverArmConfig;

typedef enum
{
    ADC_DBM_DRIVER_INACTIVE_KEEP = 0,
    ADC_DBM_DRIVER_INACTIVE_REBIND = 1
} AdcDbmDriverInactiveAction;

typedef struct
{
    uint32_t sequence;
    AdcDbmDriverSlot completed_slot;
    AdcDbmDriverInactiveAction action;
    uint32_t expected_completed_address;
    uint32_t expected_active_address;
    uint32_t replacement_address;
} AdcDbmDriverInactiveRequest;

typedef struct
{
    uint32_t sequence;
    uint32_t completed_slot;
    uint32_t action;
    uint32_t guard_status;
    uint32_t ct_prewrite;
    uint32_t ct_after;
    uint32_t ndtr_prewrite;
    uint32_t active_address_before;
    uint32_t inactive_address_before;
    uint32_t active_address_after;
    uint32_t inactive_address_after;
    uint32_t replacement_address;
    uint32_t critical_window_cycles;
    uint32_t nominal_to_decision_cycles;
} AdcDbmDriverInactiveReport;

typedef struct
{
    AdcDbmDriverState state;
    uint32_t hardware_owned;
    uint32_t address_map_trusted;
    uint32_t block_samples;
    uint32_t start_epoch_cycle;
    uint32_t completion_count;
    uint32_t bound_m0_address;
    uint32_t bound_m1_address;
    uint32_t dma_cr;
    uint32_t dma_ndtr;
    uint32_t dma_m0ar;
    uint32_t dma_m1ar;
    uint32_t dma_ct;
    uint32_t dma_lisr;
    uint32_t adc_sr;
    uint32_t adc_cr2;
    uint32_t tim2_cr1;
    uint32_t suppressed_completion_count;
    uint32_t dma_error_count;
    uint32_t inactive_keep_success_count;
    uint32_t inactive_rebind_success_count;
    uint32_t inactive_failure_count;
} AdcDbmDriverSnapshot;

typedef struct
{
    uint32_t remaining_samples;
    uint32_t captured_samples;
    uint32_t active_slot;
    uint32_t dma_lisr_before_stop;
    uint32_t dma_lisr_after_stop;
    uint32_t adc_sr_before_stop;
    uint32_t dma_cr_after_stop;
    uint32_t adc_cr2_after_stop;
    uint32_t tim2_cr1_after_stop;
} AdcDbmDriverStopReport;

/*
 * Init is an offline/quiescent operation and must be called after Cube/HAL
 * peripheral initialization. It never stops or reconfigures hardware that it
 * did not arm itself. If acquisition hardware is already active or the handle
 * topology is not the frozen platform topology, the driver enters ERROR.
 */
void AdcDbmDriver_Init(void);

/*
 * Arm configures DBM and ADC but intentionally leaves TIM2 stopped.
 *
 * The current foundation preserves fs=200 kSamples/s. Each complete DMA
 * destination span must lie inside the STM32F446RE SRAM window, M0/M1 spans
 * must not overlap, and both bases must be 32-bit aligned.
 */
AdcDbmDriverStatus AdcDbmDriver_Arm(
    const AdcDbmDriverArmConfig *config);

/*
 * Final start commit. The RUNNING state becomes visible and the DWT start epoch
 * is captured immediately before TIM2 is started. If timer start fails,
 * cleanup occurs only after the short critical section has been exited.
 */
AdcDbmDriverStatus AdcDbmDriver_CommitStart(void);

/*
 * Hardware-only inactive-slot completion transaction. Admission policy remains
 * outside this driver: the runtime chooses KEEP or REBIND, then asks the
 * driver to validate/apply that already-made decision.
 *
 * Preconditions are rechecked with CPU interrupts masked:
 *   - RUNNING + driver-owned hardware;
 *   - call occurs synchronously inside the matching DMA completion callback;
 *   - CT still identifies the expected active/inactive slot;
 *   - current M0AR/M1AR match both driver-owned physical bindings and the
 *     caller's expected active/completed addresses;
 *   - DMA/ADC/TIM2 hardware remains healthy;
 *   - NDTR and epoch-derived service window remain inside the R2 budget.
 *
 * KEEP performs no MxAR write and verifies both addresses remain unchanged.
 * It is the hardware-side validation for a controlled capacity DROP. REBIND
 * writes only the inactive MxAR, reads it back, verifies the active address
 * remained unchanged, then commits the driver's physical binding.
 *
 * No BufferPool, queue, or R2_DmaSlots mutation occurs here. The higher runtime
 * may commit logical mapping/ownership only AFTER a REBIND returns OK.
 *
 * For KEEP, replacement_address must be 0. For REBIND it must name a valid,
 * nonoverlapping SRAM span. Any malformed active-callback request, unsafe
 * precheck, missing completion action, or post-action verification failure
 * latches ERROR, stops TIM2 triggers, disables DMA interrupt sources, and
 * leaves final task-context quiescence to AdcDbmDriver_Stop().
 */
AdcDbmDriverStatus AdcDbmDriver_ApplyInactiveAction(
    const AdcDbmDriverCompletionEvent *event,
    const AdcDbmDriverInactiveRequest *request,
    AdcDbmDriverInactiveReport *report);

/*
 * ISR-safe fail-stop handoff for the software half of the completion
 * transaction.
 *
 * After a successful KEEP or REBIND, the higher runtime still has software
 * bookkeeping to commit (for example R2_DmaSlots / BufferPool / queue state).
 * If that post-hardware logical commit detects an invariant failure, it MUST
 * call this function before returning from the same completion callback rather
 * than touching TIM2/DMA registers directly or calling the blocking Stop path.
 *
 * The supplied event must identify the currently active completion callback.
 * On success this function stops TIM2 triggers, disables DMA interrupt sources,
 * latches ERROR, and leaves final DMA/ADC quiescence to task-context Stop().
 * It performs no HAL wait/abort operation and is therefore safe for the IRQ
 * fail-stop path.
 */
AdcDbmDriverStatus AdcDbmDriver_FailActiveCompletion(
    const AdcDbmDriverCompletionEvent *event);

/*
 * Stop is the hardware-only quiescence primitive. It only stops hardware that
 * this driver successfully armed. Higher-level R3 gates, READY cancellation
 * and worker ACKs intentionally do not belong here.
 */
AdcDbmDriverStatus AdcDbmDriver_Stop(
    AdcDbmDriverStopReport *report);

/*
 * Snapshot is diagnostic only. During RUNNING, disabling CPU interrupts does
 * not stop DMA, so NDTR/CT/address fields are not a transactional safety proof.
 * Completion safety decisions remain inside AdcDbmDriver_ApplyInactiveAction().
 */
AdcDbmDriverStatus AdcDbmDriver_GetSnapshot(
    AdcDbmDriverSnapshot *out);

AdcDbmDriverState AdcDbmDriver_GetState(void);

#ifdef __cplusplus
}
#endif

#endif
