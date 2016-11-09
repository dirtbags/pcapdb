#include <pthread.h>
#include <stdio.h>
#include <string.h>

// Print errors rather than logging to syslog
#include "../pcapdb.h"
#include "../pcapdb_init.h"
#include "../capture.h"
#include "../index.h"
#include "../output.h"
#include "test_args.h"

int main(int argc, char ** argv) {
    uint64_t buckets_allocated = 0;
    uint64_t buckets_freed = 0;
    struct system_state state;
    system_state_init(&state);
    pthread_t idx_bkts_thr;
    pthread_t output_bkts_thr;
    pthread_attr_t thr_attr[2];

    state.conf.bucket_mem_type = MEM_MODE_SYS_MEM;
    state.conf.bucket_pages = 64;
    state.conf.outfile_size = 1024*1024*1024;
    state.conf.capture_mode = CAP_MODE_FILE;
    state.conf.max_system_buckets = 128;

    strncpy(state.conf.base_data_path, "/tmp/capture/", BASE_DIR_LEN);
    strncpy(state.conf.db_connect_str, "host=localhost user=cap dbname=capture_sys password=ephemereal",
            DB_CONNECT_LEN);

    PERR("\n### Output Test ###");

    // Set the bucket size to pretty small. We'd like the buckets to have to actually rotate,
    // and it's nice to not have to deal in the 'real' memory sizes.
    char * pcap_fn = test_args(argc, argv, &state);
    if (pcap_fn == NULL) {
        ERR("You must supply a pcap file path.");
        usage();
        return -1;
    }

    struct capture_state * cap_state = capture_state_init(pcap_fn, &state);

    // Make as many buckets as possible.
    int ret = allocate_sysmem_buckets(&state);
    if (ret != 0) return ret;

    buckets_allocated = queue_count(&state.ready_bkts);
    PERR("Buckets Allocated: %lu", buckets_allocated);
    if (buckets_allocated == 0) {
        return 1;
    }

    PERR("Starting buckets: (Ready: %lu, Filled %lu, Indexed: %lu)",
            queue_count(&state.ready_bkts),
            queue_count(&state.filled_bkts),
            queue_count(&state.indexed_bkts));
    ret = prepare_interface(cap_state);
    if (ret != 0) return ret;

    struct thread_state * idx_thr_state = thread_state_init(&state);
    struct thread_state * out_thr_state = thread_state_init(&state);

    pthread_attr_init(&thr_attr[0]);
    pthread_create(&idx_bkts_thr, &thr_attr[0], indexer, (void *)idx_thr_state);
    pthread_attr_init(&thr_attr[1]);
    pthread_create(&output_bkts_thr, &thr_attr[1], output, (void *)out_thr_state);

    PERR("Handing off to pcap_dispatch %lx.", pthread_self());
    capture((void *) cap_state);

    // Wait until all the buckets are back in the ready_bkts queue.
    while (queue_count(&state.ready_bkts) < buckets_allocated) {
        // Sleep for one millisecond.
        struct timespec sleepytime = {0,1000000};
        struct timespec rem;
        nanosleep(&sleepytime, &rem);
        //PERR("ready, filled, indexed: %lu, %lu, %lu",
        //        queue_count(&state.ready_bkts), queue_count(&state.filled_bkts),
        //        queue_count(&state.indexed_bkts));
    }

    PERR("Done with buckets.");
    event_set(&idx_thr_state->shutdown);
    event_set(&out_thr_state->shutdown);

    queue_close(&state.ready_bkts);
    queue_close(&state.filled_bkts);
    queue_close(&state.indexed_bkts);

    PERR("Joining.");
    pthread_join(idx_bkts_thr, NULL);
    pthread_join(output_bkts_thr, NULL);
    pthread_attr_destroy(&thr_attr[0]);
    pthread_attr_destroy(&thr_attr[1]);

    PERR("Freeing buckets: (Ready: %lu, Filled %lu, Indexed: %lu)",
                queue_count(&state.ready_bkts),
                queue_count(&state.filled_bkts),
                queue_count(&state.indexed_bkts));
    pcap_close(cap_state->libpcap_if);

    buckets_freed += bucketq_free(&state.ready_bkts, &state);
    buckets_freed += bucketq_free(&state.filled_bkts, &state);
    buckets_freed += bucketq_free(&state.indexed_bkts, &state);
    if (buckets_freed != buckets_allocated) {
        PERR("Not all buckets accounted for.");
        PERR("  Allocated: %lu", buckets_allocated);
        PERR("  Freed:     %lu", buckets_freed);
        return 1;
    }

    return 0;
}
