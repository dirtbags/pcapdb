#include <stdint.h>
#include <stddef.h>

// An Layer 2 ethernet frame (preamble) without VLAN tagging.
#define ETHTYPE_IPV4 0x0008
struct eth_frame {
    unsigned char dst_mac[6];
    unsigned char src_mac[6];
    uint16_t ethertype;
};

#define BE_16(I) ((I & 0xff00) >> 8) + ((I & 0xff) << 8)
#define BE_32(I) (((I & 0xff000000) >> 24) + ((I & 0xff0000) >> 8) +\
                  ((I & 0xff00) << 8) + ((I & 0xff) << 24))

#define ETH_FRAME_INIT {\
        .src_mac = {0x42, 0x41, 0x4d, 0x63, 0x72, 0x73},\
        .dst_mac = {0x42, 0x41, 0x4d, 0x74, 0x73, 0x64},\
        .ethertype = ETHTYPE_IPV4}

struct ipv4_frame {
    uint8_t ihl:4;
    uint8_t version:4;
    uint8_t dscp:6;
    uint8_t ec:2;
    uint16_t length;
    uint16_t ident;
    uint16_t flags:3;
    uint16_t frag_off:13;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
};

#define UDP_PROTO 0x11
#define IPV4_FRAME_INIT {\
        .version = 0x04, \
        .ihl = 5, \
        .dscp = 0, \
        .ec = 0, \
        .length = 0,\
        .ident = 0,\
        .flags = 0, \
        .frag_off = 0,\
        .ttl = 0x10, \
        .proto = UDP_PROTO,\
        .checksum = 0, \
        .src = 0, \
        .dst = 0}

struct udp_frame {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};

#define UDP_FRAME_INIT {\
        .src_port = 0,\
        .dst_port = 0,\
        .length = 0,\
        .checksum = 0\
}

struct packet_head {
    struct eth_frame eth;
    struct ipv4_frame ipv4;
    struct udp_frame udp;
};

#define PACKET_HEAD_INIT {ETH_FRAME_INIT, IPV4_FRAME_INIT, UDP_FRAME_INIT}

// Returns a packet structure with dummy information. Source and dest IP and ports are set to 'id'.
void mk_fake_packet(unsigned char *, uint16_t, size_t, uint32_t, uint32_t);