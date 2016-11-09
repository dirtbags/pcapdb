#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "pcapdb.h"
#include "bucketize.h"
#include "index.h"

void * indexer(void * arg) {
    struct thread_state * thr_state = (struct thread_state *)arg;
    struct system_state * state = thr_state->sys_state;
    struct bucket * bkt;
  
    INFO("idx(%lu): Indexer running.\n", pthread_self());

    // Continue doing this until we get the exit event.
    while (!event_check(&thr_state->shutdown)) {

        thr_state->status = PCAPDB_THR_IDLE;
        // Grab a bucket. This will wait until one is available.
        bkt = bucketq_pop(&state->filled_bkts);

        // This can happen, especially when shutting down.
        if (bkt == NULL) {
            if ( !event_check(&thr_state->shutdown) && state->filled_bkts.closed == 0 ) {
                // We should report this when we're not shutting down.
                ERR("NULL bucket in indexer thread #%lx.\n", pthread_self());
            }
            continue;
        } 
       
        // Index the bucket chain.
        TERR("idx(%lu): Indexing a bucket.\n", pthread_self());
        thr_state->status = PCAPDB_THR_WORKING;
        index_bucket(bkt);
        TERR("idx(%lu): Done indexing.\n", pthread_self());

        // Send the bucket on to the next step. (Bucket writing).
        bucketq_push(&state->indexed_bkts, bkt);
    }
    thr_state->status = PCAPDB_THR_SHUTDOWN;

    TERR("idx(%lu): Thread done: %d.\n", pthread_self(), event_check(&state->shutdown));

    return NULL;
}

struct watcher {
    struct packet_record * key;
    struct watcher * next;
};

int watch_keys(struct watcher * W_HEAD) {
    struct watcher * w = W_HEAD;
    while (w != NULL) {
        if (w->key->src.vers != 6 && w->key->src.vers != 4) {
            return 1;
        }
        w = w->next;
    }
    return 0;
}

void index_bucket(struct bucket *bkt) {
    // We'll need a new index set for this bucket chain.
    struct index_set * idxs = calloc(1,sizeof(struct index_set));

    /*struct watcher * W_HEAD = NULL;
    struct watcher * w;
    int found = 0;*/

    struct network_stats * stats = bkt->stats;
    bkt->indexes = idxs;

    // Go through each packet in each bucket in the chain.
    while (bkt != NULL) {
        struct packet_record * rec = bkt->first_pkt;

        //TERR("idx(%lu): first packet: %p (%lx).\n", pthread_self(), pkt, sizeof(struct bucket));
        while (rec <= bkt->last_pkt) {
            struct index_node * flow;

            // Initialize the packet record's 5-tuple fields.
            packet_record_init(rec);

            // Parse the packet headers from the raw packet.
            packet_parse(rec, stats);

            /*
            w = calloc(1, sizeof(struct watcher));
            w->key = rec;
            w->next = W_HEAD;
            W_HEAD = w;

            if (found == 0 && watch_keys(W_HEAD) == 1) {
                fprintf(stderr, "Before indexing: %p, ", rec);
                print_packet(rec, "\n");
                found = 1;
            }
            */

            // Count this packet
            idxs->packet_cnt++;

            // Get the corresponding flow for this packet. It will be the head of the
            // flow tree.
            flow = splay_tr_insert(idxs->flows, rec, NULL, kt_FLOW);
            idxs->flows = flow;

            TERR("idx(%lu): Checking for new flow (%p)(%p).\n", pthread_self(), flow,
                                                                flow->ll.pkts.last);
            // Check to see if this is the first packet in a flow. 
            // If so, index the flow in the other indices.
            if (flow->ll.pkts.last == NULL) {
                // Since this is a new flow, it needs its own entry for the time order list.
                struct flow_list_node * tm_order = calloc(1, sizeof(struct flow_list_node));
                idxs->flow_cnt++;

                TERR("idx(%lu): Adding time order entry.\n", pthread_self());

                // Add the flow to the the list of flows in time order.
                tm_order->flow = flow;
                if (idxs->timeorder_tail == NULL) {
                    idxs->timeorder_tail = tm_order;
                } else {
                    idxs->timeorder_tail->next = tm_order;
                    idxs->timeorder_tail = tm_order;
                }

                if (idxs->timeorder_head == NULL) {
                    idxs->timeorder_head = tm_order;
                }

                TERR("idx(%lu): Indexing 5-tuple info.\n", pthread_self());

                // Index source IP
                switch (rec->src.vers) {
                    case IPv4:
                        idxs->srcv4 = splay_tr_insert(idxs->srcv4, rec, flow, kt_SRCv4);
                        idxs->srcv4_cnt++;
                        break;
                    case IPv6:
                        idxs->srcv6 = splay_tr_insert(idxs->srcv6, rec, flow, kt_SRCv6);
                        idxs->srcv6_cnt++;
                        break;
                    default:
                        // TODO: Either create a "non-IP" index or shunt these into
                        //       one of the other indexes.
                        break;
                }

                // Index dest IP
                switch (rec->dst.vers) {
                    case IPv4:
                        idxs->dstv4 = splay_tr_insert(idxs->dstv4, rec, flow, kt_DSTv4);
                        idxs->dstv4_cnt++;
                        break;
                    case IPv6:
                        idxs->dstv6 = splay_tr_insert(idxs->dstv6, rec, flow, kt_DSTv6);
                        idxs->dstv6_cnt++;
                        break;
                    default:
                        // TODO: Either create a "non-IP" index or shunt these into
                        //       one of the other indexes.
                        break;
                }

                // Index ports 
                idxs->srcport = splay_tr_insert(idxs->srcport, rec, flow, kt_SRCPORT);
                idxs->dstport = splay_tr_insert(idxs->dstport, rec, flow, kt_DSTPORT);

            }
            /*
            if (found == 0 && watch_keys(W_HEAD) == 1) {
                fprintf(stderr, "After indexing: ");
                print_packet(rec, "\n");
                found = 1;
            }*/

            rec = next_pkt(rec);

        }
        bkt = bkt->next;
    }
}

// Creates and initializes a new index node based on the given tree type.
struct index_node *mk_index_node(struct packet_record *key) {

    struct index_node * nn = calloc(1, sizeof(struct index_node));
    nn->key = key;
    nn->flow_index_offset = 0xbad1bad2bad3bad4;
    return nn;
}

// Insert the given key into the index tree of type 'treetype' at 'root'.
// Duplicate items are added to the sublist of according to tree type.
// After insertion, a splay operation is performed on the tree, which will
// make the node where the key was inserted the new tree root.
// That new root is returned.
// 
// This is meant to work both for indexing flows (kt_FLOW) and all the
// other sub-indices. As such, what's actually going on differs accordingly.
//   - For flow indexing, counter-intuitively, we're indexing packets into a
//     tree of flows, where each flow keeps track of a list of packets.
//   - For subindex (ip, port) indexing, we're indexing a flow based on
//     the key values. That tree keeps a list of matching flows at each node.
struct index_node * splay_tr_insert(struct index_node    * root, 
                                    struct packet_record * key,
                                    struct index_node * flow,
                                    keytype_t tt) {

    // The current node we're looking at in the tree.
    struct index_node * curr_node;
    // The parent of the current node.
    struct index_node * p_node = NULL;
    // The grandparent of the current node.
    struct index_node * gp_node = NULL;
    struct index_node * tmp_node = NULL;

    // Have we found where to put this node?
    int found = 0;

    TERR("idx(%lu): Inserting into %s tree.\n", pthread_self(), kt_name(tt));

    // The tree is empty.
    if (root == NULL) {
        // This is the new root of the tree.
        TERR("idx(%lu): Empty Tree.\n", pthread_self());
        curr_node = mk_index_node(key);
    } else {
        curr_node = root;
        p_node = NULL;
        while (found == 0) {
            int cmp_result = gen_cmp(key, curr_node->key, tt);
            TERR("idx(%lu): Searching tree. Result: %d\n", pthread_self(), cmp_result);
            switch (cmp_result) {
                // Left branch, the key is less than our current.
                case -1:
                    TERR("Left\n");
                    tmp_node = curr_node->left;
                    curr_node->left = p_node;
                    p_node = curr_node;
                    curr_node = tmp_node;
                    if (curr_node == NULL) {
                        curr_node = mk_index_node(key);
                        found = 1;
                    }
                    break;
                    // Right branch, the key is more than our current.
                case 1:
                    TERR("Right\n");
                    tmp_node = curr_node->right;
                    curr_node->right = p_node;
                    p_node = curr_node;
                    curr_node = tmp_node;
                    if (curr_node == NULL) {
                        curr_node = mk_index_node(key);
                        found = 1;
                    }
                    break;
                    // We found the matching entry.
                case 0:
                    TERR("Found\n");
                    found = 1;
                    break;
                default:
                    ERR("Unreachable comparison case: %d.", cmp_result);
                    return NULL;
            }
        }
    }

    // Depending on the index type, handle adding to the entry to list of packets/flows
    // for the given node.
    if (tt == kt_FLOW) {
        // Only add a new list node if this isn't the first packet.
        if (key != curr_node->key) {
            struct pkt_list_node *pkt_ln = malloc(sizeof(struct pkt_list_node));
            pkt_ln->rec = key;
            pkt_ln->next = NULL;
            // last will be NULL if the flow only had one packet so far.
            if (curr_node->ll.pkts.last != NULL) {
                curr_node->ll.pkts.last->next = pkt_ln;
                curr_node->ll.pkts.last = pkt_ln;
            } else {
                // This is the first list node for this flow (second packet).
                curr_node->ll.pkts.last = pkt_ln;
                curr_node->ll.pkts.first = pkt_ln;
            }
        }
    } else {
        // Add the flow to this entry's flow list.
        struct flow_list_node * flow_ln = malloc(sizeof(struct flow_list_node));
        flow_ln->flow = flow;
        flow_ln->next = NULL;
        if (curr_node->ll.flows.first == NULL) {
            curr_node->ll.flows.first = flow_ln;
            curr_node->ll.flows.last = flow_ln;
        } else {
            curr_node->ll.flows.last->next = flow_ln;
            curr_node->ll.flows.last = flow_ln;
        }
    }    TERR("idx(%lu): Node inserted, splaying.\n", pthread_self());

    // Since this will be the root of a new tree, we don't need to splay.
    if (root == NULL) {
        return curr_node;
    }

    // We need to know what the next parent of our current (inserted) node is.
    // In the beginning, it will be the same parent node as after the insertion process.
    struct index_node * next_parent_node = p_node;

    // Performs a splay operation on the binary tree rooted at 'root' using
    // 'splay_nd'. See the wikipedia 'splay tree' entry.
    while ( next_parent_node != NULL ) {
        int cmp_result;

        // Get the parent node we saved from last time.
        p_node = next_parent_node;

        // Find the grandparent node and fix parent->current pointer.
        // Fixing the pointers at this stage isn't strictly necessary, 
        // but it means we can compare pointers rather than keys for the
        // rest of the splay step.
        cmp_result = gen_cmp(curr_node->key, p_node->key, tt);
        TERR("idx(%lu): splay, curr: %p, prnt: %p, cmp_result: %d\n", pthread_self(), 
                               curr_node->key, p_node->key, cmp_result);
        if (cmp_result < 0) {
            gp_node = p_node->left;
            p_node->left = curr_node;
        } else if (cmp_result > 0) {
            gp_node = p_node->right;
            p_node->right = curr_node;
        } else {
            TERR("Comparing same node in splay? Inconceivable.\n");
            exit(1);
        }
        // Finding the grandparent's parent, and fixing that pointer too.
        if (gp_node != NULL) {
            if (gen_cmp(p_node->key, gp_node->key, tt) < 0) {
                next_parent_node = gp_node->left;
                gp_node->left = p_node;
            } else {
                next_parent_node = gp_node->right;
                gp_node->right = p_node;
            }
        } else {
            // There is no grandparent, so there is no next parent either.
            next_parent_node = NULL;
        }

        // Zig
        // Rotate on the edge between the parent and the node.
        // We know we have a parent because curr_node isn't the root.
        if (gp_node == NULL) {
            if (curr_node == p_node->left) {
                TERR("Zig left\n");
                // The node is to the left: 
                //   - The splay node's right becomes the parents left.
                //   - The parent becomes the splay node's right.
                p_node->left = curr_node->right;
                curr_node->right = p_node;
            } else {
                TERR("Zig right\n");
                // The node is to the right: 
                //   - The splay node's right becomes the parents left.
                //   - The parent becomes the splay node's right.
                p_node->right = curr_node->left;
                curr_node->left = p_node;
            }

        // Zig-Zig (left-left)
        // Rotate on the p/gp edge, then the p/splay node edge
        } else if (gp_node->left == p_node && p_node->left == curr_node) {
            TERR("Zig Zig\n");
            // The parent node's right becomes the grandparent's left.
            gp_node->left = p_node->right;
            // The grandparent becomes the parent's new right.
            p_node->right = gp_node;
            // The splay node's right becomes the parents left.
            p_node->left = curr_node->right;
            // The original parent becomes the splay node's right.
            curr_node->right = p_node;
        
        // Zag-Zag (right-right)
        // Rotate on the p/gp edge, then the p/splay_node edge.
        } else if (gp_node->right == p_node && p_node->right == curr_node) {
            TERR("Zag Zag\n");
            // The parent node's left becomes the grandparent's right
            gp_node->right = p_node->left;
            // The grandparent node becomes the parent's left.
            p_node->left = gp_node;
            // The splay node's left become's the parent's right.
            p_node->right = curr_node->left;
            // The parent becomes the splay node's left.
            curr_node->left = p_node;

        // Zig (left) - Zag (right) 
        // Rotate on the p/splay_node edge, then on the new gp/splay_node edge.
        } else if (gp_node->left == p_node && p_node->right == curr_node) {
            TERR("Zig Zag\n");
            // The splay node's left becomes the parent's right.
            p_node->right = curr_node->left;
            // The splay node's right becomes the grandparent's left.
            gp_node->left = curr_node->right;
            // The parent node becomes the splay node's left.
            curr_node->left = p_node;
            // The grandparent node becomes the splay node's right.
            curr_node->right = gp_node;


        // Zag (right) - Zig (left)
        // Rotate on the p/splay_node edge, then on the new gp/splay_node edge.
        } else if (gp_node->right == p_node && p_node->left == curr_node) {
            TERR("Zag Zig\n");
            // The splay node's right becomes the parent's left.
            p_node->left = curr_node->right;
            // The splay node's left becomes the grand-parent's right.
            gp_node->right = curr_node->left;
            // The parent becomes the splay node's right.
            curr_node->right = p_node;
            // The grandparent becomes the splay node's left.
            curr_node->left = gp_node;
        } else {
            // XXX This should be temporary during testing.
            ERR("Unreachable case (splay)\n");
        }

    }
    return curr_node;
}

void splay_tr_graph_node(struct index_node *, keytype_t, FILE *);

// Generate a graphvis file for the graph in the given bucket's indexes of keytype_t
// The file is saved in /tmp/cornet/test_graphs/<start_time.gv>
// This is for debugging purposes.
void splay_tr_graph(struct bucket * bkt, keytype_t tt) {
    char timebuf[200], filename[200];
    size_t result;
    struct timeval ts = {bkt->first_pkt->header.ts.tv_sec, 0};
    struct tm *time;
    FILE * outfile; 
    
    struct index_node * root;

    if (mkdir("/tmp/cornet", 00777) == -1) {
        if (errno != EEXIST) {
            ERR("Could not create /tmp/cornet: %d\n", errno);
            return;
        }
    }
    if (mkdir("/tmp/cornet/test_graphs", 00777) == -1) {
        if (errno != EEXIST) {
            ERR("Could not create /tmp/cornet/test_graphs: %d\n", errno);
            return;
        }
    }

    // Make the filename from the bucket's first packet timestamp.
    time = gmtime(&(ts.tv_sec));
    result = strftime(timebuf, 200, "%F_%T", time);
    if (result == 0) {
        ERR("Time formating error.\n");
        return;
    }
    snprintf(filename, 200, "/tmp/cornet/test_graphs/%s.%s.gv", timebuf, kt_name(tt));

    outfile = fopen(filename, "w");
    if (outfile == NULL) {
        ERR("Could not open graphfile %s\n", filename);
        return;
    }

    // Grab the correct index based on the bucket.
    switch (tt) {
        case kt_FLOW:
            root = bkt->indexes->flows; break;
        case kt_SRCv4:
            root = bkt->indexes->srcv4; break;
        case kt_DSTv4:
            root = bkt->indexes->dstv4; break;
        case kt_SRCv6:
            root = bkt->indexes->srcv6; break;
        case kt_DSTv6:
            root = bkt->indexes->dstv6; break;
        case kt_SRCPORT:
            root = bkt->indexes->srcport; break;
        case kt_DSTPORT:
            root = bkt->indexes->dstport; break;
        default:
            ERR("Invalid index type.\n");
            return;
    }
    
    fprintf(outfile, "digraph Tree {\n");

    if (root != NULL) {
        splay_tr_graph_node(root, tt, outfile);
    }

    fprintf(outfile, "}\n");
    fclose(outfile);
};

// Print to outfile the graphvis representation of this node.
void splay_tr_graph_node(struct index_node * node, keytype_t tt, FILE * outfile) {

    char * label;
    char port_buf[11];

    // Create the label depending on the tree type.
    switch (tt) {
        case kt_FLOW:
            label = flowtostr(node->key) ;
            break;
        case kt_SRCv4:
        case kt_SRCv6:
            label = iptostr(&node->key->src); break;
        case kt_DSTv4:
        case kt_DSTv6:
            label = iptostr(&node->key->dst); break;
        case kt_SRCPORT:
            snprintf(port_buf, 10, "%u", node->key->srcport); 
            label = port_buf; 
            break;
        case kt_DSTPORT:
            snprintf(port_buf, 10, "%u", node->key->dstport); 
            label = port_buf;
            break;
        default:
            label = "Bad tree type";
    };

    fprintf(outfile, "node%lu [label=\"%s\"]\n", (uint64_t)node->key, label);
    if (node->left != NULL) {
        fprintf(outfile, "node%lu -> node%lu [color=green];\n", (uint64_t)node->key, 
                                                                (uint64_t)node->left->key);
        splay_tr_graph_node(node->left, tt, outfile);
    } 
    if (node->right != NULL) {
        splay_tr_graph_node(node->right, tt, outfile);
        fprintf(outfile, "node%lu -> node%lu [color=red];\n", (uint64_t)node->key, 
                                                              (uint64_t)node->right->key);
    }
    
}

// Merge the two given lists together, least to greatest.
struct flow_list_node * merge_flow_lists(struct flow_list_node * h1, struct flow_list_node * h2) {

    // The head of the list we'll return.
    struct flow_list_node * head = NULL;
    // A pointer to where the next_dest node should be assigned.
    struct flow_list_node **next_dest = &head;

    // Keep going until we run out of items in one of our lists.
    while (h1 != NULL && h2 != NULL) {

        // We're sorting by flow offsets, least to greatest.
        if (h1->flow->flow_index_offset < h2->flow->flow_index_offset) {
            *next_dest = h1;
            h1 = h1->next;
        } else {
            *next_dest = h2;
            h2 = h2->next;
        }
        // We'll assign our the item we find next_dest to the 'next_dest' pointer of what we just found.
        next_dest = &(*next_dest)->next;
    }

    // Since one or the other list ran out of nodes, just attach the entire
    // remaining list to the end.
    if (h1 != NULL) *next_dest = h1;
    if (h2 != NULL) *next_dest = h2;

    return head;
};

#define MAX_ORDER 64
// Merge sort the given list of nodes iteratively
struct flow_list_node * merge_sort_offsets(struct flow_list_node * head) {
    // Merge sort the given list of nodes iteratively, incorporating the nodes in the original
    // linked list in order, one by one. While doing this, we keep track of merged lists we've already
    // generated. If we generate a list that has a matching list of the same size (powers of two),
    // then we merge them into a new list of double that size. If no such matching list exists, then
    // we store that list to be merged later. Each node incorporated starts as a list of length 1 and
    // follows this procedure.
    // Once all the nodes have been merged into a stored list, the remaining unmerged lists are
    // combined, and the final result is returned.

    // The following array is used to keep track of unmatched lists of each 'order' (power of 2).
    // Since we can keep track of MAX_ORDER such lists, the max nodes this supports is 2^MAX_ORDER.
    struct flow_list_node * order_lists[MAX_ORDER] = {NULL};

    // The order of current list.
    uint8_t order;
    // The current list we're merging.
    struct flow_list_node * curr_list;

    while (head != NULL) {
        // For each node we add in, it starts as a list of order 0
        curr_list = head;
        order = 0;
        // Get the next list node for next iteration, and clear the next pointer
        // of our current node so we know where our new list ends.
        head = head->next;
        curr_list->next = NULL;

        // If there already exists a list of this order, merge them into a list of the next higher
        // order.
        while(order_lists[order] != NULL) {
            curr_list = merge_flow_lists(order_lists[order], curr_list);
            // This list is now part of a larger one.
            order_lists[order] = NULL;
            // Our list is now one power of two larger.
            order++;
            // Make sure our order isn't more than we can handle.
            if (order > MAX_ORDER) {
                ERR("Too many items for merge_sort_flows. order: %d\n", order);
                return NULL;
            }
        }
        // We've run out of lists to merge with, so store the list as it's current order.
        order_lists[order] = curr_list;
    }
    // At this point all nodes will be merged into various lists.
    // Since the original list might not be a power of two, however, we need to merge all
    // the orderlists together to get the final one.
    order = 0;
    // Our prior current list will be in the order_lists already.
    curr_list = NULL;
    while (order < MAX_ORDER) {
        // We only need to work with lists that exist.
        if (order_lists[order] != NULL) {
            if (curr_list == NULL) {
                // We start with the lowest order list as our base.
                // We could work the other way, but that would mean always merging larger lists at each
                // step (and
                curr_list = order_lists[order];
            } else {
                // Since we have a current list, merge the list we found with it.
                curr_list = merge_flow_lists(order_lists[order], curr_list);
            }
        }
        order++;
    }
    return curr_list;
};

// Recursively walk the tree and print each index node.
void print_index(struct index_node *node, keytype_t kt) {
    if (node->left != NULL) {
        print_index(node->left, kt);
    }

    if (kt == kt_FLOW) {
        printf("(s,l,r)(%p,%p,%p) offs(%016lx) - ", node, node->left, node->right,
               node->flow_index_offset);
        print_packet(node->key, "\n");
    } else {
        switch (kt) {
            case kt_SRCv4:
                printf("(s,l,r)(%p,%p,%p) %15s: ", node, node->left, node->right,
                       iptostr(&node->key->src));
                break;
            case kt_SRCv6:
                printf("(s,l,r)(%p,%p,%p) %39s: ", node, node->left, node->right,
                       iptostr(&node->key->src));
                break;
            case kt_DSTv4:
                printf("(s,l,r)(%p,%p,%p) %15s: ", node, node->left, node->right,
                       iptostr(&node->key->dst));
                break;
            case kt_DSTv6:
                printf("(s,l,r)(%p,%p,%p) %39s: ", node, node->left, node->right,
                       iptostr(&node->key->dst));
                break;
            case kt_SRCPORT:
                printf("(s,l,r)(%p,%p,%p) %5u: ", node, node->left, node->right,
                       node->key->srcport);
                break;
            case kt_DSTPORT:
                printf("(s,l,r)(%p,%p,%p) %5u: ", node, node->left, node->right,
                       node->key->dstport);
                break;
            default:
                printf("Bad keytype ");
        }
        struct flow_list_node * flow_node = node->ll.flows.first;
        printf("(l,r)(%p,%p) offs(%lu) - ", node->left, node->right, node->flow_index_offset);
        while (flow_node != NULL) {
            printf("idx_offs(%016lx) ", flow_node->flow->flow_index_offset);
            print_packet(node->key, "\n");
            flow_node = flow_node->next;
        }
    }

    if (node->right != NULL) {
        print_index(node->right, kt);
    }
}
