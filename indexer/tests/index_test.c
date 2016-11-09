#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define DEBUG_ON
#include "../pcapdb.h"
#include "../pcapdb_init.h"
#include "../index.h"
#include "test_args.h"

void * clear_buckets(void *);
void walk_and_free(struct index_node *, keytype_t);

int main(int argc, char ** argv) {
    uint64_t buckets_allocated = 0;
    uint64_t buckets_freed = 0;
    struct system_state state;
    system_state_init(&state);
    pthread_t clr_bkts_thr;
    pthread_t idx_bkts_thr;
    pthread_attr_t thr_attr[2];

    PERR("\n### Indexing Test ###\n");

    char * pcap_fn = test_args(argc, argv, &state);

    struct capture_state * cap_state = capture_state_init(pcap_fn, &state);
    state.conf.bucket_pages = 4;
    state.conf.bucket_mem_type = MEM_MODE_SYS_MEM;
    state.conf.max_system_buckets = 128;
    state.conf.capture_mode = CAP_MODE_FILE;
    state.conf.outfile_size = HUGE_PAGE_SIZE*8;

    // Make as many buckets as possible.
    int ret = allocate_sysmem_buckets(&state);
    if (ret != 0) return ret;

    buckets_allocated = queue_count(&state.ready_bkts);
    PERR("Buckets Allocated: %lu\n", buckets_allocated);
    if (queue_count(&state.ready_bkts) == 0) {
        return 1;
    }
   
    PERR("Starting buckets: (Ready: %lu, Filled %lu, Indexed: %lu)\n",
            queue_count(&state.ready_bkts),
            queue_count(&state.filled_bkts), 
            queue_count(&state.indexed_bkts));
    ret = prepare_interface(cap_state);
    if (ret != 0) return ret;

    struct thread_state * thr_state = thread_state_init(&state);

    pthread_attr_init(&thr_attr[0]);
    pthread_create(&clr_bkts_thr, &thr_attr[0], clear_buckets, (void *)&state);
    pthread_attr_init(&thr_attr[1]);
    pthread_create(&idx_bkts_thr, &thr_attr[1], indexer, (void *)thr_state);

    capture(cap_state);

    PERR("Dispatch finished.\n");
    // Wait until all the buckets are back in the ready_bkts queue.
    uint64_t buckets_ready;
    do {
        // Sleep for one millisecond.
        struct timespec sleepytime = {1, 1000000};
        struct timespec rem;
        nanosleep(&sleepytime, &rem);
        //PERR("ready, filled, indexed: %lu, %lu, %lu\n",
        //        queue_count(&state.ready_bkts), queue_count(&state.filled_bkts),
        //        queue_count(&state.indexed_bkts));
        buckets_ready = queue_count(&state.ready_bkts);
        printf("(r, f, i): (%lu, %lu, %lu)\n", buckets_ready,
            queue_count(&state.filled_bkts), queue_count(&state.indexed_bkts));
        fflush(stdout);
    } while (buckets_ready < buckets_allocated);

    pcap_close(cap_state->libpcap_if);

    PERR("Done with buckets.\n");
    event_set(&state.shutdown);
    event_set(&thr_state->shutdown);
    queue_close(&state.ready_bkts);
    queue_close(&state.filled_bkts);
    queue_close(&state.indexed_bkts);
    PERR("Joining.\n");
    pthread_join(clr_bkts_thr, NULL);
    pthread_join(idx_bkts_thr, NULL);
    pthread_attr_destroy(&thr_attr[0]);
    pthread_attr_destroy(&thr_attr[1]);
    free(thr_state);
    free(cap_state);

    PERR("Freeing buckets: (Ready: %lu, Filled %lu, Indexed: %lu)\n",
                queue_count(&state.ready_bkts), 
                queue_count(&state.filled_bkts),
                queue_count(&state.indexed_bkts));
    buckets_freed += bucketq_free(&state.ready_bkts, &state);
    buckets_freed += bucketq_free(&state.filled_bkts, &state);
    buckets_freed += bucketq_free(&state.indexed_bkts, &state);
    if (buckets_freed != buckets_allocated) {
        PERR("Not all buckets accounted for.\n");
        PERR("  Allocated: %lu\n", buckets_allocated);
        PERR("  Freed:     %lu\n", buckets_freed);
        return 1;
    }


    return 0;
}

void * clear_buckets(void * thr_init) {
    struct system_state * state = thr_init;
    keytype_t tt;
    
    PERR("Clear Buckets Running (%lx).\n", pthread_self());

    // Keep going until we're told to shutdown and there aren't any buckets
    // in the filled queue.
    while (event_check(&state->shutdown) == 0) {
        struct bucket * bkt;
        struct bucket * next_bkt;

        bkt = bucketq_pop(&state->indexed_bkts);
        if (bkt == NULL) continue;

        for (tt= kt_FLOW; tt <= kt_DSTPORT; tt++) {
            splay_tr_graph(bkt, tt);
        }

        // TODO: This for each index, plus the timeorder chain.
        walk_and_free(bkt->indexes->srcv4, kt_SRCv4);
        walk_and_free(bkt->indexes->srcv6, kt_SRCv6);
        walk_and_free(bkt->indexes->dstv4, kt_DSTv4);
        walk_and_free(bkt->indexes->dstv6, kt_DSTv6);
        walk_and_free(bkt->indexes->srcport, kt_SRCPORT);
        walk_and_free(bkt->indexes->dstport, kt_DSTPORT);
        walk_and_free(bkt->indexes->flows, kt_FLOW);
        struct flow_list_node * flow_ln, * n_flow_ln;
        flow_ln = bkt->indexes->timeorder_head;
        while (flow_ln != NULL) {
            n_flow_ln = flow_ln->next;
            free(flow_ln);
            flow_ln = n_flow_ln;
        }

        //PERR("clear_buckets: Clearing bucket at %p.\n", bkt);
        // Put each bucket in the chain back on the ready queue.
        while (bkt != NULL) {
            // The next_bkt variable is required, since it's possible that
            // the bkt could be retreived from the queue and cleared before
            // we grab the next bucket in the chain.
            next_bkt = bkt->next;
            bkt->next = NULL;
            //PERR("clear_buckets: Pushing cleared bucket.\n");
            bucketq_push(&state->ready_bkts, bkt);
            bkt = next_bkt;
        }
    }
    return NULL;
}

void walk_and_free(struct index_node * node, keytype_t tt) {
    if (node == NULL) {
        return;
    }

    if (node->left != NULL) {
        walk_and_free(node->left, tt);
    }

    if (node->right != NULL) {
        walk_and_free(node->right, tt);
    }

    struct pkt_list_node *pkt_ln, *next_pkt_ln;
    struct flow_list_node *flow_ln, *next_flow_ln;

    switch (tt) {
        case kt_FLOW:
            pkt_ln = node->ll.pkts.first;
            while (pkt_ln != NULL) {
                next_pkt_ln = pkt_ln->next;
                free(pkt_ln);
                pkt_ln = next_pkt_ln;
            }
            break;
        default:
            flow_ln = node->ll.flows.first;
            while (flow_ln != NULL) {
                next_flow_ln = flow_ln->next;
                free(flow_ln);
                flow_ln = next_flow_ln;
            }
    }
    free(node);
    return;
}