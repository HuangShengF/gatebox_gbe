#ifndef __BSP_PIR_H
#define __BSP_PIR_H

#include "main.h"
#include <stdio.h>
#include <stdint.h>

void PIR_ExtiInit(void);
void PIR_GetStates(uint8_t *left_state, uint8_t *right_state);

#endif // __BSP_PIR_H__
