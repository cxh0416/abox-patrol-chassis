"""Explicit operator-requested CAR2 center maintenance for exact App 0.2.77.

No motor enable/target/limit writes. One encoder-zero mailbox byte, then one
Config V5 page programming operation after independently verified zero samples.
"""
import ctypes
import datetime
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT=Path.cwd(); LOGS=ROOT/'out/logs'
sys.path.insert(0,str(ROOT/'tools'))
from make_device_config import config_checksum, inspect_config_page, CONFIG_OFFSET, CONFIG_SIZE
SCHEMA=json.loads((LOGS/'car2-0277-schema.json').read_text(encoding='utf-8'))
MAILBOX=json.loads((LOGS/'car2-center-mailbox.json').read_text(encoding='utf-8'))
UID=bytes.fromhex('36ffd205534d383840251443')
EXPECTED='ced4fd110f4c02b1108a99cc3c2c2eb0d8d948e07232aaa04441741a219ec020'
POINTER=LOGS/'car2-center-maintenance-current.json'


def digest(data):return hashlib.sha256(data).hexdigest()


class Swd:
    def __init__(self,app=True):
        self.j=ctypes.WinDLL('E:/SEGGER/JLink_V984/JLink_x64.dll');self.opened=False
        j=self.j
        j.JLINKARM_ExecCommand.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int]
        j.JLINKARM_ExecCommand.restype=ctypes.c_int;j.JLINKARM_Open.restype=ctypes.c_char_p
        j.JLINKARM_Connect.restype=ctypes.c_int;j.JLINKARM_TIF_Select.argtypes=[ctypes.c_int]
        j.JLINKARM_SetSpeed.argtypes=[ctypes.c_uint32]
        j.JLINKARM_ReadMemEx.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32]
        j.JLINKARM_ReadMemEx.restype=ctypes.c_int
        try:
            for command in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1'):
                self.command(command)
            j.JLINKARM_EMU_SelectByUSBSN.argtypes=[ctypes.c_uint32];j.JLINKARM_EMU_SelectByUSBSN.restype=ctypes.c_int
            assert j.JLINKARM_EMU_SelectByUSBSN(59406895)>=0
            error=j.JLINKARM_Open()
            if error:raise RuntimeError(error.decode(errors='replace'))
            self.opened=True;self.command('Device = Cortex-M3');j.JLINKARM_TIF_Select(1);j.JLINKARM_SetSpeed(1000)
            assert j.JLINKARM_Connect()>=0,'SWD attach failed'
            assert self.read(0x1FFFF7E8,12)==UID,'Wrong vehicle board'
            assert digest(self.read(0x08008000,154328))==EXPECTED,'Wrong App image'
            assert SCHEMA['app_sha256']==MAILBOX['app_sha256']==EXPECTED
            if app:
                assert self.u32(0xE000ED08)==0x08008000,'App not initialized'
                assert self.u32(0xE000E010)&1,'SysTick not active'
                assert not self.u32(0xE000EDF0)&(1<<17),'CPU halted'
        except Exception:
            self.close();raise

    def command(self,text):
        assert text in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1','Device = Cortex-M3')
        b=ctypes.create_string_buffer(1024)
        assert self.j.JLINKARM_ExecCommand(text.encode('ascii'),b,len(b))>=0,text

    def read(self,address,size):
        b=(ctypes.c_uint8*size)()
        assert self.j.JLINKARM_ReadMemEx(address,size,b,0)==size,hex(address)
        return bytes(b)

    def u32(self,address):return int.from_bytes(self.read(address,4),'little')

    def field(self,name):
        f=SCHEMA['fields'][name]
        return int.from_bytes(self.read(f['address'],f['bytes']),'little',signed=f['signed'])

    def preflight(self):
        names=('tick_ms','power_command','fault_pending','fault_latched','session_active','enable_request',
               'target_rpm','requested_rpm','busoff_mask','busoff_seen','feedback_ota_busy',
               'can2_rx','t12_switch_seen','config_calibrated','center_cal_phase','steering_phase','drive_phase',
               'encoder_ms','encoder_count','encoder_config_valid')
        fields={name:self.field(name) for name in names}
        for name in names[1:-3]:
            assert fields[name]==0,('Unsafe or changed maintenance context',name,fields[name])
        assert fields['encoder_config_valid']==1 and fields['encoder_count']<4096
        age=(self.field('tick_ms')-fields['encoder_ms'])&0xffffffff
        # Sequential running SWD reads are not atomic. Re-read the clock if
        # the sample timestamp briefly leads it; never accept a stale age.
        for _ in range(6):
            if age<0x80000000:break
            time.sleep(.015)
            age=(self.field('tick_ms')-fields['encoder_ms'])&0xffffffff
        assert age<250,('Encoder stale',age)
        return fields

    def close(self):
        if self.opened:self.j.JLINKARM_Close();self.opened=False


def read_stationary():
    result=subprocess.run([sys.executable,str(LOGS/'car2-center-read.py')],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stdout+result.stderr
    pointer=json.loads((LOGS/'car2-center-read-latest.json').read_text(encoding='utf-8'))
    p=Path(pointer['directory'])
    data=json.loads((p/'results.json').read_text(encoding='utf-8'))
    assert not data.get('error')
    for key in ('stationary_before','stationary_after'):
        for row in data[key]:
            if row['index']=='0x6041':assert row['value']&0xc==0,row
            else:assert row['value']==0,row
    assert max(data['driver_position_samples'])-min(data['driver_position_samples'])<=5
    return data,p


def commander(directory,name,commands,device):
    prefix=['exec DisableAutoUpdateFW','exec SuppressGUI 1','exec InhibitConnectRetries = 1',
            'exec ForceAttachTarget = 1',f'device {device}','si SWD','speed 1000','connect']
    script=directory/(name+'.jlink')
    script.write_text('\n'.join(prefix+commands+['qc'])+'\n',encoding='ascii')
    result=subprocess.run(['E:/SEGGER/JLink_V984/JLink.exe','-NoGui','1','-ExitOnError','1',
                           '-USB','59406895','-CommanderScript',str(script)],capture_output=True,timeout=25)
    (directory/(name+'.stdout.log')).write_bytes(result.stdout)
    (directory/(name+'.stderr.log')).write_bytes(result.stderr)
    assert result.returncode==0,('J-Link failed',result.returncode,str(script))


def main():
    phase=sys.argv[1];assert phase in ('prepare','zero','verify-zero','flash','verify')
    if phase=='prepare':
        directory=ROOT/'out/flash'/datetime.datetime.now().strftime('%Y-%m-%d-car2-center-maintenance-%H%M%S-%f')
        directory.mkdir()
        stationary,position_dir=read_stationary()
        s=Swd()
        try:
            fields=s.preflight();assert s.read(MAILBOX['address'],1)==b'\0'
            config_base=SCHEMA['fields']['config_calibrated']['address']-388
            config=s.read(config_base,400)
            assert struct.unpack_from('<IHH',config)==(0x56435543,5,400)
            page=s.read(0x0803F800,2048);recovery=s.read(0x0803E000,2048)
            assert recovery==b'\xff'*2048,'Existing recovery journal; use atomic firmware path'
            assert page[CONFIG_OFFSET:CONFIG_OFFSET+400]==b'\xff'*400,'Not a new unconfigured board'
            for name,address,size in [('config-before.bin',0x0803F800,2048),('recovery-before.bin',0x0803E000,2048),
                                      ('state-a-before.bin',0x0803E800,2048),('state-b-before.bin',0x0803F000,2048)]:
                (directory/name).write_bytes(s.read(address,size))
            (directory/'ram-config-before.bin').write_bytes(config)
            plan={'created_at':datetime.datetime.now().astimezone().isoformat(),'vehicle':'CAR2',
                  'operator_confirmed_mechanical_center':True,'uid_raw_hex':UID.hex(),'app_sha256':EXPECTED,
                  'driver_center':stationary['driver_position_samples'][-1],'stationary_read':str(position_dir),
                  'encoder_before':fields['encoder_count'],'mailbox_address':MAILBOX['address'],
                  'calibration_flash_address':'0x0803F800','calibration_flash_size':2048,
                  'writes_allowed':['one byte encoder-zero request at verified ABI mailbox','Config V5 page only'],
                  'preflight':fields}
            (directory/'plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
        finally:s.close()
        POINTER.write_text(json.dumps({'directory':str(directory)},ensure_ascii=False),encoding='utf-8')
        print(json.dumps(plan,ensure_ascii=False,indent=2));return
    directory=Path(json.loads(POINTER.read_text(encoding='utf-8'))['directory'])
    plan=json.loads((directory/'plan.json').read_text(encoding='utf-8'))
    if phase in ('zero','verify-zero'):
        if phase=='zero':
            assert not (directory/'zero-request.jlink').exists(),'Never blindly repeat an uncertain zero request'
        else:
            assert (directory/'zero-request.jlink').exists(),'No prior request to verify'
        stationary,position_dir=read_stationary()
        assert abs(stationary['driver_position_samples'][-1]-plan['driver_center'])<=5
        s=Swd()
        try:
            s.preflight()
            if phase=='zero':assert s.read(MAILBOX['address'],1)==b'\0'
        finally:s.close()
        # The existing encoder task claims 1 -> 4 and handles the exact Modbus
        # write-single 06/0003/000F once, with ACK/uncertain result recording.
        if phase=='zero':
            commander(directory,'zero-request',[f'w1 0x{MAILBOX["address"]:08X} 0x01'],'Cortex-M3')
        s=Swd();samples=[]
        try:
            deadline=time.monotonic()+3;last_ms=None
            while time.monotonic()<deadline and len(samples)<3:
                fields=s.preflight();state=s.read(MAILBOX['address'],1)[0]
                if state==3:raise RuntimeError('Encoder zero send failed; do not replay')
                if state in (2,5) and fields['encoder_ms']!=last_ms:
                    count=fields['encoder_count'];assert count<=2 or count>=4094,('Encoder did not zero',count)
                    samples.append({'sample_ms':fields['encoder_ms'],'count':count,'write_state':state})
                    last_ms=fields['encoder_ms']
                time.sleep(.03)
            assert len(samples)==3,'No three fresh verified zero samples'
        finally:s.close()
        stationary,position_dir=read_stationary()
        assert abs(stationary['driver_position_samples'][-1]-plan['driver_center'])<=5
        zero={'samples':samples,'stationary_after':str(position_dir),'command_replayed':False}
        (directory/'zero-verified.json').write_text(json.dumps(zero,indent=2),encoding='utf-8')
        config=bytearray((directory/'ram-config-before.bin').read_bytes())
        struct.pack_into('<iIIHbbBB',config,372,plan['driver_center'],0,55250,2000,1,1,1,0)
        struct.pack_into('<I',config,392,2710800)
        struct.pack_into('<I',config,8,config_checksum(config))
        page=bytearray((directory/'config-before.bin').read_bytes());page[CONFIG_OFFSET:CONFIG_OFFSET+400]=config
        details=inspect_config_page(page)
        assert details['calibrated'] and details['steeringCenterCount']==plan['driver_center'] and details['absoluteCenterCount']==0
        before=(directory/'config-before.bin').read_bytes()
        assert page[:CONFIG_OFFSET]==before[:CONFIG_OFFSET] and page[CONFIG_OFFSET+400:]==before[CONFIG_OFFSET+400:]
        (directory/'ConfigV5_CAR2_Center.bin').write_bytes(page)
        (directory/'staged-summary.json').write_text(json.dumps({'driver_center':plan['driver_center'],'absolute_center':0,
             'config_page_sha256':digest(page),'zero_verified':True,'other_page_bytes_preserved':True},indent=2),encoding='utf-8')
        print(json.dumps(zero,indent=2));print('Staged verified Config V5 page; not yet flashed.');return
    if phase=='flash':
        assert (directory/'zero-verified.json').exists() and not (directory/'config-program.jlink').exists()
        stationary,_=read_stationary();assert abs(stationary['driver_position_samples'][-1]-plan['driver_center'])<=5
        s=Swd()
        try:
            f=s.preflight();assert f['encoder_count']<=2 or f['encoder_count']>=4094
            assert s.read(0x0803F800,2048)==(directory/'config-before.bin').read_bytes()
            assert s.read(0x0803E000,2048)==(directory/'recovery-before.bin').read_bytes()
        finally:s.close()
        image=(directory/'ConfigV5_CAR2_Center.bin').as_posix()
        commander(directory,'config-program',['h',f'loadbin "{image}", 0x0803F800',
                  f'verifybin "{image}", 0x0803F800','r','g'],'STM32F105RC')
        print('Config page written and verified, MCU restarted; awaiting runtime verification.');return
    s=Swd()
    try:
        page=s.read(0x0803F800,2048);assert page==(directory/'ConfigV5_CAR2_Center.bin').read_bytes()
        assert s.read(0x0803E000,2048)==(directory/'recovery-before.bin').read_bytes()
        for name,address in [('state-a-before.bin',0x0803E800),('state-b-before.bin',0x0803F000)]:
            assert s.read(address,2048)==(directory/name).read_bytes()
        assert digest(s.read(0x08000000,32768))=='981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63'
        names=('config_calibrated','config_steering_center','config_absolute_center','config_steering_scale',
               'config_absolute_scale','feedback_encoder_profile_ready','encoder_count','encoder_ms',
               'fault_pending','fault_latched','power_command','session_active','enable_request','target_rpm','can2_rx')
        fields={name:s.field(name) for name in names}
        assert fields['config_calibrated']==fields['feedback_encoder_profile_ready']==1
        assert fields['config_steering_center']==plan['driver_center'] and fields['config_absolute_center']==0
        assert fields['config_steering_scale']==2710800 and fields['config_absolute_scale']==55250
        assert fields['encoder_count']<=2 or fields['encoder_count']>=4094
        for name in ('fault_pending','fault_latched','power_command','session_active','enable_request','target_rpm','can2_rx'):
            assert fields[name]==0,(name,fields[name])
        assert (s.field('tick_ms')-fields['encoder_ms'])&0xffffffff<250
        verified={'verified_at':datetime.datetime.now().astimezone().isoformat(),'fields':fields,
                  'config_page_sha256':digest(page),'boot_app_states_recovery_unchanged':True,'post_reset_loaded':True}
        (directory/'config-after.bin').write_bytes(page)
    finally:s.close()
    stationary,position_dir=read_stationary();verified['stationary_after_reset']=str(position_dir)
    (directory/'verified.json').write_text(json.dumps(verified,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(verified,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
