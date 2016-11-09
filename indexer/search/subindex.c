#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../pcapdb.h"
#include "../keys.h"
#include "../output.h"
#include "ordered_set.h"
#include "search.h"

// Where we are in a subindex tree.
// The index of these trees counts from 1, so as to simplify the math.
struct tree_pos {
    uint64_t index;
    uint32_t node_depth;
    uint32_t tree_depth;
    int64_t last_match_index;
};

// Basically, update pos to point to the head of the left or right subtree.
// Not Basically: The head of the left (or right) subtree is offset by (subtree_size+1)/2.
//      subtree_size = 2^(tree_depth - node_depth) - 1
//      index = index +/- (subtree_size + 1)/2
//            = index +/- 2^(tree_depth - node_depth)/2
//            = index +/- 2^(tree_depth - node_depth - 1)
//            = index +/- 1 << (tree_depth - node_depth - 1)
//  Since we also need to increment the node_depth, we eliminate the -1 by doing a pre-increment operation.
#define pos_update_left(pos) pos->index-= 1UL << (pos->tree_depth - ++pos->node_depth);
#define pos_update_right(pos) pos->index+= 1UL << (pos->tree_depth - ++pos->node_depth);

off_t file_size(int);
int search_preview(uint8_t *, struct fcap_idx_header *, struct tree_pos *, union kt_ptrs);
int search_index(struct fcap_idx_header *, int, struct tree_pos *, union kt_ptrs);
int output_matches(struct fcap_idx_header *, int, struct ordered_set *,
        struct tree_pos *, union kt_ptrs, keytype_t);

int search_subindex(struct subindex_search_descr * descr, char * subidx_fn, char * result_path) {

    int index_fd;
    int ret;

    index_fd = open(subidx_fn, O_RDONLY);
    if (index_fd == -1) {
        fprintf(stderr, "Could not open index file: %s\n", subidx_fn);
        usage();
        return EIO;
    }

    // Allocate and initialize an ordered set for output. The cleanup function frees these,
    // So we can't reuse them. (Maybe we should fix that later...
    struct ordered_set * oset_out = calloc(1, sizeof(struct ordered_set));
    ret = ord_set_init(oset_out, OSET_OFFSET, OSET_WRITE, result_path);
    if (ret != 0) {
        if (ret == EEXIST) {
            // File already exists; We don't have to do this work.
            return 0;
        } else {
            CRIT("Unknown error opening output file: %s, error: %s", result_path, strerror(errno));
            return EINVAL;
        }
    }

    // Grab the first disk block of data.
    uint8_t buffer[DISK_BLOCK] = {0};
    ssize_t bytes_read = read(index_fd, buffer, DISK_BLOCK);
    if (bytes_read < sizeof(struct fcap_idx_header)) {
        fprintf(stderr, "Invalid index file (too small to have a full header.\n");
        return EBADSLT;
    }
    // Copy the header information.
    struct fcap_idx_header header;
    memcpy(&header, buffer, sizeof(struct fcap_idx_header));

    // Make sure our file size is correct.
    off_t file_end = file_size(index_fd);
    size_t offset_size = header.offset64 ? sizeof(uint64_t) : sizeof(uint32_t);
    off_t file_data_size = (kt_key_size(descr->type) + offset_size)*header.records;
    size_t file_hdr_size = header.preview != 0 ? DISK_BLOCK : sizeof(struct fcap_idx_header);
    if (file_end != (file_data_size + file_hdr_size)) {
        fprintf(stderr, "Bad file size. Have: %lu bytes, expected %lu = %lu (data) + %lu (header+preview)",
                file_end, (file_data_size + file_hdr_size), file_data_size, file_hdr_size);
        return EBADSLT;
    }

    // Some sanity checking
    if (header.ident != HEADER_IDENT) {
        ERR("ident: %u, should be: %u\n", header.ident, HEADER_IDENT);
        // not an fcap index file!
        ERR("Not a valid fcap index file: %s\n", subidx_fn);
        return EBADSLT;
    }

    if (descr->type != (keytype_t)header.key_type) {
        ERR("File key type does not match internal key type.");
        return EBADSLT;
    }

    // Incompatible header version
    if (header.version > 1) {
        // version's not right
        ERR("%s: in %s: %s\n", "search_atom", "main", "unsupported version");
        return EBADSLT;
    }

    if (kt_key_cmp(descr->start_key, descr->end_key, descr->type) == 1) {
        ERR("The end of the given range must be less than the beginning.\n");
        return EINVAL;
    }

    // For a full explanation of how we traverse this tree, see the function comment for
    // write_index() in indexer/output.c.
    // Remember: All tree positions are counted from 1.
    uint32_t tree_depth = 1;
    uint64_t cap = 2;

    // Find the depth of a left-filled tree given
    while ((cap-1) < header.records) {
        cap = cap << 1;
        tree_depth++;
    }

    // We'll use this to track our position in the tree, and where our last match was.
    struct tree_pos t_pos = {
            .index = 1UL << (tree_depth - 1UL),
            .node_depth = 1,
            .tree_depth = tree_depth,
            .last_match_index = -1};

    TERR("Preview items: %u\n", header.preview);

    // Search using the preview index, updating position as necessary.
    if (header.preview != 0) {
        ret = search_preview(buffer, &header, &t_pos, descr->start_key);
        if (ret != 0) {
            INFO("Failed searching preview. %d", ret);
            return ret;
        }
    }
    TERR("hmm: t_pos: %lu, %d, %u\n", t_pos.index, t_pos.last_match_index, t_pos.node_depth);

    // Find our item in the main index, updating position so that it points to the correct item.
    ret = search_index(&header, index_fd, &t_pos, descr->start_key);
    if (ret != 0) {
        INFO("Failed searching index. %d", ret);
        return ret;
    }
    TERR("hmm2: t_pos: %lu, %d, %u\n", t_pos.index, t_pos.last_match_index, t_pos.node_depth);

    if (t_pos.last_match_index != -1) {
        // Output our results to the given output file.
        ret = output_matches(&header, index_fd, oset_out, &t_pos, descr->end_key, descr->type);
        if (ret != 0) {
            INFO("Failed outputing matches %d.", ret);
            return ret;
        }
    }

    ord_set_cleanup(oset_out);
    close(index_fd);

    return 0;
}

// Search the preview index for the position of the key with the lowest index that's greater than
// or equal to our key. If our key exists in the tree, the lowest indexed instance will be the result.
// If our key doesn't exist in the tree, lowest indexed item greater than our key is returned.
// We need to do it this way for searching ranges.
int search_preview(uint8_t * buffer, struct fcap_idx_header * hdr,
        struct tree_pos * t_pos, union kt_ptrs key) {
    keytype_t key_type = (keytype_t) hdr->key_type;
    size_t key_size = kt_key_size(key_type);

    uint64_t pvw_size = DISK_BLOCK/key_size;
    uint32_t pvw_depth = 0;
    uint32_t pvw_cap = 2;

    // Find the depth of the preview tree.
    while ((pvw_cap - 1) < pvw_size) {
        pvw_cap = pvw_cap << 1;
        pvw_depth++;
    }
    TERR("P_tree depth: %u, %u items\n", pvw_depth, 1U << pvw_depth);

    // Find the root of the preview tree.
    uint32_t n_idx = 1U << (pvw_depth - 1);
    TERR("n_idx, hdr->preview: %u, %u\n", n_idx, hdr->preview);

    // The depth of the node we're looking at.
    uint32_t n_depth = 1;
    union kt_ptrs p_key;

    TERR("prev: t_pos: %lu, %d, %u\n", t_pos->index, t_pos->last_match_index, t_pos->node_depth);
    TERR("n_idx, pvw_depth, n_depth: %u, %u, %u\n", n_idx, pvw_depth, n_depth);
    while (n_depth <= pvw_depth) {
        size_t buf_off = (n_idx - 1)*key_size + sizeof(struct fcap_idx_header);
        if (n_idx > hdr->preview) {
            if (t_pos->index > hdr->records) {
                // We just tried to look at a node beyond the end of the tree. That's actually
                // fine, as this 'virtual' node must be more than what we're looking for, and its
                // left branch may be populated.
                pos_update_left(t_pos);
                continue;
            } else {
                // In contrast to the above, this means we just exceeded the edge of the preview
                // index. This happens sometimes because the preview index and header don't fit
                // precisely within 4096 bytes. We'll just have to walk the tree the hard way from
                // here.
                TERR("Beyond preview right edge. idx: %u, %u\n", n_idx, hdr->preview);
                return 0;
            }
        }
        p_key.generic = &(buffer[buf_off]);
        TERR("key: %s <=> ", kt_key_str(key, key_type));
        TERR("%s\n", kt_key_str(p_key, key_type));
        switch (kt_key_cmp(key, p_key, key_type)) {
            case 0:
                // Also go left.
                t_pos->last_match_index = (int64_t) t_pos->index;
            case -1:
                TERR("left\n");
                // Less than or equal. We have to search to the bottom of the tree, since we need the
                // left most matching item.
                // Move our main tree position left.
                pos_update_left(t_pos);
                // Move our preview tree position left.
                n_idx = n_idx - (1 << (pvw_depth - ++n_depth));
                break;
            case 1:
                TERR("right\n");
                // More than.
                // Move our main tree position right.
                pos_update_right(t_pos);
                // Move our preview tree position right.
                n_idx = n_idx + (1 << (pvw_depth - ++n_depth));
                break;
            default:
                CRIT("Supposedly unreachable comparison case.");
                return EINVAL;
        }
        TERR("1 prev: t_pos: %lu, %d, %u, n_idx: %u\n", t_pos->index, t_pos->last_match_index, t_pos->node_depth, n_idx);
        TERR("1 n_idx, pvw_depth, n_depth: %u, %u, %u\n", n_idx, pvw_depth, n_depth);
    }

    return 0;
}

// How much of a buffer are we willing to read at once when trying to finish our search?
// This is entirely arbitrary. For now I've set it to 1 MB
#define MAX_TREE_BUFFER DISK_BLOCK*16

// This is pretty much exactly the same as searching the preview index, except now we preload
// the entire remaining sub-tree for our search. Given the preview index, this shouldn't be very large.
// After this is done, the pos->index value probably won't make any sense, but what we
// need is the last_match_index anyway.
int search_index(struct fcap_idx_header * hdr, int idx_fno, struct tree_pos * pos, union kt_ptrs key) {
    keytype_t key_type = (keytype_t)hdr->key_type;

    // Calculate how much of the index we need to read to get it all in one operation.
    uint32_t rel_depth = pos->tree_depth - pos->node_depth;
    // The size of the subtree at under (and including) our current node.
    // This size is actually one less than this, which we'll subtract when appropriate.
    uint64_t subtree_size = 1UL << (rel_depth + 1);
    size_t key_size = kt_key_size(key_type);
    if (key_size == 0) return EINVAL;
    size_t offset_size = hdr->offset64 ? sizeof(uint64_t) : sizeof(uint32_t);
    size_t record_size = key_size + offset_size;
    size_t bytes_to_read = (subtree_size - 1) * record_size;
    TERR("Offset size: %lu, subtree_size: %lu, record_size: %lu\n",
         offset_size, subtree_size, record_size);

    size_t header_size = hdr->preview > 0 ? DISK_BLOCK : sizeof(struct fcap_idx_header);

    // An index file can potentially be very large. It's a corner case, but it is possible.
    // Rather than trashing the system memory trying to buffer a huge file, we keep walking the
    // tree the hard way until the buffer is small enough.
    // The need for this may seem obvious, but in the vast majority of cases what's left
    // at this point should be on the order of a few KB.
    while (bytes_to_read > MAX_TREE_BUFFER) {
        TERR("Walking the tree the slow way because bytes_to_read > MAX_TREE_BUFFER\n");
        // Do one search step
        uint64_t step_rec_pos = header_size + pos->index*record_size;
        TERR("Step_rec_pos: %lu, header_size: %lu, pos->index: %lx\n",
             step_rec_pos, header_size, pos->index);
        lseek(idx_fno, step_rec_pos, SEEK_SET);
        union kt_ptrs step_key;
        uint8_t step_buff[sizeof(struct in6_addr)];
        step_key.generic = step_buff;

        if (pos->index > hdr->records) {
            // Our position is beyond the number of records. That's OK though. We're on a 'virtual'
            // node in the tree. Everything is less than this.
            pos_update_left(pos);

            // Update how big our buffer would have to be.
            rel_depth = pos->tree_depth - pos->node_depth;
            subtree_size = 1UL << (rel_depth + 1);
            bytes_to_read = (subtree_size - 1) * record_size;
            continue;
        }
        ssize_t bytes_read = read(idx_fno, step_buff, key_size);
        if (bytes_read != key_size) {
            CRIT("Error reading index file: %s. bytes_read %lu", strerror(errno), bytes_read);
            return EIO;
        }
        int cmp_res = kt_key_cmp(key, step_key, key_type);
        TERR("Cmp curr: %s to ", kt_key_str(step_key, key_type));
        TERR("%s, result: %d\n", kt_key_str(key, key_type), cmp_res);
        switch (cmp_res) {
            // We're searching for the leftmost matching value, so go to the left even when we find a match.
            case 0:  // Go left and update last match.
            case -1:
                pos->last_match_index = (int64_t) pos->index;
                pos_update_left(pos);
                break;
            case 1: // Go right.
                pos_update_right(pos)
                break;
            default:
                ERR("Unreachable case.\n");
                return EINVAL;
        }

        // Update how big our buffer would have to be.
        rel_depth = pos->tree_depth - pos->node_depth;
        subtree_size = 1UL << (rel_depth + 1);
        bytes_to_read = (subtree_size - 1) * record_size;
    }

    // Find the first record in our subtree.
    uint64_t first_record = pos->index - (subtree_size/2 - 1);
    // Skip the header and preview index,
    // then seek to the left most record in the subtree rooted by our current
    // position. (Adjust for counting from 1).
    size_t read_start = header_size + (first_record - 1) * record_size;
    // Now that we know how much data to read, make a buffer of that size.
    uint8_t buffer[bytes_to_read];

    TERR("bytes_to_read: %lu from pos: %lu at file pos: %lu\n", bytes_to_read, first_record,
         read_start);

    // Seek to our read start position.
    lseek(idx_fno, read_start, SEEK_SET);
    // Fill the buffer with the subtree records.

    // Fill the buffer with records.
    ssize_t bytes_read;
    do {
        bytes_read = read(idx_fno, buffer, bytes_to_read);
    } while (bytes_read < 0 && errno == EINTR);


    // Figure out what the index of the last record in the buffer should be.
    uint64_t last_record = (pos->index + (subtree_size/2 - 1));
    TERR("last_record: %lu, pos->index: %lu\n", last_record, pos->index);
    if (hdr->records < last_record) last_record = hdr->records;

    // Make sure we read the correct number of bytes, taking into account the actual number
    // of records that should be readable.
    if (bytes_read != (last_record - first_record + 1)*record_size) {
        ERR("Invalid number of bytes read. Expected %lu, got %lu.",
                (last_record - first_record + 1)*record_size, bytes_read);
        return EIO;
    }

    union kt_ptrs curr_value;
    size_t offset;
    int cmp_res;
    while (pos->node_depth < pos->tree_depth) {
        // If we've gone beyond the end of our records, then we're at a virtual node.
        // In this case, we always just go left.
        if (pos->index > hdr->records) {
            TERR("pos: %lu, node_depth: %u, tree_depth: %u: Going left.\n",
                pos->index, pos->node_depth, pos->tree_depth);
            pos_update_left(pos);
        }

        // Get the offset in the buffer to the next record, given where the window into the
        // tree that the buffer contains.
        offset = (pos->index - first_record)*record_size;
        curr_value.generic = &buffer[offset];
        cmp_res = kt_key_cmp(key, curr_value, key_type);
        TERR("Cmp curr: %s to ", kt_key_str(curr_value, key_type));
        TERR("%s, result: %d\n", kt_key_str(key, key_type), cmp_res);
        switch (cmp_res) {
            // We're searching for the leftmost matching value, so go to the left even when we find a match.
            case 0:  // Go left and update last match.
                pos->last_match_index = (int64_t) pos->index;
            case -1:
                TERR("pos: %lu, node_depth: %lu, tree_depth: %u: Going left.\n",
                     pos->index, pos->node_depth, pos->tree_depth);
                pos_update_left(pos);
                break;
            case 1: // Go right.
                TERR("pos: %lu, node_depth: %lu, tree_depth: %u: Going right.\n",
                     pos->index, pos->node_depth, pos->tree_depth);
                pos_update_right(pos)
                break;
            default:
                ERR("Unreachable case.\n");
                return EINVAL;
        }
    }

    // Check the item at the bottom of the tree to see if it was a match.
    offset = (pos->index - first_record)*record_size;
    curr_value.generic = &buffer[offset];
    cmp_res = kt_key_cmp(key, curr_value, key_type);
    if (cmp_res == 0) {
        pos->last_match_index = (int64_t) pos->index;
    }

    return 0;
}


// How many ordered sets should we keep around before merging them
#define MAX_OSETS 1024
int output_matches(struct fcap_idx_header *hdr, int idx_fno, struct ordered_set *out_set,
                    struct tree_pos *pos, union kt_ptrs end, keytype_t key_type) {

    struct os_skip_list offset_osets;

    os_slist_init(&offset_osets);

    TERR("pos: %lu, last_match: %d\n", pos->index, pos->last_match_index);

    size_t offset_size = hdr->offset64 ? sizeof(uint64_t) : sizeof(uint32_t);

    union kt_ptrs curr_key;
    union kt_ptrs this_key;
    uint8_t this_key_buff[sizeof(struct in6_addr)] = {0};
    uint8_t curr_key_buff[sizeof(struct in6_addr)] = {0};
    // We need to keep track of the key we're getting results for right now.
    this_key.generic = this_key_buff;
    // This is for the latest key we've pulled.
    curr_key.generic = curr_key_buff;

    size_t key_size = kt_key_size(key_type);

    // Get the position of the key we found
    size_t start_offset = hdr->preview > 0 ? DISK_BLOCK : sizeof(struct fcap_idx_header);
    start_offset += (pos->last_match_index - 1) * (key_size + offset_size);
    lseek(idx_fno, start_offset, SEEK_SET);
    // Tell the kernel we intend to do the rest of our accesses sequentially.
    // This will effectively double the buffer size of our reads.
    posix_fadvise(idx_fno, start_offset, file_size(idx_fno) - start_offset, POSIX_FADV_SEQUENTIAL);

    // Read the first found key from file.
    ssize_t bytes_read;
    do {
        bytes_read = read(idx_fno, curr_key_buff, key_size);
    } while (bytes_read == 0 && errno == EINTR);
    if (bytes_read != key_size) {
        CRIT("Error reading index file: %s", strerror(errno));
        return -1;
    }
    TERR("start_offset: %lu, read: %lu\n", start_offset, bytes_read);

    // For each key between our start and end keys we're going to make an ordered
    // set of the values (offsets) for those keys. For each key, this will be easy,
    // as those offsets are already in order. For multiple keys, however, we'll have to merge
    // the stacks much like with the later OR operation. (using the same code, in fact).
    //
    // The position found by searching is the first instance of the lowest valued key >= start,
    // so we start there.

    // Create the first ordered set.
    struct ordered_set * curr_oset;

    // We keep going until we pull a key > than end.
    while (kt_key_cmp(curr_key, end, key_type) < 1) {

        // Make a new ordered set for our next bunch of offsets.
        curr_oset = calloc(1, sizeof(struct ordered_set));
        int ret = ord_set_init(curr_oset, OSET_OFFSET, OSET_TMP_WRITE, NULL);
        if (ret != 0) {
            CRIT("Error opening multikey ordered set.\n");
            return ret;
        }

        // The current key is now (and starts as) the key we're collecting values for
        memcpy(this_key_buff, curr_key_buff, sizeof(struct in6_addr));

        // We don't need to compare the first time.
        do {
            // Read the offset value.
            uint64_t offset = 0;
            if (safe_read(idx_fno, &offset, offset_size) != 0) return EIO;
            TERR("Pulled key: %s, %lx\n", kt_key_str(curr_key, key_type), offset);
            ord_set_push(curr_oset, &offset);

            // Get the next key value.
            // TODO This is fine until we hit EOF, then what?
            if (safe_read(idx_fno, curr_key_buff, key_size) != 0) return EIO;

        // If the key value is different, then we'll need to start an new ordered set.
        } while (kt_key_cmp(curr_key, this_key, key_type) == 0);

        // Set the current ordered set to read mode.
        ord_set_readmode(curr_oset);
        // and then add it to our skip list.
        os_slist_add(&offset_osets, curr_oset);

        // If we have too many ordered sets, merge them into one.
        // This partial merging strategy isn't super efficient, but it's only in corner cases
        // that we have to use it at all.
        if (offset_osets.size >= MAX_OSETS) {
            struct ordered_set * merged_set = calloc(1, sizeof(struct ordered_set));
            ord_set_init(merged_set, OSET_OFFSET, OSET_TMP_WRITE, NULL);
            os_slist_union(&offset_osets, merged_set);
            ord_set_readmode(merged_set);
            // Reset our skip list
            os_slist_init(&offset_osets);
            // Add the merged set back into our lists.
            os_slist_add(&offset_osets, merged_set);
        }
    }

    // Do a final (and probably only) merge, but this time
    // the results will go to the output file.
    TERR("Doing final union on out_set: %p\n", out_set);
    os_slist_union(&offset_osets, out_set);

    return 0;
}

// Return the offset of the end of the file; the file size
off_t file_size(int file_descriptor) {
    struct stat st;

    if (fstat(file_descriptor, &st) == 0)
        return st.st_size;

    return -1; // Something went wrong! (errno is set by fstat)
}
