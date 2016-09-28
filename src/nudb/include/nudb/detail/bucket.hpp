//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_BUCKET_HPP
#define NUDB_DETAIL_BUCKET_HPP

#include <nudb/error.hpp>
#include <nudb/type_traits.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/field.hpp>
#include <nudb/detail/format.hpp>
#include <boost/assert.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nudb {
namespace detail {

// Returns bucket index given hash, buckets, and modulus
//
inline
nbuck_t
bucket_index(nhash_t h, nbuck_t buckets, std::uint64_t modulus)
{
    BOOST_ASSERT(modulus <= 0x100000000ULL);
    auto n = h % modulus;
    if(n >= buckets)
        n -= modulus / 2;
    return static_cast<nbuck_t>(n);
}

//------------------------------------------------------------------------------

// Tag for constructing empty buckets
struct empty_t
{
    constexpr empty_t() = default;
};

static empty_t constexpr empty{};

// Allows inspection and manipulation of bucket blobs in memory
template<class = void>
class bucket_t
{
    nsize_t block_size_;    // Size of a key file block
    nkey_t size_;           // Current key count
    noff_t spill_;          // Offset of next spill record or 0
    std::uint8_t* p_;       // Pointer to the bucket blob

public:
    struct value_type
    {
        noff_t offset;
        nhash_t hash;
        nsize_t size;
    };

    bucket_t() = default;
    bucket_t(bucket_t const&) = default;
    bucket_t& operator=(bucket_t const&) = default;

    bucket_t(nsize_t block_size, void* p);

    bucket_t(nsize_t block_size, void* p, empty_t);

    nsize_t
    block_size() const
    {
        return block_size_;
    }

    // Serialized bucket size.
    // Excludes empty 
    nsize_t
    actual_size() const
    {
        return bucket_size(size_);
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

    nkey_t
    size() const
    {
        return size_;
    }

    // Returns offset of next spill record or 0
    //
    noff_t
    spill() const
    {
        return spill_;
    }

    // Set offset of next spill record
    //
    void
    spill(noff_t offset);

    // Clear contents of the bucket
    //
    void
    clear();

    // Returns the record for a key
    // entry without bounds checking.
    //
    value_type const
    at(nkey_t i) const;

    value_type const
    operator[](nkey_t i) const
    {
        return at(i);
    }

    // Returns index of entry with prefix
    // equal to or greater than the given prefix.
    //
    nkey_t
    lower_bound(nhash_t h) const;

    void
    insert(noff_t offset, nsize_t size, nhash_t h);

    // Erase an element by index
    //
    void
    erase(nkey_t i);

    // Read a full bucket from the
    // file at the specified offset.
    //
    template<class File>
    void
    read(File& f, noff_t, error_code& ec);

    // Read a compact bucket
    //
    template<class File>
    void
    read(bulk_reader<File>& r, error_code& ec);

    // Write a compact bucket to the stream.
    // This only writes entries that are not empty.
    //
    void
    write(ostream& os) const;

    // Write a bucket to the file at the specified offset.
    // The full block_size() bytes are written.
    //
    template<class File>
    void
    write(File& f,noff_t offset, error_code& ec) const;

private:
    // Update size and spill in the blob
    void
    update();
};

//------------------------------------------------------------------------------

template<class _>
bucket_t<_>::
bucket_t(nsize_t block_size, void* p)
    : block_size_(block_size)
    , p_(reinterpret_cast<std::uint8_t*>(p))
{
    // Bucket Record
    istream is(p_, block_size);
    detail::read<uint16_t>(is, size_);  // Count
    detail::read<uint48_t>(is, spill_); // Spill
}

template<class _>
bucket_t<_>::
bucket_t(nsize_t block_size, void* p, empty_t)
    : block_size_(block_size)
    , size_(0)
    , spill_(0)
    , p_(reinterpret_cast<std::uint8_t*>(p))
{
    clear();
}

template<class _>
void
bucket_t<_>::
spill(noff_t offset)
{
    spill_ = offset;
    update();
}

template<class _>
void
bucket_t<_>::clear()
{
    size_ = 0;
    spill_ = 0;
    std::memset(p_, 0, block_size_);
}

template<class _>
auto
bucket_t<_>::
at(nkey_t i) const ->
    value_type const
{
    value_type result;
    // Bucket Entry
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<f_hash>::size;            // Prefix
    // Bucket Record
    detail::istream is{p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        i * w, w};
    // Bucket Entry
    detail::read<uint48_t>(
        is, result.offset);             // Offset
    detail::read_size48(
        is, result.size);               // Size
    detail::read<f_hash>(
        is, result.hash);               // Hash
    return result;
}

template<class _>
nkey_t
bucket_t<_>::
lower_bound(nhash_t h) const
{
    // Bucket Entry
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<f_hash>::size;            // Hash
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size +         // Spill
        // Bucket Entry
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size;          // Size
    nkey_t step;
    nkey_t first = 0;
    nkey_t count = size_;
    while(count > 0)
    {
        step = count / 2;
        nkey_t i = first + step;
        nhash_t h1;
        readp<f_hash>(p + i * w, h1);
        if(h1 < h)
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

template<class _>
void
bucket_t<_>::
insert(
    noff_t offset, nsize_t size, nhash_t h)
{
    auto const i = lower_bound(h);
    // Bucket Record
    auto const p = p_ +
        field<
            std::uint16_t>::size +  // Count
        field<uint48_t>::size;      // Spill
    // Bucket Entry
    auto const w =
        field<uint48_t>::size +     // Offset
        field<uint48_t>::size +     // Size
        field<f_hash>::size;        // Hash
    std::memmove(
        p +(i + 1)  * w,
        p + i       * w,
        (size_ - i) * w);
    ++size_;
    update();
    // Bucket Entry
    ostream os{p + i * w, w};
    detail::write<uint48_t>(
        os, offset);                // Offset
    detail::write<uint48_t>(
        os, size);                  // Size
    detail::write<f_hash>(
        os, h);                     // Prefix
}

template<class _>
void
bucket_t<_>::
erase(nkey_t i)
{
    // Bucket Record
    auto const p = p_ +
        field<std::uint16_t>::size +    // Count
        field<uint48_t>::size;          // Spill
    auto const w =
        field<uint48_t>::size +         // Offset
        field<uint48_t>::size +         // Size
        field<f_hash>::size;            // Hash
    --size_;
    if(i < size_)
        std::memmove(
            p +  i      * w,
            p +(i + 1) * w,
           (size_ - i) * w);
    std::memset(p + size_ * w, 0, w);
    update();
}

template<class _>
template<class File>
void
bucket_t<_>::
read(File& f, noff_t offset, error_code& ec)
{
    auto const cap = bucket_capacity(block_size_);
    // Excludes padding to block size
    f.read(offset, p_, bucket_size(cap), ec);
    if(ec)
        return;
    istream is{p_, block_size_};
    detail::read<std::uint16_t>(is, size_); // Count
    detail::read<uint48_t>(is, spill_);     // Spill
    if(size_ > cap)
    {
        ec = error::invalid_bucket_size;
        return;
    }
}

template<class _>
template<class File>
void
bucket_t<_>::
read(bulk_reader<File>& r, error_code& ec)
{
    // Bucket Record(compact)
    auto is = r.prepare(
        detail::field<std::uint16_t>::size +
        detail::field<uint48_t>::size, ec);
    if(ec)
        return;
    detail::read<std::uint16_t>(is, size_); // Count
    detail::read<uint48_t>(is, spill_);     // Spill
    update();
    // Excludes empty bucket entries
    auto const w = size_ * (
        field<uint48_t>::size +             // Offset
        field<uint48_t>::size +             // Size
        field<f_hash>::size);               // Hash
    is = r.prepare(w, ec);
    if(ec)
        return;
    std::memcpy(p_ +
        field<std::uint16_t>::size +        // Count
        field<uint48_t>::size,              // Spill
        is.data(w), w);                     // Entries
}

template<class _>
void
bucket_t<_>::
write(ostream& os) const
{
    // Does not pad up to the block size. This
    // is called to write to the data file.
    auto const size = actual_size();
    // Bucket Record
    std::memcpy(os.data(size), p_, size);
}

template<class _>
template<class File>
void
bucket_t<_>::
write(File& f, noff_t offset, error_code& ec) const
{
    // Includes zero pad up to the block
    // size, to make the key file size always
    // a multiple of the block size.
    auto const size = actual_size();
    std::memset(p_ + size, 0, block_size_ - size);
    // Bucket Record
    f.write(offset, p_, block_size_, ec);
    if(ec)
        return;
}

template<class _>
void
bucket_t<_>::
update()
{
    // Bucket Record
    ostream os{p_, block_size_};
    detail::write<std::uint16_t>(os, size_);    // Count
    detail::write<uint48_t>(os, spill_);        // Spill
}

using bucket = bucket_t<>;

//------------------------------------------------------------------------------

//  Spill bucket if full.
//  The bucket is cleared after it spills.
//
template<class File>
void
maybe_spill(
    bucket& b, bulk_writer<File>& w, error_code& ec)
{
    if(b.full())
    {
        // Spill Record
        auto const offset = w.offset();
        auto os = w.prepare(
            field<uint48_t>::size + // Zero
            field<uint16_t>::size + // Size
            b.actual_size(), ec);
        if(ec)
            return;
        write<uint48_t>(os, 0ULL);  // Zero
        write<std::uint16_t>(
            os, b.actual_size());   // Size
        auto const spill =
            offset + os.size();
        b.write(os);                // Bucket
        // Update bucket
        b.clear();
        b.spill(spill);
    }
}

} // detail
} // nudb

#endif
