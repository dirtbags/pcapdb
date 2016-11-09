#define _MULTI_THREADED
#include <pthread.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../keys.h"
#include "../output.h"
#include "search.h"

struct search_t * parse_search(char *, char *);
struct subindex_search_descr * parse_subindex_search(char **, keytype_t);
struct and_descr * parse_and_op(char **);
int parse_timeval(char *, struct timeval32 *);
int parse_indexes(char **, char **, struct queue *);

void usage() {
    printf("Usage: search <search_desc> [OPTIONS]\n\n"
           "  OPTIONS\n\n"
           "    -p <pcapdb_path> Path to the pcapdb directory (default /var/pcapdb)\n"
           "    -P            Also fetch matching packets from the FCAP file.\n"
           "    -t <threads>  Number of threads to use when resolving searches. Default 4.\n"
           "    -h            Print this help and exit.\n"
           "    -F            Print a description of the search description format and exit\n"
           "    -s            By default progress is printed regularly as the number of indexes\n"
           "                  remaining to be processed. This flag stops that.\n"
           "                  processed. Don't do that."
           "    <search_desc> Path to the file that describes this search. See below\n"
           "                  for the file format.\n");
}

#define FORMAT \
"Search Description File Format\n\
------------------------------\n\n\
The search description contains lines describing components of a search \n\
tree. There are three types of components of this tree, described here and in\n\
other PCAPdb documentation. They look like this:\n\n\
           Packets Pull\n\
                |\n\
             Flows Pull\n\
                |\n\
               OR\n\
            /         \\\n\
       AND              AND\n\
   /        \\        /       \\\n\
subidx1   subidx2  subidx1   subidx3\n\n\
As above, all searches have an OR at the top, a second layer of AND's, and\n\
a final layer of sub index searcnes\n\n\
Sub Index Searches\n\
------------------\n\n\
Sub indexes are a set of key value pairs where the key is some flow five-tuple\n\
attribute (src ip, dst port, etc.) and the value is the the offset of the flow\n\
in the flow index. In database terms, this is an index into the flow table.\n\n\
A sub index search returns a sorted (by offset) mathmatical set of the \n\
matching flow entries.\n\n\
FORMAT\n\
<key_type> <result_name> <start_key> <end_key>\n\
  key_type - The name of the key type. Run search with the -K option for the \n\
             list of types\n\
  result_name - The name of the result file (written in each index directory)\n\
                for this operation.\n\
  start_key - The smallest key value to match.\n\
  end_key - The largest key value to match. Must be <= start_key.\n\n\
These descriptions are implicitely numbered (starting at 0) in the \n\
order they appear in the file.\n\n\
AND Operation\n\
-------------\n\n\
All sub-index searches in the tree must reside under at least one AND \n\
operation. This takes the set intersection of all the sub-index result \n\
sets. The result sets may also be inverted, such that only results not\n\
in that particular set are included.\n\n\
FORMAT\n\
AND <result_name> [!]<subidx_search_id>...\n\
  Each AND line must start with the 'AND' keyword.\n\
  result_name - The name of the result file. (as per subidx searches)\n\
  subidx_search_id - The id (from the implicit numbering mentioned\n\
      above) of a sub index search. If preceded by an exclamation\n\
      point, the results are inverted in the intersection operation.\n\
      There must be at least one non-inverted sub index search per AND op\n\n\
      Each AND op may include an unlimited number of subidx searches.\n\n\
OR Operation\n\
------------\n\n\
The OR operation takes the set union of all the AND operations. \n\n\
FORMAT\n\
OR <result_name>\n\
  There can only be one OR line, and it must start with 'OR'.\n\
  result_name - The name of the result file. \n\n\
FLOW Pull Operation\n\
-------------------\n\n\
The OR results are used to pull flow records from the flow index.\n\
These results will have a different filename depending on whether\n\
they cover the whole time range, or just a subsection of it. As such,\n\
there are two directives at this point.\n\
FORMAT\n\
PARTIAL <result_name> <index_id>...\n\
  This line may only appear once, and includes all those indexes to be \n\
  searched that may have some flows filtered out by time.\n\
  result_name - These result files will have '.flows' append to them.\n\
FULL <result_name> <index_id>...\n\
  Just like partial, except these results won't be filtered by time.\n\n\
Packet Pull Operation\n\
---------------------\n\n\
This uses the flow record results to pull packets from the FCAP file.\n\
The same result names as the flow pull step are used, except\n\
with a '.pcap' extension. This step is optional, and depends on command\n\
line arguments.\n"

struct search_thread_args {
    struct search_t * search;
    bool print_status;
};

int reconcile_subsearches(struct search_t *);
void * run_search(struct search_thread_args *);

// Totally made up number.
#define MAX_SEARCH_THREADS 10

int main(int argc, char ** argv) {

    // Both log to syslog and print our errors to stderr.
    openlog("search", LOG_NDELAY | LOG_PERROR, SYSLOG_FACILITY);

    uint64_t thread_count = 4;
    bool print_status = true;
    char * pcapdb_path = "/var/pcapdb";

    uint8_t fetch_pcap = 0;

    const char OPTIONS[] = "p:t:hFPs";

    int c = getopt(argc, argv, OPTIONS);
    while (c != -1) {
        switch (c) {
            case 'p':
                pcapdb_path = optarg;
                break;
            case 't':
                thread_count = strtoul(optarg, NULL, 10);
                if (thread_count > MAX_SEARCH_THREADS || thread_count == 0) {
                    ERR("Invalid thread count: %s", optarg);
                    return EINVAL;
                }
                break;
            case 'h':
                usage();
                return 0;
            case 'F':
                printf(FORMAT);
                return 0;
            case 'P':
                fetch_pcap = 1;
                break;
            case 's':
                print_status = false;
                break;
            default:
                ERR("Invalid argument: %c", c);
                return EINVAL;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    if (argc - optind != 1) {
        ERR("Missing search description file.");
        usage();
        return EINVAL;
    }

    // Parse the search description file
    struct search_t *search = parse_search(pcapdb_path, argv[optind++]);
    if (search == NULL) {
        ERR("Invalid search description data.");
        return EINVAL;
    }

    if (reconcile_subsearches(search) != 0) {
        return EINVAL;
    }

    search->fetch_pcap = fetch_pcap;

    // Parse all the index id's into integers, and store them in a thread safe queue.
    optind += 4;
    int ret;
    uint64_t * index_id;
    while (optind < argc) {
        // These are freed when pulled from the queue
        index_id = malloc(sizeof(uint64_t));

        *index_id = strtoul(argv[optind], NULL, 10);
        if (*index_id == ULONG_MAX && (errno == EINVAL || errno == ERANGE)) {
            ERR("Invalid index id: %s\n", argv[optind]);
            return EINVAL;
        }

        queue_push(search->index_queue, index_id);
        optind++;
    }


    search->abort = malloc(sizeof(struct event));
    event_init(search->abort);

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        CRIT("Could not initialize thread attributes for search.");
        return EFAULT;
    }

    int i;
    pthread_t threads[MAX_SEARCH_THREADS];
    struct search_thread_args thread_args[MAX_SEARCH_THREADS];
    for (i=0; i<thread_count-1; i++) {
        thread_args[i].search = search;
        thread_args[i].print_status = false;

        ret = pthread_create(&threads[i],
                             &attr,
                             (void *(*)(void*))run_search,
                             (void *)&thread_args[i]);
        if (ret != 0) {
            CRIT("Could not create thread for search, err %s\n", strerror(errno));
            return EFAULT;
        }
    }

    // This is a thread that will be searching too.
    thread_args[i].search = search;
    thread_args[i].print_status = print_status;
    run_search(&thread_args[i]);

    // When this thread is done searching, wait for the others to finish.
    for (i=0; i<thread_count-1; i++) {
        // Now just wait for the threads to finish.
        pthread_join(threads[i], NULL);
    }

    cleanup_search(search);

    return 0;
}

// Cleanup all the allocated memory and associated objects for a search.
void cleanup_search(struct search_t *search) {
    free(search->capture_path);
    queue_close(search->index_queue);
    free(search->index_queue);
    free(search->partial_index_queue);
    free(search->abort);
    size_t i;
    for (i=0; i<search->subindex_search_count; i++) {
        struct subindex_search_descr * subidx_srch = search->subindex_ops[i];
        free(subidx_srch->result_name);
        free(subidx_srch);
    }
    for (i=0; i<search->and_op_count; i++) {
        struct and_descr * and_op = search->and_ops[i];
        free(and_op->result_name);
        struct and_item_list * this = and_op->sub_searches;
        struct and_item_list * next;
        while (this != NULL) {
            next = this->next;
            free(this);
            this = next;
        }
        free(and_op);
    }

    free(search->subindex_ops);
    free(search->and_ops);
    free(search->or_result_name);
    free(search->full_result_name);
    free(search->partial_result_name);
    free(search);
}

#define INITIAL_SLOTS 32
#define MAX_SEARCH_DESCR_SIZE 1024*1024

struct search_t * parse_search(char * capture_path,
                               char * search_descr_path) {
    struct search_t * search = calloc(1, sizeof(struct search_t));
    int ret;

    // Copy the capture path out of argv
    search->capture_path = calloc(1, strlen(capture_path) + 1);
    strcpy(search->capture_path, capture_path);

    search->index_queue = malloc(sizeof(struct queue));
    queue_init(search->index_queue);
    search->partial_index_queue = malloc(sizeof(struct queue));
    queue_init(search->partial_index_queue);

    int search_descr_fd = open(search_descr_path, O_RDONLY);
    if (search_descr_fd == -1) {
        ERR("Could not open search description file: %s (%s)",
            search_descr_path, strerror(errno));
        return NULL;
    }

    struct stat descr_stat;
    ret = fstat(search_descr_fd, &descr_stat);
    if (ret != 0) {
        ERR("Could not stat search description file: %s (%s)",
            search_descr_path, strerror(errno));
        return NULL;
    }

    if (descr_stat.st_size > MAX_SEARCH_DESCR_SIZE) {
        ERR("Excessively large search description: %s, (%lu)",
            search_descr_path, descr_stat.st_size);
        return NULL;
    }

    // I would love to do this with mmap, but strtok would be modifying the file...
    size_t buffer_size = sizeof(char)*(descr_stat.st_size+1);
    char * buffer = malloc(buffer_size);
    ssize_t bytes;
    do {
        bytes = read(search_descr_fd, buffer, (size_t)descr_stat.st_size);
    } while (bytes == -1 && errno == EINTR);
    if (bytes == -1) {
        ERR("Could not read search description. %s (%s)", search_descr_path, strerror(errno));
        return NULL;
    }
    // Make sure the buffer ends with an NULL bytes.
    buffer[buffer_size-1] = '\0';

    close(search_descr_fd);

    // Our current positions in the buffer.
    char * line_save_ptr;
    char * tok_save_ptr;

    // Get our first line
    char * line = strtok_r(buffer, "\n", &line_save_ptr);

    // We store our searches in an array in the order they're listed in the file.
    // We'll realloc this as needed.
    size_t subindex_max = INITIAL_SLOTS;
    size_t and_op_max = INITIAL_SLOTS;
    search->subindex_ops = calloc(sizeof(struct subindex_search_descr *),
                                       subindex_max);
    search->and_ops = calloc(sizeof(struct and_descr *), and_op_max);
    search->subindex_search_count = 0;
    search->and_op_count = 0;
    int line_count = 0;

    while (line != NULL) {

        char * line_type = strtok_r(line, " ", &tok_save_ptr);
        keytype_t keytype = kt_strtokeytype(line_type);

        if (keytype != kt_BADKEY) {
            // This is a subindex search.
            struct subindex_search_descr * op = parse_subindex_search(&tok_save_ptr, keytype);
            if (op == NULL) {
                ERR("Could not parse subindex operation at %d.", line_count);
                return NULL;
            }
            search->subindex_ops[search->subindex_search_count] = op;
            search->subindex_search_count++;
            if (search->subindex_search_count >= subindex_max) {
                // Realloc (and NULL) more subindex search slots if necessary.
                subindex_max *= 2;
                search->subindex_ops = realloc(search->subindex_ops,
                                      sizeof(struct subindex_search_descr *)*subindex_max);
                size_t i;
                for (i=search->subindex_search_count; i<subindex_max; i++) {
                    search->subindex_ops[i] = NULL;
                }
            }
        } else if (strncmp("AND", line_type, 4) == 0) {
            // Parse and handle an AND operation.
            struct and_descr *op = parse_and_op(&tok_save_ptr);
            if (op == NULL) {
                ERR("Could not parse AND operation at %d", line_count);
                return NULL;
            }
            search->and_ops[search->and_op_count] = op;
            search->and_op_count++;
            if (search->and_op_count >= and_op_max) {
                // Realloc (and NULL) more intersection search slots if necessary.
                and_op_max *= 2;
                search->and_ops = realloc(search->and_ops, sizeof(struct and_descr) * and_op_max);
                size_t i;
                for (i = search->and_op_count; i < and_op_max; i++) {
                    search->and_ops[i] = NULL;
                }
            }
        } else if (strncmp("OR", line_type, 3) == 0) {
            if (search->or_result_name != NULL) {
                ERR("One, and only one, OR section must be included.");
                return NULL;
            }

            char *result_name_tok = strtok_r(NULL, SEARCH_TOKEN_DELIM, &tok_save_ptr);
            if (result_name_tok == NULL) {
                ERR("Missing result name in OR operation description.");
                return NULL;
            }
            size_t result_name_len = strlen(result_name_tok);
            search->or_result_name = calloc(1, result_name_len + 1);
            strncpy(search->or_result_name, result_name_tok, result_name_len);
            search->or_result_name[result_name_len] = '\0';

        } else if (strncmp("START", line_type, 6) == 0) {
            char * ts_tok = strtok_r(NULL, " ", &tok_save_ptr);
            if (ts_tok == NULL) {
                ERR("Bad START line, timestamp missing.");
                return NULL;
            }

            if (parse_timeval(ts_tok, &search->start_ts) != 0) {
                return NULL;
            }
        } else if (strncmp("END", line_type, 4) == 0) {
            char *ts_tok = strtok_r(NULL, " ", &tok_save_ptr);
            if (ts_tok == NULL) {
                ERR("Bad END line, timestamp missing.");
                return NULL;
            }

            if (parse_timeval(ts_tok, &search->end_ts) != 0) {
                return NULL;
            }
        } else if (strncmp("PROTO", line_type, 6) == 0) {
            char *proto_tok = strtok_r(NULL, " ", &tok_save_ptr);
            if (proto_tok == NULL) {
                ERR("Bad PROTO line, proto missing.");
                return NULL;
            }
            uint64_t proto64 = strtoul(proto_tok, NULL, 10);
            if (proto64 > 255) {
                ERR("Bad protocol: %s", proto_tok);
                return NULL;
            }
            search->proto = (uint8_t) proto64;
        } else if (strncmp("PARTIAL", line_type, 8) == 0) {
            if (parse_indexes(&tok_save_ptr, &search->partial_result_name,
                              search->partial_index_queue) != 0) {
                ERR("Bad PARTIAL line.");
                return NULL;
            }
        } else if (strncmp("FULL", line_type, 5) == 0) {
            if (parse_indexes(&tok_save_ptr, &search->full_result_name,
                              search->index_queue) != 0) {
                ERR("Bad FULL line.");
                return NULL;
            }
        } else {
            ERR("Invalid search type: %s at %d", line_type, line_count);
            return NULL;
        }

        line = strtok_r(NULL, "\n", &line_save_ptr);
        line_count++;
    }

    free(buffer);
    return search;
}

// Go through the 'and' operations and find the corresponding subsearches by reference number
int reconcile_subsearches(struct search_t * search) {
    size_t and_id = 0;
    struct and_descr * and_op = search->and_ops[and_id];

    while (and_op != NULL) {
        struct and_item_list * and_item = and_op->sub_searches;
        while (and_item != NULL) {
            size_t subsearch_id = and_item->subindex_search_id;
            if (subsearch_id < search->subindex_search_count) {
                struct subindex_search_descr * subidx_search = search->subindex_ops[subsearch_id];
                and_item->result_name = subidx_search->result_name;
            } else {
                ERR("AND op references subindex search (%d) that doesn't exist.", (int)subsearch_id);
                return EINVAL;
            }
            and_item = and_item->next;
        }

        and_id++;
        and_op = search->and_ops[and_id];
    }

    return 0;
}

struct subindex_search_descr * parse_subindex_search(char ** save_ptr, keytype_t keytype) {
    // The format for these is simply:
    // <keytype> <result_name> <start_key> <end_key>
    // The keytype was already parsed out, and is given.

    struct subindex_search_descr * subidx = malloc(sizeof(struct subindex_search_descr));

    subidx->type = keytype;

    char * result_name_tok = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);
    if (result_name_tok == NULL) {
        ERR("Missing result name in subindex search description.");
        return NULL;
    }
    size_t result_name_len = strlen(result_name_tok);
    subidx->result_name = malloc(result_name_len+1);
    subidx->result_name[result_name_len] = '\0';
    strncpy(subidx->result_name, result_name_tok, result_name_len);

    char * start = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);
    char * end = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);

    subidx->start_key.generic = subidx->start_buffer;
    subidx->end_key.generic = subidx->end_buffer;

    if (kt_key_parse(subidx->start_key, keytype, start) != 0) {
        return NULL;
    }
    if (kt_key_parse(subidx->end_key, keytype, end) != 0) {
        return NULL;
    }

    if (strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr) != NULL) {
        ERR("Trailing data in search description.");
        return NULL;
    }

    return subidx;
}

struct and_descr * parse_and_op(char ** save_ptr) {
    struct and_descr * and_op = calloc(sizeof(struct and_descr), 1);

    char * result_name_tok = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);
    if (result_name_tok == NULL) {
        ERR("Missing result name in AND operation description.");
        return NULL;
    }
    size_t result_name_len = strlen(result_name_tok);
    and_op->result_name = malloc(result_name_len+1);
    strncpy(and_op->result_name, result_name_tok, result_name_len);
    and_op->result_name[result_name_len] = '\0';

    char * tok = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);
    if (tok == NULL) {
        ERR("Empty AND operation.");
        return NULL;
    }

    struct and_item_list * last = NULL;
    while (tok != NULL) {
        struct and_item_list * item = malloc(sizeof(struct and_item_list));
        if (and_op->sub_searches == NULL) and_op->sub_searches = item;

        if (last != NULL) last->next = item;

        item->next = NULL;

        if (tok[0] == NOT_PREFIX) {
            item->inverted = 1;
            tok = &tok[1];
        } else {
            item->inverted = 0;
        }
        item->subindex_search_id = strtoul(tok, NULL, 10);

        if (item->subindex_search_id == ULONG_MAX && (errno == EINVAL || errno == ERANGE)) {
            ERR("Invalid subindex search id: %s", tok);
            return NULL;
        }

        tok = strtok_r(NULL, SEARCH_TOKEN_DELIM, save_ptr);
        last = item;
    }

    return and_op;
}

int parse_timeval(char * ts, struct timeval32 * tv) {
    // Parse a epoch time string into a timeval32 object.
    char *usec_str = NULL;
    uint64_t sec64 = strtoul(ts, &usec_str, 10);
    if (sec64 > UINT32_MAX) {
        ERR("Invalid timestamp seconds: %s", ts);
        return EINVAL;
    }
    tv->tv_sec = (uint32_t) sec64;

    if (usec_str[0] != '.') {
        ERR("Invalid timestamp seconds separator: %s", ts);
        return EINVAL;
    }

    usec_str += sizeof(char);
    uint64_t usec64 = strtoul(usec_str, NULL, 10);
    if (usec64 > UINT32_MAX) {
        ERR("Invalid timestamp micro-seconds: %s", ts);
        return EINVAL;
    }
    tv->tv_usec = (uint32_t) usec64;

    return 0;
}

int parse_indexes(char **save_ptr, char ** result_name, struct queue * idx_queue) {
    // Parse a PARTIAL or FULL line listing the corresponding result name and
    // indexes.

    char * result_tok = strtok_r(NULL, " ", save_ptr);
    if (result_tok == NULL) {
        ERR("Error parsing FULL or PARTIAL line.");
        return EINVAL;
    }
    size_t result_name_len = strlen(result_tok);
    *result_name = calloc(1, result_name_len + 1);
    strncpy(*result_name, result_tok, result_name_len);
    (*result_name)[result_name_len] = '\0';

    char * idx_str = strtok_r(NULL, " ", save_ptr);
    while (idx_str != NULL) {
        uint64_t * idx = calloc(1, sizeof(uint64_t));
        *idx = strtoul(idx_str, NULL, 10);
        queue_push(idx_queue, idx);

        idx_str = strtok_r(NULL, " ", save_ptr);
    }
    return 0;
}


#define TASK_STATUS_PERIOD 1
// Print the status once per second. This only has a resolution of seconds.
void * run_search(struct search_thread_args * args) {

    struct search_t * search = args->search;
    bool print_progress = args->print_status;

    struct timespec last_status;
    if (print_progress) {
        if (clock_gettime(CLOCK_REALTIME_COARSE, &last_status) == EINVAL) {
            WARN("Can't print progress, system does not support this type of clock.");
            print_progress = false;
        }
    }

    // We need to create and initialize these variables to NULL at the start, since we'll
    // be freeing them immediately (from the prior loop).
    char * index_path = NULL;
    char * packets_path = NULL;
    char * flows_path = NULL;
    char * or_results_path = NULL;
    char * and_res_paths[search->and_op_count];
    char * subidx_res_paths[search->subindex_search_count];

    int i;
    for (i=0; i<search->and_op_count; i++) and_res_paths[i] = NULL;
    for (i=0; i<search->subindex_search_count; i++) subidx_res_paths[i] = NULL;

    bool is_partial = true;
    struct queue * curr_queue = search->partial_index_queue;
    char * result_name = search->partial_result_name;

    // Walk the entire search tree (and post tree resolution operations), and do those for which
    // we don't already have the result file.
    // The process looks like:
    //      Process to be done              Result File name
    //      -----------------------------------------------------------
    //      Pull packets from FCAP      ->  <index>/<result_name>.pcap
    //          |
    //      Pull flows from FLOW idx    ->  <index>/<result_name>.flows
    //          |
    //      Resolve OR operation        ->  <index>/<result_name>
    //          |
    //      Resolve AND operations      ->  <index>/<and_op_result_name>
    //          |
    //      Resolve Sub-idx searches    ->  <index>/<type_startkey_endkey>
    while (event_check(search->abort) != 1) {

        uint64_t * index_id_ptr = (uint64_t *)queue_pop(curr_queue, Q_NOWAIT);
        if (index_id_ptr == NULL) {
            if (is_partial) {
                // We're out of things to pull from the partial queue. Switch to the
                // full search queue.
                is_partial = false;
                curr_queue = search->index_queue;
                result_name = search->full_result_name;
                // Try again with the other queue
                continue;
            } else {
                // When the regular index queue is empty, exit.
                break;
            }
        }

        uint64_t index_id = *index_id_ptr;
        free(index_id_ptr);

        index_path = make_index_path(search->capture_path, index_id);

        packets_path = make_path(index_path, result_name, ".pcap");
        if (access(packets_path, F_OK) != 0) {
            // We have to fetch the packets.
            off_t total_flows_size = 0;

            flows_path = make_path(index_path, result_name, ".flows");
            if (access(flows_path, F_OK) != 0) {
                // We'll have to fetch flows.

                or_results_path = make_path(index_path, search->or_result_name, NULL);
                if (access(or_results_path, F_OK) != 0) {
                    // We'll have to resolve the OR operation.

                    int and_id;
                    bool all_ands_ok = true;
                    for (and_id = 0; and_id < search->and_op_count; and_id++){
                        and_res_paths[and_id] = make_path(index_path,
                                                          search->and_ops[and_id]->result_name,
                                                          NULL);

                        if (access(and_res_paths[and_id], F_OK) != 0) {
                            all_ands_ok = false;
                        }
                    }

                    if (all_ands_ok == false) {
                        // We have to resolve some AND operations.

                        int s_id;
                        for (s_id=0; s_id < search->subindex_search_count; s_id++) {
                            struct subindex_search_descr * subidx_op = search->subindex_ops[s_id];
                            char * subidx_result = make_path(index_path,
                                                             subidx_op->result_name,
                                                             NULL);
                            subidx_res_paths[s_id] = subidx_result;

                            if (access(subidx_result, F_OK) != 0) {
                                // We have to do this sub_idx operation.
                                char * subidx_fn = make_path(index_path,
                                                             (char *)kt_name(subidx_op->type),
                                                             NULL);

                                if (search_subindex(subidx_op, subidx_fn, subidx_result) != 0) {
                                    // The search failed.
                                    event_set(search->abort);
                                    ERR("Search of subindex %s in index %s for keys %s-%s failed",
                                        kt_name(subidx_op->type), index_path,
                                        kt_key_str(subidx_op->start_key, subidx_op->type),
                                        kt_key_str(subidx_op->end_key, subidx_op->type));
                                    return NULL;
                                }
                                free(subidx_fn);
                            }
                        } // Sub_idx searches done.

                        for (and_id = 0; and_id < search->and_op_count; and_id++) {
                            // We have to check for file existence yet again for each and.
                            if (access(and_res_paths[and_id], F_OK) != 0) {
                                // We have to do this particular and op.
                                // Any missing subidx_search results should have been created above.
                                if (and_results(search->and_ops[and_id],
                                                subidx_res_paths,
                                                and_res_paths[and_id]) != 0) {
                                    // Operation failed.
                                    event_set(search->abort);
                                    ERR("AND operation failed for %s", and_res_paths[and_id]);
                                    return NULL;
                                }
                            }
                        } // AND ops done.
                    } // all ANDs should be ok

                    if (or_results(search, and_res_paths, or_results_path) != 0) {
                        // OR operation failed.
                        event_set(search->abort);
                        ERR("OR operation failed for %s.", or_results_path);
                        return NULL;
                    }
                } // OR op is done.

                char * flow_index = make_path(index_path, (char *)kt_name(kt_FLOW), NULL);
                int ret = flow_fetch(search, or_results_path, flow_index,
                                     &total_flows_size, flows_path);
                if (ret != 0) {
                    event_set(search->abort);
                    ERR("Flow fetch operation failed for %s (err %s).", flows_path, strerror(ret));
                    return NULL;
                }
                free(flow_index);
                free(or_results_path);
            } // Flow fetch op is done.

            if (search->fetch_pcap) {
                char * fcap_path = make_path(index_path, "FCAP", NULL);

                // We actually want the pcap.
                if (pcap_fetch(flows_path, fcap_path, total_flows_size, packets_path) != 0) {
                    event_set(search->abort);
                    ERR("Pcap fetch operation failed for %s.", packets_path);
                    return NULL;
                }
                free(fcap_path);
            }
        } // Pcap fetch op is done.

        // Now free all those paths we may or may not have created.
        // Any that are guaranteed to be reinitialized don't have to be set to NULL.
        free(index_path);
        free(packets_path);
        free(flows_path); flows_path = NULL;
        for (i=0; i < search->and_op_count; i++) {
            free(and_res_paths[i]); and_res_paths[i] = NULL;
        }
        for (i=0; i < search->subindex_search_count; i++) {
            free(subidx_res_paths[i]); subidx_res_paths[i] = NULL;
        }

        // Print the progress periodically, if that's enabled.
        if (print_progress) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME_COARSE, &now);
            if ((now.tv_sec - last_status.tv_sec) > TASK_STATUS_PERIOD) {
                printf("%lu.\n", queue_count(search->index_queue));
                fflush(stdout);
                last_status = now;
            }
        }

    }
    return NULL;
}
