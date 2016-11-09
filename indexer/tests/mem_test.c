#include <stdio.h>
#include "../pcapdb.h"
#include "../pcapdb_init.h"
#include "../bucketize.h"
#include "test_args.h"

// Runs the memory memory allocation function
int main(int argc, char ** argv) {
    uint64_t sys_mem = 0;
    uint64_t huge_mem = 0;
    struct system_state state;
    INFO("### MEM_TEST ###\n");

    system_state_init(&state);

    if (test_args(argc, argv, &state) != NULL) {
        return 1;
    };

    state.conf.max_system_buckets = 4;
    state.conf.bucket_mem_type = MEM_MODE_HUGE_PAGES;

    // Allocate buckets from both memory types.
    allocate_hugepage_buckets(&state);
    huge_mem = queue_count(&state.ready_bkts);
    printf("Huge Buckets Allocated: %lu\n", huge_mem);
    bucketq_free(&state.ready_bkts, &state);

    state.conf.bucket_mem_type = MEM_MODE_SYS_MEM;
    allocate_sysmem_buckets(&state);
    sys_mem = queue_count(&state.ready_bkts);
    printf("Sys Buckets Allocated: %lu\n", sys_mem);
    bucketq_free(&state.ready_bkts, &state);

    if (sys_mem == 0 || huge_mem == 0) {
        printf("No buckets were allocated. Has this host had at least %lu "
               "huge pages set aside at boot?\n", state.conf.bucket_pages);
        return 1;
    }
    return 0;

}
