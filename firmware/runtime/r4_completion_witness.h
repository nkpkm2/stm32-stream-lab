#ifndef R4_COMPLETION_WITNESS_H
#define R4_COMPLETION_WITNESS_H

#include <stdint.h>

/* Diagnostic-only same-event witness for T04 root-cause work.  It is never
 * compiled into the production accounting profile. */
typedef enum
{
    R4_COMPLETION_WITNESS_QUEUE_SPACE = UINT32_C(1) << 0,
    R4_COMPLETION_WITNESS_ADAPTER_VALIDATED = UINT32_C(1) << 1,
    R4_COMPLETION_WITNESS_LEDGER_COMMITTED = UINT32_C(1) << 2,
    R4_COMPLETION_WITNESS_LEASE_RELEASED = UINT32_C(1) << 3,
    R4_COMPLETION_WITNESS_QUEUE_SET_DISABLED = UINT32_C(1) << 4
} R4CompletionWitnessPathFlag;

typedef struct
{
    uint32_t valid;
    uint32_t completion_ordinal;
    uint32_t operation;
    uint32_t path_flags;
    uint64_t full_cycles;
    uint64_t prefix_cycles;
    uint64_t suffix_cycles;
    uint64_t t_lock;
    uint64_t t_commit;
    uint64_t t_unlock;
    uint32_t consistency_failures;
} R4CompletionWitness;

/* Records exactly one replacement witness when full_cycles exceeds the prior
 * witness.  The caller must invoke this only after t_unlock is captured. */
uint32_t R4CompletionWitness_Record(R4CompletionWitness *witness,
    uint32_t ordinal, uint32_t operation, uint32_t path_flags,
    uint64_t t_lock, uint64_t t_commit, uint64_t t_unlock);

uint32_t R4CompletionWitness_IsConsistent(const R4CompletionWitness *witness);

#endif /* R4_COMPLETION_WITNESS_H */
