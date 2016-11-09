#ifndef __CORNET_KEYS_H__
#define __CORNET_KEYS_H__

#include "pcapdb.h"

#include "network.h"
#include <inttypes.h>

// Get a string 'name' for each index type.
// Used to name index files.
const char *kt_name(keytype_t);

// Return a key type from the given string.
// Returns kt_BADKEY on failure.
keytype_t kt_strtokeytype(char *);

// Return the key size for the given tree type.
size_t kt_key_size(keytype_t);

// The 'key' value for the flow index.
//
// Both size and packets can be zero, indicating a flow that is too
// large to describe with 32 bit ints.
// Since this should be an exceedingly rare case, we save space in
// the indices by keeping them as 32 bit ints anyway.
//
// This structure is as well packed as I could make it, given the information
// that needed to be contained. The ip_vers fields and padding could be
// reduced to two bits total, but in memory that would still consume as
// much as it currently does.
// The src and dest are each immediately followed by a variable indicating
// the ip version. Those to variables together are the exact same format
// as ip_addr_t defined in network.h, and can be recast as such.
// Using actual ip_addr_t would have wasted 14 bytes of structure space total.
struct fcap_flow_key {
    struct timeval32 first_ts; // 8 bytes
    struct timeval32 last_ts; // 8 bytes
    // This can be cast as an ip_addr_t
    in46_addr_t src; // 16 (IPv6)
    uint8_t src_ip_vers; // 1 type byte
    uint8_t proto; // 1 byte
    uint16_t srcport; // 2 bytes
    // Number of packets in this flow. packets * 2^packets_pow
    uint32_t packets; // 4 bytes
    in46_addr_t dst; // 16 (IPv6)
    uint8_t dst_ip_vers; // 1 type byte
    // If our size or packet count exceeds UINT32_MAX, then we sacrifice precision
    // for accuracy and use exponential notation.
    uint8_t size_pow:4; // 1 byte
    uint8_t packets_pow:4;
    uint16_t dstport; // 2 bytes
    // The actual size is size * 2^size_pow, for a max supported
    // flow size of 256 TB for flow reporting.
    // When not combining flow records, size_pow must be zero (it always is),
    // since this represents the total size of the flow in the FCAP file, including libpcap (32 bit)
    // packet headeris.
    uint32_t size; // 4 bytes
} ; // total: 64 bytes

// Structure for addressing an array stored in a generic buffer
// based on the data type stored.
union kt_ptrs {
    struct in_addr * v4;
    struct in6_addr * v6;
    uint16_t * port;
    struct fcap_flow_key * flow;
    uint8_t * generic;
};

// Compare the two keys given the generic treetype unions.
// Returns (-1, 0, 1) for (<, ==, >) respectively.
int kt_key_cmp(union kt_ptrs, union kt_ptrs, keytype_t);

// Returns a string representation of key in a global buffer.
const char *kt_key_str(union kt_ptrs key, keytype_t key_type);

// Parse the given string, assuming it's of the given tree type.
// The result are stored according to the pointers in the kt_ptrs union.
// Returns 0 on success.
int kt_key_parse(
        union kt_ptrs,
        keytype_t,
        const char *);

// Compare two flow keys. <, =, > : -1, 0, 1
int flow_key_cmp(struct fcap_flow_key *, struct fcap_flow_key *);

// Merge two flow keys, storing the result in the first key.
void flow_key_merge(struct fcap_flow_key *, struct fcap_flow_key *);

#endif
