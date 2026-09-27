#ifndef R5_RESULT_STORE_H
#define R5_RESULT_STORE_H

#include <stdint.h>

#include "r5_run_metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    R5_RESULT_STORE_OK = 0,
    R5_RESULT_STORE_BUSY,
    R5_RESULT_STORE_INVALID_ARGUMENT,
    R5_RESULT_STORE_INVALID_STATE,
    R5_RESULT_STORE_STALE_REFERENCE
} R5ResultStoreStatus;

typedef struct
{
    uint32_t valid;
    uint32_t result_id;
    uint32_t reference_count;
    R5RunMetrics metrics;
} R5ResultStore;

void R5ResultStore_Initialize(R5ResultStore *store);
R5ResultStoreStatus R5ResultStore_CanBeginRun(const R5ResultStore *store);
R5ResultStoreStatus R5ResultStore_Seal(R5ResultStore *store,
    uint32_t result_id, const R5RunMetrics *metrics);
R5ResultStoreStatus R5ResultStore_Acquire(R5ResultStore *store,
    uint32_t result_id, const R5RunMetrics **out_metrics);
R5ResultStoreStatus R5ResultStore_Release(R5ResultStore *store,
    uint32_t result_id);

#ifdef __cplusplus
}
#endif

#endif /* R5_RESULT_STORE_H */
