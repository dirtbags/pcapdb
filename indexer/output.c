#define _LARGEFILE64_SOURCE

#include <postgresql/libpq-fe.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pcap.h>
#include <fcntl.h>
#include <unistd.h>

#include "bucketize.h"
#include "db.h"
#include "output.h"

void * output(void * arg) {
    struct thread_state * thr_state = (struct thread_state *)arg;
    struct system_state * state = thr_state->sys_state;
    struct bucket *bkt;
    struct bucket *this_bkt, *next_bkt;
    PGconn * pg_cnx = NULL;

    INFO("out(%lu): Output thread running.", pthread_self());

    while (!event_check(&thr_state->shutdown)) {
        output_code_t ob_result;

        thr_state->status = PCAPDB_THR_IDLE;
        bkt = bucketq_pop(&state->indexed_bkts);

        if (bkt == NULL) {
            if ( !event_check(&thr_state->shutdown) && state->indexed_bkts.closed == 0) {
                ERR("NULL bucket in output thread #%lx.", pthread_self());
            }
            continue;
        }

        thr_state->status = PCAPDB_THR_WORKING;
        do {
            // Write the contents of the bucket and assorted indices to disk.
            ob_result = output_bucket(&state->conf, &pg_cnx, bkt);
        // Keep trying as long as we're just dealing with DB errors.
        } while (ob_result == OB_DB_ERR && !event_check(&state->shutdown));

        // Take apart the bucket chain, and put the individual buckets back on the
        // ready queue.
        this_bkt = bkt;
        while (this_bkt != NULL) {
            next_bkt = this_bkt->next;
            this_bkt->next = NULL;
            bucketq_push(&state->ready_bkts, this_bkt);
            this_bkt = next_bkt;
        }
    }

    thr_state->status = PCAPDB_THR_SHUTDOWN;

    if (pg_cnx != NULL) {
        PQfinish(pg_cnx);
    }

    INFO("out(%lu): Output thread exiting.", pthread_self());

    return NULL;
}

output_code_t output_bucket(struct config * conf,
                  PGconn ** thread_pg_cnx,
                  struct bucket * bkt) {
    struct save_info save;
    PGconn * pg_cnx = *thread_pg_cnx;

    struct timeval32 start_tv;
    struct timeval32 end_tv;
    output_code_t ret;

    if (conf->use_db == PCAPDB_USE_DB) {
        // Make sure we have a db connection.
        if (pg_cnx == NULL) {
            *thread_pg_cnx = pg_cnx = get_db_conn(conf);
        }

        // Make sure our db connection is ok.
        if (PQstatus(pg_cnx) != CONNECTION_OK) {
            // Close the connection if it's bad.
            PQfinish(pg_cnx);
            // Try again to create the connection.
            *thread_pg_cnx = pg_cnx = get_db_conn(conf);
            // If the connection still failed, we'll deal with that fact after
            // we fail get the save info.
        }
    }

    // Grab the first and last timestamps from our bucket.
    if (bkt->first_pkt == NULL || bkt->last_pkt == NULL) {
        CRIT("Bucket does not have a first or last packet entry.");
        return OB_BUCKET_ERR;
    }
    start_tv = bkt->first_pkt->header.ts;
    // Get the timestamp of the last packet from the last bucket.
    struct bucket * last_bkt = bkt;
    while (last_bkt->next != NULL) last_bkt = last_bkt->next;
    end_tv = last_bkt->last_pkt->header.ts;

    // Get the save information from the db.
    // If the db has issues, we'll use our backup info.
    if (conf->use_db == PCAPDB_USE_DB) {
        if (set_save_info(conf, pg_cnx, &start_tv, &end_tv, &save) != OB_OK) {
            // On failure, just return an error.
            CRIT("Could not get save info from the db.");
            return OB_DB_ERR;
        }
    } else {
        set_save_info_nodb(&start_tv, &save);
    }

    int os_ret = mkdir(save.index_path, 0755);
    if (os_ret != 0) {
        CRIT("Could not create index directory: %s, errno: %d", save.index_path, os_ret);
        return OB_IO_ERR;
    }

    // Write out each of the indices, in turn. This will also free
    // the nodes in each index, except for the flow index (which is needed when
    // writing the others).
    keytype_t treetype;
    for (treetype = kt_FLOW; treetype <= tt_LAST; treetype++) {
        ret = write_index(conf, &save, bkt->indexes, treetype, &start_tv, &end_tv);
        if (ret != OB_OK) {
            CRIT("Failure writing index: %s or capture file: %s", save.index_id, save.slot_path);
            return ret;
        }
    }

    // Make a symlink to the FCAP file in the index directory
    char slot_symlink_path[BASE_DIR_LEN*2 + 1];
    snprintf(slot_symlink_path, BASE_DIR_LEN*2, "%s/FCAP", save.index_path);
    symlink(save.slot_path, slot_symlink_path);

    // Free all the flow nodes and the timeorder flow list.
    struct flow_list_node * flow_ln = bkt->indexes->timeorder_head;
    struct flow_list_node * next_flow_ln;
    while (flow_ln != NULL) {
        next_flow_ln = flow_ln->next;
        free(flow_ln->flow);
        free(flow_ln);
        flow_ln = next_flow_ln;
    }

    // If we get to this point and can't save info to the database, we don't really care.
    // These errors should be easily recoverable.

    if (conf->use_db == PCAPDB_USE_DB) {
        // Save the stats information to the database.
        save_stats(pg_cnx, bkt->stats, save.index_id);
        // Tell the DB that this time chunk is ready for searching.
        set_index_ready(pg_cnx, save.index_id);
    }

    return OB_OK;
}

// Write the flow in given the given index node both to the FCAP file and the flow index.
output_code_t write_flow(
        struct index_node * node,
        int offset64,
        int fcap_fno,
        int flow_idx_fno) {

    struct fcap_flow_key key = {
            .first_ts       = node->key->header.ts,
            // If the last packet pointer is NULL, then there is only one packet
            .last_ts        = (node->ll.pkts.last == NULL) ?
                    node->key->header.ts : node->ll.pkts.last->rec->header.ts,
            .proto          = node->key->proto,
            .src_ip_vers    = node->key->src.vers,
            .dst_ip_vers    = node->key->dst.vers,
            .size_pow       = 0,
            .packets_pow    = 0,
            .srcport        = node->key->srcport,
            .dstport        = node->key->dstport,
            .src            = node->key->src.addr,
            .dst            = node->key->dst.addr,
            .size           = 0,
            .packets        = 0
    };

    uint64_t fcap_offset = (uint64_t) lseek64(fcap_fno, 0, SEEK_CUR);
    node->flow_index_offset = (uint64_t) lseek64(flow_idx_fno, 0, SEEK_CUR);

    // Write the first packet separately, since it isn't in the packet list.
    uint32_t pkt_len = node->key->header.caplen;
    if (write(fcap_fno, &node->key->header, sizeof(struct pcap_pkthdr32))
            != sizeof(struct pcap_pkthdr32) ||
            write(fcap_fno, &node->key->packet, pkt_len) != pkt_len) {
        CRIT("Could not write packet to capture file.");
        return OB_IO_ERR;
    };
    key.size += pkt_len + sizeof(struct pcap_pkthdr32);
    key.packets++;

    // Write the rest of the packets to file.
    struct pkt_list_node *pl_node = node->ll.pkts.first;
    struct pkt_list_node *next_pl_node;
    while (pl_node != NULL) {
        struct packet_record *pkt = pl_node->rec;
        // Double checking the
        //packet_check(pkt);
        pkt_len = pkt->header.caplen;

        //printf("pkt_len: %d, pkt: ", pkt->header.caplen);
        //print_raw_packet(30, &(pkt->packet), 0, pkt_len, 60);
        if (write(fcap_fno, &pkt->header, sizeof(struct pcap_pkthdr32))
                != sizeof(struct pcap_pkthdr32) ||
                write(fcap_fno, &pkt->packet, pkt_len) != pkt_len) {
            CRIT("Could not write packet to capture file: %s", strerror(errno));
            return OB_IO_ERR;
        };

        key.size += pkt_len + sizeof(struct pcap_pkthdr32);
        key.packets++;
        next_pl_node = pl_node->next;
        free(pl_node);
        pl_node = next_pl_node;
    }
    TERR("Flow: %u<->%u, size: %u, pkts: %u", key.srcport, key.dstport, key.size, key.packets );

    if (write(flow_idx_fno, &key, sizeof(struct fcap_flow_key)) != sizeof(struct fcap_flow_key)) {
        CRIT("Could not write to index file.");
        return OB_IO_ERR;
    };

    // Write the offset pointer into the FCAP file.
    if (offset64) {
        if (write(flow_idx_fno, &fcap_offset, sizeof(uint64_t)) != sizeof(uint64_t)) {
            CRIT("Could not write to flow index file.");
            return OB_IO_ERR;
        }
    } else {
        uint32_t offset = (uint32_t) fcap_offset;
        if (write(flow_idx_fno, &offset, sizeof(uint32_t)) != sizeof(uint32_t)) {
            CRIT("Could not write to flow index file.");
            return OB_IO_ERR;
        }
    }

    return OB_OK;
}

// Open a new FCAP file at the given path.
// This is, with some exceptions, a PCAP file.
// The exceptions:
//    - Packets are not expected to be in time order.
//    - A pcap file is expected to have packets until its end. FCAP files are a fixed size,
//      and any remaining space is wasted.
//    - The 'sigfigs' file header field (generally unused) is used in FCAP files
//      to store the number of packets in the fcap file.
//
// FCAP file size limit: By default we use a 32 bit offset in our flow indexes, which limits
// the fcap size that can be addressed to 2**32 == 4*(1024)^3 == 4 GB, which is our default.
// We could, relatively easily, switch to 64 bit flow index offsets and have larger fcap files.
// Larger fcap files would mean larger, deeper indices, adding an additional HDD seek when
// searching each time you double the index size (which isn't terrible, really). Conversely, larger
// FCAP files would mean less simultanious index searches would need to occur, which should speed up
// searching.
//
// Another odd limit on fcap file size is the 'sigfigs' header in the fcap file format.
// Since it's only 32 bit, we're limited to 4 billion packets total. The worst case
// scenario is a file of zero length packets. Given the 16 byte packet header size that
// means at least 64 GiB of capture could fit before we have issues.
int fcap_open(struct config *conf, char * path, uint64_t packets) {

    if (packets > UINT32_MAX) {
        packets = 0;
        WARN("Too many packets for FCAP header: %s", path);
    }

    struct pcap_file_header header = {
            .magic          = 0xa1b2c3d4,   // Standard pcap magic number.
            .version_major  = 2,            // The latest pcap file version.
            .version_minor  = 4,
            .thiszone       = 0,            //GMT
            .sigfigs        = (uint32_t) packets,      // Number of packets in the fcap.
            .snaplen        = 65535,        // Max snap length
            .linktype       = 1             // LINKTYPE_ETHERNET (There is no .h define for these)
    };

    int fcap;
    int flags = O_WRONLY | O_TRUNC;
    if (conf->use_db == 0) {
        flags = flags | O_CREAT;
        int mode = S_IRUSR | S_IWUSR | S_IRGRP;
        fcap = open(path, flags, mode);
    } else {
        fcap = open(path, flags);
    }
    // Tell the OS to not prefetch any data from this file. We're overwriting it, so it never
    // needs to be read.
    // posix_fadvise(fcap, 0, MAX_CAPLEN, POSIX_FADV_RANDOM);

    if (fcap == -1) {
        CRIT("Could not open slot: %s. (%s)", path, strerror(errno));
        return -1;
    }

    if (write(fcap, &header, sizeof(struct pcap_file_header)) != sizeof(struct pcap_file_header)) {
        CRIT("Could not write to newly open slot: %s.", path);
        close(fcap);
        return -1;
    }

    return fcap;
}

#define has_preview(tt, r) (uint8_t)((tt != kt_FLOW) && (preview_depth(tt, r) != 0))

// Write the index to a save file, generating a preview index if necessary.
// The preview index is essentially the keys from the top N levels of the tree,
// written to the first disk block of the file along with the header.
//
// === On the 'shape' of these trees and files ===
// We're writing to disk a list of items sorted by (key, offset), with the intention of
// eventually searching it using a binary tree search algorithm. While searching in this
// manner can be done in several relatively equivalent ways, the presence of our
// preview index means that we need to choose how we'll traverse the tree here, otherwise
// the preview index nodes won't match how we would have traversed the tree later.
//
// We made the of representing our tree as one 'left-filled'; that is, all left subtrees
// are 'perfect' binary trees, and right nodes must either have a (perfect) left subtree or
// be virtual. Virtual nodes are always greater than any real node in the tree, and have left
// children that are either virtual or a subtree that is also 'left-filled'.
// When represented as a array of sorted (key, value) pairs, the array has no gaps,
// and all virtual nodes are beyond the end of the tree.
//
// This gives our tree some interesting properties (node positions are all counted from 1):
//  1. The tree is in general structured as a 'perfect' binary tree of the minimum
//      depth needed to contain the real nodes, with all the nodes in the same position
//      if all 'virtual nodes' were replaced with real nodes of values larger than our
//      biggest node.
//  2. This means for a tree of a given depth, the root is always in the same
//      position 2^(depth - 1) or (1 << (depth - 1).
//  3. Left branches are also a simple shift and add: n - 1 << (depth - n_depth),
//      as are right branches: n + 1 << (depth - n_depth). Where n_depth
//      is the depth of node n.
//  4. If the index of a node is outside the array bounds, then it is virtual.
//      Virtual nodes can have only left children.
//  5. The exact position of a node in the tree is trivial to calculate given its
//      index and the tree depth. This property is essential for generating the preview
//      index.
output_code_t write_index(
        struct config * conf,
        struct save_info * save,
        struct index_set * idx_set,
        keytype_t keytype,
        struct timeval32 * start_ts,
        struct timeval32 * end_ts) {

    uint8_t flow_offset_64 = (conf->outfile_size - 1) > UINT32_MAX ? true : false;

    // We need to calculate how big the flow index file will be. If it's too big, we'll need to
    // use 64 bit offsets. The larger our output files, the more likely it will be.
    // For 4 GB output files, it's not very likely.
    uint64_t flow_idx_size = sizeof(struct fcap_idx_header) +
                                idx_set->flow_cnt * (sizeof(struct fcap_flow_key) + sizeof(uint32_t) +
                                                       flow_offset_64 * sizeof(uint32_t));
    uint8_t idx_offset_64 = flow_idx_size > UINT32_MAX ? true : false;

    struct index_node * root;
    uint64_t records;
    switch (keytype) {
        case kt_FLOW:
            root = idx_set->flows;
            records = idx_set->flow_cnt;
            break;
        case kt_DSTPORT:
            root = idx_set->dstport;
            records = idx_set->flow_cnt;
            break;
        case kt_SRCPORT:
            root = idx_set->srcport;
            records = idx_set->flow_cnt;
            break;
        case kt_SRCv4:
            root = idx_set->srcv4;
            records = idx_set->srcv4_cnt;
            break;
        case kt_DSTv4:
            root = idx_set->dstv4;
            records = idx_set->dstv4_cnt;
            break;
        case kt_SRCv6:
            root = idx_set->srcv6;
            records = idx_set->srcv6_cnt;
            break;
        case kt_DSTv6:
            root = idx_set->dstv6;
            records = idx_set->dstv6_cnt;
            break;
        default:
            CRIT("Bad keytype: %d", keytype);
            return OB_TREE_ERR;
    }

    // Try to open the index file.
    char idx_path[2*BASE_DIR_LEN] = {0};
    snprintf(idx_path, 2*BASE_DIR_LEN, "%s/%s", save->index_path, kt_name(keytype));
    int idx_fno = open(idx_path, O_WRONLY | O_CREAT | O_EXCL,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (idx_fno == -1) {
        CRIT("Could not open index file: %s", idx_path);
        return OB_IO_ERR;
    }

    // If we have a preview section, go ahead and fill the first disk block with NULLs.
    // Otherwise, just fill the space where the file header will go.
    // Some of the header information (the preview tree and its size) we won't know
    // until after we've written the index, so we write the real values last.
    size_t blank_length = sizeof(struct fcap_idx_header);
    uint8_t null_bytes[DISK_BLOCK] = {0xaa};
    if (has_preview(keytype, records)) {
        blank_length = DISK_BLOCK;
    }
    off64_t wrote = write(idx_fno, &null_bytes, blank_length);
    // Make sure we were actually able to write data.
    off64_t pos = lseek64(idx_fno, 0, SEEK_CUR);
    if (pos != blank_length) {
        CRIT("Could not blank preview index in index file: %s, %ld, %lu, %ld", idx_path, pos,
             blank_length, wrote);
        close(idx_fno);
        return OB_IO_ERR;
    }

    // If we're writing the flow index, we'll also be writing the flows themselves.
    int fcap_fno = -1;
    if (keytype == kt_FLOW) {
        fcap_fno = fcap_open(conf, save->slot_path, idx_set->packet_cnt);
        if (fcap_fno == -1) {
            return OB_IO_ERR;
        }
    }

    uint8_t preview_mem[DISK_BLOCK] = {0};
    // The g_args are mainly for keeping track of preview_index related information,
    // and also passing certain constants. When writing the flow index, we won't
    // actually need this, as it doesn't use a preview index and everything else is
    // implied.
    struct idx_write_node_args g_args = {
            .treetype               = keytype,
            .idx_fno                = idx_fno,
            .preview_list.generic   = preview_mem,
            .pl_i                   = 0,
            .node_num               = 1,
            .total_nodes            = records,
            .key_size               = kt_key_size(keytype),
            .sub_offset64           = idx_offset_64
    };

    struct index_node * curr = root;
    struct index_node * prior_ascend = NULL;
    struct index_node * parent = NULL;
    struct index_node * left;

    // Iteratively walk the tree. Since there are no parent pointers, we instead re-assign
    // the 'left' pointers to point to the parent as we go down.
    // We also free the nodes as we ascend.
    while (curr != NULL) {
        // There are six distinct cases when traversing the tree:
        //  1. We just descended, and can go left (go left).
        //  2. We just descended, can't go left, but can go right (output node then go right).
        //  3. We just descended, can't go left or right. (output node, then ascend).
        //  4. We just ascended from the left, and can go right. (output node, go right).
        //  5. We just ascended from the left, but can't go right. (output node, then ascend).
        //  6. We just ascended from the right. (Free prior node, then ascend).
        // Cases 1 and 6 are unique, and are the first two handled.
        // Cases 2-5 overlap significantly, and are grouped into the third case.
        TERR("node %p, srcport: %u, ", curr, curr->key->srcport);
        if (prior_ascend == NULL && curr->left != NULL) {  // Case 1
            TERR("case 1");
            // We just descended and can go left, so go left.
            left = curr->left;
            curr->left = parent;
            parent = curr;
            curr = left;
        } else if (prior_ascend != NULL && prior_ascend == curr->right) { // Case 6
            TERR("case 6");
            // We just ascended from the right, so free the prior node and ascend again.
            if (keytype != kt_FLOW) {
                free(prior_ascend);
            }
            prior_ascend = curr;
            curr = curr->left;
        } else { // Cases 2-5
            TERR("case 2-5");
            if (prior_ascend == NULL) { // Cases 2 and 3
                // We just descended, and there was no left.
                curr->left = parent;
            } else { // Cases 4 and 5
                // We just ascended from the left, so free the prior node, but only if we're not
                // working on the flow index. (We'll free the node later).
                if (keytype != kt_FLOW) {
                    free(prior_ascend);
                }
            }

            // Output the current node.
            output_code_t res;
            if (keytype == kt_FLOW) {
                res = write_flow(curr, flow_offset_64, fcap_fno, idx_fno);
            } else {
                res = idx_write_node(curr, &g_args);
            }
            if (res != OB_OK) return res;

            if (curr->right != NULL) { // Cases 2 and 4
                // We can descend to the right.
                parent = curr;
                prior_ascend = NULL; // To signify that we didn't just ascend.
                curr = curr->right;
            } else {  // Cases 3 and 5
                // We can't go right, so ascend.
                prior_ascend = curr;
                curr = curr->left;
            }
        }

    }

    // Free the last node.
    if (prior_ascend != NULL && keytype != kt_FLOW) {
        free(prior_ascend);
    }

    // Calculate the maximum preview items we can insert given the fact that we have
    // an index header written at the beginning of this space.
    uint64_t preview_size = (DISK_BLOCK - sizeof(struct fcap_idx_header))/g_args.key_size;
    if (preview_size > g_args.pl_i) {
        // If the max preview items is more than our actual number of preview nodes,
        // use the real number instead.
        preview_size = g_args.pl_i;
    }

    struct fcap_idx_header hdr = {
        .ident      = HEADER_IDENT,
        .key_type   = keytype,
        .version    = 1,
        .offset64   = keytype == kt_FLOW ? flow_offset_64 : idx_offset_64,
        .preview    = (uint16_t) preview_size,
        .start_ts   = *start_ts,
        .end_ts     = *end_ts,
        .records    = records
    };

    // Seek to where the preview index goes.
    lseek64(idx_fno, 0, SEEK_SET);
    // Write the index header.
    if (write(idx_fno, &hdr, sizeof(struct fcap_idx_header)) != sizeof(struct fcap_idx_header)) {
        CRIT("Could not write file index header.");
    }
    // Write the preview index, if necessary.
    if (has_preview(keytype, records)) {
        size_t write_size = g_args.key_size * preview_size;
        if (write(idx_fno, preview_mem, write_size) != write_size) {

        }
    }

    fsync(idx_fno);
    close(idx_fno);
    if (keytype == kt_FLOW) {
        fsync(fcap_fno);
        close(fcap_fno);
    }

    return OB_OK;

};

// Figure out how deep into the tree this node is, given its node_number.
uint32_t node_depth(uint64_t node_num, uint64_t total_nodes) {
    uint32_t depth = 1;
    uint64_t cap = 2;

    // Find the depth of a left-filled tree given
    while ((cap-1) < total_nodes) {
        cap = cap << 1;
        depth++;
    }

    uint32_t d = 1;
    while (d <= depth) {
        uint64_t left_nodes = (1ULL << (depth - d));
        if (left_nodes < node_num) {
            // The node is down the right branch. Offset our node number.
            node_num = node_num - left_nodes;
        } else if (left_nodes == node_num) {
            // We've found our node, return its depth.
            return d;
        }
        // The third condition, left_nodes > node_num, implies a left branch,
        // and we don't need to do anything more for it.
        d++;
    }

    CRIT("Any node should match before now.");
    return 0;
}

// Walk the subtree rooted at 'node', and output the index to file indicated in args.
// It's assumed that the first node you give this function is the tree root, and
// that args is reasonably initialized.
// This will output the given tree in order from least to greatest, with keys of the
// same value sorted according to session order.
output_code_t idx_write_node(
        struct index_node * node,
        struct idx_write_node_args * args) {

    union kt_ptrs key;

    TERR("Writing. ndepth: %p", node->ll.flows.first);
    // The flows were added here in timeorder, but we really need them in flow
    // order, so we'll have to sort the list before writing it.
    struct flow_list_node * flow_ln = merge_sort_offsets(node->ll.flows.first);
    struct flow_list_node * last_flow_ln;
    uint64_t pvw_depth = preview_depth(args->treetype, args->total_nodes);
    while (flow_ln != NULL) {
        // Figure out if this node value should be in our preview tree.
        uint32_t n_depth = node_depth(args->node_num, args->total_nodes);
        if (n_depth == 0) return OB_TREE_ERR;

        bool preview = n_depth <= pvw_depth;

        // Grab the key and update the preview list according to the tree type.
        switch (args->treetype) {
            case kt_SRCv4:
                key.v4 = &(node->key->src.addr.v4);
                TERR("idx write src_IPv4: %s", iptostr(&(node->key->src)));
                // Stick the key value in the preview list, if we're at the appropriate depth.
                if (preview) args->preview_list.v4[args->pl_i++] = *key.v4;
                break;
            case kt_DSTv4:
                key.v4 = &(node->key->dst.addr.v4);
                TERR("idx write dst_IPv4: %s", iptostr(&(node->key->dst)));
                if (preview) args->preview_list.v4[args->pl_i++] = *key.v4;
                break;
            case kt_SRCv6:
                TERR("idx write src_IPv6: %s", iptostr(&(node->key->src)));
                key.v6 = &(node->key->src.addr.v6);
                if (preview) args->preview_list.v6[args->pl_i++] = *key.v6;
                break;
            case kt_DSTv6:
                TERR("idx write dst_IPv6: %s", iptostr(&(node->key->dst)));
                key.v6 = &(node->key->dst.addr.v6);
                if (preview) args->preview_list.v6[args->pl_i++] = *key.v6;
                break;
            case kt_SRCPORT:
                TERR("idx write src_PORT: %u", node->key->srcport);
                key.port = &(node->key->srcport);
                if (preview) args->preview_list.port[args->pl_i++] = *key.port;
                break;
            case kt_DSTPORT:
                TERR("idx write dst_PORT: %u", node->key->dstport);
                key.port = &(node->key->dstport);
                if (preview) args->preview_list.port[args->pl_i++] = *key.port;
                break;
            default:
                ERR("Invalid key type.");
                return OB_TREE_ERR;
        }

        // Write the key value.
        if (write(args->idx_fno, key.generic, args->key_size) != args->key_size) {
            ERR("Could not write to sub index file.");
            return OB_IO_ERR;
        }

        if (args->sub_offset64) {
            uint64_t offset = flow_ln->flow->flow_index_offset;
            if (write(args->idx_fno, &offset, sizeof(uint64_t)) != sizeof(uint64_t)) {
                ERR("Could not write to sub index file.");
                return OB_IO_ERR;
            }
        } else {
            // Make sure the offset is actually 32 bit.
            if (flow_ln->flow->flow_index_offset > UINT32_MAX) {
                ERR("Unreasonably sized offset value: %lu", flow_ln->flow->flow_index_offset);
            }

            uint32_t offset = (uint32_t) flow_ln->flow->flow_index_offset;
            if (write(args->idx_fno, &offset, sizeof(uint32_t)) != sizeof(uint32_t)) {
                ERR("Could not write to sub index file.");
                return OB_IO_ERR;
            }
        }

        // Note that we completed a node, and grab the next one from the list.
        args->node_num++;
        last_flow_ln = flow_ln;
        flow_ln = flow_ln->next;
        free(last_flow_ln);
    }
    return OB_OK;
};

// Calculate how deep our preview tree should be. If the entire index worth of
// keys can fit in the preview, then don't make one.
uint64_t preview_depth(keytype_t treetype, uint64_t total_nodes) {
    uint64_t base;
    switch(treetype) {
        case kt_FLOW:
            return 0;
        case kt_SRCv4:
        case kt_DSTv4:
            base = DISK_BLOCK/sizeof(struct in_addr);
            break;
        case kt_SRCv6:
        case kt_DSTv6:
            base = DISK_BLOCK/sizeof(struct in6_addr);
            break;
        case kt_SRCPORT:
        case kt_DSTPORT:
            base = DISK_BLOCK/sizeof(uint16_t);
            break;
        default:
            ERR("Invalid tree type: %d", treetype);
            return 0;
    }

    //TERR("tnodes: %ul, base: %ul", total_nodes, base);

    // We don't want a preview tree if we won't have many nodes.
    if (total_nodes < base) {
        return 0;
    }

    // Basically, calculate log_2(base).
    uint64_t depth=0;
    while (base != 0) {
        base = base >> 1;
        depth++;
    }
    return depth - 1;
}

// Get a capture slot from the capture system database from the next disk
// in the rotation. If the disk rotation is empty, refill it.
#define NEXT_DISK_Q "SELECT id, uuid FROM capture_node_api_disk "\
                    "WHERE mode='ACTIVE' ORDER BY usage LIMIT 1"
#define USAGE_INC_Q "UPDATE capture_node_api_disk SET usage=usage+usage_inc WHERE id=$1;"
#define OLDEST_SLOT_Q "SELECT id from capture_node_api_captureslot where disk_id=$1 "\
                      "ORDER BY used,id LIMIT 1"
#define CLEAR_INDEX_REC_Q "UPDATE capture_node_api_index SET capture_slot_id=NULL "\
                          "WHERE capture_slot_id = $1"
#define UPDATE_SLOT_AGE_Q "UPDATE capture_node_api_captureslot SET used=NOW() where id=$1"
// Note that the django default of 'False' for this table doesn't actually 
// set a default value in the database.
#define NEW_INDEX_Q "INSERT INTO capture_node_api_index "\
                    "(start_ts, end_ts, capture_slot_id, ready, expired) "\
                    "VALUES ($1, $2, $3, false, false) RETURNING id;"
output_code_t set_save_info(struct config * conf,
                  PGconn * cnx,
                  struct timeval32 * start_ts,
                  struct timeval32 * end_ts,
                  struct save_info * save) {
    PGresult *res;
    char disk_id[UINT64_STR_LEN];
    const char * params[3];
    char start_ts_str[PG_TS_LEN], end_ts_str[PG_TS_LEN];

    res = paramExec(cnx, "BEGIN", 0, NULL, NO_TUPLES,
                    "Could not even begin query");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    // Choose the next disk based on usage values.
    // The result value from this will be needed throughout the function,
    // so we don't clear these results until last.
    res = paramExec(cnx, NEXT_DISK_Q, 0, NULL, TUPLES,
                    "Could not get find next disk.");
    if (res == NULL) { 
        return OB_DB_ERR;
    } else {
        strncpy(save->disk_uuid, PQgetvalue(res, 0, 1), UUID_STR_LEN);
        strncpy(disk_id, PQgetvalue(res, 0, 0), UINT64_STR_LEN);
    } PQclear(res);

    // Increment the usage counter for the used disk.
    params[0] = disk_id;
    res = paramExec(cnx, USAGE_INC_Q, 1, params, NO_TUPLES,
                    "Could not increment disk usage.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    // TODO: This could fail with no results. This needs to be handled
    res = paramExec(cnx, OLDEST_SLOT_Q, 1, params, TUPLES,
                    "Could not get oldest slot for disk %s.");
    if (res == NULL) {
        return OB_DB_ERR;
    } else {
        save->slot_id = strtoull(PQgetvalue(res, 0, 0), NULL, 10);
        strncpy(save->slot_id_str, PQgetvalue(res, 0, 0), UINT64_STR_LEN);
    } PQclear(res);

    params[0] = save->slot_id_str;
    res = paramExec(cnx, CLEAR_INDEX_REC_Q, 1, params, NO_TUPLES,
                    "Could not remove capture slot from old index entry.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    res = paramExec(cnx, UPDATE_SLOT_AGE_Q, 1, params, NO_TUPLES,
                    "Could not update slot age.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    pgfmt_timeval(start_ts_str, start_ts); 
    pgfmt_timeval(end_ts_str, end_ts);

    params[0] = start_ts_str;
    params[1] = end_ts_str;
    params[2] = save->slot_id_str;
    res = paramExec(cnx, NEW_INDEX_Q, 3, params, TUPLES, 
                    "Could not create new index entry.");
    if (res == NULL) { 
        return OB_DB_ERR;
    } else {
        strncpy(save->index_id, PQgetvalue(res, 0, 0), UINT64_STR_LEN);
    }
    PQclear(res);

    res = paramExec(cnx, "COMMIT", 0, NULL, NO_TUPLES, "COMMIT failed.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    // XXX Should probably check these for overflow.
    snprintf(save->slot_path, BASE_DIR_LEN*2, "%s/%s/p%09lu.fcap",
             conf->base_data_path, save->disk_uuid, save->slot_id);
    snprintf(save->index_path, BASE_DIR_LEN*2, "%s/%s/%020lu",
             conf->base_data_path, INDEX_DIR_NAME, strtoul(save->index_id, NULL, 10));

    return OB_OK;
}

// Create save file information for when we don't have a database.
void set_save_info_nodb(
        struct timeval32 *ts,
        struct save_info *save) {
    save->disk_uuid[0] = '\0';
    save->index_id[0] = '\0';
    save->slot_id_str[0] = '\0';
    save->slot_id = 0;

    snprintf(save->index_path, BASE_DIR_LEN*2, "%s/%u_%u.%06u",
             NO_DB_BASEPATH, getpid(), ts->tv_sec, ts->tv_usec);
    snprintf(save->slot_path, BASE_DIR_LEN*2, "%s/packets.fcap", save->index_path);
}

#define INDEX_READY_Q "UPDATE capture_node_api_index SET ready=true, readied=now() WHERE id=$1"
output_code_t set_index_ready(
        PGconn * cnx,
        char * index_id) {

    PGresult *res;
    const char * params[1] = {index_id};

    res = paramExec(cnx, INDEX_READY_Q, 1, params, NO_TUPLES, "Could not set index as 'ready'");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    return OB_OK;
}

#define STATS_INS "INSERT INTO capture_node_api_stats "\
                  "(capture_size, ipv4, ipv6, network_other, "\
                  " received, dropped, index_id, interface) "\
                  "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING id"
#define TRANS_STATS_INS "INSERT INTO capture_node_api_transportstats (transport, count, stats_id) "\
                        "VALUES ($1, $2, $3)"
#define ERROR_STATS_INS "INSERT INTO capture_node_api_errorstats "\
                        "(dropped, dll, network, transport, stats_id)"\
                        "VALUES ($1, $2, $3, $4, $5)"
// Save the given network statistic information to the database.
output_code_t save_stats(
        PGconn * cnx,
        struct network_stats * stats,
        char * index_id) {
    char * args[8];
    char arg_vals[8][UINT64_STR_LEN];
    char stats_id[UINT64_STR_LEN];
    uint64_t i;
    PGresult *res;

    res = paramExec(cnx, "BEGIN", 0, NULL, NO_TUPLES, "Could not begin transaction.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    // Prepare the arguments to insert the base stats information,
    // and then do the insert, grabbing the row_id simultaniously.
    snprintf(arg_vals[0], UINT64_STR_LEN, "%lu", stats->chain_size);
    snprintf(arg_vals[1], UINT64_STR_LEN, "%lu", stats->ipv4);
    snprintf(arg_vals[2], UINT64_STR_LEN, "%lu", stats->ipv6);
    snprintf(arg_vals[3], UINT64_STR_LEN, "%lu", stats->other_net_layer);
    snprintf(arg_vals[4], UINT64_STR_LEN, "%lu", stats->if_seen);
    snprintf(arg_vals[5], UINT64_STR_LEN, "%lu", stats->dropped);
    for (i=0; i < 6; i++) args[i] = arg_vals[i];
    args[6] = index_id;
    // TODO: There's a strange bug that causes this to point to junk. Need to figure it out.
    args[7] = "ens192"; //stats->interface;
    res = paramExec(cnx, STATS_INS, 8, (char const * const *) args,
                    TUPLES, "Could not insert stats information.");
    if (res == NULL) return OB_DB_ERR;
    strncpy(stats_id, PQgetvalue(res, 0, 0), UINT64_STR_LEN);
    PQclear(res);

    // Insert a transport statistic for each transport that appeared.
    args[2] = stats_id;
    for (i=0; i < 256; i++) {
        if (stats->transport[i] != 0) {
            snprintf(arg_vals[0], UINT64_STR_LEN, "%lu", i);
            snprintf(arg_vals[1], UINT64_STR_LEN, "%lu", stats->transport[i]);
            args[0] = arg_vals[0];
            args[1] = arg_vals[1];
            res = paramExec(cnx, TRANS_STATS_INS, 3, (char const * const *) args,
                            NO_TUPLES, "Could not insert stats transport info.");
            if (res == NULL) return OB_DB_ERR; PQclear(res);
        }
    }

    // Insert the error information, if there was any.
    if (stats->dropped != 0 ||
        stats->dll_errors != 0 ||
        stats->network_errors != 0 ||
        stats->transport_errors != 0) {

        snprintf(arg_vals[0], UINT64_STR_LEN, "%lu", stats->dropped);
        snprintf(arg_vals[1], UINT64_STR_LEN, "%lu", stats->dll_errors);
        snprintf(arg_vals[2], UINT64_STR_LEN, "%lu", stats->network_errors);
        snprintf(arg_vals[3], UINT64_STR_LEN, "%lu", stats->transport_errors);
        for(i=0; i < 4; i++) args[i] = arg_vals[i];
        args[4] = stats_id;
        res = paramExec(cnx, ERROR_STATS_INS, 5, (char const * const *) args,
                        NO_TUPLES, "Could not insert error stats.");
        if (res == NULL) return OB_DB_ERR; PQclear(res);
    }

    res = paramExec(cnx, "COMMIT", 0, NULL, NO_TUPLES, "Could not commit transaction.");
    if (res == NULL) return OB_DB_ERR; PQclear(res);

    return OB_OK;
};
