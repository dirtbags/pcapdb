#include "pcapdb.h"

#include "keys.h"
#include "index.h"

#include <postgresql/libpq-fe.h>
#include <stdbool.h>
#include <stdint.h>


// The maximum length of a decimal representation of a 64 bit integer,
// plus one byte for an ending null.
#define UINT64_STR_LEN 21
#define UUID_STR_LEN 37

struct save_info {
    uint64_t slot_id;
    char slot_id_str[UINT64_STR_LEN];
    char disk_uuid[UUID_STR_LEN];
    char slot_path[BASE_DIR_LEN*2];
    char index_id[UINT64_STR_LEN];
    char index_path[BASE_DIR_LEN*2];
};

// fcap header information
#define HEADER_IDENT 0x58444946
struct fcap_idx_header {
    uint32_t ident;     // 4 bytes   "FIDX"
    uint8_t version:7;  // 7 bits  "1"
    uint8_t offset64:1; // 1 bit (denotes 64 bit offsets or not).
    uint8_t key_type;   // 1 byte tree_type
    uint16_t preview;   // 2 bytes, Number of items in the preview index. 0 implies no such index.
    struct timeval32 start_ts; // 16 bytes  Timestamp of the first packet.
    struct timeval32 end_ts; // 16 bytes    Timestamp of the last packet.
    uint64_t records; // 8 bytes     Total number of records
}; // Total, 0x20 bytes

typedef enum {
    OB_OK,
    OB_IO_ERR,
    OB_BUCKET_ERR,
    OB_DB_ERR,
    OB_TREE_ERR
} output_code_t;

struct idx_write_node_args {
    keytype_t treetype;         // Treetype we're dealing with.
    int idx_fno;                // Index file to write to.
    union kt_ptrs preview_list; // The preview list pointers.
    uint16_t pl_i;              // Where we are in the preview list.
    uint64_t node_num;
    uint64_t total_nodes;       // Number of nodes in the tree, total.
    size_t   key_size;          // The size of the tree's keys.
    uint8_t sub_offset64;           // Whether we have 64 bit offsets (values) or not
};

// Write the given node to disk, and record corresponding entries in
// the preview list according to the arguments provided.
output_code_t idx_write_node(
        struct index_node *,      // The node (and it's sub entries) to write.
        struct idx_write_node_args *); // Pointer to the static arguments structure.

// Output thread base function.
void * output(void * arg);

// Output the given bucket using disk information gathered from the
// database connected to with the PGconn.
output_code_t output_bucket(struct config *,
                            PGconn **,
                            struct bucket *);

// Write the flow at the given index node to disk and record it's flow record.
output_code_t write_flow(
        struct index_node *,   // The node itself.
        int offset64,          // Whether to use 64 bit offsets.
        int,                   // The FCAP file to write to.
        int);                  // The flow index to write to

// Open a new fcap file at the given path.
int fcap_open(struct config *, // System config
              char *,     // Path the the fcap file.
              uint64_t);  // Number of packets in the fcap.

// Write the index for treetype to disk.
output_code_t write_index(
        struct config *,        // System config
        struct save_info *,     // Save location info.
        struct index_set *,     // The index set data for this bucket.
        keytype_t,              // The index type of the tree
        struct timeval32 *,     // TS of first bucket packet.
        struct timeval32 *);    // TS of last bucket packet.

// Returns how deep into the tree our preview tree should go.
// May return 0, implying no preview tree.
uint64_t preview_depth(keytype_t, // The keytype of the preview index.
                       uint64_t); // The number of nodes in the tree total.
// Returns how deep this node is in the tree.
uint32_t node_depth(uint64_t,  // The index of this node.
                    uint64_t); // The total number of nodes in the tree

// Queries the database to figure out where to save the captured
// data and indexes, filling out the save_info object.
output_code_t set_save_info(
        struct config *,     // System config structure.
        PGconn *,            //
        struct timeval32 *,
        struct timeval32 *,
        struct save_info *);

// Make up save information when the db is disabled.
void set_save_info_nodb(
        struct timeval32 *,
        struct save_info *);

output_code_t set_index_ready(
        PGconn *,
        char * index_id
);

// Save the given stats in the given database, linked to the index_id
output_code_t save_stats(
        PGconn *,
        struct network_stats *,
        char *);                // index_id of the the associated index.