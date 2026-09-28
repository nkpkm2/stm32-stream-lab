#include <stddef.h>

#include "r4_completion_witness.h"

uint32_t R4CompletionWitness_Record(R4CompletionWitness *witness,
    uint32_t ordinal, uint32_t operation, uint32_t path_flags,
    uint64_t t_lock, uint64_t t_commit, uint64_t t_unlock)
{
    uint64_t prefix;
    uint64_t suffix;
    uint64_t full;

    if ((witness == NULL) || (ordinal == 0U) || (t_commit < t_lock) ||
        (t_unlock < t_commit))
    {
        if (witness != NULL)
        {
            ++witness->consistency_failures;
        }
        return 0U;
    }

    prefix = t_commit - t_lock;
    suffix = t_unlock - t_commit;
    full = t_unlock - t_lock;
    if (full != (prefix + suffix))
    {
        ++witness->consistency_failures;
        return 0U;
    }
    if ((witness->valid != 0U) && (full <= witness->full_cycles))
    {
        return 0U;
    }

    witness->completion_ordinal = ordinal;
    witness->operation = operation;
    witness->path_flags = path_flags;
    witness->full_cycles = full;
    witness->prefix_cycles = prefix;
    witness->suffix_cycles = suffix;
    witness->t_lock = t_lock;
    witness->t_commit = t_commit;
    witness->t_unlock = t_unlock;
    witness->valid = 1U;
    return 1U;
}

uint32_t R4CompletionWitness_IsConsistent(const R4CompletionWitness *witness)
{
    if ((witness == NULL) || (witness->valid == 0U) ||
        (witness->consistency_failures != 0U))
    {
        return 0U;
    }
    return witness->full_cycles ==
        (witness->prefix_cycles + witness->suffix_cycles) ? 1U : 0U;
}
