#include <stdint.h>
#include <stdlib.h>
#include "../network.h"
#include "../output.h"

// This will be used in a context in main where all of these variables are defined.
#define IDX_ERR(msg) printf("%s sub_idx: %s, flow_idx: %s, sub_pos: %ld, flow_pos: %ld\n", \
                            sub_idx_path, flow_idx_path, \
                            ftell(sidx_file) - offset_size - kt_key_size(keytype), \
                            ftell(flow_idx_file) - sizeof(struct fcap_flow_key))

void print_flow(struct fcap_flow_key *flow) {
    char src_ip_buff[INET6_ADDRSTRLEN] = "?";
    char dst_ip_buff[INET6_ADDRSTRLEN] = "?";
    if (flow->src_ip_vers == IPv4) {
        inet_ntop(AF_INET, &flow->src.v4.s_addr, src_ip_buff, INET6_ADDRSTRLEN);
    } else if (flow->src_ip_vers == IPv6) {
        inet_ntop(AF_INET6, &flow->src.v6.s6_addr, src_ip_buff, INET6_ADDRSTRLEN);
    }
    if (flow->dst_ip_vers == IPv4) {
        inet_ntop(AF_INET, &flow->dst.v4.s_addr, dst_ip_buff, INET6_ADDRSTRLEN);
    } else if (flow->src_ip_vers == IPv6) {
        inet_ntop(AF_INET6, &flow->dst.v6.s6_addr, dst_ip_buff, INET6_ADDRSTRLEN);
    }

    fprintf(stderr, "src_ver: %d, src: %s, dst_ver: %d, dst: %s, srcport: %u, "
            "dstport %u\n",
            flow->src_ip_vers, src_ip_buff, flow->dst_ip_vers, dst_ip_buff,
            flow->srcport, flow->dstport);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <index_dir>\n", argv[0]);
        printf("This takes an FCAP index directory and checks each of the sub-indices\n"
               "against the flow index for sanity.\n");
        printf("Return: 0 no errors are detected.\n");
        return EFAULT;
    }

    // Prep the path we're about to use.
    char flow_idx_path[BASE_DIR_LEN*2];
    size_t dir_len = strlen(argv[1]);
    char dir[BASE_DIR_LEN];
    strncpy(dir, argv[1], BASE_DIR_LEN);
    dir[BASE_DIR_LEN-1] = '\0';
    // Get rid of any trailing slashes.
    while (dir[dir_len-1] == '/') {
        dir[dir_len-1] = '\0';
        dir_len--;
    }

    // Get the path to the flow index.
    snprintf(flow_idx_path,BASE_DIR_LEN*2,"%s/%s", dir, kt_name(kt_FLOW));
    FILE * flow_idx_file = fopen(flow_idx_path, "r");
    if (flow_idx_file == NULL) {
        printf("Could not open flow index: %s\n", flow_idx_path);
        return EINVAL;
    }

    struct fcap_idx_header flow_idx_hdr;
    if (fread(&flow_idx_hdr, sizeof(struct fcap_idx_header), 1, flow_idx_file) != 1) {
        printf("Could not read flow index header: %s\n", flow_idx_path);
        return EIO;
    }

    // Start with the type after FLOW (which is the first type).
    keytype_t keytype = kt_FLOW + 1;
    // Go through all the sub-indexes, and check each one.
    for (; keytype < tt_LAST; keytype++) {
        FILE *sidx_file = NULL;
        struct fcap_idx_header sidx_hdr;
        uint8_t sidx_preview[DISK_BLOCK] = {0};
        union kt_ptrs preview;
        preview.generic = sidx_preview;
        size_t preview_item = 0;

        printf("Checking %s sub-index.\n", kt_name(keytype));

        // Generate the path and try to open the sub-index file.
        char sub_idx_path[BASE_DIR_LEN*2];
        snprintf(sub_idx_path,BASE_DIR_LEN*2, "%s/%s",dir, kt_name(keytype));
        sidx_file = fopen(sub_idx_path, "r");
        if (sidx_file == NULL) {
            printf("Could not open sub-index file: %s\n", sub_idx_path);
            return EINVAL;
        }

        // Try to read the header.
        if (fread(&sidx_hdr, sizeof(struct fcap_idx_header), 1, sidx_file) != 1) {
            printf("Could not read header from sub-index file: %s\n", sub_idx_path);
            return EIO;
        }

        // Cache the preview index, if there is one.
        if (sidx_hdr.preview != 0) {
            if (fread(sidx_preview, DISK_BLOCK - sizeof(struct fcap_idx_header), 1, sidx_file) != 1) {
                printf("Could not read preview index for: %s\n", sub_idx_path);
                return EIO;
            }
        }

        // At this point, our position in the sub-index file should be at the either the end of
        // the header or the start of the second block.
        // This is really for debugging.
        long pos = ftell(sidx_file);
        if ((sidx_hdr.preview != 0 && pos != DISK_BLOCK) ||
            (sidx_hdr.preview == 0 && pos != sizeof(struct fcap_idx_header))) {
            printf("Bad position after reading headers: %s\n", sub_idx_path);
            return EFAULT;
        }

        // Now we walk through each entry this sub-index, and make sure it points to what
        // we expect in the flow index.
        uint8_t key_buff[2][16] = {{0},{0}};
        union kt_ptrs sidx_key, last_sidx_key;
        sidx_key.generic = key_buff[0];
        last_sidx_key.generic = key_buff[1];

        uint64_t offset, last_offset = 0;
        size_t offset_size = sidx_hdr.offset64 ? sizeof(uint64_t) : sizeof(uint32_t);

        uint64_t key_count = 0;

        uint64_t pvw_depth = preview_depth(keytype, sidx_hdr.records);

        while (1) {
            if (fread(sidx_key.generic, kt_key_size(keytype), 1, sidx_file) != 1) {
                // We've read our last key.
                break;
            }

            offset = 0; // We have to clear this in case we load a 32 bit int into it.
            if (fread(&offset, offset_size, 1, sidx_file) != 1) {
                printf("Could not read offset, %s, %lu\n", sub_idx_path, ftell(sidx_file));
                return EFAULT;
            }

            // Read the flow key from the flow index.
            fseek(flow_idx_file, (long)offset, SEEK_SET);
            struct fcap_flow_key flow_key;
            if (fread(&flow_key, sizeof(flow_key), 1, flow_idx_file) != 1) {
                printf("Could not read flow at pos %lu in %s.\n", offset, flow_idx_path);
                return EFAULT;
            }

            union kt_ptrs kt_flow_key;
            // If we're checking IP's, make sure the types match.
            if ((keytype == kt_SRCv4 && flow_key.src_ip_vers != IPv4) ||
                (keytype == kt_DSTv4 && flow_key.dst_ip_vers != IPv4) ||
                (keytype == kt_SRCv6 && flow_key.src_ip_vers != IPv6) ||
                (keytype == kt_DSTv6 && flow_key.dst_ip_vers != IPv6)) {
                printf("key_vers: %d, keytype: %d, offset: %lu\n",
                       flow_key.src_ip_vers, keytype, offset);
                print_flow(&flow_key);
                printf("%s sub_idx: %s, flow_idx: %s, sub_pos: %ld, flow_pos: %ld\n",
                            "some message",
                            sub_idx_path, flow_idx_path,
                            ftell(sidx_file) - offset_size - kt_key_size(keytype),
                            ftell(flow_idx_file) - sizeof(struct fcap_flow_key));
                return EFAULT;
            }

            // Put the appropriate key in a more generic object for comparison.
            switch (keytype) {
                case kt_SRCv4:
                    kt_flow_key.v4 = &flow_key.src.v4;
                    break;
                case kt_DSTv4:
                    kt_flow_key.v4 = &flow_key.dst.v4;
                    break;
                case kt_SRCv6:
                    kt_flow_key.v6 = &flow_key.src.v6;
                    break;
                case kt_DSTv6:
                    kt_flow_key.v6 = &flow_key.dst.v6;
                    break;
                case kt_SRCPORT:
                    kt_flow_key.port = &flow_key.srcport;
                    break;
                case kt_DSTPORT:
                    kt_flow_key.port = &flow_key.dstport;
                    break;
                default:
                    printf("Invalid keytype: %u\n", keytype);
                    return EFAULT;
            }

            // Do the actual key comparison. They should always be equal.
            if (kt_key_cmp(sidx_key, kt_flow_key, keytype) != 0) {
                IDX_ERR("Key value mismatch");
                return EFAULT;
            }

            // Make sure our offsets are in order
            if (offset <= last_offset) {
                IDX_ERR("Out of order offsets.");
                printf("offset, last_offset: %lu, %lu", offset, last_offset);
                return EFAULT;
            }

            // Make sure the keys are in order within the sub index.
            if (kt_key_cmp(sidx_key, last_sidx_key, keytype) == -1) {
                IDX_ERR("Out of order keys.");
                return EFAULT;
            }

            // Check the preview index to make sure it makes sense.
            if (sidx_hdr.preview == 1 &&
                    node_depth(key_count, sidx_hdr.records) <= pvw_depth) {
                union kt_ptrs pidx_key;
                // There is a preview index, and this node should be in it.
                switch (keytype) {
                    case kt_SRCv4:
                    case kt_DSTv4:
                        pidx_key.v4 = &preview.v4[preview_item];
                        break;
                    case kt_SRCv6:
                    case kt_DSTv6:
                        pidx_key.v6 = &preview.v6[preview_item];
                        break;
                    case kt_SRCPORT:
                    case kt_DSTPORT:
                        pidx_key.port = &preview.port[preview_item];
                        break;
                    default:
                        return EFAULT;
                }
                // Make sure the keys match.
                if (kt_key_cmp(sidx_key, pidx_key, keytype) != 0) {
                    IDX_ERR("Mismatch with preview tree.");
                    return EFAULT;
                }
            }


            // Swap our key buffers. Basically, set last_sidx_key = sidx_key by swapping data
            // buffers.
            if (sidx_key.generic == key_buff[0]){
                sidx_key.generic = key_buff[1];
                last_sidx_key.generic = key_buff[0];
            } else {
                sidx_key.generic = key_buff[0];
                last_sidx_key.generic = key_buff[1];
            }

            key_count++;
        }

        // Make sure the number of keys we have matches the number we expected.
        if (key_count != sidx_hdr.records) {
            printf("Record count doesn't match expectations. Expected %lu, got %lu.\n",
                    sidx_hdr.records, key_count);
            return EFAULT;
        }

    }

    // Nothing went wrong, so everything went right.
    return 0;
}