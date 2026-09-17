set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

set $i = 0

while $i < 96
    printf "TRACE,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", \
        (unsigned int)g_r2_w6_result.trace[$i].sequence, \
        (unsigned int)g_r2_w6_result.trace[$i].decision, \
        (unsigned int)g_r2_w6_result.trace[$i].ct_entry, \
        (unsigned int)g_r2_w6_result.trace[$i].completed_slot, \
        (unsigned int)g_r2_w6_result.trace[$i].completed_id, \
        (unsigned int)g_r2_w6_result.trace[$i].replacement_id, \
        (unsigned int)g_r2_w6_result.trace[$i].ndtr_guard, \
        (unsigned int)g_r2_w6_result.trace[$i].free_depth_after_take, \
        (unsigned int)g_r2_w6_result.trace[$i].ready_depth_after_publish, \
        (unsigned int)g_r2_w6_result.trace[$i].nominal_to_decision_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].nominal_to_irq_exit_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].final_window_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].mapping_epoch_after, \
        (unsigned int)g_r2_w6_result.trace[$i].m0_before, \
        (unsigned int)g_r2_w6_result.trace[$i].m1_before, \
        (unsigned int)g_r2_w6_result.trace[$i].m0_after, \
        (unsigned int)g_r2_w6_result.trace[$i].m1_after, \
        (unsigned int)g_r2_w6_result.trace[$i].processing_begin_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].processing_end_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].release_commit_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].raw_min, \
        (unsigned int)g_r2_w6_result.trace[$i].raw_max, \
        (unsigned int)g_r2_w6_result.trace[$i].processed_ok

    set $i = $i + 1
end

detach
quit