#ifndef __CORNET_EVENT_H__
#define __CORNET_EVENT_H__

#include <pthread.h>
#include <stdint.h>

#define EVENT_INIT {0, PTHREAD_MUTEX_INITIALIZER}

struct event {
    uint8_t status;
    pthread_mutex_t lock;
} event; 

// Initialize the event struct, setting status to 0. Can be
// statically initialized with EVENT_INIT.
void event_init(struct event *);
// Set the event to true.
void event_set(struct event *);
// Set the event to false.
void event_clear(struct event *);
// Check the status of the event.
uint8_t event_check(struct event *);

#endif
