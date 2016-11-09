// This program takes an FCAP file and makes sure it's packets are in the expected order.

#include <pcap.h>
#include "../pcapdb.h"
#include "../pcapdb_init.h"
#include "../network.h"
#include "../capture.h"

void checker_func(uint8_t * arg, const struct pcap_pkthdr * hdr, const uint8_t * packet);

struct checker_state {
    struct network_stats stats;
    uint64_t out_of_order;
    struct packet_record * prev;
};

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: %s <pcap_file>\n", argv[0]);
        printf("This takes an PCAP file and checks the ordering.");
        printf("Use fcap2pcap to convert an fcap file to pcap format first.");
        printf("Return: 0 if the packets are ordered correctly.");
    }

    // Use our existing pcap opening functions
    struct system_state state;
    system_state_init(&state);
    state.conf.capture_mode = CAP_MODE_FILE;

    struct capture_state * cap_state = capture_state_init(argv[1], &state);

    int ret = prepare_interface(cap_state);
    if (ret != 0) return ret;

    struct checker_state *chk_state = calloc(1, sizeof(struct checker_state));

    // Run until we're out of packets.
    pcap_dispatch(cap_state->libpcap_if, 0, checker_func, (void *) chk_state);

    close_interface(cap_state);
    free(cap_state);

    if (chk_state->stats.captured_pkts > 0 && chk_state->out_of_order == 0) {
        fprintf(stderr, "All %lu packets were in the expected order.\n",
                chk_state->stats.captured_pkts);
        return 0;
    } else {
        fprintf(stderr, "%lu packets were out of order.\n", chk_state->out_of_order);
        return -1;
    }
}

void checker_func(uint8_t * arg, const struct pcap_pkthdr * hdr, const uint8_t * packet) {
    struct checker_state * chk_state = (struct checker_state *) arg;

    struct packet_record * rec = calloc(1, sizeof(packet_record) + hdr->caplen);

    chk_state->stats.captured_pkts++;

    rec->header.ts.tv_sec = (uint32_t)hdr->ts.tv_sec;
    rec->header.ts.tv_usec = (uint32_t)hdr->ts.tv_usec;
    rec->header.len = hdr->len;
    rec->header.caplen = hdr->caplen;
    memcpy(&rec->packet, packet, hdr->caplen);

    packet_parse(rec, &chk_state->stats);

    // For the first packet we don't have to check anything.

    if (chk_state->prev != NULL) {
        switch (gen_cmp(chk_state->prev, rec, kt_FLOW)) {
            // The previous packet is less than or equal to this one.
            // That is a good thing.
            case -1:
                break;
            case 0:
                // We might as well make sure the timestamps make sense.
                // We only care about timestamp order when the packets are in the same flow.
                if ((chk_state->prev->header.ts.tv_sec > rec->header.ts.tv_sec) ||
                        (chk_state->prev->header.ts.tv_sec == rec->header.ts.tv_sec &&
                         chk_state->prev->header.ts.tv_usec > rec->header.ts.tv_usec)) {
                    eprint_packet(chk_state->prev, " > ");
                    eprint_packet(rec, "\n");
                    chk_state->out_of_order++;
                }
                break;
            case 1:
                // This packet is less than the last one. That is bad.
                eprint_packet(chk_state->prev, " > ");
                eprint_packet(rec, "\n");
                chk_state->out_of_order++;
                break;
            default:
                fprintf(stderr, "Invalid comparison\n");
        }
    }
    chk_state->prev = rec;
}
