#ifndef __BSP_PIR_H
#define __BSP_PIR_H

#include "main.h"
#include <stdio.h>
#include <stdint.h>

#define PIR_CHANGED_LEFT     0x01U
#define PIR_CHANGED_RIGHT    0x02U
#define PIR_WAKEUP_QUIET_TIME_SEC  10U
#define PIR_WAKEUP_HIGH_TIME_MS    1000U

#if (PIR_WAKEUP_QUIET_TIME_SEC == 0U)
#error "PIR_WAKEUP_QUIET_TIME_SEC must be greater than zero"
#endif



void PIR_ExtiInit(void);
void PIR_GetStates(uint8_t *left_state, uint8_t *right_state);
uint8_t PIR_TakeEvent(uint8_t *flags,uint8_t *left,uint8_t *right);
void PIR_RecordChangeFromISR(uint8_t flags);
/* 仅控制USB唤醒，不影响状态查询和运动通知；返回1表示成功。 */
uint8_t PIR_SetWakeupEnabled(uint8_t enabled);
uint8_t PIR_GetWakeupEnabled(uint8_t *enabled);
void PIR_WakeupSuspend(void);
/* WKUP/总线复位只停止检测，收到主机SOF后才解除单次唤醒锁存。 */
void PIR_WakeupResume(void);
void PIR_WakeupOnSof(void);
void PIR_WakeupTimerFromISR(void);
#endif // __BSP_PIR_H__
