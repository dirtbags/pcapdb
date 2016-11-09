#include "../network.h"
#include "../output.h"
#include <string.h>
#include <stdlib.h>


int main() {
    const char * conn_info = "";
    struct save_info save;
    struct config conf;
    struct timeval32 start_tv, end_tv;

    struct network_stats stats;

    PGconn * pg_cnx;

    stats.chain_size = 1000;
    stats.dll_errors = 0;
    stats.dropped = 5;
    stats.ipv4 = 100;
    stats.ipv6 = 101;
    stats.network_errors = 3;
    stats.other_net_layer = 89;
    stats.transport_errors = 4;
    uint64_t i;
    for (i=0; i < 256; i++) {
        stats.transport[i] = i;
    }
    stats.captured_pkts = 0;
    stats.sys_dropped = 1001;

    start_tv.tv_sec = (uint32_t) time(NULL);
    start_tv.tv_usec = 123456;
    end_tv.tv_sec = (uint32_t)time(NULL) + 1000;
    end_tv.tv_usec = 654321;

    strncpy(conf.base_data_path, "/tmp/cornet", BASE_DIR_LEN);

    pg_cnx = PQconnectdb(conn_info);

    if (PQstatus(pg_cnx) != CONNECTION_OK) {
        fprintf(stderr,"Connection failed: %s\n", PQerrorMessage(pg_cnx));
        PQfinish(pg_cnx);
        return 1;
    }

    if (set_save_info(&conf, pg_cnx, &start_tv, &end_tv, &save) != OB_OK) return 1;

    if (save_stats(pg_cnx, &stats, save.index_id) != OB_OK) return 2;

    if (set_index_ready(pg_cnx, save.index_id) != OB_OK) return 3;

    PQfinish(pg_cnx);
    return 0;
}


