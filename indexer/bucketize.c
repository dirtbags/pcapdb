#include <stdlib.h>
#include <hugetlbfs.h>
#include <pcap/pcap.h>
#include <string.h>
#include <stdint.h>
#include <pfring.h>
#include "bucketize.h"

// Get the bucket that we'll put the next packet in
struct bucket * get_pkt_bucket(struct capture_state * cap_state) {

    struct config * conf = &cap_state->sys_state->conf;
    struct bucket ** head_bkt = &cap_state->head_bkt;

    // If the next packet could overfill the output file, start a new bucket.
    if (*head_bkt == NULL ||
        ((*head_bkt)->stats->chain_size + sizeof(struct pcap_pkthdr32) + conf->mtu >
            conf->outfile_size)) {
        struct bucket * old_head = *head_bkt;

        if (old_head != NULL) {
            send_bucket(cap_state);
            //bucketq_push(&cap_state->sys_state->filled_bkts, old_head);
        }

        // Try to get a new bucket.
        *head_bkt = queue_pop(&cap_state->sys_state->ready_bkts, Q_NOWAIT);
        if (*head_bkt == NULL) {
            return NULL;
        }

        // Clear the bucket header data.
        bucket_reset(*head_bkt, conf);

        cap_state->current_bkt = *head_bkt;
        // Allocate a new network_stats object for the head of this bucket chain.
        (*head_bkt)->stats = calloc(1, sizeof(network_stats));
        // We need to know which interface these packets were captured on.
        (*head_bkt)->stats->interface = cap_state->interface;

        // Record packets dropped from times when we couldn't get a bucket.
        (*head_bkt)->stats->dropped = cap_state->dropped_pkts;
        cap_state->dropped_pkts = 0;

        return (*head_bkt);
    }

    // Check if this packet will exceed the size of this bucket.
    // If so, grab a new bucket and add it to the chain.
    if (conf->mtu + sizeof(packet_record) > BKT_SPACE_LEFT(cap_state->current_bkt)) {
        // Try to get the next bucket.
        struct bucket * bkt = bucketq_pop(&cap_state->sys_state->ready_bkts);
        // Add this new bucket to end of our bucket list.
        cap_state->current_bkt->next = bkt;
        cap_state->current_bkt = cap_state->current_bkt->next;
        // Clear the bucket header data.
        bucket_reset(cap_state->current_bkt, conf);
    }
    return cap_state->current_bkt;
}

// Put the current bucket head onto the filled queue, and
// sets its basic capture stats.
void send_bucket(struct capture_state * cap_state) {

    struct bucket * bkt = cap_state->head_bkt;
    pfring_stat pf_stats;
    struct pcap_stat lp_stats;

    // There is no bucket to send.
    if (cap_state->head_bkt == NULL) {
        return;
    }

    bkt->stats->dropped = cap_state->dropped_pkts;
    switch (cap_state->sys_state->conf.capture_mode) {
        case CAP_MODE_PFRING:
        case CAP_MODE_PFRING_ZC:
            pfring_stats(cap_state->pfring_if, &pf_stats);
            bkt->stats->sys_dropped = pf_stats.drop - cap_state->pfring_last_sys_dropped;
            bkt->stats->if_seen = pf_stats.recv - cap_state->pfring_last_if_seen;
            cap_state->pfring_last_if_seen = pf_stats.recv;
            cap_state->pfring_last_sys_dropped = pf_stats.drop;
            break;
        case CAP_MODE_LIBPCAP:
            pcap_stats(cap_state->libpcap_if, &lp_stats);
            // The libpcap stats object only uses 32 bit unsigned ints, which is nowhere near
            // large enough. The way they add to it means it will overflow, so we can watch for that
            // to get accurate stats.
            if (lp_stats.ps_drop < cap_state->libpcap_last_sys_dropped) {
               bkt->stats->sys_dropped = (UINT32_MAX - cap_state->libpcap_last_sys_dropped) +
                                            lp_stats.ps_drop;
            } else {
                bkt->stats->sys_dropped = lp_stats.ps_drop - cap_state->libpcap_last_sys_dropped;
            }
            if (lp_stats.ps_recv < cap_state->libpcap_last_if_seen) {
                bkt->stats->if_seen = (UINT32_MAX - cap_state->libpcap_last_if_seen) +
                                        lp_stats.ps_recv;
            } else {
                bkt->stats->if_seen = lp_stats.ps_recv - cap_state->libpcap_last_if_seen;
            }
            cap_state->libpcap_last_if_seen = lp_stats.ps_recv;
            cap_state->libpcap_last_sys_dropped = lp_stats.ps_drop;
        default:
            // You don't get these stats from CAP_MODE_FILE.
            break;
    }

    bucketq_push(&cap_state->sys_state->filled_bkts, cap_state->head_bkt);
    cap_state->head_bkt = NULL;
}

void libpcap_bucketize(uint8_t * args, const struct pcap_pkthdr * hdr, const uint8_t * packet) {
    struct capture_state * cap_state = (struct capture_state *) args;

    // Get the bucket that we'll copy this packet into.
    struct bucket * bkt = get_pkt_bucket(cap_state);

    // We couldn't get a bucket. Drop this packet.
    if (bkt == NULL) {
        cap_state->dropped_pkts++;
        return;
    }

    // "Allocate" space in the bucket for the packet record.
    struct packet_record * rec = bkt->next_pkt;

    cap_state->head_bkt->stats->captured_pkts++;

    // Copy the packet header into the packet record.
    // This is where we deal with the native (often 64 bit) vs
    // on disk struct pcap_pkthdr timestamp size difference.
    rec->header.ts.tv_sec = (uint32_t)hdr->ts.tv_sec;
    rec->header.ts.tv_usec = (uint32_t)hdr->ts.tv_usec;
    rec->header.len = hdr->len;
    rec->header.caplen = hdr->caplen;

    // Copy the actual packet data into the bucket.
    memcpy(&rec->packet, packet, hdr->caplen);

    // Adjust the next_pkt pointer in the bucket.
    bkt->next_pkt = next_pkt(bkt->next_pkt);

    // Our current packet is now our last packet in the bucket.
    bkt->last_pkt = rec;

    // When we write captured data out, we will just be writing out the
    // the pcap header and data, so we only need to include that in our disk
    // space used calculation.
    cap_state->head_bkt->stats->chain_size += sizeof(struct pcap_pkthdr32) + hdr->caplen;

    return;
}

// To tell pfring_recv to wait for a packet.
#define WAIT_FOR_PACKET 1

// Captures up to 'limit' packets from the pfring interface defined in the given
// capture state.
int pfring_bucketize(struct capture_state * cap_state, int limit) {
    struct config * conf = &cap_state->sys_state->conf;
    // If we don't have a bucket to store a packet in, we still need to grab that packet and put
    // it somewhere, so we use this throwaway buffer.
    uint8_t null_buffer[sizeof(struct packet_record) + conf->mtu];
    // Where we'll store the packet header information. We won't be keeping all of the pfring
    // header data, so we can't just write the data directly to memory.
    struct pfring_pkthdr pf_hdr;

    // The packet's packet record location in the bucket.
    struct packet_record * rec;

    // The buffer length to hand to pfring.
    // The bucket retrieval function makes sure we have a bucket that can fit a packet of size
    // up to conf->mtu.
    // A buffer length of zero indicates we're using zero-copy mode.
    u_int buf_len;
    switch (conf->capture_mode) {
        default:
            WARN("Invalid capture mode in pfring_bucketize: %d", conf->capture_mode);
        case CAP_MODE_PFRING:
            buf_len = conf->mtu;
            break;
        case CAP_MODE_PFRING_ZC:
            buf_len = 0;
            break;
    }

    int i;
    int captured = 0;
    struct timespec ts;
    u_char * pkt;

    for (i=0; i < limit; i++) {
        // Get the bucket where we'll store this packet.
        struct bucket * bkt = get_pkt_bucket(cap_state);
        if (bkt == NULL) {
            // We couldn't get a bucket; Drop this packet.
            rec = (struct packet_record *)null_buffer;
        } else {
            // We do have a bucket. Yay.
            rec = bkt->next_pkt;
        }

        // We have to give pfring a pointer to a pointer where the packet data
        // will be stored. Since all we have is one byte in the packet_record where the packet
        // data will start, we have to make such a pointer.
        if (conf->capture_mode == CAP_MODE_PFRING) {
            pkt = &rec->packet;
            // In normal pfring mode, we hand a buffer over to have the packet copied for us.
            pfring_recv(cap_state->pfring_if, &pkt, conf->mtu, &pf_hdr, WAIT_FOR_PACKET);
        } else { // Zero copy mode
            pkt = NULL;
            // In zero copy mode, we get back the address of the interface buffer where
            // the packet is stored so that we can immediately copy it locally.
            pfring_recv(cap_state->pfring_if, &pkt, 0, &pf_hdr, WAIT_FOR_PACKET);
            memcpy(&rec->packet, pkt, pf_hdr.len);
        }
        //printf("%d, %d, %d\n", 0, pf_hdr.caplen, pf_hdr.len);
        //print_raw_packet(30, pkt, 0, pf_hdr.caplen, 60);

        if (unlikely(bkt == NULL)) {
            // If we have to drop the packet, just note it.
            cap_state->dropped_pkts++;
            continue;
        }

        cap_state->head_bkt->stats->captured_pkts++;
        captured++;

        // Copy the header information into our packet summary structure.
        rec->header.len = pf_hdr.len;
        rec->header.caplen = pf_hdr.caplen;
        if (pf_hdr.ts.tv_sec == 0) {
            // In zero-copy mode we won't get a timestamp from the interface, so we have to get
            // it ourselves.
            // The fastest way to do this is to use one of the coarse timers with clock_gettime.
            // The coarse timers don't require any system calls or blocking; On a random test
            // system the time required was around 15ns, or 1/10th the time of other clock methods.
            // It has the downside of less accuracy (it's only updated once per CPU tick, and may
            // not be 100% consistent across CPUs). Within a single bucket this won't have any
            // effect on packet ordering, since packets will always be ordered according to how
            // they arrived (for a given flow). There is the possibility packets being out of
            // order between buckets. Additionally, time inconsistancies may occur from
            // We use CLOCK_REALTIME_COARSE; though it's potentially effected by system time
            // shifts, so are all the other options.
            clock_gettime(CLOCK_REALTIME_COARSE, &ts);
            rec->header.ts.tv_sec = (uint32_t) ts.tv_sec;
            rec->header.ts.tv_usec = (uint32_t) (ts.tv_nsec/1000);
        } else {
            // Overflow is possible, but not till the year 2106 < 2**32;
            rec->header.ts.tv_sec = (uint32_t) pf_hdr.ts.tv_sec;
            rec->header.ts.tv_usec = (uint32_t) pf_hdr.ts.tv_usec;
        }

        // Adjust the next_pkt pointer in the bucket.
        bkt->next_pkt = next_pkt(bkt->next_pkt);

        // Our current packet is now our last packet in the bucket.
        bkt->last_pkt = rec;

        // When we write captured data out, we will just be writing out the
        // the pcap header and data, so we only need to include that in our disk
        // space used calculation.
        // The caplen is always set to 0 for pfring. The real length is just in .len
        cap_state->head_bkt->stats->chain_size += sizeof(struct pcap_pkthdr32) + pf_hdr.len;
    }

    return captured;
}

// Reset this bucket so that it's ready for new data.
void bucket_reset(struct bucket * bkt, struct config * conf) {
    // We don't have a next bucket at present.
    bkt->next = NULL;
    // If this has a stats struct, free the struct.
    // We make sure to set this to null when the bucket is first allocated, so 
    // we don't have to worry about random data being there.
    if (bkt->stats != NULL) {
        free(bkt->stats);
        bkt->stats = NULL;
    }
    // Do the same for the indexes struct.
    if (bkt->indexes != NULL) {
        free(bkt->indexes);
        bkt->indexes = NULL;
    }
    // Set the first_pkt pointer to point to the beginning of the data section,
    // which happens to be just after itself. Right.
    bkt->first_pkt = (void *)&bkt->last_pkt + sizeof(struct packet_record *);
    // The next_pkt is our first packet.
    bkt->next_pkt = bkt->first_pkt;
    // We don't have a last packet yet.
    bkt->last_pkt = NULL;
    // Set where the end of the bucket is, so we can easily calculate how much space is left.
    bkt->bucket_end = (void *)bkt + conf->bucket_pages*HUGE_PAGE_SIZE;
}

// Initialize a brand new bucket.
void bucket_init(struct bucket * bkt) {
    bkt->stats = NULL;
    bkt->indexes = NULL;
    bkt->next = NULL;
}

// Free all the buckets in the given queue.
// This does not actually free the queue struct itself. 
// (In all cases, those queues are part of a larger structure).
uint64_t bucketq_free(struct queue * bktq, struct system_state * state) {
    uint64_t freed = 0;
    struct bucket * bkt;
    // Call the queue pop function in nonblocking, forceful mode.
    bkt = (struct bucket *) queue_pop(bktq, Q_NOWAIT|Q_FORCE);
    while ( bkt != NULL ) {
        while ( bkt != NULL ) {
            if (bkt->stats != NULL) {
                free(bkt->stats);
            }
            if (bkt->indexes != NULL) {
                free(bkt->indexes);
            }
            struct bucket * next_bkt = bkt->next;
            switch (state->conf.bucket_mem_type) {
                case MEM_MODE_HUGE_PAGES:
                    free_huge_pages(bkt);
                    break;
                case MEM_MODE_SYS_MEM:
                    free(bkt);
                    break;
                default:
                    CRIT("Invalid bucket mem type: %d", state->conf.bucket_mem_type);
                    return freed;
            }
            freed++;
            bkt = next_bkt;
        }
        bkt = (struct bucket *) queue_pop(bktq, Q_NOWAIT|Q_FORCE);
    }
    return freed;
}
