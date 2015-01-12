//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_STORE_H_INCLUDED
#define BEAST_NUDB_STORE_H_INCLUDED

#include <beast/streams/debug_ostream.h>

#include <beast/nudb/error.h>
#include <beast/nudb/file.h>
#include <beast/nudb/mode.h>
#include <beast/nudb/recover.h>
#include <beast/nudb/detail/bucket.h>
#include <beast/nudb/detail/buffers.h>
#include <beast/nudb/detail/bulkio.h>
#include <beast/nudb/detail/cache.h>
#include <beast/nudb/detail/config.h>
#include <beast/nudb/detail/format.h>
#include <beast/nudb/detail/gentex.h>
#include <beast/nudb/detail/pool.h>
#include <beast/nudb/detail/posix_file.h>
#include <beast/nudb/detail/win32_file.h>
#include <boost/thread/lock_types.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <beast/cxx14/memory.h> // <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if DOXYGEN
#include <beast/nudb/README.md>
#endif

#ifndef BEAST_NUDB_DEBUG_CHECKS
# ifndef NDEBUG
#  define BEAST_NUDB_DEBUG_CHECKS 0
# else
#  define BEAST_NUDB_DEBUG_CHECKS 0
# endif
#endif

namespace beast {
namespace nudb {

namespace detail {

// Holds state variables of the open database.
template <class File>
struct state
{
    File df;
    File kf;
    File lf;
    path_type dp;
    path_type kp;
    path_type lp;
    buffers b;
    pool p0;
    pool p1;
    cache c0;
    cache c1;
    key_file_header const kh;

    // pool commit high water mark
    std::size_t pool_thresh = 0;

    state (state const&) = delete;
    state& operator= (state const&) = delete;

    state (File&& df_, File&& kf_, File&& lf_,
        path_type const& dp_, path_type const& kp_,
            path_type const& lp_, key_file_header const& kh_,
                std::size_t arena_alloc_size);
};

template <class File>
state<File>::state (
    File&& df_, File&& kf_, File&& lf_,
    path_type const& dp_, path_type const& kp_,
        path_type const& lp_,  key_file_header const& kh_,
            std::size_t arena_alloc_size)
    : df (std::move(df_))
    , kf (std::move(kf_))
    , lf (std::move(lf_))
    , dp (dp_)
    , kp (kp_)
    , lp (lp_)
    , b  (kh_.block_size)
    , p0 (kh_.key_size, arena_alloc_size)
    , p1 (kh_.key_size, arena_alloc_size)
    , c0 (kh_.key_size, kh_.block_size)
    , c1 (kh_.key_size, kh_.block_size)
    , kh (kh_)
{
}

} // detail

/*

    TODO
    
    - fingerprint / checksum on log records

    - size field at end of data records
        allows walking backwards

    - timestamp every so often on data records
        allows knowing the age of the data

*/

/** A simple key/value database
    @tparam File The type of File object to use.
    @tparam Hash The hash function to use on key
*/
template <class Hasher, class File>
class basic_store
{
public:
    using file_type = File;
    using hash_type = Hasher;

private:
    // requires 64-bit integers or better
    static_assert(sizeof(std::size_t)>=8, "");

    enum
    {
        // Size of bulk writes
        bulk_write_size     = 16 * 1024 * 1024,

        // Size of bulk reads during recover
        recover_read_size   = 16 * 1024 * 1024
    };

    using clock_type =
        std::chrono::steady_clock;
    using shared_lock_type =
        boost::shared_lock<boost::shared_mutex>;
    using unique_lock_type =
        boost::unique_lock<boost::shared_mutex>;
    using blockbuf =
        typename detail::buffers::value_type;

    bool open_ = false;
    // VFALCO Make consistency checks optional?
    //bool safe_ = true;              // Do consistency checks
    // VFALCO Unfortunately boost::optional doesn't support
    //        move construction so we use unique_ptr instead.
    std::unique_ptr <
        detail::state<File>> s_;    // State of an open database

    std::size_t frac_;              // accumulates load
    std::size_t thresh_;            // split threshold
    std::size_t buckets_;           // number of buckets
    std::size_t modulus_;           // hash modulus

    std::mutex u_;                  // serializes insert()
    detail::gentex g_;
    boost::shared_mutex m_;
    std::thread thread_;
    std::condition_variable_any cond_;

    std::atomic<bool> epb_;         // `true` when ep_ set
    std::exception_ptr ep_;

public:
    basic_store() = default;
    basic_store (basic_store const&) = delete;
    basic_store& operator= (basic_store const&) = delete;

    /** Destroy the database.

        Files are closed, memory is freed, and data that has not been
        committed is discarded. To ensure that all inserted data is
        written, it is necessary to call close() before destroying the
        store.

        This function catches all exceptions thrown by callees, so it
        will be necessary to call close() before destroying the store
        if callers want to catch exceptions.

        Throws:
            None
    */
    ~basic_store();

    /** Returns `true` if the database is open. */
    bool
    is_open() const
    {
        return open_;
    }

    path_type const&
    dat_path() const
    {
        return s_->dp;
    }

    path_type const&
    key_path() const
    {
        return s_->kp;
    }

    path_type const&
    log_path() const
    {
        return s_->lp;
    }

    std::uint64_t
    appnum() const
    {
        return s_->kh.appnum;
    }

    /** Close the database.

        All data is committed before closing.

        Throws:
            store_error
    */
    void
    close();

    /** Open a database.

        @param args Arguments passed to File constructors
        @return `true` if each file could be opened
    */
    template <class... Args>
    bool
    open (
        path_type const& dat_path,
        path_type const& key_path,
        path_type const& log_path,
        std::size_t arena_alloc_size,
        Args&&... args);

    /** Fetch a value.

        If key is found, BufferFactory will be called as:
            `(void*)()(std::size_t bytes)`

        where bytes is the size of the value, and the returned pointer
        points to a buffer of at least bytes size.

        @return `true` if the key exists.
    */
    template <class BufferFactory>
    bool
    fetch (void const* key, BufferFactory&& bf);

    /** Insert a value.

        Returns:
            `true` if the key was inserted,
            `false` if the key already existed
    */
    bool
    insert (void const* key, void const* data,
        std::size_t bytes);

private:
    void
    rethrow()
    {
        if (epb_.load())
            std::rethrow_exception(ep_);
    }

    std::pair <detail::bucket::value_type, bool>
    find (void const* key, detail::bucket& b);

    void
    maybe_spill (detail::bucket& b,
        detail::bulk_writer<File>& w);

    void
    split (detail::bucket& b1, detail::bucket& b2,
        detail::bucket& tmp, std::size_t n1, std::size_t n2,
            std::size_t buckets, std::size_t modulus,
                detail::bulk_writer<File>& w);

    void
    check (std::size_t n, detail::bucket& b,
        std::size_t buckets, std::size_t modulus);

    detail::bucket
    load (std::size_t n, detail::cache& c1,
        detail::cache& c0, void* buf);

    bool check();

    void
    commit();

    void
    run();
};

//------------------------------------------------------------------------------

template <class Hasher, class File>
basic_store<Hasher, File>::~basic_store()
{
    try
    {
        close();
    }
    catch (...)
    {
        // If callers want to see the exceptions
        // they have to call close manually.
    }
}

template <class Hasher, class File>
template <class... Args>
bool
basic_store<Hasher, File>::open (
    path_type const& dat_path,
    path_type const& key_path,
    path_type const& log_path,
    std::size_t arena_alloc_size,
    Args&&... args)
{
    using namespace detail;
    if (is_open())
        throw std::logic_error("nudb: already open");
    epb_.store(false);
    recover (dat_path, key_path, log_path,
        recover_read_size);
    File df(std::forward<Args>(args)...);
    File kf(std::forward<Args>(args)...);
    File lf(std::forward<Args>(args)...);
    if (! df.open (file_mode::append, dat_path))
        return false;
    if (! kf.open (file_mode::write, key_path))
        return false;
    if (! lf.create (file_mode::append, log_path))
        return false;
    dat_file_header dh;
    key_file_header kh;
    read (df, dh);
    read (kf, kh);
    verify (dh);
    verify<Hasher> (kh);
    verify<Hasher> (dh, kh);
    auto s = std::make_unique<state<File>>(
        std::move(df), std::move(kf), std::move(lf),
            dat_path, key_path, log_path, kh,
                arena_alloc_size);
    thresh_ = std::max<std::size_t>(65536UL,
        kh.load_factor * kh.capacity);
    frac_ = thresh_ / 2;
    buckets_ = kh.buckets;
    modulus_ = ceil_pow2(kh.buckets);
    // VFALCO TODO This could be better
    if (buckets_ < 1)
        throw store_corrupt_error (
            "bad key file length");
    s_ = std::move(s);
    open_ = true;
    thread_ = std::thread(
        &basic_store::run, this);
    return true;
}

template <class Hasher, class File>
void
basic_store<Hasher, File>::close()
{
    if (open_)
    {
        // Set this first otherwise a
        // throw can cause another close().
        open_ = false;
        cond_.notify_all();
        thread_.join();
        rethrow();
        s_->lf.close();
        File::erase(s_->lp);
        s_.reset();
    }
}

template <class Hasher, class File>
template <class BufferFactory>
bool
basic_store<Hasher, File>::fetch (
    void const* key, BufferFactory&& bf)
{
    using namespace detail;
    rethrow();
    std::size_t offset;
    std::size_t size;
    blockbuf buf(s_->b);
    bucket tmp (s_->kh.key_size,
        s_->kh.block_size, buf.get());
    {
        auto const h = hash<Hasher>(
           key, s_->kh.key_size, s_->kh.salt);
        shared_lock_type m (m_,
            boost::defer_lock);
        m.lock();
        {
            typename pool::iterator iter;
            iter = s_->p1.find(key);
            if (iter != s_->p1.end())
            {
                void* const b = bf(
                    iter->first.size);
                if (b == nullptr)
                    return false;
                std::memcpy (b,
                    iter->first.data,
                        iter->first.size);
                return true;
            }
            iter = s_->p0.find(key);
            if (iter != s_->p0.end())
            {
                void* const b = bf(
                    iter->first.size);
                if (b == nullptr)
                    return false;
                std::memcpy (b,
                    iter->first.data,
                        iter->first.size);
                return true;
            }
        }
        auto const n = bucket_index(
            h, buckets_, modulus_);
        auto const iter = s_->c1.find(n);
        if (iter != s_->c1.end())
        {
            auto const result =
                iter->second.find(key);
            if (result.second)
            {
                offset = result.first.offset;
                size = result.first.size;
                goto found;
            }
            // VFALCO Audit for concurrency
            auto spill = iter->second.spill();
            m.unlock();
            while (spill)
            {
                tmp.read(s_->df, spill);
                auto const result = tmp.find(key);
                if (result.second)
                {
                    offset = result.first.offset;
                    size = result.first.size;
                    goto found;
                }
                spill = tmp.spill();
            }
            return false;
        }
        // VFALCO Audit for concurrency
        genlock <gentex> g (g_);
        m.unlock();
        tmp.read (s_->kf,
            (n + 1) * tmp.block_size());
        auto const result = find(key, tmp);
        if (! result.second)
            return false;
        offset = result.first.offset;
        size = result.first.size;
    }
found:
    void* const b = bf(size);
    if (b == nullptr)
        return false;
    // Data Record
    s_->df.read (offset +
        field<uint48_t>::size + // Size
        s_->kh.key_size,        // Key
            b, size);
    return true;
}

template <class Hasher, class File>
bool
basic_store<Hasher, File>::insert (void const* key,
    void const* data, std::size_t size)
{
    using namespace detail;
    rethrow();
#if ! BEAST_NUDB_NO_DOMAIN_CHECK
    if (size > field<uint48_t>::max)
        throw std::logic_error(
            "nudb: size too large");
#endif
    blockbuf buf (s_->b);
    bucket tmp (s_->kh.key_size,
        s_->kh.block_size, buf.get());
    auto const h = hash<Hasher>(
        key, s_->kh.key_size, s_->kh.salt);
    std::lock_guard<std::mutex> u (u_);
    shared_lock_type m (m_, boost::defer_lock);
    m.lock();
    if (s_->p1.find(key) != s_->p1.end())
        return false;
    if (s_->p0.find(key) != s_->p0.end())
        return false;
    auto const n = bucket_index(
        h, buckets_, modulus_);
    auto const iter = s_->c1.find(n);
    if (iter != s_->c1.end())
    {
        if (iter->second.find(key).second)
            return false;
        // VFALCO Audit for concurrency
        auto spill = iter->second.spill();
        m.unlock();
        while (spill)
        {
            tmp.read (s_->df, spill);
            if (tmp.find(key).second)
                return false;
            spill = tmp.spill();
        }
    }
    else
    {
        genlock <gentex> g (g_);
        m.unlock();
        // VFALCO Audit for concurrency
        tmp.read (s_->kf,
            (n + 1) * s_->kh.block_size);
        if (find(key, tmp).second)
            return false;
    }
    {
        unique_lock_type m (m_);
        s_->p1.insert (h, key, data, size);
        bool const full =
            s_->p1.data_size() >= s_->pool_thresh;
        m.unlock();
        if (full)
            cond_.notify_all();
    }
    return true;
}

//  Find key in loaded bucket b or its spills.
//
template <class Hasher, class File>
std::pair <detail::bucket::value_type, bool>
basic_store<Hasher, File>::find (
    void const* key, detail::bucket& b)
{
    auto result = b.find(key);
    if (result.second)
        return result;
    auto spill = b.spill();
    while (spill)
    {
        b.read (s_->df, spill);
        result = b.find(key);
        if (result.second)
            return result;
        spill = b.spill();
    }
    return result;
}

//  Spill bucket if full
//
template <class Hasher, class File>
void
basic_store<Hasher, File>::maybe_spill(
    detail::bucket& b, detail::bulk_writer<File>& w)
{
    using namespace detail;
    if (b.full())
    {
        // Spill Record
        auto const offset = w.offset();
        auto os = w.prepare(
            field<uint48_t>::size + // Zero
            field<uint16_t>::size + // Size
            b.compact_size());
        write <uint48_t> (os, 0);   // Zero
        write <std::uint16_t> (
            os, b.compact_size());  // Size
        auto const spill =
            offset + os.size();
        b.write (os);               // Bucket
        // Update bucket
        b.clear();
        b.spill (spill);
    }
}

//  Split the bucket in b1 to b2
//  b1 must be loaded
//  tmp is used as a temporary buffer
//  splits are written but not the new buckets
//
template <class Hasher, class File>
void
basic_store<Hasher, File>::split (detail::bucket& b1,
    detail::bucket& b2, detail::bucket& tmp,
        std::size_t n1, std::size_t n2,
            std::size_t buckets, std::size_t modulus,
                detail::bulk_writer<File>& w)
{
    using namespace detail;
    // Trivial case: split empty bucket
    if (b1.empty())
        return;
    // Split
    for (std::size_t i = 0; i < b1.size();)
    {
        auto e = b1[i];
        auto const h = hash<Hasher>(
            e.key, s_->kh.key_size, s_->kh.salt);
        auto const n = bucket_index(
            h, buckets, modulus);
        assert(n==n1 || n==n2);
        if (n == n2)
        {
            b2.insert (e.offset, e.size, e.key);
            b1.erase (i);
        }
        else
        {
            ++i;
        }
    }
    std::size_t spill = b1.spill();
    if (spill)
    {
        b1.spill (0);
        do
        {
            // If any part of the spill record is
            // in the write buffer then flush first
            // VFALCO Needs audit
            if (spill + bucket_size(s_->kh.key_size,
                    s_->kh.capacity) > w.offset() - w.size())
                w.flush();
            tmp.read (s_->df, spill);
            for (std::size_t i = 0; i < tmp.size(); ++i)
            {
                auto e = tmp[i];
                auto const n = bucket_index<Hasher>(
                    e.key, s_->kh.key_size, s_->kh.salt,
                        buckets, modulus);
                assert(n==n1 || n==n2);
                if (n == n2)
                {
                    maybe_spill (b2, w);
                    b2.insert (e.offset, e.size, e.key);
                }
                else
                {
                    maybe_spill (b1, w);
                    b1.insert (e.offset, e.size, e.key);
                }
            }
            spill = tmp.spill();
        }
        while (spill);
    }
}

//  Effects:
//
//      Returns a bucket from caches or the key file
//
//      If the bucket is found in c1, returns the
//          bucket from c1.
//      Else if the bucket number is greater than buckets(),
//          throws.
//      Else, If the bucket is found in c2, inserts the
//          bucket into c1 and returns the bucket from c1.
//      Else, reads the bucket from the key file, inserts
//          the bucket into c0 and c1, and returns
//          the bucket from c1.
//
//  Preconditions:
//      buf points to a buffer of at least block_size() bytes
//
//  Postconditions:
//      c1, and c0, and the memory pointed to by buf may be modified
//
template <class Hasher, class File>
detail::bucket
basic_store<Hasher, File>::load (
    std::size_t n, detail::cache& c1,
        detail::cache& c0, void* buf)
{
    using namespace detail;
    auto iter = c1.find(n);
    if (iter != c1.end())
        return iter->second;
#if BEAST_NUDB_DEBUG_CHECKS
    if (n >= buckets_)
        throw std::logic_error(
            "nudb: missing bucket in cache");
#endif
    iter = c0.find(n);
    if (iter != c0.end())
        return c1.insert (n,
            iter->second)->second;
    bucket tmp (s_->kh.key_size,
        s_->kh.block_size, buf);
    tmp.read (s_->kf, (n + 1) *
        s_->kh.block_size);
    c0.insert (n, tmp);
    return c1.insert (n, tmp)->second;
}

template <class Hasher, class File>
void
basic_store<Hasher, File>::check (
    std::size_t n, detail::bucket& b,
        std::size_t buckets, std::size_t modulus)
{
    using namespace detail;
    for (std::size_t i = 0; i < b.size(); ++i)
    {
        auto const e = b[i];
        auto const h = hash<Hasher>(
            e.key, s_->kh.key_size, s_->kh.salt);
        auto const n1 = bucket_index(
            h, buckets, modulus);
        assert(n1 == n);
    }
}

//  Commit the memory pool to disk, then sync.
//
//  Preconditions:
//
//  Effects:
//
template <class Hasher, class File>
void
basic_store<Hasher, File>::commit()
{
    using namespace detail;
    blockbuf buf1 (s_->b);
    blockbuf buf2 (s_->b);
    bucket tmp (s_->kh.key_size,
        s_->kh.block_size, buf1.get());
    // Empty cache put in place temporarily
    // so we can reuse the memory from s_->c1
    cache c1;
    {
        unique_lock_type m (m_);
        if (s_->p1.empty())
            return;
        swap (s_->c1, c1);
        swap (s_->p0, s_->p1);
        s_->pool_thresh = std::max(
            s_->pool_thresh, s_->p0.data_size());
        m.unlock();
    }
    // Prepare rollback information
    // Log File Header
    log_file_header lh;
    lh.version = currentVersion;    // Version
    lh.appnum = s_->kh.appnum;      // Appnum
    lh.salt = s_->kh.salt;          // Salt
    lh.pepper = pepper<Hasher>(
        lh.salt);                   // Pepper
    lh.key_size = s_->kh.key_size;  // Key Size
    lh.key_file_size =
        s_->kf.actual_size();       // Key File Size
    lh.dat_file_size =
        s_->df.actual_size();       // Data File Size
    write (s_->lf, lh);
    s_->lf.sync();
    // Append data and spills to data file
    auto modulus = modulus_;
    auto buckets = buckets_;
    {
        // Bulk write to avoid write amplification
        bulk_writer<File> w (s_->df,
            s_->df.actual_size(), bulk_write_size);
        // Write inserted data to the data file
        for (auto& e : s_->p0)
        {
        #if BEAST_NUDB_DEBUG_CHECKS
            assert (e.first.hash == hash<Hasher>(
               e.first.key, s_->kh.key_size, s_->kh.salt));
        #endif
            // VFALCO This could be UB since other
            // threads are reading other data members
            // of this object in memory
            e.second = w.offset();
            auto os = w.prepare (data_size(
                e.first.size, s_->kh.key_size));
            // Data Record
            write <uint48_t> (os,
                e.first.size);          // Size
            write (os, e.first.key,
                s_->kh.key_size);       // Key
            write (os, e.first.data,
                e.first.size);          // Data
        }
        // Do inserts, splits, and build view
        // of original and modified buckets
        for (auto const e : s_->p0)
        {
        #if BEAST_NUDB_DEBUG_CHECKS
            assert (e.first.hash == hash<Hasher>(
                e.first.key, s_->kh.key_size, s_->kh.salt));
        #endif
            // VFALCO Should this be >= or > ?
            if ((frac_ += 65536) >= thresh_)
            {
                // split
                frac_ -= thresh_;
                if (buckets == modulus)
                    modulus *= 2;
                auto const n1 = buckets - (modulus / 2);
                auto const n2 = buckets++;
                auto b1 = load (n1, c1, s_->c0, buf2.get());
            #if BEAST_NUDB_DEBUG_CHECKS
                check(n1, b1, buckets, modulus);
            #endif
                auto b2 = c1.create (n2);
                // If split spills, the writer is
                // flushed which can amplify writes.
                split (b1, b2, tmp, n1, n2,
                    buckets, modulus, w);
            #if BEAST_NUDB_DEBUG_CHECKS
                check(n1, b1, buckets, modulus);
                check(n2, b2, buckets, modulus);
            #endif
            }
            // insert
            auto const n = bucket_index(
                e.first.hash, buckets, modulus);
            auto b = load (n, c1, s_->c0, buf2.get());
            // This can amplify writes if it spills.
        #if BEAST_NUDB_DEBUG_CHECKS
            check(n, b, buckets, modulus);
        #endif
            maybe_spill (b, w);
        #if BEAST_NUDB_DEBUG_CHECKS
            check(n, b, buckets, modulus);
        #endif
            b.insert (e.second, e.first.size, e.first.key);
        #if BEAST_NUDB_DEBUG_CHECKS
            check(n, b, buckets, modulus);
        #endif
        }
        w.flush();
    }
    // Give readers a view of the new buckets.
    // This might be slightly better than the old
    // view since there could be fewer spills.
    {
        unique_lock_type m (m_);
        swap(c1, s_->c1);
        s_->p0.clear();
        buckets_ = buckets;
        modulus_ = modulus;
    }
    // Write clean buckets to log file
    // VFALCO Should the bulk_writer buffer size be tunable?
    {
        bulk_writer<File> w(s_->lf,
            s_->lf.actual_size(), bulk_write_size);
        for (auto const e : s_->c0)
        {
            // Log Record
            auto os = w.prepare(
                field<std::uint64_t>::size +    // Index
                e.second.compact_size());       // Bucket
            // Log Record
            write<std::uint64_t>(os, e.first);  // Index
            e.second.write(os);                 // Bucket
        }
        s_->c0.clear();
        w.flush();
        s_->lf.sync();
    }
    // VFALCO Audit for concurrency
    {
        std::lock_guard<gentex> g (g_);
        // Write new buckets to key file
        for (auto const e : s_->c1)
            e.second.write (s_->kf,
                (e.first + 1) * s_->kh.block_size);
    }
    // Finalize the commit
    s_->df.sync();
    s_->kf.sync();
    s_->lf.trunc(0);
    s_->lf.sync();
    // Cache is no longer needed, all fetches will go straight
    // to disk again. Do this after the sync, otherwise readers
    // might get blocked longer due to the extra I/O.
    // VFALCO is this correct?
    {
        unique_lock_type m (m_);
        s_->c1.clear();
    }
}

template <class Hasher, class File>
void
basic_store<Hasher, File>::run()
{
    try
    {
        while (open_)
        {
            auto when = clock_type::now() +
                std::chrono::seconds(1);
            for(;;)
            {
                unique_lock_type m (m_);
                bool const timeout =
                    cond_.wait_until (m, when) ==
                        std::cv_status::timeout;
                if (! open_)
                    break;
                if (timeout ||
                    s_->p1.data_size() >=
                        s_->pool_thresh)
                {
                    m.unlock();
                    commit();
                }
                // Reclaim some memory if
                // we get a spare moment.
                if (timeout)
                {
                    m.lock();
                    s_->pool_thresh /= 2;
                    s_->p1.shrink_to_fit();
                    s_->p0.shrink_to_fit();
                    s_->c1.shrink_to_fit();
                    s_->c0.shrink_to_fit();
                    m.unlock();
                    when = clock_type::now() +
                        std::chrono::seconds(1);
                }
            }
        }
        commit();
    }
    catch(...)
    {
        ep_ = std::current_exception(); // must come first
        epb_.store(true);
    }
}

//------------------------------------------------------------------------------

using store = basic_store <default_hash, native_file>;

/** Generate a random salt. */
template <class = void>
std::uint64_t
make_salt()
{
    std::random_device rng;
    std::mt19937_64 gen {rng()};
    std::uniform_int_distribution <std::size_t> dist;
    return dist(gen);
}

/** Returns the best guess at the volume's block size. */
inline
std::size_t
block_size(std::string const& /*path*/)
{
    return 4096;
}

} // nudb
} // beast

#endif
