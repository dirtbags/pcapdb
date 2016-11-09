//
// Created by pflarr on 10/30/16.
//

#include "ordered_set.h"
#include "../keys.h"
#include "../network.h"

#include <errno.h>
#include <stdio.h>
#include <getopt.h>

void usage() {
    printf("flow_set_print -n <flow_set>\n"
           "Print the given flow set. -n prepends each line with a line #.\n");
}

int main(int argc, char **argv) {
    int opt;
    int number_lines = 0;

    openlog("read_flows", LOG_NDELAY | LOG_PERROR, SYSLOG_FACILITY);

    const char OPTIONS[] = "n";

    opt = getopt(argc, argv, OPTIONS);
    while (opt != -1) {
        switch (opt) {
            case 'n':
                number_lines = 1;
                break;
            default:
                printf("Invalid option: %c", opt);
                usage();
                return EINVAL;
        }
        opt = getopt(argc, argv, OPTIONS);
    }

    if (argc - optind != 1) {
        printf("You must give a flow set file.");
        usage();
        return EINVAL;
    }

    struct ordered_set * oset = calloc(1, sizeof(struct ordered_set));
    int ret = ord_set_init(oset, OSET_FLOW, OSET_READ, argv[optind]);

    struct fcap_flow_rec * rec;
    ret = ord_set_pop(oset, rec);
    uint64_t line_num = 0;
    while (ret != OSET_EMPTY) {
        union kt_ptrs ptrs;
        ptrs.flow = &rec->key;
        if (number_lines) {
            printf("%lu ", line_num++);
        }
        printf("%s\n", kt_key_str(ptrs, kt_FLOW));

        ret = ord_set_pop(oset, rec);
    }

    ord_set_cleanup(oset);
}