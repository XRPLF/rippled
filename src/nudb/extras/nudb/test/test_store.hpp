//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_TEST_TEST_STORE_HPP
#define NUDB_TEST_TEST_STORE_HPP

#include <nudb/util.hpp>
#include <nudb/test/temp_dir.hpp>
#include <nudb/test/xor_shift_engine.hpp>
#include <nudb/create.hpp>
#include <nudb/native_file.hpp>
#include <nudb/store.hpp>
#include <nudb/verify.hpp>
#include <nudb/xxhasher.hpp>
#include <iomanip>
#include <iostream>

namespace nudb {
namespace test {

template<class = void>
class Buffer_t
{
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::unique_ptr<std::uint8_t[]> p_;

public:
    Buffer_t() = default;

    Buffer_t(Buffer_t&& other);

    Buffer_t(Buffer_t const& other);

    Buffer_t& operator=(Buffer_t&& other);

    Buffer_t& operator=(Buffer_t const& other);

    bool
    empty() const
    {
        return size_ == 0;
    }

    std::size_t
    size() const
    {
        return size_;
    }

    std::uint8_t*
    data()
    {
        return p_.get();
    }

    std::uint8_t const*
    data() const
    {
        return p_.get();
    }

    void
    clear();

    void
    shrink_to_fit();

    std::uint8_t*
    resize(std::size_t size);

    std::uint8_t*
    operator()(void const* data, std::size_t size);
};

template<class _>
Buffer_t<_>::
Buffer_t(Buffer_t&& other)
    : size_(other.size_)
    , capacity_(other.capacity_)
    , p_(std::move(other.p_))
{
    other.size_ = 0;
    other.capacity_ = 0;
}

template<class _>
Buffer_t<_>::
Buffer_t(Buffer_t const& other)
{
    if(! other.empty())
        std::memcpy(resize(other.size()),
            other.data(), other.size());
}

template<class _>
auto
Buffer_t<_>::
operator=(Buffer_t&& other) ->
    Buffer_t&
{
    if(&other != this)
    {
        size_ = other.size_;
        capacity_ = other.capacity_;
        p_ = std::move(other.p_);
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

template<class _>
auto
Buffer_t<_>::
operator=(Buffer_t const& other) ->
    Buffer_t&
{
    if(&other != this)
    {
        if(other.empty())
            size_ = 0;
        else
            std::memcpy(resize(other.size()),
                other.data(), other.size());
    }
    return *this;
}

template<class _>
void
Buffer_t<_>::
clear()
{
    size_ = 0;
    capacity_ = 0;
    p_.reset();
}

template<class _>
void
Buffer_t<_>::
shrink_to_fit()
{
    if(empty() || size_ == capacity_)
        return;
    std::unique_ptr<std::uint8_t[]> p{
        new std::uint8_t[size_]};
    capacity_ = size_;
    std::memcpy(p.get(), p_.get(), size_);
    std::swap(p, p_);
}

template<class _>
std::uint8_t*
Buffer_t<_>::
resize(std::size_t size)
{
    if(capacity_ < size)
    {
        p_.reset(new std::uint8_t[size]);
        capacity_ = size;
    }
    size_ = size;
    return p_.get();
}

template<class _>
std::uint8_t*
Buffer_t<_>::
operator()(void const* data, std::size_t size)
{
    if(data == nullptr || size == 0)
        return resize(0);
    return reinterpret_cast<std::uint8_t*>(
        std::memcpy(resize(size), data, size));
}

using Buffer = Buffer_t<>;

//------------------------------------------------------------------------------

/// Describes a test generated key/value pair
struct item_type
{
    std::uint8_t* key;
    std::uint8_t* data;
    std::size_t size;
};

/// Interface to facilitate tests
template<class File>
class basic_test_store
{
    using Hasher = xxhasher;

    temp_dir td_;
    std::uniform_int_distribution<std::size_t> sizef_;
    std::function<void(error_code&)> createf_;
    std::function<void(error_code&)> openf_;
    Buffer buf_;

public:
    path_type const dp;
    path_type const kp;
    path_type const lp;
    std::size_t const keySize;
    std::size_t const blockSize;
    float const loadFactor;
    static std::uint64_t constexpr appnum = 1;
    static std::uint64_t constexpr salt = 42;
    basic_store<xxhasher, File> db;

    template<class... Args>
    basic_test_store(std::size_t keySize,
        std::size_t blockSize, float loadFactor,
            Args&&... args);

    template<class... Args>
    basic_test_store(
        boost::filesystem::path const& temp_dir,
        std::size_t keySize, std::size_t blockSize, float loadFactor,
        Args&&... args);

    ~basic_test_store();

    item_type
    operator[](std::uint64_t i);

    void
    create(error_code& ec);

    void
    open(error_code& ec);

    void
    close(error_code& ec)
    {
        db.close(ec);
    }

    void
    erase();

private:
    template<class Generator>
    static
    void
    rngfill(
        void* dest, std::size_t size, Generator& g);
};

template <class File>
template <class... Args>
basic_test_store<File>::basic_test_store(
    boost::filesystem::path const& temp_dir,
        std::size_t keySize_, std::size_t blockSize_,
            float loadFactor_, Args&&... args)
        : td_(temp_dir)
        , sizef_(250, 750)
        , createf_(
            [this, args...](error_code& ec)
            {
                nudb::create<Hasher, File>(
                    dp, kp, lp, appnum, salt,
                    keySize, blockSize, loadFactor, ec,
                    args...);
            })
        , openf_(
            [this, args...](error_code& ec)
            {
                db.open(dp, kp, lp, ec, args...);
            })
        , dp(td_.file("nudb.dat"))
        , kp(td_.file("nudb.key"))
        , lp(td_.file("nudb.log"))
        , keySize(keySize_)
        , blockSize(blockSize_)
        , loadFactor(loadFactor_)
{
}

template <class File>
template <class... Args>
basic_test_store<File>::basic_test_store(std::size_t keySize_,
    std::size_t blockSize_, float loadFactor_,
    Args&&... args)
    : basic_test_store(boost::filesystem::path{},
          keySize_,
          blockSize_,
          loadFactor_,
          std::forward<Args>(args)...)
{
}

template<class File>
basic_test_store<File>::
~basic_test_store()
{
    erase();
}

template<class File>
auto
basic_test_store<File>::
operator[](std::uint64_t i) ->
    item_type
{
    xor_shift_engine g{i + 1};
    item_type item;
    item.size = sizef_(g);
    auto const needed = keySize + item.size;
    rngfill(buf_.resize(needed), needed, g);
    // put key last so we can get some unaligned
    // keys, this increases coverage of xxhash.
    item.data = buf_.data();
    item.key = buf_.data() + item.size;
    return item;
}

template<class File>
void
basic_test_store<File>::
create(error_code& ec)
{
    createf_(ec);
}

template<class File>
void
basic_test_store<File>::
open(error_code& ec)
{
    openf_(ec);
    if(ec)
        return;
    if(db.key_size() != keySize)
        ec = error::invalid_key_size;
    else if(db.block_size() != blockSize)
        ec = error::invalid_block_size;
}

template<class File>
void
basic_test_store<File>::
erase()
{
    erase_file(dp);
    erase_file(kp);
    erase_file(lp);
}

template<class File>
template<class Generator>
void
basic_test_store<File>::
rngfill(
    void* dest, std::size_t size, Generator& g)
{
    using result_type =
        typename Generator::result_type;
    while(size >= sizeof(result_type))
    {
        auto const v = g();
        std::memcpy(dest, &v, sizeof(v));
        dest = reinterpret_cast<
            std::uint8_t*>(dest) + sizeof(v);
        size -= sizeof(v);
    }
    if(size > 0)
    {
        auto const v = g();
        std::memcpy(dest, &v, size);
    }
}

using test_store = basic_test_store<native_file>;

//------------------------------------------------------------------------------

template<class T>
static
std::string
num (T t)
{
    std::string s = std::to_string(t);
    std::reverse(s.begin(), s.end());
    std::string s2;
    s2.reserve(s.size() + (s.size()+2)/3);
    int n = 0;
    for (auto c : s)
    {
        if (n == 3)
        {
            n = 0;
            s2.insert (s2.begin(), ',');
        }
        ++n;
        s2.insert(s2.begin(), c);
    }
    return s2;
}

template<class = void>
std::ostream&
operator<<(std::ostream& os, verify_info const& info)
{
    os <<
        "avg_fetch:       " << std::fixed << std::setprecision(3) << info.avg_fetch << "\n" <<
        "waste:           " << std::fixed << std::setprecision(3) << info.waste * 100 << "%" << "\n" <<
        "overhead:        " << std::fixed << std::setprecision(1) << info.overhead * 100 << "%" << "\n" <<
        "actual_load:     " << std::fixed << std::setprecision(0) << info.actual_load * 100 << "%" << "\n" <<
        "version:         " << num(info.version) << "\n" <<
        "uid:             " << fhex(info.uid) << "\n" <<
        "appnum:          " << info.appnum << "\n" <<
        "key_size:        " << num(info.key_size) << "\n" <<
        "salt:            " << fhex(info.salt) << "\n" <<
        "pepper:          " << fhex(info.pepper) << "\n" <<
        "block_size:      " << num(info.block_size) << "\n" <<
        "bucket_size:     " << num(info.bucket_size) << "\n" <<
        "load_factor:     " << std::fixed << std::setprecision(0) << info.load_factor * 100 << "%" << "\n" <<
        "capacity:        " << num(info.capacity) << "\n" <<
        "buckets:         " << num(info.buckets) << "\n" <<
        "key_count:       " << num(info.key_count) << "\n" <<
        "value_count:     " << num(info.value_count) << "\n" <<
        "value_bytes:     " << num(info.value_bytes) << "\n" <<
        "spill_count:     " << num(info.spill_count) << "\n" <<
        "spill_count_tot: " << num(info.spill_count_tot) << "\n" <<
        "spill_bytes:     " << num(info.spill_bytes) << "\n" <<
        "spill_bytes_tot: " << num(info.spill_bytes_tot) << "\n" <<
        "key_file_size:   " << num(info.key_file_size) << "\n" <<
        "dat_file_size:   " << num(info.dat_file_size) << std::endl;

    std::string s;
    for (size_t i = 0; i < info.hist.size(); ++i)
        s += (i==0) ?
            std::to_string(info.hist[i]) :
            (", " + std::to_string(info.hist[i]));
    os << "hist:            " << s << std::endl;
    return os;
}

} // test
} // nudb

#endif

