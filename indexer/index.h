#ifndef __CORNET_INDEX_H__
#define __CORNET_INDEX_H__

#include "pcapdb.h"
#include "bucketize.h"
#include "event.h"
#include "keys.h"
#include "network.h"

#include <stdint.h>
// Pre-decleration to resolve circular definition in flow_list_node and index_node.
struct index_node; 

struct pkt_list_node {
    struct packet_record * rec;
    struct pkt_list_node * next;
};

// A linked list node for keeping track of 
struct flow_list_node {
    struct index_node * flow;
    struct flow_list_node * next;
};

// A node in an index tree. This is made for both flow and sub-indexes.
struct index_node {
    struct packet_record * key;
    struct index_node * left;
    struct index_node * right;
    union {
        struct {
            // We should always have a next/last list item, since
            // we don't have any place to store the flow in the index node.
            struct flow_list_node * first;
            struct flow_list_node * last;
        } flows;
        struct {
            // Since we already have a pointer to the packet in key,
            // first/last == NULL implies this is the only packet.
            struct pkt_list_node * first;
            struct pkt_list_node * last;
        } pkts;
    } ll;
    // The file offset to this flow's entry in the flow index.
    uint64_t flow_index_offset;
};

// The index struct made for the head of each bucket chain when indexing.
struct index_set {
    uint64_t packet_cnt;
    struct index_node * flows;
    uint64_t flow_cnt;
    struct index_node * srcv4;
    uint64_t srcv4_cnt;
    struct index_node * dstv4;
    uint64_t dstv4_cnt;
    struct index_node * srcv6;
    uint64_t srcv6_cnt;
    struct index_node * dstv6;
    uint64_t dstv6_cnt;
    struct index_node * srcport;
    struct index_node * dstport;
    // We also keep a list of the flows in time order. The only real
    // use for this, at the moment, is when freeing the flow index nodes.
    struct flow_list_node * timeorder_head;
    struct flow_list_node * timeorder_tail;
};

// Insert into the tree of given type (and then splay).
struct index_node * splay_tr_insert(struct index_node *,  // tree root
                    struct packet_record *,               // key
                    struct index_node *,                  // flow
        keytype_t);

// Flows are added to indexes in time-order, but they need to be written in
// flow order. This takes
struct flow_list_node * merge_sort_offsets(struct flow_list_node * head);

// Generate a graph for the tree of the given tree type. 
// These are saved in /tmp/cornet/test_graphs/
void splay_tr_graph(struct bucket *, keytype_t tt);

// Function to run as an indexer thread.
// arg should be a (struct system_state *)
void * indexer(void * arg);

// Index the given bucket.
void index_bucket(struct bucket *bkt);

// Print an the gien index tree.
void print_index(struct index_node *, // The head of an index tree or sub-tree.
                 keytype_t);          // The type of index.
#endif
