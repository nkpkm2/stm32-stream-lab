#include "r2_ct_control.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "main.h"
#include "r2_w6_matrix.h"

#define CT_TASK_STACK_WORDS 512U
#define CT_TASK_PRIORITY (tskIDLE_PRIORITY + 4U)
#define CT_RX_ERROR_MASK (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)
#define CT_RATE_WINDOW_TICKS pdMS_TO_TICKS(1000U)

extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_tx;

static StaticTask_t ct_task_control;
static StackType_t ct_task_stack[CT_TASK_STACK_WORDS];
static TaskHandle_t ct_task_handle;

static uint8_t rx_ring[R2_CT_RX_RING_BYTES];
static volatile uint32_t rx_write_index;
static volatile uint32_t rx_read_index;

static R2_CT_Parser parser;
static uint8_t tx_staging[R2_CT_MAX_FRAME_BYTES];
static volatile uint32_t tx_busy;
static volatile uint32_t tx_trace_index;
static volatile BaseType_t tx_irq_should_yield;

static TickType_t accepted_ticks[R2_CT_MAX_ACCEPTED_PER_SECOND];
static uint32_t accepted_count;
static uint32_t accepted_next;

volatile R2_CT_Result g_r2_ct_result;

static void LatchFault(uint32_t bit)
{
    g_r2_ct_result.fault_bits |= bit;
}

static int RxPop(uint8_t *byte)
{
    uint32_t read_index = rx_read_index;

    if (read_index == rx_write_index)
    {
        return 0;
    }

    *byte = rx_ring[read_index];
    rx_read_index = (read_index + 1U) % R2_CT_RX_RING_BYTES;
    return 1;
}

static int RateAllowed(TickType_t now)
{
    uint32_t i;
    uint32_t in_window = 0U;

    for (i = 0U; i < accepted_count; ++i)
    {
        if ((TickType_t)(now - accepted_ticks[i]) < CT_RATE_WINDOW_TICKS)
        {
            ++in_window;
        }
    }

    if (in_window >= R2_CT_MAX_ACCEPTED_PER_SECOND)
    {
        return 0;
    }

    if (accepted_count < R2_CT_MAX_ACCEPTED_PER_SECOND)
    {
        accepted_ticks[accepted_count++] = now;
    }
    else
    {
        accepted_ticks[accepted_next] = now;
        accepted_next = (accepted_next + 1U) % R2_CT_MAX_ACCEPTED_PER_SECOND;
    }

    return 1;
}

static void WaitForTxIdle(void)
{
    while ((tx_busy != 0U) && (g_r2_ct_result.fault_bits == 0U))
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20U));
    }
}

static void StoreU32Le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void SendPingReply(const R2_CT_Frame *frame, uint32_t trace_index)
{
    uint8_t reply_payload[12];
    size_t frame_bytes;
    HAL_StatusTypeDef status;
    volatile R2_CT_Trace *trace = &g_r2_ct_result.trace[trace_index];

    WaitForTxIdle();
    if (g_r2_ct_result.fault_bits != 0U)
    {
        return;
    }

    StoreU32Le(&reply_payload[0], trace->payload_crc32);
    StoreU32Le(&reply_payload[4], trace->w6_input_at_process);
    reply_payload[8] = (uint8_t)trace->w6_phase_at_process;
    reply_payload[9] = (uint8_t)R2_W6_K;
    reply_payload[10] = (uint8_t)R2_W6_DROP_MODE;
    reply_payload[11] = (uint8_t)frame->payload_length;

    frame_bytes = R2_CT_EncodeFrame(
        R2_CT_TYPE_PING_REPLY,
        frame->request_id,
        reply_payload,
        (uint16_t)sizeof(reply_payload),
        tx_staging,
        sizeof(tx_staging));
    if (frame_bytes == 0U)
    {
        LatchFault(R2_CT_FAULT_TX_START);
        ++g_r2_ct_result.reply_error_count;
        return;
    }

    ++g_r2_ct_result.reply_attempt_count;
    g_r2_ct_result.trace[trace_index].reply_start_cycle = DWT->CYCCNT;
    tx_trace_index = trace_index;
    tx_busy = 1U;

    status = HAL_UART_Transmit_DMA(&huart2, tx_staging, (uint16_t)frame_bytes);
    if (status != HAL_OK)
    {
        tx_busy = 0U;
        ++g_r2_ct_result.reply_error_count;
        LatchFault(R2_CT_FAULT_TX_START);
    }
}

static void ProcessFrame(const R2_CT_Frame *frame, uint32_t rx_complete_cycle)
{
    uint32_t trace_index;
    uint32_t phase;
    uint32_t input_count;
    TickType_t now_tick;

    if (frame->type != R2_CT_TYPE_PING)
    {
        ++g_r2_ct_result.rx_unsupported_count;
        return;
    }

    now_tick = xTaskGetTickCount();
    if (!RateAllowed(now_tick))
    {
        ++g_r2_ct_result.rate_limited_count;
        return;
    }

    trace_index = g_r2_ct_result.trace_count;
    if (trace_index >= R2_CT_TRACE_CAPACITY)
    {
        LatchFault(R2_CT_FAULT_TRACE_OVERFLOW);
        return;
    }
    g_r2_ct_result.trace_count = trace_index + 1U;

    phase = g_r2_w6_result.phase;
    input_count = g_r2_w6_result.input_count;

    g_r2_ct_result.trace[trace_index].request_id = frame->request_id;
    g_r2_ct_result.trace[trace_index].payload_length = frame->payload_length;
    g_r2_ct_result.trace[trace_index].rx_complete_cycle = rx_complete_cycle;
    g_r2_ct_result.trace[trace_index].processed_cycle = DWT->CYCCNT;
    g_r2_ct_result.trace[trace_index].w6_input_at_process = input_count;
    g_r2_ct_result.trace[trace_index].w6_phase_at_process = phase;
    g_r2_ct_result.trace[trace_index].payload_crc32 =
        R2_CT_Crc32(frame->payload, frame->payload_length);
    g_r2_ct_result.trace[trace_index].processed_while_running =
        phase == (uint32_t)R2_W6_RUNNING ? 1U : 0U;

    ++g_r2_ct_result.processed_count;
    if (phase == (uint32_t)R2_W6_RUNNING)
    {
        if (g_r2_ct_result.processed_while_running_count == 0U)
        {
            g_r2_ct_result.first_processed_w6_input = input_count;
        }
        ++g_r2_ct_result.processed_while_running_count;
        g_r2_ct_result.last_processed_w6_input = input_count;
    }

    SendPingReply(frame, trace_index);
}

static void CommunicationTask(void *argument)
{
    uint8_t byte;
    R2_CT_Frame frame;
    R2_CT_ParseStatus parse_status;
    uint32_t rx_complete_cycle = 0U;
    (void)argument;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    R2_CT_ParserInit(&parser);
    g_r2_ct_result.task_started = 1U;

    /* RX is deliberately interrupt driven. USART2/DMA1 IRQs are already
     * configured but RXNE is enabled only after the scheduler/task exists. */
    (void)USART2->SR;
    (void)USART2->DR;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_ERR);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (RxPop(&byte) != 0)
        {
            parse_status = R2_CT_ParserPush(&parser, byte, &frame);
            if (parse_status == R2_CT_PARSE_FRAME)
            {
                rx_complete_cycle = DWT->CYCCNT;
                ++g_r2_ct_result.rx_frame_count;
                ProcessFrame(&frame, rx_complete_cycle);
            }
            else if (parse_status == R2_CT_PARSE_LENGTH_ERROR)
            {
                ++g_r2_ct_result.rx_length_error_count;
            }
            else if (parse_status == R2_CT_PARSE_CRC_ERROR)
            {
                ++g_r2_ct_result.rx_crc_error_count;
            }
            else if (parse_status == R2_CT_PARSE_VERSION_ERROR)
            {
                ++g_r2_ct_result.rx_version_error_count;
            }
        }
    }
}

void R2_CT_CreateTask(void)
{
    TaskHandle_t handle;

    memset((void *)&g_r2_ct_result, 0, sizeof(g_r2_ct_result));
    memset(accepted_ticks, 0, sizeof(accepted_ticks));
    rx_write_index = 0U;
    rx_read_index = 0U;
    tx_busy = 0U;
    tx_trace_index = R2_CT_TRACE_CAPACITY;
    accepted_count = 0U;
    accepted_next = 0U;

    g_r2_ct_result.magic = R2_CT_MAGIC;
    g_r2_ct_result.first_processed_w6_input = UINT32_MAX;
    g_r2_ct_result.last_processed_w6_input = UINT32_MAX;

    handle = xTaskCreateStatic(
        CommunicationTask,
        "R2CTComm",
        CT_TASK_STACK_WORDS,
        NULL,
        CT_TASK_PRIORITY,
        ct_task_stack,
        &ct_task_control);

    ct_task_handle = handle;
    g_r2_ct_result.task_created = handle != NULL ? 1U : 0U;
    if (handle == NULL)
    {
        LatchFault(R2_CT_FAULT_TASK_CREATE);
    }
}

uint32_t R2_CT_UsartIrqHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t sr = USART2->SR;
    uint32_t dr = 0U;

    tx_irq_should_yield = pdFALSE;

    if ((sr & (USART_SR_RXNE | CT_RX_ERROR_MASK)) != 0U)
    {
        dr = USART2->DR;
    }

    if ((sr & CT_RX_ERROR_MASK) != 0U)
    {
        ++g_r2_ct_result.uart_error_count;
        LatchFault(R2_CT_FAULT_UART_RX);
    }

    if ((sr & USART_SR_RXNE) != 0U)
    {
        uint32_t write_index = rx_write_index;
        uint32_t next = (write_index + 1U) % R2_CT_RX_RING_BYTES;

        ++g_r2_ct_result.rx_byte_count;
        if (next == rx_read_index)
        {
            ++g_r2_ct_result.rx_overflow_count;
            LatchFault(R2_CT_FAULT_RX_OVERFLOW);
        }
        else
        {
            rx_ring[write_index] = (uint8_t)dr;
            rx_write_index = next;
            if (ct_task_handle != NULL)
            {
                vTaskNotifyGiveFromISR(ct_task_handle,
                    &higher_priority_task_woken);
            }
        }
    }

    /* HAL_UART_Transmit_DMA() in normal mode finishes in two stages:
     * DMA1_Stream6 completion drains the DMA transfer, then HAL enables the
     * USART TC interrupt so the final wire-complete event can close huart2's
     * TX state and call HAL_UART_TxCpltCallback().
     *
     * RXNE is owned by the bounded R2 control path above, not by HAL's
     * receive state machine. Temporarily mask RX/error interrupt enables while
     * handing only the pending TX-complete condition to HAL. Any byte that
     * arrives in this short interval remains latched in DR/RXNE and is serviced
     * after the enables are restored.
     */
    if (((sr & USART_SR_TC) != 0U) &&
        (__HAL_UART_GET_IT_SOURCE(&huart2, UART_IT_TC) != RESET))
    {
        uint32_t cr1_before = USART2->CR1;
        uint32_t cr3_before = USART2->CR3;

        __HAL_UART_DISABLE_IT(&huart2, UART_IT_RXNE);
        __HAL_UART_DISABLE_IT(&huart2, UART_IT_ERR);
        HAL_UART_IRQHandler(&huart2);

        if ((cr3_before & USART_CR3_EIE) != 0U)
        {
            __HAL_UART_ENABLE_IT(&huart2, UART_IT_ERR);
        }
        if ((cr1_before & USART_CR1_RXNEIE) != 0U)
        {
            __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
        }

        if (tx_irq_should_yield != pdFALSE)
        {
            higher_priority_task_woken = pdTRUE;
        }
    }

    return higher_priority_task_woken != pdFALSE ? 1U : 0U;
}

uint32_t R2_CT_TxDmaIrqHandler(void)
{
    tx_irq_should_yield = pdFALSE;
    HAL_DMA_IRQHandler(&hdma_usart2_tx);
    return tx_irq_should_yield != pdFALSE ? 1U : 0U;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        BaseType_t higher_priority_task_woken = pdFALSE;
        uint32_t trace_index = tx_trace_index;

        tx_busy = 0U;
        ++g_r2_ct_result.reply_success_count;
        if (trace_index < R2_CT_TRACE_CAPACITY)
        {
            g_r2_ct_result.trace[trace_index].reply_complete_cycle = DWT->CYCCNT;
        }
        if (ct_task_handle != NULL)
        {
            vTaskNotifyGiveFromISR(ct_task_handle, &higher_priority_task_woken);
        }
        if (higher_priority_task_woken != pdFALSE)
        {
            tx_irq_should_yield = pdTRUE;
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART2))
    {
        tx_busy = 0U;
        ++g_r2_ct_result.reply_error_count;
        LatchFault(R2_CT_FAULT_TX_DMA);
    }
}
