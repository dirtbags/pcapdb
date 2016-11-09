#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "../pcapdb.h"
#include "../pcapdb_init.h"
#include "../bucketize.h"
#include "test_args.h"


void * clear_buckets(void * thr_init);

// Runs the memory memory allocation function
int main(int argc, char ** argv) {
    uint64_t buckets_allocated = 0;
    uint64_t buckets_freed = 0;
    struct system_state state;
    system_state_init(&state);
    pthread_t clr_bkts_thr;
    pthread_attr_t thr_attr;

    fprintf(stderr, "\n### Bucketize Test###\n");

    state.conf.bucket_pages = 4;
    state.conf.bucket_mem_type = MEM_MODE_SYS_MEM;
    state.conf.max_system_buckets = 128;
    state.conf.capture_mode = CAP_MODE_FILE;
    state.conf.outfile_size = HUGE_PAGE_SIZE*8;

    char * pcap_fn = test_args(argc, argv, &state);
    printf("file: %s\n", pcap_fn);
    if (pcap_fn == NULL) {
        usage();
        return 1;
    }

    struct capture_state * cap_state = capture_state_init(pcap_fn, &state);

    // Make as many buckets as possible.
    allocate_sysmem_buckets(&state);
    buckets_allocated = queue_count(&state.ready_bkts);

    fprintf(stderr, "Buckets Allocated: %lu\n", buckets_allocated);
    if (buckets_allocated == 0) {
        fprintf(stderr, "No buckets were allocated. Has this host had at least %lu "
               "huge pages set aside at boot?\n", state.conf.bucket_pages);
        return 1;
    }
   
    fprintf(stderr, "Starting buckets: (Filled: %lu, Ready: %lu)\n",
                queue_count(&state.filled_bkts), queue_count(&state.ready_bkts));
    printf("file: %s\n", cap_state->interface);
    int ret = prepare_interface(cap_state);
    if (ret != 0) {
        fprintf(stderr, "Could not prepare interface: %s, %s\n",
                strerror(ret), cap_state->interface);
        return EIO;
    }

    pthread_attr_init(&thr_attr);
    pthread_create(&clr_bkts_thr, &thr_attr, clear_buckets, (void *)&state);

    fprintf(stderr, "Handing off to pcap_dispatch.\n");
    capture((void *) cap_state);

    event_set(&state.shutdown);
    pthread_join(clr_bkts_thr, NULL);
    pthread_attr_destroy(&thr_attr);
    fprintf(stderr, "Freeing buckets: (Filled: %lu, Ready: %lu)\n",
                queue_count(&state.filled_bkts), queue_count(&state.ready_bkts));
    buckets_freed += bucketq_free(&state.ready_bkts, &state);
    buckets_freed += bucketq_free(&state.filled_bkts, &state);
    if (buckets_freed != buckets_allocated) {
        fprintf(stderr, "Not all buckets accounted for.\n");
        fprintf(stderr, "  Allocated: %lu\n", buckets_allocated);
        fprintf(stderr, "  Freed:     %lu\n", buckets_freed);
        return 1;
    }

    close_interface(cap_state);

    return 0;
}

void * clear_buckets(void * thr_init) {
    struct system_state * state = thr_init;
    uint64_t bkts_filled = 0;

    FILE * outfile = fopen("/tmp/bktz_test.out", "w");

    // Keep going until we're told to shutdown and there aren't any buckets
    // in the filled queue.
    while (event_check(&state->shutdown) == 0) {
        struct bucket * bkt;
        struct bucket * next_bkt;

        bkt = bucketq_pop(&state->filled_bkts);
        bkts_filled++;
        // Put each bucket in the chain back on the ready queue.
        while (bkt != NULL) {
            // The next_bkt variable is required, since it's possible that
            // the bkt could be retreived from the queue and cleared before
            // we grab the next bucket in the chain.
            next_bkt = bkt->next;
            bkt->next = NULL;
            bucketq_push(&state->ready_bkts, bkt);
            bkt = next_bkt;
        }
    }
    fclose(outfile);
    fprintf(stderr, "Buckets filled: %lu\n", bkts_filled);
    return NULL;
}
