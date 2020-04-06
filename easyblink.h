/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-01-15     Sunwancn     the first version
 * 2020-04-01     Sunwancn     Version 2.0.0
 */

#ifndef __EASYBLINK_H__
#define __EASYBLINK_H__

#include <rtthread.h>
#include <board.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 可以使用MSH控制台手动控制LED闪烁 */
//#define PKG_EASYBLINK_USING_MSH_CMD
/* 使用MUTEX互锁，线程安全，一般情况下不需要使用 */
//#define PKG_EASYBLINK_USING_MUTEX
/* 使用系统的动态堆来创建线程栈 */
//#define PKG_EASYBLINK_USING_HEAP

/* 可控制的最大LED数目 */
#ifndef PKG_EASYBLINK_MAX_LED_NUMS
#define PKG_EASYBLINK_MAX_LED_NUMS          1
#endif

/* 线程栈相关配置 */
#define PKG_EASYBLINK_THREAD_PRIORITY       (RT_THREAD_PRIORITY_MAX - 2)
#define PKG_EASYBLINK_THREAD_STACK_SIZE     256
#define PKG_EASYBLINK_THREAD_TIMESLICE      (RT_TICK_PER_SECOND / 100)  /* 10ms */

/* 若在中断中使用 easyblink，并开了互锁，请把宏 PKG_EASYBLINK_WAIT_MUTEX_TICK 设为 0 吧，
 * 漏掉一次不闪就一次不闪吧，只是LED指示，无所谓的，还是中断的实时性重要。*/
#define PKG_EASYBLINK_WAIT_MUTEX_TICK       2U
#define PKG_EASYBLINK_WAIT_MAX_TICK         ((RT_TICK_MAX / 2) - 1)

/* 将一个LED闪烁系列从前台转向后台时，LED闪烁切换开始前的关断时间，毫秒 */
#define PKG_EASYBLINK_WAIT_MS_START         500U
/* 将一个LED闪烁系列从后台恢复时，LED闪烁切换开始前的关断时间，毫秒 */
#define PKG_EASYBLINK_WAIT_MS_END           5000U

#define PKG_EASYBLINK_INIT                  0x01
#define PKG_EASYBLINK_ACTIVE                0x02
#define PKG_EASYBLINK_CORRECT               0x04
#define PKG_EASYBLINK_LED_SHOULD_ON         0x08

#define __EASYBLINK_IS_FLAG(__LEDPTR__, __FLAG__) ((__LEDPTR__)->flag & (__FLAG__))
#define __EASYBLINK_SET_FLAG(__LEDPTR__, __FLAG__) ((__LEDPTR__)->flag |= (__FLAG__))
#define __EASYBLINK_CLEAR_FLAG(__LEDPTR__, __FLAG__) ((__LEDPTR__)->flag &= ~(__FLAG__))
#define __EASYBLINK_MS_TO_TICK(__MS__) ((__MS__) * RT_TICK_PER_SECOND / 1000U)

struct easyblink_data
{
    rt_base_t led_pin;
    rt_base_t active_level;
    rt_uint16_t flag;
    rt_int16_t nums;
    rt_int16_t nums_bak;
    rt_uint16_t pulse;
    rt_uint16_t pulse_bak;
    rt_uint16_t npulse;
    rt_uint16_t npulse_bak;
    rt_int32_t ticks;
};
typedef struct easyblink_data *ebled_t;

#ifdef SOC_FAMILY_STM32
extern ebled_t easyblink_init(GPIO_TypeDef *port, rt_uint16_t pin, GPIO_PinState active_level);
#endif
extern ebled_t easyblink_init_led(rt_base_t rt_pin, rt_base_t active_level);
extern void easyblink_deinit(ebled_t led);
extern void easyblink_stop(ebled_t led);
extern void easyblink(ebled_t led, rt_int16_t nums, rt_uint16_t pulse, rt_uint16_t period);
extern void eb_led_on(ebled_t led);
extern void eb_led_off(ebled_t led);
extern void eb_led_toggle(ebled_t led);

#ifdef __cplusplus
}
#endif

#endif /* __EASYBLINK_H__ */
