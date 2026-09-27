#ifndef R3_W2_HW_AUTHORITY_SHIM_H
#define R3_W2_HW_AUTHORITY_SHIM_H

#include <stdint.h>

#include "stream_run_authority.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t initialized;
    uint32_t ready_valid;
    uint32_t held_valid;
    uint32_t processing_valid;
    uint32_t publish_count;
    uint32_t take_count;
    uint32_t claim_count;
    uint32_t cancel_count;
    uint32_t complete_count;
    uint32_t commit_serial;
    StreamOwnershipDescriptor ready;
    StreamOwnershipDescriptor held;
    StreamOwnershipDescriptor processing;
} R3W2HwAuthorityShimSnapshot;

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_Reset(
    const StreamRunTicket *ticket);

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_PublishReady(
    const StreamRunTicket *ticket,
    const StreamOwnershipDescriptor *ownership);

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_GetHarnessSnapshot(
    R3W2HwAuthorityShimSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif /* R3_W2_HW_AUTHORITY_SHIM_H */
