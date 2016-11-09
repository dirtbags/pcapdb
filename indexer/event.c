#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "event.h"

#ifndef TERR
    #define TERR(...)
#endif

void event_init(struct event * ev) {
    ev->status = 0;
    pthread_mutex_init(&ev->lock, NULL);
}

void event_set(struct event * ev) {
    TERR("%lx event_set lock (%p)\n", pthread_self(), ev);
    pthread_mutex_lock(&ev->lock);
    ev->status = 1;
    TERR("%lx event_set unlock\n", pthread_self());
    pthread_mutex_unlock(&ev->lock);
}

void event_clear(struct event * ev) {
    TERR("%lx event_clear lock (%p)\n", pthread_self(), ev);
    pthread_mutex_lock(&ev->lock);
    ev->status = 0;
    TERR("%lx event_clear unlock\n", pthread_self());
    pthread_mutex_unlock(&ev->lock);
}

uint8_t event_check(struct event * ev) {
    uint8_t status;
    TERR("%lx event_check lock (%p).\n", pthread_self(), ev);
    pthread_mutex_lock(&ev->lock);
    status = ev->status;
    TERR("%lx event_check unlock.\n", pthread_self());
    pthread_mutex_unlock(&ev->lock);
    return status;
}
