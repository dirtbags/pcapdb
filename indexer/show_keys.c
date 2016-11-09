#include "keys.h"

#define USAGE "show_keys [-h]\n"\
    "   Print out the capture system key types used to identify various\n"\
    "   indices, and their names.\n"

int main(int argc, char ** argv) {
    if (argc != 1) {
        fprintf(stderr, USAGE);
        return 1;
    }

    int i;
    printf(" ID  - Key Name\n");
    printf("------------------------------\n");
    for (i=0; i <= tt_LAST; i++) {
        printf("%4d - %s\n", i, kt_name(i));
    }
}
