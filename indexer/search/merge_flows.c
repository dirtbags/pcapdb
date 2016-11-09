#include "../output.h"
#include "ordered_set.h"
#include "search.h"
#include <getopt.h>

void usage() {
    fprintf(stderr,
    "merge <output_path> [-p <PCAPDB_PATH>]\n"
    "                    [-r <RESULT_FN>]\n"
    "                    [-f <FLOW_FILES>]...\n"
    "                    <OUTPUT_FILE>\n"
    "                    [<INDEX_ID>...]\n"
    "   Merge the given flow result files (as produced by the pcapdb search or\n"
    "   pull_flows commands).\n"
    " <OUTPUT_FILE>     Where to write the results of this merge (required).\n"
    " <INDEX_ID>        The id of an index to combine results from. All remaining\n"
    "                   arguments after the output file are expected to be these.\n"
    " -p <PCAPDB_PATH>  The path the the pcapdb directory. Defaults to '/var/pcapdb'\n"
    " -r <RESULT_FN>    Filename for results fetched from individual indexes. This \n"
    "                   must be given when index_id's are given. Results are thus looked\n"
    "                   for in the following location for each index id given:\n"
    "                     <pcapdb_path>/capture/index/<index_id>/<result_fn>.flows\n"
    " -f <FLOW_FILE>    Give a flow file name as a normal.\n");
}

int main(int argc, char ** argv) {
    openlog("merge", LOG_NDELAY | LOG_PERROR, SYSLOG_FACILITY);

    int i, res=0;

    // Create our skip list head and initialize it.
    struct os_skip_list skip_list;
    os_slist_init(&skip_list);

    char * pcapdb_path = "/var/pcapdb";
    char * result_fn = NULL;

    struct ordered_set * path_oset;

    const char OPTIONS[] = "p:r:f:h";
    int c = getopt(argc, argv, OPTIONS);
    while (c != -1) {
        switch (c) {
            case 'p':
                pcapdb_path = optarg;
                break;
            case 'r':
                result_fn = optarg;
                break;
            case 'f':
                // Open an input file from a given path as an input file.
                path_oset = calloc(1, sizeof(struct ordered_set));
                res = ord_set_init(path_oset, OSET_FLOW, OSET_READ, optarg);
                if (res != 0) {
                    ERR("Could not open flow file '%s' {%d} (%s)", optarg,
                        access(optarg, F_OK),
                        strerror(errno));
                    return EINVAL;
                }
                os_slist_add(&skip_list, path_oset);
                break;
            case 'h':
                usage();
                break;
            default:
                ERR("Invalid argument: %c", c);
                return EINVAL;
        }

        c = getopt(argc, argv, OPTIONS);
    }

    char * out_fn = NULL;
    if (optind < argc) {
        out_fn = argv[optind++];
    } else {
        ERR("No output file given.");
        return EINVAL;
    }

    struct ordered_set * out_set = calloc(1, sizeof(struct ordered_set));
    int ret = ord_set_init(out_set, OSET_FLOW, OSET_WRITE, out_fn);
    if (ret != 0) {
        if (ret == EEXIST) {
            CRIT("Output file %s already exists.", out_fn);
            return 0;
        } else {
            CRIT("Unknown error opening output file: %s, error: %s", out_fn, strerror(errno));
            return EINVAL;
        }
        fprintf(stderr, "Could not open output file: '%s'. errno: %d\n", out_fn, errno);
        usage();
        return EIO;
    }

    char * result_paths[argc-optind];
    size_t res_c = 0;
    for (i=optind; i < argc; i++) {
        uint64_t index = strtoul(argv[i], NULL, 10);
        if (index == UINT64_MAX) {
            CRIT("Invalid index id: %s", argv[i]);
            return EINVAL;
        }

        char * flow_result_path = make_index_path(pcapdb_path, index);
        result_paths[res_c] = make_path(flow_result_path, result_fn, ".flows");
        free(flow_result_path);

        struct ordered_set * oset = calloc(1, sizeof(struct ordered_set));
        ret = ord_set_init(oset, OSET_FLOW, OSET_READ, result_paths[res_c]);
        if (ret != 0) {
            CRIT("Could not open flow result file: %s", result_paths[res_c]);
            return ret;
        };

        os_slist_add(&skip_list, oset);
        res_c++;
    };

    // Take the union of all the ordered sets.
    if (os_slist_union(&skip_list, out_set) != 0) return EIO;

    for (i=0; i < res_c; i++) free(result_paths[i]);

    res = ord_set_cleanup(out_set);
    if (res != 0) {
        CRIT("Could not successfully save/cleanup the output file: %s", out_set->path);
        return res;
    }

    return 0;
}

