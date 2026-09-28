#include <stdio.h>
#include <string.h>

#include "r4_completion_witness.h"

#define CHECK(condition) do { if (!(condition)) return 1; } while (0)
#define COMPLETE_OPERATION 3U

static uint32_t AllPathFlags(void)
{
    return R4_COMPLETION_WITNESS_QUEUE_SPACE |
        R4_COMPLETION_WITNESS_ADAPTER_VALIDATED |
        R4_COMPLETION_WITNESS_LEDGER_COMMITTED |
        R4_COMPLETION_WITNESS_LEASE_RELEASED |
        R4_COMPLETION_WITNESS_QUEUE_SET_DISABLED;
}

static int CaseNormalComplete(void)
{
    R4CompletionWitness witness = {0};
    const uint32_t flags = AllPathFlags();

    CHECK(R4CompletionWitness_Record(&witness, 7U, COMPLETE_OPERATION, flags,
        UINT64_C(100), UINT64_C(180), UINT64_C(240)) == 1U);
    CHECK(witness.completion_ordinal == 7U);
    CHECK(witness.operation == COMPLETE_OPERATION);
    CHECK(witness.path_flags == flags);
    CHECK(witness.full_cycles == UINT64_C(140));
    CHECK(witness.prefix_cycles == UINT64_C(80));
    CHECK(witness.suffix_cycles == UINT64_C(60));
    CHECK(R4CompletionWitness_IsConsistent(&witness) == 1U);
    return 0;
}

static int CaseReplaceMax(void)
{
    R4CompletionWitness witness = {0};

    CHECK(R4CompletionWitness_Record(&witness, 4U, COMPLETE_OPERATION, 1U,
        UINT64_C(10), UINT64_C(20), UINT64_C(30)) == 1U);
    CHECK(R4CompletionWitness_Record(&witness, 5U, COMPLETE_OPERATION, 2U,
        UINT64_C(100), UINT64_C(160), UINT64_C(220)) == 1U);
    CHECK(witness.completion_ordinal == 5U);
    CHECK(witness.path_flags == 2U);
    CHECK(witness.full_cycles == UINT64_C(120));
    CHECK(witness.prefix_cycles + witness.suffix_cycles == witness.full_cycles);
    return 0;
}

static int CaseSmallerNoReplace(void)
{
    R4CompletionWitness witness = {0};

    CHECK(R4CompletionWitness_Record(&witness, 7U, COMPLETE_OPERATION, 3U,
        UINT64_C(100), UINT64_C(180), UINT64_C(240)) == 1U);
    CHECK(R4CompletionWitness_Record(&witness, 8U, COMPLETE_OPERATION, 4U,
        UINT64_C(300), UINT64_C(330), UINT64_C(390)) == 0U);
    CHECK(witness.completion_ordinal == 7U);
    CHECK(witness.path_flags == 3U);
    CHECK(witness.full_cycles == UINT64_C(140));
    return 0;
}

static int CaseEdgeCycles(void)
{
    R4CompletionWitness witness = {0};

    CHECK(R4CompletionWitness_Record(&witness, 1U, COMPLETE_OPERATION, 0U,
        UINT64_C(0), UINT64_C(0), UINT64_C(1)) == 1U);
    CHECK(witness.prefix_cycles == UINT64_C(0));
    CHECK(witness.suffix_cycles == UINT64_C(1));
    CHECK(witness.full_cycles == UINT64_C(1));
    CHECK(R4CompletionWitness_Record(&witness, 2U, COMPLETE_OPERATION, 0U,
        UINT64_C(10), UINT64_C(10), UINT64_C(11)) == 0U);
    CHECK(witness.completion_ordinal == 1U);
    return 0;
}

static int CaseResetIsolation(void)
{
    R4CompletionWitness prior = {0};
    R4CompletionWitness new_run = {0};

    CHECK(R4CompletionWitness_Record(&prior, 100U, COMPLETE_OPERATION, 1U,
        UINT64_C(100), UINT64_C(150), UINT64_C(250)) == 1U);
    CHECK(R4CompletionWitness_Record(&new_run, 1U, COMPLETE_OPERATION, 2U,
        UINT64_C(10), UINT64_C(15), UINT64_C(20)) == 1U);
    CHECK(new_run.completion_ordinal == 1U);
    CHECK(new_run.path_flags == 2U);
    CHECK(new_run.full_cycles == UINT64_C(10));
    CHECK(R4CompletionWitness_IsConsistent(&prior) == 1U);
    CHECK(R4CompletionWitness_IsConsistent(&new_run) == 1U);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return 2;
    }
    if (strcmp(argv[1], "normal_complete") == 0) return CaseNormalComplete();
    if (strcmp(argv[1], "replace_max") == 0) return CaseReplaceMax();
    if (strcmp(argv[1], "smaller_no_replace") == 0) return CaseSmallerNoReplace();
    if (strcmp(argv[1], "edge_cycles") == 0) return CaseEdgeCycles();
    if (strcmp(argv[1], "reset_isolation") == 0) return CaseResetIsolation();
    return 2;
}
