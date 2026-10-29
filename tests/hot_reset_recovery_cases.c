#define CENTER_HOST_REAL_CONTROL
#define FAULT_STOP_CUSTOM_TRANSPORT
#include "center_runtime_stubs.h"

static unsigned capacity, count;
static uint8_t reject_send;
static PatrolCanFrame frames[16];
void PatrolRuntimePort_AbortCanTx(void) {}
uint8_t PatrolRuntimePort_CanTxReady(void) { return capacity != 0U; }
uint8_t PatrolRuntimePort_SendCan(const PatrolCanFrame* frame)
{
    assert(capacity); /* A previous submission may consume the last mailbox. */
    if (reject_send) return 0U;
    --capacity;
    assert(count < 16U);
    frames[count++] = *frame;
    assert(frame->id == 0x601U || frame->id == 0x602U);
    assert(frame->dlc == 8U && frame->data[0] == 0x2BU);
    assert(frame->data[1] == 0x40U && frame->data[2] == 0x60U);
    assert(frame->data[3] == 0U && frame->data[4] == 6U);
    assert(frame->data[5] == 0U && frame->data[6] == 0U && frame->data[7] == 0U);
    return 1U;
}

static void reset(void)
{
    memset(&g_motion, 0, sizeof(g_motion));
    memset(&mock_control, 0, sizeof(mock_control));
    memset(&mock_config, 0, sizeof(mock_config));
    memset(&mock_snapshot, 0, sizeof(mock_snapshot));
    mock_now = 1000U; mock_power = mock_profile_ready = 1U;
    mock_bus_off = mock_ota = mock_cal_prepare = 0U;
    mock_config.steering_counts_per_degree_milli = VEHICLE_STEERING_COUNTS_MILLI;
    mock_config.absolute_counts_per_degree_milli = VEHICLE_ABSOLUTE_COUNTS_MILLI;
    mock_config.steering_direction = mock_config.absolute_direction = 1;
    PatrolRuntime_Init();
    g_motion.diagnostics.steering_status_word = 0x27U;
    g_motion.diagnostics.drive_status_word = 7U;
    g_motion.diagnostics.steering_status_ms = g_motion.diagnostics.drive_status_ms = mock_now;
    capacity = 3U; count = reject_send = 0U;
}

int main(void)
{
    reset();
    PatrolRuntime_Tick();
    if (!PATROL_CHASSIS_ACTUATION_ENABLED) {
        assert(count == 0U);
        return 0;
    }
    assert(count == 2U && frames[0].id != frames[1].id);
    assert(!mock_control.enable_request && !mock_control.session_active);
    /* Submission is not disable confirmation or authorization. Fresh disabled
     * feedback ends recovery; absent/stale feedback cannot grant motion. */
    g_motion.diagnostics.steering_status_word = g_motion.diagnostics.drive_status_word = 0x21U;
    count = 0U; capacity = 3U;
    PatrolRuntime_Tick(); assert(!count && !mock_control.enable_request);
    reset(); capacity = 1U;
    PatrolRuntime_Tick(); assert(count == 1U);
    capacity = 1U; mock_now += 10U;
    PatrolRuntime_Tick(); assert(count == 2U && frames[0].id != frames[1].id);
    reset(); reject_send = 1U;
    PatrolRuntime_Tick(); assert(!count && !mock_control.enable_request);
    reject_send = 0U; PatrolRuntime_Tick(); assert(count == 2U);
    reset(); mock_now = 1251U;
    PatrolRuntime_Tick(); assert(!count);
    reset(); mock_power = 0U;
    PatrolRuntime_Tick(); assert(!count);
    reset(); mock_bus_off = 1U;
    PatrolRuntime_Tick(); assert(!count);
    reset(); mock_control.session_active = 1U;
    PatrolRuntime_Tick(); assert(!count);
    reset(); g_motion.center_cal.phase = CENTER_CAL_READY;
    PatrolRuntime_Tick(); assert(!count);
    puts("PASS hot-reset recovery: shutdown only, no permit, stale/owner/power/bus gates, one mailbox and rejected submission");
    return 0;
}
