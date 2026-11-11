#ifndef PATROL_PATROL_POWER_LEDGER_FSM_H
#define PATROL_PATROL_POWER_LEDGER_FSM_H
#include <stddef.h>
#include <stdint.h>
#include "abox_ufs_journal.h"
#include "abox_clock.h"

/* Product power-on ledger, backed by the public UFS journal. */
#define PATROL_POWER_SLOTS 4U
#define PATROL_POWER_WORKSPACE_SIZE 522U
typedef struct __attribute__((packed, aligned(4))) {
    uint64_t expires, protect_mono, response_time;
    char id[65];
    uint8_t state, outcome, power_on;
    char device[32];
} PatrolPowerLedgerEntry;
typedef struct {
    void *context;
    int (*marker_read)(void *); /* 0 virgin, 1 used, -1 corrupt */
    int (*marker_write)(void *); /* durable and verified */
    int (*acquire)(void *);
    void (*release)(void *);
    int (*admit)(void *);
    int (*act_on)(void *, uint8_t *power_on); /* 1 OK, 0 unavailable, -1 failed, -2 cancelled */
    void (*safe_stop)(void *);
    uint8_t (*power_state)(void *);
    uint64_t (*utc_now)(void *); /* zero if time unavailable */
} PatrolPowerLedgerPort;
typedef struct {
    ABoxUfsJournal journal;
    PatrolPowerLedgerPort port;
    PatrolPowerLedgerEntry entry[PATROL_POWER_SLOTS];
    uint64_t now_mono, final_wait_started;
    uint32_t stop_epoch, admitted_epoch;
    uint8_t phase, active, ready, cancelled, failure_pending;
    uint16_t failure_code;
} PatrolPowerLedgerFsm;
enum { PATROL_POWER_NEW=0, PATROL_POWER_WAIT, PATROL_POWER_REPLAY, PATROL_POWER_CONFLICT, PATROL_POWER_EXPIRED,
       PATROL_POWER_TIME_UNAVAILABLE, PATROL_POWER_BUSY, PATROL_POWER_FULL, PATROL_POWER_FAILED, PATROL_POWER_INVALID };
int PatrolPowerLedgerFsm_Init(PatrolPowerLedgerFsm *, const ABoxEc800UfsPort *, const PatrolPowerLedgerPort *, const char *, const char *, uint8_t *, size_t);
int PatrolPowerLedgerFsm_Load(PatrolPowerLedgerFsm *, uint64_t mono);
int PatrolPowerLedgerFsm_Busy(const PatrolPowerLedgerFsm *);
void PatrolPowerLedgerFsm_Poll(PatrolPowerLedgerFsm *, const ABoxClockSnapshot *);
/* Caller validates V4 envelope/command/params, raw lifetime and identity first.
 * same_power_on is false for any other valid logical operation with this ID. */
int PatrolPowerLedgerFsm_Request(PatrolPowerLedgerFsm *, const char *device, const char *id, int same_power_on, uint64_t expires,
                     const ABoxClockSnapshot *, size_t *slot);
void PatrolPowerLedgerFsm_SafeStop(PatrolPowerLedgerFsm *);
int PatrolPowerLedgerFsm_Response(PatrolPowerLedgerFsm *, size_t slot, char *output, size_t size);
/* The caller copies device and request ID before consuming an asynchronous
 * rejection; no action may be inferred from an uncommitted response. */
int PatrolPowerLedgerFsm_TakeFailure(PatrolPowerLedgerFsm *, char *device, size_t device_cap,
                         char *request_id, size_t id_cap, uint16_t *code);
#endif
