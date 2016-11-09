#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../pcapdb.h"

// All the tests expect a similar set of arguments.
// In addition to several flags, many tests require the path to a pcap file.
// If this is given, it is returned (otherwise NULL);
char * test_args(int argc, char **argv, struct system_state * state) {
    int c;

    const char * OPTIONS = "tsC:";

    c = getopt(argc, argv, OPTIONS);
    while (c != -1) {
        switch (c) {
            case 't':
                // Use tiny buckets
                state->conf.bucket_pages = 2;
                break;
            case 's':
                state->conf.bucket_mem_type = MEM_MODE_SYS_MEM;
                break;
            case 'C':
                if (chdir(optarg) != 0) {
                    CRIT("Could not set working directory: %s", strerror(errno));
                    return NULL;
                }
                break;

            default:
                break;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    if (optind == argc - 1) {
        return argv[optind];
    } else {
        return NULL;
    }
};



void usage() {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "test_cmd [OPTION]... [PCAP_FILE]\n\n");
    fprintf(stderr, "-t : Use tiny buckets (Two hugepages instead of 128)");
    fprintf(stderr, "-s<bkts>: Create <bkts> worth of buckets in system memory "
                               "instead of huge pages");
}

