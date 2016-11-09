#ifndef __CORNET_SEARCH__
#define __CORNET_SEARCH__

#include <stdlib.h>
#include "../keys.h"
#include "../queue.h"
#include "../event.h"

// Read the specified number of bites from the given file descriptor,
// handling EINTR errors. Returns 0 on success, errno on failure.
// It is up to the caller to ensure the buffer is big enough for the
// number of bytes specified.
int safe_read(
        int,        // The file descriptor to read from
        void *,     // The buffer to read into.
        size_t);    // How much to read.

#endif

#define SEARCH_TOKEN_DELIM " "
#define NOT_PREFIX '!'

struct subindex_search_descr {
    keytype_t type;
    union kt_ptrs start_key;
    union kt_ptrs end_key;
    uint8_t start_buffer[sizeof(struct in6_addr)];
    uint8_t end_buffer[sizeof(struct in6_addr)];
    char * result_name;
};

struct and_item_list {
    uint64_t subindex_search_id;
    char * result_name;
    int inverted;
    struct and_item_list * next;
};

struct and_descr {
    struct and_item_list * sub_searches;
    char * result_name;
};

struct search_t {
    char * capture_path;
    struct queue * partial_index_queue;
    struct queue * index_queue;
    struct event * abort;
    struct subindex_search_descr ** subindex_ops;
    uint64_t subindex_search_count;
    struct and_descr ** and_ops;
    uint64_t and_op_count;
    struct timeval32 start_ts;
    struct timeval32 end_ts;
    char * or_result_name;
    char * partial_result_name;
    char * full_result_name;
    uint8_t proto;
    uint8_t fetch_pcap;
};

int parse_ts(struct timeval32 *, // Where to put the parsed timestamp
             char * str); // The timestamp string to parse.

char * make_path(char *, // Base path
                 char *, // File name
                 char *); // File extension (or NULL)

char * make_index_path(char *,    // Path to pcapdb
                       uint64_t); // Index id

int search_subindex(struct subindex_search_descr *,
                    char *, // The path to the index file
                    char *); // The output file path for the search.
int and_results(struct and_descr *,
                char **, // An array of paths (same # as subindex searches) to the output
                         // files for each subidx search for a given index directory.
                char *);
int or_results(struct search_t *,
               char **, // An array of paths (same # as and ops) to the output files for each and
                        // operation.
               char *); // The path to the output file.

// Returns the total size of the flows that would be read from the fcap for this search.
// -1 is an error
int flow_fetch(struct search_t *,
               char *, // Path to the or operation results
               char *, // Path to the flow index.
               off_t *, // A place to store the total size of all the flows matched
               char *); // The path to the results for this fetch.

int pcap_fetch(char *,  // Path to the flow result file
               char *,  // Path to the fcap file
               off_t,   // Total size of data to be pulled.
               char *); // Path to the output pcap file.

// When the total packets to pull is less than this limit, we prefetch them all before
// trying to sort them by time. Otherwise, we pull them as needed. When we prefetch, all the
// reads of the file are sequential, but without prefetch we may have to skip around a bit.
#define PACKET_PREFETCH_LIMIT 100*1024*1024

void cleanup_search(struct search_t *);

