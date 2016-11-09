#include "../index.h"
#include "../pcapdb.h"

struct fcap_flow_rec {
    struct fcap_flow_key key;
    uint64_t flow_offset;
}; // 80 + 8 bytes

union oset_types_u {
    struct fcap_flow_rec flow;
    uint64_t offset;
};

// Buffer at most 16 memory pages of set data.
// Default is to allocate an initial buffer of one page.
#define OSET_MAX_PAGES 16
#define MAX_SKIP_LIST_LEVELS 16
typedef enum {
    OSET_WRITE,
    OSET_READ,
    OSET_TMP_WRITE,
    OSET_TMP_READ
} oset_mode;

typedef enum {
    OSET_OFFSET,
    OSET_FLOW,
} oset_type;

// When trying to write an ordered set, ignore old, stale tempfiles
// last modified more than this many seconds ago
#define OSET_TMP_STALE_TIMEOUT 10

struct ordered_set {
    // The next set in the skip list
    struct ordered_set * next;
    // The skip list skip addresses.
    struct ordered_set * skip[MAX_SKIP_LIST_LEVELS];
    // How many skip levels this set has.
    int32_t skip_levels;
    // Path to the associated file.
    char * path;
    // Path to the temp version of this file. Used to ensure we don't
    char * tmp_path;
    // Buffer of ordered items.
    // We support two main types, offsets (OSET_OFFSET) and fcap_flow_rec (OSET_FLOW).
    // They share the same buffer space.
    // The 'buffer' value is used when generically reading or writing to file.
    // This buffer is dynamically allocated
    union {
        uint8_t * buffer;
        uint64_t * offsets;
        struct fcap_flow_rec * flows;
    };
    // The current size of the buffer.
    size_t buffer_size;
    // Number of items currently in the buffer.
    size_t buffer_items;
    // Number of odd bytes from incomplete records when reading data from file.
    size_t extra_bytes;
    // The cursor to the next item to be read from the buffer
    // in read mode.
    size_t curr_item;
    // The associated file, if any.
    int fno;
    // Whether this ordered set is in read or write mode.
    oset_mode mode;
    // What kind of data this stores.
    oset_type datatype;
};

// The size of the oset datatype.
#define OSET_DSIZE(oset) (oset->datatype == OSET_FLOW ? \
                            sizeof(struct fcap_flow_rec) : sizeof(uint64_t))
// The max number of items that can fit in the buffer, by data type.
#define OSET_BMAX(oset) oset->buffer_size/OSET_DSIZE(oset)

// Initialize the ordered set.
int ord_set_init(
        struct ordered_set *,   // The set to initialize.
        oset_type,              // The type of data in the set (OSET_OFFSET or OSET_FLOW)
        oset_mode,              // The read/write mode (OSET_WRITE or OSET_READ)
        char *);                // The (optional in write mode) input output file.
                                // If NULL, uses a tempfile for writing.

#define OSET_EMPTY -1
// Get the next item in the ordered set, but without removing it.
// A return of zero denotes success, -1 means the set is empty.
int ord_set_peek_(struct ordered_set *, union oset_types_u *); // Where to store the result.
// Get the next item as per peek, but also remove it from the set.
int ord_set_pop_(struct ordered_set *, union oset_types_u *);
// Set the mode of the set to read.
int ord_set_readmode(struct ordered_set *);
// Add an item to the ordered set. It's assumed that these will be added
// in least to greatest naturally.
int ord_set_push_(struct ordered_set *, union oset_types_u *);
// Flush the buffer to file (if any, and in write mode), close the file,
// and free the ordered set memory.
int ord_set_cleanup(struct ordered_set *);
// Seek the the nth record in the ordered set. Only valid in read mode.
int ord_set_seek(struct ordered_set *, size_t);

// Shortcuts to automatically cast the dest argument pointer to the union type
#define ord_set_peek(oset, dest) ord_set_peek_(oset, (union oset_types_u *)dest)
#define ord_set_pop(oset, dest) ord_set_pop_(oset, (union oset_types_u *)dest)
#define ord_set_push(oset, val) ord_set_push_(oset, (union oset_types_u *)val)

    struct os_skip_list {
    struct ordered_set * skip[MAX_SKIP_LIST_LEVELS];
    // A counter of how many items we've inserted, total over time.
    // Used to calculate how many skip levels to give a new item.
    uint64_t items_inserted;
    // The current size of the skip list.
    uint64_t size;
    // The maximum number of levels to use.
    uint8_t max_level;
};

// Initialize the skip list
void os_slist_init(struct os_skip_list *);
// Add a new item to the skip list.
void os_slist_add(struct os_skip_list *, struct ordered_set *);
// Note that an item as been permanently removed.
void os_slist_remove(struct os_skip_list *);
// Temporarily pop an item from the skip list. (Use remove to permanently remove).
struct ordered_set * os_slist_pop(struct os_skip_list *);
// Re-insert an item into the skip list. (Use add to add a new item).
void os_slist_reinsert(struct os_skip_list *, struct ordered_set *);
// Merge all the ordered sets in the skip list, using the given ordered
// set as the output destination.
// Note: This attempts to free the merged sets, so they should all be dynamically
// allocated.
int os_slist_union(struct os_skip_list *, struct ordered_set *);
//

#define INDEX_OF_PERMS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP