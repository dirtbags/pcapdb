// This is for throughput testing just the capture threads

#include <stdlib.h>
#include "../capture.h"
#include "../pcapdb.h"
#include "../bucketize.h"
#include "../pcapdb_init.h"

int main(int argc, char ** argv) {

    struct system_state state;
    system_state_init(&state);

    struct config * conf = &state.conf;

    conf->bucket_mem_type = MEM_MODE_SYS_MEM;
#ifdef USE_CAP_MODE_PFRING
    conf->capture_mode = CAP_MODE_PFRING;
#else
    conf->capture_mode = CAP_MODE_LIBPCAP;
#endif
    conf->max_system_buckets = 4;
    conf->bucket_pages = 128;
    conf->outfile_size = HUGE_PAGE_SIZE * conf->bucket_pages * 3;

    if (argc < 3) {
        printf("Usage: %s <iface> <seconds>\n", argv[0]);
        printf("  iface - The interface to capture from.\n");
        printf("  seconds - How long to capture, in seconds.\n");
        return -1;
    }

    allocate_sysmem_buckets(&state);

    struct capture_state * cap_state = capture_state_init(argv[1], &state);
    int ret = prepare_interface(cap_state);
    if (ret != 0) return ret;

    time_t runtime = atoi(argv[2]);
    pthread_t capture_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int res = pthread_create(&capture_thread, &attr, capture, (void *)&state);
    struct timespec sleepytime;
    sleepytime.tv_sec = 0;
    sleepytime.tv_nsec = 1000000;

    time_t now = time(NULL);
    time_t end_time = now + runtime;

    printf("Capturing\n");
    while (now < end_time) {
        struct bucket * bkt = queue_pop(&state.filled_bkts, Q_NOWAIT);
        // Immediately recycle any filled buckets.
        if (bkt != NULL) {
            nw_stats_print(&state, bkt->stats);
            printf("\n\n");
            while (bkt != NULL) {
                printf("Emptying a bucket.\n");
                struct bucket *next_bkt = bkt->next;
                bucket_reset(bkt, &state.conf);
                bucketq_push(&state.ready_bkts, bkt);
                bkt = next_bkt;
            }
        }
        // Sleep for a millisecond at a time, to keep us from pegging the proc.
        nanosleep(&sleepytime, NULL);
        now = time(NULL);
    }

    event_set(&state.shutdown);
    pthread_join(capture_thread,NULL);
    struct bucket * bkt;
    while (queue_count(&state.filled_bkts) != 0) {
        bkt = queue_pop(&state.filled_bkts, Q_NOWAIT);
        nw_stats_print(&state, bkt->stats);
        while (bkt != NULL) {
            struct bucket *next_bkt = bkt->next;
            bucket_reset(bkt, &state.conf);
            bucketq_push(&state.ready_bkts, bkt);
            bkt = next_bkt;
        }
    }

    bucketq_free(&state.ready_bkts, &state);
    bucketq_free(&state.filled_bkts, &state);
    bucketq_free(&state.indexed_bkts, &state);

    if (cap_state->head_bkt != NULL) {
        printf("Head bucket not pushed!\n");
        nw_stats_print(&state, cap_state->head_bkt->stats);
        return -1;
    }
}
