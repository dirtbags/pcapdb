#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../pcapdb.h"
#include "../keys.h"
#include "../output.h"
#include "ordered_set.h"
#include "search.h"

#define FLOW_OK 0
#define FLOW_DISCARD 1

char * make_index_path(char * capture_path, uint64_t index_id) {
    size_t index_path_len = (strlen(capture_path)
                             + 1 // A '/'
                             + strlen(CAPTURE_DIR_NAME)
                             + 1 // A '/'
                             + strlen(INDEX_DIR_NAME)
                             + 1 // A '/'
                             + UINT64_STR_LEN
                             + 1); // Ending NULL
    char * index_path = malloc(index_path_len);
    snprintf(index_path, index_path_len, "%s/%s/%s/%020lu",
             capture_path, CAPTURE_DIR_NAME, INDEX_DIR_NAME, index_id);

    return index_path;
}

char * make_path(char * index_path, char * file_name, char * extension) {
    // Make a path to an input/result file.
    // Extension may be NULL.

    size_t path_len = strlen(index_path)
                      + 1 /* a '/' */
                      + strlen(file_name)
                      + 1; /* Ending NULL */
    if (extension != NULL) {
        path_len += strlen(extension);
    } else {
        extension = "";
    }

    char * path = malloc(path_len);

    snprintf(path, path_len, "%s/%s%s", index_path, file_name, extension);
    return path;
}

int and_results(struct and_descr * op,
                char ** subidx_result_paths,
                char * result_path) {

    struct ordered_set * out_set = NULL;
    // The regular sets are to be intersected.
    struct ordered_set * reg_sets = NULL;
    uint32_t reg_set_count = 0;
    struct ordered_set ** reg_sets_tail = &reg_sets;
    // The union of the inverted sets will be removed from the final set.
    struct os_skip_list inv_sets;
    os_slist_init(&inv_sets);

    //Reset variables for each iteration
    reg_sets = NULL;
    reg_set_count = 0;
    os_slist_init(&inv_sets);

    // Open the output file as an ordered set
    out_set = calloc(1, sizeof(struct ordered_set));
    int ret = ord_set_init(out_set, OSET_OFFSET, OSET_WRITE, result_path);
    if (ret != 0) {
        if (ret == EEXIST) {
            // File already exists, but that's not necessarily a bad thing.
            return 0;
        } else {
            CRIT("Unknown error opening output file: %s, error: %s", result_path, strerror(errno));
            return EINVAL;
        }
        fprintf(stderr, "Could not open output file: '%s'. errno: %d\n", result_path, errno);
        return EIO;
    }

    struct and_item_list * and_item = op->sub_searches;
    char * input_fn = NULL;
    while (and_item != NULL) {
        struct ordered_set * oset = calloc(1, sizeof(struct ordered_set));
        ret = ord_set_init(oset, OSET_OFFSET, OSET_READ,
                           subidx_result_paths[and_item->subindex_search_id]);

        if (ret != 0) {
            CRIT("Could not open input file: %s", input_fn);
            return ret;
        };

        if (and_item->inverted == 0) {
            //initialize regular input file
            reg_set_count++;
            *reg_sets_tail = oset;
            reg_sets_tail = &(oset->next);
        } else {
            os_slist_add(&inv_sets, oset);
        }
        and_item = and_item->next;
    }
    free(input_fn);

    if (reg_sets_tail == NULL) {
        CRIT("At least one non-inverted input file is required.");
        return EINVAL;
    }

    // The list of regular sets forms a 'linked loop'.
    *reg_sets_tail = reg_sets;

    struct ordered_set * inv_set = NULL;
    // If we have more than one inverted set, union them first. This is fairly fast,
    // and will reduce the overall operations needed.
    // Otherwise, just grab the one inverted set.
    if (inv_sets.size == 1) {
        inv_set = os_slist_pop(&inv_sets);
        os_slist_remove(&inv_sets); // Tell the skip list we're removing the item for good.
    } else if (inv_sets.size > 1) {
        inv_set = calloc(1, sizeof(struct ordered_set));
        // Create our destination set for the union. It will use a temp file if it's too big.
        ord_set_init(inv_set, OSET_OFFSET, OSET_TMP_WRITE, NULL);
        // Get the union
        if (os_slist_union(&inv_sets, inv_set) != 0) return EIO;
        // Set it to readmode, so we can pull the items back off again.
        ord_set_readmode(inv_set);
    }

    // The current offset value we're checking. This assumes 32 bit offsets.
    uint64_t curr_item = 0;
    // The number of sets we've seen our current offset on.
    uint64_t match_count = 0;

    // A convenience pointer to our current regular set
    struct ordered_set *curr_set = reg_sets;

    // Have the next item from the inverted set ready
    uint64_t next_inv_item = 0;
    if (inv_set != NULL) {
        // If our inverted sets were all empty, then we don't really have one.
        if (ord_set_pop(inv_set, &next_inv_item) == OSET_EMPTY) {
            ord_set_cleanup(inv_set);
            inv_set = NULL;
        }
    }

    while(1) {
        uint64_t next_item;

        if (match_count == reg_set_count) {
            // To reach here: A match has been found for each regular set.
            // Now we must make sure our item isn't in an inverted set.
            if (inv_set != NULL) {
                // First, get rid of everything in the inverted set that's less than our item
                while ((next_inv_item < curr_item) && (inv_set != NULL)) {
                    // If the inverted set is out of items, get rid of it.
                    if (ord_set_pop(inv_set, &next_inv_item) == OSET_EMPTY) {
                        ord_set_cleanup(inv_set);
                        inv_set = NULL;
                    }
                }

                // We found a match, so the current item (from our regular sets) isn't in the intersection
                if (next_inv_item != curr_item) {
                    // This node is in the intersection. Write it out.
                    ord_set_push(out_set, &curr_item);
                }
            } else {
                // This node is in the intersection. Write it out.
                ord_set_push(out_set, &curr_item);
            }

            match_count = 0;
        }

        // Grab offsets off the set until it's empty or we find one at least as big as
        // our current offset.
        do {
            ret = ord_set_pop(curr_set, &next_item);
            TERR("comparing: %lu < %lu\n", next_item, curr_item);
        } while (ret != OSET_EMPTY && next_item < curr_item);

        if (ret == OSET_EMPTY) {
            // We emptied a regular set, which means we can have can no more
            // results for our intersection.
            break;
        }

        // The offset we found is bigger than the one we were searching for.
        // That means the old offset can't be in our intersection.
        if (next_item > curr_item) {
            curr_item = next_item;
            match_count = 0;
        }

        // To reach here: next_item == curr_item
        match_count++;
        // Advance to the next regular set, circularly.
        curr_set = curr_set->next;

    }

    // Close files and free memory for the ordered sets.
    if (inv_set) {
        ord_set_cleanup(inv_set);
    }
    struct ordered_set * start_set = reg_sets;
    struct ordered_set * to_free = reg_sets;
    struct ordered_set * next;
    do {
        next = to_free->next;
        ord_set_cleanup(to_free);
        to_free = next;
    } while (to_free != start_set);

    // Make sure all the output data gets written.
    if (ord_set_cleanup(out_set) != 0) return EIO;

    return 0;
}

int or_results(struct search_t * search,
               char ** and_res_paths, // An array of paths (that must be the same length as the
                                      // number of and operations in search).
               char * result_path) {

    struct ordered_set * out_set = calloc(sizeof(struct ordered_set), 1);
    int ret = ord_set_init(out_set, OSET_OFFSET, OSET_WRITE, result_path);
    if (ret != 0) {
        if (ret == EEXIST) {
            // File already exists, but that's not necessarily a bad thing.
            return 0;
        }
        CRIT("Unknown error opening output file: %s, error: %s", result_path, strerror(errno));
        return EINVAL;
    }

    // Create our skip list head and initialize it.
    struct os_skip_list skip_list;
    os_slist_init(&skip_list);

    size_t and_i = 0;
    struct and_descr * and_op = search->and_ops[and_i];
    while (and_op != NULL) {
        char * and_res_path = and_res_paths[and_i];
        struct ordered_set *oset = calloc(1, sizeof(struct ordered_set));
        ret = ord_set_init(oset, OSET_OFFSET, OSET_READ, and_res_path);
        if (ret != 0) {
            return ret;
        };

        os_slist_add(&skip_list, oset);
        and_i++;
        and_op = search->and_ops[and_i];
    }

    // Take the union of all the ordered sets.
    if (os_slist_union(&skip_list, out_set) != 0) return EIO;

    ret = ord_set_cleanup(out_set);
    return ret;
}

static inline int filter_flow(
        struct fcap_flow_key * flow,
        struct timeval32 * start,
        struct timeval32 * end,
        uint8_t proto) {

    // Discard the flow if its last packet was before our start time.
    if (start != NULL) {
        if (start->tv_sec > flow->last_ts.tv_sec ||
            (start->tv_sec == flow->last_ts.tv_sec &&
             start->tv_usec > flow->last_ts.tv_usec)) {
            return FLOW_DISCARD;
        }
    }

    // Discard the packet if its first packet is after our end time.
    if (end != NULL) {
        if (end->tv_sec < flow->last_ts.tv_sec ||
            (end->tv_sec == flow->last_ts.tv_sec &&
             end->tv_usec < flow->last_ts.tv_usec)) {
            return FLOW_DISCARD;
        }
    }

    if (proto != 0 && proto != flow->proto) return FLOW_DISCARD;

    return FLOW_OK;
}

int flow_fetch(struct search_t * search,
               char * or_result_path,
               char * flow_index,
               off_t * total_flows_size,
               char * flows_path) {

    int flow_fd = open(flow_index, O_RDONLY);
    if (flow_fd == -1) {
        ERR("Could not open flow index: %s", flow_index);
        return errno;
    }
    posix_fadvise(flow_fd, 0, 0, POSIX_FADV_RANDOM);

    struct ordered_set * offsets = calloc(1, sizeof(struct ordered_set));
    int ret = ord_set_init(offsets, OSET_OFFSET, OSET_READ, or_result_path);
    if (ret != 0) {
        ERR("Could not open or result path: %s, (%s)", or_result_path, strerror(ret));
        return ret;
    }

    struct ordered_set * output_flows = calloc(1, sizeof(struct ordered_set));
    ret = ord_set_init(output_flows, OSET_FLOW, OSET_WRITE, flows_path);
    if (ret == EEXIST) {
        // It's ok if the result file already exists. That means we don't have to do this step.
        ord_set_cleanup(offsets);
        ord_set_cleanup(output_flows);
        return 0;
    } else if (ret != 0) {
        ERR("Could not open flows result path: %s, (%s)", flows_path, strerror(ret));
        return ret;
    }

    struct fcap_flow_rec flow_rec;

    struct fcap_idx_header hdr;

    ret = safe_read(flow_fd, &hdr, sizeof(struct fcap_idx_header));
    if (ret != 0) {
        ERR("Failed reading flow index header. %s", strerror(ret));
        return ret;
    }

    // TODO: This should be fixed.
    size_t fcap_offset_size = sizeof(uint32_t); /*(hdr.offset64 == 1 ? sizeof(uint64_t) : sizeof
                (uint32_t));*/

    *total_flows_size = 0;

    uint64_t offset;
    // Keep track of the flow file position so we don't have to seek uselessly all the time.
    off_t flow_file_pos = 0;
    while (ord_set_pop(offsets, &offset) != OSET_EMPTY) {
        if (flow_file_pos != offset) {
            lseek(flow_fd, offset, SEEK_SET);
        }

        flow_rec.flow_offset = 0;
        ret = safe_read(flow_fd, &flow_rec.key, sizeof(struct fcap_flow_key));
        if (ret != 0) {
            ERR("Failed reading flow index key. %s", strerror(ret));
            return ret;
        }
        ret = safe_read(flow_fd, &flow_rec.flow_offset, fcap_offset_size);
        if (ret != 0) {
            ERR("Failed reading flow index offset. %s", strerror(ret));
            return ret;
        }
        union kt_ptrs tmp_ptr;
        tmp_ptr.flow = &flow_rec.key;

        if (filter_flow(&(flow_rec.key),
                        &search->start_ts,
                        &search->end_ts, search->proto) == FLOW_OK) {
            // Try to push this flow into our output file.
            ret = ord_set_push(output_flows, &flow_rec);
            if (ret != 0) {
                ERR("Failed pushing flow record to output file. %s", strerror(ret));
                return ret;
            }
            *total_flows_size += flow_rec.key.size;
        }
    }

    ord_set_cleanup(offsets);
    close(flow_fd);
    return ord_set_cleanup(output_flows);
}

// Parse the given timestamp string and return a new timeval32 structure containing it.
// The returned timeval32 will need to be freed.
// The format for the timestamp is expected to be: <epoch time seconds (UTC)>.<microseconds>
// Returns NULL on failure.
int parse_ts(struct timeval32 * ts, char * str) {
    char * time_sec = strtok(str, ".");
    char * time_usec = strtok(NULL, ".");

    uint64_t tmp = strtoul(time_sec, NULL, 10);
    // Make sure tmp is acceptable. If there was an error in a conversion, tmp
    // will be set to ULONG_MAX, which is also bigger than UINT32_MAX.
    if (tmp > UINT32_MAX) {
        ERR("Invalid timestamp: %s", str);
        return EINVAL;
    }
    ts->tv_sec = (uint32_t)tmp;

    tmp = strtoul(time_usec, NULL, 10);
    if (tmp > UINT32_MAX) {
        return EINVAL;
        ERR("Invalid timestamp: %s", time_usec);
    }
    ts->tv_usec = (uint32_t)tmp;

    return 0;
}

// Read the given file, storing 'len' bytes in buff
// Buff is assumed to be at least 'len' bytes in size.
// The file is assumed to have at least len bytes till it's end.
int safe_read(int fd, void * buff, size_t len) {
    ssize_t bytes_read;
    do {
        bytes_read = read(fd, buff, len);
    } while (bytes_read == 0 && errno == EINTR);
    if (bytes_read != len) {
        return EIO;
    }
    return 0;
}
