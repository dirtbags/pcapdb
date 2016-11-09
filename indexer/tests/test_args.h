// Process arguments for a test process.
// Returns the final non-positional argument (assuming there is only one).
char * test_args(
        int,                        // argc
        char **,                    // argv
        struct system_state *);    // The capture state structure.

void usage();

// Tries to find a reasonable working directory.
void fix_working_dir();