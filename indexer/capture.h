#ifndef __CORNET_CAPTURE_H__
#define __CORNET_CAPTURE_H__

#include "pcapdb.h"
#include "event.h"
#include <pfring.h>
#include <pcap/pcap.h>

struct capture_state {
    struct system_state * sys_state;
    // The name used to open this interface. This includes it's queue.
    char interface[BASE_DIR_LEN+1];
    // The interface queue for this thread.
    int queue;
    // The capture interface handle.
    union {
        pcap_t * libpcap_if;
        pfring * pfring_if;
    };
    // An event to tell an individual capture thread to shutdown.
    struct event shutdown;
    // The head of the chain of buckets currently being captured.
    struct bucket *head_bkt;
    // The tail of the current bucket chain.
    struct bucket *current_bkt;
    // The last capture stats we saw from libpcap or pfring.
    // We'll use this to keep track of the stat differences from bucket to bucket.
    // They use different sizes because of overflow issues with libpcap that pfring doesn't have.
    // See bucketize.c, send_bucket() for more details
    union {
        uint64_t pfring_last_if_seen;
        uint32_t libpcap_last_if_seen;
    };
    union {
        uint64_t pfring_last_sys_dropped;
        uint32_t libpcap_last_sys_dropped;
    };
    // A count of dropped packets from when we can't get a new bucket.
    // This will be folded into the next set of bucket stats.
    uint64_t dropped_pkts;
    // The actual thread object for this capture interface.
    pthread_t thread;

    // The status of this capture thread
};

void * capture(void *);
void usage();

#endif