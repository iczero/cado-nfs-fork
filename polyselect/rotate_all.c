#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cado.h"
#include "utils.h"
#include "auxiliary.h" /* for common routines with polyselect.c */

extern int MAX_k;

static void usage_and_die(char *argv0) {
    fprintf(stderr, "usage: %s poly kmax\n", argv0);
    fprintf(stderr, "  apply rotation f += (j*x+k)*g to poly.\n");
    fprintf(stderr, "  poly: filename of polynomial\n");
    fprintf(stderr, "  j,k : integers\n");
    exit(1);
}

int main(int argc, char **argv) {
    cado_poly poly;
    long kmax, jmin, kmin;
    unsigned long alim = 2000;
    mpz_t b, m;

    mpz_init(b);
    mpz_init(m);
    if (argc != 3)
        usage_and_die(argv[0]);
    cado_poly_init (poly);
    if (!cado_poly_read(poly, argv[1])) {
        fprintf(stderr, "Problem when reading file %s\n", argv[1]);
        usage_and_die(argv[0]);
    }
    kmax = strtol(argv[2], NULL, 10);
    MAX_k = kmax;

    mpz_set (b, poly->g[1]);
    mpz_neg (m, poly->g[0]);
    rotate (poly->f, poly->degree, alim, m, b, &jmin, &kmin, 0, 1);
    poly->skew = SKEWNESS (poly->f, poly->degree, SKEWNESS_DEFAULT_PREC);
    mpz_clear(b);
    mpz_clear(m);

    print_poly (stdout, poly, argc, argv, 0, 1);
    return 0;
} 
