set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

printf "=== W6 RESULT IDENTITY ===\n"
printf "magic=0x%08x\n", (unsigned int)g_r2_w6_result.magic
printf "control_task_created=%u\n", (unsigned int)g_r2_w6_result.control_task_created
printf "processing_task_created=%u\n", (unsigned int)g_r2_w6_result.processing_task_created
printf "phase=%u\n", (unsigned int)g_r2_w6_result.phase
printf "test_pass=%u\n", (unsigned int)g_r2_w6_result.test_pass
printf "fault_bits=0x%08x\n", (unsigned int)g_r2_w6_result.fault_bits

printf "\n=== CONFIGURATION ===\n"
printf "system_core_clock=%u\n", (unsigned int)g_r2_w6_result.system_core_clock
printf "configured_k=%u\n", (unsigned int)g_r2_w6_result.configured_k
printf "drop_mode=%u\n", (unsigned int)g_r2_w6_result.drop_mode
printf "process_hold_blocks=%u\n", (unsigned int)g_r2_w6_result.process_hold_blocks
printf "aircr=0x%08x\n", (unsigned int)g_r2_w6_result.aircr

printf "\n=== EVENT ACCOUNTING ===\n"
printf "irq_count=%u\n", (unsigned int)g_r2_w6_result.irq_count
printf "input_count=%u\n", (unsigned int)g_r2_w6_result.input_count
printf "admitted_count=%u\n", (unsigned int)g_r2_w6_result.admitted_count
printf "capacity_drop_count=%u\n", (unsigned int)g_r2_w6_result.capacity_drop_count
printf "processed_count=%u\n", (unsigned int)g_r2_w6_result.processed_count
printf "released_count=%u\n", (unsigned int)g_r2_w6_result.released_count
printf "recovered_admission_after_drop_count=%u\n", (unsigned int)g_r2_w6_result.recovered_admission_after_drop_count
printf "max_drop_streak=%u\n", (unsigned int)g_r2_w6_result.max_drop_streak

printf "\n=== ERROR ACCOUNTING ===\n"
printf "init_hook_count=%u\n", (unsigned int)g_r2_w6_result.init_hook_count
printf "completion_hook_count=%u\n", (unsigned int)g_r2_w6_result.completion_hook_count
printf "illegal_free_send_count=%u\n", (unsigned int)g_r2_w6_result.illegal_free_send_count
printf "ready_send_fail_count=%u\n", (unsigned int)g_r2_w6_result.ready_send_fail_count
printf "notification_fail_count=%u\n", (unsigned int)g_r2_w6_result.notification_fail_count
printf "token_ledger_errors=%u\n", (unsigned int)g_r2_w6_result.token_ledger_errors
printf "dma_error_flags_seen=%u\n", (unsigned int)g_r2_w6_result.dma_error_flags_seen
printf "adc_ovr_seen=%u\n", (unsigned int)g_r2_w6_result.adc_ovr_seen

printf "\n=== TIMING ===\n"
printf "max_nominal_to_decision_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_decision_cycles
printf "max_nominal_to_irq_exit_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_irq_exit_cycles
printf "max_final_window_cycles=%u\n", (unsigned int)g_r2_w6_result.max_final_window_cycles

printf "\n=== QUEUE / TOKEN FINAL STATE ===\n"
printf "max_ready_depth=%u\n", (unsigned int)g_r2_w6_result.max_ready_depth
printf "min_free_depth=%u\n", (unsigned int)g_r2_w6_result.min_free_depth
printf "free_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.free_queue_depth_final
printf "ready_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.ready_queue_depth_final
printf "free_token_mask_final=0x%08x\n", (unsigned int)g_r2_w6_result.free_token_mask_final
printf "ready_token_mask_final=0x%08x\n", (unsigned int)g_r2_w6_result.ready_token_mask_final

printf "\n=== SAMPLE VALIDATION ===\n"
printf "full_sample_count=%u\n", (unsigned int)g_r2_w6_result.full_sample_count
printf "sample_errors=%u\n", (unsigned int)g_r2_w6_result.sample_errors
printf "canary_errors=%u\n", (unsigned int)g_r2_w6_result.canary_errors

printf "\n=== QUIET WINDOW ===\n"
printf "quiet_irq_count=%u\n", (unsigned int)g_r2_w6_result.quiet_irq_count
printf "quiet_input_count=%u\n", (unsigned int)g_r2_w6_result.quiet_input_count
printf "quiet_admitted_count=%u\n", (unsigned int)g_r2_w6_result.quiet_admitted_count
printf "quiet_drop_count=%u\n", (unsigned int)g_r2_w6_result.quiet_drop_count
printf "quiet_processed_count=%u\n", (unsigned int)g_r2_w6_result.quiet_processed_count

printf "\n=== BUFFER POOL FINAL STATE ===\n"
printf "pool_activated=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.activated
printf "pool_k=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.k
printf "pool_active_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.active_count
printf "pool_inactive_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.inactive_count
printf "pool_free_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.free_count
printf "pool_dma_owned_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.dma_owned_count
printf "pool_ready_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.ready_count
printf "pool_processing_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.processing_count
printf "pool_violation_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.violation_count
printf "pool_state_0=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.states[0]
printf "pool_state_1=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.states[1]
printf "pool_state_2=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.states[2]
printf "pool_state_3=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.states[3]

printf "\n=== DMA SLOT FINAL STATE ===\n"
printf "slots_initialized=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.initialized
printf "slots_mapping_epoch=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.mapping_epoch
printf "slots_violation_count=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.violation_count
printf "slots_m0_buffer=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.m0_buffer
printf "slots_m1_buffer=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.m1_buffer

printf "\n=== FIRST / LAST EVENT ===\n"
printf "trace_1_sequence=%u\n", (unsigned int)g_r2_w6_result.trace[0].sequence
printf "trace_1_decision=%u\n", (unsigned int)g_r2_w6_result.trace[0].decision
printf "trace_1_processed_ok=%u\n", (unsigned int)g_r2_w6_result.trace[0].processed_ok
printf "trace_96_sequence=%u\n", (unsigned int)g_r2_w6_result.trace[95].sequence
printf "trace_96_decision=%u\n", (unsigned int)g_r2_w6_result.trace[95].decision
printf "trace_96_processed_ok=%u\n", (unsigned int)g_r2_w6_result.trace[95].processed_ok

detach
quit