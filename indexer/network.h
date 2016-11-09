#ifndef __CORNET_NETWORK_H__
#define __CORNET_NETWORK_H__

#define _MULTI_THREADED
#include <pthread.h>
#include "pcapdb.h"
#include <arpa/inet.h>
#include <stdint.h>

// Various types of index trees we will be creating.
// kt_FLOW must be first.
// tt_LAST should be updated to be the last enum value if any are added.
typedef enum {
    kt_FLOW,
    kt_SRCv4,
    kt_DSTv4,
    kt_SRCv6,
    kt_DSTv6,
    kt_SRCPORT,
    kt_DSTPORT,
    kt_BADKEY
} keytype_t;

#define tt_LAST kt_DSTPORT
#define LE_U_LONG(PKT,P) (((uint32_t)PKT[P] << 24)+(PKT[P+1] << 16)+(PKT[P+2] << 8)+PKT[P+3])
#define LE_U_SHORT(PKT,P) (((uint16_t)PKT[P] << 8) + PKT[P+1])

// Network parse error codes
#define PE_UNKNOWN 0
#define PE_TRUNCATED -1
#define PE_UNHANDLED -2
#define PE_EXCESS_VLANS -3
#define PE_EXCESS_MPLS -4
#define PE_IPV6_JUMBO -5

typedef union {
    struct in_addr v4;
    struct in6_addr v6;
} in46_addr_t;

// IP address container that is IP version agnostic.
// The IPvX_MOVE macros handle filling these with packet data correctly.
typedef struct {
    // Should always be either 4 or 6.
    in46_addr_t addr;
    uint8_t vers;
} ip_addr_t;

#define IPv4 0x04
#define IPv6 0x06
#define NET_UNKNOWN 0

// Move IPv4 addr at pointer P into ip object D, and set it's type.
#define IPv4_MOVE(D, P) D.addr.v4.s_addr = *(in_addr_t *)(P); \
                        D.vers = IPv4;
// Move IPv6 addr at pointer P into ip object D, and set it's type.
#define IPv6_MOVE(D, P) memcpy(D.addr.v6.s6_addr, P, sizeof(struct in6_addr)); D.vers = IPv6;

// Buffer for writing packet record strings.
// IP_len*2 + port_len*2 + proto_len + syntax + null
#define PKT_REC_STR_BUFF_SIZE INET6_ADDRSTRLEN*2 + 5*2 + 3 + 6

// Compare two IP addresses.
// Returns (-1, 0, 1) for (<, ==, >) respectively.
// IPv4 addresses are always less than ipv6.
int ip_cmp(ip_addr_t * ip1, ip_addr_t * ip2);

#define UDP 0x11
#define TCP 0x06
#define ESP 0x32

// How many vlans tags to parse before we give up on the packet.
#define MAX_VLANS 3
// How many MPLS headers to parse before we give up on the packet.
#define MAX_MPLS 3

// Structure for keeping track of network statistics.
struct network_stats {
    // Capture interface these packets were captured on.
    // This is actually a pointer to the string in the capture state object.
    char * interface;
    // Total data + pcap_headers for this set of packets
    uint64_t chain_size;
    // The timestamp of the first packet seen
    uint64_t dll_errors;
    // # network layer errors
    uint64_t network_errors;
    // # transport layer errors
    uint64_t transport_errors;
    // # total packets captured
    uint64_t captured_pkts;
    // # ipv4 packets 
    uint64_t ipv4;
    // # ipv6 packets
    uint64_t ipv6;
    // # other network layer packets
    uint64_t other_net_layer;
    // Counts for each possible transport layer.
    uint64_t transport[256];
    // # of the packets seen by the interface.
    uint64_t if_seen;
    // # of packets dropped due to lack of buckets
    uint64_t dropped;
    // # of packets dropped due by the system (kernel or interface)
    uint64_t sys_dropped;
} network_stats;

// This is the header for packets when keeping track of them in buckets.
struct packet_record {
    // The pcap header for the packet.
    struct pcap_pkthdr32 header;
    // IPv4/IPv6 addresses
    ip_addr_t src;
    ip_addr_t dst;
    // Transport layer ports
    uint16_t    srcport;
    uint16_t    dstport;
    // Transport layer protocol number
    uint8_t     proto;
    // The start of the packet data.
    uint8_t     packet;
} packet_record;

// Initialize the packet record's 5-tuple information.
void packet_record_init(struct packet_record *);

// Returns the position of the next packet in a series given this one.
// This does not guarantee that the next packet exists, it justs tells
// where it would be if it does.
struct packet_record * next_pkt(struct packet_record *);

// Compare the two packet records based upon keytype_t
// Returns -1 (A < B), 0 (A == B), or 1 (A > B)
int gen_cmp(struct packet_record *, struct packet_record *, keytype_t);

// The following functions parse the packet protocol headers for relevant information, and then
// place that information in the given packet record object.
// Each takes the packet record object (which has the pcap/pfring packet
// header and the packet itself), and a position from which to start parsing.
// They return the position of where to start parsing the next header, the protocol of which
// is indicated in the packet record object.
int64_t datalink_parse(struct packet_record *);
int64_t ipv4_parse(struct packet_record *, int64_t);
int64_t ipv6_parse(struct packet_record *, int64_t);
int64_t udp_parse(struct packet_record *, int64_t);
int64_t tcp_parse(struct packet_record *, int64_t);
// Parse the entire packet, using the above functions.
void packet_parse(
        struct packet_record *,     // The packet record containing the packet to parse
        struct network_stats *);    // Where to add this packet's stats.

// Convert an ip struct to a string. The returned buffer is internal, 
// and need not be freed. 
char * iptostr(ip_addr_t *);
// Put the packets 5-tuple info in a string in the format
// src-srcport->dst-dstport,proto
char * flowtostr(struct packet_record *);

// Print packet bytes in hex.
// Takes:
//  - Packet size
//  - The packet *
//  - The start byte
//  - The end byte
//  - Print (wrap) characters per line
void print_raw_packet(uint32_t, const uint8_t *, uint32_t, uint32_t, u_int);


// Prints the packet in a tcpdump -qn like format. To get the tcpdump
// format to match, run it through the tests/tcpdump_clean script.
void fprint_packet(
        FILE *,                 // The output file
        struct packet_record *, // The packet record to print.
        char *);                // The string to print at EOL.
#define print_packet(packet_record_p, eol_str) fprint_packet(stdout, packet_record_p, eol_str)
#define eprint_packet(p,c) fprint_packet(stderr, p, c)

// Prints the contents of a network stats structure.
void nw_stats_print(struct system_state *, struct network_stats *);

void packet_check(struct packet_record *);

#endif
