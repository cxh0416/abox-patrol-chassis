#include "cloud_telemetry_scheduler_stubs.h"
static void drain(void)
{
    /* Complete the independent sends without simulating modem latency. */
    for (int i = 0; i < 4; ++i) { complete_publish(); CloudTx_Service(); }
}
int main(void)
{
    const ABoxCloudServiceOptions options = { CLOUD_IDLE_HEARTBEAT_MS, CLOUD_IDLE_STATUS_MS, 0U };
    assert(ABoxCloudService_Init(&g_cloud.cloud, &options, 0U));
    assert(ABoxCloudService_SetSyncStorage(&g_cloud.cloud, g_cloud.pending_sync_request_id,
                                          sizeof(g_cloud.pending_sync_request_id)));
    g_cloud.cloud.ready = 1U;
    drain();
    assert(status_packets == 1 && heartbeat_packets == 1);
    tick = 4999; drain();
    assert(status_packets == 1 && heartbeat_packets == 1);
    tick = 5000; drain();
    assert(status_packets == 1 && heartbeat_packets == 2);
    tick = 120000; drain();
    assert(status_packets == 2 && heartbeat_packets == 3);
    /* A maintenance read must wake status before the old 240000 deadline. */
    tick = 121000; CloudTelemetry_Wake(&g_cloud.telemetry, tick); drain();
    assert(status_packets == 3 && heartbeat_packets == 4);
    tick = 122000; drain(); assert(status_packets == 4);
    tick = 151000; drain(); assert(!g_cloud.telemetry.fast);
    int idle_count = status_packets;
    tick = 152000; drain(); assert(status_packets == idle_count);
    /* Active calibration remains fast even after the query window expires. */
    center_active = 1; drain(); assert(g_cloud.telemetry.fast);
    tick = 200000; drain(); assert(g_cloud.telemetry.fast);
    center_active = 0;
    tick = 229999; drain(); assert(g_cloud.telemetry.fast);
    tick = 230000; drain(); assert(!g_cloud.telemetry.fast);
    /* Faults bypass idle periods and retain reliable QoS. */
    event = "fault_raised"; tick = 230001; CloudTx_Service();
    assert(last_kind == CLOUD_TX_STATUS_REPORT && last_qos == 1);
    drain(); assert(g_cloud.telemetry.fast);
    /* Both ordinary control and preparatory bench permits hold high rate. */
    tick = 300000; control.session_active = 1; drain(); assert(g_cloud.telemetry.fast);
    control.session_active = 0; center_active = 1;
    tick = 340000; drain(); assert(g_cloud.telemetry.fast);
    center_active = 0; tick = 370000; drain(); assert(!g_cloud.telemetry.fast);
    /* A locally owned return remains observable without a remote lease. */
    auto_center_active = 1U; tick = 370001; drain(); assert(g_cloud.telemetry.fast);
    auto_center_active = 0U; tick = 400001; drain(); assert(!g_cloud.telemetry.fast);
    connected = 0; event = "safety_stop";
    int before = status_packets; drain(); assert(status_packets == before);
    connected = 1; CloudTx_Service(); assert(last_qos == 1 && status_packets == before + 1);
    /* Responses outrank telemetry and remain pending until broker confirmation. */
    drain();
    g_cloud.cloud.response_pending = 1;
    g_cloud.response_reboots = 1;
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_RESPONSE && g_cloud.cloud.response_pending && !g_cloud.reboot_pending);
    CloudTx_Service();
    assert(g_cloud.cloud.response_pending && cached_responses == 0);
    complete_publish();
    CloudTx_Service();
    assert(!g_cloud.cloud.response_pending && cached_responses == 1 && g_cloud.reboot_pending);
    assert(g_cloud.reboot_due == tick + CLOUD_REBOOT_DELAY_MS);
    g_cloud.cloud.response_pending = 1;
    g_cloud.response_reboots = 0;
    g_cloud.mqtt_candidate_pending = 1;
    CloudTx_Service();
    assert(accepted_count == 0);
    complete_publish();
    CloudTx_Service();
    assert(accepted_count == 1 && !g_cloud.mqtt_candidate_pending);
    drain();
    /* A sync immediately following get_info survives the previous PUBACK. */
    g_cloud.cloud.response_pending = 1;
    CloudTx_Service();
    strcpy(g_cloud.cloud.pending_sync_request_id, "sync-after-info");
    CloudTx_Service();
    assert(g_cloud.cloud.pending_sync_request_id[0] && !g_cloud.cloud.sync_pending);
    complete_publish();
    CloudTx_Service();
    assert(!g_cloud.cloud.pending_sync_request_id[0] && g_cloud.cloud.response_pending);
    assert(!strcmp(g_cloud.cloud.response_request_id, "sync-after-info"));
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_RESPONSE);
    drain();
    assert(ABoxCloudService_BeginExternal(&g_cloud.cloud, pub_operation, tick));
    pub_result = ABOX_MQTT_RECEIPT_FAILED;
    publish_active = 0;
    connected = 1;
    CloudTx_Service();
    assert(reconnect_count == 1 && !g_cloud.cloud.in_flight);
    /* A receipt from a different operation must not complete a response. */
    g_cloud.cloud.response_pending = 1;
    CloudTx_Service();
    assert(g_cloud.cloud.in_flight == ABOX_CLOUD_JOB_RESPONSE);
    int cached_before_stale = cached_responses;
    pub_operation++;
    pub_result = ABOX_MQTT_RECEIPT_CONFIRMED;
    publish_active = 0;
    CloudTx_Service();
    assert(g_cloud.cloud.response_pending && cached_responses == cached_before_stale);
    assert(reconnect_count == 2 && !g_cloud.cloud.in_flight);
    /* Product bootstrap uses the bounded Platform cursor; a late receipt
     * from an old connection cannot advance the new Manifest stage. */
    memset(&g_cloud, 0, sizeof(g_cloud));
    assert(ABoxCloudService_Init(&g_cloud.cloud, &options, 0U));
    assert(ABoxCloudService_SetSyncStorage(&g_cloud.cloud, g_cloud.pending_sync_request_id,
                                          sizeof(g_cloud.pending_sync_request_id)));
    tick = 0U;
    connected = 1U;
    g_cloud.cloud.heartbeat_due = 10000U;
    g_cloud.cloud.status_due = 10000U;
    ABoxCloudService_SetReady(&g_cloud.cloud, 1, tick);
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_MANIFEST && g_cloud.cloud.in_flight == ABOX_CLOUD_JOB_MANIFEST);
    uint32_t old_generation = g_cloud.cloud.generation;
    uint64_t old_operation = g_cloud.cloud.operation;
    CloudTx_NetworkChanged();
    assert(ABoxCloudService_Receipt(&g_cloud.cloud, old_generation,
                                    old_operation, 1, tick) == ABOX_CLOUD_JOB_NONE);
    ABoxCloudService_SetReady(&g_cloud.cloud, 1, tick);
    publish_active = 0U;
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_MANIFEST);
    complete_publish();
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_HEARTBEAT);
    complete_publish();
    CloudTx_Service();
    assert(last_kind == CLOUD_TX_STATUS_REPORT);
    complete_publish();
    CloudTx_Service();
    assert(!ABoxCloudService_Bootstrapping(&g_cloud.cloud));
    return 0;
}
