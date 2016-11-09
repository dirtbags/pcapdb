#include "../network.h"
#include "test_args.h"
#include <arpa/inet.h>
#include <stdlib.h>

//
// Created by pflarr on 1/13/16.
//

# define TESTS 10

uint8_t rand8() {
    uint8_t r = (uint8_t)(rand() % 256);
    //printf("r: %d", r);
    return r;
}

uint8_t main() {
    ip_addr_t ipA, ipB;
    ipA.vers = ipB.vers = IPv6;

    char buffA[INET6_ADDRSTRLEN], buffB[INET6_ADDRSTRLEN];

    int i, j;

    srand(time(NULL));

    for (i = 0; i < TESTS; i++) {
        for (j=0; j < 16; j++) {
            ipA.addr.v6.s6_addr[j] = 0;
            ipB.addr.v6.s6_addr[j] = 0;
        }
        for (j=15; j > -1; j--) {
            ipA.addr.v6.s6_addr[j] = rand8();
            ipB.addr.v6.s6_addr[j] = rand8();
            inet_ntop(AF_INET6, &ipA.addr.v6, buffA, INET6_ADDRSTRLEN);
            inet_ntop(AF_INET6, &ipB.addr.v6, buffB, INET6_ADDRSTRLEN);
            printf("A: %s, B: %s, A cmp B: %d\n", buffA, buffB, ip_cmp(&ipA, &ipB));
        }
    }
    return 0;
}
