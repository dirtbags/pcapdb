#include <stdlib.h>
#include <string.h>
#include "keys.h"
#include "network.h"

// Returns the index key size for the given tree type.
size_t kt_key_size(keytype_t treetype) {
    switch (treetype) {
        case kt_FLOW:
            return sizeof(struct fcap_flow_key);
        case kt_SRCv4:
        case kt_DSTv4:
            return sizeof(struct in_addr);
        case kt_SRCv6:
        case kt_DSTv6:
            return sizeof(struct in6_addr);
        case kt_SRCPORT:
        case kt_DSTPORT:
            return sizeof(uint16_t);
        default:
            ERR("Invalid treetype: %d", treetype);
            return 0;
    }
};

// Compare the two keys given according to key type
// Returns (-1, 0, 1) for (<, ==, >) respectively.
int kt_key_cmp(union kt_ptrs key1, union kt_ptrs key2, keytype_t key_type) {
    ip_addr_t ip1, ip2;
    switch (key_type) {
        case kt_SRCPORT:
        case kt_DSTPORT:
            if (*(key1.port) < *(key2.port)) return -1;
            if (*(key1.port) > *(key2.port)) return 1;
            return 0;
        case kt_SRCv4:
        case kt_DSTv4:
            ip1.vers = IPv4;
            ip1.addr.v4 = *(key1.v4);
            ip2.vers = IPv4;
            ip2.addr.v4 = *(key2.v4);
            return ip_cmp(&ip1, &ip2);
        case kt_SRCv6:
        case kt_DSTv6:
            ip1.vers = IPv6;
            ip1.addr.v6 = *(key1.v6);
            ip2.vers = IPv6;
            ip2.addr.v6 = *(key2.v6);
            return ip_cmp(&ip1, &ip2);
        default:
            ERR("Invalid key type: %d", key_type);
            return -2;
    }
}

// Return a string containing a representation of the given key.
// The returned string does not need to be freed, as it will use a predefined buffer.
// Note: it does not currently support kt_FLOW
#define TT_FLOW_KEY_FORMAT "%09u.%06u %09u.%06u " /* len: 4*(32bit uint) + 4 spacers = 44 */ \
                           "%s.%d " /* len: (INET6 len) + (16bit uint) + 2 spacers = INET6 + 7*/ \
                           "%s.%d " /* len: (INET6 len) + (16bit uint) + 2 spacers = INET6 + 7*/ \
                           "%u %u %u" /* len: (32bit uint)*3 + 2 spacers = 32 */
#define TT_KEY_STR_BUF_LEN 2*INET6_ADDRSTRLEN
char TT_KEY_STR_BUF[2*INET6_ADDRSTRLEN];
const char *kt_key_str(union kt_ptrs key, keytype_t key_type) {
    char srcip[INET6_ADDRSTRLEN];
    ip_addr_t src;
    ip_addr_t dst;
    switch (key_type) {
        case kt_FLOW:
            // iptostr uses an internal buffer, so we need to strcpy the first ip.
            // We can use the internal buffer for the second.
            src.addr = key.flow->src; src.vers = key.flow->src_ip_vers;
            dst.addr = key.flow->dst; dst.vers = key.flow->dst_ip_vers;
            strncpy(srcip, iptostr(&src), INET6_ADDRSTRLEN);
            snprintf(TT_KEY_STR_BUF, TT_KEY_STR_BUF_LEN, TT_FLOW_KEY_FORMAT,
                     key.flow->first_ts.tv_sec, key.flow->first_ts.tv_usec,
                     key.flow->last_ts.tv_sec, key.flow->last_ts.tv_usec,
                     srcip, key.flow->srcport, iptostr(&dst), key.flow->dstport,
                     key.flow->proto, key.flow->packets, key.flow->size);
            return TT_KEY_STR_BUF;
        case kt_DSTv4:
        case kt_SRCv4:
            return inet_ntop(AF_INET, key.v4, TT_KEY_STR_BUF, INET6_ADDRSTRLEN);
        case kt_SRCv6:
        case kt_DSTv6:
            return inet_ntop(AF_INET6, key.v6, TT_KEY_STR_BUF, INET6_ADDRSTRLEN);
        case kt_SRCPORT:
        case kt_DSTPORT:
            snprintf(TT_KEY_STR_BUF, INET6_ADDRSTRLEN, "%u", *(key.port));
            return TT_KEY_STR_BUF;
        default:
            return NULL;
    }
}

int kt_key_parse(union kt_ptrs key, keytype_t key_type, const char *string) {

    uint64_t port;
    switch (key_type) {
        case kt_SRCPORT:
        case kt_DSTPORT:
            port = strtoul(string, NULL, 10);
            if (port > UINT16_MAX || errno == ERANGE) {
                ERR("Invalid port: %s\n", string);
                return ERANGE;
            }
            *(key.port) = (uint16_t) port;
            break;
        case kt_SRCv4:
        case kt_DSTv4:
            if (inet_pton(AF_INET, string, key.v4) < 1) {
                // Not in 0.0.0.0 format

                ERR("Invalid IPv4 address: %s\n", string);
                return ERANGE;
            }
            break;
        case kt_SRCv6:
        case kt_DSTv6:
            if(inet_pton(AF_INET6, string, key.v6) < 1) {
                // Invalid format (see man inet_pton(3))
                ERR("Invalid IPv6 address: %s\n", string);
                return ERANGE;
            }
            break;
        case kt_FLOW:
            ERR("Flow indexes aren't searchable.\n");
            return EINVAL;
        default:
            ERR("Invalid key type: %d\n", key_type);
            return EINVAL;
    }

    return 0;
}

const char *kt_name(keytype_t tt) {
    const char * tt_names[kt_DSTPORT +1] = {"FLOW", "SRCv4", "DSTv4", "SRCv6", "DSTv6",
                                           "SRCPORT", "DSTPORT"};
    if (tt > kt_DSTPORT || tt < 0) return "ERROR";
    return tt_names[tt];
}

// Convert the given string name of a key_type into the key_type value.
keytype_t kt_strtokeytype(char * string) {
    keytype_t tt;

    for (tt = kt_FLOW; tt <= tt_LAST; tt++) {
        if (strncmp(string, kt_name(tt), strlen(string)) == 0) {
            return tt;
        }
    }
    return kt_BADKEY;
}

// This is pretty much identical to the comparison in index.c, gen_cmp function.
// That function takes a very different structure, however.
int flow_key_cmp(struct fcap_flow_key * k1, struct fcap_flow_key * k2) {
    int ip_cmp_res;
    if (k1->srcport != k2->srcport) return k1->srcport < k2->srcport ? -1 : 1;
    if (k1->dstport != k2->dstport) return k1->dstport < k2->dstport ? -1 : 1;
    //
    ip_cmp_res = ip_cmp((ip_addr_t *) &k1->src, (ip_addr_t *) &k2->src);
    if (ip_cmp_res != 0) return ip_cmp_res;
    ip_cmp_res = ip_cmp((ip_addr_t *) &k1->dst, (ip_addr_t *) &k2->dst);
    if (ip_cmp_res != 0) return ip_cmp_res;
    if (k1->proto != k2->proto) return k1->proto < k2->proto ? -1 : 1;
    return 0;
}

// Merge two flow keys, storing the result in the first. The basic assumption is that
// these two keys represent the same flow across multiple FCAP files.
// This updates the start and end timestamps to be the earliest and latest, respectively.
// It also adds the packet and size counts, updating the size power variable if necessary.
void flow_key_merge(struct fcap_flow_key * k1, struct fcap_flow_key * k2) {
    // Overwrite the start timestamp in k1 with the lesser of the two.
    if (k2->first_ts.tv_sec < k1->first_ts.tv_sec ||
            (k2 ->first_ts.tv_sec == k1->first_ts.tv_sec &&
                    k2->first_ts.tv_usec < k1->first_ts.tv_usec)) {
        k1->first_ts = k2->first_ts;
    }

    // Overwrite the end timestamp in k1 with the greater of the two.
    if (k2->last_ts.tv_sec > k1->last_ts.tv_sec ||
            (k2->last_ts.tv_sec == k1->last_ts.tv_sec &&
                k2->last_ts.tv_usec > k1->last_ts.tv_usec)) {
        k1->last_ts = k2->last_ts;
    }

    // These operations can't overflow a 64 bit uint.
    uint64_t total_packets = (k1->packets << k1->packets_pow) +
                                (k2->packets << k2->packets_pow);
    uint64_t total_size = (k1->size << k1->packets_pow) +
                                (k2->size << k2->size_pow);
    // Find the exponent that will make this value fit in a 32 bit int.
    uint8_t packets_pow = 0;
    while (total_packets > UINT32_MAX) {
        total_packets = total_packets >> 1;
        packets_pow++;
    }
    // We store this power in a 4 bit field. It overflows at 2^4 = 16.
    // If that has happened (should be astoundingly rare), then we not it by
    // setting the packet count and power to zero.
    if (packets_pow > 15) {
        k1->packets = k1->packets_pow = 0;
    } else {
        k1->packets = (uint32_t)total_packets;
        k1->packets_pow = packets_pow;
    }

    // The same steps have to be taken for the size variables.
    uint8_t size_pow = 0;
    while (total_size > UINT32_MAX) {
        total_size = total_size >> 1;
        size_pow++;
    }
    if (size_pow > 15) {
        k1->size = k1->size_pow = 0;
    } else {
        k1->size = (uint32_t) total_size;
        k1->size_pow = size_pow;
    }
}