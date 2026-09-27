# R3 W5 Native Isolation Execution Record

The MSVC/Ninja host profile enabled `R3_W5_LEDGER_NATIVE_TESTS`,
`R3_WORKER_CONTRACT_NATIVE_TESTS`, and `R3_WORKER_TASK_GLUE_NATIVE_TESTS`.
`ctest -R "^r3\\.(w5_ledger|worker_contract|worker_tasks)\\."` completed
**39 / 39 PASS** on 2026-09-27.

| Matrix cell | Native proof |
|---|---|
| T13-B request conflict | `r3.w5_ledger.request_conflict` |
| T13-E stale notification | `r3.worker_contract.stale_stop_rejected`, `post_ack_old_mutation_closed`; task glue `post_ack_old_work_closed` |
| T13-F stale ACK | `r3.worker_contract.ack_exact_identity`, `ack_exactly_once`, `rebind_after_ack_new_generation` |
| T20 result ownership | `r3.w5_ledger.result_busy_release` |

The test sources link the actual ledger, result store, worker contract and
task-glue C code.  Required board cells are separately recorded under
`docs/evidence/r3/w5/`.
