#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "stm32f1xx.h"
#include "SEGGER_RTT.h"
#include "abox_ec800_at.h"
#include "abox_ec800_ufs_at.h"
#include <stdio.h>
#include "patrol_power_ledger.h"
#include "cloud_internal.h"
#include "time_base.h"
#include "chassis_cloud_protocol.h"
#include "abox_mqtt_v4.h"
#include "abox_mqtt_ec800.h"

#define OWNER_UFS 16U
#define OWNER_DIAG 17U
#define RXSIZE 1536U
#define MB ((volatile uint32_t *)0x2000F000UL)
#define FILE_A "UFS:patrol_adapter_lab_a.bin"
#define FILE_B "UFS:patrol_adapter_lab_b.bin"
#define MISSING_A "UFS:abox_fsm_missing_799713843_a.bin"
#define MISSING_B "UFS:abox_fsm_missing_799713843_b.bin"
#define MARK_ADDR 0x0803D800U
#define MARK_VALUE 0x4D415031U
#define MQTT_IN "abox-ufs-lab/f96d31ea8210bdec90ea1507/in"
#define MQTT_OUT "abox-ufs-lab/f96d31ea8210bdec90ea1507/out"
static ABoxEc800At at;

static CloudContext cloud_context;
CloudContext g_cloud;
static uint32_t power_generation;
static unsigned force_virgin;
static ABoxMqttEc800 mqtt;
static uint8_t mqtt_header[160],mqtt_topic[128],mqtt_payload[1152],mqtt_publish[512];
static char out_queue[4][256];
static unsigned out_head,out_tail,out_count,actions,stops,sim_power,probes,probes_busy,received;
static uint32_t next_status;
static unsigned enable_fsm,enable_mqtt,list_found,list_missing,pause_after_action;
static unsigned finish_requested;
static uint8_t dma_rx[RXSIZE];
static uint8_t libc_heap[2816];
static unsigned libc_heap_used;
void *_sbrk(ptrdiff_t amount)
{
    if (amount < 0 || (size_t)amount > sizeof(libc_heap)-libc_heap_used) return (void *)-1;
    void *result=libc_heap+libc_heap_used;
    libc_heap_used+=(unsigned)amount;
    return result;
}
static uint32_t rx_tail;
static unsigned inject_cut;
static volatile int command_done, command_result;
static uint32_t last_cycles, elapsed_ms, fractional_cycles;


extern uint32_t __bss_start__, __bss_end__;
static void feed_watchdog(void) { IWDG->KR = 0xAAAAU; }
static uint32_t tick_ms(void *context)
{
    (void)context;
    uint32_t now=DWT->CYCCNT, delta=now-last_cycles;
    last_cycles=now;
    uint64_t cycles=(uint64_t)fractional_cycles+delta;
    elapsed_ms+=(uint32_t)(cycles/8000U);
    fractional_cycles=(uint32_t)(cycles%8000U);
    return elapsed_ms;
}
static void wait_ms(uint32_t duration) { uint32_t t=tick_ms(0); while ((uint32_t)(tick_ms(0)-t)<duration) feed_watchdog(); }
static void set_stage(unsigned stage) { MB[1]=stage; SEGGER_RTT_printf(0,"[UFS] stage=%u time=%lu ms\n",stage,(unsigned long)tick_ms(0)); }
static void stop_with(unsigned error)
{
    MB[2]=error; MB[15]=0xBAD00000U|error;
    SEGGER_RTT_printf(0,"[UFS] FAIL error=%u t=%lu at_quarantine=%u journal=%d backend=%d cme=%u rx=%lu\n",error,(unsigned long)tick_ms(0),at.quarantined,PatrolPowerLedger_CheckOther("/zxwl/abox/PATROL-V1-LAB/request","health"),0,0,(unsigned long)MB[11]);
    for (;;) feed_watchdog();
}
static int uart_write(void *context,const uint8_t *data,uint16_t length,uint32_t timeout)
{
    (void)context; uint32_t start=tick_ms(0);
    for (unsigned i=0;i<length;++i) {
        while (!(USART1->SR & USART_SR_TXE)) {
            feed_watchdog(); if ((uint32_t)(tick_ms(0)-start)>timeout) return 0;
        }
        USART1->DR=data[i];
    }
    while (!(USART1->SR & USART_SR_TC)) { feed_watchdog(); if ((uint32_t)(tick_ms(0)-start)>timeout) return 0; }
    if (inject_cut && at.active_valid && length>=128U &&
        !strncmp(at.active.command,"AT+QFWRITE=",11U)) {
        inject_cut=0U; MB[14]=1U;
        GPIOC->BRR=(1U<<7); /* Cut immediately after data TX, before QFWRITE response. */
        SEGGER_RTT_WriteString(0,"[UFS] injected modem rail cut after QFWRITE payload, before completion\n");
    }
    return 1;
}
static void mqtt_output_task(void);
static void pump(void)
{
    uint32_t head=(RXSIZE-DMA1_Channel5->CNDTR)%RXSIZE;
    while (rx_tail!=head) {
        uint8_t c=dma_rx[rx_tail]; rx_tail=(rx_tail+1U)%RXSIZE; MB[11]++;
        ABoxEc800At_Feed(&at,&c,1U);
        head=(RXSIZE-DMA1_Channel5->CNDTR)%RXSIZE;
    }
    ABoxEc800At_Task(&at);
    if (enable_mqtt) ABoxMqttEc800_Poll(&mqtt,tick_ms(0));
    if (enable_fsm) PatrolPowerLedger_Poll();
    if (enable_mqtt) mqtt_output_task();
    feed_watchdog();
}
static void diag_event(ABoxEc800Event event,const uint8_t *data,uint16_t len,void *user)
{
    (void)user;
    if (event==ABOX_EC800_EVENT_LINE && len && len<150U) {
        char line[151]; memcpy(line,data,len); line[len]=0; if (!strncmp(line,"+QFLST:",7)) list_found=1; if (!strcmp(line,"+CME ERROR: 405")) list_missing=1;
        SEGGER_RTT_printf(0,"[AT<] %s\n",line);
    }
}
static void done(ABoxEc800Result result,void *user) { (void)user; command_result=result; command_done=1; }
static int issue(const char *command,uint32_t timeout)
{
    command_done=0; command_result=-1;
    if (!ABoxEc800At_Submit(&at,command,OWNER_DIAG,ABOX_EC800_PRIORITY_NORMAL,timeout,done,0)) return 0;
    uint32_t begin=tick_ms(0);
    while (!command_done && (uint32_t)(tick_ms(0)-begin)<timeout+1000U) pump();
    return command_done && command_result==ABOX_EC800_RESULT_OK;
}
static int marker_read(void *ctx)
{
    (void)ctx; if(force_virgin)return 0; uint32_t n=*(volatile uint32_t *)MARK_ADDR;
    return n==0xFFFFFFFFU?0:n==MARK_VALUE?1:-1;
}
static int marker_write(void *ctx)
{
    (void)ctx;
    if (force_virgin) {
        if (*(volatile uint32_t *)MARK_ADDR != MARK_VALUE) return 0;
        force_virgin=0;
    }
    return marker_read(0)==1;
}
static void enqueue(const char *s);
int OTA_Info_PowerLedgerMarker(void) { return marker_read(0); }
int OTA_Info_MarkPowerLedgerInitialized(void) { return marker_write(0); }
uint8_t OtaService_IsBusy(void) { return 0; }
uint8_t MQTT_Core_IsReady(void) { return ABoxMqttEc800_IsReady(&mqtt); }
uint32_t VehiclePowerOutput_StopGeneration(void) { return power_generation; }
void VehiclePowerOutput_CancelPendingOn(void) { ++power_generation; }
uint8_t VehiclePowerOutput_IsOn(void) { return (uint8_t)sim_power; }
uint8_t VehiclePowerOutput_Set(uint8_t on) { if(!on) ++power_generation; sim_power=on; return 1; }
void RemoteService_Revoke(void) { ++power_generation; }
uint8_t RemoteService_HasCachedRequest(const char *id) { (void)id; return 0; }
uint8_t RemoteService_HandleControl(const char *payload,uint16_t len,char *out,uint16_t cap)
{ (void)payload;(void)len;(void)out;(void)cap;++stops;RemoteService_Revoke();VehiclePowerOutput_Set(0);return 1; }
int RemoteService_PowerOnPrepared(uint32_t generation,uint8_t *state)
{
    if(generation!=power_generation) { *state=(uint8_t)sim_power; return -2; }
    ++actions;sim_power=1;*state=1;
    if(pause_after_action) { MB[9]=actions; SEGGER_RTT_WriteString(0,"[adapter] simulated action happened; paused before result save\n");for(;;)feed_watchdog(); }
    return 1;
}
ABoxClockSnapshot TimeBase_GetSnapshot(void)
{ uint32_t n=tick_ms(0);ABoxClockSnapshot c={0};c.mono_ms=n;c.utc_ms=1800000700000ULL+n;c.quality=ABOX_CLOCK_SYNCHRONIZED;c.error_ms=1;return c; }
uint64_t TimeBase_GetMs(void) { return 1800000700000ULL+tick_ms(0); }
uint8_t TimeBase_FormatUtcMs(char *out,uint16_t cap)
{ return (uint8_t)ABoxMqttV4_FormatInteger(out,cap,TimeBase_GetMs()); }
uint8_t CloudMessages_ScheduleRawResponse(const char *topic,const char *payload)
{ (void)topic;enqueue(payload);return 1; }
static void enqueue(const char *s)
{
    if(out_count>=4U || strlen(s)>=sizeof(out_queue[0])) { MB[7]++;return; }
    strcpy(out_queue[out_head],s);out_head=(out_head+1U)%4U;++out_count;
}
static void on_message(void *ctx,const uint8_t *topic,size_t tl,const uint8_t *payload,size_t len)
{
    (void)ctx;(void)topic;(void)tl;++received;
    if(len==4 && !memcmp(payload,"ping",4)) { ++probes;if(PatrolPowerLedger_StorageBusy())++probes_busy;return; }
    if(len==6 && !memcmp(payload,"finish",6)) { finish_requested=1;return; }
    if(len==9 && !memcmp(payload,"arm-after",9)) { pause_after_action=1;enqueue("{\"event\":\"armed-after\"}");return; }
    if(!len || len>=1025U) { enqueue("{\"event\":\"invalid\"}");return; }
    char json[1025],response[512];memcpy(json,payload,len);json[len]=0;
    ChassisCloudCommand cmd;
    if(ChassisCloud_Parse(json,(uint16_t)len,&cmd)!=CHASSIS_CLOUD_OK) { enqueue("{\"event\":\"invalid\"}");return; }
    int result=PatrolPowerLedger_Intercept("/zxwl/abox/PATROL-V1-LAB/request",json,response,sizeof(response));
    if(result==1) enqueue(response);
    else if(result==0 && (cmd.type==CHASSIS_CLOUD_COMMAND_POWER_OFF ||
             (cmd.type==CHASSIS_CLOUD_COMMAND_MOTION && !strcmp(cmd.action,"stop")))) {
        RemoteService_HandleControl(json,(uint16_t)len,response,sizeof(response));enqueue("{\"event\":\"safe_stop\"}");
    } else if(result==2 && cmd.type==CHASSIS_CLOUD_COMMAND_POWER_ON) {
        char event[128];snprintf(event,sizeof(event),"{\"event\":\"begin\",\"id\":\"%s\"}",cmd.request_id);enqueue(event);
    }
}
static void on_state(void *ctx,ABoxMqttEc800State state) { (void)ctx;SEGGER_RTT_printf(0,"[mqtt] state=%u\n",(unsigned)state); }
static void on_publish(void *ctx,uint64_t op,ABoxMqttEc800PublishEvent event)
{ (void)ctx;(void)op;if(event==ABOX_MQTT_EC800_PUBLISH_CONFIRMED)MB[4]++;else if(event==ABOX_MQTT_EC800_PUBLISH_FAILED)MB[5]++; }
static void mqtt_output_task(void)
{
    if(!ABoxMqttEc800_IsReady(&mqtt))return;
    uint32_t now=tick_ms(0);
    if((int32_t)(now-next_status)>=0 && out_count<3) {
        char s[256];snprintf(s,sizeof(s),"{\"event\":\"stats\",\"tick\":%lu,\"actions\":%u,\"stops\":%u,\"probes\":%u,\"busyProbes\":%u,\"received\":%u,\"marker\":%d,\"ready\":%u,\"drops\":%lu}",
            (unsigned long)now,actions,stops,probes,probes_busy,received,marker_read(0),PatrolPowerLedger_CheckOther("/zxwl/abox/PATROL-V1-LAB/request","health")==200,(unsigned long)MB[7]);enqueue(s);next_status=now+1000U;
    }
    if(out_count && !mqtt.publish_active) {
        uint64_t op;char *p=out_queue[out_tail];
        if(ABoxMqttEc800_Publish(&mqtt,MQTT_OUT,(const uint8_t*)p,strlen(p),1,0,&op)) { out_tail=(out_tail+1U)%4U;--out_count; }
    }
}
static void clock_and_uart(void)
{
    __disable_irq(); SysTick->CTRL=0; NVIC->ICER[0]=0xFFFFFFFFUL; NVIC->ICER[1]=0xFFFFFFFFUL;
    DBGMCU->CR=0U; feed_watchdog();
    RCC->CR |= RCC_CR_HSION; while (!(RCC->CR&RCC_CR_HSIRDY)) feed_watchdog();
    RCC->CFGR &= ~(RCC_CFGR_SW); while (RCC->CFGR&RCC_CFGR_SWS) feed_watchdog();
    RCC->CFGR &= ~(RCC_CFGR_HPRE|RCC_CFGR_PPRE1|RCC_CFGR_PPRE2);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT=0; DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk; last_cycles=DWT->CYCCNT; elapsed_ms=0U; fractional_cycles=0U;
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN|RCC_APB2ENR_IOPCEN|RCC_APB2ENR_AFIOEN|RCC_APB2ENR_USART1EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel4->CCR=0; DMA1_Channel5->CCR=0;
    USART1->CR1=0; USART1->CR2=0; USART1->CR3=0;
    GPIOA->CRH=(GPIOA->CRH & ~((0xFU<<4)|(0xFU<<8))) | (0xBU<<4) | (0x4U<<8);
    GPIOC->BRR=(1U<<7)|(1U<<8)|(1U<<9);
    GPIOC->CRL=(GPIOC->CRL & ~(0xFU<<28)) | (2U<<28);
    GPIOC->CRH=(GPIOC->CRH & ~0xFFU) | 0x22U;
    USART1->BRR=69U; USART1->CR1=USART_CR1_UE|USART_CR1_TE|USART_CR1_RE;
    DMA1_Channel5->CPAR=(uint32_t)&USART1->DR;
    DMA1_Channel5->CMAR=(uint32_t)dma_rx; DMA1_Channel5->CNDTR=RXSIZE;
    DMA1_Channel5->CCR=DMA_CCR_MINC|DMA_CCR_CIRC|DMA_CCR_PL_1|DMA_CCR_EN;
    USART1->CR3|=USART_CR3_DMAR;
}
static void start_modem(void)
{
    SEGGER_RTT_WriteString(0,"[UFS] modem power off 6s, rail 2s, PWRKEY 3s\n");
    wait_ms(6000U); GPIOC->BSRR=(1U<<7); wait_ms(2000U);
    GPIOC->BSRR=(1U<<8); wait_ms(3000U); GPIOC->BRR=(1U<<8);
}
static void program_main(uint32_t mode)
{
    for(unsigned i=0;i<16;i++)MB[i]=0; MB[0]=mode;
    force_virgin=(mode==30); clock_and_uart();SEGGER_RTT_Init(); strcpy(g_cloud.topic_control_resp,"/zxwl/abox/PATROL-V1-LAB/response");
    SEGGER_RTT_printf(0,"[fsm] mode=%u marker initial=%d\n",(unsigned)mode,marker_read(0));
    if((mode==30 && marker_read(0)!=0)||((mode==31 || mode==32) && marker_read(0)!=1))stop_with(50);
    start_modem();ABoxEc800AtPort atport={0};atport.tick_ms=tick_ms;atport.write=uart_write;
    if(!ABoxEc800At_Init(&at,&atport)||!ABoxEc800At_Register(&at,OWNER_DIAG,diag_event,0))stop_with(1);
    int ok=0;for(unsigned i=0;i<12&&!ok;i++){ok=issue("AT",1500);if(!ok)wait_ms(500);}
    if(!ok||!issue("ATE0",3000))stop_with(2);
    if(mode==30 || mode==32) {
        const char *a=mode==30?"AT+QFLST=\"" FILE_A "\"":"AT+QFLST=\"" MISSING_A "\"";
        const char *b=mode==30?"AT+QFLST=\"" FILE_B "\"":"AT+QFLST=\"" MISSING_B "\"";
        list_found=list_missing=0;ok=issue(a,3000);if((!ok&&!list_missing)||list_found)stop_with(40);
        list_found=list_missing=0;ok=issue(b,3000);if((!ok&&!list_missing)||list_found)stop_with(41);
    }
    if(!PatrolPowerLedger_Init(&at))stop_with(3);
    enable_fsm=1;uint32_t start=tick_ms(0);
    while(PatrolPowerLedger_CheckOther("/zxwl/abox/PATROL-V1-LAB/request","health")!=200 &&
          (uint32_t)(tick_ms(0)-start)<60000U) pump();
    if(PatrolPowerLedger_CheckOther("/zxwl/abox/PATROL-V1-LAB/request","health")!=200)stop_with(6);
    SEGGER_RTT_printf(0,"[adapter] storage ready marker=%d mode=%u\n",marker_read(0),(unsigned)mode);
    static const char *subs[]={MQTT_IN};
    ABoxMqttEc800Config cfg={0};cfg.host="test.mosquitto.org";cfg.client_id="abox-fsm-799713843";cfg.port=1883;cfg.subscriptions=subs;cfg.subscription_count=1;
    cfg.command_timeout_ms=5000;cfg.open_timeout_ms=125000;cfg.connect_timeout_ms=10000;cfg.subscribe_timeout_ms=10000;cfg.publish_timeout_ms=15000;cfg.retry_delay_ms=4000;
    cfg.legacy_json=1;cfg.mqtt_version=4;cfg.pdp_context_id=1;cfg.clean_session=1;
    ABoxMqttEc800Buffers buffers={mqtt_header,mqtt_topic,mqtt_payload,sizeof(mqtt_header),sizeof(mqtt_topic),sizeof(mqtt_payload),mqtt_publish,sizeof(mqtt_publish)};
    ABoxMqttEc800Callbacks cb={0,on_message,on_state,on_publish,0};
    if(!ABoxMqttEc800_Init(&mqtt,&at,&cfg,&buffers,&cb,tick_ms(0)))stop_with(7);
    ABoxMqttEc800_SetSecurityReady(&mqtt,1);
    if(!ABoxMqttEc800_Start(&mqtt,tick_ms(0)))stop_with(8);
    enable_mqtt=1;MB[15]=0x600D0000U|mode;
    unsigned recoveries=0;
    for(;;) {
        pump();
        if(finish_requested && enable_mqtt) {
            out_count=0;
            int result=ABoxMqttEc800_Stop(&mqtt,tick_ms(0));
            if(result!=0) { enable_mqtt=0; SEGGER_RTT_WriteString(0,"[fsm] test MQTT stopped; RAM firmware left running\n"); }
        }
        if(!finish_requested && mqtt.state==ABOX_MQTT_EC800_BLOCKED && recoveries<2U) {
            ++recoveries;SEGGER_RTT_WriteString(0,"[fsm] AT blocked; isolate and physically restart modem\n");
            enable_fsm=0;enable_mqtt=0;
            ABoxEc800At_AbortAll(&at,ABOX_EC800_RESULT_ERROR);
            GPIOC->BRR=(1U<<7);wait_ms(6000U);GPIOC->BSRR=(1U<<7);wait_ms(2000U);
            GPIOC->BSRR=(1U<<8);wait_ms(3000U);GPIOC->BRR=(1U<<8);
            rx_tail=(RXSIZE-DMA1_Channel5->CNDTR)%RXSIZE;
            PatrolPowerLedger_AfterModemReset();
            ABoxEc800At_OnModemReset(&at);
            enable_fsm=1;enable_mqtt=1;
            SEGGER_RTT_WriteString(0,"[fsm] physical reset notified; reload before action\n");
        }
    }
}
void Reset_Handler(void)
{
    uint32_t mode=MB[0];
    for (uint32_t *p=&__bss_start__;p<&__bss_end__;) *p++=0U;
    program_main(mode);
    for (;;) {}
}
