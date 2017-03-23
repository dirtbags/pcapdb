#ifndef __CORNET_CORNET_H__
#define __CORNET_CORNET_H__

// This header is meant to contain macros and types needed by most of
// the other components of this system. For simplicities sake, don't
// do anything in here that would be dependent upon other components, 
// as that would create circular dependencies.

#include <arpa/inet.h>
#include <limits.h>
#include <pcap.h>
#include <postgresql/libpq-fe.h>
#include <stdint.h>
#include <syslog.h>
#include <errno.h>
#include "event.h"
#include "queue.h"

// Meant to be set by the compiler
#ifdef DEBUG_ON
    #define TERR(...) fprintf(stderr, __VA_ARGS__);
#else
    // Don't even compile these if we aren't in debug mode.
    #define TERR(...)
#endif

#define SYSLOG_FACILITY LOG_LOCAL5
#define SYSLOG_IDENT "capture"
#ifdef PRINT_LOGS
    #define CRIT(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
    #define ERR(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
    #define WARN(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
    #define INFO(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
    #define DEBUG(...) fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
    #define PERR(...) ERR(__VA_ARGS__)
#else
    #define CRIT(...) syslog(LOG_CRIT, __VA_ARGS__)
    #define ERR(...) syslog(LOG_ERR, __VA_ARGS__)
    // A special error that should be both printed and logged.
    #define PERR(...) fprintf(stderr, __VA_ARGS__); syslog(LOG_ERR, __VA_ARGS__)
    #define WARN(...) syslog(LOG_WARNING, __VA_ARGS__)
    #define INFO(...) syslog(LOG_INFO, __VA_ARGS__)
    #define DEBUG(...) syslog(LOG_DEBUG, __VA_ARGS__)
#endif

// Where our capture lockfile will be located.
#define LOCK_FILE_PATH "/var/lock/capture"

// The signals for which we register a signal handler.
#define HANDLED_SIGNALS {SIGINT, SIGTERM, SIGQUIT, SIGHUP};
#define HANDLED_SIGNAL_COUNT 4

// How big of a buffer we allocate for file paths, by default.
// If an extra big name is expected, we'll allocate a multiple of this.
#define BASE_DIR_LEN 128

// Limit on the database connection string length.
#define DB_CONNECT_LEN 256

// How big we expect a filesystem block size to be. We don't actually care
// if it's actually this big; we just use this as a reference size.
#define DISK_BLOCK 4096ULL

#define INDEX_DIR_NAME "index"
#define CAPTURE_DIR_NAME "capture"

// The output format of fcap/pcap uses a 32 bit timestamp regardless of
// whether it's generated on a 64 or 32 bit system.
// Since libpcap doesn't provide this structure, we have to create our own.
// We'll use this everywhere instead of libpcap's version, and do the
// conversion when we initially copy the packet (in bucketize).
struct timeval32 {
    uint32_t tv_sec;
    uint32_t tv_usec;
};

struct pcap_pkthdr32 {
    struct timeval32 ts;
    uint32_t caplen;
    uint32_t len;
};

// The different capture modes this system can handle.
// CAP_MODE_LIBPCAP uses libpcap for capture. It is limited in the speeds at which it can
//                  capture without dropping packets.
// CAP_MODE_PFRING uses NTOP's pfring for capture.
// CAP_MODE_PFRING_ZC With pfring_zc drivers installed on supported hardware, this will use pfring
//                    zero copy. You will also need a pfring zerocopy license.
// CAP_MODE_FILE uses libpcap for reading the capture from a file. The capture threads will
//               automatically shutdown when their files are empty, and the entire
//               system will shutdown when all the capture threads have finished.
typedef enum {
    CAP_MODE_LIBPCAP,
    CAP_MODE_PFRING,
    CAP_MODE_PFRING_ZC,
    CAP_MODE_FILE
} cornet_capture_mode;

// Memory type to use for buckets.
// HUGE_PAGES are the expected type for production. They must be set aside at boot time;
//            see system documentation for more information.
// SYS_MEM just allocates the buckets from system memory. This works fine for light testing.
typedef enum {
    MEM_MODE_HUGE_PAGES,
    MEM_MODE_SYS_MEM
} cornet_memory_mode;

// Whether or not to use the database at all. Useful for testing.
typedef enum {
    PCAPDB_NO_USE_DB,
    PCAPDB_USE_DB
} cornet_db_use;

// The name of the status file. This will be written to the working directory.
#define STATUS_PATH "status"
#define STATUS_TMP_PATH ".status"
// How often to update the status file, in seconds
#define STATUS_PERIOD 5


// Struct for storing the value of various system options.
// These should be pretty mo
// #defines for the default values are included after each option.
struct config {
    // The base directory where we're storing our results.
    char base_data_path[BASE_DIR_LEN];
    // Whether or not to use the database at all.
    cornet_db_use use_db;
    #define CFG_USE_DB_DF PCAPDB_USE_DB
    #define NO_DB_BASEPATH "/tmp/pcapdb"
    // Connection string for the system database.
    char db_connect_str[DB_CONNECT_LEN];
    #define CFG_DB_CONNECT_STR_DF "dbname=capture_node"
    // Maximum expected packet size.
    uint32_t mtu;
    // When running in pfring capture mode, packets larger than this could potentially
    // cause buffer overflows. You can see relatively huge packets, even on IPv4 networks.
    // Set our MTU to a reasonably large value.
    #define CFG_MTU_DF 65536
    // The uid and gid to switch to after configuring interfaces.
    uid_t capture_uid;
    #define CFG_CAPTURE_UID_DF 0
    gid_t capture_gid;
    #define CFG_CAPTURE_GID_DF 0
    // Underlying capture library and mode to use.
    cornet_capture_mode capture_mode;
    #define CFG_CAPTURE_MODE_DF CAP_MODE_LIBPCAP
    // Whether or not to use huge pages for buckets.
    cornet_memory_mode bucket_mem_type;
    #define CFG_BUCKET_MEM_TYPE_DF MEM_MODE_HUGE_PAGES
    // How much memory to reserve for each capture interface.
    int32_t pcap_buffer_mem;
    #define CFG_PCAP_BUFFER_MEM_DF INT32_MAX
    // Number of buckets to create from system memory, if using system memory buckets.
    uint64_t max_system_buckets;
    #define CFG_MAX_SYSTEM_BUCKETS_DF 0
    // Number of (huge) pages per bucket.
    uint64_t bucket_pages;
    #define CFG_BUCKET_PAGES_DF 128
    // Where to store the output files when the db is disabled.
    char outfile_dir[BASE_DIR_LEN];
    #define CFG_OUTFILE_DIR_DF "/var/capture"
    // The size of our capture files.
    uint64_t outfile_size;
    #define CFG_OUTFILE_SIZE_DF DISK_BLOCK*1024*1024
};

// These are the absolute maximum thread counts. Nothing will explode
// if they get set higher.
// The real number is derived from the number of processors on the system and
// the number of requested capture interfaces.
#define MAX_CAPTURE_THREADS 10
// More capture threads will greatly increase memory usage (assuming you actually
// have more capture interfaces). You should have at least twice the outfile_size
// as bucket memory per capture thread.
#define MAX_INDEXING_THREADS 10
// More Indexing threads just means each capture batch will be indexed faster.
// The time it takes to index can be significantly more than the time it takes
#define MAX_OUTPUT_THREADS 10

// The maximum amount of time we'll wait on something when starting up, in seconds
#define MAX_STARTUP_WAIT 10

// These macros define how to calculate how many of each type of thread to
// create, based on the number of CPUs on a system.
// We pretty much consume all of the available CPUs.
#define CAPTURE_THREAD_LIMIT(cpus) cpus/4+1;
// We want more index and output threads than input threads.
// Maybe some day this will be dynamic based on the number of items
// in the bucket queues.
#define INDEX_THREAD_LIMIT(cpus) 1 + cpus*3/8;
#define OUTPUT_THREAD_LIMIT(cpus) 1 + cpus*3/8;

// Allowed characters in interface names.
// Interface names are passed to shell calls as root, so
// this is to make sure they're safe.
#define IFACE_ALLOWED_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@:_-"

// The maximum outfile size in MB
#define OUTFILE_MAX 50*1024

// Expected size of a huge page (2 MiB). If huge pages aren't this big, we send a warning.
#define HUGE_PAGE_SIZE 2097152ULL

// This is fully defined in capture.h
struct capture_state;

enum thread_status {
    PCAPDB_THR_IDLE,
    PCAPDB_THR_WORKING,
    PCAPDB_THR_SHUTDOWN,
};
struct thread_state {
    // The system state
    struct system_state * sys_state;
    // The shutdown event for this thread.
    struct event shutdown;
    // The thread's handle
    pthread_t thread;
    // The thread's state. This is used to track whether the threads are currently working,
    // idle, or shutdown.
    enum thread_status status;
};


// Keeps track of the state of the current capture.
struct system_state {
    struct config conf;
    // Tells threads when to stop.
    struct event shutdown;
    // The state structures for the capture threads, and how many their are.
    // These also hold the threading object itself.
    struct capture_state * capture_threads[MAX_CAPTURE_THREADS];
    int capture_thread_count;
    // State structures for indexing and output threads.
    struct thread_state * index_threads[MAX_INDEXING_THREADS];
    int index_thread_count;
    struct thread_state * output_threads[MAX_OUTPUT_THREADS];
    int output_thread_count;
    // Empty buckets ready to be filled with freshly captured packets.
    struct queue ready_bkts;
    // Filled buckets ready to be indexed.
    struct queue filled_bkts;
    // Indexed buckets ready to be written to disk.
    struct queue indexed_bkts;
    // The number of CPUs on this system.
    int cpu_count;
    // The lockfile we use to prevent other instances of this
    // system from starting simultaniously.
    int lockfile;
};

#endif
