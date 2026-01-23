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

#include <rtthread.h>
#include <rtdevice.h>
#include "easyblink.h"

#if !defined(PKG_EASYBLINK_MAX_LED_NUMS) || (PKG_EASYBLINK_MAX_LED_NUMS == 0)
#error "Please define at least one PKG_EASYBLINK_MAX_LED_NUMS"
#endif

#ifndef PKG_EASYBLINK_USING_HEAP
ALIGN(RT_ALIGN_SIZE)
static char eb_thread_stack[PKG_EASYBLINK_THREAD_STACK_SIZE];
static struct rt_thread eb_thread_struct = {0};
static struct rt_semaphore eb_semaphore = {0};
#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
static struct rt_mutex eb_mutex_struct = {0};
#endif
#endif /* PKG_EASYBLINK_USING_HEAP */

static struct easyblink_data eb_leds[PKG_EASYBLINK_MAX_LED_NUMS] = {0};
static rt_thread_t eb_thread = RT_NULL;
static rt_sem_t eb_sem = RT_NULL;
#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
static rt_mutex_t eb_mutex = RT_NULL;
#endif

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
        if (!__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
        {
            __EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_INIT);

            led->led_pin = led_pin;
            led->active_level = active_level;

            rt_pin_mode(led_pin, PIN_MODE_OUTPUT);
            rt_pin_write(led_pin, !active_level);

            /* 若 easyBlink 守护线程未创建或已关闭，则创建 */
            if (eb_thread == RT_NULL || eb_thread->stat == RT_THREAD_CLOSE)
            {
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

#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
                eb_sem = rt_sem_create("eb_sem", 0, RT_IPC_FLAG_FIFO);
#else
                eb_sem = &eb_semaphore;
                rt_sem_init(eb_sem, "eb_sem", 0, RT_IPC_FLAG_FIFO);
#endif
                RT_ASSERT(eb_sem);

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
                eb_mutex = rt_mutex_create("eb_mutex", RT_IPC_FLAG_FIFO);
#else
                eb_mutex = &eb_mutex_struct;
                rt_mutex_init(eb_mutex, "eb_mutex", RT_IPC_FLAG_FIFO);
#endif
                RT_ASSERT(eb_mutex);
#endif /* defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX) */
            }

            break;
        }
    }

    return led;
}

#ifdef SOC_FAMILY_STM32
/***************************************************************************************************
 * @fn      easyblink_init
 *
 * @brief   STM32 初始化 LED
 *
 * @param   port         - GPIOx
 *          pin          - GPIO_PIN_0 - GPIO_PIN_15
 *          active_level - 点亮LED的有效电平，GPIO_PIN_RESET 或 GPIO_PIN_SET
 *
 * @return  ebled_t类型指针，若 init 次数超出最大LED数目，返回空指针
 ***************************************************************************************************/
ebled_t easyblink_init(GPIO_TypeDef *port, rt_uint16_t pin, GPIO_PinState active_level)
{
    rt_base_t lpin = 0;
    rt_base_t alevel;

    RT_ASSERT(pin > 0);

    do
    {
        if (pin & 1U)
            break;
        pin >>= 1;
        lpin++;
    }
    while(lpin < 16);

    lpin += (rt_base_t)(16 * (((rt_base_t)port - (rt_base_t)GPIOA_BASE) / (0x0400UL)));
    alevel = (rt_base_t)active_level;

    return easyblink_init_led(lpin, alevel);
}
#endif /* SOC_FAMILY_STM32 */

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
        if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
        {
            noled = RT_FALSE;
            break;
        }
    }

    if (noled)
    {
#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
        if (eb_mutex != RT_NULL)
        {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
            rt_mutex_delete(eb_mutex);
#else
            rt_mutex_detach(eb_mutex);
#endif
            eb_mutex = RT_NULL;
        }
#endif /* defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX) */
        if (eb_sem != RT_NULL)
        {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
            rt_sem_delete(eb_sem);
#else
            rt_sem_detach(eb_sem);
#endif
            eb_sem = RT_NULL;
        }
        if (eb_thread != RT_NULL)
        {
#if defined(RT_USING_HEAP) && defined(PKG_EASYBLINK_USING_HEAP)
            rt_thread_delete(eb_thread);
#else
            rt_thread_detach(eb_thread);
#endif
            eb_thread = RT_NULL;
        }
    }
}

/***************************************************************************************************
 * @fn      easyblink
 *
 * @brief   以给定的次数、脉宽和周期闪烁LED。
 *          若要在中断中使用 easyblink，并开了互锁，请把宏 PKG_EASYBLINK_WAIT_MUTEX_TICK 设为 0 吧，
 *          漏掉一次不闪就一次不闪吧，只是LED指示，无所谓的，还是中断的实时性重要。
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

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
    if (rt_mutex_take(eb_mutex, PKG_EASYBLINK_WAIT_MUTEX_TICK) == RT_EOK)
#endif
    {
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
        else if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE))
        {
            /* 原先的LED还在闪烁，参数压入后备区，等待闪烁 */
            led->nums_bak = nums;
            led->pulse_bak = __EASYBLINK_MS_TO_TICK(pulse);
            led->npulse_bak = __EASYBLINK_MS_TO_TICK(period - pulse);
        }
        else
        {
            /* 立刻开始闪烁 */
            blink_data_set(led, nums, pulse, period);
            led_blink_delay(led, 0);
        }

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
        rt_mutex_release(eb_mutex);
#endif
    }
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
    __EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_ACTIVE);
}

/* Seting the LED data */
void blink_data_set(ebled_t led, rt_int16_t nums, rt_uint16_t pulse, rt_uint16_t period)
{
    RT_ASSERT(led);

    led->nums = nums;
    led->pulse = __EASYBLINK_MS_TO_TICK(pulse);
    led->npulse = __EASYBLINK_MS_TO_TICK(period - pulse);
}

/* LED relaese the semaphore to blink or blink delay */
void led_blink_delay(ebled_t led, rt_uint16_t delay)
{
    RT_ASSERT(led);

    led->ticks = __EASYBLINK_MS_TO_TICK(delay);
    __EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON | PKG_EASYBLINK_ACTIVE | PKG_EASYBLINK_CORRECT);

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

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
        rt_mutex_take(eb_mutex, RT_WAITING_FOREVER);
#endif

        for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
        {
            led = &eb_leds[i];

            led->ticks -= wait_tick;
            if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE) && led->ticks <= 0)
            {
                if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON))
                {
                    /* 这个时间点应该 LED ON */
                    if ((led->nums > 0) || (led->nums == -1))
                    {
                        /* LED还有闪烁次数或无限次 */
                        eb_led_on(led);
                        /* 下一个时间点将关闭LED */
                        __EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON);
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
                                led->ticks = __EASYBLINK_MS_TO_TICK(PKG_EASYBLINK_WAIT_MS_END);
                            }
                        }
                        else
                        {
                            /* LED闪烁任务结束 */
                            __EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_ACTIVE);
                        }
                    }
                }
                else
                {
                    /* 这个时间点应该 LED OFF */
                    eb_led_off(led);
                    /* 下一个时间点将点亮LED */
                    __EASYBLINK_SET_FLAG(led, PKG_EASYBLINK_LED_SHOULD_ON);
                    led->ticks = led->npulse;
                    if (led->nums > 0)
                        led->nums --;
                }
            }
        }
#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
        rt_mutex_release(eb_mutex);
#endif
    }
}

/* 校正其余LED的ticks，并返回最小ticks，当corr为false时，只单纯取最小ticks */
rt_tick_t correct_or_get_min_ticks(rt_tick_t tick, rt_bool_t corr)
{
    int i;
    ebled_t led = RT_NULL;
    rt_int32_t tick_min = PKG_EASYBLINK_WAIT_MAX_TICK;

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
    rt_mutex_take(eb_mutex, RT_WAITING_FOREVER);
#endif

    if (corr)
        tick = rt_tick_get() - tick;

    for (i = 0; i < PKG_EASYBLINK_MAX_LED_NUMS; i++)
    {
        led = &eb_leds[i];
        if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_ACTIVE))
        {
            if (corr)
            {
                if (__EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_CORRECT))
                    __EASYBLINK_CLEAR_FLAG(led, PKG_EASYBLINK_CORRECT);
                else
                    led->ticks -= tick;
            }

            if (led->ticks < tick_min)
                tick_min = led->ticks;
        }
    }

#if defined(RT_USING_MUTEX) && defined(PKG_EASYBLINK_USING_MUTEX)
    rt_mutex_release(eb_mutex);
#endif

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
        if (! led || ! __EASYBLINK_IS_FLAG(led, PKG_EASYBLINK_INIT))
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
