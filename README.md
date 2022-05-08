# CoroutineLib

这是一个实现Linux下协程的库，其用到了ucontext库文件，在后续的更新中会加入经过优化的上下文保存函数

# 头文件

```cpp
#ifndef __LIBCO_H
#define __LIBCO_H

typedef void (*cofunc)(void *);

#define CO_STATUS_DEAD 0    // 协程执行结束
#define CO_STATUS_SUSPEND 1 // 协程创建后未run，或yield后处的状态
#define CO_STATUS_RUNNING 2 // 协程当前正在运行

// 类型声明
struct co;

// todo: main 函数的执行也是一个协程，因此可以在 main 中调用 co_yield 或 co_wait
// todo: main 函数返回后，无论有多少协程，进程都将直接终止

struct co *co_start(const char *, void *, void *); /*
  ^ co_start(name, func, arg) 创建一个新的协程，并返回一个指向 struct co 的指针 (类似于 pthread_create)。
  * 新创建的协程从函数 func 开始执行，并传入参数 arg。新创建的协程不会立即执行，而是调用 co_start 的协程继续执行。
  * 使用协程的应用程序不需要知道 struct co 的具体定义，因此请把这个定义留在 libco.c 中
  * 框架代码中并没有限定 struct co 结构体的设计，所以你可以自由发挥。
  * co_start 返回的 struct co 指针需要分配内存。我们推荐使用 malloc() 分配。
  */

void co_yield(); /*
  ^ co_yield() 实现协程的切换。
  * 协程运行后一直在 CPU 上执行，直到 func 函数返回或调用 co_yield 使当前运行的协程暂时放弃执行。
  * co_yield 时若系统中有多个可运行的协程时 (包括当前协程)，你应当随机选择下一个系统中可运行的协程。
  */

void co_wait(struct co *); /*
  ^ co_wait(co) 表示当前协程需要等待，直到 co 协程的执行完成才能继续执行 (类似于 pthread_join)。
  * 在被等待的协程结束后、 co_wait() 返回前，co_start 分配的 struct co 需要被释放。如果你使用 malloc()，使用 free() 释放即可。
  * 因此，每个协程只能被 co_wait 一次 (使用协程库的程序应当保证除了初始协程外，其他协程都必须被 co_wait 恰好一次，否则会造成内存泄漏)。
  */

#endif
```

标准执行流：

```shell
co_start :创建一个协程（在第一次执行的时候会隐式的调用co_init完成初始化）
    co_yeild :从一个协程切换至另一个协程执行（随机，暂不能在main函数中调用）
    co_wait :等待一个协程的结束，暂只能在主函数中执行
```

# 源文件

## 文件头

```cpp
#include "libco.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#define DEFAULT_STACK_SIZE 65536
#define DEFAULT_COROUTINE 32
#define MAIN_CO_ID 0
```

定义了一些初始默认值：

默认栈大小为 64 KiB，默认协程数量为 32

## 数据结构

```cpp
typedef struct co
{
    int id;
    char name[10];
    ucontext_t ctx; // 协程的执行环境
    cofunc func;
    void *arg;
    char coStatus; //当前状态
    char *stack;   // 64KiB 栈内存
} co_t;

typedef struct schedule
{
    int running;          // 当前运行的协程号
    int coCount;          // 当前有几个协程
    int coMax;            // 协程数组容量
    struct co **coHeader; // 协程数组
} schedule_t;

static schedule_t sch; // 默认调度器，不可更改
```

### co_t

协程信息：

1. 协程编号（0号为主协程，也就是主函数）
2. 协程名字，最多9字符
3. 协程上下文
4. 协程调用的函数
5. 协程调用函数的参数
6. 当前协程的状态
7. 栈指针

调度器：

1. 当前运行的协程编号
2. 当前有几个协程在运行
3. 当前调度器最高能容纳的最大数量
4. 协程数组

除此之外，还有一个全局的调度器

## co_start

```cpp
co_t *co_start(const char *name, void *func, void *arg)
{
    if (sch.coMax == 0)
        co_init();

    int cid = -1;
    if (sch.coCount == sch.coMax)
    {
        cid = sch.coCount;
        sch.coHeader = realloc(sch.coHeader, 2 * sch.coMax * sizeof(struct co *));
        memset(sch.coHeader + sch.coMax, 0, sizeof(sch.coMax * sizeof(struct co *)));
        sch.coMax *= 2;
    }
    else
        for (int i = 0; i < sch.coMax; i++)
            if (sch.coHeader[i] == NULL ||
                sch.coHeader[i]->coStatus == CO_STATUS_DEAD)
            {
                cid = i;
                break;
            }

    sch.coCount++;

    assert(cid >= 0);

    co_t *co;
    if (sch.coHeader[cid] != NULL)
    {
        co = sch.coHeader[cid];
    }
    else
    {
        co = malloc(sizeof(co_t));
        memset(co, 0, sizeof(co_t));
        sch.coHeader[cid] = co;
        co->stack = (cid != MAIN_CO_ID) ? (malloc(DEFAULT_STACK_SIZE)) : (NULL);
        if (co->stack)
            memset(co->stack, 0, DEFAULT_STACK_SIZE);
    }

    co->id = cid;
    strcpy(co->name, name);
    co->coStatus = CO_STATUS_SUSPEND;
    co->func = func;
    co->arg = arg;

    getcontext(&co->ctx);

    if (func)
    {
        //设置上下文的栈信息
        co->ctx.uc_stack.ss_sp = co->stack;
        co->ctx.uc_stack.ss_size = DEFAULT_STACK_SIZE;
        co->ctx.uc_link = &sch.coHeader[MAIN_CO_ID]->ctx;
        //包装一个新的上下文，调用func

        makecontext(&co->ctx, (void (*)(void))co_func, 1, cid);
        // setcontext(&co->ctx);
    }
    return co;
}
```

创建一个新的协程

1. 在执行的时候，检测是否是第一次执行，如果是第一次执行，那么先对于调度器进行初始化（co_init）
2. 分配协程空间
   1. 如果是直接继承于死亡协程的，那么不用再分配空间，直接初始化即可
   2. 如果是新分配的，那么需要初始化协程的信息
      1. 如果不是主协程，那么需要malloc一段栈空间
      2. 如果是主协程，那么直接是调用系统栈，不须要新的栈空间
3. 标记当前的协程状态为SUSPEND
4. 如过传入了调用函数信息（即不是主协程），那么需要设置栈空间的起始位置，并且设置当前协程完成后需要恢复的上下文，并且包装一个新的上下文，使用cofun用于调用这个协程

### co_init

```cpp
static void co_init()
{
    sch.coCount = 0;
    sch.coMax = DEFAULT_COROUTINE;
    sch.coHeader = malloc(sch.coMax * sizeof(struct co *));
    memset(sch.coHeader, 0, sch.coMax * sizeof(struct co *));

    co_t *co = co_start("MAIN", NULL, NULL);
    assert(co->id == MAIN_CO_ID);
    sch.coHeader[0] = co;
    co_t *m_co = sch.coHeader[MAIN_CO_ID];
    assert(m_co->id == MAIN_CO_ID);
    // DEBUG(co->name);

    m_co->coStatus = CO_STATUS_RUNNING;
    sch.running = co->id;
}
```

其本质还是调用的 co_start 创建一个编号为 0 的协程

### co_func

```cpp
static void co_func(int nid)
{
    int pid = sch.running;
    co_t *pco = sch.coHeader[pid];
    co_t *nco = sch.coHeader[nid];
    pco->coStatus = CO_STATUS_SUSPEND;
    nco->coStatus = CO_STATUS_RUNNING;
    sch.running = nid;

    nco->func(nco->arg);
    sch.coHeader[nid]->coStatus = CO_STATUS_DEAD;
    sch.coCount--;
}
```

用来调用协程执行入口的函数

## co_yield

```cpp
void co_yield()
{
    int nowco = sch.running;
    int nextco = getnextid();

    assert(nowco != MAIN_CO_ID);
    assert(nextco != MAIN_CO_ID && nextco < sch.coMax);

    sch.coHeader[nowco]->coStatus = CO_STATUS_SUSPEND;
    sch.coHeader[nextco]->coStatus = CO_STATUS_RUNNING;
    sch.running = nextco;
    swapcontext(&sch.coHeader[nowco]->ctx, &sch.coHeader[nextco]->ctx);
}
```

先判断是否合法，然后直接交换协程

## co_wait

```cpp
void co_wait(struct co *co)
{
    while (co->coStatus != CO_STATUS_DEAD)
        co_run();
}
```

循环判断当前协程是否已近死亡

1. 死亡，直接返回
2. 未死亡，执行 co_run 随机执行一个

### co_run

```cpp
static void co_run()
{
    int nextco = getnextid();
    assert(nextco != MAIN_CO_ID && nextco < sch.coMax);

    sch.coHeader[nextco]->coStatus = CO_STATUS_RUNNING;
    sch.running = nextco;

    swapcontext(&sch.coHeader[MAIN_CO_ID]->ctx, &sch.coHeader[nextco]->ctx);
}
```

随机运行一个非主协程的协程

### co_getnextid

```cpp
static int getnextid()
{
    unsigned int volatile rd = MAIN_CO_ID;
    asm volatile("rdrand %0"
                 : "=r"(rd));
    rd %= (sch.coCount - 1);
    rd++;

    for (int i = 1; i < sch.coMax; i++)
    {
        if ((sch.coHeader[i] != NULL) && (sch.coHeader[i]->coStatus != CO_STATUS_DEAD))
            rd--;
        if (!rd)
            return i;
    }
    return MAIN_CO_ID;
}
```

随机获取一个除主协程外的协程