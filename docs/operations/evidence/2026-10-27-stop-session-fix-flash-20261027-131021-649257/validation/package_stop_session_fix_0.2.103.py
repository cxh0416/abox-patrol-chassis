"""Build and publish the three-file flash package for patrol-chassis-0.2.103."""
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
version = "patrol-chassis-0.2.103"
build = root / "app/build/BenchSteering"
app_path = build / "Patrol_Vehicle_Chassis_App.bin"
boot_path = root / "platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin"
verifier_template = (
    root
    / "docs/operations/evidence/2026-10-27-target-commit-flash-20261027-112403-819758"
    / "verify-state-0.2.102.c"
)
stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d-%H%M%S-%f")
destination = root / f"docs/operations/evidence/2026-10-27-stop-session-fix-flash-{stamp}"
stage = root / f"out/logs/stop-session-package-stage-{uuid.uuid4().hex}"
validation = stage / "validation"

if destination.exists():
    raise SystemExit(f"evidence destination already exists: {destination}")
stage.mkdir(parents=True)
validation.mkdir()


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


def write_json(path: Path, value: object) -> None:
    write_text(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def run(args: list[object], log_name: str, allow_failure: bool = False) -> str:
    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    result = subprocess.run(
        [str(value) for value in args],
        cwd=root,
        env=env,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    output = result.stdout + result.stderr
    write_text(validation / log_name, output)
    if result.returncode and not allow_failure:
        raise RuntimeError(f"{log_name} failed:\n{output[-6000:]}")
    return output


run(["cmake", "--build", build], "arm-release-build.log")
run([sys.executable, "-X", "utf8", "tools/verify_bench_stack.py", build], "bench-stack.log")
run([sys.executable, "-X", "utf8", "tools/verify_can2_stack.py", build], "can2-stack.log")
test_log = run(
    [
        sys.executable,
        "-m",
        "unittest",
        "discover",
        "-s",
        "tests",
        "-p",
        "test_*.py",
        "-v",
    ],
    "product-python.log",
    allow_failure=True,
)
diff_check = run(
    ["git", "-c", "core.whitespace=cr-at-eol", "diff", "--check"],
    "git-diff-check.log",
)

assert not diff_check.strip(), diff_check
assert "Ran 56 tests" in test_log
assert "FAILED (failures=8)" in test_log
assert test_log.count("\nFAIL: ") == 8
assert "FAIL: test_identity_and_safe_defaults" in test_log
assert "FAIL: test_document_links_and_archived_evidence_references" in test_log
assert "FAIL: test_production_stop_arbitration" not in test_log

app = app_path.read_bytes()
boot = boot_path.read_bytes()
assert len(boot) == 0x8000
assert 8 <= len(app) <= 0x36000
assert (version + "\0").encode("ascii") in app or version.encode("ascii") in app
assert sha(boot_path) == "981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63"

cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
for key, value in {
    "CMAKE_BUILD_TYPE": "Release",
    "PATROL_ACTIVE_CAR": "2",
    "PATROL_CHASSIS_APP_VERSION_OVERRIDE": version,
    "PATROL_CHASSIS_ACTUATION_ENABLED": "ON",
    "PATROL_STEERING_EXECUTOR_ENABLED": "ON",
    "PATROL_DRIVE_EXECUTOR_ENABLED": "True",
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

verifier_text = verifier_template.read_text(encoding="utf-8")
verifier_text = verifier_text.replace("assert(app_size == 156828);", f"assert(app_size == {len(app)});")
verifier_text = verifier_text.replace("patrol-chassis-0.2.102", version)
verifier_path = stage / "verify-state-0.2.103.c"
write_text(verifier_path, verifier_text)
verifier_exe = stage / "verify-state-0.2.103.exe"
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

(stage / "Patrol_Vehicle_Chassis_Full.bin").write_bytes(full)

source_files = [
    "VERSION",
    "app/Core/User/motion/private/motion_internal.h",
    "app/Core/User/motion/src/patrol_runtime.c",
    "app/Core/User/motion/src/bench_steering.c",
    "app/Core/User/motion/include/bench_steering.h",
    "tests/stop_scheduler_cases.c",
    "tests/bench_steering_test.c",
]
for relative in source_files:
    target = stage / "source" / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / relative, target)
(stage / "source-changes.patch").write_bytes(
    subprocess.check_output(["git", "diff", "--binary"], cwd=root)
)
(stage / "candidate").mkdir()
shutil.copyfile(app_path, stage / "candidate" / app_path.name)
shutil.copyfile(
    root / "out/candidates/bench-drive/candidate.manifest.json",
    stage / "candidate" / "candidate.manifest.json",
)
shutil.copyfile(verifier_path, validation / verifier_path.name)
shutil.copyfile(Path(__file__), validation / Path(__file__).name)

app_sha = sha(app_path)
full_path = stage / "Patrol_Vehicle_Chassis_Full.bin"
full_sha = sha(full_path)
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
    "active_car": 2,
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
        "sha256": app_sha.upper(),
    },
    "boot": {
        "address": "0x08000000",
        "size": len(boot),
        "sha256": sha(boot_path).upper(),
    },
    "full": {
        "file": full_path.name,
        "address": "0x08000000",
        "size": len(full),
        "crc32": full_crc,
        "sha256": full_sha.upper(),
    },
    "states": [
        {
            "file": state_a.name,
            "address": "0x0803E800",
            "size": 2048,
            "record_crc32": state_a_record_crc,
            "sha256": state_a_sha.upper(),
        },
        {
            "file": state_b.name,
            "address": "0x0803F000",
            "size": 2048,
            "erased": True,
            "sha256": state_b_sha.upper(),
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
        "product_python_passed": 48,
        "product_python_total": 56,
        "known_failures": 8,
        "known_failure_summary": "1 static identity rule and 7 historical document links",
    },
    "source_commit": head,
    "platform_commit": platform_head,
    "product_dirty": True,
    "board_flashed": False,
    "field_verified": False,
}
write_json(stage / "flash.manifest.json", manifest)

readme = f"""# {version} 显式停车与反馈过期误判修复烧录包

本包由当前工作树重新执行 ARM Release 构建并校验生成。

## 修改

- 显式停车释放会话后，仍由 `PREPARING/ACTIVE/STOPPING` 执行器继续完成有界零速和撤使能。
- 单次反馈过期只启动按轴锁存的受控停车，不再直接产生 `BenchDrive` fault 3。
- 停车责任按转向、行走两个轴分别保持，不会误停仅行走制动或自动回中的另一轴。
- 转向准备阶段可直接 shutdown，不因限位尚未对齐而误判故障。
- 节点离线、Bus-Off、驱动器故障、CAN 发送失败和停车序列超时仍进入故障锁存。

## 烧录

| 文件 | 地址 |
|---|---|
| `{full_path.name}` | `0x08000000` |
| `{state_a.name}` | `0x0803E800` |
| `{state_b.name}` | `0x0803F000` |

禁止整片擦除。保留 `0x0803E000..0x0803E7FF` 配置恢复页和 `0x0803F800..0x0803FFFF` Config V5/故障日志页。

## 文件

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `{full_path.name}` | {len(full):,} B | `{full_sha.upper()}` |
| `{state_a.name}` | 2,048 B | `{state_a_sha.upper()}` |
| `{state_b.name}` | 2,048 B | `{state_b_sha.upper()}` |

- Full CRC32：`{full_crc}`
- State A record CRC32：`{state_a_record_crc}`
- App CRC32：`{app_crc}`
- 初始 SP：`{hex(initial_sp)}`
- Reset 向量：`{hex(reset_vector)}`

## 验证边界

ARM Release、CAN2/Bench 栈、Full 的冻结 Boot 前缀和 App 后缀、版本标记、向量、真实 Boot State V3 解析/CRC、State A 绑定及 State B 擦除检查通过。

全量产品回归为 48/56；剩余 8 项为既有静态身份规则 1 项和历史文档链接 7 项，`test_production_stop_arbitration` 已通过。本包尚未烧录或实车验证。
"""
write_text(stage / "README.md", readme)

addresses = f"""patrol_vehicle_chassis / {version}

1. Patrol_Vehicle_Chassis_Full.bin -> 0x08000000 ({len(full)} bytes)
2. BootState_A.bin -> 0x0803E800 (2048 bytes)
3. BootState_B.bin -> 0x0803F000 (2048 bytes)

禁止整片擦除。
保留 0x0803E000..0x0803E7FF 配置恢复页。
保留 0x0803F800..0x0803FFFF Config V5 与故障日志页。

Full CRC32: {full_crc}
Full SHA256: {full_sha.upper()}
State A SHA256: {state_a_sha.upper()}
State B SHA256: {state_b_sha.upper()}
App SHA256: {app_sha.upper()}
"""
write_text(stage / "FLASH_ADDRESSES.txt", addresses)

bin_files = sorted(path.name for path in stage.glob("*.bin"))
assert bin_files == ["BootState_A.bin", "BootState_B.bin", "Patrol_Vehicle_Chassis_Full.bin"]

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
                "archive_path": (destination.relative_to(root) / path.relative_to(stage)).as_posix(),
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
    "app_sha256": app_sha.upper(),
    "full_size": len(full),
    "full_crc32": full_crc,
    "full_sha256": full_sha.upper(),
    "states": manifest["states"],
    "three_file_package": True,
    "files": artifact_entries,
    "board_flashed": False,
    "field_verified": False,
    "dist_updated": False,
}
write_json(stage / "index.json", index)

destination.parent.mkdir(parents=True, exist_ok=True)
os.rename(stage, destination)

result = {
    "evidence": str(destination),
    "files": [
        str(destination / "Patrol_Vehicle_Chassis_Full.bin"),
        str(destination / "BootState_A.bin"),
        str(destination / "BootState_B.bin"),
    ],
    "version": version,
    "app_size": len(app),
    "app_crc32": app_crc,
    "app_sha256": app_sha.upper(),
    "full_size": len(full),
    "full_crc32": full_crc,
    "full_sha256": full_sha.upper(),
    "state_a_sha256": state_a_sha.upper(),
    "state_b_sha256": state_b_sha.upper(),
    "dist_updated": False,
}
write_json(root / "out/logs/stop-session-fix-0.2.103-latest.json", result)
print(json.dumps(result, ensure_ascii=False, indent=2))
