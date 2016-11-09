#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include "network.h"
#include "keys.h"

#define VERBOSE(A)
#define SHOW_RAW(A)

// Return a pointer to to packet in the bucket after the one given.
// The system can work with the our header data much more quickly if it's aligned to the word
// size, so we adjust for that too.
struct packet_record * next_pkt(struct packet_record * pkt) {
    struct packet_record * next = (void *)pkt + (sizeof(struct packet_record) - 1) +
                                    pkt->header.caplen;

    size_t offset = (uint64_t)(void *)next%sizeof(long);
    return offset == 0 ? next : next + (size_t)(sizeof(long) - offset);
}

void packet_record_init(struct packet_record * rec) {
    // Initialize the remaining packet record fields.
    rec->dst.vers = rec->src.vers = NET_UNKNOWN;
    int ipv6_i;
    for (ipv6_i=0; ipv6_i < 4; ipv6_i++) {
        rec->src.addr.v6.__in6_u.__u6_addr32[ipv6_i] = 0;
        rec->dst.addr.v6.__in6_u.__u6_addr32[ipv6_i] = 0;
    }
    rec->dstport = rec->srcport = 0;
    rec->proto = 0;
}

// Parse the  headers, and return the payload position (0 on error).
int64_t datalink_parse(
        struct packet_record * rec) {
    struct pcap_pkthdr32 * header = &rec->header;
    uint8_t * packet = &rec->packet;
    int64_t pos = 0;
    uint8_t vlans = 0;
    uint16_t ethtype;

    if (header->len < 14) {
        fprintf(stderr, "Truncated Packet(eth) (hdr_len: %u)\n", header->len);
        return PE_TRUNCATED;
    }

    /* We don't need the mac addresses.
    while (pos < 6) {
        eth->dstmac[pos] = packet[pos];
        eth->srcmac[pos] = packet[pos+6];
        pos++;
    }
    pos = pos + 6;
    */
    pos += 12;
   
    // Skip the extra 2 byte field inserted in "Linux Cooked" captures.
    /* Should probably support this eventually.
    if (conf->LINUX_COOKED == 1) {
        pos = pos + 2;
    }
    */

    // Skip VLAN tagging. 
    while (LE_U_SHORT(packet, pos) == 0x8100) {
        // Make sure we have enough packet for the rest of the eth header.
        if (header->len - pos < 4) 
            return PE_TRUNCATED;
        pos += 4;
        vlans++;
        if (vlans > MAX_VLANS) {
            // We should probably have a structure to keep track of bad packets.
            return PE_EXCESS_VLANS;
        }
    }

    ethtype = LE_U_SHORT(packet, pos);
    pos = pos + 2;

    // Parse MPLS
    if (ethtype == 0x8847 || ethtype == 0x8848) {
        // Bottom of stack flag.
        uint8_t mpls_bos;
        do {
            VERBOSE(printf("MPLS Layer.\n");)
            // Deal with truncated MPLS.
            if (header->len < (pos + 4)) 
                return PE_TRUNCATED;
            
            mpls_bos = packet[pos + 2] & (uint8_t)0x01;
            pos += 4;
        } while (mpls_bos == 0);
    }
    
    SHOW_RAW(
        printf("\neth ");
            print_raw_packet(header->len, packet, 0, pos, 18);
    )
    return pos;
}
   
// Parse the IPv4 header. 
// See RFC791 for header info.
int64_t ipv4_parse(
        struct packet_record * rec,
        int64_t pos) {

    struct pcap_pkthdr32 * header = &rec->header;
    uint8_t * packet = &rec->packet;
    uint32_t h_len;

    if (header-> len - pos < 20) {
        return PE_TRUNCATED;
    }

    h_len = packet[pos] & (uint8_t)0x0f;
   
    // Grab and store the protocol, length, and 
    rec->proto = packet[pos+9];
    IPv4_MOVE(rec->src, packet + pos + 12);
    IPv4_MOVE(rec->dst, packet + pos + 16);

    SHOW_RAW(
        printf("\nipv4\n");
            print_raw_packet(header->len, packet, pos, pos + 4 * h_len, 4);
    )
    VERBOSE(
        printf("version: %d, length: %d, proto: %d\n", 
                IPv4, 
                LE_U_SHORT(packet, pos+2) - h_len*4, // The ipv4 length header.
                rec->proto);
        printf("src ip: %s, ", iptostr(&rec->src));
        printf("dst ip: %s\n", iptostr(&rec->dst));
    )

    // move the position up past the options section.
    return pos + 4*h_len;
}

// Parse the IPv6 header. 
// See RFC2460 for header description
int64_t ipv6_parse(
        struct packet_record * rec,
        int64_t pos) {

    struct pcap_pkthdr32 * header = &rec->header;
    uint8_t * packet = &rec->packet;
    uint32_t header_len = 0;

    if (header->len < (pos + 40)) {
        return PE_TRUNCATED;
    }
    IPv6_MOVE(rec->src, packet + pos + 8);
    IPv6_MOVE(rec->dst, packet + pos + 24);

    /*
    if (ip->length == 0) {
        // Possible jumbo packet.
        return PE_IPV6_JUMBO;
    }
    */

    uint8_t next_hdr = packet[pos+6];
    VERBOSE(print_raw_packet(header->len, packet, pos, pos + 40, 4);)
    VERBOSE(printf("IPv6 src: %s, ", iptostr(&rec->src));)
    VERBOSE(printf("IPv6 dst: %s\n", iptostr(&rec->dst));)
    pos += 40;
   
    // We pretty much have no choice but to parse all extended sections,
    // since there is nothing to tell where the actual data is.
    while (1) {
        VERBOSE(printf("IPv6, next header: %u\n", next_hdr);)
        switch (next_hdr) {
            // Handle hop-by-hop, dest, and routing options.
            // Yay for consistent layouts.
            case IPPROTO_HOPOPTS:
            case IPPROTO_DSTOPTS:
            case IPPROTO_ROUTING:
                if (header->len < (pos + 16)) return 0;
                next_hdr = packet[pos];
                header_len += 16;
                pos += packet[pos+1] + 1;
                break;
            case 51: // Authentication Header. See RFC4302
                if (header->len < (pos + 2)) return 0;
                next_hdr = packet[pos];
                header_len += (packet[pos+1] + 2) * 4;
                pos += (packet[pos+1] + 2) * 4;
                if (header->len < pos) return 0;
                break;
            case 50: // ESP Protocol. See RFC4303.
                // We assume the rest of the packet is encrypted. 
                rec->proto = 50;
                return pos;
            case 135: // IPv6 Mobility See RFC 6275
                if (header->len < (pos + 2)) return 0;
                next_hdr = packet[pos];
                header_len += packet[pos+1] * 8;
                pos += packet[pos+1] * 8;
                if (header->len < pos) return 0;
                break;
            case IPPROTO_FRAGMENT:
            case TCP:
            case UDP:
                rec->proto = next_hdr;
                return pos; 
            default:
                rec->proto = next_hdr;
                return PE_UNHANDLED;
        }
    }
}

// Parse the udp headers.
int64_t udp_parse(
        struct packet_record * rec,
        int64_t pos) {

    struct pcap_pkthdr32 * header = &rec->header;
    uint8_t * packet = &rec->packet;
    if (header->len - pos < 8) {
        return PE_TRUNCATED;
    }

    rec->srcport = LE_U_SHORT(packet,pos);
    rec->dstport = LE_U_SHORT(packet,pos+2);
    VERBOSE(printf("udp\n");)
    VERBOSE(printf("srcport: %d, dstport: %d\n", rec->srcport, rec->dstport);)
    SHOW_RAW(print_raw_packet(header->len, packet, pos, pos, 4);)
    return pos + 8;
}

// Parses TCP headers.
int64_t tcp_parse(
        struct packet_record * rec,
        int64_t pos) {

    struct pcap_pkthdr32 * header = &rec->header;
    uint8_t * packet = &rec->packet;

    if (header->len - pos < 4) {
        return PE_TRUNCATED;
    }
    rec->srcport = LE_U_SHORT(packet, pos);
    rec->dstport = LE_U_SHORT(packet, pos+2);
    // Use the data offset header to find the start of the data section.
    return pos + (packet[pos + 12] >> 4)*4;
}

// Fully parse the given packet and store the results in the given packet_record.
// Also updates the stats structure.
void packet_parse(
        struct packet_record * rec,
        struct network_stats * stats) {

    uint8_t * packet = &rec->packet;

    int64_t pos;

    stats->captured_pkts++;

    // Parse the data link layer. We don't need any information from this layer
    // (and MPLS means it won't be useful anyway, since MPLS will keep us from 
    // knowing what the next layer is).
    pos = datalink_parse(rec);
    if (pos <= 0) {
        stats->dll_errors++;
        VERBOSE(printf("Apparent error parsing datalink layer (%ld).\n", pos);)
        return;
    }

    // Parse the network layer, if we can. We have to guess what the next protocol
    // layer is based on the first byte.
    if (packet[pos] >> 4 == 0x04) {
        pos = ipv4_parse(rec, pos);
        stats->ipv4++;
    } else if (packet[pos] >> 4 == 0x06) {
        pos = ipv6_parse(rec, pos);
        stats->ipv6++;
    } else {
        // We can't currently (I should actually try) identify the network 
        // layer protocol of this packet.
        rec->src.vers = NET_UNKNOWN;
        rec->src.addr.v4.s_addr = 0;
        rec->dst.vers = NET_UNKNOWN;
        rec->src.addr.v4.s_addr = 0;
        rec->proto = 0;
        stats->dll_errors++;
        stats->other_net_layer++;
        VERBOSE(printf("Bad datalink parsing: %ld, %d\n", pos, packet[pos]);)
        return;
    }

    if (pos <= 0 && pos != PE_UNHANDLED) {
        // If we don't handle a protocol, that's not really an error.
        VERBOSE(printf("Apparent error parsing network layer (%ld).\n", pos);)
        stats->network_errors++;
        return;
    }

    // Parse the transport layer. We'll know what it is from the network layer.
    stats->transport[rec->proto]++;
    switch (rec->proto) {
        case UDP:
            pos = udp_parse(rec, pos);
            break;
        case TCP:
            pos = tcp_parse(rec, pos);
            break;
        default:
            rec->srcport = rec->dstport = 0;
            return;
    }

    if (pos <=0) {
        stats->transport_errors++;
        VERBOSE(printf("Apparent error parsing transport layer (%ld).\n", pos);)
    } 
}

// Thread local storage for our IP string buffer.
__thread char IP_STR_BUFF[INET6_ADDRSTRLEN];
// Convert an ip struct to a string. The returned buffer is internal,
// and need not be freed. 
char * iptostr(ip_addr_t * ip) {
    if (ip->vers == IPv4) {
        inet_ntop(AF_INET, (const void *) &(ip->addr.v4),
                  IP_STR_BUFF, INET6_ADDRSTRLEN);
    } else if (ip->vers == IPv6) { // IPv6
        inet_ntop(AF_INET6, (const void *) &(ip->addr.v6),
                  IP_STR_BUFF, INET6_ADDRSTRLEN);
    } else {
        // Unknown IP version.
        snprintf(IP_STR_BUFF, INET6_ADDRSTRLEN, "IP?, p: %p v: %u", ip, ip->vers);
    }
    return IP_STR_BUFF;
}

// Thread local storage for the packet record buffer.
__thread char PKT_REC_STR_BUFF[PKT_REC_STR_BUFF_SIZE];
// Convert the 5-tuple info in a packet record to a string.
// Returns a pointer to PKT_REC_STR_BUFF, an internal buffer that need not be freed.
char * flowtostr(struct packet_record * rec) {
    char src_buff[INET6_ADDRSTRLEN], dst_buff[INET6_ADDRSTRLEN];
    // Get src str.
    if (rec->src.vers == IPv4) {
        inet_ntop(AF_INET, (const void *) &(rec->src.addr.v4), src_buff, INET6_ADDRSTRLEN);
    } else { // IPv6
        inet_ntop(AF_INET6, (const void *) &(rec->src.addr.v6), src_buff, INET6_ADDRSTRLEN);
    }
    // Get dst str.
    if (rec->dst.vers == IPv4) {
        inet_ntop(AF_INET, (const void *) &(rec->dst.addr.v4), dst_buff, INET6_ADDRSTRLEN);
    } else { // IPv6
        inet_ntop(AF_INET6, (const void *) &(rec->dst.addr.v6), dst_buff, INET6_ADDRSTRLEN);
    }
   
    snprintf(PKT_REC_STR_BUFF, PKT_REC_STR_BUFF_SIZE, "%s|%u->%s|%u,%u", 
             src_buff, rec->srcport, dst_buff, rec->dstport, rec->proto);
    return PKT_REC_STR_BUFF;
}

// Compare the two ip_addr_t objects ip1 and ip2
// Returns -1 (ip1 < ip2), 0 (ip1 == ip2) or 1 (ip1 > ip2)
// An ipv4 address is always less than an ipv6 address
int ip_cmp(ip_addr_t * ip1, ip_addr_t * ip2) {
    int v6_c;

    if (ip1->vers != ip2->vers)
        return ip1->vers < ip2->vers ? -1: 1;
    if (ip1->vers == IPv4) {
        if (ip1->addr.v4.s_addr == ip2->addr.v4.s_addr) 
            return 0;
        return htonl(ip1->addr.v4.s_addr) < htonl(ip2->addr.v4.s_addr) ? -1 : 1;
    } else if (ip1->vers == IPv6) {
        for (v6_c=0; v6_c < 16; v6_c++) {
            if (ip1->addr.v6.s6_addr[v6_c] != ip2->addr.v6.s6_addr[v6_c]) {
                if (ip1->addr.v6.s6_addr[v6_c] < ip2->addr.v6.s6_addr[v6_c]) {
                    return -1;
                } else {
                    return 1;
                }
            }
        }
        return 0;
    } // If the IP's are both some other (or invalid type), consider them equal.
    return 0;
}

// Compare the packet records according to tree type.
int gen_cmp(struct packet_record * pr1,
            struct packet_record * pr2,
            keytype_t tt) {
    int ip_cmp_res;

    TERR("idx(%lu): gen_cmp(%p, %p, %s)\n", pthread_self(), pr1, pr2, kt_name(tt));
    TERR("idx(%lu): %s <-> ", pthread_self(), flowtostr(pr1));
    TERR("%s\n", flowtostr(pr2));

    switch (tt) {
        case kt_FLOW:
            // Compare the parts of the flow such that the operations
            // that are the cheapest and most likely to generate differences
            // occur first. The ephemeral port on a given flow has very
            // good odds of being different, so we look at those first.
            // Additionally, that comparison is much cheaper than comparing
            // (especially v6) IP's.
            if (pr1->srcport != pr2->srcport) {
                //printf("srcport\n");
                return pr1->srcport < pr2->srcport ? -1 : 1;
            }
            if (pr1->dstport != pr2->dstport) {
                //printf("dstport\n");
                return pr1->dstport < pr2->dstport ? -1 : 1;
            }
            ip_cmp_res = ip_cmp(&pr1->src, &pr2->src);
            if (ip_cmp_res != 0) {
                //printf("srcip\n");
                return ip_cmp_res;
            }
            ip_cmp_res = ip_cmp(&pr1->dst, &pr2->dst);
            if (ip_cmp_res != 0) {
                //printf("dstip\n");
                return ip_cmp_res;
            }
            if (pr1->proto != pr2->proto) {
                //printf("proto: %d <> %d\n", pr1->proto, pr2->proto);
                return pr1->proto < pr2->proto ? -1 : 1;
            }
            return 0;

        case kt_SRCv4:
        case kt_SRCv6:
            return ip_cmp(&pr1->src, &pr2->src);

        case kt_DSTv4:
        case kt_DSTv6:
            return ip_cmp(&pr1->dst, &pr2->dst);

        case kt_SRCPORT:
            if (pr1->srcport != pr2->srcport)
                return pr1->srcport < pr2->srcport ? -1 : 1;
            return 0;

        case kt_DSTPORT:
            if (pr1->dstport != pr2->dstport)
                return pr1->dstport < pr2->dstport ? -1 : 1;
            return 0;

        default:
            ERR("Invalid tree type.\n");
            return -1;
    }
}

// Print the packet details from a packet record as a single line.
// To get the same format from tcpdump (for comparison purposes), use
// the -qn options, and then pass that through the tests/tcpdump_clean script.
void fprint_packet(FILE * outfile, struct packet_record * rec, char * lineend) {
    time_t when_ts;
    struct tm * when_tm;

    when_ts = rec->header.ts.tv_sec;
    when_tm = localtime(&when_ts);
    fprintf(outfile, "%02d:%02d:%02d.%06u IP ",
            when_tm->tm_hour, when_tm->tm_min, when_tm->tm_sec, rec->header.ts.tv_usec);

    fprintf(outfile, "%s.%u > ", iptostr(&rec->src), rec->srcport);
    fprintf(outfile, "%s.%u: ", iptostr(&rec->dst), rec->dstport);
    switch (rec->proto) {
        case TCP:
            fprintf(outfile, "tcp ");
            break;
        case UDP:
            fprintf(outfile, "UDP ");
            break;
        default:
            fprintf(outfile, "(%u) ", rec->proto);
    }
    fprintf(outfile, "%u ", rec->header.caplen);

    // This originally tried to copy tcpdump output. It was more often misleading than not.
    /*
    fprintf(outfile, "%s > ", iptostr(&rec->src));
    fprintf(outfile, "%s: ", iptostr(&rec->dst));
    if (rec->proto == ESP) {
        fprintf(outfile, "ESP %u", rec->header.caplen);
    } else {
        fprintf(outfile, "? %u", rec->header.caplen);
    }*/

    fprintf(outfile, " p%d", rec->proto);
    fprintf(outfile, "%s", lineend);
}

void print_raw_packet(uint32_t max_len, const uint8_t *packet,
                      uint32_t start, uint32_t end, u_int wrap) {
    int i=0;
    while (i < end - start && (i + start) < max_len) {
        printf("%02x ", packet[i+start]);
        i++;
        if ( i % wrap == 0) printf("\n");
    }
    if ( i % wrap != 0) printf("\n");
    return;
}

// Print the contents of the network stats structure.
// Mostly for debugging purposes.
void nw_stats_print(struct system_state * state, struct network_stats * stats) {
    int i;
    printf("# Bucket Stats #\n");
    printf(" - chain_size: %lu, %lu, %02.2f%%\n",
            stats->chain_size, state->conf.outfile_size,
           100.0*stats->chain_size/state->conf
                    .outfile_size);
    printf(" - dll_errors: %lu\n", stats->dll_errors);
    printf(" - network_errors: %lu\n", stats->network_errors);
    printf(" - transport_errors: %lu\n", stats->transport_errors);
    printf(" - captured packets: %lu\n", stats->captured_pkts);
    printf(" - ipv4: %lu\n", stats->ipv4);
    printf(" - ipv6: %lu\n", stats->ipv6);
    printf(" - other_net_layer: %lu\n", stats->other_net_layer);
    printf(" - Transport layer counts:\n");
    for (i=0; i<256; i++) {
        if (stats->transport[i] > 0) {
            printf("   - %d: %lu\n", i, stats->transport[i]);
        }
    }
    printf(" - dropped: %lu\n", stats->dropped);
}

void packet_check(struct packet_record * rec) {
    // Reparse the packet in rec to make sure it hasn't changed since it was originally parsed.
    // This function is exclusively for debugging purposes.

    struct packet_record * newrec = calloc(sizeof(struct packet_record) + rec->header.len, 1);
    struct network_stats * fake_stats = calloc(1, sizeof(struct network_stats));

    memcpy(&newrec->packet, &rec->packet, rec->header.len);
    memcpy(&newrec->header, &rec->header, sizeof(struct pcap_pkthdr32));

    packet_parse(newrec, fake_stats);

    if (gen_cmp(rec, newrec, kt_FLOW) != 0) {
        printf("Packet altered during capture.");
        print_packet(rec, "\n");
        print_packet(newrec, "\n");
    }

    free(newrec);
    free(fake_stats);

}
