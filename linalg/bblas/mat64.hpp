#ifndef BBLAS_MAT64_HPP_
#define BBLAS_MAT64_HPP_

#include <cstdint>
#include <cstring>

namespace bblas_bitmat_details {

    template<typename T> struct bblas_bitmat_type_supported {
        static constexpr const bool value = false;
    };
    template<> struct bblas_bitmat_type_supported<uint8_t> {
        static constexpr const bool value = true;
        static constexpr const int width = 8;
    };
    template<> struct bblas_bitmat_type_supported<uint16_t> {
        static constexpr const bool value = true;
        static constexpr const int width = 16;
    };
    template<> struct bblas_bitmat_type_supported<uint32_t> {
        static constexpr const bool value = true;
        static constexpr const int width = 32;
    };
    template<> struct bblas_bitmat_type_supported<uint64_t> {
        static constexpr const bool value = true;
        static constexpr const int width = 64;
    };
}


template<typename T>
class bitmat
{
    typedef bblas_bitmat_details::bblas_bitmat_type_supported<T> S;
    static_assert(S::value, "bblas bitmap must be built on uintX_t");

    public:
    static constexpr const int width = S::width;
    typedef T datatype;

    private:
    T x[width];

    public:
    inline T* data() { return x; }
    inline const T* data() const { return x; }
    T& operator[](int i) { return x[i]; }
    T operator[](int i) const { return x[i]; }
    inline bool operator==(bitmat const& a) const
    {
        return memcmp(x, a.x, sizeof(x)) == 0;
    }
    bitmat() {}
    inline bitmat(bitmat const& a) { memcpy(x, a.x, sizeof(x)); }
    inline bitmat& operator=(bitmat const& a)
    {
        memcpy(x, a.x, sizeof(x));
        return *this;
    }
    inline bitmat(int a) { *this = a; }
    inline bitmat& operator=(int a)
    {
        if (a & 1) {
            T mask = 1;
            for (int j = 0; j < width; j++, mask <<= 1)
                x[j] = mask;
        } else {
            memset(x, 0, sizeof(x));
        }
        return *this;
    }
};

typedef bitmat<uint64_t> mat64;
// ATTRIBUTE((aligned(64)); somewhere

#endif	/* BBLAS_MAT64_HPP_ */
