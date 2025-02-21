//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STBLOB_H_INCLUDED
#define RIPPLE_PROTOCOL_STBLOB_H_INCLUDED

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/STBase.h>

#include <cstring>
#include <memory>

namespace ripple {

// variable length byte string
class STBlob : public STBase, public CountedObject<STBlob>
{
    Buffer value_;

public:
    using value_type = Slice;

    STBlob() = default;
    STBlob(STBlob const& rhs);

    STBlob(SField const& f, void const* data, std::size_t size);
    STBlob(SField const& f, Buffer&& b);
    STBlob(SField const& n);
    STBlob(SerialIter&, SField const& name = sfGeneric);

    std::size_t
    size() const;

    std::uint8_t const*
    data() const;

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    STBlob&
    operator=(Slice const& slice);

    value_type
    value() const noexcept;

    STBlob&
    operator=(Buffer&& buffer);

    void
    setValue(Buffer&& b);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

inline STBlob::STBlob(STBlob const& rhs)
    : STBase(rhs), CountedObject<STBlob>(rhs), value_(rhs.data(), rhs.size())
{
}

inline STBlob::STBlob(SField const& f, void const* data, std::size_t size)
    : STBase(f), value_(data, size)
{
}

inline STBlob::STBlob(SField const& f, Buffer&& b)
    : STBase(f), value_(std::move(b))
{
}

inline STBlob::STBlob(SField const& n) : STBase(n)
{
}

inline std::size_t
STBlob::size() const
{
    return value_.size();
}

inline std::uint8_t const*
STBlob::data() const
{
    return reinterpret_cast<std::uint8_t const*>(value_.data());
}

inline STBlob&
STBlob::operator=(Slice const& slice)
{
    value_ = Buffer(slice.data(), slice.size());
    return *this;
}

inline STBlob::value_type
STBlob::value() const noexcept
{
    return value_;
}

inline STBlob&
STBlob::operator=(Buffer&& buffer)
{
    value_ = std::move(buffer);
    return *this;
}

inline void
STBlob::setValue(Buffer&& b)
{
    value_ = std::move(b);
}

}  // namespace ripple

#endif
