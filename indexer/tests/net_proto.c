#include "net_proto.h"
#include <pcap.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

void mk_fake_packet(unsigned char * buffer, uint16_t id, size_t length,
                    uint32_t i_div, uint32_t p_div) {
    struct eth_frame * eth = (struct eth_frame *) buffer;
    struct ipv4_frame * ipv4 = (struct ipv4_frame *) &buffer[sizeof(struct eth_frame)];
    struct udp_frame * udp = (struct udp_frame *) &buffer[sizeof(struct eth_frame)+
                                                          sizeof(struct ipv4_frame)];
    uint16_t * data = (uint16_t*)((void *)udp + sizeof(struct udp_frame));

    memcpy(eth->src_mac, "srcMAC", 6);
    memcpy(eth->dst_mac, "dstMAC", 6);
    eth->ethertype = ETHTYPE_IPV4;

    ipv4->version = 0x04;
    ipv4->ihl = 5;
    ipv4->dscp = 0;
    ipv4->ec = 0;
    ipv4->length = BE_16(20 + length + sizeof(struct udp_frame));
    ipv4->ident = 0;
    ipv4->flags = 0;
    ipv4->frag_off = 0;
    ipv4->ttl = 0x10;
    ipv4->proto = UDP_PROTO;
    ipv4->checksum = 0x7069;
    ipv4->src = BE_32((id%i_div + 1u));
    ipv4->dst = BE_32((id%i_div + 1u));

    udp->src_port = BE_16(id%p_div + 1u);
    udp->dst_port = BE_16(id%p_div + 1u);
    udp->length = BE_16(length + 8);
    udp->checksum = 0x4455;

    // Fill the rest of the packet with the packet id.
    int p;
    for (p=0; p < length/2; p++) {
        data[p] = id+1;
    }

    if (length%2 == 1) {
        data[p] = 0;
    }

    return;
};

#define PACKETS 5000

#define DUMMY_PCAP_PATH "/tmp/dummy.pcap"
#define DUMMY_PCAP_CONTENT {0xd4,0xc3,0xb2,0xa1,0x2,0,0x4,0,\
                            0,0,0,0,0,0,0,0,0xff,0xff,0,0,1,0,0,0}
#define OUTFILE_PATH "/tmp/enum.pcap"

int main(int argc, char ** argv){

    pcap_dumper_t * outfile;
    char * out_fn = OUTFILE_PATH;

    pcap_t * dummy_pcap;
    char errbuff[PCAP_ERRBUF_SIZE];
    unsigned char packet[1500];
    uint64_t packets = PACKETS;

    const char * OPTIONS = "i:p:k:";
    uint32_t i_div = 20;
    uint32_t p_div = 20;

    int c = getopt(argc, argv, OPTIONS);
    while (c != -1) {
        switch (c) {
            case 'i':
                i_div = strtoul(optarg,NULL,10);
                break;
            case 'p':
                p_div = strtoul(optarg,NULL,10);
                break;
            case 'k':
                packets = strtoul(optarg, NULL, 10);
                break;
            default:
                printf("Invalid argument: %c\n", c);
                return -1;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    if (optind != argc) {
        out_fn = argv[optind];
    }

    FILE * dummy_file = fopen("/tmp/dummy.pcap", "w");
    unsigned char dummy_data[24] = DUMMY_PCAP_CONTENT;
    fwrite(dummy_data, sizeof(unsigned char), 24, dummy_file);
    fclose(dummy_file);

    dummy_pcap = pcap_open_offline(DUMMY_PCAP_PATH, errbuff);
    outfile = pcap_dump_open(dummy_pcap, out_fn);

    printf("sizes, eth: %lu, ipv4: %lu, udp: %lu\n", sizeof(struct eth_frame),
                                                     sizeof(struct ipv4_frame),
                                                     sizeof(struct udp_frame));

    uint16_t i;
    uint32_t base_len = sizeof(struct eth_frame) +
                        sizeof(struct ipv4_frame) +
                        sizeof(struct udp_frame);
    for (i=0; i < packets; i++) {
        uint32_t data_len = 2 + i%1000;
        struct timeval now = {i, 0x000073757374};
        struct pcap_pkthdr hdr = {
                .caplen = base_len + data_len,
                .len = base_len + data_len,
                .ts = now};
        mk_fake_packet(packet, i, (size_t)data_len, i_div, p_div);

        pcap_dump((void *)outfile, &hdr, packet);
    }

    pcap_close(dummy_pcap);
    unlink(DUMMY_PCAP_PATH);
    pcap_dump_close(outfile);
    printf("Created enum pcap at '%s'.\n", out_fn);

    return 0;
}
