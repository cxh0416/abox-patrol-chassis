static void drain(void)
{
    /* Complete the independent sends without simulating modem latency. */
    for (int i = 0; i < 4; ++i) service_tx();
}
int main(void)
{
    drain();
    assert(status_packets == 1 && heartbeat_packets == 1);
    tick = 59999; drain();
    assert(status_packets == 1 && heartbeat_packets == 1);
    tick = 60000; drain();
    assert(status_packets == 1 && heartbeat_packets == 2);
    tick = 120000; drain();
    assert(status_packets == 2 && heartbeat_packets == 3);
    /* A maintenance read must wake status before the old 240000 deadline. */
    tick = 121000; CloudTelemetry_Wake(&g_telemetry, tick); drain();
    assert(status_packets == 3 && heartbeat_packets == 4);
    tick = 122000; drain(); assert(status_packets == 4);
    tick = 151000; drain(); assert(!g_telemetry.fast);
    int idle_count = status_packets;
    tick = 152000; drain(); assert(status_packets == idle_count);
    /* Active calibration remains fast even after the query window expires. */
    center_active = 1; drain(); assert(g_telemetry.fast);
    tick = 200000; drain(); assert(g_telemetry.fast);
    center_active = 0;
    tick = 229999; drain(); assert(g_telemetry.fast);
    tick = 230000; drain(); assert(!g_telemetry.fast);
    /* Faults bypass idle periods and retain reliable QoS. */
    event = "fault_raised"; tick = 230001; service_tx();
    assert(last_kind == CLOUD_TX_STATUS_REPORT && last_qos == 1);
    drain(); assert(g_telemetry.fast);
    /* Both ordinary control and preparatory bench permits hold high rate. */
    tick = 300000; control.session_active = 1; drain(); assert(g_telemetry.fast);
    control.session_active = 0; bench_active = 1;
    tick = 340000; drain(); assert(g_telemetry.fast);
    bench_active = 0; tick = 370000; drain(); assert(!g_telemetry.fast);
    connected = 0; event = "safety_stop";
    int before = status_packets; drain(); assert(status_packets == before);
    connected = 1; service_tx(); assert(last_qos == 1 && status_packets == before + 1);
    return 0;
}
