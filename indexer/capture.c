#include <getopt.h>
#include <pfring.h>
#include <signal.h>
#include "bucketize.h"

void * capture(void *);

#define PKT_READ_LIMIT 10000
void * capture(void * arg) {
    struct capture_state * cap_state = (struct capture_state *) arg;
    struct system_state * state = cap_state->sys_state;

    // We only really need to keep track of this for when we read from a libpcap file.
    // On the first iteration we need to trick the loop into thinking we had at least 1 packet read.
    int pkts_read = 1;

    INFO("Starting capture thread.");

    // Between the time when we start capturing on this interface and when
    pfring_stat pf_stats;
    struct pcap_stat lp_stats;
    switch (state->conf.capture_mode) {
        case CAP_MODE_PFRING:
        case CAP_MODE_PFRING_ZC:
            pfring_stats(cap_state->pfring_if, &pf_stats);
            cap_state->pfring_last_sys_dropped = pf_stats.drop;
            cap_state->pfring_last_if_seen = pf_stats.recv;
            break;
        case CAP_MODE_LIBPCAP:
            INFO("(CT) Interface at %p.", cap_state->libpcap_if);
            pcap_stats(cap_state->libpcap_if, &lp_stats);
            cap_state->libpcap_last_sys_dropped = lp_stats.ps_drop;
            cap_state->libpcap_last_if_seen = lp_stats.ps_recv;
            break;
        case CAP_MODE_FILE:
            // There are no interface stats in this mode.
            break;
        default:
            ERR("Invalid capture mode in capture: %d", state->conf.capture_mode);
            break;
    }

    INFO("cap(%lx) Starting capture on iface: %s", pthread_self(), cap_state->interface);

    // Continue until something tells us to stop.
    while (!event_check(&cap_state->shutdown)) {
        switch (state->conf.capture_mode) {
            case CAP_MODE_FILE:
                // If we failed to read anything, then we were at EOF.
                // Shutdown this capture thread.
                if (pkts_read == 0) {
                    event_set(&cap_state->shutdown);
                    // Tell the main process to quit. Our signal handler will
                    // make the shutdown a gentle one, so any data in the pipeline
                    // will finish being processed.
                    raise(SIGTERM);
                    break;
                }
            case CAP_MODE_LIBPCAP:
                // This is the same for both libpcap and file modes (which also uses libpcap).
                pkts_read = pcap_dispatch(cap_state->libpcap_if, PKT_READ_LIMIT,
                              libpcap_bucketize, (uint8_t *) cap_state);
                break;
            case CAP_MODE_PFRING:
            case CAP_MODE_PFRING_ZC:
                pkts_read = pfring_bucketize(cap_state, PKT_READ_LIMIT);
                break;
            default:
                break;
        }
        // Make sure we give the system some time to cope with our monopolization
        // of this processor.
        sched_yield();
    }

    // Ship off whatever is left in the current bucket.
    send_bucket(cap_state);
    INFO("cap(%lx) Capture thread exiting.", pthread_self());

    return 0;
}
