#ifndef FACUL_HPP_
#define FACUL_HPP_

#include <stdio.h>
#include <stdint.h>
#include <gmp.h>
//{{to define modset_t
#include "modredc_ul.h"
#include "modredc_15ul.h"
#include "modredc_2ul2.h"
#include "mod_mpz.h"
//}}
#include "cxx_mpz.hpp"
#include <vector>
#include <array>

#define PM1_METHOD 1
#define PP1_27_METHOD 2
#define PP1_65_METHOD 3
#define EC_METHOD 4
#define MPQS_METHOD 5

/* we should have FACUL_NOT_SMOOTH < 0, FACUL_MAYBE = 0,
   and FACUL_SMOOTH, FACUL_AUX >= 1 */
#define FACUL_NOT_SMOOTH (-1)
#define FACUL_MAYBE (0)
#define FACUL_SMOOTH (1)
#define FACUL_AUX (2)

#define STATS_LEN 128

/* We authorize at most NB_MAX_METHODS different methods in our
 * strategies. For the descent, we do have an interest in raising this
 * number somewhat.
 */
#define NB_MAX_METHODS 400

typedef struct {
  long method; /* Which method to use (P-1, P+1 or ECM) */
  void *plan;  /* Parameters for that method */
} facul_method_t;


/* All prime factors in the input number must be > fb. A factor of the 
   input number is assumed to be prime if it is < fb^2.
   The input number is taken to be not smooth if it has a 
   prime factor > 2^lpb. */

typedef struct {
  unsigned long lpb;        /* Large prime bound 2^lpb */
  double assume_prime_thresh; /* The factor base bound squared.
                               We assume that primes <= fbb have already been 
                               removed, thus any factor <= assume_prime_thresh 
                               is assumed prime without further test. */
  double BBB;               /* The factor base bound cubed. */
  facul_method_t *methods;  /* List of methods to try */
} facul_strategy_t;



typedef struct {
  facul_method_t* method;
  int side; /* To know on which side this method will be applied */
  int is_the_last; /* To know if this method is the last on its side
		      (used when you chain methods).  */
}facul_method_side_t;

typedef struct {
  unsigned long lpb[2];        /* Large prime bounds 2^lpb */
  double assume_prime_thresh[2]; /* The factor base bounds squared.
				    We assume that primes <= fbb have
				    already been removed, thus any
				    factor <= assume_prime_thresh is
				    assumed prime without further
				    test. */
  double BBB[2];               /* The factor base bounds cubed. */
  unsigned int mfb[2];        /* The cofactor bounds.*/
  facul_method_side_t ***methods;  /* List of methods to factor each pair
				      of cofactors.*/
  facul_method_t* precomputed_methods;  /* Optimization for
					   facul_make_strategies ().*/

  facul_method_side_t * uniform_strategy[2]; /* this is 0 if we have a
                                                strategy file, and 1 if we
                                                just have a uniform
                                                one-size-fits-all strategy.
                                                In the latter case, we avoid
                                                most of the allocation */
} facul_strategies_t;


/*  Required functionality:
    From an integer of any class/bitlength, initialise the fastest Modulus class and return a pointer to it or its base class.
    - need base class for Modulus classes
    - need base class for Integer classes with virtual size()/get()
    - need Factory method to generate the Modulus class: Modulus *initModulus(Integer i);
    From the Modulus class, call the correct facul implementation
    What is performance effect of having a virtual method in a base class? Does it add overhead when calling the final method in the final class?

    With templates:
    Need modset_t(Integer i) for each Integer class.
*/


struct modset_t {
  /* The arith variable tells which modulus type has been initialised for 
     arithmetic. It has a value of CHOOSE_NONE if no modulus currently 
     initialised. */
  enum {
      CHOOSE_NONE,
      CHOOSE_UL,
      CHOOSE_15UL,
      CHOOSE_2UL2,
      CHOOSE_MPZ,
  } arith;

  modulusredcul_t m_ul;
  modulusredc15ul_t m_15ul;
  modulusredc2ul2_t m_2ul2;
  modulusmpz_t m_mpz;
  void clear ();
  void get_z (mpz_t) const;

  /* Run the relevant mod_isprime() function, using the arithmetic selected in the modset */
  int isprime () const
  {
    switch (arith) {
    case CHOOSE_UL:
      return modredcul_isprime (m_ul);
    case CHOOSE_15UL:
      return modredc15ul_isprime (m_15ul);
    case CHOOSE_2UL2:
      return modredc2ul2_isprime (m_2ul2);
    case CHOOSE_MPZ:
      return modmpz_isprime (m_mpz);
    default:
      abort();
    }
  }
};

facul_method_t* facul_make_default_strategy (int, const int);
void facul_clear_methods (facul_method_t*);

int nb_curves (unsigned int, unsigned int);
facul_strategy_t * facul_make_strategy (unsigned long, unsigned int, unsigned int, int, int);
void facul_clear_strategy (facul_strategy_t *);
void facul_print_stats (FILE *);
int facul (std::vector<cxx_mpz> &, cxx_mpz const &, const facul_strategy_t *);

facul_strategies_t* facul_make_strategies (unsigned long, unsigned int,
					   unsigned int, unsigned long,
					   unsigned int, unsigned int,
                                           bool,
					   int, int, FILE*, const int);

void facul_clear_strategies (facul_strategies_t*);

int
facul_fprint_strategies (FILE*, facul_strategies_t* );


std::array<int,2>
facul_both (std::array<std::vector<cxx_mpz>, 2>&, std::array<cxx_mpz, 2> & ,
	    const facul_strategies_t *, int*);

#endif /* FACUL_HPP_ */
