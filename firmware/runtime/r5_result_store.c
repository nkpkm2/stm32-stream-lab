#include "r5_result_store.h"

#include <string.h>

void R5ResultStore_Initialize(R5ResultStore *store)
{
    if (store != NULL) (void)memset(store, 0, sizeof(*store));
}

R5ResultStoreStatus R5ResultStore_CanBeginRun(const R5ResultStore *store)
{
    if (store == NULL) return R5_RESULT_STORE_INVALID_ARGUMENT;
    return ((store->valid != 0U) && (store->reference_count != 0U)) ?
        R5_RESULT_STORE_BUSY : R5_RESULT_STORE_OK;
}

R5ResultStoreStatus R5ResultStore_Seal(R5ResultStore *store,
    uint32_t result_id, const R5RunMetrics *metrics)
{
    if ((store == NULL) || (metrics == NULL) || (result_id == 0U))
    {
        return R5_RESULT_STORE_INVALID_ARGUMENT;
    }
    if ((metrics->phase != R5_METRICS_SEALED) ||
        (R5ResultStore_CanBeginRun(store) != R5_RESULT_STORE_OK))
    {
        return R5_RESULT_STORE_INVALID_STATE;
    }
    store->metrics = *metrics;
    store->result_id = result_id;
    store->reference_count = 0U;
    store->valid = 1U;
    return R5_RESULT_STORE_OK;
}

R5ResultStoreStatus R5ResultStore_Acquire(R5ResultStore *store,
    uint32_t result_id, const R5RunMetrics **out_metrics)
{
    if ((store == NULL) || (out_metrics == NULL))
    {
        return R5_RESULT_STORE_INVALID_ARGUMENT;
    }
    if ((store->valid == 0U) || (store->result_id != result_id))
    {
        return R5_RESULT_STORE_STALE_REFERENCE;
    }
    ++store->reference_count;
    if (store->reference_count == 0U) return R5_RESULT_STORE_INVALID_STATE;
    *out_metrics = &store->metrics;
    return R5_RESULT_STORE_OK;
}

R5ResultStoreStatus R5ResultStore_Release(R5ResultStore *store,
    uint32_t result_id)
{
    if (store == NULL) return R5_RESULT_STORE_INVALID_ARGUMENT;
    if ((store->valid == 0U) || (store->result_id != result_id) ||
        (store->reference_count == 0U))
    {
        return R5_RESULT_STORE_STALE_REFERENCE;
    }
    --store->reference_count;
    return R5_RESULT_STORE_OK;
}
