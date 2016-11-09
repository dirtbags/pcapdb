#include "pcapdb.h"

// This library has all the shared pcapdb initialization functions. These are exposed mainly so
// that test programs can take advantage of them.

// Initialize the sytem state object (and config structure) with the
// system defaults, as defined pcapdb.h
int system_state_init(struct system_state *);
// Initialize a capture state object, and set it's capture interface name to the given parameter.
struct capture_state * capture_state_init(char *, struct system_state *);
// Initialize a thread state object.
struct thread_state * thread_state_init(struct system_state *);
// Allocate as many buckets as possible from hugepages.
void allocate_hugepage_buckets(struct system_state *);
// Allocate buckets from system memory according to the parameters in
// the system_state->config structure. The allocated buckets are put on the ready queue.
int allocate_sysmem_buckets(struct system_state *);
// Make sure the given interface name is reasonable.
// Returns 0 when the name is ok.
int check_iface_name(char *);
// Setup the interface named in the capture_state according to the parameters in the system_state.
int prepare_interface(struct capture_state *);
// Close the interface in the given capture_state.
void close_interface(struct capture_state *);
// Set the tstamping method based on the supported types.
void set_tstamp_type(pcap_t *);
// Returns the number of CPU's on the system, not including Hyperthreading
int get_cpus();

