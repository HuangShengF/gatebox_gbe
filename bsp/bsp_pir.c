#include "bsp_pir.h"
#include "usb_pwr.h"

#define PIR_PORT              GPIOA
/* 左右引脚映射需要结合原理图或实机方向确认 */
#define PIR_LEFT_PIN          GPIO_PIN_3
#define PIR_RIGHT_PIN         GPIO_PIN_7
#define PIR_PIN               (PIR_LEFT_PIN | PIR_RIGHT_PIN)
#define PIR_PORT_SOURCE       GPIOA_PORT_SOURCE
#define PIR_WAKEUP_TIMER_HZ   1000U

typedef enum
{
    PIR_WAKEUP_DISABLED = 0,
    PIR_WAKEUP_GUARD,
    PIR_WAKEUP_WAIT_LOW,
    PIR_WAKEUP_ARMED
} PIR_WakeupState;


static volatile uint8_t g_pir_changed_flags = 0U;
static volatile uint8_t g_pir_left_snapshot = 0U;
static volatile uint8_t g_pir_right_snapshot = 0U;
static volatile PIR_WakeupState g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
static uint8_t g_pir_wakeup_initialized = 0U;

static void PIR_WakeupTimerInit(void)
{
    RCC_ClocksType clocks;
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    NVIC_InitType NVIC_InitStructure;
    uint32_t timer_clock;
    uint32_t prescaler_div;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM4, ENABLE);

    RCC_GetClocksFreqValue(&clocks);
    if (clocks.Pclk1Freq == clocks.HclkFreq)
    {
        timer_clock = clocks.Pclk1Freq;
    }
    else
    {
        timer_clock = clocks.Pclk1Freq * 2U;
    }
    prescaler_div = timer_clock / PIR_WAKEUP_TIMER_HZ;

    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = (uint16_t)(prescaler_div - 1U);
    TIM_TimeBaseStructure.Period = (uint16_t)(PIR_WAKEUP_GUARD_TIME_MS - 1U);
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM4, &TIM_TimeBaseStructure);
    TIM_SelectOnePulseMode(TIM4, TIM_OPMODE_SINGLE);
    TIM_SetCnt(TIM4, 0U);
    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    TIM_ConfigInt(TIM4, TIM_INT_UPDATE, ENABLE);
    TIM_Enable(TIM4, DISABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    g_pir_wakeup_initialized = 1U;
}

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

    PIR_WakeupTimerInit();

    if (bDeviceState == SUSPENDED)
    {
        PIR_WakeupSuspend();
    }
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

    if (bDeviceState != SUSPENDED)
    {
        g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
        return;
    }

    if (g_pir_wakeup_state == PIR_WAKEUP_WAIT_LOW)
    {
        if ((g_pir_left_snapshot == 0U) && (g_pir_right_snapshot == 0U))
        {
            g_pir_wakeup_state = PIR_WAKEUP_ARMED;
        }
        return;
    }

    if (g_pir_wakeup_state == PIR_WAKEUP_ARMED)
    {
        if ((((flags & PIR_CHANGED_LEFT) != 0U) && (g_pir_left_snapshot != 0U))
            || (((flags & PIR_CHANGED_RIGHT) != 0U) && (g_pir_right_snapshot != 0U)))
        {
            g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
            USB_Remote_Wakeup();
        }
    }
}

void PIR_WakeupSuspend(void)
{
    if (g_pir_wakeup_initialized == 0U)
    {
        return;
    }

    g_pir_wakeup_state = PIR_WAKEUP_GUARD;
    TIM_Enable(TIM4, DISABLE);
    TIM_SetCnt(TIM4, 0U);
    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    TIM_Enable(TIM4, ENABLE);
}

void PIR_WakeupResume(void)
{
    g_pir_wakeup_state = PIR_WAKEUP_DISABLED;

    if (g_pir_wakeup_initialized != 0U)
    {
        TIM_Enable(TIM4, DISABLE);
        TIM_SetCnt(TIM4, 0U);
        TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    }
}

void PIR_WakeupTimerFromISR(void)
{
    if (TIM_GetIntStatus(TIM4, TIM_INT_UPDATE) == RESET)
    {
        return;
    }

    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    TIM_Enable(TIM4, DISABLE);

    if ((bDeviceState != SUSPENDED)
        || (g_pir_wakeup_state != PIR_WAKEUP_GUARD))
    {
        g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
        return;
    }

    g_pir_left_snapshot = (uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);
    g_pir_right_snapshot = (uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);

    if ((g_pir_left_snapshot == 0U) && (g_pir_right_snapshot == 0U))
    {
        g_pir_wakeup_state = PIR_WAKEUP_ARMED;
    }
    else
    {
        g_pir_wakeup_state = PIR_WAKEUP_WAIT_LOW;
    }
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
