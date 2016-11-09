#ifndef __CORNET_QUEUE_H__
#define __CORNET_QUEUE_H__

#include <stdint.h>
#include <pthread.h>

// This queue library deals in void pointers to generic memory structures.
// You should wrap the functions with appropriate macros that do type conversion.
// It is thread safe, but doesn't block when trying to pop from an empty queue.
struct queue_node {
    struct queue_node * next;
    void * item;
} queue_node;

struct queue {
    struct queue_node * head;
    struct queue_node * tail;
    uint64_t count;
    int closed; 
    pthread_mutex_t lock; 
    pthread_cond_t cond;
} queue;

#define QUEUE_INIT {NULL, NULL, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER}

// Initialize the empty queue.
void queue_init(struct queue *);
// Add a new item to the queue tail.
// Returns 0 on success, 1 on memory allocation failure.
int queue_push(struct queue *, void *);

// Flags for popping from the queue.
// Q_NOWAIT - When popping, if the queue is empty, return NULL immediately.
#define Q_NOWAIT (uint8_t)0x01
// Q_FORCE - When popping, ignore whether the queue is closed or not. This
//           is primarily for when draining a closed queue and freeing the objects.
#define Q_FORCE (uint8_t)0x02
// Grabs the item from the top of the queue. If there is no such
// item, wait until there is one. Can possibly return NULL if the 
// the condition is signaled when there is nothing to get.
void * queue_pop(struct queue *, uint8_t flags);
// Get the current size of the queue.
uint64_t queue_count(struct queue *);

// Shutdown to queue, making sure no thread is locked from it.
void queue_close(struct queue *);
// 

// We need a free function to free everything in a queue, probably. 
//void queue_free(struct queue *, 

#endif
