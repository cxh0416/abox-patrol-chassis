#ifndef PATROL_ENROLLMENT_H
#define PATROL_ENROLLMENT_H

#include "abox_ec800_at.h"

uint8_t PatrolEnrollment_Init(ABoxEc800At *at);
void PatrolEnrollment_Task(void);
void PatrolEnrollment_AfterModemReset(void);

#endif
