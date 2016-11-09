#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "../queue.h"

#define THINGS 25000
#define THREADS 8

static void * renumber(void *);
struct thread_data {
    uint64_t thread_num;
    struct queue * q;
};

#define thing_q_push(Q, I) queue_push(Q, (void *)I)
#define thing_q_pop(Q) (int *) queue_pop(Q,0)

int main() {
    int things[THINGS];
    uint64_t i;
    pthread_t threads[THREADS];
    pthread_attr_t attr;
    struct queue thing_q = QUEUE_INIT;
    struct thread_data td[THREADS];

    pthread_attr_init(&attr);

    for (i=0; i < THINGS; i++) {
        things[i] = i;
    }

    for (i=0; i < THREADS; i++) {
        td[i].thread_num = i;
        td[i].q = &thing_q;
        if (pthread_create(&threads[i], &attr, renumber, (void *)&td[i]) != 0) {
            printf("Thread creation failed.\n");
        }
    }
    for (i=0; i < THINGS/2; i++) {
        if (thing_q_push(&thing_q, &things[i]) != 0) {
            printf("Queue allocation error.\n");
        }
    }
    while (queue_count(&thing_q) > 0);
    for (; i < THINGS; i++) {
        if (thing_q_push(&thing_q, &things[i]) != 0) {
            printf("Queue allocation error.\n");
        }
    }
  
    uint64_t count = queue_count(&thing_q);
    while (count > 0) {
        count = queue_count(&thing_q);
    }
    queue_close(&thing_q);

    for (i=0; i < THREADS; i++) {
        uint64_t ret_val=1; 
        void * rp = &ret_val;
        if (pthread_join(threads[i], &rp) != 0) {
            printf("Could not join thread %lu\n", i);
        }
    }
    for (i=0; i < THINGS; i++) {
        if (things[i] != i*2) {
            printf("Incorrect item grabbed.\n");
            return 1;
        }
    }
    if (queue_count(&thing_q) > 0) {
        printf("Failure: Items %lu still in queue.\n", queue_count(&thing_q));
        return 1;
    }
    printf("Success: %d items processed with %d threads.\n", THINGS, THREADS);
    return 0;
}

static void * renumber(void * arg) {
    struct thread_data * my_td = (struct thread_data *) arg;
    //printf("Thread %d started.\n", my_td->thread_num);
    int * thing;
    while (1) {
        thing = thing_q_pop(my_td->q);
        if (thing == NULL) {
            //printf("(%d) Thread dying.\n", my_td->thread_num);
            return 0;
        }
        *thing = *thing * 2;
    }
}

