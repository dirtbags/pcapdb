#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

#ifndef TERR
    #define TERR(...)
#endif

// Initialize a queue object.
void queue_init(struct queue * q) {
    q->head = q->tail = NULL;
    q->count = 0;
    q->closed = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

// Creates a new queue node for the given item,
// and adds it to the queue. 
int queue_push(struct queue * q, void * item) {
    struct queue_node * new_node;
    TERR("%lx q push lock.\n", pthread_self());
    pthread_mutex_lock(&q->lock);

    //printf("Item pushed: %p\n", item);

    if (q->closed) {
        TERR("%lx q_push q->closed unlock.\n", pthread_self());
        pthread_mutex_unlock(&q->lock);
        return 1;
    }

    new_node = calloc(1, sizeof(struct queue_node));
    if (new_node == NULL) {
        pthread_mutex_unlock(&q->lock);
        return 1;
    }
    
    new_node->next = NULL;
    new_node->item = item;

    if (q->tail != NULL) {
        q->tail->next = new_node;
    } else {
        q->head = new_node;
    }
    q->tail = new_node;
    q->count++;

    // Tell other threads that there is something to grab now.
    TERR("%lx q signal unlock.\n", pthread_self());
    pthread_cond_signal(&q->cond);
    // This should go after the signal. The signaled thread will
    // wait on this release anyway.
    TERR("%lx q push unlock.\n", pthread_self());
    pthread_mutex_unlock(&q->lock);
    return 0;
}

// Grabs the head of the queue, frees the queue node,
// and returns it's item. If there was nothing to grab,
// wait until there is. 
void * queue_pop(struct queue * q, uint8_t flags) {
    uint8_t nowait = flags & Q_NOWAIT;
    uint8_t force  = flags & Q_FORCE;
    struct queue_node * old_node;
    void * item = NULL;
    int i;

    TERR("%lx q pop lock.\n", pthread_self());
    pthread_mutex_lock(&q->lock);

    for (i=0; i<2; i++) {
        // Don't even try to return anything if the queue is closed.
        if (q->closed && !force) {
            TERR("%lx q_pop q->closed unlock.\n", pthread_self());
            pthread_mutex_unlock(&q->lock);
            return NULL;
        }

        if (q->head != NULL) {
            old_node = q->head;
            q->head = old_node->next;
            item = old_node->item;
            q->count--;
            free(old_node);
            if (q->head == NULL) {
                q->tail = NULL;
            }
            // Unlock and return the item.
            break;
        }
        if (i == 0 && !nowait) {
            // After our first failed attempt, release the lock and wait
            // on the queue condition. The lock is re-acquired when the condition
            // is signalled.
            TERR("%lx q_pop cond_wait unlock.\n", pthread_self());
            pthread_cond_wait(&q->cond, &q->lock);
        }
    }
    TERR("%lx q_pop unlock.\n", pthread_self());
    pthread_mutex_unlock(&q->lock);
    TERR("%lx q_pop unlock done.\n", pthread_self());
    //printf("Item popped: %p\n", item);
    return item;
}

uint64_t queue_count(struct queue * q) {
    uint64_t count;
    TERR("%lx q push lock.\n", pthread_self());
    pthread_mutex_lock(&q->lock);
    count = q->count;
    TERR("%lx q_count unlock.\n", pthread_self());
    pthread_mutex_unlock(&q->lock);
    return count;
}

void queue_close(struct queue * q) {
    TERR("%lx q close lock.\n", pthread_self());
    pthread_mutex_lock(&q->lock);
    q->closed = 1;
    TERR("%lx q_close broadcast unlock.\n", pthread_self());
    pthread_cond_broadcast(&q->cond);
    TERR("%lx q_close unlock.\n", pthread_self());
    pthread_mutex_unlock(&q->lock);
}
