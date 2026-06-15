#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/unit_test/suite.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace xrpl::detail {

template <class T>
class TestUserTypeMember
{
private:
    T t_;

public:
    explicit TestUserTypeMember(T const& t = T()) : t_(t)
    {
    }

    template <class Hasher>
    friend void
    // NOLINTNEXTLINE(readability-identifier-naming)
    hash_append(Hasher& h, TestUserTypeMember const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.t_);
    }
};

template <class T>
class TestUserTypeFree
{
private:
    T t_;

public:
    explicit TestUserTypeFree(T const& t = T()) : t_(t)
    {
    }

    template <class Hasher>
    friend void
    // NOLINTNEXTLINE(readability-identifier-naming)
    hash_append(Hasher& h, TestUserTypeFree const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.t_);
    }
};

}  // namespace xrpl::detail

//------------------------------------------------------------------------------

namespace xrpl {

namespace detail {

template <class T>
using test_hardened_unordered_set = std::unordered_set<T, HardenedHash<>>;

template <class T>
using test_hardened_unordered_map = std::unordered_map<T, int, HardenedHash<>>;

template <class T>
using test_hardened_unordered_multiset = std::unordered_multiset<T, HardenedHash<>>;

template <class T>
using test_hardened_unordered_multimap = std::unordered_multimap<T, int, HardenedHash<>>;

}  // namespace detail

template <std::size_t Bits, class UInt = std::uint64_t>
class UnsignedInteger
{
private:
    static_assert(
        std::is_integral_v<UInt> && std::is_unsigned_v<UInt>,
        "UInt must be an unsigned integral type");

    static_assert(Bits % (8 * sizeof(UInt)) == 0, "Bits must be a multiple of 8*sizeof(UInt)");

    static_assert(Bits >= (8 * sizeof(UInt)), "Bits must be at least 8*sizeof(UInt)");

    static std::size_t const kSize = Bits / (8 * sizeof(UInt));

    std::array<UInt, kSize> vec_;

public:
    using value_type = UInt;

    static std::size_t const kBits = Bits;
    static std::size_t const kBytes = kBits / 8;

    template <class Int>
    static UnsignedInteger
    fronumber(Int v)
    {
        UnsignedInteger result;
        for (std::size_t i(1); i < kSize; ++i)
            result.vec_[i] = 0;
        result.vec_[0] = v;
        return result;
    }

    void*
    data() noexcept
    {
        return &vec_[0];
    }

    [[nodiscard]] void const*
    data() const noexcept
    {
        return &vec_[0];
    }

    template <class Hasher>
    friend void
    hash_append(Hasher& h, UnsignedInteger const& a) noexcept
    {
        using beast::hash_append;
        hash_append(h, a.vec_);
    }

    friend std::ostream&
    operator<<(std::ostream& s, UnsignedInteger const& v)
    {
        for (std::size_t i(0); i < kSize; ++i)
            s << std::hex << std::setfill('0') << std::setw(2 * sizeof(UInt)) << v.vec_[i];
        return s;
    }
};

using sha256_t = UnsignedInteger<256, std::size_t>;

#ifndef __INTELLISENSE__
static_assert(sha256_t::kBits == 256, "sha256_t must have 256 bits");
#endif

}  // namespace xrpl

//------------------------------------------------------------------------------

namespace xrpl {

class hardened_hash_test : public beast::unit_test::Suite
{
public:
    template <class T>
    void
    check()
    {
        T t{};
        HardenedHash<>()(t);
        pass();
    }

    template <template <class T> class U>
    void
    checkUserType()
    {
        check<U<bool>>();
        check<U<char>>();
        check<U<signed char>>();
        check<U<unsigned char>>();
        // These cause trouble for boost
        // check <U <char16_t>> ();
        // check <U <char32_t>> ();
        check<U<wchar_t>>();
        check<U<short>>();
        check<U<unsigned short>>();
        check<U<int>>();
        check<U<unsigned int>>();
        check<U<long>>();
        check<U<long long>>();
        check<U<unsigned long>>();
        check<U<unsigned long long>>();
        check<U<float>>();
        check<U<double>>();
        check<U<long double>>();
    }

    template <template <class T> class C>
    void
    checkContainer()
    {
        {
            C<detail::TestUserTypeMember<std::string>> const c;
        }

        pass();

        {
            C<detail::TestUserTypeFree<std::string>> const c;
        }

        pass();
    }

    void
    testUserTypes()
    {
        testcase("user types");
        checkUserType<detail::TestUserTypeMember>();
        checkUserType<detail::TestUserTypeFree>();
    }

    void
    testContainers()
    {
        testcase("containers");
        checkContainer<detail::test_hardened_unordered_set>();
        checkContainer<detail::test_hardened_unordered_map>();
        checkContainer<detail::test_hardened_unordered_multiset>();
        checkContainer<detail::test_hardened_unordered_multimap>();
    }

    void
    run() override
    {
        testUserTypes();
        testContainers();
    }
};

BEAST_DEFINE_TESTSUITE(hardened_hash, basics, xrpl);

}  // namespace xrpl
