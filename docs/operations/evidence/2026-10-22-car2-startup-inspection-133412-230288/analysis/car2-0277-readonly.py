"""Read-only SWD verification for the exact 0.2.77 image. No halt/reset/go/write APIs."""
import ctypes,datetime,hashlib,json,pathlib,sys
root=pathlib.Path.cwd()
schema_path=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root/'out/logs/car2-0277-schema.json'
schema=json.loads(schema_path.read_text(encoding='utf-8'))
manifest=json.loads((root/'out/candidates/bench-drive/candidate.manifest.json').read_text(encoding='utf-8'))
assert manifest['sha256']==schema['app_sha256']
for name,digest in manifest['source_sha256'].items():
 assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Source changed; refuse ABI assumption: '+name
stamp=datetime.datetime.now().strftime('%Y-%m-%d-fault-stop-readonly-%H%M%S-%f')
output=root/'out/flash'/stamp;output.mkdir(parents=True,exist_ok=False)
result={'host_started_at':datetime.datetime.now().astimezone().isoformat(),'mode':'SWD attach/read only; no halt/reset/go/target writes','schema':str(schema_path),'expected_app_sha256':schema['app_sha256'],'probe_serial':59406895}
j=ctypes.WinDLL('E:/SEGGER/JLink_V984/JLink_x64.dll')
j.JLINKARM_ExecCommand.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int];j.JLINKARM_ExecCommand.restype=ctypes.c_int
j.JLINKARM_Open.restype=ctypes.c_char_p
j.JLINKARM_Connect.restype=ctypes.c_int
j.JLINKARM_TIF_Select.argtypes=[ctypes.c_int]
j.JLINKARM_SetSpeed.argtypes=[ctypes.c_uint32]
j.JLINKARM_ReadMemEx.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32];j.JLINKARM_ReadMemEx.restype=ctypes.c_int
opened=False
# 只允许明确列出的连接配置，禁止运行/暂停/复位和内存写入。
def command(value):
 assert value in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1','Device = Cortex-M3')
 buffer=ctypes.create_string_buffer(1024);rc=j.JLINKARM_ExecCommand(value.encode('ascii'),buffer,len(buffer))
 if rc<0:raise RuntimeError('J-Link command failed: '+value+': '+buffer.value.decode(errors='replace'))
# ReadMemEx 仅读取；完整 App 只计算哈希，不保存 Flash 或逐车配置。
def read(address,size):
 buffer=(ctypes.c_uint8*size)();rc=j.JLINKARM_ReadMemEx(address,size,buffer,0)
 if rc!=size:raise RuntimeError('Read failed at '+hex(address)+': '+str(rc))
 return bytes(buffer)
try:
 for value in ('DisableAutoUpdateFW','SuppressGUI 1','InhibitConnectRetries = 1','ForceAttachTarget = 1'):command(value)
 if hasattr(j,'JLINKARM_EMU_SelectByUSBSN'):
  j.JLINKARM_EMU_SelectByUSBSN.argtypes=[ctypes.c_uint32];j.JLINKARM_EMU_SelectByUSBSN.restype=ctypes.c_int
  if j.JLINKARM_EMU_SelectByUSBSN(59406895)<0:raise RuntimeError('Expected J-Link probe unavailable')
 error=j.JLINKARM_Open()
 if error:raise RuntimeError(error.decode(errors='replace'))
 opened=True;command('Device = Cortex-M3');j.JLINKARM_TIF_Select(1);j.JLINKARM_SetSpeed(1000)
 if j.JLINKARM_Connect()<0:raise RuntimeError('SWD attach failed')
 before=int.from_bytes(read(0xE000EDF0,4),'little');result['dhcsr_before']=hex(before)
 if before&(1<<17):raise RuntimeError('CPU already halted; do not change run state')
 digest=hashlib.sha256(read(schema['app_address'],schema['app_bytes'])).hexdigest();result['observed_app_sha256']=digest
 if digest!=schema['app_sha256']:raise RuntimeError('Running image mismatch; refuse RAM interpretation')
 vtor=int.from_bytes(read(0xE000ED08,4),'little');result['vtor']=hex(vtor)
 if vtor!=schema['app_address']:raise RuntimeError('App VTOR not active; refuse RAM interpretation')
 if not (int.from_bytes(read(0xE000E010,4),'little')&1):raise RuntimeError('SysTick disabled; refuse RAM interpretation')
 values={}
 for name,field in schema['fields'].items():
  data=read(field['address'],field['bytes'])
  if name=='last_fault':
   (output/'motion-fault.bin').write_bytes(data);values[name+'_hex']=data.hex()
  else:values[name]=int.from_bytes(data,'little',signed=field['signed'])

 for name in ('fault_pending','fault_latched','session_active','enable_request','config_calibrated','feedback_calibrated'):
  if values[name] not in (0,1):raise RuntimeError('RAM invariant failed: '+name)
 result['fields']=values
 result['ram_decoding_valid']=True
 result['tick_after_ms']=int.from_bytes(read(schema['fields']['tick_ms']['address'],4),'little')
 registers={}
 for name,address,size in [('CAN1',0x40006400,32),('CAN2',0x40006800,32)]:
  data=read(address,size);(output/(name+'-registers.bin')).write_bytes(data);registers[name]=data.hex()
 result['registers']=registers
 result['dhcsr_after']=hex(int.from_bytes(read(0xE000EDF0,4),'little'))
 result['verified_image']='patrol-chassis-0.2.77'
except Exception as exc:result['error']=type(exc).__name__+': '+str(exc)
finally:
 if opened:j.JLINKARM_Close()
 result['host_finished_at']=datetime.datetime.now().astimezone().isoformat()
 (output/'snapshot.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
 (root/'out/logs/car2-startup-readonly-latest.json').write_text(json.dumps({'directory':str(output),'error':result.get('error')},ensure_ascii=False),encoding='utf-8')
 print(json.dumps(result,ensure_ascii=False,indent=2))
 print('SNAPSHOT',str(output))
if 'error' in result:raise SystemExit(1)
