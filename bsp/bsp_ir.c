#include "bsp_ir.h"

void IR_PWM_Init(void)
{
    GPIO_InitType GPIO_InitStructure;
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    OCInitType TIM_OCInitStructure;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);
    GPIO_InitStructure.Pin            = GPIO_PIN_2;
    GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Current   = GPIO_DC_4mA;
    GPIO_InitStructure.GPIO_Alternate = GPIO_AF2_TIM2;
    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);

    /*
     * TIM2_CLK = 24MHz
     * 24MHz / (631 + 1) = 37.975kHz
     */
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 0;
    TIM_TimeBaseStructure.Period    = 631;
    TIM_TimeBaseStructure.ClkDiv    = 0;
    TIM_TimeBaseStructure.CntMode   = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM2, &TIM_TimeBaseStructure);

    TIM_InitOcStruct(&TIM_OCInitStructure);
    TIM_OCInitStructure.OcMode      = TIM_OCMODE_PWM1;
    TIM_OCInitStructure.OutputState = TIM_OUTPUT_STATE_DISABLE;
    TIM_OCInitStructure.Pulse       = 211;
    TIM_OCInitStructure.OcPolarity  = TIM_OC_POLARITY_HIGH;
    TIM_InitOc3(TIM2, &TIM_OCInitStructure);

    TIM_ConfigOc3Preload(TIM2, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigArPreload(TIM2, ENABLE);
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_DISABLE);

    /* 计数器运行，但CH3输出暂时关闭 */
    TIM_Enable(TIM2, ENABLE);
}

void IR_TIM6_Init(void)
{
    // 包络定时器
    
}


void IR_Init(void)
{
    // PWM定时器初始化
    IR_PWM_Init();

    // 控制PWM时间的定时器

}

void IR_Start(void)
{
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_ENABLE);
}

void IR_Stop(void)
{
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_DISABLE);
}