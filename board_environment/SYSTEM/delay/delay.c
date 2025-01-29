#include "delay.h"
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////
//如果使用ucos,则包括下面的头文件即可.
#include "FreeRTOS.h" //ucos 使用

//----------------------------------ADD-------------------------------------
#include "task.h"

//////////////////////////////////////////////////////////////////////////////////
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
// ALIENTEK STM32H7开发板
//使用SysTick的普通计数模式对延迟进行管理(支持ucosii)
//包括delay_us,delay_ms
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2017/6/8
//版本：V1.0
//版权所有，盗版必究。
// Copyright(C) 广州市星翼电子科技有限公司 2014-2024
// All rights reserved
//********************************************************************************
//修改说明
//////////////////////////////////////////////////////////////////////////////////

static u32 fac_us = 0; // us延时倍乘数
static u16 fac_ms = 0; // ms延时倍乘数,在os下,代表每个节拍的ms数

//-------------------------------------ADD---------------------------------------------
typedef BaseType_t (*TaskHookFunction_t)(void *);
extern void xPortSysTickHandler(void);
extern BaseType_t xTaskGetSchedulerState(void);
//#define taskSCHEDULER_NOT_STARTED ((BaseType_t)1)

void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) //??????
    {
        xPortSysTickHandler();
    }
}

void delay_init(u16 SYSCLK)
{
    u32 reload;
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    fac_us = SYSCLK;
    reload = SYSCLK;
    reload *= 1000000 / configTICK_RATE_HZ;    //?? configTICK_RATE_HZ ??????
    fac_ms = 1000 / configTICK_RATE_HZ;        //?? OS ?????????
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk; //?? SYSTICK ??
    SysTick->LOAD = reload;                    //? 1/configTICK_RATE_HZ ???
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;  //?? SYSTICK
}

void delay_us(u32 nus)
{
    u32 ticks;
    u32 told, tnow, tcnt = 0;
    u32 reload = SysTick->LOAD; // LOAD ??
    ticks = nus * fac_us;       //??????
    told = SysTick->VAL;        //?????????
    while (1) {
        tnow = SysTick->VAL;
        if (tnow != told) {
            //?????? SYSTICK ?????????????.
            if (tnow < told)
                tcnt += told - tnow;
            else
                tcnt += reload - tnow + told;
            told = tnow;
            if (tcnt >= ticks)
                break; //????/????????,???.
        }
    };
}

void delay_ms(u32 nms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) //??????
    {
        if (nms >= fac_ms) //??????? OS ???????
        {
            vTaskDelay(nms / fac_ms); // FreeRTOS ??
        }
        nms %= fac_ms; // OS ?????????????,
    }
    delay_us((u32)(nms * 1000)); //??????
}

void delay_xms(u32 nms)
{
    u32 i;
    for (i = 0; i < nms; i++)
        delay_us(1000);
}
