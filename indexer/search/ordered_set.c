#define _GNU_SOURCE

#include "../output.h"
#include "../keys.h"
#include "../network.h"
#include "ordered_set.h"
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#define TERR(...)
//DEBUG(__VA_ARGS__)

// Initialize the given ordered set object.
//   mode -> OSET_READ or OSET_WRITE
//   path -> Required when starting in READ mode.
//           Used as the path of the output file if started in WRITE mode.
//           If NULL is given, a temporary file is used in /tmp/
// Returns 0 on success.
// On failure an errno is returned, typically from an error opening the
// given filename.
int ord_set_init(
        struct ordered_set *oset,
        oset_type datatype,
        oset_mode mode,
        char *path) {
    oset->curr_item = oset->buffer_items = oset->extra_bytes = 0;
    oset->fno = -1;
    oset->skip_levels = 0;
    oset->path = path;
    oset->mode = mode;


    size_t page_size = (size_t)getpagesize();

    switch (datatype) {
        case OSET_OFFSET:
        case OSET_FLOW:
            oset->datatype = datatype;
            break;
        default:
            CRIT("Invalid ordered set datatype: %d", datatype);
            return EINVAL;
    }

    //DEBUG("Initializing oset %p. path: %s, mode: %d (%d == OSET_READ)",
    //     oset, path, mode, OSET_READ);

    oset->tmp_path = NULL;
    if (oset->mode == OSET_READ || oset->mode == OSET_WRITE) {
        if (oset->path == NULL) {
            CRIT("A path is required when in OSET_READ or OSET_WRITE mode.");
            return EINVAL;
        }
        oset->tmp_path = malloc(sizeof(char)*BASE_DIR_LEN*3);
        // Create the temporary output file path. If the name doesn't fit, fail.
        // When we write, we write to this file then move it to its final location.
        if (snprintf(oset->tmp_path, BASE_DIR_LEN*3, "%s.tmp", path) >= BASE_DIR_LEN*3) {
            CRIT("Tempfile path too long. (path, limit) %s, %d", path, BASE_DIR_LEN*3);
            return EINVAL;
        }

    }

    TERR("Trying to open %s in mode %d\n", path, mode);
    if (mode == OSET_READ) {
        // Open the input file.
        oset->fno = open(path, O_RDONLY);
        if (oset->fno == -1) {
            TERR("Could not open input file: %s, err: %s", path, strerror(errno));
            // The file doesn't exist or there was some other error; Check to see if the
            // corresponding tmp file exists. If it does, we just need to wait for the tmp to
            // finish being written.
            if (access(oset->tmp_path, F_OK) != 0) {
                // The tmpfile doesn't exist, so we don't expect the main file to soon.
                // The is not recoverable.
                CRIT("Input file %s does not exist, and isn't expected to soon.", path);
                return EINVAL;
            }

            // Now, setup inotify to wait for the write to finish and the tmp file to be moved to
            // its final location.
            int tmp_watcher = inotify_init();
            // Watch the tempfile for when it is moved. That will signal when we can read the
            // main file.
            inotify_add_watch(tmp_watcher, oset->tmp_path, IN_MOVE_SELF);

            // Before we start waiting, we need to check again for the file. It may have been
            // created while we set up inotify.
            if (access(path, F_OK) != 0) {
                struct pollfd fds = {.fd = tmp_watcher,
                                     .events = POLLIN,
                                     .revents = 0};
                const struct timespec timeout = {.tv_sec = 1,
                                                 .tv_nsec = 0};

                struct timespec last_stat_time = {0,0};
                while (1) {
                    // Use poll to tell us when an inotify event occured. We don't actually check
                    // to see if the event is the one we want, since we only allowed the kind we
                    // wanted anyway.
                    int poll_res = ppoll(&fds, 1, &timeout, NULL);

                    if (poll_res > 0) {
                        // An event happened.
                        break;
                    } else if (poll_res == 0) {
                        // We timed out.
                        struct stat tmp_stats;
                        if (stat(oset->tmp_path, &tmp_stats) == 0) {
                            if (last_stat_time.tv_sec != 0 &&
                                last_stat_time.tv_sec == tmp_stats.st_mtim.tv_sec &&
                                last_stat_time.tv_nsec == tmp_stats.st_mtim.tv_nsec) {
                                // The tmp_file hasn't been written to since we last timed out.
                                // We should abandon it.
                                CRIT("Input file not ready, and tmpfile (%s) isn't being written "
                                             "to.", oset->tmp_path);
                                return EINVAL;
                            }
                            // Copy the timestamp so we can check against it next time.
                            memcpy(&last_stat_time, &tmp_stats.st_mtim, sizeof(struct timespec));
                        } else {
                            // We couldn't stat the file? Probably doesn't exist anymore, which
                            // means it was probably moved.
                            break;
                        }
                    } else {
                        // An error has occurred with poll.
                        CRIT("Error polling while waiting for %s.", oset->tmp_path);
                        return EIO;
                    }
                }
            }

            close(tmp_watcher);

            // The input file should definitely exist at this point, try again to open it.
            oset->fno = open(path, O_RDONLY);
            if (oset->fno == -1) {
                CRIT("Could not open input set (%s) after waiting for write to finish.", path);
                return EINVAL;
            }
        }
        // Tell the system we'll be reading this file sequentially.
        posix_fadvise(oset->fno, 0, 0, POSIX_FADV_SEQUENTIAL);

        // When reading, set our initial buffer to the size of the file,
        // or to the MAX if the file is larger than our max.
        struct stat rfile_stat;
        int stat_ret = stat(path, &rfile_stat);
        if (stat_ret == -1) {
            CRIT("Could not stat oset input file: %s (%s)", path, strerror(errno));
            return EACCES;
        }
        oset->buffer_size = rfile_stat.st_size > OSET_MAX_PAGES * page_size ?
                            OSET_MAX_PAGES*page_size : page_size;
        if (rfile_stat.st_size % OSET_DSIZE(oset)) {
            WARN("Opening file as ordered set that has a size that isn't a multiple"
                 "of the set item size.");
        }

    } else if (mode == OSET_WRITE) {
        if (access(path, F_OK) == 0) {
            // The final output file already exists, so fail (with success).
            return EEXIST;
        }

        if (access(oset->tmp_path, F_OK) == 0) {
            // The tmp file already exists. If it's old, then just delete it.
            // We do this because sometimes things happen, and we need to more
            // gracefully recover on future attempts.
            struct stat tmp_path_stat;
            int stat_ret = stat(oset->tmp_path, &tmp_path_stat);
            time_t now = time(NULL);
            if (stat_ret != -1 &&
                (tmp_path_stat.st_mtim.tv_sec + OSET_TMP_STALE_TIMEOUT < now)) {
                // Try to unlink the file. It's ok if we fail.
                unlink(oset->tmp_path);
            }

        }

        // Try to open the specified output tmp file
        oset->fno = open(oset->tmp_path, O_RDWR | O_CREAT | O_EXCL,
                         S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (oset->fno == -1) {
            if (errno == EEXIST) {
                // The tmp_file already exists, which should mean that some other process is
                // currently writing what we would have anyway. Fail with success.
                return EEXIST;
            } else {
                CRIT("Could not open temp output file (%s): %s", oset->tmp_path, strerror(errno));
                return EIO;
            }
        }
        // Set our buffer size to the min, and we'll write reasonably sized chunks to disk
        // if we go over.
        oset->buffer_size = page_size;
    } else if (mode == OSET_TMP_WRITE) {
        if (path != NULL) {
            CRIT("The path argument must be null for temporary ordered sets.");
            return EINVAL;
        }
        // Set our buffer size to the min for now. We'll increase it as needed and buffer to a
        // file if it gets particularly large.
        oset->buffer_size = page_size;
    } else if (mode == OSET_TMP_READ) {
        CRIT("You cannot create an ordered set in TMP_READ mode. (There's nothing to read...)");
        return EINVAL;
    } else {
        CRIT("Invalid ordered set mode: %d", mode);
        return EINVAL;
    }

    oset->buffer = malloc(oset->buffer_size);

    return 0;
}

// Try to write the buffer to file.
int o_set_dump_buffer(struct ordered_set * oset) {
    // This is internal use only, and should only be used when in a write mode.

    if(oset->mode == OSET_TMP_WRITE && oset->fno == -1) {
        // Set up a tmp file to dump to, if we haven't already.
        char template[] = "/tmp/osettmp_XXXXXX";
        oset->fno = mkstemp(template);
        if (oset->fno == -1) {
            CRIT("Could not open temp buffer file. error: %s", strerror(errno));
            return EIO;
        }
    }

    TERR("Writing %lu items\n", oset->buffer_items);
    ssize_t wr_ret;
    if (oset->buffer_items > 0) {
        // Dump the buffer, but only if there's something in it.
        do {
            wr_ret = write(oset->fno, &oset->buffer[0], oset->buffer_items * OSET_DSIZE(oset));
        } while (wr_ret == -1 && errno == EINTR);
        if (wr_ret == -1) {
            CRIT("Could not write to temp buffer. error: %s", strerror(errno));
            return EIO;
        }
    }

    oset->buffer_items = 0;

    return 0;
}

// Fill the stack buffer while dealing with the possibility of partial reads.
int o_set_fill_buffer(struct ordered_set *oset) {
    if (oset->mode != OSET_READ && oset->mode != OSET_TMP_READ) {
        CRIT("Trying to fill ordered set buffer while in write mode.");
        return EINVAL;
    }

    if (oset->fno == -1) {
        CRIT("Trying to read from a non-open file.");
        return EINVAL;
    }

    size_t bytes_to_read = oset->buffer_size - oset->extra_bytes;

    // Copy the extra bytes from the end of the buffer to the beginning.
    if (oset->extra_bytes > 0) {
        memcpy(oset->buffer, &oset->buffer[oset->buffer_items*OSET_DSIZE(oset)],
               oset->extra_bytes);
    }

    ssize_t bytes_read;
    do {
        bytes_read = read(oset->fno, &(oset->buffer[oset->extra_bytes]), bytes_to_read);
    } while (bytes_read == -1 && errno == EINTR);
    if (bytes_read == -1) {
        CRIT("Error reading input file: %s, error: %s.", oset->path, errno);
        oset->buffer_items = oset->extra_bytes = 0;
        return -1;
    }

    // If we fail to read anything, say we're done even if there were extra bytes.
    if (bytes_read == 0) {
        if (oset->extra_bytes != 0)
            WARN("Extra bytes at the end of a search file: %s.", oset->path);
        oset->buffer_items = oset->extra_bytes = 0;
        oset->curr_item = 0;
        return 0;
    }

    oset->curr_item = 0;
    oset->buffer_items = (bytes_read + oset->extra_bytes) / OSET_DSIZE(oset);
    // Keep track of any extra bytes at the end of our read. It's unlikely, but it could happen.
    // We'll move those the the beginning of the buffer and account for them in our next read.
    oset->extra_bytes = (bytes_read + oset->extra_bytes) % OSET_DSIZE(oset);

    return 0;
}

// Add val to the ordered set. It is assumed that all values added are in order.
// If the memory buffer is full, then dump the buffer to a temp file.
int ord_set_push_(struct ordered_set *oset, union oset_types_u * val) {
    if (oset->mode != OSET_WRITE && oset->mode != OSET_TMP_WRITE) {
        CRIT("Ordered set is in the wrong mode for a push.");
        return EINVAL;
    }

    // If our buffer is full, either make it bigger or write it out.
    if (oset->buffer_items == OSET_BMAX(oset)) {
        if (oset->mode == OSET_TMP_WRITE &&
                 (oset->buffer_size < OSET_MAX_PAGES*getpagesize())) {
            // In tmp mode, double the size of our buffer if it's less than our page limit.
            // Doubling the number of pages should go pretty cleanly.
            oset->buffer = realloc(oset->buffer, oset->buffer_size*2);
            oset->buffer_size = oset->buffer_size*2;
        } else {
            // Try to write the buffer to file.
            // This will open a tmp file if needed in TMP_WRITE mode
            if (o_set_dump_buffer(oset) != 0) {
                return EIO;
            };
            oset->buffer_items = 0;
        }
    }

    union kt_ptrs ptrs;
    ptrs.flow = &(*val).flow.key;

    // Copy the value according to type.
    if (oset->datatype == OSET_OFFSET) {
        oset->offsets[oset->buffer_items] = (*val).offset;
    } else {
        oset->flows[oset->buffer_items] = (*val).flow;
    }
    TERR("Added %lx on %p, %lu total\n", (*val).offset, oset, oset->buffer_items);
    oset->buffer_items++;

    return 0;
}

// Dump our current buffer to file (if there is one), and then switch to read mode.
// This can only be if we're already in write mode.
// If there is no buffer file, then there's no need to dump to it.
// We also go ahead and read a buffer full (or as much as we can).
int ord_set_readmode(struct ordered_set *oset) {
    if (oset->mode != OSET_TMP_WRITE) {
        CRIT("You must have opened the file in TMP_WRITE mode to transition "
             "to TMP_READ mode.");
        return EINVAL;
    }

    oset->mode = OSET_READ;
    TERR("Setting readmode on %p\n", oset);

    // Check for a buffer file, and dump our buffer to it if we have one.
    // Then reset the file cursor to the start of the file.
    if (oset->fno != -1) {
        TERR("Dumping buffer on %p\n", oset);
        // Try to write the buffer to file.
        if (o_set_dump_buffer(oset) != 0) {
            return EIO;
        };

        // Try to refill the buffer from the start of the file.
        lseek(oset->fno, 0, SEEK_SET);
        if (o_set_fill_buffer(oset) != 0) return EIO;
    } else {
        oset->curr_item = 0;
    }

    return 0;
}

// Get the next item from the ordered set, and store it in dest.
// This will return either 0 or OSET_EMPTY if the set is empty or on read errors.
// The log will have relevant read error messages.
int ord_set_peek_(struct ordered_set *oset, union oset_types_u * dest) {
    if (oset->curr_item >= oset->buffer_items) {
        if (oset->fno == -1) {
            // There's no more data to get
            return OSET_EMPTY;
        }

        // Try to refill the buffer if it's empty.
        // There are a lot of potential errors here
        int ret = o_set_fill_buffer(oset);
        if (ret != 0) {
            // If we have a read failure, report it and try to move on.
            CRIT("Error reading ordered set in file (%s). %s", oset->path, strerror(ret));
            return OSET_EMPTY;
        }
        if (oset->buffer_items == 0) {
            // There was nothing to get.
            return OSET_EMPTY;
        }
    }

    if (oset->datatype == OSET_OFFSET) {
        dest->offset = oset->offsets[oset->curr_item];
    } else {
        dest->flow = oset->flows[oset->curr_item];
    }
    return 0;
}

// As per stack_peep, except remove the given item from the stack.
int ord_set_pop_(struct ordered_set *oset, union oset_types_u * dest) {
    int ret = ord_set_peek_(oset, dest);
    TERR("Popped item %lu of %lu, %lx\n", oset->curr_item, oset->buffer_items, dest->offset);
    oset->curr_item++;
    return ret;
}

// Seek to the nth record (counting from 0).
// This is only available in READ mode, and will completely reset the buffer.
int ord_set_seek(struct ordered_set *oset, size_t rec) {
    if (oset->mode != OSET_READ) {
        return EINVAL;
    }

    // Reset where we think we are in the buffer.
    oset->curr_item = oset->buffer_items = oset->extra_bytes = 0;

    size_t new_pos;
    if (oset->datatype == OSET_OFFSET) {
        new_pos = rec * sizeof(uint64_t);
    } else {
        new_pos = rec * sizeof(struct fcap_flow_rec);
    }
    lseek(oset->fno, new_pos, SEEK_SET);

    return 0;
}

// Clear the buffer, close the file, and free the oset object.
int ord_set_cleanup(struct ordered_set *oset) {
    //DEBUG("Cleaning up: %p. mode: %d, fno: %d, path: %s",
    //      oset, oset->mode, oset->fno, oset->path);

    // Only dump the buffer if we're in write mode
    int ret = 0;
    if (oset->mode == OSET_WRITE) {
        ret = o_set_dump_buffer(oset);
    }

    // Close the file if we ever opened one.
    if (oset->fno != -1) {
        close(oset->fno);
    }

    if (oset->mode == OSET_WRITE) {
        // Copy the temporary file for writing to the final location, so that
        // the file is atomically ready.
        rename(oset->tmp_path, oset->path);
    }

    free(oset->tmp_path);
    free(oset->buffer);
    free(oset);
    return ret;
}

// Initialize the skip list
void os_slist_init(struct os_skip_list * sl) {
    sl->max_level = 0;
    sl->items_inserted = 0;
    sl->size = 0;
    int i;
    for (i=0; i < MAX_SKIP_LIST_LEVELS; i++) {
        sl->skip[i] = NULL;
    }
}

// Add an entirely new oset to the skip list.
// This increments the total number of items in the list and the number
// of skip list levels.
// If you only need to momentarily remove and re-insert an item, use 'pop' and 'insert'.
void os_slist_add(struct os_skip_list * sl, struct ordered_set * oset) {
    sl->size++;

    // Recalculate the max skip level based on the new size.
    uint64_t items = sl->size;
    sl->max_level = 0;
    while (items > 0) {
        items = items >> 1;
        sl->max_level++;
    }

    os_slist_reinsert(sl, oset);
}

// Call when you know you've removed an item for good.
void os_slist_remove(struct os_skip_list * sl) {
    sl->size--;

    // Recalculate the max skip level based on the new size.
    uint64_t items = sl->size;
    sl->max_level = 0;
    while (items > 0) {
        items = items >> 1;
        sl->max_level++;
    }
}

// Pop the top item from the skip list. It's expected that the item will be
// re-added momentarily, so we don't recalculate max_level.
// If the skip list is empty, will return NULL;
struct ordered_set * os_slist_pop(struct os_skip_list * sl) {
    struct ordered_set * oset = sl->skip[0];

    TERR("Popped %p\n", oset);

    // Change any links in head that point to oset to point to oset's successor
    // at that level.
    if (oset != NULL) {
        uint32_t lvl;
        for (lvl = 0; lvl < sl->max_level && lvl < oset->skip_levels; lvl++) {
            if (sl->skip[lvl] != oset) {
                break;
            }
            sl->skip[lvl] = oset->skip[lvl];
        }
    }

    return oset;
}

// Reinsert oset into the skip list. It's assumed that it was only momentarily removed,
// so we don't try to adjust max_level.
void os_slist_reinsert(struct os_skip_list *sl, struct ordered_set *oset) {
    int32_t lvl = 0;

    // Calculate how many skip links this node should have. For normal skip lists,
    // this is done randomly. For ours, however, we do it based on a counter internal to the
    // skip list. A link at a given level will occur every 2^lvl nodes inserted.
    //
    // Skip lists normally have O(log_2 n) average case, and O(n) worst case. For some reason,
    // our usage results in actual results that better model the worst case. Our alternative way
    // in which we non-randomly calculate how many levels a node has causes insertion
    // to model O(log_2 n), however, so that's what we use.
    oset->skip_levels = 0;
    while (sl->items_inserted % (1 << lvl) == 0 && lvl < sl->max_level) {
        oset->skip[lvl] = NULL;
        lvl++;
    }
    oset->skip_levels = lvl;
    sl->items_inserted++;

    // Starting from the head, go as far at each level as we can until we find an item
    // more than what we're inserting (or NULL). Then go down a level and continue.
    lvl = sl->max_level - 1;
    struct ordered_set ** prior_skips = sl->skip;
    union oset_types_u val;
    int peek_ret = ord_set_peek_(oset, &val);

    if (peek_ret == OSET_EMPTY) {
        // The set is empty, we don't need to reinsert it.
        ord_set_cleanup(oset);
        os_slist_remove(sl);
        return;
    }

    // We update links as we descend, so when we've passed the bottom level we're entirely done.
    while (lvl >= 0) {
        struct ordered_set * next = prior_skips[lvl];
        union oset_types_u next_val;
        if (next != NULL && ord_set_peek_(next, &next_val) == OSET_EMPTY) {
            CRIT("Found an empty ordered set from a skip list.");
            return;
        };

        // If we're at the end of the links for a skip level, add
        // this item if it has that many levels, and go to the next level
        if (next == NULL) {
            TERR("Nowhere to go: %d, %d, %d\n", oset->skip_levels, sl->max_level, lvl);
            if (oset->skip_levels > lvl) {
                prior_skips[lvl] = oset;
                oset->skip[lvl] = NULL;
            }
            lvl--;
            continue;
        }

        // Compare this item to the next.
        int cmp_result;
        if (oset->datatype == OSET_OFFSET) {
            cmp_result = val.offset >= next_val.offset;
        } else {
            cmp_result = flow_key_cmp(&val.flow.key, &next_val.flow.key) >= 0;
        }

        if (cmp_result) {
            // Our item will definitely go past this node.
            prior_skips = next->skip;
        } else {
            // Our item is before this (next) node, go down a level and
            // insert this node at this skip level if it has links this high.
            if (oset->skip_levels > lvl) {
                struct ordered_set * tmp;
                tmp = prior_skips[lvl];
                prior_skips[lvl] = oset;
                oset->skip[lvl] = tmp;
            }
            lvl--;
        }
    }

    int j;
    for(j=0; j<sl->max_level; j++) {
        TERR("sl->skip[j]: %p\n", sl->skip[j]);
    }
}

// Output the union of our ordered sets.
// 1. Grab the next set from our sorted (by next item) list of sets
// 2. Pop the next item from that set.
// 3. If that item is different from the last item, output it.
// 4. Reinsert the set into our sorted set list, unless it was empty.
// 5. Go to step 1, until we're out of sets.
int os_slist_union(struct os_skip_list *sl, struct ordered_set *output_set) {
    struct ordered_set * next_oset = os_slist_pop(sl);
    union oset_types_u item;
    union oset_types_u last_item;

    if (next_oset == NULL) {
        // The skip list was empty.
        return 0;
    }
    int datatype = next_oset->datatype;

    // Go ahead and grab a new 'last' item.
    if (ord_set_pop_(next_oset, &last_item) == OSET_EMPTY) {
        CRIT("Ordered set pulled from skip list was empty. This should never happen.");
        return EFAULT;
    }


    os_slist_reinsert(sl, next_oset);
    next_oset = os_slist_pop(sl);

    while (next_oset != NULL ) {
        if (ord_set_pop_(next_oset, &item) == OSET_EMPTY) {
            CRIT("Ordered set pulled from skip list was empty. This should never happen.");
            return EFAULT;
        }
        os_slist_reinsert(sl, next_oset);

        if (datatype == OSET_OFFSET) {
            if (item.offset != last_item.offset) {
                // We've got a new offset value, so output this one.
                if(ord_set_push_(output_set, &last_item) != 0) return EIO;
                last_item.offset = item.offset;
            }
        } else {  // OSET_FLOW
            if (flow_key_cmp(&last_item.flow.key, &item.flow.key) != 0) {
                // We've got a new flow value, so output this one.
                if(ord_set_push_(output_set, &last_item) != 0) return EIO;
                last_item.flow = item.flow;
            } else {
                // When we union flows, we merge their data.
                flow_key_merge(&last_item.flow.key, &item.flow.key);
            }
        }

        next_oset = os_slist_pop(sl);
        TERR("union: next_oset: %p\n", next_oset);
    }

    // We're done, write out the last item.
    if (ord_set_push_(output_set, &last_item) != 0) return EIO;

    return 0;
}
