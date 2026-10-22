"""Stage/verify the fault-stop candidate, then publish managed artifacts and immutable evidence."""
import hashlib
import json
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
version='patrol-chassis-0.2.75'
build=root/'app/build/FaultStopRelease'
old=root/'docs/operations/evidence/2026-10-21-steering-updated-163918'
boot=root/'platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin'
evidence=root/'docs/operations/evidence'/datetime.now().strftime('%Y-%m-%d-fault-stop-before-latch-%H%M%S')
bundle=root/'docs/operations/evidence/FaultStopBeforeLatch_0_2_75_Flash.zip'
if evidence.exists() or bundle.exists():raise RuntimeError('Do not overwrite evidence')

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
assert sha(boot)=='981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63'
dist_before=sha(root/'dist/release.manifest.json')
git_head=run(['git','rev-parse','HEAD']).stdout.strip()
platform_head=run(['git','-C','platform','rev-parse','HEAD']).stdout.strip()
source_files=sorted({p for folder in ['app/Core','platform_config','shared/include'] for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.c','.h')})
source_files += [root/'app/CMakeLists.txt',root/'app/Core/User/modules.cmake',root/'app/STM32F105XX_FLASH.ld',root/'VERSION']
source_hashes={str(p.relative_to(root)).replace('\\','/'):sha(p) for p in source_files}
validation=dict(arm_release_build=True,stack_checks=True,flash_layout=True,frozen_boot=True,
    standalone_product_c_tests=True,fault_stop_production_regression=True,product_python_passed=43,
    product_python_total=44,known_existing_failure='test_identity_and_safe_defaults: existing hot-reset recovery design; unchanged assertion',
    formal_publication=False,formal_blocker='Existing Python failure and clean-worktree publication gate',
    hardware_motion_verified=False,board_flashed=False)

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
        fault_stop_transaction_ms=40,physical_a_enabled=True,physical_b_brake_enabled=False,
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
    (root/'out/logs/fault-stop-boot-state.log').write_text(checked.stdout,encoding='utf-8')
    addresses='''patrol_vehicle_chassis / patrol-chassis-0.2.75 / CANDIDATE (not formal dist)
本包用于检查故障锁存前双节点撤使能。尚未烧录、尚未完成实车停车验收。

完整安装方式（写完三个文件再复位，不与旧版状态文件混用）：
Patrol_Vehicle_Chassis_Full.bin -> 0x08000000
BootState_A.bin -> 0x0803E800
BootState_B.bin -> 0x0803F000

已有匹配冻结 Boot 时，可用 App.bin -> 0x08008000 替代 Full.bin；仍需本包配套 State A/B。
保留 0x0803E000 配置恢复页和 0x0803F800 Config V5，不整片擦除。本包不含逐车配置。
State A/B 为离线生成的 NORMAL 配套记录，明确不宣称已有有效 stable UFS 元数据。

故障发生即禁止新运动，先撤使能/行走零速并确认状态，再正式锁存。
600 ms 未确认或活动 Bus-Off 先锁存、尝试关闭 OUT1；锁存后仍只重试安全撤使能。
手动接电绕过 OUT1 的车辆不能用软件输出关闭代替物理急停或真正断电。
正常行走上限 1000 rpm、转向 300/180/90 rpm、停车/换向 6 s、A 使能和 B 无功能保留。

验证：ARM Release、RAM/栈、布局、冻结 Boot、产品 C 和新增故障回归通过。
产品 Python 43/44；既有热复位设计静态检查仍失败，正式发布也要求干净工作区。
dist 未替换，本包不代表已发布或通过现场验收。
'''
    (package/'FLASH_ADDRESSES.txt').write_text(addresses,encoding='utf-8-sig')
    for directory,destination in [(candidate,'out/candidates/bench-drive'),(debug,'out/debug/online')]:
        run([sys.executable,'tools/artifact_store.py','--root',root,'--stage',directory,'--destination',destination,'--validate-only'])
    # Both stages and actual Boot parser are verified before either managed target is replaced.
    for directory,destination in [(candidate,'out/candidates/bench-drive'),(debug,'out/debug/online')]:
        run([sys.executable,'tools/artifact_store.py','--root',root,'--stage',directory,'--destination',destination])
    evidence.mkdir()
    entries=[]
    def archive(source,relative,origin=None):
        target=evidence/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():raise RuntimeError('would overwrite evidence')
        shutil.copyfile(source,target)
        assert sha(source)==sha(target)
        entries.append(dict(original_path=str(source.resolve()) if origin is None else origin,
                            archive_path=str(target.relative_to(root)),bytes=target.stat().st_size,sha256=sha(target)))
    for source in sorted(package.iterdir()):archive(source,source.name)
    archive(stage/'verify-fault-stop-state.c','verify-fault-stop-state.c','derived host verifier from immutable 0.2.74 evidence; size/version updated only')
    archive(build/'Patrol_Vehicle_Chassis_App.elf','Patrol_Vehicle_Chassis_App.elf')
    archive(build/'Patrol_Vehicle_Chassis_App.map','Patrol_Vehicle_Chassis_App.map')
    for name in ['fault-stop-arm-configure.log','fault-stop-arm-final.log','fault-stop-bench-stack.log','fault-stop-can-stack.log',
                 'fault-stop-service-stack.log','fault-stop-frozen-boot.log','fault-stop-layout.log','fault-stop-product-c.log',
                 'fault-stop-new-regression-final.log','fault-stop-boot-state.log','fault-stop-python-validated.log','fault-stop-baseline-check.json',
                 'fault-stop-formal-gate.log','package-fault-stop.py']:
        archive(root/'out/logs'/name,Path('validation')/name)
    for name in ['app/Core/User/motion/include/patrol_fault_stop.h','app/Core/User/motion/src/patrol_fault_stop.c',
                 'app/Core/User/motion/src/patrol_runtime.c','app/Core/User/motion/src/chassis_control.c',
                 'app/Core/User/motion/include/chassis_control.h','app/Core/User/cloud/src/remote_service.c',
                 'tests/fault_stop_runtime_cases.c','tests/test_fault_stop_runtime.py','tools/verify_can2_stack.py',
                 'tools/service_stack_budget.json']:
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
write_json(root/'out/logs/fault-stop-package-latest.json',dict(evidence=str(evidence.resolve()),bundle=str(bundle.resolve()),version=version,app_sha256=manifest['sha256'],app_size=len(app)))
print(json.dumps(dict(evidence=str(evidence.resolve()),bundle=str(bundle.resolve()),app_sha256=manifest['sha256'],app_size=len(app),states_verified=True,dist_changed=False),ensure_ascii=False,indent=2))
