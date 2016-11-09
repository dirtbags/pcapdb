#include "pcapdb.h"

#include <postgresql/libpq-fe.h>

// Attempts to get a connection to the database.
PGconn * get_db_conn(struct config * conf);

#define PG_TS_LEN 31
// Format the given timeval32 pointer as a timezone aware (GMT) string compatible with
// insertion into postgres.
void pgfmt_timeval(char *,            // Character array where the result should be stored.
        // This array should be of length PG_TS_LEN or longer.
        struct timeval32 *); // The time to format. Assumed to be GMT.

// Attempts to perform the given query with the given parameters.
// This is accomplished using the PQexecParams function, so parameters don't need
// to be escaped.
// Returns a pointer to a PGresult struct on success. This result structure
// will need to be freed with PQclear when you're done with it.
// Returns NULL and outputs the given error message on failure. The query and params
// are also output into the error log.
#define NO_TUPLES 0
#define TUPLES 1
PGresult * paramExec(PGconn *,              // PG db connection to use.
        const char *,          // query string. Arguments should be in $1,$2 format.
        int,                   // Number of parameters expected.
        const char * const *,  // Array of parameter strings.
        int,                   // Whether or not to expect tuples in the result.
        const char *);         // Error message to print on failure.

