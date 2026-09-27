#ifndef R3_RESULT_STORE_H
#define R3_RESULT_STORE_H

#include <stdint.h>

#include "stream_run_authority.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R3_RESULT_STORE_BYTES 32U

typedef enum
{
    R3_RESULT_STORE_OK = 0,
    R3_RESULT_STORE_BUSY,
    R3_RESULT_STORE_INVALID_ARGUMENT,
    R3_RESULT_STORE_INVALID_STATE,
    R3_RESULT_STORE_STALE_REFERENCE
} R3ResultStoreStatus;

typedef struct
{
    uint32_t valid;
    uint32_t result_id;
    StreamRunIdentity identity;
    uint32_t reference_count;
    uint8_t bytes[R3_RESULT_STORE_BYTES];
} R3ResultStoreSnapshot;

typedef struct
{
    R3ResultStoreSnapshot snapshot;
} R3ResultStore;

void R3ResultStore_Initialize(R3ResultStore *store);
R3ResultStoreStatus R3ResultStore_CanBeginRun(const R3ResultStore *store);
R3ResultStoreStatus R3ResultStore_Seal(R3ResultStore *store,
    const StreamRunIdentity *identity, uint32_t result_id);
R3ResultStoreStatus R3ResultStore_Acquire(R3ResultStore *store,
    uint32_t result_id, const uint8_t **out_bytes, uint32_t *out_size);
R3ResultStoreStatus R3ResultStore_Release(R3ResultStore *store,
    uint32_t result_id);
R3ResultStoreStatus R3ResultStore_GetSnapshot(const R3ResultStore *store,
    R3ResultStoreSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
