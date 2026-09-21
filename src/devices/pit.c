#include "devices/pit.h"

#include "threads/interrupt.h"
#include "threads/io.h"
#include <debug.h>
#include <stdint.h>

/* 8254可编程中断计时器(PIT)的接口。
   详见[8254]。 */

/* 8254寄存器。 */
#define PIT_PORT_CONTROL 0x43                        /* 控制端口。 */
#define PIT_PORT_COUNTER(CHANNEL) (0x40 + (CHANNEL)) /* 计数器端口。 */

/* PIT每秒的周期数。 */
#define PIT_HZ 1193180

/* 配置PIT中的给定CHANNEL。在PC中，PIT的三个输出通道的连接方式如下：

     - 通道0连接到中断线0，可用作周期性定时器中断，在
       Pintos中由devices/timer.c实现。

     - 通道1用于动态RAM刷新(在较旧的PC中)。
       最好不要改动此通道。

     - 通道2连接到PC扬声器，可用于播放音调，在
       Pintos中由devices/speaker.c实现。

   MODE指定输出的形式：

     - 模式2是周期性脉冲：通道的输出在周期的大部分时间为1，
       但在周期快结束时短暂下降到0。这适用于连接到中断控制器
       以生成周期性中断。

     - 模式3是方波：在周期的前半部分为1，在后半部分为0。
       这适用于在扬声器上生成音调。

     - 其他模式用处不大。

   FREQUENCY是每秒的周期数，单位为Hz。 */
void pit_configure_channel(int channel, int mode, int frequency)
{
    uint16_t count;
    enum intr_level old_level;

    ASSERT(channel == 0 || channel == 2);
    ASSERT(mode == 2 || mode == 3);

    /* 将FREQUENCY转换为PIT计数器值。PIT有一个以PIT_HZ周期每秒
       运行的时钟。我们必须将FREQUENCY转换为这些周期的数量。 */
    if (frequency < 19) {
        /* 频率太低：商会超出16位计数器的范围。
           强制设置为0，PIT将其视为65536，即最高可能计数。
           这产生约18.2 Hz的定时器。 */
        count = 0;
    }
    else if (frequency > PIT_HZ) {
        /* 频率太高：商会下溢到0，PIT将其解释为65536。
           模式2中计数为1是非法的，所以我们强制设置为2，这产生
           约596.590 kHz的定时器。(这个定时器速率可能太快而无用。) */
        count = 2;
    }
    else
        count = (PIT_HZ + frequency / 2) / frequency;

    /* 配置PIT模式并加载其计数器。 */
    old_level = intr_disable();
    outb(PIT_PORT_CONTROL, (channel << 6) | 0x30 | (mode << 1));
    outb(PIT_PORT_COUNTER(channel), count);
    outb(PIT_PORT_COUNTER(channel), count >> 8);
    intr_set_level(old_level);
}
