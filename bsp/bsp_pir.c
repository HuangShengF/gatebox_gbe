#include "bsp_pir.h"

#define PIR_PORT              GPIOA
/* 左右引脚映射需要结合原理图或实机方向确认 */
#define PIR_LEFT_PIN          GPIO_PIN_3
#define PIR_RIGHT_PIN         GPIO_PIN_7
#define PIR_PIN               (PIR_LEFT_PIN | PIR_RIGHT_PIN)
#define PIR_PORT_SOURCE       GPIOA_PORT_SOURCE


static volatile uint8_t g_pir_changed_flags = 0U;
static volatile uint8_t g_pir_left_snapshot = 0U;
static volatile uint8_t g_pir_right_snapshot = 0U;
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
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 双边沿触发
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

void PIR_GetStates(uint8_t *left_state, uint8_t *right_state)
{
 
    if ((left_state == NULL) || (right_state == NULL))
    {
        return;
    }

    *left_state = GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);
    *right_state = GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);
}

void PIR_RecordChangeFromISR(uint8_t flags)
{
    g_pir_left_snapshot =(uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);

    g_pir_right_snapshot =(uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);

    g_pir_changed_flags |= flags;
}
uint8_t PIR_TakeEvent(uint8_t *flags,uint8_t *left,uint8_t *right)
{
    if(flags == NULL || left == NULL || right == NULL)
    return 0;

    if(g_pir_changed_flags == 0U)
    return 0;

    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    *flags = g_pir_changed_flags;
    *left = g_pir_left_snapshot;
    *right = g_pir_right_snapshot;

    g_pir_changed_flags = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}