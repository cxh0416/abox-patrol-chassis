from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class FaultJournalTests(unittest.TestCase):
 def test_torn_write_full_journal_and_config_preservation(self):
  with tempfile.TemporaryDirectory() as d:
   d=Path(d)
   (d/'main.h').write_text("""#include <stdint.h>
typedef int HAL_StatusTypeDef;
typedef struct {uint32_t TypeErase,PageAddress,NbPages;} FLASH_EraseInitTypeDef;
#define HAL_OK 0
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_TYPEPROGRAM_WORD 1
void HAL_FLASH_Unlock(void); void HAL_FLASH_Lock(void);
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef*,uint32_t*);
int HAL_FLASH_Program(uint32_t,uint32_t,uint64_t);
""")
   (d/'test.c').write_text(r"""
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "main.h"
#include "ota_info.h"
#include "motion_fault.h"
static int fail_after=-1;
static int fail_primary_after=-1;
static int fail_recovery_erase_after=-1;
void HAL_FLASH_Unlock(void) {} void HAL_FLASH_Lock(void) {}
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef* e,uint32_t* error) {
 if(e->PageAddress==CONFIG_RECOVERY_ADDR && fail_recovery_erase_after==0)return 1;
 if(e->PageAddress==CONFIG_RECOVERY_ADDR && fail_recovery_erase_after>0)fail_recovery_erase_after--;
 (void)error; memset((void*)(uintptr_t)e->PageAddress,255,2048); return 0;
}
int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t value) {
 (void)type; if(fail_after==0)return 1; if(fail_after>0)fail_after--;
 if(address>=OTA_INFO_ADDR && address<OTA_INFO_ADDR+2048) {
  if(fail_primary_after==0)return 1; if(fail_primary_after>0)fail_primary_after--;
 }
 uint32_t* p=(uint32_t*)(uintptr_t)address; assert((*p & (uint32_t)value)==(uint32_t)value); *p=(uint32_t)value; return 0;
}
int main(void) {
#ifdef _WIN32
 assert(VirtualAlloc((void*)0x08030000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap((void*)0x08030000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void*)-1);
#endif
 memset((void*)OTA_INFO_ADDR,255,2048);
 MotionFaultRecord r={0},out={0}; r.magic=0x4D463031; r.sequence=1; r.drive_sw=0xBF;
 assert(!OTA_Info_LoadMotionFault(&out));
 assert(OTA_Info_SaveMotionFault(&r)); assert(OTA_Info_LoadMotionFault(&out)&&out.sequence==1);
 r.sequence=2; fail_after=6; assert(!OTA_Info_SaveMotionFault(&r)); fail_after=-1;
 assert(OTA_Info_LoadMotionFault(&out)&&out.sequence==1);
 for(unsigned i=2;i<=7;i++){r.sequence=i;assert(OTA_Info_SaveMotionFault(&r));}
 r.sequence=8;assert(!OTA_Info_SaveMotionFault(&r));
 assert(OTA_Info_LoadMotionFault(&out)&&out.sequence==7);
 OTA_Info_t cfg; memset(&cfg,0x5A,sizeof(cfg)); assert(OTA_Info_Save(&cfg));
 assert(memcmp((void*)OTA_INFO_ADDR,&cfg,sizeof(cfg))==0);
 assert(OTA_Info_LoadMotionFault(&out)&&out.sequence==7);
 assert(OTA_Info_SaveMotionFault(&r)); assert(OTA_Info_LoadMotionFault(&out)&&out.sequence==8);
 OTA_Info_t old=cfg,next=cfg,loaded;
 next.fw_size=0x12345678;
 fail_primary_after=3; assert(!OTA_Info_Save(&next)); fail_primary_after=-1;
 OTA_Info_Load(&loaded); assert(memcmp(&loaded,&old,sizeof(old))==0);
 /* Power after primary verification but before recovery-page erase also rolls back. */
 fail_recovery_erase_after=1; assert(!OTA_Info_Save(&next)); fail_recovery_erase_after=-1;
 OTA_Info_Load(&loaded); assert(memcmp(&loaded,&old,sizeof(old))==0);
 /* Frozen V1 journal from the old firmware, interrupted before primary write. */
 typedef struct { uint32_t magic; uint16_t version,length;
   unsigned char info[offsetof(OTA_Info_t,mqtt_tls_secret)];
   unsigned char fault_valid,reserved[3]; MotionFaultRecord fault;
   uint32_t checksum,commit; } V1Journal;
 V1Journal legacy={0};legacy.magic=0x43524631;legacy.version=1;
 legacy.length=sizeof(legacy);legacy.commit=0x434F4D31;
 memcpy(legacy.info,&old,sizeof(legacy.info));
 uint32_t crc=0xffffffff;const unsigned char *bytes=(const unsigned char*)&legacy;
 for(unsigned i=0;i<offsetof(V1Journal,checksum);++i){crc^=bytes[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320:0);}
 legacy.checksum=crc^0xffffffff;
 memset((void*)CONFIG_RECOVERY_ADDR,255,2048);memcpy((void*)CONFIG_RECOVERY_ADDR,&legacy,sizeof(legacy));
 memset((void*)OTA_INFO_ADDR,255,2048);
 OTA_Info_Load(&loaded);assert(!memcmp(&loaded,&old,sizeof(legacy.info)));
 ABoxTlsCredentials credentials={0};assert(OTA_Info_LoadMqttCredentials(&credentials));
 assert(credentials.mode==ABOX_TLS_AUTH_CA);
 return 0;
}
""")
   cmd=[shutil.which('gcc'),' -std=c11'.strip(),'-D_DEFAULT_SOURCE','-Wno-int-to-pointer-cast','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(d),'-I'+str(ROOT/'shared/include'),str(d/'test.c'),str(ROOT/'app/Core/User/ota/src/ota_info.c'),'-I'+str(ROOT/'platform/components/ec800/include'),str(ROOT/'platform/components/ec800/src/abox_tls_secret_record.c'),'-o',str(d/'test.exe')]
   r=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   subprocess.run([str(d/'test.exe')],check=True)
