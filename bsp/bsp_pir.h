#ifndef __BSP_PIR_H
#define __BSP_PIR_H

#include "main.h"
#include <stdio.h>
#include <stdint.h>

#define PIR_CHANGED_LEFT     0x01U
#define PIR_CHANGED_RIGHT    0x02U



void PIR_ExtiInit(void);
void PIR_GetStates(uint8_t *left_state, uint8_t *right_state);
uint8_t PIR_TakeEvent(uint8_t *flags,uint8_t *left,uint8_t *right);
void PIR_RecordChangeFromISR(uint8_t flags);
#endif // __BSP_PIR_H__
