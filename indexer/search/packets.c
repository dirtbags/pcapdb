#include "ordered_set.h"
#include <fcntl.h>
#include <assert.h>
#include "../output.h"
#include "search.h"


//#define DEBUG_MODE

// We use a heap to keep track of which our list of nodes has the oldest next packet. Since all we
// care about is this next minimum packet, we don't need a strict ordering.
// The heap algorithm we use is a Pairing Heap, which has O(log n) insert time, and O(1) time
// for every other operation we use.
struct flow_heap {
    // The timestamp for the next packet for this flow.
    struct timeval32 ts;
    // The next header pointer. Is NULL if we haven't actually read
    // the flow yet.
    struct pcap_pkthdr32 * header;
    // The total flow length, when we get around to reading it.
    size_t len;
    // Where the packets are located in the fcap file.
    ssize_t offset;
    // Where we've allocated the buffer for our flow.
    uint8_t *buffer;

#ifdef DEBUG_MODE
    struct fcap_flow_rec rec;
#endif

    // The heap is structured such that each node may have one or more children nodes,
    // but all such children are kept as a linked list with the first child as the head.
    struct flow_heap * child;
    struct flow_heap * sibling;
};
// Test a flow heap node to see if we've read past the end of file.
#define fh_eof(fh) ((void *)fh->header >= (void *)fh->buffer + fh->len)

// Allocate and initialize a new flow heap.
struct flow_heap * fh_init(struct fcap_flow_rec * flow) {
    struct flow_heap * fh = calloc(1, sizeof(struct flow_heap));
#ifdef DEBUG_MODE
    fh->rec = *flow;
#endif
    fh->ts = flow->key.first_ts;
    fh->len = flow->key.size;
    fh->offset = flow->flow_offset;
    return fh;
}

// Read the packets from the fcap file into memory.
int fh_load(struct flow_heap * fh, int fcap_fno) {
    // We assume we haven't already loaded this flow.
    assert(fh->header == NULL);

    uint8_t *buffer = calloc(fh->len, sizeof(uint8_t));

    lseek(fcap_fno, fh->offset, SEEK_SET);
    ssize_t bytes_read = 0;
    // Try to read the whole flow, trying again in case of interrupts.
    do {
        bytes_read = read(fcap_fno, buffer, fh->len);
    } while (bytes_read == -1 && errno == EINTR);

    // Make sure we read all that we expected.
    if (bytes_read != fh->len) {
        CRIT("Could not read flow from fcap file. read: %ld, expected: %ld at %lu, err: %s",
             bytes_read, fh->len, fh->offset, strerror(errno));
        return EIO;
    }

    // Tell the OS we're done with this part of the file.
    posix_fadvise(fcap_fno, fh->offset, fh->len, POSIX_FADV_DONTNEED);

    // For sanity purposes, make sure our first packet timestamp matches what the
    // flow says it should be.
    fh->header = (struct pcap_pkthdr32 *) buffer;
    if (fh->ts.tv_sec != fh->header->ts.tv_sec ||
            fh->ts.tv_usec != fh->header->ts.tv_usec) {
        CRIT("WTF? %d, %d",
             fh->ts.tv_sec != fh->header->ts.tv_sec,
             fh->ts.tv_usec != fh->header->ts.tv_usec);
        CRIT("Mismatched flow and packet timestamp from flow at %lx.", fh->offset);
#ifdef DEBUG_MODE
        union kt_ptrs ptrs;
        ptrs.flow = &fh->rec.key;
        INFO("Mismatch on flow key: %s %lu", kt_key_str(ptrs, kt_FLOW), fh->rec.flow_offset);
        INFO("Packet Header: ts: %u.%6u len: %u",
             fh->header->ts.tv_sec, fh->header->ts.tv_usec, fh->header->caplen);
        // If you're getting a warning about this, it's because debug is enabled when it
        // shouldn't be.
        char * pcap_fn = tempnam(NULL, NULL);
        int tmp_pcap = open(pcap_fn, O_CREAT | O_EXCL | O_WRONLY, 00664);
        struct pcap_file_header hdr = {
                .magic          = 0xa1b2c3d4,   // Standard pcap magic number.
                .version_major  = 2,            // The latest pcap file version.
                .version_minor  = 4,
                .thiszone       = 0,            //GMT
                .sigfigs        = 0,
                .snaplen        = 65535,        // Max snap length
                .linktype       = 1             // LINKTYPE_ETHERNET (There is no .h define for these)
        };
        write(tmp_pcap, &hdr, sizeof(struct pcap_file_header));
        write(tmp_pcap, fh->buffer, fh->len);
        close(tmp_pcap);

        INFO("Wrote flow PCAP to %s\n", pcap_fn);
#endif
        return EFAULT;
    }
    fh->buffer = buffer;

    return 0;
}

// Free the flow heap and associated buffer, if any.
void fh_free(struct flow_heap * fh) {
    if (fh->buffer != NULL) {
        free(fh->buffer);
    }
    free(fh);
}

// Merge two flow heaps, and return the root node.
struct flow_heap * fh_merge(struct flow_heap * fh1, struct flow_heap * fh2) {

    // There was no heap to merge back into, so fh2 is the heap (and in all cases the only thing
    // left in it). The reverse case, where fh2 is NULL, should never occur.
    if (fh1 == NULL) return fh2;

    // Neither node should have a sibling.
    assert(fh1->sibling == NULL && fh2->sibling == NULL);

    // Compare the timestamps of the two heaps.
    if (fh1->ts.tv_sec < fh2->ts.tv_sec ||
            (fh1->ts.tv_sec == fh2->ts.tv_sec && fh1->ts.tv_usec < fh2->ts.tv_usec)) {
        // fh1 has the oldest timestamp, and becomes the heap root.
        // fh2 is now just another child of fh1
        fh2->sibling = fh1->child;
        fh1->child = fh2;
        return fh1;
    } else {
        // fh2 has the oldest timestamp
        fh1->sibling = fh2->child;
        fh2->child = fh1;
        return fh2;
    }
}

// Merge pairs of items in the given node's sibling list, returning the new root.
struct flow_heap * fh_merge_pairs(struct flow_heap * fh) {
    if (fh->sibling == NULL) {
        // Nothing to do, since there are no siblings.
        return fh;
    }

    // Detach the sibling to make the heaps independent.
    struct flow_heap * sibling = fh->sibling;
    fh->sibling = NULL;

    if (sibling->sibling == NULL) {
        // Merge with just our sibling, since it's the last node.
        return fh_merge(fh, sibling);
    }

    // Our sibling has a sibling, so detach that too.
    struct flow_heap * g_sibling = sibling->sibling;
    sibling->sibling = NULL;

    // Merge this node with its sibling
    struct flow_heap * merged = fh_merge(fh, sibling);

    // Now recursively pair merge from the grand_sibling on.
    struct flow_heap * neighbor = fh_merge_pairs(g_sibling);

    // Merge the final two heaps together.
    return fh_merge(merged, neighbor);
}

// Remove the root node and return the new root.
struct flow_heap * fh_del_min(struct flow_heap * fh) {
    struct flow_heap * child = fh->child;

    // If the heap root has no child, then there is no new root.
    if (child == NULL) {
        return NULL;
    }

    // Clear the child from the root, since it's no longer attached to the heap.
    fh->child = NULL;
    // Merge pairs in the child list to produce the new root.
    return fh_merge_pairs(child);
}

// Write the next packet from this flow to the given file.
// Returns 0 on success, an errno otherwise.
int fh_write_packet(struct flow_heap * fh, int pcap_fno, int fcap_fno) {
    // Check to see if we've read anything in yet.
    if (fh->header == NULL) {
        // We haven't read in the flow, so (try to) do so.
        int ret = fh_load(fh, fcap_fno);
        if (ret != 0) return ret;
    }
    
#ifdef DEBUG_MODE
    union kt_ptrs ptrs;
    ptrs.flow = &fh->rec.key;
    INFO("writing %u, %u, %s\n", fh->ts.tv_sec, fh->ts.tv_usec, kt_key_str(ptrs, kt_FLOW));
#endif

    // Make sure the data we're about to write is all within the buffer.
    size_t write_size = (size_t)fh->header->caplen + sizeof(struct pcap_pkthdr32);
    ssize_t bytes_written;
    if (((void *)fh->header - (void *)fh->buffer) + write_size > fh->len) {
        CRIT("Trying to read beyond end of buffer. %p %p %d %ld %ld", (void *)fh->header,
             (void *) fh->buffer, (void *)fh->header - (void *)fh->buffer, write_size, fh->len);
        return EFAULT;
    }
    // Try to write the packets, handling interrupts.
    do {
        bytes_written = write(pcap_fno, fh->header, write_size);
    } while (bytes_written == -1 && errno == EINTR);
    if (bytes_written != write_size) {
        return EFAULT;
    }

    fh->header = (struct pcap_pkthdr32 *)((void*)fh->header + write_size);
    fh->ts.tv_sec = fh->header->ts.tv_sec;
    fh->ts.tv_usec = fh->header->ts.tv_usec;
    return 0;
}

void fh_print(FILE * fd, struct flow_heap * fh) {
    struct flow_heap * first = fh;
    struct flow_heap * sib = NULL;

    while (first != NULL) {
        fprintf(fd, "(%p %u.%u)", first, first->ts.tv_sec, first->ts.tv_usec);
        sib = first->sibling;
        while (sib != NULL) {
            fprintf(fd, " -> (%p %u.%u)", sib, sib->ts.tv_sec, sib->ts.tv_usec);
            sib = sib->sibling;
        }
        fprintf(fd, "\n");
        first = first->child;
    }
}

// TODO: An optimization that we don't do, but probably should:
//       As it stands, when we don't preload all the flows, we end up skipping around the file a bit
//       with our reads (though we do tell the OS we're reading all those parts in advance). It
//       would be at least marginally better, when loading as needed, to progress forward and
//       read everything up to the point we need, so we don't ever have to skip back and forth.

#define OUT_PCAP_PERMS S_IRUSR|S_IWUSR|S_IRGRP|S_IRGRP
int pcap_fetch(char * flows_path, // The flow results file to use.
               char * fcap_path,  // The fcap file to pull from.
               off_t pull_size,   // The total amount of packets to pull (to use to determine
                                  // preloading).
               char * result_path) {  // Where to store the resulting pcap file.
    int ret;

    bool preload = false;
    if (pull_size > PACKET_PREFETCH_LIMIT) {
        preload = true;
    }

    struct ordered_set flows;
    ret = ord_set_init(&flows, OSET_FLOW, OSET_READ, flows_path);
    if (ret != 0) {
        CRIT("Could not open flow set %s", flows_path);
        return ret;
    }

    int fcap_fno = open(fcap_path, O_RDONLY);
    if (fcap_fno == -1) {
        CRIT("Could not open fcap file. %s (%s)", fcap_path, strerror(errno));
        return EINVAL;
    }

    int pcap_fno = open(result_path, O_CREAT | O_RDWR, OUT_PCAP_PERMS);
    if (pcap_fno == -1) {
        CRIT("Could not open pcap file: %s (%s)", result_path, strerror(errno));
        return EINVAL;
    }

    // Generate and write a pcap file header.
    struct pcap_file_header file_header = {
            .magic          = 0xa1b2c3d4,   // Standard pcap magic number.
            .version_major  = 2,            // The latest pcap file version.
            .version_minor  = 4,
            .thiszone       = 0,            //GMT
            .sigfigs        = 0,
            .snaplen        = 65535,        // Max snap length
            .linktype       = 1             // LINKTYPE_ETHERNET (There is no .h define for these)
    };

    ssize_t bytes_written;
    do {
        bytes_written = write(pcap_fno, &file_header, sizeof(struct pcap_file_header));
    } while (bytes_written == -1 && errno == EINTR);
    if (bytes_written != sizeof(struct pcap_file_header)) {
        CRIT("Could not write to pcap file: %s (%s)", result_path, strerror(errno));
        return EIO;
    }

    struct flow_heap * root = NULL;
    while (1) {
        struct fcap_flow_rec flow_rec;
        if (ord_set_pop(&flows, &flow_rec) != 0) {
            // We've read the last flow record.
            break;
        }
        union kt_ptrs p;
        p.flow = &flow_rec.key;

        // Initialize a new flow heap node from the flow record.
        struct flow_heap * new_node = fh_init(&flow_rec);
        if (root == NULL) {
            root = new_node;
        } else {
            // Merge the new node into the flow heap.
            root = fh_merge(root, new_node);
        }

        // Warn the kernel that we're going to be reading this sequentially, and soon.
        posix_fadvise(fcap_fno, new_node->offset, new_node->len, POSIX_FADV_RANDOM|
                                                                 POSIX_FADV_WILLNEED);

        if (preload == true) {
            // Preload all of the pcap data.
            // The advantage of this is that we read the entire file in order.
            // The disadvantage is that we have to read everything we need into memory first.
            if (fh_load(new_node, fcap_fno) != 0) {
                return EFAULT;
            }
        }
    }

    // Since this was allocated on the stack, just close the file descriptor.
    close(flows.fno);

    // Write the packets to file, in time order.
    // This operation, given the heap and the fact that the flows are already time-ordered,
    // is O(n*log(f)), where n is the number of packets and f is the number of flows.
    struct flow_heap * next;
    while (root != NULL) {
        // The next flow to get a packet from is always the root.
        next = root;
        // Remove 'next' from the heap.
        root = fh_del_min(root);

        // Write the next packet from this flow. This will update
        // the flows timestamp.
        ret = fh_write_packet(next, pcap_fno, fcap_fno);
        if (ret != 0) {
            return EFAULT;
        }

        // Check if that was the last packet in the flow.
        if (fh_eof(next)) {
            // Tell the system we're done with reading in that area of the disk, and can flush
            // that portion of its cache.
            posix_fadvise(fcap_fno, next->offset, next->len, POSIX_FADV_DONTNEED);

            // Free this flow heap node.
            fh_free(next);
        } else {
            // If the flow has more packets, merge it back into the heap.
            root = fh_merge(root, next);
        }
    }

    close(fcap_fno);
    close(pcap_fno);
    return 0;
}
