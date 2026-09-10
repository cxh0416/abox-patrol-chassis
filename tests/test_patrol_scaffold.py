from __future__ import annotations

import hashlib
import importlib.util
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PatrolScaffoldTests(unittest.TestCase):
    def test_identity_and_safe_defaults(self):
        cmake = (ROOT / "app/CMakeLists.txt").read_text(encoding="utf-8")
        config = (ROOT / "platform_config/abox_product_config.h").read_text(encoding="utf-8")
        device_config = (ROOT / "app/Core/User/Inc/vcu_config.h").read_text(encoding="utf-8")
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        self.assertIn("Patrol_Vehicle_Chassis_App", cmake)
        self.assertIn('"patrol_vehicle_chassis"', config)
        self.assertIn("PATROL_CHASSIS_ACTUATION_ENABLED", cmake)
        self.assertIn("PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED", cmake)
        self.assertIn("PATROL_CHASSIS_POWER_OUTPUT_ENABLED", cmake)
        self.assertNotIn('#define VCU_CONFIG_DEFAULT_MQTT_USER ""', device_config)
        self.assertNotIn('#define VCU_CONFIG_DEFAULT_MQTT_PASS ""', device_config)
        self.assertIn("hardware_available=0", rtos)
        self.assertIn("HAL_CAN_AddTxMessage", rtos)
        self.assertIn("request.data[0] == 0x40U", rtos)
        self.assertNotIn("PatrolCanopen_SdoWrite", rtos)

    def test_product_version_has_one_external_default_and_optional_script_override(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, r"^patrol-chassis-\d+\.\d+\.\d+$")
        cmake = (ROOT / "app/CMakeLists.txt").read_text(encoding="utf-8")
        header = (ROOT / "app/Core/User/Inc/app_version.h").read_text(encoding="utf-8")
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
        port = (ROOT / "app/Core/User/Src/vehicle_power_port.c").read_text(encoding="utf-8")
        self.assertIn("PVIN_OUT1_GPIO_Port", port)
        self.assertIn("PVIN_OUT1_Pin", port)
        self.assertNotIn("PVIN_OUT2", port)
        self.assertNotIn("PVIN_OUT3", port)
        self.assertIn("VehiclePowerOutput_Init(write_power_output)", port)

    def test_public_topics_and_limits_are_documented(self):
        mqtt = (ROOT / "docs/protocols/A-BOX车辆底盘业务协议-V3.0.md").read_text(encoding="utf-8")
        canopen = (ROOT / "docs/operations/CANopen执行约束.md").read_text(encoding="utf-8")
        self.assertIn("/zxwl/abox/{deviceId}", mqtt)
        self.assertIn("vehicle_chassis", mqtt)
        self.assertIn("`±200 rpm`", canopen)
        self.assertIn("`±30°`", canopen)
        self.assertIn("连续三次", canopen)

    def test_public_and_internal_mqtt_contracts_are_separated(self):
        cloud = (ROOT / "app/Core/User/Src/cloud_service.c").read_text(encoding="utf-8")
        remote = (ROOT / "app/Core/User/Src/remote_service.c").read_text(encoding="utf-8")
        self.assertIn('"/zxwl/abox/%s/ctrl"', cloud)
        self.assertIn('"/zxwl/abox/%s/status/report"', cloud)
        self.assertIn('\\"deviceType\\":\\"vehicle_chassis', cloud)
        self.assertIn('\\"product\\":\\"patrol_vehicle_chassis', cloud)
        self.assertNotIn("commandAccepted", remote)
        self.assertNotRegex(cloud, r"%[-+0-9.#]*[fFeEgGaA]")
        self.assertNotRegex(remote, r"%[-+0-9.#]*[fFeEgGaA]")
        self.assertIn('\\\"code\\\":%u', remote)
        mqtt_core = (ROOT / "app/Core/User/Src/mqtt_core.c").read_text(encoding="utf-8")
        self.assertIn('AT+QMTSUB=%d,1,\\\"%s\\\",1', mqtt_core)
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
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        match = re.search(r"#define\s+VCU_WATCHDOG_STACK_WORDS\s+(\d+)U", rtos)
        self.assertIsNotNone(match)
        # ARM release .su reports: Watchdog 32 + Confirm 128 + StateSave 592
        # + nested StateLoad 240 = 992 bytes before HAL flash helpers.
        self.assertGreaterEqual(int(match.group(1)), 384)

    def test_idle_center_calibration_lease_tolerates_platform_delay_without_latching_motion_fault(self):
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        lease = re.search(r"#define\s+CENTER_CAL_READY_LEASE_MS\s+(\d+)U", rtos)
        self.assertIsNotNone(lease)
        self.assertGreaterEqual(int(lease.group(1)), 15000)
        helper = rtos[rtos.index("static uint8_t center_calibration_expire_idle"):
                      rtos.index("uint8_t VcuRtos_CenterCalibrationStart")]
        self.assertIn("RemoteService_BenchRevoke()", helper)
        self.assertIn("VehiclePowerOutput_Set(0U)", helper)
        self.assertIn("g_bench.phase == BENCH_IDLE", helper)
        self.assertNotIn("RemoteService_BenchFault()", helper)
        self.assertNotIn("BenchSteering_Stop", helper)
        self.assertGreaterEqual(rtos.count("deadline_ms = now + CENTER_CAL_READY_LEASE_MS"), 2)

    def test_center_calibration_freezes_structured_failure_and_rejection_evidence(self):
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        cloud = (ROOT / "app/Core/User/Src/cloud_service.c").read_text(encoding="utf-8")
        diagnostics = (ROOT / "app/Core/User/Src/patrol_diagnostics.c").read_text(encoding="utf-8")
        self.assertIn("center_capture_failure_locked", rtos)
        for token in ("diagVersion", "gateMask", "driverRaw", "feedbackAge",
                      "encoderAge", "encoderErrTotal", "lastReject", "lastAbort"):
            self.assertIn(token, rtos)
        self.assertIn("last_encoder_error", diagnostics)
        self.assertIn("encoderTransportErrors", diagnostics)
        rejected = cloud[cloud.index("if (!accepted) {"):cloud.index("} else {", cloud.index("if (!accepted) {"))]
        self.assertIn("VcuRtos_FormatCenterCalibration", rejected)
        self.assertIn("schedule_response", rejected)

    def test_center_calibration_stop_keeps_commissioning_safety_context(self):
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        executor = rtos[rtos.index("uint8_t center_requested ="):
                        rtos.index("BenchDrive_Tick", rtos.index("uint8_t center_requested ="))]
        self.assertIn("g_center_cal.phase == CENTER_CAL_STOPPING", executor)
        self.assertIn("center_executor_active ? center_safe : normal_safe", executor)
        self.assertIn("uint8_t requested = remote_requested || center_requested", executor)

    def test_idle_coordinate_watchdog_ignores_non_position_motion_gates(self):
        rtos = (ROOT / "app/Core/User/Src/vcu_rtos.c").read_text(encoding="utf-8")
        angle = (ROOT / "app/Core/User/Src/steering_angle.c").read_text(encoding="utf-8")
        watchdog = rtos[rtos.index("else if (g_coordinate.valid"):
                        rtos.index("SteeringCoordinate_Observe", rtos.index("else if (g_coordinate.valid"))]
        self.assertIn("SteeringCoordinate_EpochLost", watchdog)
        self.assertNotIn("!snapshot.drive_online", watchdog)
        helper = angle[angle.index("uint8_t SteeringCoordinate_EpochLost"):
                       angle.index("uint8_t SteeringAngle_ToControllerCount")]
        self.assertIn("1U | 4U | 32U", helper)
        self.assertNotIn("2U |", helper)
        self.assertNotIn("16U", helper)

    def test_config_page_is_v4_and_unarmed_by_default(self):
        path = ROOT / "tools/make_device_config.py"
        spec = importlib.util.spec_from_file_location("patrol_config", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(module)
        page = module.build_config_page(vid="PATROL-001", password="local-test-secret")
        details = module.inspect_config_page(page)
        self.assertEqual(len(page), 0x800)
        self.assertFalse(details["remoteEnabled"])
        self.assertFalse(details["calibrated"])
        self.assertEqual(details["steeringCountsPerDegreeMilli"], 2710800)

    def test_config_page_round_trips_wheel_angle_calibration(self):
        path = ROOT / "tools/make_device_config.py"
        spec = importlib.util.spec_from_file_location("patrol_config_calibrated", path)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(module)
        page = module.build_config_page(
            vid="PATROL-001",
            password="local-test-secret",
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


if __name__ == "__main__":
    unittest.main()
