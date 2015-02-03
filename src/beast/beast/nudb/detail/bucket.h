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

#ifndef BEAST_NUDB_DETAIL_BUCKET_H_INCLUDED
#define BEAST_NUDB_DETAIL_BUCKET_H_INCLUDED

#include <beast/nudb/common.h>
#include <beast/nudb/detail/bulkio.h>
#include <beast/nudb/detail/field.h>
#include <beast/nudb/detail/format.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace beast {
namespace nudb {
namespace detail {

// bucket calculations:

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

//------------------------------------------------------------------------------

// Tag for constructing empty buckets
struct empty_t { };
static empty_t empty;

// Allows inspection and manipulation of bucket blobs in memory
template <class = void>
class bucket_t
{
private:
    std::size_t block_size_;    // Size of a key file block
    std::size_t size_;          // Current key count
    std::size_t spill_;         // Offset of next spill record or 0
    std::uint8_t* p_;           // Pointer to the bucket blob

public:
    struct value_type
    {
        std::size_t offset;
        std::size_t size;
        std::size_t hash;
    };

    bucket_t (bucket_t const&) = default;
    bucket_t& operator= (bucket_t const&) = default;

    bucket_t (std::size_t block_size, void* p);

    bucket_t (std::size_t block_size, void* p, empty_t);

    std::size_t
    block_size() const
    {
        return block_size_;
    }

    std::size_t
    compact_size() const
    {
        return detail::bucket_size(size_);
    }

    bool
    empty() const
    {
        return size_ == 0;
    }

    bool
    full() const
    {
        return size_ >=
            detail::bucket_capacity(block_size_);
    }

    std::size_t
    size() const
    {
        return size_;
    }

    // Returns offset of next spill record or 0
    //
    std::size_t
    spill() const
    {
        return spill_;
    }

    // Set offset of next spill record
    //
    void
    spill (std::size_t offset);

    // Clear contents of the bucket
    //
    void
    clear();

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

    // Returns index of entry with prefix
    // equal to or greater than the given prefix.
    //
    std::size_t
    lower_bound (std::size_t h) const;

    void
    insert (std::size_t offset,
        std::size_t size, std::size_t h);

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
};

//------------------------------------------------------------------------------

template <class _>
bucket_t<_>::bucket_t (
        std::size_t block_size, void* p)
    : block_size_ (block_size)
    , p_ (reinterpret_cast<std::uint8_t*>(p))
{
    // Bucket Record
    istream is(p_, block_size);
    detail::read<uint16_t>(is, size_);  // Count
    detail::read<uint48_t>(is, spill_); // Spill
}

template <class _>
bucket_t<_>::bucket_t (
        std::size_t block_size, void* p, empty_t)
    : block_size_ (block_size)
    , size_ (0)
    , spill_ (0)
    , p_ (reinterpret_cast<std::uint8_t*>(p))
{
    clear();
}

template <class _>
void
bucket_t<_>::spill (std::size_t offset)
{
    spill_ = offset;
    update();
}

template <class _>
void
bucket_t<_>::clear()
{
    size_ = 0;
    spill_ = 0;
    std::memset(p_, 0, block_size_);
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
        field<hash_t>::size;            // Prefix
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
    detail::read<hash_t>(
        is, result.hash);               // Hash
    return result;
}

template <class _>
std::size_t
bucket_t<_>::lower_bound (
    std::size_t h) const
{
    // Bucket Entry
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<hash_t>::size;            // Hash
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        // Bucket Entry
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size;          // Size
    std::size_t step;
    std::size_t first = 0;
    std::size_t count = size_;
    while (count > 0)
    {
        step = count / 2;
        auto const i = first + step;
        std::size_t h1;
        readp<hash_t>(p + i * w, h1);
        if (h1 < h)
        {
            first = i + 1;
            count -= step + 1;
        }
        else
        {
            count = step;
        }
    }
    return first;
}

template <class _>
void
bucket_t<_>::insert (std::size_t offset,
    std::size_t size, std::size_t h)
{
    std::size_t i = lower_bound(h);
    // Bucket Record
    auto const p = p_ +
        field<
            std::uint16_t>::size +  // Count
        field<uint48_t>::size;      // Spill
    // Bucket Entry
    std::size_t const w =
        field<uint48_t>::size +     // Offset
        field<uint48_t>::size +     // Size
        field<hash_t>::size;        // Hash
    std::memmove (
        p + (i + 1)  * w,
        p +  i       * w,
        (size_ - i) * w);
    size_++;
    update();
    // Bucket Entry
    ostream os (p + i * w, w);
    detail::write<uint48_t>(
        os, offset);                // Offset
    detail::write<uint48_t>(
        os, size);                  // Size
    detail::write<hash_t>(
        os, h);                     // Prefix
}

template <class _>
void
bucket_t<_>::erase (std::size_t i)
{
    // Bucket Record
    auto const p = p_ +
        field<
            std::uint16_t>::size +  // Count
        field<uint48_t>::size;      // Spill
    auto const w =
        field<uint48_t>::size +     // Offset
        field<uint48_t>::size +     // Size
        field<hash_t>::size;        // Hash
    --size_;
    if (i < size_)
        std::memmove(
            p +  i      * w,
            p + (i + 1) * w,
            (size_ - i) * w);
    std::memset(p + size_ * w, 0, w);
    update();
}

template <class _>
template <class File>
void
bucket_t<_>::read (File& f, std::size_t offset)
{
    auto const cap = bucket_capacity (
        block_size_);
    // Excludes padding to block size
    f.read (offset, p_, bucket_size(cap));
    istream is(p_, block_size_);
    detail::read<
        std::uint16_t>(is, size_);     // Count
    detail::read<
        uint48_t>(is, spill_);          // Spill
    if (size_ > cap)
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
        std::uint16_t>(is, size_);  // Count
    detail::read<uint48_t>(
        is, spill_);                // Spill
    update();
    // Excludes empty bucket entries
    auto const w = size_ * (
        field<uint48_t>::size +     // Offset
        field<uint48_t>::size +     // Size
        field<hash_t>::size);       // Hash
    is = r.prepare (w);
    std::memcpy(p_ +
        field<
            std::uint16_t>::size +  // Count
        field<uint48_t>::size,      // Spill
        is.data(w), w);             // Entries
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
        std::uint16_t>(os, size_);  // Count
    detail::write<
        uint48_t>(os, spill_);      // Spill
}

using bucket = bucket_t<>;

//  Spill bucket if full.
//  The bucket is cleared after it spills.
//
template <class File>
void
maybe_spill(bucket& b, bulk_writer<File>& w)
{
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

} // detail
} // nudb
} // beast

#endif
