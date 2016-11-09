#include <getopt.h>
#include <pcap.h>
#include <stdint.h>
#include "pcapdb.h"

#define ARG_ERR 1
#define FCAP_ERR 2
#define WRITE_ERR 3

#define BUFFER_SIZE 4096

void usage();

int main(int argc, char ** argv) {
    int c;

    const char * OPTIONS = "r:w:";

    char * out_fn = NULL;
    char * in_fn = NULL;

    c = getopt(argc, argv, OPTIONS);
    while( c != -1 ) {
        switch (c) {
            case 'r':
                in_fn = optarg;
                break;
            case 'w':
                out_fn = optarg;
                break;
            default:
                fprintf(stderr, "Invalid option: %c\n", c);
                usage();
                return ARG_ERR;
        }
        c = getopt(argc, argv, OPTIONS);
    }

    if (optind != argc) {
        fprintf(stderr, "Bad arguments.\n");
        usage();
        return ARG_ERR;
    }

    FILE * in_file = stdin;
    if (in_fn != NULL) {
        in_file = fopen(in_fn, "r");
        if (in_file == NULL) {
            fprintf(stderr, "Bad input filename: %s\n", in_fn);
            usage();
            return ARG_ERR;
        }
    }

    FILE * out_file = stdout;
    if (out_fn != NULL) {
        out_file = fopen(out_fn, "w");
        if (out_file == NULL) {
            fprintf(stderr, "Bad output filename: %s\n", out_fn);
            usage();
            return ARG_ERR;
        }
    }

    struct pcap_file_header header;

    size_t read = fread(&header, sizeof(struct pcap_file_header), 1, in_file);
    if (read != 1) {
        fprintf(stderr, "Truncated FCAP file (incomplete header).\n");
        usage();
        return FCAP_ERR;
    }

    uint64_t pkt_count = header.sigfigs;

    header.sigfigs = 0;
    size_t wrote = fwrite(&header, sizeof(struct pcap_file_header), 1, out_file);
    uint64_t written = wrote*sizeof(struct pcap_file_header);
    if (wrote != 1) {
        fprintf(stderr, "Could not write to output.\n");
        usage();
        return WRITE_ERR;
    }

    uint64_t pkt, b;
    struct pcap_pkthdr32 phdr;
    uint8_t buffer[BUFFER_SIZE];
    pkt = 0;
    int limit = 0;
    while (pkt < pkt_count) {
        //fprintf(stderr, "Reading pkt: %lu\n", pkt);
        read = fread(&phdr, sizeof(struct pcap_pkthdr32), 1, in_file);
        if (read != 1) {
            fprintf(stderr, "Truncated FCAP file (incomplete packet header).\n");
            return FCAP_ERR;
        }
        wrote = fwrite(&phdr, sizeof(struct pcap_pkthdr32), 1, out_file);
        written += wrote*sizeof(struct pcap_pkthdr32);
        if (wrote != 1) {
            fprintf(stderr, "Could not write to output.");
            return WRITE_ERR;
        }

        //fprintf(stderr, "phdr: %lx, %lx, %x, %x\n", phdr.ts.tv_sec, phdr.ts.tv_usec, phdr.caplen, phdr.len);

        b = 0;
        while (b < phdr.caplen) {
            size_t to_read = (phdr.caplen - b > BUFFER_SIZE) ? BUFFER_SIZE : phdr.caplen;
            //fprintf(stderr, "pkt, caplen, to_read: %lu, %u, %lu\n", pkt, phdr.caplen, to_read);
            read = fread(buffer, sizeof(uint8_t), to_read, in_file);
            b += read;
            //fprintf(stderr, "%lu bytes of packet data read.\n", read);
            int i, n;
            //fprintf(stderr, "Data Read:\n");
            /*
            for (i=0; i < read; i++) {
                fprintf(stderr, "%02x ", buffer[i]);
                if (i%8 == 7 || (i + 1) == read ) {
                    if ((i+1) == read) for (n=i+1; n % 8 != 0; n++) fprintf(stderr, "   ");
                    for (n = i - i%8; n <= i; n++) {
                        uint8_t pchar = buffer[n];
                        if (pchar < 0x20 || pchar > 0x7e) pchar = '.';
                        fprintf(stderr, "%c", pchar);
                    }
                    fprintf(stderr, "\n");
                }
            } fprintf(stderr, "\n");
            */
            if (read == 0) {
                fprintf(stderr, "Truncated FCAP file (incomplete packet data).\n");
                return FCAP_ERR;
            }
            wrote = fwrite(buffer, sizeof(uint8_t), read, out_file);
            written += wrote;
            //fprintf(stderr, "%lu bytes of packet data written.\n", wrote);
            if (wrote != read) {
                fprintf(stderr, "Could not write to output.");
                return WRITE_ERR;
            }
        }
        //if (limit++ > 2) break;
        pkt++;
    }

    //fprintf(stderr, "Total Bytes Written: %lu\n", written);

    fclose(out_file);
    fclose(in_file);

    return 0;

}

void usage() {
    fprintf(stderr,"Usage:\n");
    fprintf(stderr,"fcap2pcap [-r in_file] [-w out_file]\n\n");
    fprintf(stderr,"The input and output files default to stdin and stdout, respectfully.\n");
}