#include "ordered_set.h"
#include <fcntl.h>
#include "../output.h"
#include "search.h"

void usage() {
    fprintf(stderr,
    "pull_packets <pcapdb_path> <output_file> <ordered_flow_set> <fcap_file> [index_id]...\n\n"
    "Extract the flows specified by the given flow set from the fcap file, and \n"
    "then write them, time-ordered by packet, as a pcap file.\n");
}


#define OUT_PCAP_PERMS S_IRUSR|S_IWUSR|S_IRGRP|S_IRGRP
int main(int argc, char **argv) {
    int ret;

    if (argc < 6) {
        usage();
        return EINVAL;
    }

    char * pcapdb_path = malloc(strlen(argv[1]) + strlen(CAPTURE_DIR_NAME) + strlen(INDEX_DIR_NAME) + 2);
    sprintf(pcapdb_path, "%s/%s/%s", argv[1], CAPTURE_DIR_NAME, INDEX_DIR_NAME);

    char * pcap_fn = argv[2];
    char * flows_fn = argv[3];
    char * fcap_fn = argv[4];
 
    ERR("%s %s %s %s %s %s", argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);

    int index = 5;
    while (index < argc) { // Iterate through index files
        char * flows_path = malloc(strlen(pcapdb_path) + UINT64_STR_LEN + strlen(flows_fn) + 2);
        sprintf(flows_path, "%s/%020lu/%s", pcapdb_path, (uint64_t) atoi(argv[index]), flows_fn);

        char * fcap_path = malloc(strlen(pcapdb_path) + UINT64_STR_LEN + strlen(fcap_fn) + 2);
        sprintf(fcap_path, "%s/%020lu/%s", pcapdb_path, (uint64_t) atoi(argv[index]), fcap_fn);

        char * pcap_path = malloc(strlen(pcapdb_path) + UINT64_STR_LEN + strlen(pcap_fn) + 2);
        sprintf(pcap_path, "%s/%020lu/%s", pcapdb_path, (uint64_t) atoi(argv[index]), pcap_fn);

        ret = pcap_fetch(flows_path, fcap_path, 0, pcap_path);
        if (ret != 0) return ret;
        index++;

        free(flows_path);
        free(fcap_path);
        free(pcap_path);
    }

    free(pcapdb_path);
    return 0;
}
