#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "ordered_set.h"
#include "search.h"
//#define TERR(...) fprintf(stderr, __VA_ARGS__)

void usage() {
    fprintf(stderr, "and_atoms <pcapdb_path> <output_path> -i <input set> [-v <inverted set file>]... <index1> <index2>...\n"
            "   Perform an intersection operation on the offsets in the given search\n"
            "   result files. Items in the inverted set files are removed from the\n"
            "   intersection. At least one input set is required, and exactly one output set.\n");
}

int main(int argc, char ** argv) {


    const char * OPTIONS = "i:v:";
    int opt = getopt(argc, argv, OPTIONS);
    int index;

    size_t subidx_path_count = 0;
    struct and_descr and_op;
    struct and_item_list ** and_items_tail = &and_op.sub_searches;
    struct and_item_list * and_item;
    while (opt != -1) {
        switch (opt) {
            case 'i':
            case 'v':
                // Store the name of a regular input file
                and_item = calloc(sizeof(struct and_item_list), 1);
                if (and_op.sub_searches == NULL) {
                    and_op.sub_searches = and_item;
                }
                size_t result_name_len = strlen(optarg);
                and_item->result_name = malloc(result_name_len + 1);
                strncpy(and_item->result_name, optarg, result_name_len);
                and_item->subindex_search_id = subidx_path_count;
                if (opt == 'i') {
                    and_item->inverted = 0;
                } else {
                    and_item->inverted = 1;
                }
                *and_items_tail = and_item;
                and_items_tail = &and_item->next;
                subidx_path_count++;
                break;
            default:
                CRIT("Bad arguments.");
                usage();
                return EINVAL;
        }
        opt = getopt(argc, argv, OPTIONS);
    }

    if (argc - optind < 3) {
        CRIT("Need one pcapdb_path, one output file, and at least one index file");
        return EINVAL;
    }

    //Get path to pcapdb directory
    char * pcapdb_dir = argv[optind];

    char * result_name = argv[optind+1];

    index = optind + 2;

    char * subidx_res_paths[subidx_path_count];
    while (index < argc) {
        //iterate through all indexes and perform the and operation on each

        // Convert the index_id to an int to make sure it's sane.
        uint64_t index_id = strtoul(argv[index], NULL, 10);
        if (index_id == UINT64_MAX && (errno == ERANGE || errno == EINVAL)) {
            ERR("Invalid index id: %s", argv[index]);
            return EINVAL;
        }

        // Make the paths to the index directory and the result file.
        char * index_path = make_index_path(pcapdb_dir, index_id);
        char * result_path = make_path(index_path, result_name, NULL);

        // Generate the paths to all of the
        int i = 0;
        and_item = and_op.sub_searches;
        while (and_item != NULL) {
            subidx_res_paths[i] = make_path(index_path, and_item->result_name, NULL);
            and_item = and_item->next;
            i++;
        }

        if (and_results(&and_op, subidx_res_paths, result_path) != 0) {
            ERR("Error performing AND operation.");
            return EINVAL;
        }
        free(index_path);
        free(result_name);
        and_item = and_op.sub_searches;
        i = 0;
        while (and_item != NULL) {
            free(subidx_res_paths[i]);
            i++;
            and_item = and_item->next;
        }
        index++;
    }

    // Free all the and_items.
    and_item = and_op.sub_searches;
    while (and_item != NULL) {
        struct and_item_list * next = and_item->next;
        free(and_item);
        and_item = next;
    }

    return 0;
}
