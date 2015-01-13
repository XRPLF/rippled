//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <beast/hash/tests/hash_metrics.h>
#include <beast/hash/hash_append.h>
#include <beast/chrono/chrono_io.h>
#include <beast/unit_test/suite.h>
#include <beast/utility/type_name.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <random>

namespace beast {

template <class Block, class Derived>
class block_stream
{
private:
    Block m_block;
    std::size_t m_size;

    std::size_t
    needed() const noexcept
    {
        return sizeof(Block) - m_size;
    }

    void*
    tail() noexcept
    {
        return ((char *)&m_block) + m_size;
    }

protected:
    void
    finish()
    {
        if (m_size > 0)
        {
            // zero-pad
            memset (tail(), 0, needed());
            static_cast <Derived*> (this)->process_block (m_block);
        }
    }

public:
    block_stream ()
        : m_size(0)
    {
    }

    void
    operator() (void const* data, std::size_t bytes) noexcept
    {
        // handle leftovers
        if (m_size > 0)
        {
            std::size_t const n (std::min (needed(), bytes));
            std::memcpy (tail(), data, n);
            data = ((char const*)data) + n;
            bytes -= n;
            m_size += n;

            if (m_size < sizeof(Block))
                return;

            static_cast <Derived*> (this)->process_block (m_block);
        }

        // loop over complete blocks
        while (bytes >= sizeof(Block))
        {
            m_block = *((Block const*)data);
            static_cast <Derived*> (this)->process_block (m_block);
            data = ((char const*)data) + sizeof(m_block);
            bytes -= sizeof(m_block);
        }

        // save leftovers
        if (bytes > 0)
        {
            memcpy (tail(), data, bytes);
            m_size += bytes;
        }
    }
};

//------------------------------------------------------------------------------

namespace hash_append_tests {

template <std::size_t> class fnv1a_imp;

template <>
class fnv1a_imp<64>
{
private:
    std::uint64_t state_ = 14695981039346656037u;

public:
    void
    append (void const* key, std::size_t len) noexcept
    {
        unsigned char const* p = static_cast<unsigned char const*>(key);
        unsigned char const* const e = p + len;
        for (; p < e; ++p)
            state_ = (state_ ^ *p) * 1099511628211u;
    }

    explicit
    operator std::size_t() noexcept
    {
        return state_;
    }
};

template <>
class fnv1a_imp<32>
{
private:
    std::uint32_t state_ = 2166136261;

public:
    void
    append (void const* key, std::size_t len) noexcept
    {
        unsigned char const* p = static_cast<unsigned char const*>(key);
        unsigned char const* const e = p + len;
        for (; p < e; ++p)
            state_ = (state_ ^ *p) * 16777619;
    }

    explicit
    operator std::size_t() noexcept
    {
        return state_;
    }
};

class fnv1a
    : public fnv1a_imp<CHAR_BIT*sizeof(std::size_t)>
{
public:
};

class jenkins1
{
private:
    std::size_t state_ = 0;

public:
    void
    append (void const* key, std::size_t len) noexcept
    {
        unsigned char const* p = static_cast <unsigned char const*>(key);
        unsigned char const* const e = p + len;
        for (; p < e; ++p)
        {
            state_ += *p;
            state_ += state_ << 10;
            state_ ^= state_ >> 6;
        }
    }

    explicit
    operator std::size_t() noexcept
    {
        state_ += state_ << 3;
        state_ ^= state_ >> 11;
        state_ += state_ << 15;
        return state_;
    }
};

class spooky
{
private:
    SpookyHash state_;

public: 
    spooky(std::size_t seed1 = 1, std::size_t seed2 = 2) noexcept
    {
        state_.Init(seed1, seed2);
    }

    void
    append(void const* key, std::size_t len) noexcept
    {
        state_.Update(key, len);
    }

    explicit
    operator std::size_t() noexcept
    {
        std::uint64_t h1, h2;
        state_.Final(&h1, &h2);
        return h1;
    }

};

template <
    class PRNG = std::conditional_t <
        sizeof(std::size_t)==sizeof(std::uint64_t),
        std::mt19937_64,
        std::mt19937
    >
>
class prng_hasher
    : public block_stream <std::size_t, prng_hasher <PRNG>>
{
private:
    std::size_t m_seed;
    PRNG m_prng;

    typedef block_stream <std::size_t, prng_hasher <PRNG>> base;
    friend base;

    // compress
    void
    process_block (std::size_t block)
    {
        m_prng.seed (m_seed + block);
        m_seed = m_prng();
    }

public:
    prng_hasher (std::size_t seed = 0)
        : m_seed (seed)
    {
    }

    void
    append (void const* data, std::size_t bytes) noexcept
    {
        base::operator() (data, bytes);
    }

    explicit
    operator std::size_t() noexcept
    {
        base::finish();
        return m_seed;
    }
};

class SlowKey
{
private:
    std::tuple <short, unsigned char, unsigned char> date_;
    std::vector <std::pair <int, int>> data_;

public:
    SlowKey()
    {
        static std::mt19937_64 eng;
        std::uniform_int_distribution<short> yeardata(1900, 2014);
        std::uniform_int_distribution<unsigned> monthdata(1, 12);
        std::uniform_int_distribution<unsigned> daydata(1, 28);
        std::uniform_int_distribution<std::size_t> veclen(0, 100);
        std::uniform_int_distribution<int> int1data(1, 10);
        std::uniform_int_distribution<int> int2data(-3, 5000);
        std::get<0>(date_) = yeardata(eng);
        std::get<1>(date_) = (unsigned char)monthdata(eng);
        std::get<2>(date_) = (unsigned char)daydata(eng);
        data_.resize(veclen(eng));
        for (auto& p : data_)
        {
            p.first = int1data(eng);
            p.second = int2data(eng);
        }
    }

    // Hook into the system like this
    template <class Hasher>
    friend
    void
    hash_append (Hasher& h, SlowKey const& x) noexcept
    {
        using beast::hash_append;
        hash_append (h, x.date_, x.data_);
    }

    friend
    bool operator< (SlowKey const& x, SlowKey const& y) noexcept
    {
        return std::tie(x.date_, x.data_) < std::tie(y.date_, y.data_);
    }

    // Hook into the std::system like this
    friend struct std::hash<SlowKey>;
    friend struct X_fnv1a;
};

struct FastKey
{
private:
    std::array <std::size_t, 4> m_values;

public:
    FastKey()
    {
        static std::conditional_t <sizeof(std::size_t)==sizeof(std::uint64_t),
            std::mt19937_64, std::mt19937> eng;
        for (auto& v : m_values)
            v = eng();
    }

    friend
    bool
    operator< (FastKey const& x, FastKey const& y) noexcept
    {
        return x.m_values < y.m_values;
    }
};

} // hash_append_tests

//------------------------------------------------------------------------------

template<>
struct is_contiguously_hashable <hash_append_tests::FastKey>
    : std::true_type
{
};

//------------------------------------------------------------------------------

class hash_append_test : public unit_test::suite
{
public:
    typedef hash_append_tests::SlowKey SlowKey;
    typedef hash_append_tests::FastKey FastKey;

    struct results_t
    {
        results_t()
            : collision_factor (0)
            , distribution_factor (0)
            , elapsed (0)
        {
        }

        float collision_factor;
        float distribution_factor;
        float windowed_score;
        std::chrono::milliseconds elapsed;
    };

    // Generate a set of keys
    template <class Key>
    std::set <Key>
    make_keys (std::size_t count)
    {
        std::set <Key> keys;
        while (count--)
            keys.emplace();
        return keys;
    }

    // Generate a set of hashes from a container
    template <class Hasher, class Keys>
    std::vector <std::size_t>
    make_hashes (Keys const& keys)
    {
        std::vector <std::size_t> hashes;
        hashes.reserve (keys.size());
        for (auto const& key : keys)
        {
            Hasher h;
            hash_append (h, key);
            hashes.push_back (static_cast <std::size_t> (h));
        }
        return hashes;
    }

    template <class Hasher, class Hashes>
    void
    measure_hashes (results_t& results, Hashes const& hashes)
    {
        results.collision_factor =
            hash_metrics::collision_factor (
                hashes.begin(), hashes.end());

        results.distribution_factor =
            hash_metrics::distribution_factor (
                hashes.begin(), hashes.end());

        results.windowed_score =
            hash_metrics::windowed_score (
                hashes.begin(), hashes.end());
    }

    template <class Hasher, class Keys>
    void
    measure_keys (results_t& results, Keys const& keys)
    {
        auto const start (
            std::chrono::high_resolution_clock::now());
        
        auto const hashes (make_hashes <Hasher> (keys));
        
        results.elapsed = std::chrono::duration_cast <std::chrono::milliseconds> (
            std::chrono::high_resolution_clock::now() - start);

        measure_hashes <Hasher> (results, hashes);
    }

    template <class Hasher, class Key>
    void
    test_hasher (std::string const& name, std::size_t n)
    {
        results_t results;
        auto const keys (make_keys <Key> (n));
        measure_keys <Hasher> (results, keys);
        report (name, results);
    }

    void
    report (std::string const& name, results_t const& results)
    {
        log <<
            std::left <<
            std::setw (39) << name << " | " <<
            std::right <<
            std::setw (13) << std::setprecision (5) <<
                results.collision_factor << " | " <<
            std::setw (13) << std::setprecision (5) <<
                results.distribution_factor << " | " <<
            std::setw (13) << std::setprecision (5) <<
                results.windowed_score << " | " <<
            std::left <<
            results.elapsed.count();
        pass ();
    }

    void
    run()
    {
        log <<
            "name                                    |     collision |  distribution |   windowed    | time (milliseconds)" << std::endl <<
            "----------------------------------------+---------------+---------------+---------------+--------------------";

        //test_hasher <hash_append_tests::prng_hasher<>, SlowKey> ("prng_hasher <SlowKey>", 10000);
        //test_hasher <hash_append_tests::prng_hasher<>, FastKey> ("prng_hasher <FastKey>", 100000);

        test_hasher <hash_append_tests::jenkins1, SlowKey>      ("jenkins1 <SlowKey>",  1000000);
        test_hasher <hash_append_tests::spooky, SlowKey>        ("spooky <SlowKey>",    1000000);
        test_hasher <hash_append_tests::fnv1a, SlowKey>         ("fnv1a <SlowKey>",     1000000);

        test_hasher <hash_append_tests::jenkins1, FastKey>      ("jenkins1 <FastKey>",  1000000);
        test_hasher <hash_append_tests::spooky, FastKey>        ("spooky <FastKey>",    1000000);
        test_hasher <hash_append_tests::fnv1a, FastKey>         ("fnv1a <FastKey>",     1000000);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(hash_append,container,beast);

}
