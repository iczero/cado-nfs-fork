#ifndef POLYSELECT2L_STR_H
#define POLYSELECT2L_STR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gmp.h>
#include <pthread.h>
#include "cado.h"
#include "utils.h"
#include "auxiliary.h"
#include "murphyE.h"

#define LEN_SPECIAL_Q 55

/* hash table structure */
typedef struct
{
  uint32_t *p;              /* contains the primes */
  int64_t *i;               /* contains the values of r such that p^2
                               divides N - (m0 + r)^2 */
  unsigned int alloc;      /* total allocated size */
  unsigned int size;       /* number of entries in hash table */
} __hash_struct;
typedef __hash_struct hash_t[1];

/* thread structure */
typedef struct
{
  mpz_t N;
  unsigned int d;
  unsigned long ad;
  int thread;
} __tab_struct;
typedef __tab_struct tab_t[1];

/* structure to store P roots */
typedef struct
{
  unsigned long size;    /* used size */
  unsigned int *nr;     /* number of roots of x^d = N (mod p) */
  uint64_t **roots; /* roots of (m0+x)^d = N (mod p^2) */
} __proots_struct;
typedef __proots_struct proots_t[1];

/* structure to store q roots */
typedef struct
{
  unsigned int alloc;   /* allocated size */
  unsigned int size;    /* used size */
  unsigned int *q;
  unsigned int *nr;     /* number of roots of x^d = N (mod p) */
  uint64_t **roots; /* roots of (m0+x)^d = N (mod p^2) */
} __qroots_struct;
typedef __qroots_struct qroots_t[1];

/* structure to store information on N, d, ad, etc... */
typedef struct
{
  mpz_t N;
  unsigned long d;
  unsigned long ad;
  mpz_t Ntilde;
  mpz_t m0;
} _header_struct;
typedef _header_struct header_t[1];

/* declarations */

extern const unsigned int SPECIAL_Q[];

unsigned long initPrimes (unsigned long, uint32_t**);
void printPrimes (uint32_t*, unsigned long);
void clearPrimes (uint32_t**);

void header_init (header_t, mpz_t, unsigned long, unsigned long);
void header_clear (header_t);

void proots_init (proots_t, unsigned long);
void proots_add (proots_t, unsigned long, unsigned long*, unsigned long);
void proots_print (proots_t, unsigned long);
void proots_clear (proots_t, unsigned long);

void qroots_init (qroots_t);
void qroots_realloc (qroots_t, unsigned long);
void qroots_add (qroots_t, unsigned int, unsigned int, unsigned long*);
void qroots_print (qroots_t);
void qroots_clear (qroots_t);

void hash_init (hash_t, unsigned long);
void hash_add (hash_t, unsigned long, int64_t, mpz_t, unsigned long,
                      unsigned int, mpz_t, unsigned long, mpz_t);
void hash_grow (hash_t);
void hash_clear (hash_t);

void print_poly_info (mpz_t *, unsigned int d, mpz_t *, int);

void match (unsigned long p1, unsigned long p2, int64_t i, mpz_t m0,
            unsigned long ad, unsigned int d, mpz_t N, unsigned long q,
            mpz_t rq);

#endif
