/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-01-15     Sunwancn     the first version
 * 2020-04-01     Sunwancn     Version 2.0.0
 * 2026-01-25     Sunwancn     Version 2.1.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "easyblink.h"

#ifdef PKG_USING_EASYBLINK

#if !defined(PKG_EASYBLINK_MAX_LED_NUMS) || (PKG_EASYBLINK_MAX_LED_NUMS == 0)
#error "Please define at least one PKG_EASYBLINK_MAX_LED_NUMS"
#endif

#ifndef PKG_EASYBLINK_USING_HEAP
#ifndef ALIGN
#define ALIGN   rt_align
#endif
ALIGN(RT_ALIGN_SIZE)
static char eb_thread_stack[PKG_EASYBLINK_THREAD_STACK_SIZE];
static struct rt_thread eb_thread_struct = {0};
static struct rt_semaphore eb_semaphore = {0};
#endif /* ! PKG_EASYBLINK_USING_HEAP */

static struct easyblink_data eb_leds[PKG_EASYBLINK_MAX_LED_NUMS] = {0};
static rt_thread_t eb_thread = RT_NULL;
static rt_sem_t eb_sem = RT_NULL;

static void led_blink_delay(ebled_t led, rt_uint16_t delay);
static void blink_data_set(ebled_t led, rt_int16_t nums, rt_uint16_t pulse, rt_uint16_t period);
static void eb_daemon_thread_entry(void *parameter);
static rt_tick_t correct_or_get_min_ticks(rt_tick_t tick, rt_bool_t corr);

/***************************************************************************************************
 * @fn      easyblink_init_led
 *
 * @brief   初始化 LED
 *
 * @param   led_pin      - LED 驱动引脚编号，查看 PIN 驱动代码 drv_gpio.c 文件确认引脚编号，
 *                         对 STM32，可以使用GET_PIN()宏，如 GET_PIN(F, 9) 为 PF9
 *          active_level - 点亮LED的有效电平，PIN_LOW 或 PIN_HIGH
 *
 * @return  ebled_t类型指针，若 init 次数超出最大LED数目，返回空指针
 ***************************************************************************************************/
ebled_t easyblink_init_led(rt_base_t led_pin, rt_base_t active_level)
{
    int i;
    ebled_t led = RT_NULL;

    for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
    {
        led = &eb_leds[i];
        if (!EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
        {
            EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_INIT);

            led->led_pin = led_pin;
            led->active_level = active_level;

            rt_pin_mode(led_pin, PIN_MODE_OUTPUT);
            rt_pin_write(led_pin, !active_level);

            /* 若 easyBlink 守护线程未创建，则创建 */
            if (eb_thread == RT_NULL)
            {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
                eb_sem = rt_sem_create("eb_sem", 0, RT_IPC_FLAG_FIFO);
#else
                eb_sem = &eb_semaphore;
                rt_sem_init(eb_sem, "eb_sem", 0, RT_IPC_FLAG_FIFO);
#endif
                RT_ASSERT(eb_sem);

#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
                eb_thread = rt_thread_create("ebThread", eb_daemon_thread_entry, RT_NULL,
                                             PKG_EASYBLINK_THREAD_STACK_SIZE, PKG_EASYBLINK_THREAD_PRIORITY, PKG_EASYBLINK_THREAD_TIMESLICE);
#else
                eb_thread = &eb_thread_struct;
                rt_thread_init(eb_thread, "ebThread", eb_daemon_thread_entry, RT_NULL,
                               &eb_thread_stack, sizeof(eb_thread_stack), PKG_EASYBLINK_THREAD_PRIORITY, PKG_EASYBLINK_THREAD_TIMESLICE);
#endif
                RT_ASSERT(eb_thread);
                rt_thread_startup(eb_thread);
            }

            break;
        }
    }

    if (i == PKG_EASYBLINK_MAX_LED_NUMS)
    {
        return RT_NULL;
    }

    return led;
}

/***************************************************************************************************
 * @fn      easyblink_deinit
 *
 * @brief   去初始化
 *
 * @param   led         - LED句柄
 *
 ***************************************************************************************************/
void easyblink_deinit(ebled_t led)
{
    int i;
    rt_bool_t noled = RT_TRUE;

    RT_ASSERT(led);

    rt_pin_mode(led->led_pin, PIN_MODE_INPUT);

    rt_memset(led, 0x0, sizeof(struct easyblink_data));

    for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
    {
        led = &eb_leds[i];
        if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
        {
            noled = RT_FALSE;
            break;
        }
    }

    if (noled)
    {
        if (eb_thread != RT_NULL)
        {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
            rt_thread_delete(eb_thread);
#else
            rt_thread_detach(eb_thread);
#endif
            eb_thread = RT_NULL;
        }
        if (eb_sem != RT_NULL)
        {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
            rt_sem_delete(eb_sem);
#else
            rt_sem_detach(eb_sem);
#endif
            eb_sem = RT_NULL;
        }
    }
}

/***************************************************************************************************
 * @fn      easyblink
 *
 * @brief   以给定的次数、脉宽和周期闪烁LED。
 *
 * @param   led         - LED句柄
 *          nums        - LED闪烁次数，-1为无限次
 *          pulse       - LED闪亮的脉冲宽度，以毫秒为单位
 *          period      - LED闪烁的周期，以毫秒为单位
 *
 * @return  None
 ***************************************************************************************************/
void easyblink(ebled_t led, rt_int16_t nums, rt_uint16_t pulse, rt_uint16_t period)
{
    RT_ASSERT(led);
    RT_ASSERT(pulse <= period);

    rt_base_t level = rt_hw_interrupt_disable();

    if (led->nums == -1 && nums > 0)
    {
        /* 原先若是无限次闪烁，原先的LED闪烁参数压入后备区 */
        led->nums_bak = led->nums;
        led->pulse_bak = led->pulse;
        led->npulse_bak = led->npulse;

        eb_led_off(led);

        blink_data_set(led, nums, pulse, period);
        led_blink_delay(led, PKG_EASYBLINK_WAIT_MS_START);
    }
    else if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE))
    {
        /* 原先的LED还在闪烁，参数压入后备区，等待闪烁 */
        led->nums_bak = nums;
        led->pulse_bak = EASYBLINK_MS_TO_TICK(pulse);
        led->npulse_bak = EASYBLINK_MS_TO_TICK(period - pulse);
    }
    else
    {
        /* 立刻开始闪烁 */
        blink_data_set(led, nums, pulse, period);
        led_blink_delay(led, 0);
    }

    rt_hw_interrupt_enable(level);
}

/***************************************************************************************************
 * @fn      easyblink_stop
 *
 * @brief   主动停止LED闪烁
 *
 * @param   led         - 要停止的LED句柄
 *
 * @return  None
 ***************************************************************************************************/
void easyblink_stop(ebled_t led)
{
    RT_ASSERT(led);

    eb_led_off(led);

    led->nums_bak = led->pulse_bak = led->npulse_bak = 0;
    EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_ACTIVE);
}

/* Set the LED data */
void blink_data_set(ebled_t led, rt_int16_t nums, rt_uint16_t pulse, rt_uint16_t period)
{
    RT_ASSERT(led);

    led->nums = nums;
    led->pulse = EASYBLINK_MS_TO_TICK(pulse);
    led->npulse = EASYBLINK_MS_TO_TICK(period - pulse);
}

/* LED relaese the semaphore to blink or blink delay */
void led_blink_delay(ebled_t led, rt_uint16_t delay)
{
    RT_ASSERT(led);

    led->ticks = EASYBLINK_MS_TO_TICK(delay);
    EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON | PKG_EASYBLINK_ACTIVE | PKG_EASYBLINK_CORRECT);

    rt_sem_release(eb_sem);
}

/* LED on */
void eb_led_on(ebled_t led)
{
    RT_ASSERT(led);

    rt_pin_write(led->led_pin, led->active_level);
}

/* LED off */
void eb_led_off(ebled_t led)
{
    RT_ASSERT(led);

    if (led->active_level == PIN_HIGH)
    {
        rt_pin_write(led->led_pin, PIN_LOW);
    }
    else
    {
        rt_pin_write(led->led_pin, PIN_HIGH);
    }
}

/* LED toggle */
void eb_led_toggle(ebled_t led)
{
    int level;

    RT_ASSERT(led);

    level = rt_pin_read(led->led_pin);
    if (level == PIN_HIGH)
    {
        rt_pin_write(led->led_pin, PIN_LOW);
    }
    else
    {
        rt_pin_write(led->led_pin, PIN_HIGH);
    }
}

/* LED守护线程入口函数 */
void eb_daemon_thread_entry(void *parameter)
{
    int i;
    ebled_t led = RT_NULL;
    rt_tick_t wait_tick, tick;

    while (1)
    {
        /* 取得LED序列中运行状态将发生改变的最小ticks */
        wait_tick = correct_or_get_min_ticks(0, RT_FALSE);

        /* 若wait_tick非零，在此阻塞线程wait_tick，或通过信号量同步后，开始准备闪烁 */
        while (wait_tick)
        {
            tick = rt_tick_get();
            /* 线程等待 wait_tick 个 os tick */
            if (rt_sem_take(eb_sem, wait_tick) != RT_EOK)
                break;
            /* 接收到信号量，校正其它LED的tick数，并取最小ticks */
            wait_tick = correct_or_get_min_ticks(tick, RT_TRUE);
        }

        for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
        {
            led = &eb_leds[i];

            rt_enter_critical();

            led->ticks -= wait_tick;
            if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE) && led->ticks <= 0)
            {
                if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON))
                {
                    /* 这个时间点应该 LED ON */
                    if ((led->nums > 0) || (led->nums == -1))
                    {
                        /* LED还有闪烁次数或无限次 */
                        eb_led_on(led);
                        /* 下一个时间点将关闭LED */
                        EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON);
                        led->ticks = led->pulse;
                    }
                    else
                    {
                        /* LED闪烁次数已用尽 */
                        if (led->pulse_bak)
                        {
                            /* 有在后备区的LED，提取压入的LED闪烁参数 */
                            led->nums = led->nums_bak;
                            led->pulse = led->pulse_bak;
                            led->npulse = led->npulse_bak;
                            led->nums_bak = led->pulse_bak = led->npulse_bak = 0;
                            led->ticks = 0;
                            if (led->nums == -1)
                            {
                                led->ticks = EASYBLINK_MS_TO_TICK(PKG_EASYBLINK_WAIT_MS_END);
                            }
                        }
                        else
                        {
                            /* LED闪烁任务结束 */
                            EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_ACTIVE);
                        }
                    }
                }
                else
                {
                    /* 这个时间点应该 LED OFF */
                    eb_led_off(led);
                    /* 下一个时间点将点亮LED */
                    EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON);
                    led->ticks = led->npulse;
                    if (led->nums > 0)
                        led->nums --;
                }
            }

            rt_exit_critical();
        }
    }
}

/* 校正其余LED的ticks，并返回最小ticks，当corr为false时，只单纯取最小ticks */
rt_tick_t correct_or_get_min_ticks(rt_tick_t tick, rt_bool_t corr)
{
    int i;
    ebled_t led = RT_NULL;
    rt_int32_t tick_min = PKG_EASYBLINK_WAIT_MAX_TICK;

    if (corr)
        tick = rt_tick_get() - tick;

    for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
    {
        led = &eb_leds[i];

        if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE))
        {
            rt_enter_critical();

            if (corr)
            {
                if (EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_CORRECT))
                    EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_CORRECT);
                else
                    led->ticks -= tick;
            }

            if (led->ticks < tick_min)
                tick_min = led->ticks;

            rt_exit_critical();
        }
    }

    return (rt_tick_t)((tick_min < 0) ? 0 : tick_min);
}

#if defined(RT_USING_FINSH) && defined(PKG_EASYBLINK_USING_MSH_CMD)
#include <stdlib.h>

static void __easyblink(rt_uint8_t argc, char **argv)
{
    int init_num, nums, pulse, period;
    ebled_t led = RT_NULL;

    if (argc <= 2)
    {
        rt_kprintf("Please input: eblink <init_num> <nums> [period] [pulse]\n");
    }
    else
    {
        init_num = atoi(argv[1]) - 1;
        if (init_num < 0 || init_num > PKG_EASYBLINK_MAX_LED_NUMS - 1)
        {
            rt_kprintf("Out of range! Must be at: init_num[1-%d]\n", PKG_EASYBLINK_MAX_LED_NUMS);
            return;
        }
        led = &eb_leds[init_num];
        if (! led || ! EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
        {
            rt_kprintf("Not initialized! Must be initialize first.\n");
            return;
        }

        if (argc == 3)
        {
            nums = atoi(argv[2]);
            if (nums > 0)
                easyblink(led, nums, 500, 1000);
            else
                rt_kprintf("Out of range! Must be at: nums[1-any]\n");
        }
        else if (argc == 4)
        {
            nums = atoi(argv[2]);
            period = atoi(argv[3]);
            if (nums > 0 && period >= 10)
                easyblink(led, nums, period >> 1, period);
            else
                rt_kprintf("Out of range! Must be at: nums[1-any] period[10-any]\n");
        }
        else
        {
            nums = atoi(argv[2]);
            period = atoi(argv[3]);
            pulse = atoi(argv[4]);
            if (nums > 0 && period >= 10 && pulse >= 10 && pulse <= period)
                easyblink(led, nums, pulse, period);
            else
                rt_kprintf("Out of range! Must be at: nums[1-any] period[10-any] pulse[10-%d]\n", period);
        }
    }
}
MSH_CMD_EXPORT_ALIAS(__easyblink, eblink, Blink the LED easily);
#endif /* defined(RT_USING_FINSH) && defined(PKG_EASYBLINK_USING_MSH_CMD) */

#endif /* PKG_USING_EASYBLINK */

