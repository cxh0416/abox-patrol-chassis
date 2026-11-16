"""Exercise both product clock trees and GD CAN timing against platform backends."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / "platform"


class ProductClockTests(unittest.TestCase):
    def compile_and_run(self, sources, includes, definitions=()):
        gcc = shutil.which("gcc")
        self.assertIsNotNone(gcc, "host gcc is required for clock tests")
        with tempfile.TemporaryDirectory(prefix="patrol-clock-") as temp:
            binary = Path(temp) / "test.exe"
            command = [gcc, "-std=c11", "-Wall", "-Wextra"]
            command += [f"-D{item}" for item in definitions]
            command += [f"-I{item}" for item in includes]
            command += [str(item) for item in sources] + ["-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)

    def test_product_clock_backends(self):
        includes = [PLATFORM / "tests/clock_stub", PLATFORM / "hardware/api/include",
                    PLATFORM / "hardware/internal/include", PLATFORM / "components/core/include",
                    ROOT / "platform_config"]
        for mcu, definitions in (("stm32f105", ("TEST_STM32=1",)),
                                 ("gd32f105", ("ABOX_MCU_GD32F105=1",))):
            with self.subTest(mcu=mcu):
                self.compile_and_run(
                    [ROOT / "tests/patrol_clock_backend_test.c",
                     PLATFORM / "hardware/api/src/abox_mcu_clock.c",
                     PLATFORM / f"hardware/mcu/{mcu}/clock_{mcu}.c"],
                    includes, definitions)

    def test_gd_both_can_buses_at_108mhz(self):
        self.compile_and_run(
            [ROOT / "tests/patrol_gd_can_clock_test.c",
             PLATFORM / "hardware/mcu/gd32f105/can_port_gd32f105.c",
             PLATFORM / "hardware/api/src/abox_can_board.c",
             PLATFORM / "hardware/boards/abox_v1/board_can_gd32f105.c"],
            [PLATFORM / "tests/gd32_can_stub", PLATFORM / "hardware/api/include",
             PLATFORM / "hardware/internal/include", ROOT / "app/Core/User/system/include"])

    def test_product_uart_baud_at_both_clock_rates(self):
        includes = [PLATFORM / "hardware/api/include", PLATFORM / "hardware/internal/include",
                    PLATFORM / "components/core/include", PLATFORM / "components/ec800/include",
                    ROOT / "platform_config"]
        sources = [ROOT / "tests/patrol_uart_clock_test.c", PLATFORM / "hardware/mcu/uart_f105.c",
                   PLATFORM / "hardware/api/src/abox_dma_resource.c",
                   PLATFORM / "components/ec800/src/abox_ec800_rx.c"]
        for mcu, definitions in (("stm32f105", ()), ("gd32f105", ("ABOX_MCU_GD32F105=1",))):
            with self.subTest(mcu=mcu):
                self.compile_and_run(sources, includes, definitions)


if __name__ == "__main__":
    unittest.main()
