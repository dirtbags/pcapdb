#include "../network.h"
#include "test_args.h"
#include <arpa/inet.h>

int main(int argc, char ** argv) {
    FILE * testfile;
    char ipstr[20];

    ip_addr_t ip1, ip2;
    ip_addr_t *this, *next, *tmp;
    ip1.vers = ip2.vers = IPv4;
    this = &ip1;
    next = &ip2;

    if (argc == 2) {
        testfile = fopen(argv[1], "r");
        if (testfile == NULL) {
            fprintf(stderr, "Bad test file path: %s\n", argv[1]);
            return 1;
        }
    } else {
        fprintf(stderr, "Usage: ipcmp_test <test_file>");
        return 1;
    }
    fgets(ipstr, 20, testfile);
    this->addr.v4.s_addr = inet_addr(ipstr);
    while (fgets(ipstr, 20, testfile) != NULL) {
        next->addr.v4.s_addr = inet_addr(ipstr);
        if (ip_cmp(this, next) != -1 ||
            ip_cmp(next, this) != 1 || 
            ip_cmp(this, this) != 0) {
                printf("Comparison error %s <-> ", iptostr(this));
                printf("%s (%x <-> %x) - %d, %d, %d\n", iptostr(next), this->addr.v4.s_addr,
                                                                       next->addr.v4.s_addr,
                        ip_cmp(this, next), ip_cmp(next, this), ip_cmp(this, this));
                return 1;
        }

        tmp = this;
        this = next;
        next = tmp;

    }

    return 0;
}
