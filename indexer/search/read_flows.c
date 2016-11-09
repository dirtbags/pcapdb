#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include "ordered_set.h"
#include <sys/stat.h>

int sort_dir;

// This is used with the qsort function
// Compares two src_ip addresses of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
// An ipv4 address is always less than an ipv6 address
int src_ip_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    ip_addr_t * ip1 = (ip_addr_t *)(&a->key.src);
    ip_addr_t * ip2 = (ip_addr_t *)(&b->key.src);

    int ret = ip_cmp(ip1, ip2);

    return sort_dir * ret;
}

// This is used with the qsort function
// Compares two dst_ip addresses of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
// An ipv4 address is always less than an ipv6 address
int dst_ip_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    ip_addr_t * ip1 = (ip_addr_t *)(&a->key.dst);
    ip_addr_t * ip2 = (ip_addr_t *)(&b->key.dst);

    int ret = ip_cmp(ip1, ip2);

    return sort_dir * ret;
}

// This is used with the qsort function
// Compares the src_port of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
int src_port_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    if (a->key.srcport == b->key.srcport) return 0;
    return sort_dir * (a->key.srcport < b->key.srcport ? -1 : 1);
}

// This is used with the qsort function
// Compares the dst_port of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
int dst_port_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    if (a->key.dstport == b->key.dstport) return 0;
    return sort_dir * (a->key.dstport < b->key.dstport ? -1 : 1);
}

// This is used with the qsort function
// Compares the size of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
int size_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    if (a->key.size == b->key.size) return 0;
    return sort_dir * (a->key.size < b->key.size ? -1 : 1);
}

// This is used with the qsort function
// Compares the packets of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 ( c > d)
int packets_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    if (a->key.packets == b->key.packets) return 0;
    return sort_dir * (a->key.packets < b->key.packets ? -1 : 1);
}

// This is used with the qsort function
// Compares the start_ts of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
int start_ts_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;
    long long t1 = a->key.first_ts.tv_sec;
    t1 <<= 32;
    t1 += a->key.first_ts.tv_usec;
    long long t2 = b->key.first_ts.tv_sec;
    t2 <<= 32;
    t2 += b->key.first_ts.tv_usec;

    if (t1 == t2) return 0;
    return sort_dir * (t1 < t2 ? -1: 1);
}

// This is used with the qsort function
// Compares the end_ts of flow records c and d
// Returns -1 (c < d), 0 (c == d), 1 (c > d)
int end_ts_cmp(const void *c, const void *d) {
    const struct fcap_flow_rec *a = c;
    const struct fcap_flow_rec *b = d;

    long long t1 = a->key.last_ts.tv_sec;
    t1 <<= 32;
    t1 += a->key.last_ts.tv_usec;
    long long t2 = b->key.last_ts.tv_sec;
    t2 <<= 32;
    t2 += b->key.last_ts.tv_usec;

    if (t1 == t2) return 0;
    return sort_dir * (t1 < t2 ? -1: 1);
}

#define TS_BUF_LEN 5 +   /* The year and a hyphen */\
                   5*3 + /* The month, day, hour, minute, second and a trailing char each */\
                   7 + 1 /* The microseconds, Z for UTC, plus a trailing NULL */
#define ISO8601_FMT "%Y-%m-%d %H:%M:%S"
#define ISO8601_us_FMT "%s.%06luZ"
char * fmt_timeval32_iso8601us(struct timeval32 tv, char * buf) {
    // Format a timeval32 as an UTC ISO8601 timestamp with microseconds.
    // The given buffer must be at least TS_BUF_LEN in size
    char tmp_ts_str[TS_BUF_LEN];
    struct timeval ts = {tv.tv_sec, tv.tv_usec};
    strftime(tmp_ts_str, TS_BUF_LEN, ISO8601_FMT, gmtime(&ts.tv_sec));
    snprintf(buf, TS_BUF_LEN, ISO8601_us_FMT, tmp_ts_str, ts.tv_usec);
    return buf;
}

void usage() {
    fprintf(stderr, "read_flows <flow_file> [options]\n\n"
            "Prints flow records from the given flow result file in JSON. The format is\n"
            "a json object containing the flow list as the 'flows' attribute, and the\n"
            "a recordsTotal attribute giving the number of records present. The flows\n"
            "are sorted and paged according to the parameters below.\n\n"
            "  Options:\n"
            "    -a <field>    Sort ascending, by field.\n"
            "    -d <field>    Sort descending, by field.\n"
            "                  (Default sort is ascending, by start time.\n"
            "    -w <#records> Only return this many records.\n"
            "                  (Default: 20)\n"
            "    -s <#skip>    Skip to this record before returning results.\n"
            "                  (Default: 0)\n"
            "                    recordsTotal - Total flows in the unfiltered set.\n"
            "                    data - The list of flow records\n"
            "    -P            Make the results a little prettier.\n"
            "    -F            Print the sort field names and exit.\n"
            "    -h            Print this help and exit.\n"
    );
}

#define SORT_SRC_IP "src_ip"
#define SORT_DST_IP "dst_ip"
#define SORT_SRC_PORT "src_port"
#define SORT_DST_PORT "dst_port"
#define SORT_START_TS "start_ts"
#define SORT_END_TS "end_ts"
#define SORT_SIZE "size"
#define SORT_PACKETS "packets"
#define LONGEST_SORT_FIELD 8

// Set a hard size limit for sorting to 1 GB
#define SORT_SIZE_LIMIT 1024*1024*1024

int main(int argc, char **argv) {
    int i;
    int ret;
    int opt;
    const char * OPTIONS = "d:a:w:s:hFP";

    openlog("read_flows", LOG_NDELAY | LOG_PERROR, SYSLOG_FACILITY);

    opt = getopt(argc, argv, OPTIONS);

    // Set the default values
    sort_dir = 1;
    char sort_postfix = 'a';
    char sort_field[LONGEST_SORT_FIELD+1] = SORT_START_TS;
    uint64_t window = 20;
    uint64_t skip = 0;
    char * pp_nl = "";
    char * pp_indent = "";

    while (opt != -1) {
        switch (opt) {
            case 'F':
                printf("%s\n", SORT_SRC_IP);
                printf("%s\n", SORT_DST_IP);
                printf("%s\n", SORT_SRC_PORT);
                printf("%s\n", SORT_DST_PORT);
                printf("%s\n", SORT_START_TS);
                printf("%s\n", SORT_END_TS);
                printf("%s\n", SORT_SIZE);
                printf("%s\n", SORT_PACKETS);
                return 0;
            case 'h':
                usage();
                return 0;
            case 'P':
                pp_nl = "\n";
                pp_indent = "\n  ";
                break;
            case 'd':
                sort_dir = -1;
                sort_postfix = 'd';
                strncpy(sort_field, optarg, LONGEST_SORT_FIELD);
                break;
            case 'a':
                sort_dir = 1;
                sort_postfix = 'a';
                strncpy(sort_field, optarg, LONGEST_SORT_FIELD);
                break;
            case 'w':
                window = strtoul(optarg, NULL, 10);
                if (window == UINT64_MAX) {
                    CRIT("Invalid window size: %s", optarg);
                    return EINVAL;
                }
                break;
            case 's':
                skip = strtoul(optarg, NULL, 10);
                if (skip == UINT64_MAX) {
                    CRIT("Invalid skip size: %s", optarg);
                    return EINVAL;
                }
                break;
            default:
                CRIT("Invalid option: %c", opt);
                usage();
                return EINVAL;
        }
        opt = getopt(argc, argv, OPTIONS);
    }

    // Determine which sorting function to use
    __compar_fn_t func;
    if (strcmp(sort_field, SORT_SRC_IP) == 0) {
        func = src_ip_cmp;
    } else if (strcmp(sort_field, SORT_DST_IP) == 0) {
        func = dst_ip_cmp;
    } else if (strcmp(sort_field, SORT_SRC_PORT) == 0) {
        func = src_port_cmp;
    } else if (strcmp(sort_field, SORT_DST_PORT) == 0) {
        func = dst_port_cmp;
    } else if (strcmp(sort_field, SORT_SIZE) == 0) {
        func = size_cmp;
    } else if (strcmp(sort_field, SORT_PACKETS) == 0) {
        func = packets_cmp;
    } else if (strcmp(sort_field, SORT_START_TS) == 0) {
        func = start_ts_cmp;
    } else if (strcmp(sort_field, SORT_END_TS) == 0) {
        func= end_ts_cmp;
    } else {
        CRIT("Invalid sort\n");
        usage();
        return EINVAL;
    }

    if (argc - optind != 1) {
        CRIT("You must specify an input file.");
        return EINVAL;
    }

    size_t flow_fn_len = strlen(argv[optind]);
    char * flow_fn = argv[optind];

    struct stat flow_file_stat;
    stat(flow_fn, &flow_file_stat);

    char * sorted_fn;

    if (flow_file_stat.st_size > SORT_SIZE_LIMIT) {
        // If the file is larger than our size limit, don't sort the file.
        sorted_fn = flow_fn;
    } else {
        size_t sorted_fn_len = (flow_fn_len
                             + 1  /* '.' */
                             + LONGEST_SORT_FIELD  /* for the sort field name */
                             + 3);  /* '.%c' and a NULL */
        sorted_fn = malloc(sorted_fn_len);
        snprintf(sorted_fn, sorted_fn_len, "%s.%s.%c", flow_fn, sort_field, sort_postfix);
    }

    struct ordered_set * sorted_set;
    if (access(sorted_fn, F_OK) != 0) {
        // The sorted file doesn't already exist. Create it and sort the data.
        // Try opening the specified output file
        sorted_set = calloc(1, sizeof(struct ordered_set));
        ret = ord_set_init(sorted_set, OSET_FLOW, OSET_WRITE, sorted_fn);
        if (ret != 0) {
            // There is a race condition here between ord_set_init and the access call above.
            // I don't consider it likely enough to cause issues, and the issues it might
            // cause are just a minor interface hiccup.

            // Ord_set_init opens and writes the file in an otherwise safe and deadlock free way.
            CRIT("Unknown error opening output file: %s, error %s", sorted_fn, strerror(errno));
            return EACCES;
        }

        struct ordered_set * flow_set = calloc(1, sizeof(struct ordered_set));
        ret = ord_set_init(flow_set, OSET_FLOW, OSET_READ, flow_fn);
        if (ret != 0) {
            CRIT("Could not open flow set %s", flow_fn);
            return ret;
        }

        off_t file_size = flow_file_stat.st_size;
        struct fcap_flow_rec * flows = malloc((size_t)file_size);

        optind++;
        size_t index = 0;
        // Read through all flows
        while (1) {
            struct fcap_flow_rec flow_rec;
            if (ord_set_pop(flow_set, &flow_rec) != 0) {
                // We've read the last flow record
                break;
            }

            // Store each flow record in an array
            flows[index++] = flow_rec;
        }

        // Quicksort all of the flows based on the specified sort
        qsort(flows, index, sizeof(struct fcap_flow_rec), func);

        // Write the sorted flows to the output file
        for (i = 0; i < index; i++) {
            ret = ord_set_push(sorted_set, &flows[i]);
            if (ret != 0) return ret;
        }


        free(flow_set);
        free(flows);
        ord_set_cleanup(sorted_set);
    }

    sorted_set = calloc(1, sizeof(struct ordered_set));
    ret = ord_set_init(sorted_set, OSET_FLOW, OSET_READ, sorted_fn);
    if (ret != 0) {
        CRIT("Could not open sorted flow set %s", sorted_fn);
        return ret;
    }

    // The only error this can return is from being in the wrong mode.
    ord_set_seek(sorted_set, skip);

    printf("{%s\"recordsTotal\":%lu,%s\"flows\":[",
           pp_nl,
           flow_file_stat.st_size/sizeof(struct fcap_flow_rec),
           pp_nl);
    for (i = 0; i < window; i++) {
        union oset_types_u rec;

        if (ord_set_pop(sorted_set, &rec) == OSET_EMPTY) {
            break;
        }

        // Only print the comma before items after the first.
        if (i != 0) {
            printf(",");
        }

        char start_ts_buff[TS_BUF_LEN];
        char end_ts_buff[TS_BUF_LEN];

        printf("%s{%s\"first_ts\":\"%s\",\"last_ts\":\"%s\",",
               pp_nl, pp_indent,
               fmt_timeval32_iso8601us(rec.flow.key.first_ts, start_ts_buff),
               fmt_timeval32_iso8601us(rec.flow.key.last_ts, end_ts_buff)
        );
        printf("%s\"src_ip\":\"%s\",\"src_port\":%u,\"src_ip_vers\": %u,",
               pp_indent,
               iptostr((ip_addr_t *)&(rec.flow.key.src)),
               rec.flow.key.srcport,
               rec.flow.key.src_ip_vers
        );
        printf("%s\"dst_ip\":\"%s\",\"dst_port\":%u,\"dst_ip_vers\":%u,",
               pp_indent,
               iptostr((ip_addr_t *)&(rec.flow.key.dst)),
               rec.flow.key.dstport,
               rec.flow.key.dst_ip_vers
        );
        printf("%s\"proto\":%u,"
               "\"packets\":%lu,"
               "\"size\":%lu%s}",
               pp_indent,
               rec.flow.key.proto,
               ((uint64_t)rec.flow.key.packets) << rec.flow.key.packets_pow,
               ((uint64_t)rec.flow.key.size) << rec.flow.key.size_pow,
               pp_nl
        );
    }
    printf("]}%s", pp_nl);

    ord_set_cleanup(sorted_set);
    if (flow_fn != sorted_fn) free(sorted_fn);
}
