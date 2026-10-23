"""Stage/verify the fault-stop candidate, then publish managed artifacts and immutable evidence."""
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from datetime import datetime
from pathlib import Path

root=Path.cwd()
version='patrol-chassis-0.2.77'
build=root/'app/build/FaultStopRelease'
old=root/'docs/operations/evidence/2026-10-21-steering-updated-163918'
boot=root/'platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin'
incomplete=[p for p in (root/'docs/operations/evidence').glob('*-write-ack-verify-*')
            if p.is_dir() and not (p/'index.json').exists()]
assert len(incomplete)<=1, 'Multiple incomplete archives require inspection'
evidence=incomplete[0] if incomplete else root/'docs/operations/evidence'/datetime.now().strftime('%Y-%m-%d-write-ack-verify-%H%M%S')
bundle=root/'docs/operations/evidence/WriteAckVerify_0_2_77_Flash.zip'
if (evidence/'index.json').exists() or bundle.exists():raise RuntimeError('Do not overwrite evidence')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args):
    result=subprocess.run([str(a) for a in args],cwd=root,capture_output=True,text=True)
    if result.returncode:
        print(result.stdout+result.stderr,file=sys.stderr)
        result.check_returncode()
    return result
def write_json(p,data):p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

app=(build/'Patrol_Vehicle_Chassis_App.bin').read_bytes()
assert b'PATROL_VEHICLE_CHASSIS_FW_VERSION='+version.encode() in app
assert len(app)<=0x36000
assert re.search(r'#define TRANSACTION_MS\s+200U', (root/'app/Core/User/motion/src/bench_steering.c').read_text())
assert sha(boot)=='981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63'
header=(root/'app/Core/User/motion/include/bench_steering.h').read_text(encoding='utf-8')
assert '#define BENCH_WRITE_ACK_WAIT_MS 40U' in header and '#define BENCH_WRITE_VERIFY_MS 80U' in header and '#define BENCH_VERIFY_READ_MS 40U' in header
dist_before=sha(root/'dist/release.manifest.json')
git_head=run(['git','rev-parse','HEAD']).stdout.strip()
platform_head=run(['git','-C','platform','rev-parse','HEAD']).stdout.strip()
source_files=sorted({p for folder in ['app/Core','platform_config','shared/include'] for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.c','.h')})
source_files += [root/'app/CMakeLists.txt',root/'app/Core/User/modules.cmake',root/'app/STM32F105XX_FLASH.ld',root/'VERSION']
source_hashes={str(p.relative_to(root)).replace('\\','/'):sha(p) for p in source_files}
validation=dict(arm_release_build=True,stack_checks=True,flash_layout=True,frozen_boot=True,
    standalone_product_c_tests=True,fault_stop_production_regression=True,normal_steering_read_199_200_ms_boundary=True,write_ack_production_scenarios=20,diagnostic_retry_condition_regression=True,product_python_passed=44,
    product_python_total=45,known_existing_failure='test_identity_and_safe_defaults: existing hot-reset recovery design; unchanged assertion',
    formal_publication=False,formal_blocker='Existing Python failure and clean-worktree publication gate',
    hardware_motion_verified=False,board_flashed=False)

python_log=(root/'out/logs/write-verify-python.log').read_text(encoding='utf-8')
assert 'Ran 45 tests' in python_log and 'FAILED (failures=1)' in python_log
assert python_log.count('\nFAIL: ')==1 and 'FAIL: test_identity_and_safe_defaults' in python_log
assert 'PASS single diagnostic read loss' in python_log and 'PASS new target cannot cancel proof' in python_log
assert 'PASS verified zero does not bypass three fresh stopped feedback pairs' in python_log
assert not (root/'out/logs/write-verify-whitespace.log').read_bytes()
for log in ('bench-stack','can-stack','service-stack','layout','frozen-boot','product-c'):
    value=(root/('out/logs/write-verify-'+log+'.log')).read_text(encoding='utf-8-sig')
    assert 'Traceback' not in value and 'Assertion failed' not in value,log
    assert ('PASS' in value or 'verified' in value),log
baseline=json.loads((root/'out/logs/write-verify-baseline-check.json').read_text(encoding='utf-8'))
assert baseline['assertion_changed'] is False and baseline['baseline_matches']==baseline['current_matches']==2

with tempfile.TemporaryDirectory(prefix='patrol-fault-stop-stage-') as temporary:
    stage=Path(temporary);candidate=stage/'candidate';debug=stage/'debug';package=stage/'package'
    for folder in (candidate,debug,package):folder.mkdir()
    for folder in (candidate,debug,package):(folder/'Patrol_Vehicle_Chassis_App.bin').write_bytes(app)
    full=boot.read_bytes().ljust(0x8000,b'\xff')+app
    for folder in (debug,package):
        shutil.copyfile(boot,folder/'ABox_Boot.bin')
        (folder/'Patrol_Vehicle_Chassis_Full.bin').write_bytes(full)
    manifest=dict(product='patrol_vehicle_chassis',version=version,candidate=True,startup_permit=False,
        actuation_enabled=True,steering_executor_compiled=True,drive_executor_compiled=True,active_car=0,
        commit=git_head,dirty=True,source_matches_commit=False,platform_commit=platform_head,
        app_start='0x08008000',app_end='0x0803DFFF',config_recovery_page='0x0803E000',config_page='0x0803F800',
        size=len(app),sha256=hashlib.sha256(app).hexdigest(),source_sha256=source_hashes,
        drive_max_rpm=1000,steering_profile_rpm=dict(maximum=300,medium=180,creep=90),
        direction_timeout_ms=6000,drive_stop_timeout_ms=6000,fault_stop_latch_timeout_ms=600,
        fault_stop_transaction_ms=40,steering_transaction_timeout_ms=200,motion_write_ack_wait_ms=40,motion_write_verify_ms=80,verify_upload_timeout_ms=40,diagnostic_read_timeout_ms=60,diagnostic_retry_count=1,diagnostic_retry_feedback_max_age_ms=170,physical_a_enabled=True,physical_b_brake_enabled=False,
        board_flashed=False,validation=validation)
    write_json(candidate/'candidate.manifest.json',manifest)
    shutil.copyfile(candidate/'candidate.manifest.json',package/'candidate.manifest.json')
    sp,reset=struct.unpack_from('<II',app)
    debug_manifest=dict(kind='online-debug',configuration='Release',product='patrol_vehicle_chassis',candidate=True,
        app_version=version,boot_version='abox-boot-2.3.0',app_offset='0x00008000',initial_sp=hex(sp),reset_vector=hex(reset),
        full_length=len(full),full_sha256=hashlib.sha256(full).hexdigest(),app_sha256=hashlib.sha256(app).hexdigest(),
        boot_sha256=sha(boot),board_flashed=False,includes_boot_state=False,includes_device_config=False,validation=validation)
    write_json(debug/'debug.manifest.json',debug_manifest)
    shutil.copyfile(debug/'debug.manifest.json',package/'debug.manifest.json')
    fmt='<IHHIIBBBBII32sII32sIBB2sI'
    assert struct.calcsize(fmt)==112
    states=[]
    for name,seq,address in [('BootState_A.bin',1,'0x0803E800'),('BootState_B.bin',2,'0x0803F000')]:
        original=(old/name).read_bytes();fields=list(struct.unpack(fmt,original[:112]))
        assert fields[:3]==[0x42535632,3,112] and fields[4]==0 and fields[5:7]==[255,255]
        assert fields[12:15]==[0,0,b'\x00'*32]
        assert zlib.crc32(original[:108])&0xffffffff==fields[-1]
        fields[3]=seq;fields[9]=len(app);fields[10]=zlib.crc32(app)&0xffffffff;fields[11]=version.encode().ljust(32,b'\x00')
        record=struct.pack(fmt,*fields);record=record[:108]+struct.pack('<I',zlib.crc32(record[:108])&0xffffffff)
        (package/name).write_bytes(record+b'\xff'*(2048-len(record)))
        states.append(dict(file=name,address=address,sequence=seq,state='NORMAL',stable_ufs_metadata='explicitly unseeded',
                           sha256=sha(package/name),bytes=2048,app_size=len(app),app_crc32=f'{zlib.crc32(app)&0xffffffff:08X}'))
    verifier=(old/'verify-state-steering-updated.c').read_text(encoding='utf-8').replace('patrol-chassis-0.2.74',version).replace('app_size==151016',f'app_size=={len(app)}')
    (stage/'verify-fault-stop-state.c').write_text(verifier,encoding='utf-8')
    run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'platform/components/boot_v2/include'),
         '-I'+str(root/'platform/components/core/include'),
         stage/'verify-fault-stop-state.c',root/'platform/components/boot_v2/src/abox_boot_v2_state.c',
         root/'platform/components/boot_v2/src/abox_boot_v2_boot.c','-o',stage/'verify-state.exe'])
    checked=run([stage/'verify-state.exe',package/'BootState_A.bin',package/'BootState_B.bin',package/'Patrol_Vehicle_Chassis_App.bin'])
    (root/'out/logs/write-verify-boot-state.log').write_text(checked.stdout,encoding='utf-8')
    addresses='''patrol_vehicle_chassis / patrol-chassis-0.2.77 / CANDIDATE (not formal dist)
本包包含诊断读取有限重试、活动运动写入失 ACK 后只读核验，以及故障双节点撤使能。尚未烧录、尚未完成实车验收。

完整安装方式（写完三个文件再复位，不与旧版状态文件混用）：
Patrol_Vehicle_Chassis_Full.bin -> 0x08000000
BootState_A.bin -> 0x0803E800
BootState_B.bin -> 0x0803F000

已有匹配冻结 Boot 时，可用 App.bin -> 0x08008000 替代 Full.bin；仍需本包配套 State A/B。
保留 0x0803E000 配置恢复页和 0x0803F800 Config V5，不整片擦除。本包不含逐车配置。
State A/B 为离线生成的 NORMAL 配套记录，明确不宣称已有有效 stable UFS 元数据。

核验失败、反馈过期或其他需锁存的故障发生时禁止新运动，先撤使能/行走零速并确认状态，再正式锁存。
600 ms 未确认或活动 Bus-Off 先锁存、尝试关闭 OUT1；锁存后仍只重试安全撤使能。
手动接电绕过 OUT1 的车辆不能用软件输出关闭代替物理急停或真正断电。
正常行走上限 1000 rpm、转向 300/180/90 rpm、停车/换向 6 s、A 使能和 B 无功能保留。
活动运动写入等待 ACK 40 ms 后转只读核验，最长 80 ms，每次核验读取不超过 40 ms。
诊断读取单次 60 ms、至多重读一次，仅门禁通过且关键反馈年龄不超过 170 ms 时重试。
核验中暂停两轴新运动，成功后只执行最新目标；不盲目重发定位触发，不按遥控帧自动清锁。
一般转向事务 200 ms；独立故障停机事务 40 ms、锁存期限 600 ms 不变。

验证：ARM Release、RAM/栈、布局、冻结 Boot、产品 C 和新增故障回归通过。
产品 Python 44/45；既有热复位设计静态检查仍失败，正式发布也要求干净工作区。
dist 未替换，本包不代表已发布或通过现场验收。
'''
    (package/'FLASH_ADDRESSES.txt').write_text(addresses,encoding='utf-8-sig')
    for directory,destination in [(candidate,'out/candidates/bench-drive'),(debug,'out/debug/online')]:
        run([sys.executable,'tools/artifact_store.py','--root',root,'--stage',directory,'--destination',destination,'--validate-only'])
    # Both stages, source hashes, archive inputs and actual Boot parser are verified before publication.
    for name,digest in source_hashes.items():assert sha(root/name)==digest,'source changed before publication: '+name
    for name in ['write-verify-arm-configure.log', 'write-verify-arm-build.log', 'write-verify-bench-stack.log', 'write-verify-can-stack.log', 'write-verify-service-stack.log', 'write-verify-frozen-boot.log', 'write-verify-layout.log', 'write-verify-product-c.log', 'write-verify-python.log', 'write-verify-baseline-check.json', 'write-verify-formal-gate.log', 'write-verify-whitespace.log', 'write-verify-boot-state.log', 'package-write-verify.py']:
        assert (root/'out/logs'/name).is_file(),name
    for directory,destination in [(candidate,'out/candidates/bench-drive'),(debug,'out/debug/online')]:
        run([sys.executable,'tools/artifact_store.py','--root',root,'--stage',directory,'--destination',destination])
    evidence.mkdir(exist_ok=True)
    entries=[]
    def archive(source,relative,origin=None):
        target=evidence/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():
            assert sha(source)==sha(target),'existing partial archive differs: '+str(target)
        else:shutil.copyfile(source,target)
        assert sha(source)==sha(target)
        entries.append(dict(original_path=str(source.resolve()) if origin is None else origin,
                            archive_path=str(target.relative_to(root)),bytes=target.stat().st_size,sha256=sha(target)))
    for source in sorted(package.iterdir()):archive(source,source.name)
    archive(stage/'verify-fault-stop-state.c','verify-fault-stop-state.c','derived host verifier from immutable 0.2.74 evidence; size/version updated only')
    archive(build/'Patrol_Vehicle_Chassis_App.elf','Patrol_Vehicle_Chassis_App.elf')
    archive(build/'Patrol_Vehicle_Chassis_App.map','Patrol_Vehicle_Chassis_App.map')
    for name in ['write-verify-arm-configure.log', 'write-verify-arm-build.log', 'write-verify-bench-stack.log', 'write-verify-can-stack.log', 'write-verify-service-stack.log', 'write-verify-frozen-boot.log', 'write-verify-layout.log', 'write-verify-product-c.log', 'write-verify-python.log', 'write-verify-baseline-check.json', 'write-verify-formal-gate.log', 'write-verify-whitespace.log', 'write-verify-boot-state.log', 'package-write-verify.py']:
        archive(root/'out/logs'/name,Path('validation')/name)
    for name in ['app/Core/User/motion/include/patrol_fault_stop.h','app/Core/User/motion/src/patrol_fault_stop.c',
                 'app/Core/User/motion/src/patrol_runtime.c','app/Core/User/motion/src/chassis_control.c',
                 'app/Core/User/motion/include/chassis_control.h','app/Core/User/cloud/src/remote_service.c',
                 'tests/fault_stop_runtime_cases.c','tests/test_fault_stop_runtime.py','tests/bench_steering_test.c','app/Core/User/motion/src/bench_steering.c','tools/verify_can2_stack.py',
                 'tools/service_stack_budget.json',
                 'app/Core/User/motion/include/bench_steering.h','app/Core/User/motion/include/bench_drive.h',
                 'app/Core/User/motion/src/bench_drive.c','app/Core/User/motion/include/patrol_diagnostics.h',
                 'app/Core/User/motion/src/patrol_diagnostics.c','tests/chassis_safety_test.c',
                 'tests/write_verify_runtime_cases.c','tests/center_runtime_stubs.h','tests/drive_brake_runtime_cases.c']:
        archive(root/name,Path('source')/name)
    patch=run(['git','diff','--binary']).stdout
    (evidence/'source-changes.patch').write_text(patch,encoding='utf-8')
    entries.append(dict(original_path='git diff --binary; untracked new source files also copied under source/',archive_path=str((evidence/'source-changes.patch').relative_to(root)),bytes=(evidence/'source-changes.patch').stat().st_size,sha256=sha(evidence/'source-changes.patch')))
    with zipfile.ZipFile(stage/'flash.zip','x',zipfile.ZIP_DEFLATED) as zipped:
        for p in sorted(package.iterdir()):zipped.write(p,p.name)
    shutil.copyfile(stage/'flash.zip',bundle)
    assert sha(stage/'flash.zip')==sha(bundle)
    entries.append(dict(original_path='verified staged package',archive_path=str(bundle.relative_to(root)),bytes=bundle.stat().st_size,sha256=sha(bundle)))
    assert sha(root/'dist/release.manifest.json')==dist_before
    write_json(evidence/'index.json',dict(product='patrol_vehicle_chassis',version=version,app_sha256=manifest['sha256'],app_size=len(app),
        board_flashed=False,field_motion_tested=False,formal_release=False,dist_manifest_sha256_unchanged=dist_before,
        boot_state_policy='offline NORMAL A/B; stable UFS explicitly unseeded; not a readback of the current board',states=states,validation=validation,files=entries))
    for entry in entries:assert sha(root/entry['archive_path'])==entry['sha256']
    for name,digest in source_hashes.items():assert sha(root/name)==digest,'source changed during packaging: '+name
write_json(root/'out/logs/write-verify-package-latest.json',dict(evidence=str(evidence.resolve()),bundle=str(bundle.resolve()),version=version,app_sha256=manifest['sha256'],app_size=len(app)))
print(json.dumps(dict(evidence=str(evidence.resolve()),bundle=str(bundle.resolve()),app_sha256=manifest['sha256'],app_size=len(app),states_verified=True,dist_changed=False),ensure_ascii=False,indent=2))
