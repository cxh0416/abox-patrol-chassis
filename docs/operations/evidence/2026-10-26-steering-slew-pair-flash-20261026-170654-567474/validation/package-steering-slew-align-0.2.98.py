"""Build the verified 0.2.98 steering slew/time-alignment flash evidence."""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import uuid
import zlib
from datetime import datetime, timedelta, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[2]
version = "patrol-chassis-0.2.98"
build = root / "out/logs/build-steering-slew-align-0.2.98"
boot_path = root / "platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin"
stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d-%H%M%S-%f")
destination = root / f"docs/operations/evidence/2026-10-26-steering-slew-pair-flash-{stamp}"
stage = root / f"out/logs/steering-slew-package-stage-{uuid.uuid4().hex}"
candidate_stage = stage / "candidate"
validation_dir = stage / "validation"

if destination.exists():
    raise SystemExit(f"Evidence destination already exists: {destination}")
stage.mkdir(parents=True)
candidate_stage.mkdir()
validation_dir.mkdir()


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_text(path: Path, text: str) -> None:
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def write_json(path: Path, value: object) -> None:
    write_text(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def run(
    args: list[object], log_name: str | None = None, allow_failure: bool = False
) -> str:
    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    result = subprocess.run(
        [str(item) for item in args],
        cwd=root,
        env=env,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    output = result.stdout + result.stderr
    if log_name:
        write_text(validation_dir / log_name, output)
    if result.returncode and not allow_failure:
        raise RuntimeError(f"{log_name or args[0]} failed:\n{output[-4000:]}")
    return output


run(["cmake", "--build", build], "arm-release-build.log")
run([sys.executable, "-X", "utf8", "tools/verify_bench_stack.py", build], "bench-stack.log")
run([sys.executable, "-X", "utf8", "tools/verify_can2_stack.py", build], "can2-stack.log")
python_log = run(
    [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"],
    "product-python.log",
    allow_failure=True,
)
diff_check = run(["git", "diff", "--check"], "git-diff-check.log")
assert diff_check == "", diff_check
assert "Ran 57 tests" in python_log
assert "FAILED (failures=8)" in python_log
assert python_log.count("\nFAIL: ") == 8
assert "FAIL: test_identity_and_safe_defaults" in python_log
assert "FAIL: test_document_links_and_archived_evidence_references" in python_log

app_path = build / "Patrol_Vehicle_Chassis_App.bin"
elf_path = build / "Patrol_Vehicle_Chassis_App.elf"
map_path = build / "Patrol_Vehicle_Chassis_App.map"
app = app_path.read_bytes()
boot = boot_path.read_bytes()
assert len(boot) == 0x8000
assert 8 <= len(app) <= 0x36000
assert ("PATROL_VEHICLE_CHASSIS_FW_VERSION=" + version).encode("ascii") in app
assert sha(boot_path) == "981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63"

cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
for key, value in {
    "CMAKE_BUILD_TYPE": "Release",
    "PATROL_ACTIVE_CAR": "0",
    "PATROL_CHASSIS_APP_VERSION_OVERRIDE": version,
    "PATROL_CHASSIS_ACTUATION_ENABLED": "ON",
    "PATROL_STEERING_EXECUTOR_ENABLED": "ON",
    "PATROL_DRIVE_EXECUTOR_ENABLED": "ON",
}.items():
    assert re.search(rf"^{re.escape(key)}:[^=]+={re.escape(value)}$", cache, re.M), (key, value)

initial_sp, reset_vector = struct.unpack_from("<II", app)
assert 0x20000000 <= initial_sp <= 0x20010000 and initial_sp % 8 == 0
assert reset_vector & 1
assert 0x08008000 <= (reset_vector & ~1) < 0x08008000 + len(app)

full = boot + app
assert full[: len(boot)] == boot
assert full[0x8000:] == app

state_a = stage / "BootState_A.bin"
state_b = stage / "BootState_B.bin"
run(
    [
        sys.executable,
        "-X",
        "utf8",
        "tools/make_bootstate_image.py",
        "--app",
        app_path,
        "--version",
        version,
        "--state-a",
        state_a,
        "--state-b",
        state_b,
    ],
    "bootstate-create.log",
)

state_format = "<IHHIIBBBBII32sII32sIBB2sI"
assert struct.calcsize(state_format) == 112
state_a_bytes = state_a.read_bytes()
state_b_bytes = state_b.read_bytes()
fields = struct.unpack(state_format, state_a_bytes[:112])
assert fields[:5] == (0x42535632, 3, 112, 1, 4)
assert fields[9] == len(app)
assert fields[10] == (zlib.crc32(app) & 0xFFFFFFFF)
assert fields[11].split(b"\0", 1)[0].decode("ascii") == version
assert fields[-1] == (zlib.crc32(state_a_bytes[:108]) & 0xFFFFFFFF)
assert state_b_bytes == b"\xff" * 2048

verifier_source = r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "abox_boot_v2.h"
#include "abox_boot_v2_boot.h"

static uint8_t pages[4096], app[0x36000];
static uint32_t app_size;
static unsigned jumps, installs, writes;

void ABoxBootV2_ImageBindLayoutInternal(const ABoxBootV2Layout *layout)
{
    (void)layout;
}

int ABox_PortFlashRead(uint32_t address, uint8_t *data, uint32_t length)
{
    if (address < 0x0803e800U || address + length > 0x0803f800U) return 0;
    memcpy(data, pages + address - 0x0803e800U, length);
    return 1;
}

int ABox_PortFlashBegin(void) { writes++; return 0; }
int ABox_PortFlashWrite(uint32_t a, const uint8_t *d, uint32_t n)
{
    (void)a; (void)d; (void)n; writes++; return 0;
}
int ABox_PortFlashErasePage(uint32_t a) { (void)a; writes++; return 0; }
void ABox_PortFlashEnd(void) {}
int ABoxBootV2_FaultValid(void) { return 0; }
void ABoxBootV2_FaultClear(void) { assert(!"CONFIRMED boot must not clear a fault"); }

static uint8_t valid_image(void *context, uint32_t size, uint32_t crc)
{
    uint32_t sp, reset;
    (void)context;
    memcpy(&sp, app, 4);
    memcpy(&reset, app + 4, 4);
    return size == app_size && crc == ABoxBootV2_Crc32(app, app_size) &&
        sp >= 0x20000000U && sp <= 0x20010000U && (reset & 1U) &&
        (reset & ~1U) >= 0x08008000U && (reset & ~1U) < 0x08008000U + app_size;
}

static ABoxBootV2InstallResult install(void *context, uint8_t slot,
                                        uint32_t size, uint32_t crc)
{
    (void)context; (void)slot; (void)size; (void)crc;
    installs++;
    return ABOX_BOOT_V2_INSTALL_FAILED;
}
static void jump(void *context) { (void)context; jumps++; }
static uint8_t reason(void *context) { (void)context; return 0; }

static void load(const char *name, uint8_t *data, size_t size)
{
    FILE *file = fopen(name, "rb");
    assert(file);
    assert(fread(data, 1U, size, file) == size);
    assert(fgetc(file) == EOF);
    fclose(file);
}

int main(int argc, char **argv)
{
    ABoxBootV2Layout layout = {
        0x0803e800U, 0x0803f000U, 0x800U, 0x08008000U,
        0x0803dfffU, 0x20000000U, 0x20010000U
    };
    ABoxBootV2BootPort port = {0};
    ABoxBootV2Record record;
    ABoxBootV2Diagnostics diagnostics;
    uint8_t original_pages[4096];
    FILE *file;

    assert(argc == 4);
    load(argv[1], pages, 2048U);
    load(argv[2], pages + 2048U, 2048U);
    file = fopen(argv[3], "rb");
    assert(file);
    app_size = (uint32_t)fread(app, 1U, sizeof(app), file);
    assert(feof(file));
    fclose(file);
    assert(app_size == __APP_SIZE__);

    port.flash_image_valid = valid_image;
    port.install_slot = install;
    port.jump_to_app = jump;
    port.reset_reason = reason;
    assert(ABoxBootV2_StateBindLayout(&layout));
    assert(ABoxBootV2Boot_Init(&port));
    assert(ABoxBootV2_StateLoad(&record));
    assert(ABoxBootV2_StateDiagnostics(&diagnostics));
    assert(record.sequence == 1U);
    assert(record.state == ABOX_BOOT_V2_CONFIRMED);
    assert(record.image_size == app_size);
    assert(record.image_crc32 == ABoxBootV2_Crc32(app, app_size));
    assert(!strcmp(record.image_version, "__VERSION__"));
    assert(diagnostics.page_a_valid && !diagnostics.page_b_valid);
    assert(diagnostics.selected_page == 0U && diagnostics.selected_sequence == 1U);

    ABoxBootV2Boot_Task();
    assert(jumps == 1U && installs == 0U && writes == 0U);

    memcpy(original_pages, pages, sizeof(pages));
    pages[108] ^= 1U;
    jumps = installs = writes = 0U;
    ABoxBootV2Boot_Task();
    assert(jumps == 0U && installs == 0U && writes == 0U);

    memcpy(pages, original_pages, sizeof(pages));
    app[32] ^= 1U;
    jumps = installs = writes = 0U;
    ABoxBootV2Boot_Task();
    assert(jumps == 0U && installs == 0U && writes == 0U);
    puts("PASS: real Boot State V3 parser/CRC, State A binding, State B erase, matching-image jump and mismatch rejection; no install/write callbacks.");
    return 0;
}
'''
verifier_source = verifier_source.replace("__APP_SIZE__", str(len(app)))
verifier_source = verifier_source.replace("__VERSION__", version)
verifier_path = stage / "verify-state-0.2.98.c"
write_text(verifier_path, verifier_source)
verifier_exe = stage / "verify-state-0.2.98.exe"
run(
    [
        "gcc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I" + str(root / "platform/components/boot_v2/include"),
        "-I" + str(root / "platform/components/core/include"),
        verifier_path,
        root / "platform/components/boot_v2/src/abox_boot_v2_state.c",
        root / "platform/components/boot_v2/src/abox_boot_v2_boot.c",
        "-o",
        verifier_exe,
    ],
    "bootstate-verifier-build.log",
)
state_check = run(
    [verifier_exe, state_a, state_b, app_path],
    "bootstate-verifier.log",
)
assert "PASS: real Boot State V3 parser/CRC" in state_check

for source in (app_path, elf_path, map_path):
    shutil.copyfile(source, stage / source.name)
shutil.copyfile(boot_path, stage / "ABox_Boot.bin")
(stage / "Patrol_Vehicle_Chassis_Full.bin").write_bytes(full)
shutil.copyfile(verifier_path, validation_dir / verifier_path.name)

source_files = [
    "app/Core/User/cloud/include/remote_service.h",
    "app/Core/User/cloud/src/remote_service.c",
    "app/Core/User/motion/include/chassis_control.h",
    "app/Core/User/motion/src/chassis_control.c",
    "app/Core/User/motion/src/patrol_phy_remote.c",
    "app/Core/User/motion/src/patrol_runtime.c",
    "app/Core/User/system/src/patrol_rtos.c",
    "tests/can_remote_test.c",
    "tests/chassis_safety_test.c",
    "tests/drive_brake_runtime_cases.c",
    "tests/fault_stop_runtime_cases.c",
    "tests/test_can_remote.py",
]
for relative in source_files:
    target = stage / "source" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / relative, target)
patch = subprocess.check_output(["git", "diff", "--binary"], cwd=root)
(stage / "source-changes.patch").write_bytes(patch)
shutil.copyfile(Path(__file__), validation_dir / Path(__file__).name)

app_sha = sha(app_path)
full_sha = sha(stage / "Patrol_Vehicle_Chassis_Full.bin")
state_a_sha = sha(state_a)
state_b_sha = sha(state_b)
app_crc = f"{zlib.crc32(app) & 0xFFFFFFFF:08X}"
full_crc = f"{zlib.crc32(full) & 0xFFFFFFFF:08X}"
state_a_record_crc = f"{fields[-1]:08X}"
head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
platform_head = subprocess.check_output(
    ["git", "-C", "platform", "rev-parse", "HEAD"], cwd=root, text=True
).strip()

manifest = {
    "product": "patrol_vehicle_chassis",
    "version": version,
    "candidate": True,
    "configuration": "Release",
    "active_car": 0,
    "actuation_enabled": True,
    "steering_executor_compiled": True,
    "drive_executor_compiled": True,
    "startup_motion_permit": False,
    "app": {
        "file": app_path.name,
        "address": "0x08008000",
        "size": len(app),
        "initial_sp": hex(initial_sp),
        "reset_vector": hex(reset_vector),
        "crc32": app_crc,
        "sha256": app_sha,
    },
    "boot": {
        "file": "ABox_Boot.bin",
        "address": "0x08000000",
        "size": len(boot),
        "sha256": sha(boot_path),
    },
    "full": {
        "file": "Patrol_Vehicle_Chassis_Full.bin",
        "address": "0x08000000",
        "size": len(full),
        "crc32": full_crc,
        "sha256": full_sha,
    },
    "states": [
        {
            "file": "BootState_A.bin",
            "address": "0x0803E800",
            "size": 2048,
            "record_crc32": state_a_record_crc,
            "sha256": state_a_sha,
        },
        {
            "file": "BootState_B.bin",
            "address": "0x0803F000",
            "size": 2048,
            "erased": True,
            "sha256": state_b_sha,
        },
    ],
    "verification": {
        "arm_release_build": True,
        "can2_stack": True,
        "bench_stack": True,
        "full_boot_prefix": True,
        "full_app_suffix": True,
        "version_marker": True,
        "vectors_valid": True,
        "state_a_bound": True,
        "state_b_erased": True,
        "product_python_passed": 49,
        "product_python_total": 57,
        "known_failures": 8,
        "known_failure_summary": "1 existing static identity rule and 7 existing historical document links",
    },
    "source_commit": head,
    "platform_commit": platform_head,
    "product_dirty": True,
    "board_flashed": False,
    "field_verified": False,
}
write_json(stage / "flash.manifest.json", manifest)

readme = f"""# {version} 转向斜率、跨中位过渡与反馈时间对齐烧录包

本包针对遥控快速大幅换向时，独立编码器与驱动器位置反馈的时间偏差叠加机械动态变化，瞬时超过 `2°` 而锁存 `ANGLE_MISMATCH` 的问题。

## 修改

- 实体遥控转向目标限制为 `12000 mdeg/s`，约 `12°/s`。
- 目标跨过中位且变化达到 `5000 mdeg` 时，先回中；两路角度均在 `±1000 mdeg` 且连续 3 个有效帧稳定后，再转向新目标。
- 只有驱动器位置与独立编码器样本时间差不超过 `30 ms` 时，才比较两路角度差值。
- 两路角度未形成有效配对时返回不锁存的 `FEEDBACK_STALE`，拒绝新运动并禁止自动回中；不得用异时样本误报角度故障。
- 时间对齐后的两路差值超过 `2°` 仍立即锁存，并保留真实首因 `ANGLE_MISMATCH`，不再被泛化的显式停车覆盖。
- 独立绝对编码器运行期轮询保持 `20 ms`；未恢复 Node 1 高频 `0x6041/0x6064` SDO 轮询。

## 烧录

- `Patrol_Vehicle_Chassis_Full.bin` → `0x08000000`
- `BootState_A.bin` → `0x0803E800`
- `BootState_B.bin` → `0x0803F000`

已有匹配冻结 Boot 时，可用 `Patrol_Vehicle_Chassis_App.bin` → `0x08008000` 替代 Full，但仍须写入本包配套 State A/B。

不要整片擦除；保留：

- `0x0803E000..0x0803E7FF` 配置恢复页
- `0x0803F800..0x0803FFFF` Config V5 与故障日志页

## 校验

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `Patrol_Vehicle_Chassis_Full.bin` | {len(full):,} B | `{full_sha.upper()}` |
| `Patrol_Vehicle_Chassis_App.bin` | {len(app):,} B | `{app_sha.upper()}` |
| `BootState_A.bin` | 2,048 B | `{state_a_sha.upper()}` |
| `BootState_B.bin` | 2,048 B | `{state_b_sha.upper()}` |

- App CRC32：`{app_crc}`
- Full CRC32：`{full_crc}`
- State A record CRC32：`{state_a_record_crc}`
- 初始 SP：`{hex(initial_sp)}`
- Reset 向量：`{hex(reset_vector)}`

## 验证边界

ARM Release、CAN2 栈、Bench 栈、`git diff --check`、Full 拼接、版本标记、App 向量及真实 Boot State V3 解析/CRC 检查通过。全量主机回归为 49/57；8 项失败与修改前相同，为 1 项既有源码静态规则和 7 项历史证据链接失效。

尚未烧录或实车验证。现场应依次检查缓慢全幅左右转向、快速 `+450 → -450` 换向、跨中位是否先回中、双转角是否保持低于 `2°`，以及故障台是否记录真实 `ANGLE_MISMATCH`。
"""
write_text(stage / "README.md", readme)

addresses = f"""patrol_vehicle_chassis / {version} / STEERING SLEW + PAIR ALIGN

推荐完整安装，写完三个文件后再复位：
1. Patrol_Vehicle_Chassis_Full.bin -> 0x08000000 ({len(full)} bytes)
2. BootState_A.bin -> 0x0803E800 (2048 bytes)
3. BootState_B.bin -> 0x0803F000 (2048 bytes)

Full 已包含 32 KiB 冻结 Boot 和 App，不要同时重复烧录独立 App/Boot。
已有匹配冻结 Boot 时，可使用：
Patrol_Vehicle_Chassis_App.bin -> 0x08008000 ({len(app)} bytes)
但仍必须写入本包配套 BootState_A/B。

禁止整片擦除。保留：
- 0x0803E000..0x0803E7FF 配置恢复页
- 0x0803F800..0x0803FFFF Config V5 与故障日志页

转向目标斜率：12000 mdeg/s
大幅反向门限：5000 mdeg
中位稳定范围：+/-1000 mdeg，连续 3 帧
双路反馈配对窗口：30 ms
保留双转角硬门禁：2000 mdeg

App CRC32: {app_crc}
App SHA256: {app_sha.upper()}
Full SHA256: {full_sha.upper()}
BootState_A SHA256: {state_a_sha.upper()}
BootState_B SHA256: {state_b_sha.upper()}
"""
write_text(stage / "FLASH_ADDRESSES.txt", addresses)

candidate_manifest = {
    "product": "patrol_vehicle_chassis",
    "version": version,
    "candidate": True,
    "steering_executor_compiled": True,
    "drive_executor_compiled": True,
    "startup_permit": False,
    "actuation_enabled": True,
    "active_car": 0,
    "commit": head,
    "dirty": True,
    "app_start": "0x08008000",
    "app_end": "0x0803DFFF",
    "config_recovery_page": "0x0803E000",
    "config_page": "0x0803F800",
    "size": len(app),
    "sha256": app_sha.upper(),
}
shutil.copyfile(app_path, candidate_stage / app_path.name)
write_json(candidate_stage / "candidate.manifest.json", candidate_manifest)

hashed_files = []
for path in sorted(stage.rglob("*")):
    if path.is_file() and path.name != "sha256.json":
        hashed_files.append(
            {
                "file": path.relative_to(stage).as_posix(),
                "size": path.stat().st_size,
                "sha256": sha(path),
            }
        )
write_json(stage / "sha256.json", hashed_files)

artifact_entries = []
for path in sorted(stage.rglob("*")):
    if path.is_file() and path.name != "index.json":
        artifact_entries.append(
            {
                "archive_path": (
                    destination.relative_to(root) / path.relative_to(stage)
                ).as_posix(),
                "size": path.stat().st_size,
                "sha256": sha(path),
            }
        )
index = {
    "product": "patrol_vehicle_chassis",
    "version": version,
    "candidate": True,
    "app_size": len(app),
    "app_crc32": app_crc,
    "app_sha256": app_sha,
    "full_size": len(full),
    "full_crc32": full_crc,
    "full_sha256": full_sha,
    "states": manifest["states"],
    "validation": manifest["verification"],
    "board_flashed": False,
    "field_verified": False,
    "dist_updated": False,
    "files": artifact_entries,
}
write_json(stage / "index.json", index)

sys.path.insert(0, str(root / "tools"))
from artifact_store import publish  # noqa: E402

publish(root, candidate_stage, "out/candidates/bench-drive")
destination.parent.mkdir(parents=True, exist_ok=True)
os.rename(stage, destination)

result = {
    "evidence": str(destination),
    "version": version,
    "app_size": len(app),
    "app_crc32": app_crc,
    "app_sha256": app_sha,
    "full_size": len(full),
    "full_crc32": full_crc,
    "full_sha256": full_sha,
    "state_a_sha256": state_a_sha,
    "state_b_sha256": state_b_sha,
    "candidate_updated": True,
    "dist_updated": False,
}
write_json(root / "out/logs/steering-slew-align-0.2.98-latest.json", result)
print(json.dumps(result, ensure_ascii=False, indent=2))
