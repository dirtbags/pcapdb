#include "../output.h"
#include "../network.h"
#include "../pcapdb_init.h"

#include <stdlib.h>
#include <sys/stat.h>

#define RECURSE_LIMIT 500000
int main() {
    struct save_info save = {0,"","","","","/tmp/"};

    struct system_state state;
    system_state_init(&state);
    struct config * conf = &state.conf;


    struct packet_record * pkt = calloc(1, sizeof(struct packet_record));
    // Stack based initialization would have been better, but valgrind complains.
    pkt->header.ts.tv_sec = 1498;
    pkt->header.ts.tv_usec = 1234;
    pkt->header.caplen = pkt->header.len = 1500;
    pkt->src.vers = IPv4;
    pkt->src.addr.v4.s_addr = 0x34765049;
    pkt->dst.vers = IPv4;
    pkt->dst.addr.v4.s_addr = 0x7f000001;
    pkt->srcport = 80;
    pkt->dstport = 8080;
    pkt->proto = IPPROTO_TCP;
    pkt->packet = 0;

    struct index_node flow = {NULL,NULL,NULL,{NULL,NULL}, 0x7473666f};

    struct index_node * root = NULL;

    struct timeval32 start_ts = {0x5354, 0x54524154};
    struct timeval32 end_ts = {0x5354, 0x444E45};

    // Make the test directory
    mkdir(save.index_path, 0777);

    uint64_t i;
    struct index_node * last_node = NULL;
    for (i=0; i<RECURSE_LIMIT; i++) {
        struct index_node * next_node = calloc(1, sizeof(struct index_node));
        // There may be warnings about values potentially escaping the scope of this function,
        // but those don't matter since this is main.
        next_node->key = pkt;
        next_node->ll.flows.first = calloc(1, sizeof(struct flow_list_node));
        next_node->ll.flows.first->flow = &flow;
        if (root == NULL) {
            root = next_node;
        } else {
            last_node->left = next_node;
        }
        last_node = next_node;
    }

    struct index_set idx_set = {0};
    idx_set.srcv4 = root;
    idx_set.srcv4_cnt = RECURSE_LIMIT;

    output_code_t ret;
    ret = write_index(conf, &save, &idx_set, kt_SRCv4, &start_ts, &end_ts);

    free(pkt);
    return ret;

}