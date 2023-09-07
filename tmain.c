#include <pthread.h>
#include <stdio.h>

void *entry(void *arg)
{
    int c = *((int *)arg);
    // for (volatile int i = 1; i < 1000; i++)
    while (1)
    {
        printf("%d\n", c);
    }
}

pthread_t pthread_id[100];
int args[100];

int main()
{
    for (int i = 0; i < 100; i++)
    {
        args[i] = i;
        pthread_create(&pthread_id[i], NULL, entry, args + i);
    }
    
    for(int i=0; i<100; i++)
        pthread_join(pthread_id[i], NULL);
    // printf("\nMain return\n");
}
