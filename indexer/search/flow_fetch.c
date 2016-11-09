#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include "../pcapdb.h"
#include "../keys.h"
#include "../output.h"
#include "search.h"

void usage() {
    fprintf(stderr,
    "pull_flow_records [options] <pcapdb_path> <flow_index_results> <result_name>"
            "[index files]...\n"
    "    Jump to the offsets listed in the offset_file, and grab \n"
    "    each cooresponding flow record. The flow records are written in flow order\n"
    "    to the output_file.\n\n"
    "  Options:\n"
    "    -s <start_ts>  Flows must end after this time to be retreived.\n"
    "    -e <end_ts>    Flows must start before this time to be retreived.\n"
    "    -p <proto>     Only retreive flows with this transport proto (default all)\n\n"
    "  Timestamps are expected to be in UTC epoch time with decimal microseconds.\n"
    "  For example: 1454509487.534286\n"
    );
}

#define FLOW_OK 0
#define FLOW_DISCARD 1
inline int filter_flow(
        struct fcap_flow_key *,
        struct timeval32 *,
        struct timeval32 *,
        uint8_t);

int main(int argc, char ** argv) {
    int c, tmp;
    const char OPTIONS[] = "s:e:p:";

    struct search_t search;
    search.start_ts.tv_sec = 0;
    search.start_ts.tv_usec = 0;
    search.end_ts.tv_sec = UINT32_MAX;
    search.end_ts.tv_usec = UINT32_MAX;
    search.proto = 0;

    c = getopt(argc, argv, OPTIONS);
    while (c != -1) {
        switch (c) {
            case 's':
                if (parse_ts(&search.start_ts, optarg) != 0) {
                    return EINVAL;
                }
                break;
            case 'e':
                if (parse_ts(&search.end_ts, optarg) != 0) {
                    return EINVAL;
                }
                break;
            case 'p':
                tmp = atoi(optarg);
                if (tmp < UINT8_MAX && tmp >= 0) {
                    search.proto = (uint8_t) tmp;
                } else {
                    CRIT("Bad protocol: %s", optarg);
                    return EINVAL;
                }
                break;
            default:
                CRIT("Invalid arguments");
                return EINVAL;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    if (argc < 3) {
        usage();
        return EINVAL;
    }

    char * pcapdb_dir = argv[optind];
    char * flow_index_result_name = argv[optind+1];
    char * result_name = argv[optind+2];

    int index = optind + 3;
    
    while (index < argc) { // Iterate through index files

        // Convert the index_id to an int to make sure it's sane.
        uint64_t index_id = strtoul(argv[index], NULL, 10);
        if (index_id == UINT64_MAX && (errno == ERANGE || errno == EINVAL)) {
            ERR("Invalid index id: %s", argv[index]);
            return EINVAL;
        }

        // Make the paths to the index directory and the result file.
        char * index_path = make_index_path(pcapdb_dir, index_id);
        char * result_path = make_path(index_path, result_name, NULL);
        char * flow_index_results_path = make_path(index_path, flow_index_result_name, NULL);
        char * flow_index_path = make_path(index_path, (char *)kt_name(kt_FLOW), NULL);
        off_t total_size;

        int ret = flow_fetch(&search, flow_index_results_path, flow_index_path, &total_size,
                       result_path);
        if (ret != 0) {
            ERR("Error fetching flows from: %s", flow_index_path);
            return ret;
        }

        free(index_path);
        free(result_path);
        free(flow_index_results_path);
        free(flow_index_path);

        index++;
    }
}
