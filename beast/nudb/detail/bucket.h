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

#ifndef BEAST_NUDB_BUCKET_H_INCLUDED
#define BEAST_NUDB_BUCKET_H_INCLUDED

#include <beast/nudb/error.h>
#include <beast/nudb/detail/bulkio.h>
#include <beast/nudb/detail/config.h>
#include <beast/nudb/detail/field.h>
#include <beast/nudb/detail/format.h>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace beast {
namespace nudb {
namespace detail {

// Key, hash, and bucket calculations:

// Returns the hash of a key given the salt
//
template <class Hasher>
inline
typename Hasher::result_type
hash (void const* key,
    std::size_t key_size, std::size_t salt)
{
    Hasher h (salt);
    h.append (key, key_size);
    return static_cast<
        typename Hasher::result_type>(h);
}

// Returns bucket index given hash, buckets, and modulus
//
inline
std::size_t
bucket_index (std::size_t h,
    std::size_t buckets, std::size_t modulus)
{
    std::size_t n = h % modulus;
    if (n >= buckets)
        n -= modulus / 2;
    return n;
}

// Returns the bucket index of a key
//
template <class Hasher>
inline
std::size_t
bucket_index (void const* key, std::size_t key_size,
    std::size_t salt, std::size_t buckets,
        std::size_t modulus)
{
    return bucket_index (hash<Hasher>
        (key, key_size, salt), buckets, modulus);
}

// Returns the bucket index of a key
// given the key file header
template <class Hasher>
inline
std::size_t
bucket_index (void const* key, key_file_header const& kh)
{
    return bucket_index<Hasher>(key, kh.key_size,
        kh.salt, kh.buckets, kh.modulus);
}

//------------------------------------------------------------------------------

// Tag for constructing empty buckets
struct empty_t { };
static empty_t empty;

// Allows inspection and manipulation of bucket blobs in memory
template <class = void>
class bucket_t
{
private:
    std::size_t key_size_;      // Size of key in bytes
    std::size_t block_size_;    // Size of a key file block
    std::size_t count_;         // Current key count
    std::size_t spill_;         // Offset of next spill record or 0
    std::uint8_t* p_;           // Pointer to the bucket blob

public:
    struct value_type
    {
        std::size_t offset;
        std::size_t size;
        void const* key;
    };

    bucket_t (bucket_t const&) = default;
    bucket_t& operator= (bucket_t const&) = default;

    bucket_t (std::size_t key_size,
        std::size_t block_size, void* p);

    bucket_t (std::size_t key_size,
        std::size_t block_size, void* p, empty_t);

    std::size_t
    key_size() const
    {
        return key_size_;
    }

    std::size_t
    block_size() const
    {
        return block_size_;
    }

    std::size_t
    compact_size() const
    {
        return detail::compact_size(
            key_size_, count_);
    }

    bool
    empty() const
    {
        return count_ == 0;
    }

    bool
    full() const
    {
        return count_ >= detail::bucket_capacity(
            key_size_, block_size_);
    }

    std::size_t
    size() const
    {
        return count_;
    }

    // Returns offset of next spill record or 0
    std::size_t
    spill() const
    {
        return spill_;
    }

    // Clear contents of the bucket
    void
    clear();

    // Set offset of next spill record
    void
    spill (std::size_t offset);

    // Returns the record for a key
    // entry without bounds checking.
    //
    value_type const
    at (std::size_t i) const;

    value_type const
    operator[] (std::size_t i) const
    {
        return at(i);
    }

    std::pair<value_type, bool>
    find (void const* key) const;

    void
    insert (std::size_t offset,
        std::size_t size, void const* key);

    // Erase an element by index
    //
    void
    erase (std::size_t i);

    // Read a full bucket from the
    // file at the specified offset.
    //
    template <class File>
    void
    read (File& f, std::size_t offset);

    // Read a compact bucket
    //
    template <class File>
    void
    read (bulk_reader<File>& r);

    // Write a compact bucket to the stream.
    // This only writes entries that are not empty.
    //
    void
    write (ostream& os) const;

    // Write a bucket to the file at the specified offset.
    // The full block_size() bytes are written.
    //
    template <class File>
    void
    write (File& f, std::size_t offset) const;

private:
    // Update size and spill in the blob
    void
    update();

    std::pair<std::size_t, bool>
    lower_bound (void const* key) const;
};

//------------------------------------------------------------------------------

template <class _>
bucket_t<_>::bucket_t (std::size_t key_size,
        std::size_t block_size, void* p)
    : key_size_ (key_size)
    , block_size_ (block_size)
    , p_ (reinterpret_cast<std::uint8_t*>(p))
{
    // Bucket Record
    istream is(p_, block_size);
    detail::read<uint16_t>(is, count_); // Count
    detail::read<uint48_t>(is, spill_); // Spill
}

template <class _>
bucket_t<_>::bucket_t (std::size_t key_size,
        std::size_t block_size, void* p, empty_t)
    : key_size_ (key_size)
    , block_size_ (block_size)
    , count_ (0)
    , spill_ (0)
    , p_ (reinterpret_cast<std::uint8_t*>(p))
{
    update();
}

template <class _>
void
bucket_t<_>::clear()
{
    count_ = 0;
    spill_ = 0;
    update();
}

template <class _>
void
bucket_t<_>::spill (std::size_t offset)
{
    spill_ = offset;
    update();
}

template <class _>
auto
bucket_t<_>::at (std::size_t i) const ->
    value_type const
{
    value_type result;
    // Bucket Entry
    std::size_t const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        key_size_;                      // Key
    // Bucket Record
    detail::istream is(p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        i * w, w);
    // Bucket Entry
    detail::read<uint48_t>(
        is, result.offset);             // Offset
    detail::read<uint48_t>(
        is, result.size);               // Size
    result.key = is.data(key_size_);    // Key
    return result;
}

template <class _>
auto
bucket_t<_>::find (void const* key) const ->
    std::pair<value_type, bool>
{
    std::pair<value_type, bool> result;
    std::size_t i;
    std::tie(i, result.second) = lower_bound(key);
    if (result.second)
        result.first = at(i);
    return result;
}

template <class _>
void
bucket_t<_>::insert (std::size_t offset,
    std::size_t size, void const* key)
{
    bool found;
    std::size_t i;
    std::tie(i, found) = lower_bound(key);
    (void)found;
    assert(! found);
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size;          // Spill
    // Bucket Entry
    std::size_t const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        key_size_;                      // Key
    std::memmove (
        p + (i + 1)  * w,
        p +  i       * w,
        (count_ - i) * w);
    count_++;
    update();
    // Bucket Entry
    ostream os (p + i * w, w);
    detail::write<uint48_t>(os, offset);    // Offset
    detail::write<uint48_t>(os, size);      // Size
    std::memcpy (os.data(key_size_),
        key, key_size_);                    // Key
}

template <class _>
void
bucket_t<_>::erase (std::size_t i)
{
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size;          // Spill
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        key_size_;                      // Key
    --count_;
    if (i != count_)
        std::memmove(
            p +  i       * w,
            p + (i + 1)  * w,
            (count_ - i) * w);
    update();
}

template <class _>
template <class File>
void
bucket_t<_>::read (File& f, std::size_t offset)
{
    auto const cap = bucket_capacity (
        key_size_, block_size_);
    // Excludes padding to block size
    f.read (offset, p_, bucket_size(
        key_size_, bucket_capacity(
            key_size_, block_size_)));
    istream is(p_, block_size_);
    detail::read<
        std::uint16_t>(is, count_);     // Count
    detail::read<
        uint48_t>(is, spill_);          // Spill
    if (count_ > cap)
        throw store_corrupt_error(
            "bad bucket size");
}

template <class _>
template <class File>
void
bucket_t<_>::read (bulk_reader<File>& r)
{
    // Bucket Record (compact)
    auto is = r.prepare(
        detail::field<std::uint16_t>::size +
        detail::field<uint48_t>::size);
    detail::read<
        std::uint16_t>(is, count_);     // Count
    detail::read<uint48_t>(is, spill_); // Spill
    update();
    // Excludes empty bucket entries
    auto const w = count_ * (
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        key_size_);                     // Key
    is = r.prepare (w);
    std::memcpy(p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size,          // Spill
        is.data(w), w);                 // Entries
}

template <class _>
void
bucket_t<_>::write (ostream& os) const
{
    // Does not pad up to the block size. This
    // is called to write to the data file.
    auto const size = compact_size();
    // Bucket Record
    std::memcpy (os.data(size), p_, size);
}

template <class _>
template <class File>
void
bucket_t<_>::write (File& f, std::size_t offset) const
{
    // Includes zero pad up to the block
    // size, to make the key file size always
    // a multiple of the block size.
    auto const size = compact_size();
    std::memset (p_ + size, 0,
        block_size_ - size);
    // Bucket Record
    f.write (offset, p_, block_size_);
}

template <class _>
void
bucket_t<_>::update()
{
    // Bucket Record
    ostream os(p_, block_size_);
    detail::write<
        std::uint16_t>(os, count_); // Count
    detail::write<
        uint48_t>(os, spill_);      // Spill
}

// bool is true if key matches index
template <class _>
std::pair<std::size_t, bool>
bucket_t<_>::lower_bound (
    void const* key) const
{
    // Bucket Entry
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        key_size_;                      // Key
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        // Bucket Entry
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size;          // Size
    std::size_t step;
    std::size_t first = 0;
    std::size_t count = count_;
    while (count > 0)
    {
        step = count / 2;
        auto const i = first + step;
        auto const c = std::memcmp (
            p + i * w, key, key_size_);
        if (c < 0)
        {
            first = i + 1;
            count -= step + 1;
        }
        else if (c > 0)
        {
            count = step;
        }
        else
        {
            return std::make_pair (i, true);
        }
    }
    return std::make_pair (first, false);
}
using bucket = bucket_t<>;

} // detail
} // nudb
} // beast

#endif
