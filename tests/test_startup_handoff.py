"""Execute the actual main startup prefix against failed HAL/tick handoffs."""
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StartupHandoffTests(unittest.TestCase):
    def test_boot_app_boundary_and_tick_failure(self):
        main = (ROOT / "app/Core/Src/main.c").read_text(encoding="utf-8")
        body = main.split("int main(void)", 1)[1].split("{", 1)[1]
        prefix = body.split("SystemClock_Config();", 1)[0] + "SystemClock_Config();"
        boot = (ROOT / "platform/targets/stm32f105_abox_boot/Core/User/Src/boot_jump.c").read_text(encoding="utf-8")
        boot = re.sub(r"/\*.*?\*/|//[^\n]*", "", boot, flags=re.S)
        jump = boot.split("void Boot_JumpToApp", 1)[1]
        self.assertLess(jump.index("__disable_irq();"), jump.index("app_entry();"))
        self.assertIn("NVIC->ICER[i] = 0xFFFFFFFFU", jump)
        self.assertIn("NVIC->ICPR[i] = 0xFFFFFFFFU", jump)
        self.assertNotIn("__enable_irq();", jump)
        config = (ROOT / "shared/include/boot_cfg.h").read_text(encoding="utf-8")
        self.assertIn("#define APP_START_ADDR 0x08008000U", config)
        linker = (ROOT / "app/STM32F105xx_FLASH.ld").read_text(encoding="utf-8")
        self.assertRegex(linker, r"FLASH[^\n]*ORIGIN = 0x08008000")
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
#define APP_START_ADDR 0x08008000U
#define TIM_CR1_CEN 1U
#define TIM_DIER_UIE 1U
#define TIM1_UP_IRQn 25U
#define HAL_OK 0
static struct { uint32_t VTOR; } scb;
static struct { uint32_t CR1, DIER; } tim;
#define SCB (&scb)
#define TIM1 (&tim)
static int mode, primask, hal_called, enabled, clock_called, nvic_enabled;
static jmp_buf failure;
static void __DSB(void) {}
static void __ISB(void) {}
static void Error_Handler(void) { longjmp(failure, 1); }
static int HAL_Init(void) {
    assert(SCB->VTOR == APP_START_ADDR);
    hal_called = 1;
    TIM1->CR1 = mode == 2 ? 0U : TIM_CR1_CEN;
    TIM1->DIER = mode == 3 ? 0U : TIM_DIER_UIE;
    nvic_enabled = mode != 4;
    if (mode == 5) SCB->VTOR = 0x08000000U;
    return mode == 1 ? 1 : HAL_OK;
}
static unsigned NVIC_GetEnableIRQ(unsigned irq) {
    assert(irq == TIM1_UP_IRQn); return (unsigned)nvic_enabled;
}
static void __enable_irq(void) {
    assert(hal_called && SCB->VTOR == APP_START_ADDR);
    assert(TIM1->CR1 && TIM1->DIER && nvic_enabled);
    primask = 0; ++enabled;
}
static void SystemClock_Config(void) {
    assert(!primask && enabled == 1); ++clock_called;
}
static void startup(void) {
''' + prefix + r'''
}
int main(void) {
    for (mode = 0; mode <= 5; ++mode) {
        primask = 1; enabled = clock_called = hal_called = nvic_enabled = 0;
        scb.VTOR = 0x08000000U; tim.CR1 = tim.DIER = 0;
        if (!setjmp(failure)) { startup(); assert(mode == 0); }
        else assert(mode != 0);
        assert(enabled == (mode == 0) && clock_called == (mode == 0));
        if (mode) assert(primask == 1);
    }
    return 0;
}
'''
        build = ROOT / "app/build/host-tests"
        build.mkdir(parents=True, exist_ok=True)
        cfile, exe = build / "startup_handoff_test.c", build / "startup_handoff_test.exe"
        cfile.write_text(source, encoding="utf-8", newline="\n")
        result = subprocess.run([shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra",
                                 "-Werror", str(cfile), "-o", str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
