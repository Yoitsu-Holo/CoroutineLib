#include "libco.h"
#include <stdio.h>
#include <stdlib.h>

void entry(void *arg)
{
    int c = *((int *)arg);
    // for (volatile int i = 1; i < 1000; i++)
    while (1)
    {
        printf("%d ", c);
        co_yield();
    }
}
struct co *coo[100];
int args[100];

int main()
{
    for (int i = 0; i < 100; i++)
    {
        args[i] = i;
        coo[i] = co_start("co", entry, args + i);
    }
    co_wait(coo[0]);
    // printf("\nMain return\n");
}