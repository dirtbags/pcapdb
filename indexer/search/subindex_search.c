//
// Created by pflarr on 9/30/16.
//

#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../pcapdb.h"
#include "../keys.h"
#include "../output.h"
#include "search.h"


//#define TERR(...) fprintf(stderr, __VA_ARGS__)
//#define TERR(...)

void usage() {
    fprintf(stderr,
            "Usage: subidx_search <pcapdb_path> <key_type> <result_name> <start_key> <end_key> "
                    "<index>...\n"
                    "Perform a search of a subindex across one or more index directories.\n"
                    "Note that while this works, the fastest way to perform complete searches is \n"
                    "'./search', as it allows for the best use of cached index file data.\n\n"
                    "index_dir - The directory where all the indexes live. (Usually \n"
                    "            /var/pcapdb/capture/index\n"
                    "subindex - The type of index we're searching across. This also corresponds to the \n"
                    "             standard index file names.\n"
                    "output_name - The name of the output file for this search. These are standardized by\n"
                    "              the search system, and in this case are of the form \n"
                    "             '_<INDEX_TYPE>_<start_key>_<end_key>'\n"
                    "start_key - The lowest value to match in the search.\n"
                    "end_key - The highest value to match in the search. To match a single value, we \n"
                    "          do searches such that start_key == end_key.\n"
                    "index - The id of the index file to search. This also corresponds to the directory\n"
                    "        name of that index (with leading zeros).\n\n");

}

int main(int argc, char **argv) {

    int index_fd, index, end_key_exists = 0;
    int ret;

    struct subindex_search_descr *descr = calloc(sizeof(struct subindex_search_descr), 1);

    // Make sure we have enough arguments.
    if (argc < 7) {
        usage();
        return EINVAL;
    }

    // Remap argv to some a new indexes array.
    char **indexes = &argv[6];
    int index_count = argc - 6;

    char *base_path = malloc(strlen(argv[1]) + 1);
    char *subindex = argv[2];
    descr->result_name = malloc(strlen(argv[3]) + 1);

    // Copy a bunch of the args out of argv
    strncpy(base_path, argv[1], strlen(argv[1]));

    descr->type = kt_strtokeytype(subindex);
    if (descr->type == kt_BADKEY) {
        ERR("Invalid key type: %s\n", subindex);
        return EINVAL;
    }

    // Parse the key from human-readable to binary
    // Make a buf to fit the largest sized key.
    descr->start_key.generic = descr->start_buffer;
    ret = kt_key_parse(descr->start_key, descr->type, argv[4]);
    if (ret != 0) return ret;

    descr->end_key.generic = descr->end_buffer;
    ret = kt_key_parse(descr->end_key, descr->type, argv[4]);
    if (ret != 0) return ret;

    size_t i;
    for (i=5; i < argc; i++) {
        size_t idx_path_len = (strlen(base_path) + 1 + 20 + 1);

        uint64_t index_id = strtoul(argv[i], NULL, 10);
        if (index_id == ULONG_MAX && (errno == ERANGE || errno == EINVAL)) {
            ERR("Invalid index id: %s", argv[i]);
            return EINVAL;
        }
        char * index_path = make_index_path(base_path, index_id);
        char * result_path = make_path(index_path, descr->result_name, NULL);

        ret = search_subindex(descr, index_path, result_path);
        if (ret != 0) {
            ERR("Failed searching in index %s of %s.", index_path, kt_name(descr->type));
            return ret;
        }
    }


}