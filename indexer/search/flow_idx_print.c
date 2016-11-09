//
// Created by pflarr on 4/15/16.
//

#define PRINT_LOGS

#include <fcntl.h>
#include <stdio.h>
#include "../pcapdb.h"
#include "../keys.h"
#include "../output.h"
#include "search.h"

struct record {
    struct fcap_flow_key key;
    uint64_t offset;
};

int main(int argc, char ** argv) {
    if (argc != 2) {
        printf("Usage: %s <flow_index>\n", argv[0]);
        printf("   Pretty print each flow record in the given index file.\n");
        return -1;
    }

    int flow_idx_fd = open(argv[1], O_RDONLY);

    struct fcap_idx_header hdr;
    if (safe_read(flow_idx_fd, &hdr, sizeof(struct fcap_idx_header)) != 0) {
        ERR("Could not read header.");
    }

    size_t offset_size = hdr.offset64 ? sizeof(uint64_t) : sizeof(uint32_t);

    struct record rec;
    while (1) {
        rec.offset = 0; // We're using this to hold either 32 or 64 bits, so it has to be cleared.
        if (safe_read(flow_idx_fd, &rec, sizeof(struct fcap_flow_key) + offset_size) != 0) {
            if (lseek(flow_idx_fd, 0, SEEK_CUR) != lseek(flow_idx_fd, 0, SEEK_END)) {
                ERR("File truncated.");
                return -1;
            }
            // We're at the end of the file.
            break;
        }

        printf("%09d.%09d %09d.%09d ",
               rec.key.first_ts.tv_sec, rec.key.first_ts.tv_usec,
               rec.key.last_ts.tv_sec, rec.key.last_ts.tv_usec);
        ip_addr_t src = {rec.key.src, rec.key.src_ip_vers};
        printf("%s:%d -> ", iptostr(&src), rec.key.srcport);
        ip_addr_t dst = {rec.key.dst, rec.key.dst_ip_vers};
        printf("%s:%d ", iptostr(&dst), rec.key.dstport);
        printf("%d %d %d %lu\n", rec.key.proto, rec.key.packets, rec.key.size, rec.offset);
    }

    return 0;
}

