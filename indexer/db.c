#include <postgresql/libpq-fe.h>
#include "db.h"

// Get a new postgres connection given our connection info.
PGconn * get_db_conn(struct config * conf) {
    PGconn * pg_cnx;

    pg_cnx = PQconnectdb(conf->db_connect_str);

    if (PQstatus(pg_cnx) != CONNECTION_OK) {
        ERR("Could not connect to the system database.\n");
        ERR(conf->db_connect_str);
        PQfinish(pg_cnx);
        return NULL;
    }

    return pg_cnx;
}

// Try to perform the given query with the given params.
// On failure, roll back the transaction and clear all result values.
// The result variable is returned, and will need to be cleared.
PGresult * paramExec(PGconn * conn, // The postgres connection to use
                     const char * query, // The query string
                     int nParams, // The number of expected parameters in the query
                     const char * const *params, // The parameters, as strings
                     int expect_tuples, // Result flags vary depending on whether or not
                                        // tuples are the expected as the result.
                     const char * eMsg) { // The error message to print when things go wrong.
    PGresult * res;
    ExecStatusType res_flag;
    int i;

    // Send query over psql conn, as well the parameters.
    // The NULL tells the server to figure out the param types on its own.
    res = PQexecParams(conn, query, nParams, NULL, params,
            // The last three args specify that we are neither sending nor want
            // binary data.
            NULL, NULL, 0);
    res_flag = PQresultStatus(res);
    if (! ((res_flag == PGRES_TUPLES_OK && expect_tuples == TUPLES) ||
            (res_flag == PGRES_COMMAND_OK && expect_tuples == NO_TUPLES)) ) {
        ERR("Query Error: %s(%s)", eMsg, PQresultErrorMessage(res));
        INFO("query: %s", query); INFO("\n");
        if (nParams > 0) {
            INFO("query params: ");
            for (i = 0; i < nParams; i++) INFO("%s ", params[i]);
        }
        PQclear(res);
        PQclear(PQexec(conn, "ROLLBACK"));
        return NULL;
    }
    return res;
}

// Format a timeval pointer for use as a postgres parameter.
// Stores the result in dest, which should be at least PG_TS_LEN bytes in length.
void pgfmt_timeval(char * dest, struct timeval32 * tv) {
    char tmp_ts_str[PG_TS_LEN];
    struct timeval ts = {tv->tv_sec, tv->tv_usec};

    strftime(tmp_ts_str, PG_TS_LEN, "%Y-%m-%d %H:%M:%S", gmtime(&ts.tv_sec));
    snprintf(dest, PG_TS_LEN, "%s.%06lu UTC", tmp_ts_str, ts.tv_usec);
}