set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

printf "ct_magic=0x%08x\n", (unsigned int)g_r2_ct_result.magic
printf "ct_task_created=%u\n", (unsigned int)g_r2_ct_result.task_created
printf "ct_task_started=%u\n", (unsigned int)g_r2_ct_result.task_started
printf "ct_fault_bits=0x%08x\n", (unsigned int)g_r2_ct_result.fault_bits
printf "ct_rx_byte_count=%u\n", (unsigned int)g_r2_ct_result.rx_byte_count
printf "ct_rx_frame_count=%u\n", (unsigned int)g_r2_ct_result.rx_frame_count
printf "ct_rx_length_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_length_error_count
printf "ct_rx_crc_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_crc_error_count
printf "ct_rx_version_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_version_error_count
printf "ct_rx_unsupported_count=%u\n", (unsigned int)g_r2_ct_result.rx_unsupported_count
printf "ct_rx_overflow_count=%u\n", (unsigned int)g_r2_ct_result.rx_overflow_count
printf "ct_uart_error_count=%u\n", (unsigned int)g_r2_ct_result.uart_error_count
printf "ct_rate_limited_count=%u\n", (unsigned int)g_r2_ct_result.rate_limited_count
printf "ct_processed_count=%u\n", (unsigned int)g_r2_ct_result.processed_count
printf "ct_processed_while_running_count=%u\n", (unsigned int)g_r2_ct_result.processed_while_running_count
printf "ct_reply_attempt_count=%u\n", (unsigned int)g_r2_ct_result.reply_attempt_count
printf "ct_reply_success_count=%u\n", (unsigned int)g_r2_ct_result.reply_success_count
printf "ct_reply_error_count=%u\n", (unsigned int)g_r2_ct_result.reply_error_count
printf "ct_first_processed_w6_input=%u\n", (unsigned int)g_r2_ct_result.first_processed_w6_input
printf "ct_last_processed_w6_input=%u\n", (unsigned int)g_r2_ct_result.last_processed_w6_input
printf "ct_trace_count=%u\n", (unsigned int)g_r2_ct_result.trace_count

set $i = 0
while $i < 10
  printf "CTTRACE,%u,%u,%u,%u,%u,%u,%u,%u,0x%08x,%u\n", $i, (unsigned int)g_r2_ct_result.trace[$i].request_id, (unsigned int)g_r2_ct_result.trace[$i].payload_length, (unsigned int)g_r2_ct_result.trace[$i].rx_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].processed_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_start_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].w6_input_at_process, (unsigned int)g_r2_ct_result.trace[$i].payload_crc32, (unsigned int)g_r2_ct_result.trace[$i].processed_while_running
  set $i = $i + 1
end

printf "w6_magic=0x%08x\n", (unsigned int)g_r2_w6_result.magic
printf "w6_phase=%u\n", (unsigned int)g_r2_w6_result.phase
printf "w6_test_pass=%u\n", (unsigned int)g_r2_w6_result.test_pass
printf "w6_fault_bits=0x%08x\n", (unsigned int)g_r2_w6_result.fault_bits
printf "w6_configured_k=%u\n", (unsigned int)g_r2_w6_result.configured_k
printf "w6_drop_mode=%u\n", (unsigned int)g_r2_w6_result.drop_mode
printf "w6_process_hold_blocks=%u\n", (unsigned int)g_r2_w6_result.process_hold_blocks
printf "w6_irq_count=%u\n", (unsigned int)g_r2_w6_result.irq_count
printf "w6_input_count=%u\n", (unsigned int)g_r2_w6_result.input_count
printf "w6_admitted_count=%u\n", (unsigned int)g_r2_w6_result.admitted_count
printf "w6_capacity_drop_count=%u\n", (unsigned int)g_r2_w6_result.capacity_drop_count
printf "w6_processed_count=%u\n", (unsigned int)g_r2_w6_result.processed_count
printf "w6_released_count=%u\n", (unsigned int)g_r2_w6_result.released_count
printf "w6_recovered_admission_after_drop_count=%u\n", (unsigned int)g_r2_w6_result.recovered_admission_after_drop_count
printf "w6_current_drop_streak=%u\n", (unsigned int)g_r2_w6_result.current_drop_streak
printf "w6_max_drop_streak=%u\n", (unsigned int)g_r2_w6_result.max_drop_streak
printf "w6_max_nominal_to_decision_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_decision_cycles
printf "w6_max_nominal_to_irq_exit_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_irq_exit_cycles
printf "w6_max_final_window_cycles=%u\n", (unsigned int)g_r2_w6_result.max_final_window_cycles
printf "w6_illegal_free_send_count=%u\n", (unsigned int)g_r2_w6_result.illegal_free_send_count
printf "w6_ready_send_fail_count=%u\n", (unsigned int)g_r2_w6_result.ready_send_fail_count
printf "w6_notification_fail_count=%u\n", (unsigned int)g_r2_w6_result.notification_fail_count
printf "w6_token_ledger_errors=%u\n", (unsigned int)g_r2_w6_result.token_ledger_errors
printf "w6_dma_error_flags_seen=%u\n", (unsigned int)g_r2_w6_result.dma_error_flags_seen
printf "w6_adc_ovr_seen=%u\n", (unsigned int)g_r2_w6_result.adc_ovr_seen
printf "w6_full_sample_count=%u\n", (unsigned int)g_r2_w6_result.full_sample_count
printf "w6_sample_errors=%u\n", (unsigned int)g_r2_w6_result.sample_errors
printf "w6_canary_errors=%u\n", (unsigned int)g_r2_w6_result.canary_errors
printf "w6_free_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.free_queue_depth_final
printf "w6_ready_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.ready_queue_depth_final
printf "w6_quiet_irq_count=%u\n", (unsigned int)g_r2_w6_result.quiet_irq_count
printf "w6_quiet_input_count=%u\n", (unsigned int)g_r2_w6_result.quiet_input_count
printf "w6_quiet_admitted_count=%u\n", (unsigned int)g_r2_w6_result.quiet_admitted_count
printf "w6_quiet_drop_count=%u\n", (unsigned int)g_r2_w6_result.quiet_drop_count
printf "w6_quiet_processed_count=%u\n", (unsigned int)g_r2_w6_result.quiet_processed_count
printf "w6_pool_free_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.free_count
printf "w6_pool_dma_owned_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.dma_owned_count
printf "w6_pool_ready_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.ready_count
printf "w6_pool_processing_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.processing_count
printf "w6_pool_violation_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.violation_count
printf "w6_slots_violation_count=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.violation_count

printf "W6TRACE_HEADER,index,sequence,decision,completed_id,replacement_id,mapping_epoch_after,m0_before,m0_after,m1_before,m1_after,free_depth_after_take,ready_depth_after_publish,processed_ok,nominal_to_decision_cycles,final_window_cycles\n"
set $i = 0
while $i < 800
  printf "W6TRACE,%u,%u,%u,%u,%u,%u,0x%08x,0x%08x,0x%08x,0x%08x,%u,%u,%u,%u,%u\n", $i, (unsigned int)g_r2_w6_result.trace[$i].sequence, (unsigned int)g_r2_w6_result.trace[$i].decision, (unsigned int)g_r2_w6_result.trace[$i].completed_id, (unsigned int)g_r2_w6_result.trace[$i].replacement_id, (unsigned int)g_r2_w6_result.trace[$i].mapping_epoch_after, (unsigned int)g_r2_w6_result.trace[$i].m0_before, (unsigned int)g_r2_w6_result.trace[$i].m0_after, (unsigned int)g_r2_w6_result.trace[$i].m1_before, (unsigned int)g_r2_w6_result.trace[$i].m1_after, (unsigned int)g_r2_w6_result.trace[$i].free_depth_after_take, (unsigned int)g_r2_w6_result.trace[$i].ready_depth_after_publish, (unsigned int)g_r2_w6_result.trace[$i].processed_ok, (unsigned int)g_r2_w6_result.trace[$i].nominal_to_decision_cycles, (unsigned int)g_r2_w6_result.trace[$i].final_window_cycles
  set $i = $i + 1
end

detach
quit
