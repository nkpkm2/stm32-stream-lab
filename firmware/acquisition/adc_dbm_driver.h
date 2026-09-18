#ifndef ADC_DBM_DRIVER_H
#define ADC_DBM_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-2.
 *
 * This module is the future sole owner of the ADC1 / DMA2 Stream0 / TIM2
 * acquisition hardware lifecycle. Stage 1 deliberately does NOT migrate any
 * historical R1/R2 profile to this driver yet. Historical profiles remain
 * immutable behavioral oracles until parity has been established.
 *
 * Stage-1 scope:
 *   - R2-compatible hardware configuration validation,
 *   - DBM arm with caller-provided M0/M1 addresses,
 *   - final TIM2 start commit,
 *   - bounded hardware stop/quiescence,
 *   - diagnostic read-only hardware/state snapshots,
 *   - driver-owned DMA completion/error callback dispatch.
 *
 * Not yet in Stage 1:
 *   - inactive-MxAR rebind (added before any runtime migration),
 *   - BufferPool / DmaSlots / queue ownership,
 *   - R3 RunContext / generation / START/STOP protocol / worker ACKs.
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
    ADC_DBM_DRIVER_HARDWARE_ERROR
} AdcDbmDriverStatus;

typedef enum
{
    ADC_DBM_DRIVER_SLOT_M0 = 0,
    ADC_DBM_DRIVER_SLOT_M1 = 1
} AdcDbmDriverSlot;

/*
 * Both callbacks execute in DMA IRQ context.
 * They must remain bounded and nonblocking. They must not call ordinary
 * task-context FreeRTOS APIs, perform UART/printf work, allocate memory, or
 * wait on HAL/RTOS objects. Any RTOS interaction must obey the project ISR
 * priority contract and use the appropriate FromISR API.
 */
typedef void (*AdcDbmDriverCompleteCallback)(
    AdcDbmDriverSlot completed_slot,
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

typedef struct
{
    AdcDbmDriverState state;
    uint32_t hardware_owned;
    uint32_t block_samples;
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
 * Stage 1 preserves the current R2 hardware profile (200 kSamples/s, N supplied
 * by block_samples). Each complete DMA destination span must lie inside the
 * STM32F446RE SRAM window, M0/M1 spans must not overlap, and both bases must be
 * 32-bit aligned.
 */
AdcDbmDriverStatus AdcDbmDriver_Arm(
    const AdcDbmDriverArmConfig *config);

/*
 * Final start commit. The RUNNING state becomes visible before TIM2 CEN is
 * asserted, matching the already-validated R2 ordering. If timer start fails,
 * cleanup occurs only after the short critical section has been exited.
 */
AdcDbmDriverStatus AdcDbmDriver_CommitStart(void);

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
 * Future rebind safety decisions must remain inside dedicated driver APIs.
 */
AdcDbmDriverStatus AdcDbmDriver_GetSnapshot(
    AdcDbmDriverSnapshot *out);

AdcDbmDriverState AdcDbmDriver_GetState(void);

#ifdef __cplusplus
}
#endif

#endif
