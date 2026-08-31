from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RamLayoutContractTests(unittest.TestCase):
    def test_app_keeps_mailbox_and_runtime_guard(self):
        linker = (ROOT / "app/STM32F105xx_FLASH.ld").read_text(encoding="utf-8")
        self.assertIn(".noinit 0x2000BC40", linker)
        self.assertIn("App BSS overlaps Boot/App fault mailbox", linker)
        self.assertIn("Boot/App fault mailbox address mismatch", linker)
        self.assertIn("App RAM guard below 4 KiB", linker)

    def test_boot_uses_same_mailbox(self):
        linker = (ROOT / "platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld").read_text(encoding="utf-8")
        self.assertIn("RAM_MAILBOX (xrw) : ORIGIN = 0x2000BC40", linker)
        self.assertIn("Boot minimum heap overlaps Boot/App fault mailbox", linker)
        self.assertIn("Boot/App fault mailbox address mismatch", linker)


if __name__ == "__main__":
    unittest.main()
