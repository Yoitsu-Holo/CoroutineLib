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
    int running;
    int coCount;          // 当前有几个协程
    int coMax;            // 协程数组容量
    struct co **coHeader; // 协程数组
} schedule_t;

static schedule_t sch;

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

static void co_run()
{
    int nextco = getnextid();
    assert(nextco != MAIN_CO_ID && nextco < sch.coMax);

    sch.coHeader[nextco]->coStatus = CO_STATUS_RUNNING;
    sch.running = nextco;

    swapcontext(&sch.coHeader[MAIN_CO_ID]->ctx, &sch.coHeader[nextco]->ctx);
}

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

void co_wait(struct co *co)
{
    while (co->coStatus != CO_STATUS_DEAD)
        co_run();
}
