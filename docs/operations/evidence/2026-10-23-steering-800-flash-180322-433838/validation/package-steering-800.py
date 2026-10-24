"""Validate and package the saved 800-rpm worktree without accessing hardware."""
from pathlib import Path
from datetime import datetime, timezone, timedelta
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

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from artifact_store import validate, publish
build = root / 'app/build/Steering800'
version = 'patrol-chassis-0.2.81'
prefix = 'steering-800-'
stage = root / 'out/logs' / (prefix + 'stage-' + uuid.uuid4().hex)
stage.mkdir()
package, candidate, debug = [stage / name for name in ('package', 'candidate', 'debug')]
for folder in (package, candidate, debug):
    folder.mkdir()
now = datetime.now(timezone(timedelta(hours=8)))
destination = root / 'docs/operations/evidence' / now.strftime('%Y-%m-%d-steering-800-flash-%H%M%S-%f')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write_json(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def run(args, log_name):
    result = subprocess.run([str(a) for a in args], cwd=root, capture_output=True, encoding='utf-8', errors='replace')
    (root / 'out/logs' / (prefix + log_name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(log_name + ': ' + result.stdout[-1000:] + result.stderr[-1500:])
    return result.stdout

dist_hashes = {p.name: sha(p) for p in (root / 'dist').iterdir() if p.is_file()}
source_names = subprocess.check_output(['git', 'ls-files', 'app', 'shared', 'tests', 'VERSION'], cwd=root, text=True).splitlines()
source_names = [p for p in source_names if Path(p).suffix in ('.c', '.h', '.ld', '.cmake', '.txt', '.py') or p == 'VERSION']
sources = {name: sha(root / name) for name in source_names}
header = (root / 'app/Core/User/motion/include/vehicle_steering_profile.h').read_text(encoding='utf-8')
for level in ('MAX', 'MID', 'CREEP'):
    assert re.search(rf'#define VEHICLE_STEERING_PROFILE_{level}_RPM 800U', header)
cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
for key, value in {'PATROL_ACTIVE_CAR': '0', 'PATROL_CHASSIS_ACTUATION_ENABLED': 'ON', 'PATROL_STEERING_EXECUTOR_ENABLED': 'ON', 'PATROL_DRIVE_EXECUTOR_ENABLED': 'ON', 'PATROL_CHASSIS_APP_VERSION_OVERRIDE': version}.items():
    assert re.search(rf'^{key}:[^=]+={re.escape(value)}$', cache, re.M)
for name, args in [
    ('bench-stack', [sys.executable, '-X', 'utf8', 'tools/verify_bench_stack.py', build]),
    ('can2-stack', [sys.executable, '-X', 'utf8', 'tools/verify_can2_stack.py', build]),
    ('service-stack', [sys.executable, '-X', 'utf8', 'platform/tools/verify_service_stack.py', '--build', build, '--budget', 'tools/service_stack_budget.json']),
    ('layout', ['pwsh', '-NoProfile', '-File', 'platform/tools/validate_flash_layout.ps1', '-BootLinker', 'platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld', '-AppLinker', 'app/STM32F105xx_FLASH.ld']),
    ('frozen-boot', [sys.executable, '-X', 'utf8', 'platform/targets/stm32f105_abox_boot/tools/verify_boot_artifact.py', 'platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin']),
]:
    run(args, name)

# Keep repository tests intact. This archived variant checks the current equal-speed
# setup, former thresholds and position handshake against the actual product sources.
test = (root / 'tests/bench_steering_test.c').read_text(encoding='utf-8')
test = test.replace('static void advance(', 'static unsigned speed_writes;\nstatic void advance(', 1)
test = test.replace('    if (b->pending) {', '''    if (f.id==0x601 && !b->read && (b->index==0x6080 || b->index==0x6081)) {
        assert(f.data[4]==0x20 && f.data[5]==0x03 && f.data[6]==0 && f.data[7]==0);
        speed_writes++;
    }
    if (b->pending) {''', 1)
start = test.index('    /* 0x6080 is a fixed')
end = test.index('    BenchSteering_Init(&b);\n    assert(BenchSteering_Start(&b,-83815,-72187', start)
test = test[:start] + '''    /* Current 800/800/800 setting: no profile change at old thresholds. */
    BenchSteering_Init(&b); now=0; speed_writes=0;
    assert(BenchSteering_Start(&b,-83815,-85123,-85123,30000,now));
    for(int i=0;i<20;i++) advance(&b,&now);
    assert(b.phase==BENCH_ACTIVE && b.applied_profile_rpm==800 && speed_writes>=2);
    const int angles[][2]={{30000,0},{29000,27000},{25000,24000},{2000,0},{0,28000},{-1000,-27000}};
    for(unsigned i=0;i<sizeof(angles)/sizeof(angles[0]);i++) {
        BenchSteering_Tick(&b,now,1,1,angles[i][0],angles[i][1]);
        assert(BenchSteering_Take(&b,now,&f) && b.index==0x607A);
        r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+1));
        assert(BenchSteering_Take(&b,now+2,&f) && b.index==0x6041 && b.read);
        r=reply(&b,0x27); assert(BenchSteering_OnFrame(&b,&r,now+3));
        assert(BenchSteering_Take(&b,now+4,&f) && b.index==0x6040 && f.data[4]==0x3F);
        r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+5));
        assert(BenchSteering_Take(&b,now+6,&f) && b.index==0x6040 && f.data[4]==0x2F);
        r=reply(&b,0); assert(BenchSteering_OnFrame(&b,&r,now+7));
        assert(b.phase==BENCH_ACTIVE && b.step==16 && b.applied_profile_rpm==800);
        now+=10;
    }

''' + test[end:]
fixture = root / 'out/logs/steering-800-bench-variant.c'
fixture.write_text(test, encoding='utf-8')
includes = ['-I' + str(root / 'platform/components/core/include'), '-I' + str(root / 'platform/components/mqtt/include')]
includes += ['-I' + str(root / 'app/Core/User' / p / 'include') for p in ('system', 'motion', 'cloud', 'mqtt', 'ota', 'board', 'common')]
run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', *includes, 'app/Core/User/motion/src/bench_steering.c', 'app/Core/User/motion/src/steering_angle.c', 'app/Core/User/motion/src/patrol_canopen.c', fixture, '-o', stage / 'bench.exe'], 'bench-variant-build')
run([stage / 'bench.exe'], 'bench-variant')

python_log = (root / 'out/logs/steering-800-python.log').read_text(encoding='utf-8-sig')
failures = re.findall(r'^FAIL: (.+)$', python_log, re.M)
assert len(failures) == 2 and 'Ran 45 tests' in python_log and 'FAILED (failures=2)' in python_log
validation = dict(arm_release_build=True, stack_checks=True, flash_layout=True, frozen_boot=True,
                  product_python_total=45, product_python_passed=43, failed_tests=failures,
                  current_800_rpm_bench_variant=True, board_flashed=False, hardware_motion_verified=False,
                  formal_publication=False, formal_blocker='Two repository test failures: legacy hot-reset static check and old 90-rpm profile expectation')
app_path = build / 'Patrol_Vehicle_Chassis_App.bin'
boot_path = root / 'platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin'
app, boot = app_path.read_bytes(), boot_path.read_bytes()
assert len(app) <= 0x36000 and len(boot) == 0x8000
assert sha(boot_path) == '981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63'
assert ('PATROL_VEHICLE_CHASSIS_FW_VERSION=' + version).encode() + b'\0' in app
full = boot + app
for folder in (package, debug):
    (folder / app_path.name).write_bytes(app)
    (folder / 'ABox_Boot.bin').write_bytes(boot)
    (folder / 'Patrol_Vehicle_Chassis_Full.bin').write_bytes(full)
(candidate / app_path.name).write_bytes(app)
manifest = dict(product='patrol_vehicle_chassis', version=version, candidate=True, startup_permit=False,
                actuation_enabled=True, steering_executor_compiled=True, drive_executor_compiled=True,
                active_car=0, commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                dirty=True, source_matches_commit=False, source_sha256=sources,
                platform_commit=subprocess.check_output(['git', '-C', 'platform', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                app_start='0x08008000', app_end='0x0803DFFF', config_recovery_page='0x0803E000',
                boot_state_pages=['0x0803E800', '0x0803F000'], config_page='0x0803F800',
                size=len(app), sha256=sha(app_path), steering_profile_rpm=800, drive_max_rpm=2000,
                build_directory='app/build/Steering800', configuration='Release', validation=validation,
                built_at=now.isoformat(), board_flashed=False)
write_json(candidate / 'candidate.manifest.json', manifest)
shutil.copyfile(candidate / 'candidate.manifest.json', package / 'candidate.manifest.json')
dm = dict(kind='online-debug', configuration='Release', product='patrol_vehicle_chassis', candidate=True,
          app_version=version, boot_version='abox-boot-2.3.0', app_offset='0x00008000',
          full_length=len(full), full_sha256=hashlib.sha256(full).hexdigest(), app_sha256=sha(app_path),
          boot_sha256=sha(boot_path), includes_boot_state=False, includes_device_config=False, validation=validation)
write_json(debug / 'debug.manifest.json', dm)
shutil.copyfile(debug / 'debug.manifest.json', package / 'debug.manifest.json')
states = []
for name, seq, address in [('BootState_A.bin', 1, '0x0803E800'), ('BootState_B.bin', 2, '0x0803F000')]:
    record = struct.pack('<IHHIIBBBBII32sII32sIBB2sI', 0x42535632, 3, 112, seq, 0, 255, 255, 0, 0,
                         len(app), zlib.crc32(app) & 0xffffffff, version.encode().ljust(32, b'\0'), 0, 0, b'\0'*32, 0, 0, 0, b'\0\0', 0)
    record = record[:108] + struct.pack('<I', zlib.crc32(record[:108]) & 0xffffffff)
    (package / name).write_bytes(record + b'\xff' * (2048-len(record)))
    states.append(dict(file=name, address=address, bytes=2048, sha256=sha(package/name), sequence=seq, state='NORMAL'))
verifier = (root / 'out/logs/verify-state-steering-updated.c').read_text(encoding='utf-8-sig')
verifier = verifier.replace('patrol-chassis-0.2.74', version).replace('app_size==151016', f'app_size=={len(app)}')
verifier_path = stage / 'verify-state.c'
verifier_path.write_text(verifier, encoding='utf-8')
run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'platform/components/boot_v2/include'), '-I'+str(root/'platform/components/core/include'), verifier_path, 'platform/components/boot_v2/src/abox_boot_v2_state.c', 'platform/components/boot_v2/src/abox_boot_v2_boot.c', '-o', stage/'verify-state.exe'], 'boot-state-build')
run([stage/'verify-state.exe', package/'BootState_A.bin', package/'BootState_B.bin', package/app_path.name], 'boot-state')
notes = f'''patrol_vehicle_chassis / {version} / 转向800 rpm，行走上限2000 rpm
本包为测试候选，未烧录、未进行现场运动验收。active_car=0，使用本板逐车配置，不内置标定。
保持车辆停稳，三项写入并校验完成后再复位：
Patrol_Vehicle_Chassis_Full.bin -> 0x08000000 ({len(full)} bytes; 包含Boot+App)
BootState_A.bin -> 0x0803E800 (2048 bytes)
BootState_B.bin -> 0x0803F000 (2048 bytes)
如果Boot已核验一致，可用App.bin -> 0x08008000替代Full；不要重复写Full和App。
不整片擦除，保留恢复页0x0803E000和配置页0x0803F800。
已整片擦除的板：以上三项仅恢复固件与启动状态，还需恢复本板本车有效Config V5到0x0803F800或重新配置及标定。
本包不含逐车配置，不可直接采用其他车辆配置。0x0803E000无需人为构造恢复记录。
State A/B为离线NORMAL记录；stable UFS未建立，不宣称旧备份槽有效。
ARM、栈、布局、冻结Boot、当前800 rpm转向执行器主机检查及真实Boot状态解析检查通过。
完整Python回归43/45：既有热复位静态检查、旧90 rpm断言失败，详见日志；正式dist不变。
App SHA256: {sha(app_path)}
'''
(package/'FLASH_ADDRESSES.txt').write_text(notes, encoding='utf-8')
for suffix in ('elf', 'map'):
    shutil.copyfile(build/f'Patrol_Vehicle_Chassis_App.{suffix}', package/f'Patrol_Vehicle_Chassis_App.{suffix}')
for path in sorted((root/'out/logs').glob(prefix+'*')):
    if path.is_file() and path.suffix in ('.log', '.c'):
        target=package/'validation'/path.name
        target.parent.mkdir(exist_ok=True)
        shutil.copyfile(path,target)
shutil.copyfile(Path(__file__), package/'validation/package-steering-800.py')
for name in ('app/Core/User/motion/include/vehicle_steering_profile.h', 'app/Core/User/motion/src/bench_steering.c'):
    target=package/'source'/name
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(root/name,target)
(package/'source-changes.patch').write_bytes(subprocess.check_output(['git','diff','--','app','shared','tests'],cwd=root))
validate(candidate,'candidate')
validate(debug,'debug')
assert all(sha(root/name)==digest for name,digest in sources.items())
entries=[dict(archive_path=(destination.relative_to(root)/p.relative_to(package)).as_posix(), bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(package.rglob('*')) if p.is_file()]
write_json(package/'index.json', dict(product='patrol_vehicle_chassis', version=version, validation=validation,
           app_sha256=sha(app_path), app_size=len(app), full_size=len(full), states=states,
           board_flashed=False, field_motion_tested=False, formal_release=False, files=entries))
for entry in entries:
    assert sha(package/Path(entry['archive_path']).relative_to(destination.relative_to(root)))==entry['sha256']
publish(root,candidate,'out/candidates/bench-drive')
publish(root,debug,'out/debug/online')
assert dist_hashes=={p.name:sha(p) for p in (root/'dist').iterdir() if p.is_file()}
os.rename(package,destination)
result=dict(evidence=str(destination),version=version,app_size=len(app),app_sha256=sha(app_path),full_size=len(full),board_flashed=False,dist_changed=False)
write_json(root/'out/logs/steering-800-flash-latest.json',result)
print(json.dumps(result,ensure_ascii=False,indent=2))
