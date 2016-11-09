#include <stdint.h>
#include <stdlib.h>
#include "../network.h"
#include "../output.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <flow_index_file> <fcap_file>\n", argv[0]);
        printf("This takes an FCAP file and flow index file and makes sure\n"
               "the index file is sane.");
        printf("Return: 0 no errors are detected.\n");
    }

    struct network_stats * stats = calloc(1, sizeof(struct network_stats));

    FILE *cap_file = fopen(argv[2], "r");
    if (cap_file == NULL) {
        printf("Could not open capture file: %s\n", argv[2]);
        return EINVAL;
    }

    // We're going to keep track of where we think we are, as well as where
    // the file cursor actually is.
    uint64_t capfile_pos = sizeof(struct pcap_file_header);
    // Read the header section of the capture file.
    struct pcap_file_header fcap_header;
    if (fread(&fcap_header, sizeof(struct pcap_file_header),1, cap_file) !=1) {
        printf("Could not read cap file header.\n");
        return EIO;
    }

    FILE *flow_file = fopen(argv[1], "r");
    if (flow_file == NULL) {
        printf("Could not open flow index file: %s\n", argv[1]);
        return EINVAL;
    }

    struct fcap_idx_header idx_header;
    if (fread(&idx_header, sizeof(struct fcap_idx_header), 1, flow_file) != 1) {
        printf("Could not read flow index header.\n");
        return EIO;
    }

    if (idx_header.preview != 0) {
        printf("Flow indexes should never have a preview index.");
        return EFAULT;
    }

    struct packet_record * rec = calloc(1, sizeof(packet_record) + CFG_MTU_DF);
    struct fcap_flow_key * flow_rec = calloc(1, sizeof(struct fcap_flow_key));
    uint64_t offset;
    size_t off_size = idx_header.offset64 ? sizeof(uint64_t) : sizeof(uint32_t);

    uint64_t flow_count = 0;
    int64_t flow_pos;
    uint64_t pkt_no = 1;

    while (1) {
        flow_pos = ftello(flow_file);

        // Grab the next flow record.
        if (fread(flow_rec, sizeof(struct fcap_flow_key),1,flow_file) != 1) {
            // No more flow records to read.
            break;
        }

        // Clear the offset value.
        offset = 0;
        // Grab the offset value.
        // This will work regardless of whether it's a 32 or 64 bit offset, but only
        // on little endian machines.
        if (fread(&offset, off_size, 1, flow_file) != 1) {
            printf("Expected offset value at pos: %lu\n", ftell(flow_file));
            return EFAULT;
        }

        if (flow_rec->packets_pow != 0) {
            printf("Packets_pow not zero, which should never happen. (%u)\n",
                flow_rec->packets_pow);
            return EFAULT;
        }

        // Copy the flow record important bits into a packet_record struct.
        // This will make it easy to do comparisons.
        struct packet_record flow_pkt_rec;
        flow_pkt_rec.header.ts = flow_rec->first_ts;
        flow_pkt_rec.src = *((ip_addr_t *)&flow_rec->src);
        flow_pkt_rec.dst = *((ip_addr_t *)&flow_rec->dst);
        flow_pkt_rec.srcport = flow_rec->srcport;
        flow_pkt_rec.dstport = flow_rec->dstport;
        flow_pkt_rec.proto = flow_rec->proto;
        flow_pkt_rec.header.caplen = flow_rec->size;

        //print_packet(&flow_pkt_rec, " ");
        //printf("offset: %lu\n", offset);

        uint64_t pkts_left = flow_rec->packets;
        // Check each packet to make sure it's what we expect.
        while (pkts_left > 0) {
            // Make sure our position makes sense if this is the first packet.
            if (flow_rec->packets == pkts_left) {
                // This isn't strictly necessary, but helps with debugging.
                if (ftell(cap_file) != capfile_pos) {
                    printf("Bad pos. F: %lu, V: %lu\n", ftell(cap_file), capfile_pos);
                    return EFAULT;
                }
                // This is where we check to make sure flow index really points to the beginning of
                // a flow.
                if (capfile_pos != offset) {
                    printf("Flow is not in it's expected position: F: %lu, V: %lu\n",
                            offset, capfile_pos);
                    return EFAULT;
                }
            }

            packet_record_init(rec);

            // Read the next packet header and packet from file.
            if (fread(&rec->header, sizeof(struct pcap_pkthdr32), 1, cap_file) != 1) {
                printf("Error reading header.");
                return EIO;
            }
            capfile_pos += sizeof(struct pcap_pkthdr32);
            if (rec->header.caplen > CFG_MTU_DF) {
                printf("Packet larger than allowed MTU. P: %u, MTU: %u\n",
                        rec->header.caplen, CFG_MTU_DF);
                return EFAULT;
            }
            if (fread(&rec->packet, sizeof(uint8_t), rec->header.caplen, cap_file) != rec->header.caplen) {
                printf("Could not read full packet. Expected %u bytes.\n", rec->header.caplen);
                return EIO;
            }
            capfile_pos += rec->header.caplen;

            // Try to parse the packet.
            packet_parse(rec, stats);

            // Compare our flow record with what we've parsed from the packet.
            if (gen_cmp(&flow_pkt_rec, rec, kt_FLOW) != 0) {
                printf("Flow described does not match packet at FCAP: %lu, "
                               "FLOW: %lu, flow #: %lu, packet #: %lu\n",
                       offset, flow_pos, flow_count+1, pkt_no);
                print_packet(&flow_pkt_rec, " <> \n");
                print_packet(rec, "\n");
                return EFAULT;
            }

            pkt_no++;
            pkts_left--;
        }
        flow_count++;
    }

    if (flow_count != idx_header.records) {
        printf("Mismatch in the number of flow records. Expected: %lu, got: %lu\n",
            idx_header.records, flow_count);
    }
    return 0;
}