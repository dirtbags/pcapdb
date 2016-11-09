#ifndef __CORNET_BUCKETIZE_H__
#define __CORNET_BUCKETIZE_H__

#include "pcapdb.h"
#include "capture.h"
#include "network.h"
#include "queue.h"

#include <pcap/pcap.h>
#include <stdint.h>

#define bucketq_push(Q,I) queue_push(Q, (void *)I)
#define bucketq_pop(Q) (struct bucket *) queue_pop(Q, 0)
// Free all buckets in the given queue. Only use after the queue is closed.
uint64_t bucketq_free(struct queue *, struct system_state *);

#define BKT_SPACE_LEFT(bkt) bkt->bucket_end - (void *)bkt->next_pkt

// A bucket is used to collect a large group of (time ordered) packets to 
// pass off to other threads.
struct bucket {
    struct bucket * next;
    // One byte past the end of the bucket.
    void * bucket_end;
    // Network statistics for packets in this bucket chain.
    struct network_stats * stats; 
    // Pointer to the indexes for this bucket chain.
    struct index_set * indexes;
    // Where to put the next_pkt received. 
    struct packet_record * next_pkt;
    // This will just point to the first byte of the data section.
    struct packet_record * first_pkt;
    // The last packet in the bucket
    // Do not add fields to this struct after this one. It's position is used to determine
    // where to start putting data.
    struct packet_record * last_pkt;
};

// Initialize a brand new bucket. It will still need to be reset.
void bucket_init(struct bucket *);
// Reset a bucket and ready it for capture.
void bucket_reset(struct bucket *, struct config *);
// Call back function for putting packets into buckets via libpcap dispatch() or loop()
void libpcap_bucketize(
        uint8_t *, // Generic pointer (should be given a struct capture_state *)
        const struct pcap_pkthdr *, // The captured packet's header.
        const uint8_t *); // The captured packet
// Puts packets into buckets using pfring.
int pfring_bucketize(
        struct capture_state *, // This thread's capture state.
        int); // How many packets to capture.

// Put the current bucket head onto the filled queue, and
// sets its basic capture stats.
void send_bucket(struct capture_state *);

#endif
