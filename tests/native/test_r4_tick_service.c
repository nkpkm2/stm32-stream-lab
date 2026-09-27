#include "r4_tick_service.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #x, __FILE__, __LINE__); exit(EXIT_FAILURE); } } while (0)

typedef struct
{
    uint64_t now;
    uint64_t start_at;
    uint64_t release_at[4];
    uint32_t start_count;
    uint32_t release_count;
} Fake;

static uint64_t Now(void *context) { return ((Fake *)context)->now; }
static int Start(uint64_t seq, void *context)
{
    Fake *fake = (Fake *)context;
    fake->start_at = seq;
    ++fake->start_count;
    return 1;
}
static int Release(uint64_t seq, uint64_t cycle, void *context)
{
    Fake *fake = (Fake *)context;
    fake->release_at[fake->release_count] = seq;
    ++fake->release_count;
    fake->now = cycle;
    return 1;
}
static R4_TickService New(Fake *fake)
{
    R4_TickService service;
    R4_TickServiceCallbacks c;
    c.now = Now; c.commit_start = Start; c.release_job = Release; c.context = fake;
    CHECK(R4_TickService_Initialize(&service, &c) == R4_TICK_SERVICE_OK);
    return service;
}
static void Service(R4_TickService *service, Fake *fake, uint64_t cycle)
{
    fake->now = cycle;
    CHECK(R4_TickService_OnService(service) == R4_TICK_SERVICE_OK);
}
static void CaseSuspendedVector(void)
{
    Fake fake = { 0U, 0U, { 0U }, 0U, 0U };
    R4_TickService service = New(&fake);
    CHECK(R4_TickService_ArmStart(&service, 2U, 2U) == R4_TICK_SERVICE_OK);
    Service(&service, &fake, 1000U);
    Service(&service, &fake, 2000U); /* q0/job0 */
    CHECK(fake.start_at == 2U && fake.release_at[0] == 2U);
    Service(&service, &fake, 3000U);
    Service(&service, &fake, 4000U); /* occupied -> skip */
    Service(&service, &fake, 5000U);
    Service(&service, &fake, 6000U); /* occupied -> skip */
    Service(&service, &fake, 7000U);
    Service(&service, &fake, 8000U); /* occupied -> skip */
    CHECK(service.skipped_count == 3U);
    CHECK(R4_TickService_CompleteJob(&service) == R4_TICK_SERVICE_OK);
    Service(&service, &fake, 9000U);
    Service(&service, &fake, 10000U); /* next preserved release */
    CHECK(fake.release_at[1] == 10U);
    CHECK(service.service_seq == 10U);
}
static void CaseNoCatchup(void)
{
    Fake fake = { 0U, 0U, { 0U }, 0U, 0U };
    R4_TickService service = New(&fake);
    CHECK(R4_TickService_ArmStart(&service, 3U, 3U) == R4_TICK_SERVICE_OK);
    Service(&service, &fake, 1U);
    Service(&service, &fake, 2U);
    Service(&service, &fake, 3U);
    CHECK(service.release_count == 1U);
    CHECK(R4_TickService_CompleteJob(&service) == R4_TICK_SERVICE_OK);
    Service(&service, &fake, 4U);
    Service(&service, &fake, 5U);
    Service(&service, &fake, 6U);
    CHECK(service.release_count == 2U);
    CHECK(fake.release_at[1] == 6U);
}
static void CaseFutureTicket(void)
{
    Fake fake = { 0U, 0U, { 0U }, 0U, 0U };
    R4_TickService service = New(&fake);
    CHECK(R4_TickService_ArmStart(&service, 1U, 2U) == R4_TICK_SERVICE_INVALID_STATE);
    CHECK(R4_TickService_ArmStart(&service, 2U, 2U) == R4_TICK_SERVICE_OK);
    CHECK(R4_TickService_CancelStart(&service) == R4_TICK_SERVICE_OK);
    Service(&service, &fake, 1U);
    Service(&service, &fake, 2U);
    CHECK(service.start_count == 0U);
}
int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    if (strcmp(argv[1], "suspension") == 0) CaseSuspendedVector();
    else if (strcmp(argv[1], "no_catchup") == 0) CaseNoCatchup();
    else if (strcmp(argv[1], "future_ticket") == 0) CaseFutureTicket();
    else return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
