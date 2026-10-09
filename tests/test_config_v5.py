from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ConfigV5Tests(unittest.TestCase):
    def test_v5_only_load_save_and_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            (d / "main.h").write_text("""#include <stdint.h>
typedef int HAL_StatusTypeDef;
typedef struct {uint32_t TypeErase,PageAddress,NbPages;} FLASH_EraseInitTypeDef;
#define HAL_OK 0
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_TYPEPROGRAM_WORD 1
void HAL_FLASH_Unlock(void); void HAL_FLASH_Lock(void);
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef*,uint32_t*);
int HAL_FLASH_Program(uint32_t,uint32_t,uint64_t);
""", encoding="utf-8")
            (d / "test.c").write_text(r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "main.h"
#include "ota_info.h"

static unsigned erase_count;
void HAL_FLASH_Unlock(void) {} void HAL_FLASH_Lock(void) {}
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef* e,uint32_t* error) {
 (void)error; erase_count++; memset((void*)(uintptr_t)e->PageAddress,255,2048); return 0;
}
int HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t value) {
 (void)type; uint32_t* p=(uint32_t*)(uintptr_t)address;
 assert((*p & (uint32_t)value)==(uint32_t)value); *p=(uint32_t)value; return 0;
}
static uint32_t checksum(const void* value,size_t size) {
 const uint8_t* p=value; uint32_t sum=0x13572468U;
 for(size_t i=0;i<size;i++){if(i>=8&&i<12)continue;sum=(sum<<5)^(sum>>27)^p[i];}
 return sum;
}
static void base(PatrolRemoteConfigV5_t* c) {
 memset(c,0,sizeof(*c));
 c->magic=PATROL_CONFIG_MAGIC;c->version=PATROL_CONFIG_VERSION;c->length=sizeof(*c);
 c->reserved_remote=1;c->diag_log_enabled=1;c->diag_log_level=2;
 strcpy(c->vid,"PATROL-V5-TEST");strcpy(c->mqtt_host,"broker.test");c->mqtt_port=19683;
 strcpy(c->mqtt_username,"account");strcpy(c->mqtt_password,"secret");
 strcpy(c->diag_log_host,"diag.test");c->diag_log_port=30080;strcpy(c->ota_origin,"https://ota.test");
 c->steering_center_count=-12345;c->absolute_center_count=6789;
 c->absolute_counts_per_degree_milli=55250;c->steering_tolerance_mdeg=2000;
 c->steering_direction=1;c->absolute_direction=1;c->calibrated=1;
}
static void install(const void* cfg,size_t size) {
 memset((void*)OTA_INFO_ADDR,255,2048); memcpy((void*)(OTA_INFO_ADDR+offsetof(OTA_Info_t,remote_config)),cfg,size);
}
int main(void) {
#ifdef _WIN32
 assert(VirtualAlloc((void*)0x08030000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap((void*)0x08030000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void*)-1);
#endif
 memset((void*)0x08030000,255,65536);
 PatrolRemoteConfigV5_t out,v5;
 base(&v5);v5.steering_counts_per_degree_milli=2710800;
 v5.mqtt_tls_enabled=0;v5.mqtt_tls_profile_id=0;v5.checksum=checksum(&v5,sizeof(v5));
 install(&v5,sizeof(v5));erase_count=0;assert(OTA_Info_LoadRemoteConfig(&out));assert(!memcmp(&out,&v5,sizeof(v5)));assert(!erase_count);
 assert(OTA_Info_SaveRemoteConfig(&v5));memset(&out,0,sizeof(out));assert(OTA_Info_LoadRemoteConfig(&out));assert(!memcmp(&out,&v5,sizeof(v5)));
 ABoxTlsCredentials key={0},read={0};
 assert(OTA_Info_LoadMqttCredentials(&read) && read.mode==ABOX_TLS_AUTH_CA);
 key.mode=ABOX_TLS_AUTH_PSK;key.generation=1;
 strcpy(key.identity,"storage-test.g1");strcpy(key.secret,"0123456789abcdefghijklmnopqrstuv");
 assert(OTA_Info_SaveMqttSnapshot(&v5,&key));
 assert(OTA_Info_LoadMqttCredentials(&read) && !memcmp(&read,&key,sizeof(key)));
 assert(OTA_Info_SaveRemoteConfig(&v5));
 assert(OTA_Info_LoadMqttCredentials(&read) && read.generation==1);
 ((OTA_Info_t*)OTA_INFO_ADDR)->mqtt_tls_secret.checksum^=1;
 assert(!OTA_Info_LoadMqttCredentials(&read) && read.mode==0xff);

 v5.checksum^=1;install(&v5,sizeof(v5));assert(!OTA_Info_LoadRemoteConfig(&out));
 v5.checksum^=1;v5.length--;v5.checksum=checksum(&v5,sizeof(v5));install(&v5,sizeof(v5));assert(!OTA_Info_LoadRemoteConfig(&out));
 v5.length=sizeof(v5);v5.version=4;v5.checksum=checksum(&v5,sizeof(v5));install(&v5,sizeof(v5));assert(!OTA_Info_LoadRemoteConfig(&out));
 v5.version=3;v5.checksum=checksum(&v5,sizeof(v5));install(&v5,sizeof(v5));assert(!OTA_Info_LoadRemoteConfig(&out));
 return 0;
}
""", encoding="utf-8")
            cmd = [shutil.which("gcc"), "-std=c11", "-D_DEFAULT_SOURCE", "-Wno-int-to-pointer-cast",
                   "-I" + str(d), "-I" + str(ROOT / "shared/include"), str(d / "test.c"),
                   str(ROOT / "app/Core/User/ota/src/ota_info.c"), "-I" + str(ROOT / "platform/components/ec800/include"), str(ROOT / "platform/components/ec800/src/abox_tls_secret_record.c"), "-o", str(d / "test.exe")]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(d / "test.exe")], check=True)


if __name__ == "__main__":
    unittest.main()
