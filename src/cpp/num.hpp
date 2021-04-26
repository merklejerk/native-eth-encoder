#include <boost/multiprecision/cpp_int.hpp>

template <unsigned TBits>
using bigint_t = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<
        TBits,
        TBits,
        boost::multiprecision::signed_magnitude,
        boost::multiprecision::unchecked,
        void
    >
>;

template <unsigned TBits>
using biguint_t = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<
        TBits,
        TBits,
        boost::multiprecision::unsigned_magnitude,
        boost::multiprecision::unchecked,
        void
    >
>;

typedef biguint_t<256> uint256_t;
typedef biguint_t<128> uint128_t;
typedef biguint_t<112> uint112_t;

typedef bigint_t<256> int256_t;
typedef bigint_t<128> int128_t;
typedef bigint_t<112> int112_t;
