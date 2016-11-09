#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "../event.h"

#define THREADS 8
#define ITER 1000
#define SLEEP_NANO 50000

struct thread_data {
    uint64_t thread_num;
    struct event * quit_ev;
};

static void * donothing(void *);

int main() {
    uint64_t i;
    pthread_t threads[THREADS];
    pthread_attr_t attr;
    struct thread_data td[THREADS];
    struct event quit_event = EVENT_INIT;
    void * retval;

    pthread_attr_init(&attr);

    for (i=0; i < THREADS; i++) {
        td[i].thread_num = i;
        td[i].quit_ev = &quit_event;
        if (pthread_create(&threads[i], &attr, donothing, (void *)&td[i]) != 0) {
            printf("Thread creation failed.\n");
        }
    }

    printf("Done creating threads.\n");
    for (i=0; i < ITER; i++) {
        struct timespec sleep_time = {0, SLEEP_NANO};
        struct timespec rem;
        nanosleep(&sleep_time, &rem);
        event_check(&quit_event);
    }
    // Quiting threads
    printf("Quiting thread.\n");
    event_set(&quit_event);

    // Wait for threads.
    for (i=0; i < THREADS; i++) {
        pthread_join(threads[i], &retval);
    }

    printf("Threads joined. Event test done.\n");

    return 0;
}

static void * donothing(void * arg) {
    struct thread_data * my_td = (struct thread_data *) arg;
    struct timespec sleep_time = {0, 1000 + my_td->thread_num};
    struct timespec rem;
    uint64_t checks = 0;
    printf("Thread (%lu) %lx running.\n", my_td->thread_num, pthread_self());
    while (event_check(my_td->quit_ev) == 0) {
        nanosleep(&sleep_time, &rem);
        checks++;
    }
    printf("Thread (%lu) %lx looped %lu times.\n", my_td->thread_num, pthread_self(), checks);
    return NULL;
}

