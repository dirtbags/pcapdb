#include <stdlib.h>
#include <hugetlbfs.h>
#include <linux/limits.h>
#include <fcntl.h>
#include "pcapdb.h"
#include "pcapdb_init.h"
#include "bucketize.h"

int system_state_init(struct system_state * state) {
    struct config * conf = &state->conf;
    int i;

    // Get and set our working directory to the SITE_ROOT
    char * site_root = getenv("SITE_ROOT");
    if (site_root != NULL) {
        if (chdir(site_root) != 0) {
            CRIT("Invalid SITE_ROOT: %s", site_root);
        }
    }

    char cwd[BASE_DIR_LEN];
    if (getcwd(cwd, BASE_DIR_LEN) == NULL) {
        CRIT("Current working directory too long.");
        return EFAULT;
    }

    // Set all the default configuration parameters.
    snprintf(conf->base_data_path, BASE_DIR_LEN, "%s/%s", cwd, CAPTURE_DIR_NAME);
    conf->base_data_path[BASE_DIR_LEN-1] = '\0';
    conf->use_db = CFG_USE_DB_DF;
    snprintf(conf->db_connect_str, DB_CONNECT_LEN, "%s", CFG_DB_CONNECT_STR_DF);
    conf->db_connect_str[DB_CONNECT_LEN-1] = '\0';
    conf->mtu = CFG_MTU_DF;
    conf->capture_uid = CFG_CAPTURE_UID_DF;
    conf->capture_gid = CFG_CAPTURE_GID_DF;
    conf->capture_mode = CFG_CAPTURE_MODE_DF;
    conf->bucket_mem_type = CFG_BUCKET_MEM_TYPE_DF;
    conf->pcap_buffer_mem = CFG_PCAP_BUFFER_MEM_DF;
    conf->max_system_buckets = CFG_MAX_SYSTEM_BUCKETS_DF;
    conf->bucket_pages = CFG_BUCKET_PAGES_DF;
    snprintf(conf->outfile_dir, BASE_DIR_LEN, "%s", CFG_OUTFILE_DIR_DF);
    conf->outfile_dir[BASE_DIR_LEN-1] = '\0';
    conf->outfile_size = CFG_OUTFILE_SIZE_DF;

    event_init(&state->shutdown);
    for (i=0; i < MAX_INDEXING_THREADS; i++) state->capture_threads[i] = NULL;
    state->capture_thread_count = 0;
    for (i=0; i < MAX_INDEXING_THREADS; i++) state->index_threads[i] = NULL;
    state->index_thread_count = 0;
    for (i=0; i < MAX_OUTPUT_THREADS; i++) state->output_threads[i] = NULL;
    state->output_thread_count = 0;
    queue_init(&state->ready_bkts);
    queue_init(&state->filled_bkts);
    queue_init(&state->indexed_bkts);
    // Go ahead and get the real system CPU count.
    state->cpu_count = 0;
    // This will get set iff we actually need it.
    state->lockfile = -1;

    return 0;
}

// Create, initialize, and return a capture state object.
struct capture_state * capture_state_init(char * interface, struct system_state * sys_state) {
    struct capture_state * cs = calloc(1, sizeof(struct capture_state));
    strncpy(cs->interface, interface, BASE_DIR_LEN);
    cs->interface[BASE_DIR_LEN] = '\0';
    cs->head_bkt = cs->current_bkt = NULL;
    cs->sys_state = sys_state;
    return cs;
}

// Creates and returns a thread state object.
struct thread_state * thread_state_init(struct system_state * sys_state) {
    struct thread_state * thr_state = calloc(1, sizeof(struct thread_state));
    event_init(&thr_state->shutdown);
    thr_state->sys_state = sys_state;
    thr_state->status = PCAPDB_THR_IDLE;
    return thr_state;
}

#define MEMINFO_PATH "/proc/meminfo"
#define FAILURE_PAGES 10000
#define MEMINFO_BUFFER_SIZE (size_t)(1<<20) // Make a 1 MB buffer.
#define FREE_HUGE "HugePages_Free:"
#define RSVD_HUGE "HugePages_Rsvd:"
// Allocate as many hugepage buckets as we can, given the available huge pages.
void allocate_hugepage_buckets(struct system_state * state) {
    struct bucket * bkt;
    size_t bkt_size = state->conf.bucket_pages * HUGE_PAGE_SIZE;

    int meminfo_fd = open(MEMINFO_PATH, O_RDONLY);
    char buffer[MEMINFO_BUFFER_SIZE]; // Have enough buffer space for a 1 MB file.
    uint32_t b;
    // Clear the buffer
    for (b=0; b < MEMINFO_BUFFER_SIZE; b++) {
        buffer[b] = 0;
    }
    uint64_t free_pages = 0;
    uint64_t rsvd_pages = 0;
    ssize_t bytes_read;
    do {
        bytes_read = read(meminfo_fd, buffer, MEMINFO_BUFFER_SIZE-1);
    } while (bytes_read == -1 && errno == EINTR);
    if (bytes_read != -1) {
        // Only do this if we could read the file.
        // Find the key strings for each of the values we're interested in and extract that value.
        char * pos = strstr(buffer, FREE_HUGE);
        if (pos != NULL) {
            // Skip the field name
            pos += strlen(FREE_HUGE);
            free_pages = strtoul(pos, NULL, 16);
        }
        pos = strstr(buffer, RSVD_HUGE);
        if (pos != NULL) {
            pos += strlen(RSVD_HUGE);
            rsvd_pages = strtoul(pos, NULL, 16);
        }
    }
    close(meminfo_fd);

    // If we couldn't get a number of free pages, just assume there are a bunch.
    // We won't be able to allocate any bucket, but we can still try.
    if (free_pages == 0) free_pages = FAILURE_PAGES;

    // Reserved pages are 'free' but already claimed by a process (but not yet written to).
    free_pages -= rsvd_pages;

    // Make buckets as long as we can get the memory for them.
    while (free_pages >= state->conf.bucket_pages) {
        bkt = get_huge_pages(bkt_size, GHP_DEFAULT);
        if (bkt == NULL) {
            break;
        }
        bucket_init(bkt);
        bucketq_push(&state->ready_bkts, bkt);
        free_pages -= state->conf.bucket_pages;
    }
    return;
}

// Allocate system memory buckets according to configuration parameters
int allocate_sysmem_buckets(struct system_state * state) {
    struct bucket * bkt;
    size_t bkt_size = state->conf.bucket_pages * HUGE_PAGE_SIZE;
    int bkts_allocated;

    // The limit has to be set in configuration.
    for (bkts_allocated=0; bkts_allocated < state->conf.max_system_buckets; bkts_allocated++) {
        bkt = malloc(bkt_size);
        if (bkt == NULL) {
            CRIT("Could not allocate system memory bucket.");
            return EFAULT;
        }
        bucket_init(bkt);
        bucketq_push(&state->ready_bkts, bkt);
    }


    return 0;
}

// Makes sure the interface name only uses allowed characters
int check_iface_name(char * iface_name) {
    size_t len = strlen(IFACE_ALLOWED_CHARS);
    size_t i,c;

    for (c=0; iface_name[c] != '\0'; c++) {
        int found = 0;

        for (i=0; i < len; i++) {
            if (iface_name[c] == IFACE_ALLOWED_CHARS[i]) {
                found = 1;
                break;
            }
        }
        if (found == 0) {
            return EINVAL;
        }
    }
    return 0;
}

// Get the interface described in cap_state ready for operation.
int prepare_interface(struct capture_state *cap_state) {
    char errbuf[PCAP_ERRBUF_SIZE];
    struct system_state * state = cap_state->sys_state;

    // Make sure we even have an interface name.
    if (cap_state->interface[0] == '\0') {
        CRIT("Empty capture interface name.");
        return EINVAL;
    }

    fflush(stdout);

    switch (state->conf.capture_mode) {
        case CAP_MODE_FILE:
            cap_state->libpcap_if = pcap_open_offline(cap_state->interface, errbuf);
            if (cap_state->libpcap_if == NULL) {
                CRIT("Could not open pcap file %s.", cap_state->interface);
                return EINVAL;
            }
            break;
        case CAP_MODE_LIBPCAP:
            // Assume the last argument is an cap_state->interface name. Attempt to open and activate it.
            cap_state->libpcap_if = pcap_create(cap_state->interface, errbuf);
            if (cap_state->libpcap_if == NULL) {
                CRIT("Could not open interface %s for capture: %s.",
                        cap_state->interface, errbuf);
                return EINVAL;
            }

            //set_tstamp_type(cap_state->libpcap_if);
            pcap_set_promisc(cap_state->libpcap_if, 1);
            pcap_setdirection(cap_state->libpcap_if, PCAP_D_IN);
            pcap_set_snaplen(cap_state->libpcap_if, state->conf.mtu);
            pcap_set_buffer_size(cap_state->libpcap_if, state->conf.pcap_buffer_mem);

            int act_ret = pcap_activate(cap_state->libpcap_if);
            switch (act_ret) {
                case 0:
                    // Everything is fine.
                    break;
                case PCAP_WARNING_PROMISC_NOTSUP:
                    PERR("Iface (%s) does not support promiscuous mode.", cap_state->interface);
                    return EINVAL;
                case PCAP_WARNING_TSTAMP_TYPE_NOTSUP:
                    PERR("Timestamp type reported as supported could not be set.\n");
                    // This isn't fatal, just bad.
                    break;
                case PCAP_WARNING:
                    PERR("Generic pcap activate warning: %s\n", pcap_geterr(cap_state->libpcap_if));
                    break;
                case PCAP_ERROR_NO_SUCH_DEVICE:
                case PCAP_ERROR_PERM_DENIED:
                case PCAP_ERROR_PROMISC_PERM_DENIED:
                    PERR("Error opening cap_state->interface (%s):\n%s\n",
                            cap_state->interface, pcap_geterr(cap_state->libpcap_if));
                    return EINVAL;
                default:
                    PERR("Unknown pcap activate warning: %s\n", pcap_geterr(cap_state->libpcap_if));
                    return EINVAL;
            }
            break;
        case CAP_MODE_PFRING:
        case CAP_MODE_PFRING_ZC:
            // There's no difference when setting up a pfring interface for zero-copy or non zero-
            // copy modes. The main difference occurs when we request packets.
            cap_state->pfring_if = pfring_open(cap_state->interface, state->conf.mtu,
                    PF_RING_DO_NOT_PARSE | // We'll parse the packet into our own structure.
                    PF_RING_HW_TIMESTAMP | // Try to use hardware timestamps.
                    PF_RING_PROMISC);      // Open the cap_state->interface in promiscuous mode.

            if (cap_state->pfring_if == NULL) {
                CRIT("Could not open interface (%s), error: %s.",
                        cap_state->interface, strerror(errno));
                return EINVAL;
            }

            // Try to enable hardware timestamps./
            // This will definitely fail for zero-copy cap_state->interfaces.
            pfring_enable_hw_timestamp(cap_state->pfring_if, cap_state->interface, 1, 1);

            pfring_set_cluster(cap_state->pfring_if, 0, cluster_round_robin);

            // We go ahead and enable to the cap_state->interface. We'll lose some packets this way, but
            // we'll lose them anyway if we wait.
            int ret = pfring_enable_ring(cap_state->pfring_if);
            if (ret != 0) {
                CRIT("Could not enable pfring interface (%s), retval: %d.",
                        cap_state->interface, ret);
                return EINVAL;
            };

            break;
        default:
            CRIT("Invalid capture mode: %d", state->conf.capture_mode);
            return EINVAL;
    }
    return 0;
}

// Close the interface used by the given capture state.
void close_interface(struct capture_state *cap_state) {
    switch (cap_state->sys_state->conf.capture_mode) {
        case CAP_MODE_LIBPCAP:
        case CAP_MODE_FILE:
            pcap_close(cap_state->libpcap_if);
            break;
        case CAP_MODE_PFRING:
        case CAP_MODE_PFRING_ZC:
            pfring_close(cap_state->pfring_if);
            break;
        default:
            WARN("Invalid capture mode when closing interface: %d",
                    cap_state->sys_state->conf.capture_mode);
    }
}

// Run the 'core_count' script, which returns the number of cpus on the system.
int get_cpus() {
    char path[PATH_MAX+1];
    char cwd[PATH_MAX+1];
    // TODO check for exceeding path limit
    getcwd(cwd, PATH_MAX);

    snprintf(path, BASE_DIR_LEN, "%s/bin/core_count", cwd);
    path[PATH_MAX] = '\0';
    int status = system(path);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 0;
    }
}

int PCAP_TSTAMP_ORDER[] = {PCAP_TSTAMP_ADAPTER,
        PCAP_TSTAMP_ADAPTER_UNSYNCED,
        PCAP_TSTAMP_HOST_HIPREC,
        PCAP_TSTAMP_HOST,
        PCAP_TSTAMP_HOST_LOWPREC};
int PCAP_TSTAMP_ORDER_LEN = 5;
// Go through the list of available timestamp types, and choose one in the order specified in
// PCAP_TSTAMP_ORDER.
void set_tstamp_type(pcap_t * pcap_in) {
    int * tstamp_types;
    int best_type = PCAP_TSTAMP_ORDER[0];
    int i, j;
    int ntypes = pcap_list_tstamp_types(pcap_in, &tstamp_types);
    if (ntypes == 0) {
        CRIT("Timestamp type cannot be specified.");
    }
    for (i=0; i<PCAP_TSTAMP_ORDER_LEN; i++) {
        best_type = PCAP_TSTAMP_ORDER[i];
        for (j=0; j < ntypes; j++ ) {
            if ( best_type == tstamp_types[j]) {
                goto set_ts_done;  // Jump out of the loop.
            }
        }
    }
    set_ts_done:
    // If no match was found, this will be set in error. At that point,
    // we've failed totally anyway.
    if ( pcap_set_tstamp_type(pcap_in, best_type) !=0 ) {
        ERR( "Error setting timestamp type: %s.\n", pcap_tstamp_type_val_to_name(best_type));
    }
    INFO("Set tstamp to %s.\n", pcap_tstamp_type_val_to_name(best_type));

    pcap_free_tstamp_types(tstamp_types);
}