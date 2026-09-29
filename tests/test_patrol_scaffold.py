from __future__ import annotations

import hashlib
import importlib.util
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def motion_sources():
    return "\n".join((ROOT / "app/Core/User" / name).read_text(encoding="utf-8")
                     for name in ("system/src/patrol_rtos.c", "motion/src/center_calibration.c", "motion/src/patrol_runtime.c"))

def cloud_sources():
    return "\n".join((ROOT / "app/Core/User/cloud/src" / name).read_text(encoding="utf-8")
                     for name in ("cloud_service.c", "cloud_commands.c", "cloud_messages.c", "cloud_tx.c"))


class PatrolScaffoldTests(unittest.TestCase):
    def test_uart5_contract_matches_hal_without_changing_hardware(self):
        config = (ROOT / "platform_config/abox_product_config.h").read_text(encoding="utf-8")
        usart = (ROOT / "app/Core/Src/usart.c").read_text(encoding="utf-8")
        declared = re.search(r"0x08008000U,\s*0x0803F800U,\s*0x800U,\s*\d+U,\s*\d+U,\s*(\d+)U", config)
        actual = re.search(r"void MX_UART5_Init\(void\).*?huart5\.Init\.BaudRate\s*=\s*(\d+);", usart, re.S)
        self.assertIsNotNone(declared)
        self.assertIsNotNone(actual)
        self.assertEqual(int(declared.group(1)), int(actual.group(1)))

    def test_identity_and_safe_defaults(self):
        cmake = (ROOT / "app/CMakeLists.txt").read_text(encoding="utf-8")
        config = (ROOT / "platform_config/abox_product_config.h").read_text(encoding="utf-8")
        device_config = (ROOT / "app/Core/User/system/include/patrol_config.h").read_text(encoding="utf-8")
        rtos = motion_sources()
        self.assertIn("Patrol_Vehicle_Chassis_App", cmake)
        self.assertIn('"patrol_vehicle_chassis"', config)
        self.assertIn("PATROL_CHASSIS_ACTUATION_ENABLED", cmake)
        self.assertIn("PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED", cmake)
        self.assertIn("PATROL_CHASSIS_POWER_OUTPUT_ENABLED", cmake)
        self.assertIn('#define PATROL_CONFIG_DEFAULT_MQTT_USER ""', device_config)
        self.assertIn('#define PATROL_CONFIG_DEFAULT_MQTT_PASS ""', device_config)
        self.assertIn('#define PATROL_CONFIG_DEFAULT_MQTT_HOST ""', device_config)
        self.assertNotIn("contactor_feedback", rtos)
        self.assertIn("HAL_CAN_AddTxMessage", rtos)
        self.assertIn("request.data[0] == 0x40U", rtos)
        self.assertNotIn("PatrolCanopen_SdoWrite", rtos)

    def test_product_version_has_one_external_default_and_optional_script_override(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^patrol-chassis-\d+\.\d+\.\d+$")
        cmake = (ROOT / "app/CMakeLists.txt").read_text(encoding="utf-8")
        header = (ROOT / "app/Core/User/system/include/app_version.h").read_text(encoding="utf-8")
        helper = (ROOT / "tools/product_version.ps1").read_text(encoding="utf-8")
        self.assertIn('../VERSION', cmake)
        self.assertIn('PATROL_CHASSIS_APP_VERSION_OVERRIDE "" CACHE STRING', cmake)
        self.assertIn('"patrol-chassis-unversioned"', header)
        self.assertNotIn(version, header)
        self.assertIn("Resolve-PatrolChassisAppVersion", helper)
        for name in ("build_bench_steering.ps1", "build_online_debug.ps1", "build_release.ps1"):
            script = (ROOT / "tools" / name).read_text(encoding="utf-8")
            self.assertIn("product_version.ps1", script)
            self.assertIn("-Override $AppVersion", script)
            self.assertNotIn(version, script)

    def test_confirmed_vehicle_power_output_is_isolated_to_pvin_out1(self):
        port = (ROOT / "app/Core/User/board/src/vehicle_power_port.c").read_text(encoding="utf-8")
        self.assertIn("ABoxBoardIo_OutputSet(&g_board_io, ABOX_BOARD_IO_OUT1, on)", port)
        self.assertIn("allowed_outputs = (uint8_t)(1U << ABOX_BOARD_IO_OUT1)", port)
        self.assertNotIn("ABOX_BOARD_IO_OUT2", port)
        self.assertNotIn("ABOX_BOARD_IO_OUT3", port)
        self.assertNotIn("ABOX_BOARD_IO_RELAY", port)
        self.assertIn("VehiclePowerOutput_Init(write_power_output)", port)

    def test_public_profile_references_v4_and_documents_business_limits(self):
        mqtt = (ROOT / "docs/protocols/A-BOX巡防车底盘业务协议-V4.0.md").read_text(encoding="utf-8")
        canopen = (ROOT / "docs/operations/CANopen执行约束.md").read_text(encoding="utf-8")
        self.assertIn("A-BOX 设备公共 MQTT 协议 V4", mqtt)
        self.assertNotIn("/zxwl/abox/{deviceId}", mqtt)
        self.assertIn("vehicle_chassis", mqtt)
        self.assertIn("`±500 rpm`", canopen)
        self.assertIn("`±30°`", canopen)
        self.assertIn("连续三次", canopen)

    def test_public_and_internal_mqtt_contracts_are_separated(self):
        cloud = cloud_sources()
        remote = (ROOT / "app/Core/User/cloud/src/remote_service.c").read_text(encoding="utf-8")
        self.assertIn("ABoxMqttV4_BuildTopic", cloud)
        for kind in ("REQUEST", "RESPONSE", "REPORT", "MANIFEST", "HEARTBEAT"):
            self.assertIn(f"ABOX_MQTT_V4_TOPIC_{kind}", cloud)
        self.assertIn('"/zxwl/abox/%s/internal/config"', cloud)
        self.assertIn('\\"reportName\\":\\"chassis_status', cloud)
        self.assertIn('\\"product\\":\\"patrol_vehicle_chassis', cloud)
        self.assertNotIn("commandAccepted", remote)
        self.assertNotRegex(cloud, r"%[-+0-9.#]*[fFeEgGaA]")
        self.assertNotRegex(remote, r"%[-+0-9.#]*[fFeEgGaA]")
        self.assertIn('\\\"code\\\":%u', remote)
        transport = (ROOT / "platform/components/mqtt/src/abox_mqtt_ec800.c").read_text(encoding="utf-8")
        product_modules = (ROOT / "app/Core/User/modules.cmake").read_text(encoding="utf-8")
        self.assertIn("AT+QMTSUB=%u,%u", transport)
        self.assertIn("mqtt_core_adapter.c", product_modules)
        self.assertIn('\\\"transportDiagnostics\\\"', cloud)
        self.assertIn('\\\"ecDmaOverflows\\\"', cloud)
        self.assertNotIn('\\\"encoderTimeouts\\\":%lu}}}', cloud)

    def test_archive_hashes(self):
        archive = ROOT / "docs/archive/2026-08-31-input"
        expected = {
            "编码器.pdf": "031c0d55b0a2f421289989239f2d62317b76f57cd79bd4d474a8f91c44a9c14f",
            "底盘小车/底盘小车草图.pdf": "ff03d16394c9ecf3717b27b49b7544dbadf922e54f36947cc2fbfc250e56446a",
        }
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((archive / name).read_bytes()).hexdigest(), digest)

    def test_boot_trial_confirmation_has_flash_write_stack_margin(self):
        rtos = motion_sources()
        match = re.search(r"#define\s+PATROL_WATCHDOG_STACK_WORDS\s+(\d+)U", rtos)
        self.assertIsNotNone(match)
        # ARM release .su reports: Watchdog 32 + Confirm 128 + StateSave 592
        # + nested StateLoad 240 = 992 bytes before HAL flash helpers.
        self.assertGreaterEqual(int(match.group(1)), 384)

    def test_idle_center_calibration_lease_tolerates_platform_delay_without_latching_motion_fault(self):
        rtos = motion_sources()
        lease = re.search(r"#define\s+CENTER_CAL_READY_IDLE_MS\s+(\d+)U", rtos)
        self.assertIsNotNone(lease)
        self.assertGreaterEqual(int(lease.group(1)), 300000)
        helper = rtos[rtos.index("static uint8_t center_calibration_expire_idle"):
                      rtos.index("uint8_t CenterCalibration_Start")]
        self.assertIn("RemoteService_Revoke()", helper)
        self.assertIn("VehiclePowerOutput_Set(0U)", helper)
        self.assertIn("g_motion.bench.phase == BENCH_IDLE", helper)
        self.assertNotIn("RemoteService_ExecutionFault()", helper)
        self.assertNotIn("BenchSteering_Stop", helper)
        self.assertGreaterEqual(rtos.count("deadline_ms = now + CENTER_CAL_READY_IDLE_MS"), 2)
        self.assertIn("Reading status never extends the READY idle deadline", rtos)

    def test_center_calibration_freezes_structured_failure_and_rejection_evidence(self):
        rtos = motion_sources()
        cloud = cloud_sources()
        diagnostics = (ROOT / "app/Core/User/motion/src/patrol_diagnostics.c").read_text(encoding="utf-8")
        self.assertIn("CenterCalibration_CaptureFailureLocked", rtos)
        for token in ("diagVersion", "gateMask", "driverRaw", "feedbackAge",
                      "encoderAge", "encoderErrTotal", "lastReject", "lastAbort"):
            self.assertIn(token, rtos)
        self.assertIn("last_encoder_error", diagnostics)
        self.assertIn("encoderTransportErrors", diagnostics)
        rejected = cloud[cloud.index("if (!accepted) {"):cloud.index("} else {", cloud.index("if (!accepted) {"))]
        self.assertIn("CenterCalibration_Format", rejected)
        self.assertIn("CloudMessages_ScheduleResponse", rejected)

    def test_center_calibration_stop_keeps_commissioning_safety_context(self):
        rtos = motion_sources()
        executor = rtos[rtos.index("uint8_t center_requested ="):
                        rtos.index("BenchDrive_Tick", rtos.index("uint8_t center_requested ="))]
        self.assertIn("g_motion.center_cal.phase == CENTER_CAL_STOPPING", executor)
        self.assertIn("center_executor_active ? center_safe : normal_safe", executor)
        self.assertIn("uint8_t requested = remote_requested || center_requested", executor)

    def test_idle_coordinate_watchdog_ignores_non_position_motion_gates(self):
        rtos = motion_sources()
        angle = (ROOT / "app/Core/User/motion/src/steering_angle.c").read_text(encoding="utf-8")
        watchdog = rtos[rtos.index("else if (g_motion.coordinate.valid"):
                        rtos.index("SteeringCoordinate_Observe", rtos.index("else if (g_motion.coordinate.valid"))]
        self.assertIn("SteeringCoordinate_EpochLost", watchdog)
        self.assertNotIn("!snapshot.drive_online", watchdog)
        helper = angle[angle.index("uint8_t SteeringCoordinate_EpochLost"):
                       angle.index("uint8_t SteeringAngle_ToControllerCount")]
        self.assertIn("1U | 4U | 32U", helper)
        self.assertNotIn("2U |", helper)
        self.assertNotIn("16U", helper)

    def test_intentional_power_off_consumes_late_coordinate_reset_without_idle_fault(self):
        rtos = motion_sources()
        power_off = rtos.index("if (!VehiclePowerOutput_IsOn()) {")
        reset_request = rtos.index("if (g_motion.coordinate_reset_requested)", power_off)
        reset_block = rtos[reset_request:rtos.index("uint32_t PatrolRuntime_ApplyCut", reset_request)]
        self.assertLess(rtos.index("SteeringCoordinate_Reset(&g_motion.coordinate)", power_off), reset_request)
        self.assertIn("if (g_motion.coordinate.valid) BenchSteering_Stop(&g_motion.bench, now, 1)", reset_block)
        self.assertNotIn("\n                BenchSteering_Stop(&g_motion.bench, now, 1);", reset_block)

    def test_config_page_is_v5_and_unarmed_by_default(self):
        path = ROOT / "tools/make_device_config.py"
        spec = importlib.util.spec_from_file_location("patrol_config", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(module)
        page = module.build_config_page(vid="PATROL-001")
        details = module.inspect_config_page(page)
        self.assertEqual(len(page), 0x800)
        self.assertFalse(details["remoteEnabled"])
        self.assertFalse(details["mqttTlsEnabled"])
        self.assertEqual(details["mqttTlsProfileId"], 0)
        self.assertFalse(details["calibrated"])
        self.assertEqual(details["mqttPort"], 0)
        self.assertEqual(details["steeringCountsPerDegreeMilli"], 2710800)

    def test_config_page_round_trips_wheel_angle_calibration(self):
        path = ROOT / "tools/make_device_config.py"
        spec = importlib.util.spec_from_file_location("patrol_config_calibrated", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(module)
        page = module.build_config_page(
            vid="PATROL-001",
            steering_center_count=-85123,
            absolute_center_count=3182,
            steering_counts_per_degree_milli=2710800,
            absolute_counts_per_degree_milli=55250,
            calibrated=True,
        )
        details = module.inspect_config_page(page)
        self.assertEqual(details["steeringCenterCount"], -85123)
        self.assertEqual(details["absoluteCenterCount"], 3182)
        self.assertEqual(details["steeringCountsPerDegreeMilli"], 2710800)
        self.assertEqual(details["absoluteCountsPerDegreeMilli"], 55250)
        self.assertTrue(details["calibrated"])

    def test_config_tool_rejects_non_v5_layouts(self):
        path = ROOT / "tools/make_device_config.py"
        spec = importlib.util.spec_from_file_location("patrol_config_v5", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(module)
        current = module.build_config_page(vid="PATROL-LEGACY")
        for version, size in ((3, 392), (4, 396)):
            page = bytearray(b"\xff" * module.PAGE_SIZE)
            config = bytearray(current[module.CONFIG_OFFSET:module.CONFIG_OFFSET + module.CONFIG_SIZE])
            struct.pack_into("<HHI", config, 4, version, size, 0)
            page[module.CONFIG_OFFSET:module.CONFIG_OFFSET + module.CONFIG_SIZE] = config
            with self.assertRaisesRegex(ValueError, "header is invalid"):
                module.inspect_config_page(bytes(page))

    def test_ota_port_tracks_active_mqtt_client(self):
        source = (ROOT / "app/Core/User/ota/src/ota_service.c").read_text(encoding="utf-8")
        self.assertIn('#include "mqtt_runtime.h"', source)
        self.assertIn("return MqttRuntime_ActiveClient();", source)
        self.assertIn("port.mqtt_client_index = adapter_mqtt_client_index;", source)


if __name__ == "__main__":
    unittest.main()
