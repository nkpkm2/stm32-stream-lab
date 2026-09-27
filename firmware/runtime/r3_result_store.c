#include "r3_result_store.h"

#include <string.h>

void R3ResultStore_Initialize(R3ResultStore *store)
{
    if (store != NULL) (void)memset(store, 0, sizeof(*store));
}

R3ResultStoreStatus R3ResultStore_CanBeginRun(const R3ResultStore *store)
{
    if (store == NULL) return R3_RESULT_STORE_INVALID_ARGUMENT;
    return (store->snapshot.valid != 0U && store->snapshot.reference_count != 0U) ?
        R3_RESULT_STORE_BUSY : R3_RESULT_STORE_OK;
}

R3ResultStoreStatus R3ResultStore_Seal(R3ResultStore *store,
    const StreamRunIdentity *identity, uint32_t result_id)
{
    if ((store == NULL) || (identity == NULL) || (result_id == 0U))
        return R3_RESULT_STORE_INVALID_ARGUMENT;
    if (R3ResultStore_CanBeginRun(store) != R3_RESULT_STORE_OK)
        return R3_RESULT_STORE_BUSY;
    (void)memset(&store->snapshot, 0, sizeof(store->snapshot));
    store->snapshot.valid = 1U;
    store->snapshot.result_id = result_id;
    store->snapshot.identity = *identity;
    /* The canonical fixed-size staging payload is immutable after sealing.
     * It deliberately exposes identity bytes so a host can prove no result
     * crosses runs while a reference is held. */
    (void)memcpy(&store->snapshot.bytes[0], &identity->boot_id, sizeof(uint32_t));
    (void)memcpy(&store->snapshot.bytes[4], &identity->run_id, sizeof(uint32_t));
    (void)memcpy(&store->snapshot.bytes[8], &identity->generation, sizeof(uint32_t));
    (void)memcpy(&store->snapshot.bytes[12], &result_id, sizeof(uint32_t));
    return R3_RESULT_STORE_OK;
}

R3ResultStoreStatus R3ResultStore_Acquire(R3ResultStore *store,
    uint32_t result_id, const uint8_t **out_bytes, uint32_t *out_size)
{
    if ((store == NULL) || (out_bytes == NULL) || (out_size == NULL))
        return R3_RESULT_STORE_INVALID_ARGUMENT;
    if ((store->snapshot.valid == 0U) || (store->snapshot.result_id != result_id))
        return R3_RESULT_STORE_STALE_REFERENCE;
    ++store->snapshot.reference_count;
    if (store->snapshot.reference_count == 0U) return R3_RESULT_STORE_INVALID_STATE;
    *out_bytes = store->snapshot.bytes;
    *out_size = R3_RESULT_STORE_BYTES;
    return R3_RESULT_STORE_OK;
}

R3ResultStoreStatus R3ResultStore_Release(R3ResultStore *store,
    uint32_t result_id)
{
    if (store == NULL) return R3_RESULT_STORE_INVALID_ARGUMENT;
    if ((store->snapshot.valid == 0U) || (store->snapshot.result_id != result_id) ||
        (store->snapshot.reference_count == 0U))
        return R3_RESULT_STORE_STALE_REFERENCE;
    --store->snapshot.reference_count;
    return R3_RESULT_STORE_OK;
}

R3ResultStoreStatus R3ResultStore_GetSnapshot(const R3ResultStore *store,
    R3ResultStoreSnapshot *out)
{
    if ((store == NULL) || (out == NULL)) return R3_RESULT_STORE_INVALID_ARGUMENT;
    *out = store->snapshot;
    return R3_RESULT_STORE_OK;
}
