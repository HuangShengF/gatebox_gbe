#ifndef __BSP_PIR_H
#define __BSP_PIR_H

#include "main.h"
#include <stdio.h>
#include <stdint.h>

#define PIR_CHANGED_LEFT     0x01U
#define PIR_CHANGED_RIGHT    0x02U
#define PIR_WAKEUP_QUIET_TIME_SEC  60U

#if (PIR_WAKEUP_QUIET_TIME_SEC == 0U)
#error "PIR_WAKEUP_QUIET_TIME_SEC must be greater than zero"
#endif



void PIR_ExtiInit(void);
void PIR_GetStates(uint8_t *left_state, uint8_t *right_state);
uint8_t PIR_TakeEvent(uint8_t *flags,uint8_t *left,uint8_t *right);
void PIR_RecordChangeFromISR(uint8_t flags);
void PIR_WakeupSuspend(void);
void PIR_WakeupResume(void);
void PIR_WakeupTimerFromISR(void);
#endif // __BSP_PIR_H__
