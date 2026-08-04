#include "bsp_pir.h"

#define PIR_PORT              GPIOA
#define PIR_PIN               (GPIO_PIN_3 | GPIO_PIN_7)
#define PIR_PORT_SOURCE       GPIOA_PORT_SOURCE

void PIR_ExtiInit(void)
{
    GPIO_InitType GPIO_InitStructure;
    EXTI_InitType EXTI_InitStructure;
    NVIC_InitType NVIC_InitStructure;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);
    GPIO_InitStructure.Pin       = PIR_PIN;
    GPIO_InitStructure.GPIO_Pull = GPIO_Pull_Down;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Input;
    GPIO_InitPeripheral(PIR_PORT, &GPIO_InitStructure);

    GPIO_ConfigEXTILine(PIR_PORT_SOURCE, GPIO_PIN_SOURCE3);
    GPIO_ConfigEXTILine(PIR_PORT_SOURCE, GPIO_PIN_SOURCE7);

    EXTI_ClrITPendBit(EXTI_LINE3 | EXTI_LINE7);
    EXTI_InitStructure.EXTI_Line    = EXTI_LINE3 | EXTI_LINE7;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x05;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x0F;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_Init(&NVIC_InitStructure);
}