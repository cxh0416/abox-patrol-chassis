from __future__ import annotations

import hashlib
import importlib.util
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
        self.assertIn("PATROL_CHASSIS_POWER_OUTPUT_ENABLED", cmake)
        self.assertNotIn('#define VCU_CONFIG_DEFAULT_MQTT_USER ""', device_config)
        self.assertNotIn('#define VCU_CONFIG_DEFAULT_MQTT_PASS ""', device_config)
        self.assertIn("hardware_available=0", rtos)
        self.assertNotIn("HAL_CAN_AddTxMessage", rtos)

    def test_confirmed_vehicle_power_output_is_isolated_to_pvin_out1(self):
        port = (ROOT / "app/Core/User/Src/vehicle_power_port.c").read_text(encoding="utf-8")
        self.assertIn("PVIN_OUT1_GPIO_Port", port)
        self.assertIn("PVIN_OUT1_Pin", port)
        self.assertNotIn("PVIN_OUT2", port)
        self.assertNotIn("PVIN_OUT3", port)
        self.assertIn("VehiclePowerOutput_Init(write_power_output)", port)

    def test_public_topics_and_limits_are_documented(self):
        mqtt = (ROOT / "docs/protocols/A-BOX车辆底盘业务协议-V3.0.md").read_text(encoding="utf-8")
        canopen = (ROOT / "docs/protocols/CANopen执行约束.md").read_text(encoding="utf-8")
        self.assertIn("/zxwl/abox/{deviceId}", mqtt)
        self.assertIn("vehicle_chassis", mqtt)
        self.assertIn("±300 rpm", canopen)
        self.assertIn("±5°", canopen)
        self.assertIn("连续三次", canopen)

    def test_public_and_internal_mqtt_contracts_are_separated(self):
        cloud = (ROOT / "app/Core/User/Src/cloud_service.c").read_text(encoding="utf-8")
        remote = (ROOT / "app/Core/User/Src/remote_service.c").read_text(encoding="utf-8")
        self.assertIn('"/zxwl/abox/%s/ctrl"', cloud)
        self.assertIn('"/zxwl/abox/%s/status/report"', cloud)
        self.assertIn('\\"deviceType\\":\\"vehicle_chassis', cloud)
        self.assertIn('\\"product\\":\\"patrol_vehicle_chassis', cloud)
        self.assertNotIn("commandAccepted", remote)
        self.assertIn('\\\"code\\\":%u', remote)

    def test_archive_hashes(self):
        archive = ROOT / "docs/archive/2026-08-31-input"
        expected = {
            "编码器.pdf": "031c0d55b0a2f421289989239f2d62317b76f57cd79bd4d474a8f91c44a9c14f",
            "底盘小车/底盘小车草图.pdf": "ff03d16394c9ecf3717b27b49b7544dbadf922e54f36947cc2fbfc250e56446a",
        }
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((archive / name).read_bytes()).hexdigest(), digest)

    def test_config_page_is_v3_and_unarmed_by_default(self):
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


if __name__ == "__main__":
    unittest.main()
