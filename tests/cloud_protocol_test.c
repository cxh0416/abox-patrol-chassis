#include <stdio.h>
#include <string.h>

#include "cloud_protocol.h"

static int expect_status(const char* name, const char* json, uint8_t ota, CloudProtocolStatus_t expected,
                         CloudProtocolAction_t action, const char* request_id)
{
    CloudProtocolRequest_t request;
    CloudProtocolStatus_t actual = CloudProtocol_Parse(json, (uint16_t)strlen(json), ota, &request);
    if (actual != expected || request.action != action || strcmp(request.request_id, request_id) != 0)
    {
        fprintf(stderr, "%s failed: status=%d action=%d request=%s\n", name, actual, request.action,
                request.request_id);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const char get_info[] =
        "{\"version\":\"2.0\",\"timestamp\":0,\"data\":{\"requestId\":\"req-1\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_info\"}}}";
    static const char unknown[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-2\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"drive\"}}}";
    static const char can_diag[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-can\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_can_diag\"}}}";
    static const char get_calibration[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-cal-get\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_calibration\"}}}";
    static const char clear_calibration[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-cal-clear\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"clear_calibration\"}}}";
    static const char set_calibration[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-cal-set\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"set_calibration\","
        "\"steeringCenterCount\":-85123,\"absoluteCenterCount\":3182,"
        "\"steeringCountsPerDegreeMilli\":2710800,"
        "\"absoluteCountsPerDegreeMilli\":55250,\"steeringDirection\":1,"
        "\"absoluteDirection\":1,\"steeringToleranceMdeg\":2000}}}";
    static const char bad_calibration[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-cal-bad\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"set_calibration\","
        "\"steeringCenterCount\":-85123.5,\"absoluteCenterCount\":3182,"
        "\"steeringCountsPerDegreeMilli\":0,\"absoluteCountsPerDegreeMilli\":55250,"
        "\"steeringDirection\":0,\"absoluteDirection\":1,\"steeringToleranceMdeg\":2001}}}";
    static const char wrong_product[] =
        "{\"version\":\"2.0\",\"data\":{\"product\":\"top_flying_wing\",\"requestId\":\"req-wrong\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"get_info\"}}}";
    static const char ota[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-3\","
        "\"command\":\"ota_upgrade\",\"params\":{\"url\":\"https://ota.zxwl.ntiov.com:20443/fw-revisions/patrol_vehicle_chassis/patrol-chassis-1.1.1/patrol_vehicle_chassis_app.bin\","
        "\"version\":\"patrol-chassis-1.1.1\",\"size\":12345,\"crc32\":\"89ABCDEF\"}}}";
    static const char set_mqtt[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-4\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"set_mqtt\","
        "\"vid\":\"PATROL_CHASSIS-001\",\"host\":\"36.153.162.171\",\"port\":19683,"
        "\"username\":\"patrol\",\"password\":\"test-pass\"}}}";
    static const char bad_set_mqtt[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-5\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"set_mqtt\","
        "\"vid\":\"bad/vid\",\"host\":\"36.153.162.171\",\"port\":0,"
        "\"username\":\"patrol\",\"password\":\"test-pass\"}}}";
    static const char power_on[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-power-on\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"power_on\"}}}";
    static const char power_off[] =
        "{\"version\":\"2.0\",\"data\":{\"requestId\":\"req-power-off\","
        "\"command\":\"internal_config\",\"params\":{\"action\":\"power_off\"}}}";
    char oversized[1100];
    static const char oversized_prefix[] = "{\"requestId\":\"req-big\",\"padding\":\"";

    if (!expect_status("get_info", get_info, 0U, CLOUD_PROTOCOL_OK, CLOUD_PROTOCOL_ACTION_GET_INFO, "req-1")) return 1;
    if (!expect_status("get_can_diag", can_diag, 0U, CLOUD_PROTOCOL_OK, CLOUD_PROTOCOL_ACTION_GET_CAN_DIAG, "req-can")) return 1;
    if (!expect_status("get_calibration", get_calibration, 0U, CLOUD_PROTOCOL_OK,
                       CLOUD_PROTOCOL_ACTION_GET_CALIBRATION, "req-cal-get")) return 1;
    if (!expect_status("clear_calibration", clear_calibration, 0U, CLOUD_PROTOCOL_OK,
                       CLOUD_PROTOCOL_ACTION_CLEAR_CALIBRATION, "req-cal-clear")) return 1;
    if (!expect_status("set_calibration", set_calibration, 0U, CLOUD_PROTOCOL_OK,
                       CLOUD_PROTOCOL_ACTION_SET_CALIBRATION, "req-cal-set")) return 1;
    if (!expect_status("bad_calibration", bad_calibration, 0U, CLOUD_PROTOCOL_INVALID,
                       CLOUD_PROTOCOL_ACTION_NONE, "req-cal-bad")) return 1;
    if (!expect_status("unknown", unknown, 0U, CLOUD_PROTOCOL_UNKNOWN_ACTION, CLOUD_PROTOCOL_ACTION_NONE, "req-2")) return 1;
    if (!expect_status("wrong_product", wrong_product, 0U, CLOUD_PROTOCOL_INVALID,
                       CLOUD_PROTOCOL_ACTION_NONE, "req-wrong")) return 1;
    if (!expect_status("ota", ota, 1U, CLOUD_PROTOCOL_OK, CLOUD_PROTOCOL_ACTION_OTA, "req-3")) return 1;
    if (!expect_status("set_mqtt", set_mqtt, 0U, CLOUD_PROTOCOL_OK, CLOUD_PROTOCOL_ACTION_SET_MQTT, "req-4")) return 1;
    if (!expect_status("bad_set_mqtt", bad_set_mqtt, 0U, CLOUD_PROTOCOL_INVALID, CLOUD_PROTOCOL_ACTION_NONE, "req-5")) return 1;
    if (!expect_status("power_on", power_on, 0U, CLOUD_PROTOCOL_OK,
                       CLOUD_PROTOCOL_ACTION_POWER_ON, "req-power-on")) return 1;
    if (!expect_status("power_off", power_off, 0U, CLOUD_PROTOCOL_OK,
                       CLOUD_PROTOCOL_ACTION_POWER_OFF, "req-power-off")) return 1;
    {
        CloudProtocolRequest_t request;
        if (CloudProtocol_Parse(ota, (uint16_t)strlen(ota), 1U, &request) != CLOUD_PROTOCOL_OK ||
            request.ota_size != 12345U || request.ota_crc32 != 0x89ABCDEFU) return 1;
    }
    {
        CloudProtocolRequest_t request;
        if (CloudProtocol_Parse(set_calibration, (uint16_t)strlen(set_calibration), 0U, &request) !=
                CLOUD_PROTOCOL_OK || request.steering_center_count != -85123 ||
            request.absolute_center_count != 3182U ||
            request.steering_counts_per_degree_milli != 2710800U ||
            request.absolute_counts_per_degree_milli != 55250U ||
            request.steering_direction != 1 || request.absolute_direction != 1 ||
            request.steering_tolerance_mdeg != 2000U) return 1;
    }
    {
        CloudProtocolRequest_t request;
        if (CloudProtocol_Parse(set_mqtt, (uint16_t)strlen(set_mqtt), 0U, &request) != CLOUD_PROTOCOL_OK ||
            strcmp(request.mqtt_vid, "PATROL_CHASSIS-001") != 0 || request.mqtt_port != 19683U ||
            strcmp(request.mqtt_password, "test-pass") != 0) return 1;
    }

    memset(oversized, 'x', sizeof(oversized));
    memcpy(oversized, oversized_prefix, sizeof(oversized_prefix) - 1U);
    oversized[sizeof(oversized) - 2U] = '"';
    oversized[sizeof(oversized) - 1U] = '}';
    {
        CloudProtocolRequest_t request;
        CloudProtocolStatus_t status = CloudProtocol_Parse(oversized, sizeof(oversized), 0U, &request);
        if (status != CLOUD_PROTOCOL_TOO_LARGE || strcmp(request.request_id, "req-big") != 0) return 1;
    }
    return 0;
}
