#include <stdlib.h>
#include <stdio.h>
#include "../ordered_set.h"
#include "../../pcapdb.h"
#include "../../output.h"
#include "../../keys.h"
#include <inttypes.h>
#include "../../network.h"
#include <arpa/inet.h>
#include <string.h>

void usage() {
    fprintf(stdout, "generate_flows <output_file> <size>");
}


int main(int argc, char **argv) {

    if (argc != 3) {
        usage();
        return EINVAL;
    }

    char * out_fn = argv[1];
    int size = atoi(argv[2]);

    if (size % 72 != 0) {
        size -= size % 72;
    }

    struct ordered_set * out_set;

    out_set = calloc(1, sizeof(struct ordered_set));
    int ret = ord_set_init(out_set, OSET_FLOW, OSET_WRITE, out_fn);
    if (ret != 0) {
        if (ret == EEXIST) {
            return 0;
        } else {
            CRIT("Unknown error opening output file: %s, error %s", out_fn, strerror(errno));
            return EINVAL;
        }
    }

    srand(100);

    int index = 0;
    while (index < size) {
        struct fcap_flow_rec flow;
        flow.key.src.v4.s_addr = (uint32_t)rand()%4294967295;
        flow.key.dst.v4.s_addr = (uint32_t)rand()%4294967295;
        flow.key.src_ip_vers = 4;
        flow.key.dst_ip_vers = 4;
        flow.key.srcport = (uint8_t)rand()%65535;
        flow.key.dstport = (uint8_t)rand()%65535;
        flow.key.packets = (uint32_t)rand()%4294967295;
        flow.key.size = (uint32_t)rand()%4294967295;
        flow.key.first_ts.tv_sec = (uint32_t)rand()%4294967295;
        flow.key.first_ts.tv_usec = (uint32_t)rand()%4294967295;
        flow.key.last_ts.tv_sec = (uint32_t)rand()%4294967295;
        flow.key.last_ts.tv_usec = (uint32_t)rand()%4294967295;

        ret = ord_set_push(out_set, &flow);
        if (ret != 0) return ret;
        index += 72;
    }

    ord_set_cleanup(out_set);
}
