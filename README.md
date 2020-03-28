# easyblink

## 简介
小巧轻便的LED控制软件包，可以容易地控制LED开、关、反转和各种间隔闪烁，占用RAM少，可以设置成线程安全型的；不需要移植就可以同时适用 RT-Thread 标准版和 Nano 版。  

## 特点
和其它LED同类软件相比，easyblink 有一个显著的特点，占用 RAM 特别少，其它 LED 软件一般每一个LED都需要创建一个线程，LED一多，线程数就多了，所占用的栈空间就相应的增大。而 easyblink 始终只使用一个守护线程（线程栈可以是预先分配的静态栈空间），无论多少个 LED，就一个线程。另外，不需要移植就可以同时适用 RT-Thread 标准版和 Nano 版，特别适合 RAM 紧张的产品。同时，也可以设置成线程安全型的。

## 获取软件包

使用 easyblink 软件包需要在 ENV 环境的包管理中选中它，若没有找到，先使用 `pkgs --upgrade` ，升级本地的包列表，运行 menuconfig 后具体路径如下：

```
RT-Thread online packages
    peripheral libraries and drivers  --->
        [*] easyblink: Blink the LED easily and use a little RAM  --->
```

```
        --- easyblink: Blink the LED easily and use a little RAM
        (3)   Setting the max LED nums
        [ ]   Blink led on console to test
        [ ]   Use mutex to make thread safe
        [ ]   Use heap with the easyblink thread stack created
              Version (latest)  --->
```

 **Setting the max LED nums：**   
最多可使用的LED数目。  

 **Blink led on console to test：**   
可以在控制台控制LED闪烁，进行测试。  

 **Use mutex to make thread safe：**   
使用互锁，成为线程安全型的应用。  

 **Use heap with the easyblink daemon thread stack created：**   
LED守护线程栈使用系统动态堆内存。  

## API 简介

`ebled_t easyblink_init(GPIO_TypeDef *port, rt_uint16_t pin, GPIO_PinState active_level)`

easyblink 的初始化函数，必须的。  
 **port：**  为LED驱动引脚端口，GPIOx，x为 A、B、C、D 等等。  
 **pin：**  引脚端口号，GPIO_PIN_0 - GPIO_PIN_15。  
 **active_level：**  点亮LED时的端口电平，为 GPIO_PIN_RESET 或 GPIO_PIN_SET。  
函数返回 ebled_t 类型指针，init 次数超过最大LED数目，返回空指针。

```
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
```

 **led：**  easyblink_init 后得到的 LED 句柄。  
 **nums：**  闪烁次数，-1 为一直闪烁。  
 **pulse：**  LED 闪烁时点亮LED的脉冲宽度，以毫秒为单位。  
 **period：**  LED 闪烁周期，亮和暗即一个完整的周期。  
 
 当前LED正在闪烁时，如果这时LED再调用一次 easyblink()，若当前LED的nums为-1（即一直闪烁），则会打断当前LED的闪烁，先关闭LED 500ms，然后使用新的参数进行闪烁，闪烁好了后，会再关闭LED 5000ms，再恢复原来的LED参数继续闪烁，关断LED的前后时间可以在 easyblink.h 里调整设置。否则，会保存参数在后备区，等当前的闪烁完了后，接着以新的参数闪烁。  
 因为只有一个后备区，因此当后备区有待闪烁的参数时，再次调用 easyblink()，原来后备区的参数将丢失，代之于新的参数。

`void easyblink_stop(ebled_t led)`

主动停止 LED 闪烁。

`void eb_led_on(ebled_t led)`

点亮 LED 。

`void eb_led_off(ebled_t led)`

关闭 LED 。

`void eb_led_toggle(ebled_t led)`

翻转 LED 。  


Nano 版直接拷贝 easyblink.h 和 easyblink.c 到用户文件夹，然后要确保开启宏 RT_USING_SEMAPHORE，再自己配置几个宏定义：  

PKG_EASYBLINK_MAX_LED_NUMS：定义 LED 数目。  
PKG_EASYBLINK_USING_MSH_CMD：定义可以在控制台使用 eblink 进行LED的闪烁测试。  
PKG_EASYBLINK_USING_MUTEX：使用互锁，成为线程安全型应用，确保开启 RT_USING_MUTEX 。对只有少量的LED，闪烁频次不是很高，并且都是同一个线程控制的话，觉得不是非常必要。  
PKG_EASYBLINK_USING_HEAP：LED的守护线程栈使用系统的动态堆内存，确保开启 RT_USING_HEAP，否则，在编译时自动分配好内存。

## 使用示例

```
#include "easyblink.h"
  ...

ebled_t led1 = RT_NULL;
ebled_t led2 = RT_NULL;

int main(void)
{
  ...
    /* 初始化LED1，引脚B口5脚，低电平有效 */
    led1 = easyblink_init(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    /* 初始化LED2，引脚B口3脚，低电平有效 */
    led2 = easyblink_init(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);


    /* led1 闪3次，周期1000ms，亮500ms */
    easyblink(led1, 3, 500, 1000);
    /* led1 上次的闪完后，再接着闪2次，周期2000ms，亮1500ms */
    easyblink(led1, 2, 1500, 2000);
    /* led2 一直闪，周期1000ms，亮5ms */
    easyblink(led2, -1, 5, 1000);

    rt_thread_mdelay(8000);

    /* 打断led2闪烁，开始闪5次，周期500ms，亮300ms，闪完后恢复led2前次的序列 */
    easyblink(led2, 5, 300, 500);

    easyblink(led1, -1, 10, 5000);

    rt_thread_mdelay(8000);

    /* 中途打断，停止闪烁 */
    easyblink_stop(led2);

    rt_thread_mdelay(5000);

    while(1)
    {
        eb_led_toggle(led2);
        rt_thread_mdelay(3000);
    }
}
```

在 Msh 控制台，可以使用 easyblink 命令闪烁LED：

`eblink <init_num> <nums> [period] [pulse]`

前2个参数必选，后2个可以省略，可以输入 2、3、或 4 个参数，pulse默认为period的一半，period默认 1000ms 。
init_num：从 1 开始的，按 init 初始化顺序排序的 LED号。

## 最后
如果这个软件包对你有用的话，请点个赞！
