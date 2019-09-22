#include "cado.h"
#include "mpfq_layer.h"
#include <cstdlib>
#include "portability.h"
#include "macros.h"
#include "lingen_matpoly_ft.hpp"
#include "lingen_bigmatpoly_ft.hpp"
#include "logline.h"
#include "fmt/format.h"

/* This is the interface for doing products of polynomial matrices by
 * caching transforms, and transferring them over MPI. There are
 * currently two pitfalls.
 *
 * 1/ we have no streaming of computations and communications. This means
 * two things. First, communication/computation overlap can be
 * beneficial: while transforms of rows on the left term of a product are
 * computed (assuming transforms of the right term are ready), it is
 * possible to communicate the already computed row transforms. A second
 * point is that if we manage to get these computed soon, then there is
 * no need to keep them in memory. Such an approach would make it
 * possible to reduce the overall memory footprint. The place to fix this
 * is to modify the control flow in bigmatpoly_mul_caching_adj
 *
 * 2/ we are transferring full-length transforms, while it would 
 * be sufficient to transfer the truncated transforms of course.
 */

#include "lingen_matpoly_bigmatpoly_ft_common.hpp"

template<typename fft_type> struct OP_CTX<bigmatpoly, fft_type> : public OP_CTX_base<bigmatpoly> {
    MPI_Datatype mpi_ft;
    tree_stats & stats;
    typedef bigmatpoly T;
    typedef fft_type FFT;
    template<typename... Args>
    OP_CTX(tree_stats & stats, fft_transform_info const & fti, Args&&... args) : OP_CTX_base<T>(args...), stats(stats) {
        size_t fft_alloc_sizes[3];
        fft_transform_info_get_alloc_sizes(&fti, fft_alloc_sizes);
        size_t tsize = fft_alloc_sizes[0];
        MPI_Type_contiguous(tsize, MPI_BYTE, &mpi_ft);
        MPI_Type_commit(&mpi_ft);
    }
    ~OP_CTX() {
        MPI_Type_free(&mpi_ft);
    }
    inline int a_irank() const { return a.irank(); }
    inline int b_irank() const { return b.irank(); }
    inline int a_jrank() const { return a.jrank(); }
    inline int b_jrank() const { return b.jrank(); }
    inline int mesh_inner_size() const { return a.n1; }
    static const bool uses_mpi = true;
    inline void mesh_checks() const {
        ASSERT_ALWAYS(a.get_model().is_square());
        ASSERT_ALWAYS(a.get_model() == b.get_model());
        ASSERT_ALWAYS(a.irank() == b.irank());
        ASSERT_ALWAYS(a.jrank() == b.jrank());
    }
    void alloc_c_if_needed(size_t size) const {
        if (c.m != a.m || c.n != a.n || c.get_size() != size)
            c = T(a.ab, a.get_model(), a.m, b.n, size);
    }
    inline matpoly const & a_local() { return a.my_cell(); }
    inline matpoly const & b_local() { return b.my_cell(); }
    inline matpoly & c_local() { return c.my_cell(); }
    private:
    inline void do_allgather(void * p, MPI_Comm com, int n) {
        MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, 
                p, n,
                mpi_ft, com);
    }
    public:
    inline void a_allgather(void * p, int n) {
        do_allgather(p, a.get_model().com[1], n);
    }
    inline void b_allgather(void * p, int n) {
        do_allgather(p, b.get_model().com[2], n);
    }
    template<typename OP> void doit(OP & op, lingen_call_companion::mul_or_mp_times * M) {
        size_t ram = SIZE_MAX;
        if (M) {
            std::array<size_t, 3> alloc_sizes = op.fti.get_alloc_sizes();
            ram = M->ram(alloc_sizes);
            if (ram > M->ram()) {
                int rank;
                MPI_Comm_rank(a.get_model().com[0], &rank);
                if (rank == 0)
                fprintf(stderr, "Transform size for %s with input operand sizes (%zu, %zu) is (%zu,%zu,%zu), which exceeds expected (%zu,%zu,%zu) (anticipated for operand sizes (%zu, %zu). Adjusting memory\n",
                        OP::name,
                        a.get_size(),
                        b.get_size(),
                        alloc_sizes[0],
                        alloc_sizes[1],
                        alloc_sizes[2],
                        M->fft_alloc_sizes[0],
                        M->fft_alloc_sizes[1],
                        M->fft_alloc_sizes[2],
                        M->asize,
                        M->bsize
                       );
                /* save it to the object. this means that we must pay
                 * attention to taking it by reference in the calling
                 * function.
                 */
                M->fft_alloc_sizes = alloc_sizes;
            }
        }
        try {
            typename matpoly_ft<fft_type>::memory_guard dummy(ram);
            mp_or_mul<OP_CTX<bigmatpoly, fft_type>, OP>(*this, op, M)();
        } catch (memory_pool_exception const & e) {
            int rank;
            MPI_Comm_rank(a.get_model().com[0], &rank);
            fprintf(stderr, "Rank %d raised a memory pool exception: %s\n", rank, e.what());
            typename matpoly_ft<fft_type>::memory_guard dummy(SIZE_MAX);
            mp_or_mul<OP_CTX<bigmatpoly, fft_type>, OP>(*this, op, M)();
        }
    }
};

template<typename fft_type>
void bigmatpoly_ft<fft_type>::mp_caching(tree_stats & stats, bigmatpoly & c, bigmatpoly const & a, bigmatpoly const & b, lingen_call_companion::mul_or_mp_times * M)
{
    op_mp<fft_type> op(a, b, UINT_MAX);
    OP_CTX<bigmatpoly, fft_type>(stats, op.fti, c, a, b).doit(op, M);
}
template<typename fft_type>
void bigmatpoly_ft<fft_type>::mul_caching(tree_stats & stats, bigmatpoly & c, bigmatpoly const & a, bigmatpoly const & b, lingen_call_companion::mul_or_mp_times * M)
{
    op_mul<fft_type> op(a, b, UINT_MAX);
    OP_CTX<bigmatpoly, fft_type>(stats, op.fti, c, a, b).doit(op, M);
}

#ifdef SELECT_MPFQ_LAYER_u64k1
template class bigmatpoly_ft<gf2x_fake_fft>;
template class bigmatpoly_ft<gf2x_cantor_fft>;
template class bigmatpoly_ft<gf2x_ternary_fft>;
#else
template class bigmatpoly_ft<fft_transform_info>;
#endif

