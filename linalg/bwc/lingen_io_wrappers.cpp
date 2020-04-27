#include "cado.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "lingen_io_wrappers.hpp"
#include "timing.h"
#include "lingen_average_matsize.hpp"
#include "lingen_io_matpoly.hpp"
#include "fmt/format.h"
#include "misc.h"
#ifdef HAVE_OPENMP
#include <omp.h>
#endif

#pragma GCC diagnostic ignored "-Wunused-parameter"

/* This one is currently in lingen_io_matpoly.cpp, but that file will go
 * away eventually */

extern unsigned int io_matpoly_block_size;

constexpr const unsigned int simd = matpoly::over_gf2 ? ULONG_BITS : 1;
constexpr const unsigned int splitwidth = matpoly::over_gf2 ? 64 : 1;

static int mpi_rank()
{
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

/* A note on MPI usage of these routines.
 *
 * All data is expected to follow these rules
 *
 *  - the matpoly arguments to {read_to,write_from}_matpoly are always
 *    expected to be significant only at the root. What happens to this
 *    data on non-zero ranks is largely unspecified, beyond the fact that
 *    collective calls to the routines should work and not have divergent
 *    behaviour. In practice we strive to just never access these
 *    arguments at non-zero ranks. (the same holds for the caches that
 *    are embedded in the i/o wrappers, with the exception that their
 *    limits cache_k0 and cache_k1 are consistent at all ranks).
 *
 *  - the data embedded in the i/o wrappers is either meaningful only at
 *    the root (in which case the {read_to,write_from} calls will do
 *    close to nothing, or is of collective type, meaning that
 *    {read_to,write_from} will engage in colective operations.
 *
 *  - all calls are implicit barriers, and their returned value is
 *    identical at all ranks.
 */

/* XXX It seems likely that lingen_average_matsize.?pp will go away
 */
double lingen_io_wrapper_base::average_matsize() const
{
    return ::average_matsize(ab, nrows, ncols, false);
}

unsigned int lingen_io_wrapper_base::preferred_window() const
{
    unsigned int nmats = iceildiv(io_matpoly_block_size, average_matsize());
    unsigned int nmats_pad = simd * iceildiv(nmats, simd);
    return nmats_pad;
}

double lingen_file_input::average_matsize() const
{
    return ::average_matsize(ab, nrows, ncols, ascii);
}

unsigned int lingen_file_input::preferred_window() const
{
    unsigned int nmats = iceildiv(io_matpoly_block_size, average_matsize());
    unsigned int nmats_pad = simd * iceildiv(nmats, simd);
#ifdef HAVE_OPENMP
    /* This might be a bit large */
    nmats_pad *= omp_get_max_threads() * omp_get_max_threads() / 4;
#if ULONG_BITS != 32
    /* limit to 32GB */
    for( ; ((size_t) nmats_pad) * average_matsize() > (1UL << 35) ; nmats_pad /= 2);
#endif
#endif
    return nmats_pad;
}

void lingen_file_input::open_file()
{
    if (mpi_rank()) return;
    f = fopen(filename.c_str(), ascii ? "r" : "rb");
    DIE_ERRNO_DIAG(!f, "open", filename.c_str());
}

void lingen_file_input::close_file()
{
    if (mpi_rank()) return;
    fclose(f);
}

size_t lingen_file_input::guessed_length() const
{
    unsigned long guess;

    if (length_hint) return length_hint;

    if (mpi_rank() == 0) {
        struct stat sbuf[1];
        int rc = fstat(fileno(f), sbuf);
        if (rc < 0) {
            perror(filename.c_str());
            exit(EXIT_FAILURE);
        }

        size_t filesize = sbuf->st_size;

        size_t avg = average_matsize();

        if (!ascii) {
            if (filesize % avg) {
                fprintf(stderr, "File %s has %zu bytes, while its size"
                        " should be a multiple of %zu bytes "
                        "(assuming binary input; "
                        "perhaps --ascii is missing ?).\n",
                        filename.c_str(), filesize, avg);
                exit(EXIT_FAILURE);
            }
            guess = filesize / avg;
        } else {
            double expected_length = filesize / avg;
            printf("# Expect roughly %.2f items in the sequence.\n", expected_length);

            /* First coefficient is always lighter, so we add a +1. */
            guess = 1 + expected_length;
        }
    }
    MPI_Bcast(&guess, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    return guess;
}

ssize_t lingen_file_input::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    /* This reads k1-k0 fresh coefficients from f into dst */
    size_t ll;
    if (!mpi_rank())
        ll = matpoly_read(ab, f, dst, k0, k1, ascii, 0);
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    return ll;
}

ssize_t lingen_random_input::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    ssize_t nk = MIN(length, next_src_k + k1 - k0) - next_src_k;
    if (!mpi_rank())
        dst.fill_random(k0, k0 + nk, rstate);
    next_src_k += nk;
    return nk;
}
unsigned int lingen_random_input::preferred_window() const
{
    unsigned int nmats = iceildiv(io_matpoly_block_size, average_matsize());
    unsigned int nmats_pad = simd * iceildiv(nmats, simd);
#ifdef HAVE_OPENMP
    /* This might be a bit large */
    nmats_pad *= omp_get_max_threads() * omp_get_max_threads() / 4;
#if ULONG_BITS != 32
    /* limit to 32GB */
    for( ; ((size_t) nmats_pad) * average_matsize() > (1UL << 35) ; nmats_pad /= 2);
#endif
#endif
    return nmats_pad;
}


/* pivots is a vector of length r <= M.n ;
 * M is a square matrix ;
 * coefficient (pivots[j],j) of M is -1.
 *
 * return true (and extend pivots) if column r of M is independent of the
 * previous columns.
 *
 * Only the coefficient of degree 0 is considered.
 */
static bool
reduce_column_mod_previous(matpoly& M, std::vector<unsigned int>& pivots)
{
    abelt tmp MAYBE_UNUSED;
    abinit(M.ab, &tmp);
    unsigned int r = pivots.size();
    for (unsigned int v = 0; v < r; v++) {
        unsigned int u = pivots[v];
        /* the v-th column in the M is known to
         * kill coefficient u (more exactly, to have a -1 as u-th
         * coefficient, and zeroes for the other coefficients
         * referenced in the pivots[0] to pivots[v-1] indices).
         */
        /* add M[u,r]*column v of M to column r of M */
#ifdef SELECT_MPFQ_LAYER_u64k1
        if (abis_zero(M.ab, M.coeff(u, r, 0)))
            continue;
#endif
        for (unsigned int i = 0; i < M.m; i++) {
#ifndef SELECT_MPFQ_LAYER_u64k1
            abmul(M.ab, tmp, M.coeff(i, v, 0), M.coeff(u, r, 0));
            abadd(M.ab, M.coeff(i, r, 0), M.coeff(i, r, 0), tmp);
            /* don't touch coeff u,r right now, since we're still using
             * it !
             */
            if (i == u)
                continue;
#else
            /* here, we know that coeff (u,r) is one */
            M.coeff_accessor(i, r, 0) += M.coeff(i, v);
#endif
        }
#ifndef SELECT_MPFQ_LAYER_u64k1
        abset_zero(M.ab, M.coeff(u, r, 0));
#endif
    }
    unsigned int u = 0;
    for (; u < M.m; u++) {
        if (!abis_zero(M.ab, M.coeff(u, r, 0)))
            break;
    }
    abclear(M.ab, &tmp);
    if (u != M.m) {
        pivots.push_back(u);
        return true;
    } else {
        return false;
    }
}

#ifndef SELECT_MPFQ_LAYER_u64k1
static void
normalize_column(matpoly& M, std::vector<unsigned int> const& pivots)
{
    abelt tmp MAYBE_UNUSED;
    abinit(M.ab, &tmp);
    unsigned int r = pivots.size() - 1;
    unsigned int u = pivots.back();
    /* Multiply the column so that the pivot becomes -1 */
    int rc = abinv(M.ab, tmp, M.coeff(u, r, 0));
    if (!rc) {
        fprintf(stderr, "Error, found a factor of the modulus: ");
        abfprint(M.ab, stderr, tmp);
        fprintf(stderr, "\n");
        exit(EXIT_FAILURE);
    }
    abneg(M.ab, tmp, tmp);
    for (unsigned int i = 0; i < M.m; i++) {
        abmul(M.ab, M.coeff(i, r, 0), M.coeff(i, r, 0), tmp);
    }
    abclear(M.ab, &tmp);
}
#endif

void
lingen_F0::share(int root, MPI_Comm comm)
{
    /* share the bw_dimensions structure, but I don't think it's even
     * remotely possible that nodes disagree on it.
     */
    MPI_Bcast(&nrhs, 1, MPI_UNSIGNED, root, comm);
    MPI_Bcast(&m, 1, MPI_UNSIGNED, root, comm);
    MPI_Bcast(&n, 1, MPI_UNSIGNED, root, comm);
    MPI_Bcast(&t0, 1, MPI_UNSIGNED, root, comm);
    static_assert(std::is_same<typename decltype(fdesc)::value_type,
                               std::array<unsigned int, 2>>::value,
                  "want unsigned ints");
    unsigned int fsize = fdesc.size();
    MPI_Bcast(&fsize, 1, MPI_UNSIGNED, root, comm);
    if (mpi_rank() != root)
        fdesc.assign(fsize, {{ 0, 0 }});
    if (fsize)
        MPI_Bcast(&fdesc.front(), 2 * fsize, MPI_UNSIGNED, root, comm);
}

/* It's a bit tricky. F0 is the _reversal_ of what we get here. (with
 * respect to t0).
 */
std::tuple<unsigned int, unsigned int> lingen_F0::column_data_from_Aprime(unsigned int jE) const
{
    unsigned int kA, jA;
    if (jE < n) {
        jA = jE;
        kA = t0;
    } else {
        jA = fdesc[jE-n][1];
        kA = fdesc[jE-n][0];
    }
    return { kA, jA };
}
std::tuple<unsigned int, unsigned int> lingen_F0::column_data_from_A(unsigned int jE) const
{
    unsigned int kA, jA;
    std::tie(kA, jA) = column_data_from_Aprime(jE);
    kA += jA >= nrhs;
    return { kA, jA };
}

void lingen_E_from_A::refresh_cache_upto(unsigned int k)
{
    unsigned int next_k1 = simd * iceildiv(k, simd);

    ASSERT_ALWAYS(next_k1 >= cache_k1);

    if (next_k1 != cache_k1) {
        if (!mpi_rank())
            cache.zero_pad(next_k1);
        ssize_t nk = A.read_to_matpoly(cache, cache_k1, next_k1);
        if (nk < (ssize_t) (k - cache_k1)) {
            fprintf(stderr, "short read from A\n");
            printf("This amount of data is insufficient. "
                    "Cannot find %u independent cols within A\n",
                    m);
            exit(EXIT_FAILURE);
        }
        cache_k1 = next_k1;
    }
}

void
lingen_E_from_A::initial_read()
{
    /* This is called collectively, because _all_
     * {read_to,write_from}_matpoly call are collective calls. */

    /* Our goal is to determine t0 and F0, and ultimately ensure that the
     * constant coefficient of E at t=t0 is a full-rank matrix.
     *
     * The initial constant coefficient of E is in fact the coefficient
     * of degree t0 of A'*F, where column j of A' is column j of A,
     * divided by X if j >= bm.d.nrhs.
     *
     * Therefore, for j >= bm.d.nrhs, the contribution to the constant
     * coefficient of E comes the following data from A:
     *  for cols of A with j < bm.d.rhs:  coeffs 0 <= deg <= t0-1
     *  for cols of A with j >= bm.d.rhs: coeffs 1 <= deg <= t0
     *
     * We need m independent columns.
     *
     * When we inspect the k coefficients of A, we are, in effect,
     * considering k*n-(n-rhs) = (k-1)*n+rhs columns. Therefore we expect
     * at the very least that we need to go as fas as
     *  (k-1)*n+rhs >= m
     *  k >= ceiling((m+n-rhs)/n)
     *
     * Note that these k coefficients of A mean k' coefficients of A',
     * where k' is either k or k-1. Namely, k' is k - (rhs == n)
     */

    share(0, MPI_COMM_WORLD);

    if (!mpi_rank())
        printf("Computing t0\n");

    /* We want to create a full rank m*m matrix M, by extracting columns
     * from the first coefficients of A */

    matpoly M(bw_dimensions::ab, m, m, 1);
    M.zero_pad(1);

    /* For each integer i between 0 and m-1, we have a column, picked
     * from column fdesc[i][1] of coeff fdesc[i][0] of A' which, once
     * reduced modulo the other ones, has coefficient at row
     * pivots[i] unequal to zero.
     */
    std::vector<unsigned int> pivots;

    for (t0 = 1; pivots.size() < m; t0++) {
        /* We are going to access coefficient k from A (perhaps
         * coefficients k-1 and k) */
        unsigned int k = t0 - (nrhs == n);

        /* k + 1 because we're going to access coeff of degree k, of
         * course.
         * k + 2 is because of the leading identity block in F0. To
         * form coefficient of degree t0, we'll need degree t0 in A',
         * which means up to degree t0 + (nrhs < n) = k + 1 in A
         */
        refresh_cache_upto(k + 2);

        for (unsigned int j = 0; pivots.size() < m && j < n; j++) {
            /* Extract a full column into M (column j, degree t0 or
             * t0-1 in A) */
            /* adjust the coefficient degree to take into account the
             * fact that for SM columns, we might in fact be
             * interested by the _previous_ coefficient */

            int inc;
            if (!mpi_rank()) {
                M.extract_column(pivots.size(), 0, cache, j, t0 - (j < nrhs));
                inc = reduce_column_mod_previous(M, pivots);
            }
            MPI_Bcast(&inc, 1, MPI_INT, 0, MPI_COMM_WORLD);
            if (inc && mpi_rank()) {
                // we only need to have pivots.size() right
                pivots.push_back(0);
            }

            if (!inc) {
                if (!mpi_rank())
                    printf(
                            "[X^%d] A, col %d does not increase rank (still %zu)\n",
                            t0 - (j < nrhs),
                            j,
                            pivots.size());

                /* we need at least m columns to get as starting matrix
                 * with full rank. Given that we have n columns per
                 * coefficient, this means at least m/n matrices.
                 */

                if ((t0-1) * n > m + 40) {
                    if (!mpi_rank())
                        printf("The choice of starting vectors was bad. "
                                "Cannot find %u independent cols within A\n",
                                m);
                    exit(EXIT_FAILURE);
                }
                continue;
            }

            /* Bingo, it's a new independent col. */

            fdesc.push_back( {{ t0 - 1, j }} );
#ifndef SELECT_MPFQ_LAYER_u64k1
            if (!mpi_rank())
                normalize_column(M, pivots);
#endif

            /*
            if (!mpi_rank())
                printf("[X^%d] A, col %d increases rank to %zu%s\n",
                        t0 - (j < nrhs),
                        j,
                        pivots.size(),
                        (j < nrhs) ? " (column not shifted because of the RHS)"
                        : "");
                        */

        }
        /* Do this before we increase t0 */
        if (pivots.size() == m)
            break;
    }

    if (!mpi_rank())
        printf("Found satisfactory init data for t0=%d\n", t0);

    share(0, MPI_COMM_WORLD);
}

void lingen_E_from_A::share(int root, MPI_Comm comm)
{
    lingen_F0::share(root, comm);
    MPI_Bcast(&cache_k0, 1, MPI_UNSIGNED, root, comm);
    MPI_Bcast(&cache_k1, 1, MPI_UNSIGNED, root, comm);
    if (!mpi_rank())
        cache.zero_pad(cache_k1 - cache_k0);
}

/* read k1-k0 new coefficients from src, starting at coefficient k0,
 * write them to the destination (which is embedded in the struct as a
 * reference), starting at coefficient next_dst_k
 */
template<>
ssize_t lingen_scatter<matpoly>::write_from_matpoly(matpoly const & src, unsigned int k0, unsigned int k1)
{
    long long nk;
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(next_dst_k % simd == 0);
    if (!mpi_rank()) {
        nk = MIN(k1, src.get_size()) - k0;
        ASSERT_ALWAYS(k1 <= src.get_size());
        E.zero_pad(next_dst_k + nk);
        for(unsigned int i = 0 ; i < nrows ; i++) {
            for(unsigned int j = 0; j < ncols ; j++) {
                abdst_vec to = E.part_head(i, j, next_dst_k);
                absrc_vec from = src.part_head(i, j, k0);
                abvec_set(ab, to, from, simd * iceildiv(nk, simd));
            }
        }
    }
    MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    next_dst_k += nk;
    return nk;
}

/* read k1-k0 new coefficients from src, starting at coefficient k0,
 * write them to the destination (which is embedded in the struct as a
 * reference), starting at coefficient next_dst_k
 */
template<>
ssize_t lingen_scatter<bigmatpoly>::write_from_matpoly(matpoly const & src, unsigned int k0, unsigned int k1)
{
    /* This one ***IS*** a collective call */
    long long nk;
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(next_dst_k % simd == 0);
    if (!mpi_rank()) {
        nk = MIN(k1, src.get_size()) - k0;
        ASSERT_ALWAYS(k1 <= src.get_size());
    }
    MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    E.zero_pad(next_dst_k + nk);
    E.scatter_mat_partial(src, k0, next_dst_k, nk);
    next_dst_k += nk;
    return nk;
}

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
template<>
ssize_t lingen_gather<matpoly>::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    long long nk;
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);
    ASSERT_ALWAYS(next_src_k % simd == 0);
    if (!mpi_rank()) {
        nk = MIN(k1, dst.get_size()) - k0;
        nk = MIN(nk, (ssize_t) (pi.get_size() - next_src_k));
        ASSERT_ALWAYS(k1 <= dst.get_size());
        for(unsigned int i = 0 ; i < nrows ; i++) {
            for(unsigned int j = 0; j < ncols ; j++) {
                abdst_vec to = dst.part_head(i, j, k0);
                absrc_vec from = pi.part_head(i, j, next_src_k);
                abvec_set(ab, to, from, nk);
            }
        }
    }
    MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    next_src_k += nk;
    return nk;
}

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
template<>
ssize_t lingen_gather<bigmatpoly>::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    /* This one ***IS*** a collective call */
    long long nk;
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(next_src_k % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);
    if (!mpi_rank()) {
        nk = MIN(k1, dst.get_size()) - k0;
        nk = MIN(nk, (ssize_t) (pi.get_size() - next_src_k));
        ASSERT_ALWAYS(k1 <= dst.get_size());
    }
    MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    pi.gather_mat_partial(dst, k0, next_src_k, nk);
    next_src_k += nk;
    return nk;
}

/* read k1-k0 new coefficients from pi, starting at coefficient
 * next_src_k, and write them to the destination, starting at coefficient
 * k0.
 */
ssize_t reverse_matpoly_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1, matpoly const & pi, unsigned int & next_src_k)
{
    ASSERT_ALWAYS(!mpi_rank());
    ssize_t nk;
    nk = MIN(k1, dst.get_size()) - k0;
    nk = MIN(nk, (ssize_t) (pi.get_size() - next_src_k));
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);
    ASSERT_ALWAYS(next_src_k % simd == 0);
    unsigned int d = pi.get_size();
    abdst_field ab = pi.ab;
#ifndef SELECT_MPFQ_LAYER_u64k1
    for(unsigned int i = 0 ; i < pi.nrows() ; i++) {
        for(unsigned int j = 0; j < pi.ncols() ; j++) {
            /* We'll read coefficients of degrees [d-k1, d-k0[, and
             * it might well be that d-k1<0
             */
            for(unsigned int k = k0 ; k < k0 + nk ; k++) {
                if (next_src_k + k < d + k0)
                    abset(ab,
                            dst.coeff(i, j, k),
                            pi.coeff(i, j, d-1-next_src_k-(k-k0)));
                else
                    abset_zero(ab, dst.coeff(i, j, k));
            }
        }
    }
#else
    size_t ntemp = (k1-k0) / simd + 2;
    unsigned int rk0, rk1;
    rk1 = next_src_k;
    rk1 = d >= rk1 ? d - rk1 : 0;
    rk0 = rk1 >= (unsigned int) nk ? rk1 - nk : 0;
    unsigned int n_rk = rk1 - rk0;
    unsigned int q_rk = rk0 / simd;
    unsigned int r_rk = rk0 % simd;
    unsigned int nq = iceildiv(n_rk + r_rk, simd);
    if (nq) {
        unsigned long * temp = new unsigned long[ntemp];
        for(unsigned int i = 0 ; i < pi.nrows() ; i++) {
            for(unsigned int j = 0; j < pi.ncols() ; j++) {
                /* We'll read coefficients of degrees [rk0, rk1[, and
                 * it might well be that rk0<0
                 */
                memset(temp, 0, ntemp * sizeof(unsigned long));

                ASSERT_ALWAYS(nq <= ntemp);
#define R(x, b) ((b)*iceildiv((x),(b)))
                abvec_set(ab, temp, pi.part(i, j) + q_rk, R(n_rk + r_rk, simd));
                if (rk1 % simd) mpn_lshift(temp, temp, nq, simd - (rk1 % simd));
                /* Now we only have to bit-reverse temp */
                bit_reverse(temp, temp, nq);
                abvec_set(ab, dst.part_head(i, j, k0), temp, R(nk, simd));
#undef R
            }
        }
        delete[] temp;
    }
#endif
    next_src_k += nk;
    return nk;
}

#if 0
shared_or_common_size<matpoly>::shared_or_common_size(matpoly const & pi)
{
    unsigned long s = pi.get_size();
    MPI_Bcast(&s, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    shared = s;
};
#endif

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
template<>
ssize_t lingen_gather_reverse<matpoly>::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    long long ll;
    if (!mpi_rank())
        ll = reverse_matpoly_to_matpoly(dst, k0, k1, pi, next_src_k);
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    return ll;
}

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
template<>
ssize_t lingen_gather_reverse<bigmatpoly>::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    /* This one ***IS*** a collective call */
    long long nk;
    unsigned int nq;
    unsigned int offset;
    long long dk;

    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);

    if (!mpi_rank()) {
        nk = MIN(k1, dst.get_size()) - k0;
        nk = MIN(nk, (ssize_t) (pi.get_size() - next_src_k));
        ASSERT_ALWAYS(k1 <= dst.get_size());
        unsigned int d = pi.get_size();
        unsigned int rk1 = next_src_k;
        rk1 = d >= rk1 ? d - rk1 : 0;
        unsigned int rk0 = rk1 >= (unsigned int) nk ? rk1 - nk : 0;
        unsigned int n_rk = rk1 - rk0;
        unsigned int q_rk = rk0 / simd;
        unsigned int r_rk = rk0 % simd;
        nq = iceildiv(n_rk + r_rk, simd);
        offset = q_rk * simd;
    }
    MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    MPI_Bcast(&nq, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
    MPI_Bcast(&offset, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    matpoly tpi(pi.ab, pi.nrows(), pi.ncols(), nq * simd);
    tpi.zero_pad(nq * simd);
    unsigned int zero = 0;
    pi.gather_mat_partial(tpi, zero, offset, nk);

    if (!mpi_rank())
        dk = reverse_matpoly_to_matpoly(dst, k0, k0 + nk, tpi, zero);

    MPI_Bcast(&dk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    ASSERT_ALWAYS(dk == nk);
    next_src_k += dk;
    return nk;
}

matpoly lingen_F_from_PI::recompute_rhs()
{
    abdst_field ab = bw_dimensions::ab;
    /*
     * first compute the rhscontribs. Use that to decide on a renumbering
     * of the columns, because we'd like to get the "primary" solutions
     * first, in a sense. Those are the ones with fewer zeroes in the
     * rhscontrib part. So we would want that matrix to have its columns
     * sorted in decreasing weight order
     *
     * An alternative, possibly easier, is to have a function which
     * decides the solution ordering precisely based on the inspection of
     * this rhscoeffs matrix (?). But how should we spell that info when
     * we give it to mksol ??
     */

    matpoly rhs(ab, nrhs, n, 1);
    rhs.zero_pad(1);

    /* adding the contributions to the rhs matrix. This is formed of the
     * LEADING COEFFICIENT of the corresponding columns of pi.
     */
    for (unsigned int jF = 0; jF < n; jF++) {
        unsigned int jpi = sols[jF].j;
        for (unsigned int ipi = 0; ipi < m + n; ipi++) {
            unsigned int iF;
            unsigned int s;
            std::tie(iF, s) = get_shift_ij(ipi, jF);
            if (iF >= nrhs)
                continue;
            s--;
            /* Coefficient (iF, ipi) of F0 is x^kF (with 0<=kF<=t0).
             * Multiplied by coefficient (ipi, jpi) of pi, whose length
             * is <=delta[jpi]+1, this induces a contribution to
             * coefficient
             * (iF, jF) of F. (actually since the length of column jF is
             * at most delta[jpi]+1, the length of column jpi of pi is at
             * most delta[jpi]+1-kF)
             *
             * The following formula gives the contribution C of this
             * coefficient (iF, ipi) of F0 to coefficient delta[j] + k of
             * A' * F:
             *
             * C = \sum_{s=0}^{delta[j]} A'_{iF,s+k} F_{iF,jF,delta[j]-s}
             *     (we understand F_{iF,jF,...} as representing the
             *     contribution of the coeff (iF, ipi) of F0 to this)
             *
             * C = \sum_{s=0}^{delta[j]} A'_{iF,s+k} Frev_{ipi,jpi,s}
             *     with Frev = rev_{delta[j]}(F).
             *
             * Let F0rev = rev_{t0}(F0) ; F0rev_{iF, ipi} is x^{t0-kF}
             *     pirev = rev_{G-1}(pi)  with G large enough. (>=pi.get_size())
             *
             * We have Frev{*,j} = (F0rev*pirev)_{*,j}  div (t0+G-1-delta[j])
             *         Frev{*,j,s} = (F0rev*pirev)_{*,j,s+t0+G-1-delta[j]}
             *                     = pirev_{*,j,s+G-1-delta[j]+kF}
             *
             * Let shift = G - 1 - delta[j]
             *
             * C = \sum_{s=0}^{delta[j]} A'_{iF,s+k} * pirev_{ipi,jpi,s+shift+kF}
             *
             * x^T*M^k*\sum_{s=0}^{delta[j]-kF} a_s c_s
             *
             * with a_s = M^s * Y_{iF}
             * and  c_s = [x^{shift + s + kF}](rev_{G}(pi))
             *            (with shift = G - 1 - delta[j])
             *
             * and rev_G(pi) is precisely what we have in our source, and
             * cached in cache.
             *
             * Now take this with s==0: we get the contribution of the
             * rhs.
             */

            /* add coefficient (ipi, jpi, s) of the reversed pi to
             * coefficient (iF, jF, 0) of rhs */
#ifndef SELECT_MPFQ_LAYER_u64k1
            absrc_elt src = cache.coeff(ipi, jpi, s);
            abdst_elt dst = rhs.coeff(iF, jF, 0);
            abadd(ab, dst, dst, src);
#else
            rhs.part(iF,jF)[0] ^= !abis_zero(ab, cache.coeff(ipi, jpi, s));
#endif
        }
    }
    return rhs;
}

void lingen_F_from_PI::reorder_solutions()
{
    abdst_field ab = bw_dimensions::ab;
    /* compute the score per solution */
    std::vector<std::array<unsigned int, 2>> sol_score;
    for (unsigned int jF = 0; jF < n; jF++) {
        unsigned int s = 0;
        for (unsigned int iF = 0; iF < nrhs; iF++)
            s += abis_zero(ab, rhs.coeff(iF, jF, 0));
        sol_score.push_back({{ s, jF }});
    }
    std::sort(sol_score.begin(), sol_score.end());
    if (nrhs && sol_score.size() && !mpi_rank()) {
        printf("Reordered solutions:\n");
        for (unsigned int i = 0; i < n; i++) {
            printf(" %u (col %u in pi, weight %u on rhs vectors)\n",
                   sol_score[i][1],
                   sols[sol_score[i][1]].j,
                   nrhs-sol_score[i][0]);
        }
    }
    {
        std::vector<sol_desc> sols2;
        for(auto const & s : sol_score) {
            sols2.push_back(sols[s[1]]);
        }
        sols = std::move(sols2);
    }
}

std::tuple<unsigned int, unsigned int> lingen_F_from_PI::get_shift_ij(unsigned int ipi, unsigned int jF) const
{
    // unsigned int jpi = sols[jF].j;
    unsigned int shift = sols[jF].shift;
    unsigned int iF, kF;
    std::tie(kF, iF) = column_data_from_Aprime(ipi);
    kF = t0 - kF;
    unsigned int s0 = shift + kF + (iF < nrhs);

    return {iF, s0 };
}

lingen_F_from_PI::lingen_F_from_PI(bmstatus const & bm,
        lingen_input_wrapper_base& pi, lingen_F0 const& F0)
    : lingen_F0(F0)
    , lingen_input_wrapper_base(pi.ab, n, n)
    , pi(pi)
    , cache(pi.ab, pi.nrows, pi.ncols, 0)
    , tail(pi.ab, nrows, ncols, 0)
    , rhs(pi.ab, nrhs, n, 1)
{
    /* The first n columns of F0 are the identity matrix, so that for (0
     * <= j < n),
     * column j of E is actually X^{-t0-e}*column j=jA of A. Notice that we
     * have in this case column_data_from_A(j) = { t0+e, j }. Here, e is
     * (j >= bm.d.nrhs)
     *
     * The last m columns, namely for (n <= j < m+n), are as follows: if
     *      std::tie(kA, jA) = column_data_from_A(j-n);
     * Then the entry at position (jA, j) is x^(t0-kA), so that
     * column j of E is actually X^{-kA}*column jA of A.
     *
     * As for F and pi, we have F = F0*pi so that
     *
     *  A' * F0 * pi = G
     *
     * colum j is such that max(1+deg((F0*pi)_j), G_j) <= delta[j]
     *
     * Each column uses therefore a combination of data from various
     * columns of A', which means in turn various columns of A.
     *
     * We want to separate the resulting expressions between the "rhs
     * contributions" (coefficients that appear as multipliers for the
     * right-hand-side columns, which are coefficients of degree 0 in A),
     * and the others (which all appear to some non-zero degree).
     *
     * Recall that each column of F0 has one single coefficient.
     * Therefore a coefficient of pi contributes to only one coefficient
     * of F0*pi. However, this is not 1-to-1: coefficients of F0*pi can
     * be the combination of several polynomials from pi.
     */

    /* We need to determine the spread between smallest and largest
     * degree columns among the solution columns.
     *
     * The length of column j of pi is at most 1+delta[j] (this might be
     * uneven depending on the rows).
     *
     * This length is also at least 1+delta[j]-t0, since otherwise we
     * can't have deg(F_j)=delta[j]
     */
    unsigned int lookback_needed = 0;

    for (unsigned int j = 0; j < m + n; j++) {
        if (bm.lucky[j] <= 0)
            continue;
        ASSERT_ALWAYS(bm.delta[j] >= t0);
        /* Really any large enough G should do. We must strive to change
         * in relevant places:
         * - here
         * - the comment in lingen_F_from_PI::recompute_rhs
         * - in reverse_matpoly_to_matpoly
         */
        size_t G = pi.guessed_length();
        ASSERT_ALWAYS(bm.delta[j] <= G);
        unsigned int s = G - 1 - bm.delta[j];
        sols.push_back({ j, s });
        if (s + 1 >= lookback_needed)
            lookback_needed = s + 1;
    }

    lookback_needed += t0;

    if (sols.size() != n)
        throw std::runtime_error("Cannot compute F, too few solutions found\n");

    lookback_needed = iceildiv(lookback_needed, simd) * simd;
    if (!mpi_rank()) {
        cache.zero_pad(lookback_needed);
    }
    pi.read_to_matpoly(cache, 0, lookback_needed);
    cache_k0 = 0;
    cache_k1 = lookback_needed;

    if (!mpi_rank()) {
        rhs = recompute_rhs();
        reorder_solutions();
    }
    MPI_Bcast(&sols.front(), 2 * n, MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    /* recompute rhs. Same algorithm as above. */
    if (!mpi_rank())
        rhs = recompute_rhs();
}

void lingen_F_from_PI::write_rhs(lingen_output_wrapper_base & Srhs)
{
    if (nrhs)
        Srhs.write_from_matpoly(rhs, 0, 1);
}

    

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
ssize_t lingen_E_from_A::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    abdst_field ab = bw_dimensions::ab;

    /* The first n columns of F0 are the identity matrix, so that
     * for (0 <= j < n),
     * column j of E is actually X^{-t0-e}*column j=jA of A. Notice that we
     * have in this case column_data_from_A(j) = { t0+e, j }. Here, e is
     * (j >= bm.d.nrhs)
     *
     * The last m columns, namely for (n <= j < m+n), are as follows: if 
     *      std::tie(kA, jA) = column_data_from_A(j);
     * Then the entry at position (jA, j) is x^(t0-kA), so that 
     * column j of E is actually X^{-kA}*column jA of A.
     *
     *
     * the max value of kA is t0 + (n < rhs)
     */
    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);

    ssize_t produced = 0;

    if (cache_k1 != cache_k0) {
        unsigned int F0_lookback = t0 + (nrhs < n);
        unsigned int lookback = cache_k1 - cache_k0;
        ASSERT_ALWAYS(lookback >= F0_lookback);

        long long nk;
        if (!mpi_rank()) {
            nk = MIN(k1, dst.get_size()) - k0;
            ASSERT_ALWAYS(cache.get_size() == lookback);
            cache.zero_pad(lookback + nk);
        }
        MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        ASSERT_ALWAYS(nk % simd == 0);

        /* This may be a collective call. At any rate, the cache variable is
         * meaningful only at the root. */
        ssize_t nread = A.read_to_matpoly(cache, lookback, lookback + nk);

        if (!mpi_rank())
            cache.set_size(lookback + nread);

        cache_k1 += nread;

        /* Do the bulk of the processing with an aligned count.
         * Note that if nread is not already aligned, it means that we had a
         * short read, since nk has to be aligned. And if we have a short
         * read, we're going to clear the cache on exit, so that it doesn't
         * matter if we lose the possibility to rotate the cache correctly.
         */
        nread -= nread % simd;

        if (!mpi_rank()) {
            /* process data at rank 0 */
            if (nread > 0) {
                for(unsigned int j = 0; j < m + n; j++) {
                    unsigned int jA;
                    unsigned int kA;
                    std::tie(kA, jA) = column_data_from_A(j);
                    /* column j of E is actually X^{-kA}*column jA of A. */
                    for(unsigned int i = 0 ; i < m ; i++) {
                        abdst_vec to = dst.part_head(i, j, k0);
                        absrc_vec from = cache.part_head(i, jA, kA);
#ifndef SELECT_MPFQ_LAYER_u64k1
                        abvec_set(ab, to, from, nread);
#else
                        unsigned int sr = kA % simd;
                        unsigned int nq = nread / simd;
                        if (sr) {
                            mpn_rshift(to, from, nq, sr);
                            to[nq-1] ^= from[nq] << (simd - sr);
                        } else {
                            abvec_set(ab, to, from, nread);
                        }
#endif
                    }
                }
            }
        }

        /* not that nread is agreed at all ranks */

        produced = nread;

        if (nread + k0 < k1) {
            /* We need to handle the end of the input stream, and stow
             * that to the (tail) field. This will be done at rank 0.
             */

            /* At this point we have a choice to make.
             * - Either we strive to perform the operation that is
             *   mathematically unambiguous on our input, akin to
             *   polynomial multiplication.
             * - Or we adapt to the true nature of the source we're
             *   dealing in the first place: an infinite series, which we
             *   know only to some precision.
             * We do the latter, by using coefficients from the cache
             * only until the point where some noise creeps in.
             */
            unsigned int cache_avail = cache_k1 - cache_k0;
            unsigned int cache_access = nread + F0_lookback;

            if (!mpi_rank()) {
                /* This is the correct final size of the tail */
                tail.zero_pad(cache_avail - MIN(cache_avail, cache_access));
                for(unsigned int k = nread ; k + F0_lookback < cache_avail; k += simd) {
                    for(unsigned int j = 0; j < m + n; j++) {
                        unsigned int jA;
                        unsigned int kA;
                        std::tie(kA, jA) = column_data_from_A(j);
                        /* column j of E is actually X^{-kA}*column jA of A. */
                        if (k + kA >= cache_avail) continue;
                        for(unsigned int i = 0 ; i < m ; i++) {
                            abdst_vec to = tail.part_head(i, j, k - nread);
                            absrc_vec from = cache.part_head(i, jA, kA + k);
#ifndef SELECT_MPFQ_LAYER_u64k1
                            abvec_set(ab, to, from, 1);
#else
                            unsigned int sr = kA % simd;
                            if (sr) {
                                *to = *from >> sr;
                                if (((kA + k) / simd+1) * simd < cache_avail)
                                    *to ^= from[1] << (simd - sr);
                            } else {
                                *to = *from;
                            }
#endif
                        }
                    }
                }
            }
            cache.clear();
            cache_k1 = cache_k0 = 0;
        } else {
            if (!mpi_rank())
                cache.rshift(nread);
            cache_k0 += nread;
        }
    }

    if (!mpi_rank()) {
        size_t extra;
        for(extra = 0 ; extra < tail.get_size() ; ) {
            for(unsigned int j = 0; j < ncols ; j++) {
                for(unsigned int i = 0 ; i < nrows ; i++) {
                    abdst_vec to = dst.part_head(i, j, k0 + produced + extra);
                    absrc_vec from = tail.part_head(i, j, extra);
                    abvec_set(ab, to, from, simd);
                }
            }
            extra += MIN(simd, tail.get_size() - extra);
        }
        produced += MIN(extra, tail.get_size());
        tail.rshift(MIN(extra, tail.get_size()));
    }

    long long ll = produced;
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    produced = ll;

    return produced;
}

/* read k1-k0 new coefficients from the source (which is embedded in the
 * struct as a reference), starting at coefficient next_src_k, and write
 * them to the destination, starting at coefficient k0.
 */
ssize_t lingen_F_from_PI::read_to_matpoly(matpoly & dst, unsigned int k0, unsigned int k1)
{
    abdst_field ab = bw_dimensions::ab;

    ASSERT_ALWAYS(k0 % simd == 0);
    ASSERT_ALWAYS(k1 % simd == 0);

    ssize_t produced = 0;

    if (cache_k1 != cache_k0) {
        unsigned int F0_lookback = t0 + (nrhs < n);
        unsigned int lookback = cache_k1 - cache_k0;
        ASSERT_ALWAYS(lookback >= F0_lookback - 1);
    
        long long nk;
        if (!mpi_rank()) {
            nk = MIN(k1, dst.get_size()) - k0;
            ASSERT_ALWAYS(nk % simd == 0);
            ASSERT_ALWAYS(cache.get_size() == lookback);
            cache.zero_pad(lookback + nk);
        }
        MPI_Bcast(&nk, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        ASSERT_ALWAYS(nk % simd == 0);

        /* This may be a collective call. At any rate, the cache variable is
         * meaningful only at the root. */
        ssize_t nread = pi.read_to_matpoly(cache, lookback, lookback + nk);

        if (!mpi_rank())
            cache.set_size(lookback + nread);

        cache_k1 += nread;

        /* Do the bulk of the processing with an aligned count.
         * Note that if nread is not already aligned, it means that we had a
         * short read, since nk has to be aligned. And if we have a short
         * read, we're going to clear the cache on exit, so that it doesn't
         * matter if we lose the possibility to rotate the cache correctly.
         */
        nread -= nread % simd;

        if (!mpi_rank()) {
            if (nread > 0) {
                for(unsigned int jF = 0 ; jF < n ; jF++) {
                    unsigned int jpi = sols[jF].j;
                    for(unsigned int ipi = 0 ; ipi < m + n ; ipi++) {
                        unsigned int s;
                        unsigned int iF;
                        std::tie(iF, s) = get_shift_ij(ipi, jF);

                        /* The reversal of pi_{ipi, jpi} contributes to entry (iF,
                         * jF), once shifted right by s.  */

                        abdst_vec to = dst.part_head(iF, jF, k0);
                        absrc_vec from = cache.part_head(ipi, jpi, s);
#ifndef SELECT_MPFQ_LAYER_u64k1
                        abvec_add(ab, to, to, from, nread);
#else
                        /* Add must at the same time do a right shift by sr */
                        unsigned int sr = s % simd;
                        if (sr) {
                            unsigned long cy = from[nread / simd] << (simd - sr);
                            for(unsigned int i = nread / simd ; i-- ; ) {
                                unsigned long t = (from[i] >> sr) ^ cy;
                                cy = from[i] << (simd - sr);
                                to[i] ^= t;
                            }
                        } else {
                            abvec_add(ab, to, to, from, nread);
                        }
#endif
                    }
                }
            }
        }

        /* not that nread is agreed at all ranks */

        produced = nread;

        if (nread + k0 < k1) {
            /* We've reached the end of our input stream. Now is time to drain
             * the cache. We'll first stow that in the (tail) field.
             */
            unsigned int cache_avail = cache_k1 - cache_k0;
            unsigned int max_tail = 0;

            for(unsigned int jF = 0 ; jF < n ; jF++) {
                for(unsigned int ipi = 0 ; ipi < m + n ; ipi++) {
                    unsigned int s;
                    unsigned int iF;
                    std::tie(iF, s) = get_shift_ij(ipi, jF);
                    if (nread + s < cache_avail) {
                        unsigned int pr = cache_avail - (nread + s);
                        if (pr >= max_tail)
                            max_tail = pr;
                    }
                }
            }

            if (!mpi_rank()) {
                tail.zero_pad(max_tail);

                for(unsigned int k = nread ; k < nread + max_tail ; k += simd) {
                    for(unsigned int jF = 0 ; jF < n ; jF++) {
                        unsigned int jpi = sols[jF].j;
                        for(unsigned int ipi = 0 ; ipi < m + n ; ipi++) {
                            unsigned int s;
                            unsigned int iF;
                            std::tie(iF, s) = get_shift_ij(ipi, jF);
                            abdst_vec to = tail.part_head(iF, jF, k - nread);
                            absrc_vec from = cache.part_head(ipi, jpi, k + s);
#ifndef SELECT_MPFQ_LAYER_u64k1
                            abvec_add(ab, to, to, from, 1);
#else
                            /* Add must at the same time do a right shift by sr */
                            unsigned int sr = s % simd;
                            if (sr) {
                                to[0] ^= from[0] >> sr;
                                if (((s + produced) / simd+1) * simd < (cache_k1 - cache_k0))
                                    to[0] ^= from[1] << (simd - sr);
                            } else {
                                to[0] ^= from[0];
                            }
#endif
                        }
                    }
                }
            }
            cache.clear();
            cache_k1 = cache_k0 = 0;
        } else {
            if (!mpi_rank())
                cache.rshift(nread);
            cache_k0 += nread;
        }
    }

    if (!mpi_rank()) {
        size_t extra;
        for(extra = 0 ; extra < tail.get_size() ; ) {
            for(unsigned int j = 0; j < ncols ; j++) {
                for(unsigned int i = 0 ; i < nrows ; i++) {
                    abdst_vec to = dst.part_head(i, j, k0 + produced + extra);
                    absrc_vec from = tail.part_head(i, j, extra);
                    abvec_set(ab, to, from, simd);
                }
            }
            extra += MIN(simd, tail.get_size() - extra);
        }
        produced += MIN(extra, tail.get_size());
        tail.rshift(MIN(extra, tail.get_size()));
    }

    long long ll = produced;
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    produced = ll;

    return produced;
}

void lingen_output_to_singlefile::open_file()
{
    if (!mpi_rank()) {
        std::ios_base::openmode mode = std::ios_base::out;
        if (!ascii) mode |= std::ios_base::binary;
        os = std::unique_ptr<std::ofstream>(new std::ofstream(filename, mode));
    }
    done_open = true;
}

ssize_t lingen_output_to_singlefile::write_from_matpoly(matpoly const & src, unsigned int k0, unsigned int k1)
{
    /* This should be fixed. We seem to be used to writing this
     * transposed. That's slightly weird.
     */
    if (!done_open)
        open_file();
    ssize_t ll;
    if (!mpi_rank())
        ll = matpoly_write(ab, *os, src, k0, k1, ascii, 1);
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    return ll;
}

ssize_t lingen_output_to_splitfile::write_from_matpoly(matpoly const & src, unsigned int k0, unsigned int k1)
{
    /* This should be fixed. We seem to be used to writing this
     * transposed. That's slightly weird.
     */
    if (!done_open)
        open_file();
    ssize_t ll;
    if (!mpi_rank())
        ll = matpoly_write_split(ab, fw, src, k0, k1, ascii);
    MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    return ll;
}

void lingen_output_to_splitfile::open_file()
{
    if (!mpi_rank()) {
        ASSERT_ALWAYS(!done_open);
        std::ios_base::openmode mode = std::ios_base::out;
        if (!ascii) mode |= std::ios_base::binary;
        for(unsigned int i = 0 ; i < nrows ; i+=splitwidth) {
            for(unsigned int j = 0 ; j < ncols ; j+=splitwidth) {
                std::string s = fmt::format(pattern,
                        i, i+splitwidth,
                        j, j+splitwidth);
                fw.emplace_back(std::ofstream { s, mode });
                DIE_ERRNO_DIAG(!fw.back(), "open", s.c_str());
            }
        }
    }
    done_open = true;
}

lingen_output_to_splitfile::lingen_output_to_splitfile(abdst_field ab, unsigned int m, unsigned int n, std::string const & pattern, bool ascii)
    : lingen_output_wrapper_base(ab, m, n)
    , pattern(pattern)
    , ascii(ascii)
{
}

lingen_output_to_sha1sum::~lingen_output_to_sha1sum()
{
    if (!written || mpi_rank()) return;
    char out[41];
    f.checksum(out);
    printf("checksum(%s): %s\n", who.c_str(), out);
}

ssize_t
lingen_output_to_sha1sum::write_from_matpoly(matpoly const& src,
                               unsigned int k0,
                               unsigned int k1)
{
    ASSERT_ALWAYS(k0 % simd == 0);
#ifdef SELECT_MPFQ_LAYER_u64k1
    ASSERT_ALWAYS(src.high_word_is_clear());
    size_t nbytes = iceildiv(k1 - k0, ULONG_BITS) * sizeof(unsigned long);
#else
    size_t nbytes = abvec_elt_stride(src.ab, k1-k0);
#endif
    written += src.nrows() * src.ncols() * nbytes;
    if (!mpi_rank()) {
        for(unsigned int i = 0 ; i < src.nrows() ; i++)
            for(unsigned int j = 0; j < src.ncols() ; j++)
                f.write((const char *) src.part_head(i, j, k0), nbytes);
    }
    return k1 - k0;
}

void pipe(lingen_input_wrapper_base & in, lingen_output_wrapper_base & out, const char * action, bool skip_trailing_zeros)
{
    unsigned int window = std::max(in.preferred_window(), out.preferred_window());
    if (window == UINT_MAX) {
        window=4096;
    }
    matpoly F(in.ab, in.nrows, in.ncols, 0);
    matpoly Z(in.ab, in.nrows, in.ncols, 0);
    if (!mpi_rank()) {
        F.zero_pad(window);
        Z.zero_pad(window);
    }

    double tt0 = wct_seconds();
    double next_report_t = tt0;
    size_t next_report_k = 0;
    size_t expected = in.guessed_length();
    size_t zq = 0;
    for(size_t done = 0 ; ; ) {
        F.set_size(0);
        F.zero_pad(window);
        ssize_t n = in.read_to_matpoly(F, 0, window);
        bool is_last = n < (ssize_t) window;
        if (n <= 0) break;
        ssize_t n1;
        long long ll;
        if (!mpi_rank()) {
            F.set_size(n);
            ll = skip_trailing_zeros ? F.get_true_nonzero_size() : n;
        }
        MPI_Bcast(&ll, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        n1 = ll;
        if (n1 == 0) {
            zq += n;
            continue;
        }
        /* write the good number of zeros */
        for( ; zq ; ) {
            Z.set_size(0);
            unsigned int nz = MIN(zq, window);
            if (!mpi_rank())
                Z.zero_pad(nz);
            ssize_t nn = out.write_from_matpoly(Z, 0, nz);
            if (nn < (ssize_t) nz) {
                fprintf(stderr, "short write\n");
                exit(EXIT_FAILURE);
            }
            zq -= nz;
            done += nz;
        }
        ssize_t nn = out.write_from_matpoly(F, 0, n1);
        if (nn < n1) {
            fprintf(stderr, "short write\n");
            exit(EXIT_FAILURE);
        }
        zq = n - n1;
        done += n1;
        if (action && !mpi_rank() && (done >= next_report_k || is_last)) {
            double tt = wct_seconds();
            if (tt > next_report_t || is_last) {
                printf(
                        "%s %zu coefficients (%.1f%%)"
                        " in %.1f s (%.1f MB/s)\n",
                        action,
                        done, 100.0 * done / expected,
                        tt-tt0, done * in.average_matsize() / (tt-tt0)/1.0e6);
                next_report_t = tt + 10;
                next_report_k = done + expected / 100;
            }
        }
        if (is_last) break;
    }
}
