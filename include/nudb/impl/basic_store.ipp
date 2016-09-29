//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_BASIC_STORE_IPP
#define NUDB_IMPL_BASIC_STORE_IPP

#include <nudb/concepts.hpp>
#include <nudb/recover.hpp>
#include <boost/assert.hpp>
#include <cmath>
#include <memory>

#ifndef NUDB_DEBUG_LOG
#define NUDB_DEBUG_LOG 0
#endif
#if NUDB_DEBUG_LOG
#include <beast/unit_test/dstream.hpp>
#include <iostream>
#endif

namespace nudb {

template<class Hasher, class File>
basic_store<Hasher, File>::state::
state(File&& df_, File&& kf_, File&& lf_,
    path_type const& dp_, path_type const& kp_,
        path_type const& lp_,
            detail::key_file_header const& kh_)
    : df(std::move(df_))
    , kf(std::move(kf_))
    , lf(std::move(lf_))
    , dp(dp_)
    , kp(kp_)
    , lp(lp_)
    , hasher(kh_.salt)
    , p0(kh_.key_size, "p0")
    , p1(kh_.key_size, "p1")
    , c1(kh_.key_size, kh_.block_size, "c1")
    , kh(kh_)
{
    static_assert(is_File<File>::value,
        "File requirements not met");
}

//------------------------------------------------------------------------------

template<class Hasher, class File>
basic_store<Hasher, File>::
~basic_store()
{
    error_code ec;
    // We call close here to make sure data is intact
    // if an exception destroys the basic_store, but callers
    // should always call close manually to receive the
    // error code.
    close(ec);
}

template<class Hasher, class File>
path_type const&
basic_store<Hasher, File>::
dat_path() const
{
    BOOST_ASSERT(is_open());
    return s_->dp;
}

template<class Hasher, class File>
path_type const&
basic_store<Hasher, File>::
key_path() const
{
    BOOST_ASSERT(is_open());
    return s_->kp;
}

template<class Hasher, class File>
path_type const&
basic_store<Hasher, File>::
log_path() const
{
    BOOST_ASSERT(is_open());
    return s_->lp;
}

template<class Hasher, class File>
std::uint64_t
basic_store<Hasher, File>::
appnum() const
{
    BOOST_ASSERT(is_open());
    return s_->kh.appnum;
}

template<class Hasher, class File>
std::size_t
basic_store<Hasher, File>::
key_size() const
{
    BOOST_ASSERT(is_open());
    return s_->kh.key_size;
}

template<class Hasher, class File>
std::size_t
basic_store<Hasher, File>::
block_size() const
{
    BOOST_ASSERT(is_open());
    return s_->kh.block_size;
}

template<class Hasher, class File>
template<class... Args>
void
basic_store<Hasher, File>::
open(
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    error_code& ec,
    Args&&... args)
{
    static_assert(is_Hasher<Hasher>::value,
        "Hasher requirements not met");
    using namespace detail;
    BOOST_ASSERT(! is_open());
    ec_ = {};
    ecb_.store(false);
    recover<Hasher, File>(
        dat_path, key_path, log_path, ec, args...);
    if(ec)
        return;
    File df(args...);
    File kf(args...);
    File lf(args...);
    df.open(file_mode::append, dat_path, ec);
    if(ec)
        return;
    kf.open(file_mode::write, key_path, ec);
    if(ec)
        return;
    lf.create(file_mode::append, log_path, ec);
    if(ec)
        return;
    // VFALCO TODO Erase empty log file if this
    //             function subsequently fails.
    dat_file_header dh;
    read(df, dh, ec);
    if(ec)
        return;
    verify(dh, ec);
    if(ec)
        return;
    key_file_header kh;
    read(kf, kh, ec);
    if(ec)
        return;
    verify<Hasher>(kh, ec);
    if(ec)
        return;
    verify<Hasher>(dh, kh, ec);
    if(ec)
        return;
    boost::optional<state> s;
    s.emplace(std::move(df), std::move(kf), std::move(lf),
        dat_path, key_path, log_path, kh);
    thresh_ = std::max<std::size_t>(65536UL,
        kh.load_factor * kh.capacity);
    frac_ = thresh_ / 2;
    buckets_ = kh.buckets;
    modulus_ = ceil_pow2(kh.buckets);
    // VFALCO TODO This could be better
    if(buckets_ < 1)
    {
        ec = error::short_key_file;
        return;
    }
    dataWriteSize_ = 32 * nudb::block_size(dat_path);
    logWriteSize_ = 32 * nudb::block_size(log_path);
    s_.emplace(std::move(*s));
    open_ = true;
    t_ = std::thread(&basic_store::run, this);
}

template<class Hasher, class File>
void
basic_store<Hasher, File>::
close(error_code& ec)
{
    if(open_)
    {
        open_ = false;
        cv_.notify_all();
        t_.join();
        if(ecb_)
        {
            ec = ec_;
            return;
        }
        s_->lf.close();
        state s{std::move(*s_)};
        File::erase(s.lp, ec_);
        if(ec_)
            ec = ec_;
    }
}

template<class Hasher, class File>
template<class Callback>
void
basic_store<Hasher, File>::
fetch(
    void const* key,
    Callback && callback,
    error_code& ec)
{
    using namespace detail;
    BOOST_ASSERT(is_open());
    if(ecb_)
    {
        ec = ec_;
        return;
    }
    auto const h =
        hash(key, s_->kh.key_size, s_->hasher);
    shared_lock_type m{m_};
    {
        auto iter = s_->p1.find(key);
        if(iter == s_->p1.end())
        {
            iter = s_->p0.find(key);
            if(iter == s_->p0.end())
                goto cont;
        }
        callback(iter->first.data, iter->first.size);
        return;
    }
cont:
    auto const n = bucket_index(h, buckets_, modulus_);
    auto const iter = s_->c1.find(n);
    if(iter != s_->c1.end())
        return fetch(h, key, iter->second, callback, ec);
    genlock<gentex> g{g_};
    m.unlock();
    buffer buf{s_->kh.block_size};
    // b constructs from uninitialized buf
    bucket b{s_->kh.block_size, buf.get()};
    b.read(s_->kf, (n + 1) * b.block_size(), ec);
    if(ec)
        return;
    fetch(h, key, b, callback, ec);
}

template<class Hasher, class File>
void
basic_store<Hasher, File>::
insert(
    void const* key,
    void const* data,
    nsize_t size,
    error_code& ec)
{
    using namespace detail;
    using namespace std::chrono;
    BOOST_ASSERT(is_open());
    if(ecb_)
    {
        ec = ec_;
        return;
    }
    // Data Record
    BOOST_ASSERT(size > 0);                     // zero disallowed
    BOOST_ASSERT(size <= field<uint32_t>::max); // too large
    auto const h =
        hash(key, s_->kh.key_size, s_->hasher);
    std::lock_guard<std::mutex> u{u_};
    {
        shared_lock_type m{m_};
        if(s_->p1.find(key) != s_->p1.end() ||
           s_->p0.find(key) != s_->p0.end())
        {
            ec = error::key_exists;
            return;
        }
        auto const n = bucket_index(h, buckets_, modulus_);
        auto const iter = s_->c1.find(n);
        if(iter != s_->c1.end())
        {
            auto const found = exists(
                h, key, &m, iter->second, ec);
            if(ec)
                return;
            if(found)
            {
                ec = error::key_exists;
                return;
            }
            // m is now unlocked
        }
        else
        {
            // VFALCO Audit for concurrency
            genlock<gentex> g{g_};
            m.unlock();
            buffer buf;
            buf.reserve(s_->kh.block_size);
            bucket b{s_->kh.block_size, buf.get()};
            b.read(s_->kf,
                   static_cast<noff_t>(n + 1) * s_->kh.block_size, ec);
            if(ec)
                return;
            auto const found = exists(h, key, nullptr, b, ec);
            if(ec)
                return;
            if(found)
            {
                ec = error::key_exists;
                return;
            }
        }
    }
    // Perform insert
    unique_lock_type m{m_};
    s_->p1.insert(h, key, data, size);
    auto const now = clock_type::now();
    auto const elapsed = duration_cast<duration<float>>(
        now > s_->when ? now - s_->when : clock_type::duration{1});
    auto const work = s_->p1.data_size() +
        3 * s_->p1.size() * s_->kh.block_size;
    auto const rate = static_cast<std::size_t>(
        std::ceil(work / elapsed.count()));
    auto const sleep =
        s_->rate && rate > s_->rate;
    m.unlock();
    if(sleep)
        std::this_thread::sleep_for(milliseconds{25});
}

// Fetch key in loaded bucket b or its spills.
//
template<class Hasher, class File>
template<class Callback>
void
basic_store<Hasher, File>::
fetch(
    detail::nhash_t h,
    void const* key,
    detail::bucket b,
    Callback&& callback,
    error_code& ec)
{
    using namespace detail;
    buffer buf0;
    buffer buf1;
    for(;;)
    {
        for(auto i = b.lower_bound(h); i < b.size(); ++i)
        {
            auto const item = b[i];
            if(item.hash != h)
                break;
            // Data Record
            auto const len =
                s_->kh.key_size +       // Key
                item.size;              // Value
            buf0.reserve(len);
            s_->df.read(item.offset +
                field<uint48_t>::size,  // Size
                    buf0.get(), len, ec);
            if(ec)
                return;
            if(std::memcmp(buf0.get(), key,
                s_->kh.key_size) == 0)
            {
                callback(
                    buf0.get() + s_->kh.key_size, item.size);
                return;
            }
        }
        auto const spill = b.spill();
        if(! spill)
            break;
        buf1.reserve(s_->kh.block_size);
        b = bucket(s_->kh.block_size,
            buf1.get());
        b.read(s_->df, spill, ec);
        if(ec)
            return;
    }
    ec = error::key_not_found;
}

// Returns `true` if the key exists
// lock is unlocked after the first bucket processed
//
template<class Hasher, class File>
bool
basic_store<Hasher, File>::
exists(
    detail::nhash_t h,
    void const* key,
    detail::shared_lock_type* lock,
    detail::bucket b,
    error_code& ec)
{
    using namespace detail;
    buffer buf{s_->kh.key_size + s_->kh.block_size};
    void* pk = buf.get();
    void* pb = buf.get() + s_->kh.key_size;
    for(;;)
    {
        for(auto i = b.lower_bound(h); i < b.size(); ++i)
        {
            auto const item = b[i];
            if(item.hash != h)
                break;
            // Data Record
            s_->df.read(item.offset +
                field<uint48_t>::size,      // Size
                pk, s_->kh.key_size, ec);       // Key
            if(ec)
                return false;
            if(std::memcmp(pk, key, s_->kh.key_size) == 0)
                return true;
        }
        auto spill = b.spill();
        if(lock && lock->owns_lock())
            lock->unlock();
        if(! spill)
            break;
        b = bucket(s_->kh.block_size, pb);
        b.read(s_->df, spill, ec);
        if(ec)
            return false;
    }
    return false;
}

//  Split the bucket in b1 to b2
//  b1 must be loaded
//  tmp is used as a temporary buffer
//  splits are written but not the new buckets
//
template<class Hasher, class File>
void
basic_store<Hasher, File>::
split(
    detail::bucket& b1,
    detail::bucket& b2,
    detail::bucket& tmp,
    nbuck_t n1,
    nbuck_t n2,
    nbuck_t buckets,
    nbuck_t modulus,
    detail::bulk_writer<File>& w,
    error_code& ec)
{
    using namespace detail;
    // Trivial case: split empty bucket
    if(b1.empty())
        return;
    // Split
    for(std::size_t i = 0; i < b1.size();)
    {
        auto const e = b1[i];
        auto const n = bucket_index(e.hash, buckets, modulus);
        (void)n1;
        (void)n2;
        BOOST_ASSERT(n==n1 || n==n2);
        if(n == n2)
        {
            b2.insert(e.offset, e.size, e.hash);
            b1.erase(i);
        }
        else
        {
            ++i;
        }
    }
    noff_t spill = b1.spill();
    if(spill)
    {
        b1.spill(0);
        do
        {
            // If any part of the spill record is
            // in the write buffer then flush first
            if(spill + bucket_size(s_->kh.capacity) >
               w.offset() - w.size())
            {
                w.flush(ec);
                if(ec)
                    return;
            }
            tmp.read(s_->df, spill, ec);
            if(ec)
                return;
            for(std::size_t i = 0; i < tmp.size(); ++i)
            {
                auto const e = tmp[i];
                auto const n = bucket_index(
                    e.hash, buckets, modulus);
                BOOST_ASSERT(n==n1 || n==n2);
                if(n == n2)
                {
                    maybe_spill(b2, w, ec);
                    if(ec)
                        return;
                    b2.insert(e.offset, e.size, e.hash);
                }
                else
                {
                    maybe_spill(b1, w, ec);
                    if(ec)
                        return;
                    b1.insert(e.offset, e.size, e.hash);
                }
            }
            spill = tmp.spill();
        }
        while(spill);
    }
}

template<class Hasher, class File>
detail::bucket
basic_store<Hasher, File>::
load(
    nbuck_t n,
    detail::cache& c1,
    detail::cache& c0,
    void* buf,
    error_code& ec)
{
    using namespace detail;
    auto iter = c1.find(n);
    if(iter != c1.end())
        return iter->second;
    iter = c0.find(n);
    if(iter != c0.end())
        return c1.insert(n, iter->second)->second;
    bucket tmp{s_->kh.block_size, buf};
    tmp.read(s_->kf,
             static_cast<noff_t>(n + 1) * s_->kh.block_size, ec);
    if(ec)
        return {};
    c0.insert(n, tmp);
    return c1.insert(n, tmp)->second;
}

template<class Hasher, class File>
void
basic_store<Hasher, File>::
commit(detail::unique_lock_type& m,
    std::size_t& work, error_code& ec)
{
    using namespace detail;
    BOOST_ASSERT(m.owns_lock());
    BOOST_ASSERT(! s_->p1.empty());
    swap(s_->p0, s_->p1);
    m.unlock();
    work = s_->p0.data_size();
    cache c0(s_->kh.key_size, s_->kh.block_size, "c0");
    cache c1(s_->kh.key_size, s_->kh.block_size, "c1");
    // 0.63212 ~= 1 - 1/e
    {
        auto const size = static_cast<std::size_t>(
            std::ceil(0.63212 * s_->p0.size()));
        c0.reserve(size);
        c1.reserve(size);
    }
    buffer buf1{s_->kh.block_size};
    buffer buf2{s_->kh.block_size};
    bucket tmp{s_->kh.block_size, buf1.get()};
    // Prepare rollback information
    log_file_header lh;
    lh.version = currentVersion;            // Version
    lh.uid = s_->kh.uid;                    // UID
    lh.appnum = s_->kh.appnum;              // Appnum
    lh.key_size = s_->kh.key_size;          // Key Size
    lh.salt = s_->kh.salt;                  // Salt
    lh.pepper = pepper<Hasher>(lh.salt);    // Pepper
    lh.block_size = s_->kh.block_size;      // Block Size
    lh.key_file_size = s_->kf.size(ec);     // Key File Size
    if(ec)
        return;
    lh.dat_file_size = s_->df.size(ec);     // Data File Size
    if(ec)
        return;
    write(s_->lf, lh, ec);
    if(ec)
        return;
    // Checkpoint
    s_->lf.sync(ec);
    if(ec)
        return;
    // Append data and spills to data file
    auto modulus = modulus_;
    auto buckets = buckets_;
    {
        // Bulk write to avoid write amplification
        auto const size = s_->df.size(ec);
        if(ec)
            return;
        bulk_writer<File> w{s_->df, size, dataWriteSize_};
        // Write inserted data to the data file
        for(auto& e : s_->p0)
        {
            // VFALCO This could be UB since other
            // threads are reading other data members
            // of this object in memory
            e.second = w.offset();
            auto os = w.prepare(value_size(
                e.first.size, s_->kh.key_size), ec);
            if(ec)
                return;
            // Data Record
            write<uint48_t>(os, e.first.size);          // Size
            write(os, e.first.key, s_->kh.key_size);    // Key
            write(os, e.first.data, e.first.size);      // Data
        }
        // Do inserts, splits, and build view
        // of original and modified buckets
        for(auto const e : s_->p0)
        {
            // VFALCO Should this be >= or > ?
            if((frac_ += 65536) >= thresh_)
            {
                // split
                frac_ -= thresh_;
                if(buckets == modulus)
                    modulus *= 2;
                auto const n1 = buckets - (modulus / 2);
                auto const n2 = buckets++;
                auto b1 = load(n1, c1, c0, buf2.get(), ec);
                if(ec)
                    return;
                auto b2 = c1.create(n2);
                // If split spills, the writer is
                // flushed which can amplify writes.
                split(b1, b2, tmp, n1, n2,
                    buckets, modulus, w, ec);
                if(ec)
                    return;
            }
            // Insert
            auto const n = bucket_index(
                e.first.hash, buckets, modulus);
            auto b = load(n, c1, c0, buf2.get(), ec);
            if(ec)
                return;
            // This can amplify writes if it spills.
            maybe_spill(b, w, ec);
            if(ec)
                return;
            b.insert(e.second, e.first.size, e.first.hash);
        }
        w.flush(ec);
        if(ec)
            return;
    }
    work += s_->kh.block_size * (2 * c0.size() + c1.size());
    // Give readers a view of the new buckets.
    // This might be slightly better than the old
    // view since there could be fewer spills.
    m.lock();
    swap(c1, s_->c1);
    s_->p0.clear();
    buckets_ = buckets;
    modulus_ = modulus;
    g_.start();
    m.unlock();
    // Write clean buckets to log file
    {
        auto const size = s_->lf.size(ec);
        if(ec)
            return;
        bulk_writer<File> w{s_->lf, size, logWriteSize_};
        for(auto const e : c0)
        {
            // Log Record
            auto os = w.prepare(
                field<std::uint64_t>::size +    // Index
                e.second.actual_size(), ec);    // Bucket
            if(ec)
                return;
            // Log Record
            write<std::uint64_t>(os, e.first);  // Index
            e.second.write(os);                 // Bucket
        }
        c0.clear();
        w.flush(ec);
        if(ec)
            return;
        s_->lf.sync(ec);
        if(ec)
            return;
    }
    g_.finish();
    // Write new buckets to key file
    for(auto const e : s_->c1)
    {
        e.second.write(s_->kf,
           (e.first + 1) * s_->kh.block_size, ec);
        if(ec)
            return;
    }
    // Finalize the commit
    s_->df.sync(ec);
    if(ec)
        return;
    s_->kf.sync(ec);
    if(ec)
        return;
    s_->lf.trunc(0, ec);
    if(ec)
        return;
    s_->lf.sync(ec);
    if(ec)
        return;
    // Cache is no longer needed, all fetches will go straight
    // to disk again. Do this after the sync, otherwise readers
    // might get blocked longer due to the extra I/O.
    m.lock();
    s_->c1.clear();
}

template<class Hasher, class File>
void
basic_store<Hasher, File>::
run()
{
    using namespace std::chrono;
    using namespace detail;

#if NUDB_DEBUG_LOG
    beast::unit_test::dstream dout{std::cout};
#endif
    for(;;)
    {
        unique_lock_type m{m_};
        if(! s_->p1.empty())
        {
            std::size_t work;
            commit(m, work, ec_);
            if(ec_)
            {
                ecb_.store(true);
                return;
            }
            BOOST_ASSERT(m.owns_lock());
            auto const now = clock_type::now();
            auto const elapsed = duration_cast<duration<float>>(
                now > s_->when ? now - s_->when : clock_type::duration{1});
            s_->rate = static_cast<std::size_t>(
                std::ceil(work / elapsed.count()));
        #if NUDB_DEBUG_LOG
            dout <<
                "work=" << work <<
                ", time=" << elapsed.count() <<
                ", rate=" << s_->rate <<
                "\n";
        #endif
        }
        s_->p1.periodic_activity();

        cv_.wait_until(m, s_->when + seconds{1},
            [this]{ return ! open_; });
        if(! open_)
            break;
        s_->when = clock_type::now();
    }
    {
        unique_lock_type m{m_};
        std::size_t work;
        if(! s_->p1.empty())
            commit(m, work, ec_);
    }
    if(ec_)
    {
        ecb_.store(true);
        return;
    }
}

} // nudb

#endif
