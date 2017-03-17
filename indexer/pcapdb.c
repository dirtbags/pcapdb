#define _GNU_SOURCE  // Enables several non-portable functions
#define _MULTI_THREADED
#include <pthread.h>

#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <sys/stat.h>
#include "pcapdb.h"
#include "pcapdb_init.h"
#include "bucketize.h"
#include "db.h"
#include "output.h"

// Very few of the functions in this file need to be exported.
void pcapdb_run(struct system_state *);
void pcapdb_shutdown(struct system_state *);
void write_status(struct system_state *);
int pcapdb_start_threads(struct system_state *, char[MAX_CAPTURE_THREADS][BASE_DIR_LEN], int);
int set_interrupt_affinity(char *, int);
void setup_signal_handling(struct system_state *);
void shutdown_thread_set(struct system_state *, struct queue *, struct thread_state **, int);
void signal_handler(int);

struct event * sys_shutdown;

#define TEST_Q "SELECT id, uuid FROM capture_node_api_disk WHERE mode='ACTIVE' ORDER BY usage "\
               "LIMIT 1"
const char * HELP = "Usage:\n"
        "   capture -h\n"
        "   capture -i <iface/file> [OPTIONS]\n\n"
        "Options:\n"
        "   -C <working_directory>\n"
        "       Set the working directory to this.\n"
        "   -d <db_connect_str>\n"
        "       Postgres database connection string of the form:\n"
        "         host=<hostname> user=<db_user> dbname=<db_name> password=<pw>\n"
        "       For production, the string 'dbname=indexer' is expected (and default).\n"
        "   -D\n"
        "       Don't try to use the database.\n"
        "       Capture files and indexes are written to '/tmp/pcapdb/<PID>_<TS>/' by default, "
        "       where PID is the output process PID and TS is first packet timestamp for bucket.\n"
        "       Use the -o option to change the destination directory.\n"
        "   -g <group>\n"
        "       Run capture under this group.\n"
        "   -h\n"
        "       Print this help and exit.\n"
        "   -i <interface/input_file>\n"
        "       This supports any interface name supported by the chosen capture library.\n"
        "       If -r is used, these are interpreted as filenames, otherwise interface names\n"
        "       are expected.\n"
         "      Multiple interfaces and files can be specified.\n"
        "   -l\n"
        "       Use libpcap to read from all interfaces.\n"
        "   -m <# of buckets>\n"
        "       Generate the given number of buckets in system memory without using hugetlbfs.\n"
        "       The default is to allocate buckets using hugetlbfs pages,\n"
        "       and to allocate as many as we can.\n"
        "   -n Use ntop's pfring library to read from all interfaces. -z is then same except in \n"
        "       zero-copy mode. Zero copy mode requires specific hardware and drivers, as well as\n"
        "       a zero-copy license for each interface.\n"
        "   -o <output_dir>\n"
        "       Base directory where to write output FCAP and indexes.\n"
        "       Defaults to '/tmp/pcapdb/' in -D (NO_DB) mode.\n"
        "       Defaults to '/var/capture/' (and an entirely different write scheme) otherwise\n"
        "   -p <pages>\n"
        "       Number of pages (2 MB each) per bucket. Default: 128\n"
        "   -r \n"
        "       Interperet the input names as files, and capture from those until the files are \n"
        "       empty. The program will exit once all input has been processed.\n"
        "   -s <# of chunks>\n"
        "       Number of 4MB chunks in an output FCAP file. Defaults to 1024 (4 GB).\n"
        "       Either the entire capture system must be configured from the start for a change \n"
        "       in this setting, or you must use the NO_DB mode (-D).\n"
        "   -u <user>\n"
        "       After doing those steps that require root access, switch to this username.\n"
        "   -V\n"
        "       Output log messages to stderr as well as syslog.\n"
        "   -z\n"
        "       Try to use the pfring zero-copy mode. You will need a zero-copy license\n"
        "       from ntop.org installed for each capture interface.\n\n"
        "   -Z\n"
        "       Daemonize this process.\n"
        "Captures and indexes packets from the given ports. Produces FCAP files (flow ordered \n"
        "PCAP) and a series of index files for each capture file.\n\n"
        "Errors and info are sent to LOG_LOCAL5 by default. Use -V to send to stderr as well.\n\n"
        "Ignores many signals; Can be killed gently with a SIGTERM, SIGINT, or SIGQUIT\n\n";

int main(int argc, char **argv) {
    int c;
    char arg_failure;
    char iface_names[MAX_CAPTURE_THREADS][BASE_DIR_LEN];
    int i, iface_name_count = 0;
    for (i = 0; i < MAX_CAPTURE_THREADS; i++) iface_names[i][0] = '\0';

    struct config *conf;
    // See cornet.h for state and config defaults.
    struct system_state state;
    system_state_init(&state);
    conf = &state.conf;

    int outfile_size_tmp;

    int syslog_options = LOG_NDELAY | LOG_PID;

    struct passwd * pw_data;
    struct group * gr_data;

    char tmp_buffer[BASE_DIR_LEN];
    getcwd(tmp_buffer, BASE_DIR_LEN);

    char * cwd_tmp = NULL;

    const char *OPTIONS = "C:d:Dg:hi:lm:no:p:rs:u:Vz";
    c = getopt(argc, argv, OPTIONS);
    arg_failure = 0;
    while (c != -1) {
        switch (c) {
            case 'C':
                if (chdir(optarg) != 0) {
                    CRIT("Could not set working directory: %s", strerror(errno));
                    return EFAULT;
                }
                cwd_tmp = get_current_dir_name();
                INFO("Working directory set to: %s", cwd_tmp);
                free(cwd_tmp);

                break;
            case 'd':
                strncpy(conf->db_connect_str, optarg, DB_CONNECT_LEN - 1);
                conf->db_connect_str[DB_CONNECT_LEN - 1] = '\0';
                conf->use_db = PCAPDB_USE_DB;
                break;
            case 'D':
                conf->use_db = PCAPDB_NO_USE_DB;
                strncpy(conf->outfile_dir, NO_DB_BASEPATH, BASE_DIR_LEN);
                conf->outfile_dir[BASE_DIR_LEN-1] = '\0';
                break;
            case 'g':
                gr_data = getgrnam(optarg);
                if (gr_data == NULL) {
                    CRIT("No such group: %s", optarg);
                    return EINVAL;
                }
                conf->capture_gid = gr_data->gr_gid;
                break;
            case 'h':
                fprintf(stderr, HELP);
                return 0;
            case 'i':
                if (iface_name_count < MAX_CAPTURE_THREADS) {
                    strncpy(iface_names[iface_name_count], optarg, BASE_DIR_LEN);
                    iface_names[iface_name_count][BASE_DIR_LEN-1] = '\0';
                    iface_name_count++;
                } else {
                    CRIT("No more than %d input interfaces or files are supported.",
                            MAX_CAPTURE_THREADS);
                    return EINVAL;
                }
                break;
            case 'l':
                conf->capture_mode = CAP_MODE_LIBPCAP;
                break;
            case 'm':
                conf->bucket_mem_type = MEM_MODE_SYS_MEM;
                conf->max_system_buckets = strtoul(optarg, NULL, 10);
                break;
            case 'n':
                conf->capture_mode = CAP_MODE_PFRING;
                break;
            case 'o':
                strncpy(conf->outfile_dir, optarg, BASE_DIR_LEN);
                conf->outfile_dir[BASE_DIR_LEN-1] = '\0';
                break;
            case 'p':
                conf->bucket_pages = strtoul(optarg, NULL, 10);
                if (conf->bucket_pages < 1) {
                    fprintf(stderr, "Invalid bucket pages value: %s\n", optarg);
                    arg_failure = 1;
                }
                break;
            case 'r':
                conf->capture_mode = CAP_MODE_FILE;
                break;
            case 's':
                outfile_size_tmp = atoi(optarg);
                // Totally arbitrary size limits.
                // Since this is for testing, not production though, that should be ok.
                if (outfile_size_tmp < 0 || outfile_size_tmp > OUTFILE_MAX) {
                    CRIT("Bad outfile size: %d. Range 0 < n < %d.", outfile_size_tmp, OUTFILE_MAX);
                    return EINVAL;
                }
                conf->outfile_size = DISK_BLOCK * 1024 * (unsigned int) outfile_size_tmp;
                break;
            case 'u':
                pw_data = getpwnam(optarg);
                if (pw_data == NULL) {
                    CRIT("No such user: %s", optarg);
                    return EINVAL;
                }
                conf->capture_uid = pw_data->pw_uid;
                break;
            case 'V':
                syslog_options = syslog_options | LOG_PERROR;
                break;
            case 'z':
                conf->capture_mode = CAP_MODE_PFRING_ZC;
                break;
            case 'Z':
                if (setsid() == -1) {
                    CRIT("Capture process could not be daemonized. (%s)", strerror(errno));
                    return EFAULT;
                }
                break;
            default:
                fprintf(stderr, "Bad argument: %c\n", c);
                arg_failure = 1;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    CRIT("Working directory: %s\n", tmp_buffer);
    if (conf->capture_mode != CAP_MODE_FILE) {
        for (i=0; i<iface_name_count; i++) {
            if (check_iface_name(iface_names[i]) != 0) {
                CRIT("Invalid interface name: %s", optarg);
                return EINVAL;
            }
        }
    }

    if (arg_failure || optind != argc || iface_name_count == 0) {
        printf("arg_failure: %d, optind: %d, argc: %d, iface_name_count: %d",
        arg_failure, optind, argc, iface_name_count);
        fprintf(stderr, HELP);
        return EINVAL;
    }

    // Setup syslog according to compile time parameters (and the verbosity argument).
    openlog(SYSLOG_IDENT, syslog_options, SYSLOG_FACILITY);

    // Get and check the number of CPU's.
    state.cpu_count = get_cpus();
    INFO("Using %lu MB capture files.", conf->outfile_size/(1024*1024));
    int cap_limit = CAPTURE_THREAD_LIMIT(state.cpu_count);

    // We can only handle so many capture threads given a system's CPU count.
    if (iface_name_count > cap_limit) {
        CRIT("# of ifaces exceeds capture thread limit. CPUs: %d, Ifaces: %d, Limit: %d",
              state.cpu_count, iface_name_count, cap_limit);
        return EINVAL;
    }

    // Give a warning if the uid or gid for the capture user is root
    if (conf->capture_uid == 0 || conf->capture_gid == 0) {
        WARN("Capturing as the root user or group is dangerous.");
    }

    // Go ahead and switch our gid to what we were given. As long as we're the root user our group
    // shouldn't matter.
    if (setgid(conf->capture_gid) != 0) {
        CRIT("Could not change group id to %d (%s).", conf->capture_gid, strerror(errno));
        return EFAULT;
    }

    // Setup signal handling so that most signals that can be handled either result
    // in a soft shutdown or are ignored.
    setup_signal_handling(&state);

    // Make sure this is the only capture process in full operation.
    // Another capture process can be finishing up; when shutting down we release
    // the lockfile as soon all the interfaces are shutdown and most memory is freed.
    state.lockfile = open(LOCK_FILE_PATH, O_CREAT | O_RDWR, S_IRWXU | S_IROTH);
    if (flock(state.lockfile, LOCK_EX | LOCK_NB) != 0) {
        CRIT("Another capture process is still in full operation.");
        return EFAULT;
    }
    INFO("Acquired capture lock.");

    if (state.conf.bucket_mem_type == MEM_MODE_HUGE_PAGES) {
        // Make sure each capture thread has enough buckets for three full output files.
        uint64_t min_start_buckets = conf->outfile_size/(HUGE_PAGE_SIZE*conf->bucket_pages) *
                                        iface_name_count * 3;
        time_t timeout_start = time(NULL);
        do {
            allocate_hugepage_buckets(&state);
            if ((time(NULL) - timeout_start) > MAX_STARTUP_WAIT) {
                ERR("Could not allocate enough buckets for operation within %d seconds.",
                    MAX_STARTUP_WAIT);
                return EFAULT;
            }
        } while (queue_count(&state.ready_bkts) < min_start_buckets);
    } else {
        // Allocate buckets from system memory, if we happen to be in that mode.
        if (allocate_sysmem_buckets(&state) != 0) {
            return EINVAL;
        }
    }
    INFO("Allocated buckets. Count: %lu, Size (2MB pages): %lu",
         queue_count(&state.ready_bkts), conf->bucket_pages);


    // Try to start our various threads. This also drops user privileges.
    int ret = pcapdb_start_threads(&state, iface_names, iface_name_count);
    if (ret != 0) return 0;


    // Test database connection
    // It's better to find out now that we can't connect than when we've already
    // captured data. This needs to happen after we switch to the capture user,
    // as root probably won't have a DB role.
    PGconn *conf_cnx;
    if (conf->use_db == PCAPDB_USE_DB) {
        conf_cnx = get_db_conn(conf);
        PGresult *test_result = paramExec(conf_cnx, TEST_Q, 0, NULL, TUPLES,
                                          "Could not complete connection test.");
        if (test_result != NULL) {
            PQclear(test_result);
        } else {
            CRIT("Could not create test database connection.");
            pcapdb_shutdown(&state);
            return EINVAL;
        }
    }

    // Run until something forces us to exit.
    pcapdb_run(&state);

    // Shutdown the various threads, gently.
    pcapdb_shutdown(&state);

    return 0;
}

int pcapdb_start_threads(
        struct system_state *state,
        char iface_names[MAX_CAPTURE_THREADS][BASE_DIR_LEN],
        int iface_name_count) {
    int res;
    int i;

    pthread_attr_t attr;

    // We're going to make our capture threads FIFO scheduled.
    // The priority shouldn't matter too much, since FIFO scheduled processes are higher priority
    // than any non-real-time processes (everything else). We thus just set the priority to
    // the middle of the range.
    struct sched_param priority;
    int priority_min = sched_get_priority_min(SCHED_FIFO);
    int priority_max = sched_get_priority_max(SCHED_FIFO);
    priority.sched_priority = (priority_max - priority_min) / 2 + priority_min;

    // We're also going to set the affinity of our capture threads, so we'll need a
    // CPU set.
    cpu_set_t cpu_set;

    // Try to initialize threading attributes.
    if (pthread_attr_init(&attr) != 0) {
        CRIT("Could not initialize thread attributes.");
        return EFAULT;
    }

    // Clear all set cpus.
    CPU_ZERO(&cpu_set);

    // Create our capture threads.
    for (i = 0; i < iface_name_count; i++) {
        struct capture_state *cap_state = capture_state_init(iface_names[i], state);
        INFO("Starting capture thread #%d/%d (%s)", i+1, iface_name_count, cap_state->interface);

        // Set up this interface to start capturing.
        res = prepare_interface(cap_state);
        if (res != 0) return res;

        // Create our capture thread.
        res = pthread_create(&cap_state->thread, &attr, capture, (void *) cap_state);
        if (res != 0) {
            CRIT("Could not create capture thread, errno %d.", res);
            return EFAULT;
        }

        INFO("Starting set sched param %u\n", time(NULL));
        // Set the capture thread to a very aggressive scheduling policy (see above).
        pthread_setschedparam(cap_state->thread, SCHED_FIFO, &priority);

        INFO("CPU_SET %u\n", time(NULL));
        // Skip CPU0, as it gets the majority of system interrupts.
        // Only the capture threads will be assigned a set CPU; the remaining threads
        // will have neutral affinity.
        int cpu = i + 1;
        CPU_SET(cpu, &cpu_set);

        // Set the thread affinity.

        INFO("affinity %u\n", time(NULL));
        if (pthread_setaffinity_np(cap_state->thread, sizeof(cpu_set_t), &cpu_set) != 0) {
            // This isn't fatal.
            WARN("Could not set thread affinity to cpu %d", cpu);
        }

        INFO("int affinity %u\n", time(NULL));
        // Set the interrupt affinity to match the thread affinity.
        if (set_interrupt_affinity(iface_names[i], cpu) != 0) {
            // This isn't fatal either, but could really break things if it was
            // set at some time in the past. If it was set at sometime in the past though,
            // then why would it fail now?
            WARN("Could not set interrupt affinity for iface %s, cpu %d", iface_names[i], cpu);
        }
        state->capture_threads[i] = cap_state;
    }
    state->capture_thread_count = iface_name_count;

    INFO("setuid %u\n", time(NULL));
    // Before we do anything else, we need to drop user privileges.
    if (setuid(state->conf.capture_uid) != 0) {
        CRIT("Could not drop user privileges to uid %d (%s).",
             state->conf.capture_uid, strerror(errno));
        return EFAULT;
    }

    INFO("mkdir %u\n", time(NULL));
    // TODO: This needs to be split into two functions, with the setuid and the no_db directory
    // creation sandwiched in between the calls.

    // Make the base output directory if it doesn't exist.
    // Only do this in no_use_db mode; there's no saving use if the directories don't
    // exist in use_db mode.
    if (state->conf.use_db == PCAPDB_NO_USE_DB) {
        if (mkdir(state->conf.outfile_dir, 0777) == -1 && errno != EEXIST) {
            CRIT("Base output directory does not exist and could not be created.");
            return EINVAL;
        }
    }

    // Start all the indexing threads.
    state->index_thread_count = INDEX_THREAD_LIMIT(state->cpu_count);
    for (i = 0; i < state->index_thread_count; i++) {
        INFO("Starting indexing thread #%d/%d", i+1, state->index_thread_count);
        struct thread_state *thr_state = thread_state_init(state);
        res = pthread_create(&thr_state->thread, &attr, indexer, (void *) thr_state);
        if (res != 0) {
            fprintf(stderr, "Could not create indexing thread %d, errno %d\n", i, res);
            return EFAULT;
        }
        state->index_threads[i] = thr_state;
    }

    // Start all the output threads.
    state->output_thread_count = OUTPUT_THREAD_LIMIT(state->cpu_count);
    for (i = 0; i < state->output_thread_count; i++) {
        INFO("Starting output thread #%d/%d", i+1, state->output_thread_count);
        struct thread_state *thr_state = thread_state_init(state);
        res = pthread_create(&thr_state->thread, &attr, output, (void *) thr_state);
        if (res != 0) {
            fprintf(stderr, "Could not create output thread %d, errno %d\n", i, res);
            return EFAULT;
        }
        state->output_threads[i] = thr_state;
    }

    return 0;
}

// This thread does nothing more than wait for signals and try to get more buckets.
void pcapdb_run(struct system_state * state) {

    while (event_check(&(state->shutdown)) == 0) {
        // We're really just waiting for a signal.
        sleep(1);

        write_status(state);

        if (state->conf.bucket_mem_type == MEM_MODE_HUGE_PAGES) {
            // Try to allocate more buckets.
            // The purpose of this is to allow us to start capture before a prior capture process
            // has completed running. See pcapdb_cleanup below.
            allocate_hugepage_buckets(state);
        }
    }
}

// Gently shut down the capture system.
void pcapdb_shutdown(struct system_state * state) {
    int i;

    INFO("Shutting down capture threads.");
    // Tell all the capture threads to shutdown.
    // They will stop capturing at the next opportunity, and ship their buckets off
    // for indexing.
    for (i=0; i < state->capture_thread_count; i++) {
        struct capture_state * cap_state = state->capture_threads[i];
        event_set(&cap_state->shutdown);

        // Tell the capture interface to interrupt any loop it's in, or even break a
        // pfring interface out of blocking and waiting for a packet.
        switch (state->conf.capture_mode) {
            case CAP_MODE_LIBPCAP:
            case CAP_MODE_FILE:
                pcap_breakloop(cap_state->libpcap_if);
                break;
            case CAP_MODE_PFRING:
            case CAP_MODE_PFRING_ZC:
                pfring_breakloop(cap_state->pfring_if);
                break;
            default:
                ERR("Invalid capture mode in pcapdb_shutdown: %d", state->conf.capture_mode);
        }
    }

    // Finish cleaning up the capture threads, and close their interfaces.
    for (i=0; i < state->capture_thread_count; i++) {
        struct capture_state * cap_state = state->capture_threads[i];
        pthread_join(cap_state->thread, NULL);
        close_interface(cap_state);
        free(cap_state);
    }

    // By unlocking this file, we're telling the overall system that it can go ahead and start a
    // new capture process.
    flock(state->lockfile, LOCK_UN);

    INFO("Shutting down indexing threads.");
    // Shutdown all the indexing threads, when they're ready for it.
    shutdown_thread_set(state, &state->filled_bkts,
            state->index_threads, state->index_thread_count);

    INFO("Shutting down output threads.");
    // Shutdown all the output threads, when they're ready for it.
    shutdown_thread_set(state, &state->indexed_bkts,
            state->output_threads, state->output_thread_count);

    // The ready bucket queue is the last that could have anything added to it.
    // There shouldn't be anything left to add buckets to it now, however.
    bucketq_free(&state->ready_bkts, state);

}

// Shutdown a set of threads, while freeing any buckets that appear in the ready queue
// in the process.
void shutdown_thread_set(
        // The main system state structure.
        struct system_state * state,
        // The input queue for this thread set (filled for index threads, indexed for output thr)
        struct queue * thr_queue,
        // The array of thread states (state->index_threads or state->output_threads)
        struct thread_state ** thr_states,
        // The number of threads of this type.
        int thr_count) {

    // We'll sleep for 50 microseconds at a time.
    struct timespec sleep_time = {.tv_sec=0, .tv_nsec=50000000};

    // Wait for the thread set's input queue to be empty.
    while (queue_count(thr_queue) != 0) {
        // Wait a bit
        nanosleep(&sleep_time, NULL);
        write_status(state);
    }

    // Close the queue, so we're not stuck waiting on it.
    queue_close(thr_queue);

    int i;
    // Tell the threads to shutdown. This won't happen immediately; each thread will finish
    // processing it's current data.
    for (i=0; i < thr_count; i++) {
        event_set(&thr_states[i]->shutdown);
    }
    for (i=0; i < thr_count; i++) {
        int ret;
        do {
            ret = pthread_timedjoin_np(thr_states[i]->thread, NULL, &sleep_time);
        // Either we timed out, joined the thread, or can't join it (all other error codes).
        } while (ret == ETIMEDOUT);
        // Free our thread state structure.
        free(thr_states[i]);
    }
};

time_t LAST_STATUS = 0;
void write_status(struct system_state * state) {

    const char * state_strs[3];
    state_strs[PCAPDB_THR_IDLE] = "'idle'";
    state_strs[PCAPDB_THR_WORKING] = "'working'";
    state_strs[PCAPDB_THR_SHUTDOWN] = "'shutdown'";

    time_t now = time(NULL);

    if (now - LAST_STATUS < STATUS_PERIOD) {
        return;
    }
    LAST_STATUS = now;

    // Write a file of JSON with our current status
    FILE *status_file = fopen(STATUS_TMP_PATH, "w");
    int i;
    if (status_file != 0) {
        fprintf(status_file, "{'capture_events': [");
        fprintf(status_file, "%d", event_check(&state->capture_threads[0]->shutdown));
        for (i = 1; i < state->capture_thread_count; i++) {
            fprintf(status_file, ",%d", event_check(&state->capture_threads[i]->shutdown));
        }
        fprintf(status_file, "],\n 'index_threads': [");
        fprintf(status_file, "%s", state_strs[state->index_threads[0]->status]);
        for (i=1; i < state->index_thread_count; i++) {
            fprintf(status_file, ",%s", state_strs[state->index_threads[i]->status]);
        }
        fprintf(status_file, "],\n 'output_threads': [");
        fprintf(status_file, "%s", state_strs[state->output_threads[0]->status]);
        for (i=1; i < state->output_thread_count; i++) {
            fprintf(status_file, ",%s", state_strs[state->output_threads[i]->status]);
        }
        fprintf(status_file, "],\n'queued_ready_bkts': %lu, \n",
                queue_count(&state->ready_bkts));
        fprintf(status_file, "'queued_filled_bkts': %lu, \n",
                queue_count(&state->filled_bkts));
        fprintf(status_file, "'queued_indexed_bkts': %lu \n",
                queue_count(&state->indexed_bkts));

        fprintf(status_file, "}");
        fclose(status_file);

        rename(STATUS_TMP_PATH, STATUS_PATH);
    }

}

// Calls our script for setting an interface's interrupt affinity
// This assumes we've checked the interface name to make sure it's safe.
int set_interrupt_affinity(char * iface_name, int cpu) {
    char path[2*BASE_DIR_LEN+1];
    path[BASE_DIR_LEN] = '\0';
    char cwd[BASE_DIR_LEN+1];
    if (getcwd(cwd, BASE_DIR_LEN) == NULL) {
        CRIT("Working directory name too long.");
        return EFAULT;
    }

    snprintf(path, BASE_DIR_LEN, "%s/bin/set_interrupt_affinity %s %d",
            cwd, iface_name, cpu);
    return system(path);
}

// Register our signal
void setup_signal_handling(struct system_state * state) {
    int sigs[] = HANDLED_SIGNALS;
    int sig_count = HANDLED_SIGNAL_COUNT;
    int i;

    // Put a pointer to our shutdown event in a global so our signal handler
    // can see it.
    sys_shutdown = &state->shutdown;

    struct sigaction action;
    action.sa_handler = signal_handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    // Register our signal handler for all the signals in sigs.
    for (i=0; i < sig_count; i++) {
        if (sigaction(sigs[i], &action, NULL) != 0) {
            WARN("Could not register a handler for signal %d.", sigs[i]);
        }
    }
}

void signal_handler(int signo) {
    switch (signo) {
        case SIGINT:
        case SIGTERM:
        case SIGQUIT:
            // Do a gentle shutdown.
            INFO("Received exit signal.");
            event_set(sys_shutdown);
            break;
        default:
            // Ignore any other signals;
            break;
    }
}
